package adapter.core;

import android.os.Binder;
import android.os.DeadObjectException;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.TransactionTooLargeException;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Android binder calls between two Westlake processes, over a Unix stream socket (ADR-0001). The
 * kernel driver can't be used: policy lets no app process register anywhere a peer could find it.
 *
 * A Parcel crosses as its bytes plus a table of what its objects were. On arrival the objects are
 * written back at the same offsets, so the receiver's readStrongBinder and readFileDescriptor work
 * unchanged and AIDL code on both sides needs no change.
 *
 * What it keeps from the driver:
 * - An object sent back to its owner arrives as the original object, and a remote object is always
 *   the same proxy, so identity comparisons and callback maps work.
 * - A call that calls back into its caller runs on the thread waiting for the reply, so a lock that
 *   thread holds doesn't deadlock.
 * - Oneway calls to one object run in order, one at a time.
 * - A peer's end fails calls with DeadObjectException and fires death recipients.
 * - Over about 1 MB fails with TransactionTooLargeException, as the driver's buffer does.
 *
 * A frame is an int length, then HEADER_SIZE bytes of header, the object table and the data. A peer
 * is not trusted: malformed frames end the channel.
 */
public final class SocketBinder {
    private static final int TYPE_CALL = 1;
    private static final int TYPE_REPLY = 2;
    private static final int TYPE_RELEASE = 3;

    private static final int KIND_SENDER_OBJECT = 1;
    private static final int KIND_RECEIVER_OBJECT = 2;
    private static final int KIND_FD = 3;

    private static final int STATUS_OK = 0;
    private static final int STATUS_NO_TARGET = 1;
    private static final int STATUS_FAILED = 2;

    private static final int HEADER_SIZE = 40;
    private static final int TABLE_ENTRY_SIZE = 13;
    private static final int FLAT_OBJECT_SIZE = 24;
    private static final int MAX_DATA = 1024 * 1024 - 8 * 1024;
    private static final int MAX_OBJECTS = 16 * 1024;
    private static final int MAX_FDS = 64;
    private static final int MAX_FRAME = HEADER_SIZE + MAX_OBJECTS * TABLE_ENTRY_SIZE + MAX_DATA;

    private static final long ROOT_ID = 1;

    /** The call a thread is serving, innermost first, for routing callbacks and caller identity. */
    private static final ThreadLocal<Inbound> sInbound = new ThreadLocal<>();

    private static final class Inbound {
        final SocketBinder channel;
        final long txn;
        final Inbound outer;

        Inbound(SocketBinder channel, long txn, Inbound outer) {
            this.channel = channel;
            this.txn = txn;
            this.outer = outer;
        }
    }

    /** An exported object and how many references the peer holds. */
    private static final class Export {
        final IBinder binder;
        long references;
        final boolean pinned;

        Export(IBinder binder, boolean pinned) {
            this.binder = binder;
            this.pinned = pinned;
        }
    }

    private static final class Frame {
        int type;
        long txn;
        long parent;
        long target;
        int code;
        int flags;
        int[] offsets;
        int[] kinds;
        long[] ids;
        byte[] data;
        int dataOffset;
        int dataLength;
        List<ParcelFileDescriptor> fds;

        void closeFds() {
            if (fds == null) return;
            for (ParcelFileDescriptor fd : fds) {
                closeQuietly(fd);
            }
            fds = null;
        }
    }

    /** A thread waiting for its reply, which may serve callbacks from the peer while it waits. */
    private static final class Waiter {
        final LinkedBlockingQueue<Object> queue = new LinkedBlockingQueue<>();
    }

    private static final Object DEAD = new Object();

    private final int mFd;
    private final String mPeerName;
    private final AtomicLong mNextTxn = new AtomicLong(1);
    private final AtomicLong mNextExport = new AtomicLong(ROOT_ID + 1);
    private final Object mWriteLock = new Object();
    private final Object mTableLock = new Object();
    private final Map<Long, Export> mExports = new HashMap<>();
    private final IdentityHashMap<IBinder, Long> mExportIds = new IdentityHashMap<>();
    private final Map<Long, WeakReference<Proxy>> mProxies = new HashMap<>();
    private final ConcurrentHashMap<Long, Waiter> mPending = new ConcurrentHashMap<>();
    private final Map<Long, ArrayDeque<Frame>> mOneway = new HashMap<>();
    private final List<DeathLink> mDeathLinks = new ArrayList<>();
    private final ExecutorService mPool;
    private volatile boolean mDead;
    private volatile int mCallerUid;
    private volatile int mCallerPid;

    private static final class DeathLink {
        final Proxy proxy;
        final IBinder.DeathRecipient recipient;

        DeathLink(Proxy proxy, IBinder.DeathRecipient recipient) {
            this.proxy = proxy;
            this.recipient = recipient;
        }
    }

    /**
     * Starts a channel on a connected socket, which it then owns. root is the object the peer gets
     * from getRemoteRoot; it may be null. peerName identifies the peer to callingPeer().
     */
    public SocketBinder(int fd, IBinder root, String peerName) {
        mFd = fd;
        mPeerName = peerName;
        mPool = Executors.newCachedThreadPool(runnable -> {
            Thread thread = new Thread(runnable, "SocketBinder-" + peerName);
            thread.setDaemon(true);
            return thread;
        });
        if (root != null) {
            Export export = new Export(root, true);
            mExports.put(ROOT_ID, export);
            mExportIds.put(root, ROOT_ID);
        }
        Thread reader = new Thread(this::readLoop, "SocketBinderReader-" + peerName);
        reader.setDaemon(true);
        reader.start();
    }

    /** The object the peer passed as its root. */
    public IBinder getRemoteRoot() {
        return proxyFor(ROOT_ID, 0);
    }

    public boolean isAlive() {
        return !mDead;
    }

    /** Ends the channel; the peer sees this process's end as a death. */
    public void shutdown() {
        SocketBinderNative.shutdown(mFd);
    }

    /** Opens a named socket for peers to connect to, for tests until appspawn-x makes channels. */
    public static int listen(String name) throws IOException {
        return SocketBinderNative.listen(name);
    }

    public static int accept(int listener) throws IOException {
        return SocketBinderNative.accept(listener);
    }

    public static int connect(String name) throws IOException {
        return SocketBinderNative.connect(name);
    }

    /** Who Binder.getCallingUid and getCallingPid say is calling while this channel's calls are served. */
    public void setCaller(int uid, int pid) {
        mCallerPid = pid;
        mCallerUid = uid;
    }

    /** Makes Binder's calling identity answer from setCaller. Once per process, in the serving process. */
    public static void installCallingIdentity() {
        SocketBinderNative.installCallingIdentity();
    }

    /** The process id of a connected socket's peer, as the kernel reports it. */
    public static int peerPid(int fd) throws IOException {
        return SocketBinderNative.peerPid(fd);
    }

    /** The peer whose call this thread is serving, or null if it is serving none. */
    public static String callingPeer() {
        Inbound inbound = sInbound.get();
        return inbound == null ? null : inbound.channel.mPeerName;
    }

    /** Stands in for an object in the peer. A Binder, so that Parcel can hold it. */
    private static final class Proxy extends Binder {
        final SocketBinder channel;
        final long id;
        private long mReferences;
        private String mDescriptor;

        Proxy(SocketBinder channel, long id) {
            this.channel = channel;
            this.id = id;
        }

        synchronized void addReferences(long count) {
            mReferences += count;
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            return channel.call(id, code, data, reply, flags);
        }

        @Override
        public String getInterfaceDescriptor() {
            synchronized (this) {
                if (mDescriptor != null) return mDescriptor;
            }
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                if (!transact(INTERFACE_TRANSACTION, data, reply, 0)) return null;
                String descriptor = reply.readString();
                synchronized (this) {
                    mDescriptor = descriptor;
                }
                return descriptor;
            } catch (RemoteException e) {
                return null;
            } finally {
                data.recycle();
                reply.recycle();
            }
        }

        @Override
        public boolean pingBinder() {
            return channel.isAlive();
        }

        @Override
        public boolean isBinderAlive() {
            return channel.isAlive();
        }

        @Override
        public void linkToDeath(DeathRecipient recipient, int flags) {
            channel.link(this, recipient);
        }

        @Override
        public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
            return channel.unlink(this, recipient);
        }

        @Override
        protected void finalize() throws Throwable {
            try {
                channel.proxyFinalized(this, mReferences);
            } finally {
                super.finalize();
            }
        }
    }

    private void link(Proxy proxy, IBinder.DeathRecipient recipient) {
        synchronized (mTableLock) {
            if (!mDead) {
                mDeathLinks.add(new DeathLink(proxy, recipient));
                return;
            }
        }
        // Binder's linkToDeath declares no exception; IBinder's callers expect this one.
        SocketBinder.<RuntimeException>sneakyThrow(new DeadObjectException());
    }

    @SuppressWarnings("unchecked")
    private static <T extends Throwable> void sneakyThrow(Throwable t) throws T {
        throw (T) t;
    }

    private boolean unlink(Proxy proxy, IBinder.DeathRecipient recipient) {
        synchronized (mTableLock) {
            for (int i = 0; i < mDeathLinks.size(); i++) {
                DeathLink link = mDeathLinks.get(i);
                if (link.proxy == proxy && link.recipient == recipient) {
                    mDeathLinks.remove(i);
                    return true;
                }
            }
        }
        return false;
    }

    private Proxy proxyFor(long id, long newReferences) {
        synchronized (mTableLock) {
            WeakReference<Proxy> known = mProxies.get(id);
            Proxy proxy = known == null ? null : known.get();
            if (proxy == null) {
                proxy = new Proxy(this, id);
                mProxies.put(id, new WeakReference<>(proxy));
            }
            proxy.addReferences(newReferences);
            return proxy;
        }
    }

    private void proxyFinalized(Proxy proxy, long references) {
        synchronized (mTableLock) {
            WeakReference<Proxy> known = mProxies.get(proxy.id);
            if (known != null && (known.get() == null || known.get() == proxy)) {
                mProxies.remove(proxy.id);
            }
        }
        if (references == 0 || mDead) return;
        Frame frame = new Frame();
        frame.type = TYPE_RELEASE;
        frame.target = proxy.id;
        frame.txn = references;
        try {
            mPool.execute(() -> {
                try {
                    write(frame, null, new byte[0]);
                } catch (IOException e) {
                    die();
                }
            });
        } catch (RuntimeException e) {
            // The pool is gone because the channel is.
        }
    }

    /** A sync call waits here for its reply, serving the peer's callbacks on this thread meanwhile. */
    private boolean call(long target, int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        boolean oneway = (flags & IBinder.FLAG_ONEWAY) != 0;
        Frame frame = new Frame();
        frame.type = TYPE_CALL;
        frame.txn = mNextTxn.getAndIncrement();
        frame.target = target;
        frame.code = code;
        frame.flags = flags;
        frame.parent = oneway ? 0 : currentInbound();
        Waiter waiter = oneway ? null : new Waiter();
        if (waiter != null) mPending.put(frame.txn, waiter);
        try {
            if (mDead) throw new DeadObjectException();
            send(frame, data);
            if (oneway) return true;
            return await(waiter, reply);
        } finally {
            if (waiter != null) mPending.remove(frame.txn);
        }
    }

    private long currentInbound() {
        for (Inbound inbound = sInbound.get(); inbound != null; inbound = inbound.outer) {
            if (inbound.channel == this) return inbound.txn;
        }
        return 0;
    }

    private boolean await(Waiter waiter, Parcel reply) throws RemoteException {
        boolean interrupted = false;
        try {
            while (true) {
                Object next;
                try {
                    next = waiter.queue.take();
                } catch (InterruptedException e) {
                    interrupted = true;
                    continue;
                }
                if (next == DEAD) throw new DeadObjectException();
                Frame frame = (Frame) next;
                if (frame.type == TYPE_CALL) {
                    serve(frame);
                    continue;
                }
                if (frame.code == STATUS_NO_TARGET) {
                    frame.closeFds();
                    throw new DeadObjectException("object no longer exists in the peer");
                }
                if (frame.code != STATUS_OK) {
                    frame.closeFds();
                    throw new RemoteException("the peer's method failed");
                }
                if (reply != null) {
                    if (!decode(frame, reply)) throw new DeadObjectException();
                } else {
                    frame.closeFds();
                }
                return frame.flags != 0;
            }
        } finally {
            if (interrupted) Thread.currentThread().interrupt();
        }
    }

    private void serve(Frame call) {
        boolean oneway = (call.flags & IBinder.FLAG_ONEWAY) != 0;
        Parcel data = Parcel.obtain();
        Parcel reply = Parcel.obtain();
        Inbound outer = sInbound.get();
        sInbound.set(new Inbound(this, call.txn, outer));
        long identity = mCallerUid != 0 ? SocketBinderNative.setCallingIdentity(mCallerUid, mCallerPid) : 0;
        Frame answer = new Frame();
        answer.type = TYPE_REPLY;
        answer.txn = call.txn;
        answer.code = STATUS_OK;
        try {
            IBinder target;
            synchronized (mTableLock) {
                Export export = mExports.get(call.target);
                target = export == null ? null : export.binder;
            }
            if (target == null) {
                call.closeFds();
                answer.code = STATUS_NO_TARGET;
            } else if (!decode(call, data)) {
                answer.code = STATUS_FAILED;
            } else {
                answer.flags = invoke(target, call, data, reply, oneway, answer) ? 1 : 0;
            }
        } finally {
            if (mCallerUid != 0) SocketBinderNative.restoreCallingIdentity(identity);
            sInbound.set(outer);
        }
        try {
            if (!oneway) sendReply(answer, reply);
        } catch (RemoteException e) {
            die();
        } finally {
            data.recycle();
            reply.recycle();
        }
    }

    /** As Binder.execTransact would: a failure the Parcel can carry goes back to the caller. */
    private static boolean invoke(IBinder target, Frame call, Parcel data, Parcel reply, boolean oneway, Frame answer) {
        try {
            return target.transact(call.code, data, reply, call.flags);
        } catch (RemoteException | RuntimeException e) {
            if (oneway) {
                System.err.println("SocketBinder: oneway call failed: " + e);
                return true;
            }
            try {
                reply.setDataSize(0);
                reply.setDataPosition(0);
                reply.writeException(e);
                return true;
            } catch (RuntimeException unwritable) {
                System.err.println("SocketBinder: call failed: " + e);
                answer.code = STATUS_FAILED;
                return false;
            }
        }
    }

    private void sendReply(Frame answer, Parcel reply) throws RemoteException {
        try {
            send(answer, reply);
        } catch (TransactionTooLargeException e) {
            reply.setDataSize(0);
            answer.code = STATUS_FAILED;
            send(answer, reply);
        }
    }

    /** Puts a Parcel on the wire as bytes and a table of its objects. */
    private void send(Frame frame, Parcel parcel) throws RemoteException {
        byte[] bytes = parcel == null ? new byte[0] : SocketBinderNative.parcelBytes(parcel);
        if (bytes.length > MAX_DATA) throw new TransactionTooLargeException();
        List<ParcelFileDescriptor> fds = new ArrayList<>();
        try {
            encodeObjects(frame, parcel, bytes, fds);
            frame.fds = fds;
            write(frame, fds, bytes);
        } catch (IOException e) {
            die();
            throw new DeadObjectException(e.getMessage());
        } finally {
            frame.fds = null;
            for (ParcelFileDescriptor fd : fds) {
                closeQuietly(fd);
            }
        }
    }

    private void encodeObjects(Frame frame, Parcel parcel, byte[] bytes, List<ParcelFileDescriptor> fds)
            throws RemoteException {
        int[] objects = parcel == null ? new int[0] : SocketBinderNative.parcelObjects(parcel);
        int count = objects.length / 2;
        if (count > MAX_OBJECTS) throw new TransactionTooLargeException();
        frame.offsets = new int[count];
        frame.kinds = new int[count];
        frame.ids = new long[count];
        if (count == 0) return;
        int position = parcel.dataPosition();
        try {
            for (int i = 0; i < count; i++) {
                int offset = objects[i * 2];
                int type = objects[i * 2 + 1];
                parcel.setDataPosition(offset);
                frame.offsets[i] = offset;
                if (type == SocketBinderNative.TYPE_FD) {
                    if (fds.size() == MAX_FDS) throw new TransactionTooLargeException();
                    ParcelFileDescriptor fd = parcel.readFileDescriptor();
                    if (fd == null) throw new RemoteException("unreadable file descriptor in Parcel");
                    fds.add(fd);
                    frame.kinds[i] = KIND_FD;
                } else if (type == SocketBinderNative.TYPE_BINDER || type == SocketBinderNative.TYPE_HANDLE) {
                    IBinder binder = parcel.readStrongBinder();
                    if (binder == null) throw new RemoteException("unreadable binder in Parcel");
                    encodeBinder(frame, i, binder);
                } else {
                    throw new RemoteException("unsupported Parcel object 0x" + Integer.toHexString(type));
                }
                // The slot held local pointers. They mean nothing to the peer and it shouldn't see them.
                if (offset + FLAT_OBJECT_SIZE > bytes.length) throw new RemoteException("Parcel object cut short");
                java.util.Arrays.fill(bytes, offset, offset + FLAT_OBJECT_SIZE, (byte) 0);
            }
        } finally {
            parcel.setDataPosition(position);
        }
    }

    private void encodeBinder(Frame frame, int index, IBinder binder) {
        if (binder instanceof Proxy && ((Proxy) binder).channel == this) {
            frame.kinds[index] = KIND_RECEIVER_OBJECT;
            frame.ids[index] = ((Proxy) binder).id;
            return;
        }
        synchronized (mTableLock) {
            Long id = mExportIds.get(binder);
            if (id == null) {
                id = mNextExport.getAndIncrement();
                mExportIds.put(binder, id);
                mExports.put(id, new Export(binder, false));
            }
            mExports.get(id).references++;
            frame.kinds[index] = KIND_SENDER_OBJECT;
            frame.ids[index] = id;
        }
    }

    /** Rebuilds a Parcel from a frame. False if the frame names an object that doesn't exist. */
    private boolean decode(Frame frame, Parcel parcel) {
        boolean ok = true;
        int nextFd = 0;
        parcel.setDataSize(0);
        parcel.setDataPosition(0);
        parcel.unmarshall(frame.data, frame.dataOffset, frame.dataLength);
        for (int i = 0; i < frame.offsets.length && ok; i++) {
            parcel.setDataPosition(frame.offsets[i]);
            switch (frame.kinds[i]) {
                case KIND_FD:
                    ParcelFileDescriptor fd = frame.fds.get(nextFd++);
                    parcel.writeFileDescriptor(fd.getFileDescriptor());
                    break;
                case KIND_SENDER_OBJECT:
                    parcel.writeStrongBinder(proxyFor(frame.ids[i], 1));
                    break;
                default:
                    IBinder own;
                    synchronized (mTableLock) {
                        Export export = mExports.get(frame.ids[i]);
                        own = export == null ? null : export.binder;
                    }
                    if (own == null) {
                        ok = false;
                    } else {
                        parcel.writeStrongBinder(own);
                    }
                    break;
            }
        }
        parcel.setDataPosition(0);
        frame.closeFds(); // Parcel holds its own copies.
        return ok;
    }

    private void write(Frame frame, List<ParcelFileDescriptor> fds, byte[] data) throws IOException {
        int objects = frame.offsets == null ? 0 : frame.offsets.length;
        int total = HEADER_SIZE + objects * TABLE_ENTRY_SIZE + data.length;
        ByteBuffer out = ByteBuffer.allocate(4 + total).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(total);
        out.putInt(frame.type);
        out.putLong(frame.txn);
        out.putLong(frame.parent);
        out.putLong(frame.target);
        out.putInt(frame.code);
        out.putInt(frame.flags);
        out.putInt(objects);
        for (int i = 0; i < objects; i++) {
            out.putInt(frame.offsets[i]);
            out.put((byte) frame.kinds[i]);
            out.putLong(frame.ids[i]);
        }
        out.put(data);
        int[] fdNumbers = null;
        if (fds != null && !fds.isEmpty()) {
            fdNumbers = new int[fds.size()];
            for (int i = 0; i < fdNumbers.length; i++) {
                fdNumbers[i] = fds.get(i).getFd();
            }
        }
        byte[] bytes = out.array();
        synchronized (mWriteLock) {
            int sent = 0;
            while (sent < bytes.length) {
                sent += SocketBinderNative.send(mFd, bytes, sent, bytes.length - sent, sent == 0 ? fdNumbers : null);
            }
        }
    }

    private void readLoop() {
        byte[] length = new byte[4];
        List<ParcelFileDescriptor> fds = new ArrayList<>();
        try {
            while (true) {
                if (!readFully(length, 4, fds, true)) break;
                int total = ByteBuffer.wrap(length).order(ByteOrder.LITTLE_ENDIAN).getInt();
                if (total < HEADER_SIZE || total > MAX_FRAME) throw new IOException("bad frame length " + total);
                byte[] body = new byte[total];
                if (!readFully(body, total, fds, false)) break;
                Frame frame = parse(body, fds);
                fds = new ArrayList<>();
                dispatch(frame);
            }
        } catch (IOException | RuntimeException e) {
            System.err.println("SocketBinder: channel to " + mPeerName + " ended: " + e);
        } finally {
            for (ParcelFileDescriptor fd : fds) {
                closeQuietly(fd);
            }
            die();
            SocketBinderNative.close(mFd);
        }
    }

    private boolean readFully(byte[] into, int length, List<ParcelFileDescriptor> fds, boolean atFrameStart)
            throws IOException {
        int[] received = new int[MAX_FDS + 1];
        int got = 0;
        while (got < length) {
            int n = SocketBinderNative.recv(mFd, into, got, length - got, received);
            for (int i = 0; i < received[0]; i++) {
                fds.add(ParcelFileDescriptor.adoptFd(received[1 + i]));
            }
            if (fds.size() > MAX_FDS) throw new IOException("too many descriptors");
            if (n == 0) {
                if (atFrameStart && got == 0) return false;
                throw new IOException("frame cut short");
            }
            got += n;
        }
        return true;
    }

    private Frame parse(byte[] body, List<ParcelFileDescriptor> fds) throws IOException {
        ByteBuffer in = ByteBuffer.wrap(body).order(ByteOrder.LITTLE_ENDIAN);
        Frame frame = new Frame();
        frame.type = in.getInt();
        frame.txn = in.getLong();
        frame.parent = in.getLong();
        frame.target = in.getLong();
        frame.code = in.getInt();
        frame.flags = in.getInt();
        int objects = in.getInt();
        if (frame.type < TYPE_CALL || frame.type > TYPE_RELEASE) throw new IOException("bad frame type");
        if (objects < 0 || objects > MAX_OBJECTS || objects > (body.length - HEADER_SIZE) / TABLE_ENTRY_SIZE) {
            throw new IOException("bad object count");
        }
        frame.offsets = new int[objects];
        frame.kinds = new int[objects];
        frame.ids = new long[objects];
        frame.dataOffset = HEADER_SIZE + objects * TABLE_ENTRY_SIZE;
        frame.dataLength = body.length - frame.dataOffset;
        frame.data = body;
        int expectedFds = 0;
        int end = 0;
        for (int i = 0; i < objects; i++) {
            int offset = in.getInt();
            int kind = in.get();
            long id = in.getLong();
            if (offset < end || offset % 4 != 0 || offset > frame.dataLength - FLAT_OBJECT_SIZE) {
                throw new IOException("bad object offset");
            }
            if (kind == KIND_FD) {
                expectedFds++;
            } else if (kind != KIND_SENDER_OBJECT && kind != KIND_RECEIVER_OBJECT) {
                throw new IOException("bad object kind");
            }
            end = offset + FLAT_OBJECT_SIZE;
            frame.offsets[i] = offset;
            frame.kinds[i] = kind;
            frame.ids[i] = id;
        }
        if (expectedFds != fds.size()) {
            frame.fds = fds;
            frame.closeFds();
            throw new IOException("descriptor count doesn't match the object table");
        }
        frame.fds = fds;
        return frame;
    }

    private void dispatch(Frame frame) {
        switch (frame.type) {
            case TYPE_RELEASE:
                release(frame.target, frame.txn);
                return;
            case TYPE_REPLY:
                Waiter waiter = mPending.get(frame.txn);
                if (waiter == null) {
                    frame.closeFds();
                } else {
                    waiter.queue.add(frame);
                }
                return;
            default:
                dispatchCall(frame);
        }
    }

    private void dispatchCall(Frame frame) {
        if ((frame.flags & IBinder.FLAG_ONEWAY) != 0) {
            enqueueOneway(frame);
            return;
        }
        Waiter parent = frame.parent == 0 ? null : mPending.get(frame.parent);
        if (parent != null) {
            parent.queue.add(frame);
        } else {
            mPool.execute(() -> serve(frame));
        }
    }

    private void enqueueOneway(Frame frame) {
        boolean start = false;
        synchronized (mOneway) {
            ArrayDeque<Frame> queue = mOneway.get(frame.target);
            if (queue == null) {
                queue = new ArrayDeque<>();
                mOneway.put(frame.target, queue);
                start = true;
            }
            queue.add(frame);
        }
        if (start) mPool.execute(() -> drainOneway(frame.target));
    }

    private void drainOneway(long target) {
        while (true) {
            Frame next;
            synchronized (mOneway) {
                ArrayDeque<Frame> queue = mOneway.get(target);
                next = queue.poll();
                if (next == null) {
                    mOneway.remove(target);
                    return;
                }
            }
            serve(next);
        }
    }

    private void release(long id, long count) {
        synchronized (mTableLock) {
            Export export = mExports.get(id);
            if (export == null) return;
            export.references -= count;
            if (export.references <= 0 && !export.pinned) {
                mExports.remove(id);
                mExportIds.remove(export.binder);
            }
        }
    }

    private void die() {
        List<DeathLink> links;
        synchronized (mTableLock) {
            if (mDead) return;
            mDead = true;
            links = new ArrayList<>(mDeathLinks);
            mDeathLinks.clear();
            mExports.clear();
            mExportIds.clear();
        }
        SocketBinderNative.shutdown(mFd);
        for (Waiter waiter : mPending.values()) {
            waiter.queue.add(DEAD);
        }
        for (DeathLink link : links) {
            mPool.execute(() -> link.recipient.binderDied(link.proxy));
        }
        mPool.shutdown();
    }

    private static void closeQuietly(ParcelFileDescriptor fd) {
        try {
            fd.close();
        } catch (IOException e) {
            // Nothing can be done about a descriptor that won't close.
        }
    }
}
