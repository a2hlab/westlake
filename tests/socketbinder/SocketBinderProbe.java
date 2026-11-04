package org.westlake.socketbinderprobe;

import adapter.core.SocketBinder;
import android.os.Binder;
import android.os.Bundle;
import android.os.DeadObjectException;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.os.TransactionTooLargeException;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;

/**
 * ADR-0001 milestone 2: Android binder objects, callbacks and file descriptors between two
 * processes over SocketBinder. Runs on any runtime with android.os and libbinder.
 *   server <name>             serves until killed
 *   client <name>             runs every check against a server, then asks it to exit
 */
public final class SocketBinderProbe {
    static final int ECHO = 1;
    static final int MAKE_OBJECT = 2;
    static final int IS_SAME_OBJECT = 3;
    static final int CALL_BACK = 4;
    static final int WRITE_TO_FD = 5;
    static final int GIVE_FD = 6;
    static final int THROW = 7;
    static final int BUNDLE = 8;
    static final int RECORD = 9;
    static final int RECORDED = 10;
    static final int EXIT = 11;
    static final int LARGE_REPLY = 12;
    static final int MADE_OBJECT_ALIVE = 13;
    static final int CALLBACK_CODE = 100;
    static final int MADE_CODE = 200;
    static final String DESCRIPTOR = "westlake.test.Probe";

    static void writeAll(ParcelFileDescriptor fd, byte[] bytes) throws Exception {
        try (FileOutputStream out = new FileOutputStream(fd.getFileDescriptor())) {
            out.write(bytes);
        }
    }

    static String readAll(ParcelFileDescriptor fd) throws Exception {
        try (FileInputStream in = new FileInputStream(fd.getFileDescriptor())) {
            byte[] buffer = new byte[256];
            int n = in.read(buffer);
            return n <= 0 ? "" : new String(buffer, 0, n, "UTF-8");
        }
    }

    /** The service, written as AIDL's generated Stub would be. */
    static final class Service extends Binder {
        private IBinder mMade;
        private java.lang.ref.WeakReference<IBinder> mMadeWeak;
        private final List<Integer> mRecorded = new ArrayList<>();

        Service() {
            attachInterface(null, DESCRIPTOR);
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == INTERFACE_TRANSACTION) {
                reply.writeString(DESCRIPTOR);
                return true;
            }
            data.enforceInterface(DESCRIPTOR);
            try {
                switch (code) {
                    case ECHO: {
                        reply.writeNoException();
                        reply.writeString(data.readString());
                        reply.writeByteArray(data.createByteArray());
                        return true;
                    }
                    case MAKE_OBJECT: {
                        mMade = new Binder() {
                            @Override
                            protected boolean onTransact(int c, Parcel d, Parcel r, int f) {
                                r.writeInt(c + 1);
                                return true;
                            }
                        };
                        mMadeWeak = new java.lang.ref.WeakReference<>(mMade);
                        reply.writeNoException();
                        reply.writeStrongBinder(mMade);
                        mMade = null; // Only the channel keeps it alive now.
                        return true;
                    }
                    case IS_SAME_OBJECT: {
                        IBinder back = data.readStrongBinder();
                        reply.writeNoException();
                        reply.writeInt(back == mMadeWeak.get() ? 1 : 0);
                        return true;
                    }
                    case MADE_OBJECT_ALIVE: {
                        Runtime.getRuntime().gc();
                        System.runFinalization();
                        Thread.sleep(200);
                        Runtime.getRuntime().gc();
                        reply.writeNoException();
                        reply.writeInt(mMadeWeak.get() != null ? 1 : 0);
                        return true;
                    }
                    case CALL_BACK: {
                        IBinder callback = data.readStrongBinder();
                        int value = data.readInt();
                        Parcel d = Parcel.obtain();
                        Parcel r = Parcel.obtain();
                        d.writeInt(value);
                        callback.transact(CALLBACK_CODE, d, r, 0);
                        int answer = r.readInt();
                        d.recycle();
                        r.recycle();
                        reply.writeNoException();
                        reply.writeInt(answer);
                        return true;
                    }
                    case WRITE_TO_FD: {
                        ParcelFileDescriptor fd = data.readFileDescriptor();
                        String text = data.readString();
                        writeAll(fd, text.getBytes("UTF-8"));
                        reply.writeNoException();
                        return true;
                    }
                    case GIVE_FD: {
                        ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
                        writeAll(pipe[1], data.readString().getBytes("UTF-8"));
                        reply.writeNoException();
                        reply.writeInt(1);
                        pipe[0].writeToParcel(reply, 0);
                        pipe[0].close();
                        return true;
                    }
                    case THROW: {
                        throw new IllegalArgumentException(data.readString());
                    }
                    case BUNDLE: {
                        Bundle bundle = data.readBundle(Service.class.getClassLoader());
                        IBinder inner = bundle.getBinder("callback");
                        ParcelFileDescriptor fd = bundle.getParcelable("fd");
                        writeAll(fd, bundle.getString("text").getBytes("UTF-8"));
                        Parcel d = Parcel.obtain();
                        Parcel r = Parcel.obtain();
                        d.writeInt(5);
                        inner.transact(CALLBACK_CODE, d, r, 0);
                        int answer = r.readInt();
                        d.recycle();
                        r.recycle();
                        Bundle out = new Bundle();
                        out.putInt("answer", answer);
                        out.putBinder("service", this);
                        reply.writeNoException();
                        reply.writeBundle(out);
                        return true;
                    }
                    case RECORD: {
                        synchronized (mRecorded) {
                            mRecorded.add(data.readInt());
                        }
                        Thread.sleep(data.dataAvail() == 0 ? 0 : 1);
                        return true;
                    }
                    case RECORDED: {
                        reply.writeNoException();
                        synchronized (mRecorded) {
                            int[] all = new int[mRecorded.size()];
                            for (int i = 0; i < all.length; i++) {
                                all[i] = mRecorded.get(i);
                            }
                            reply.writeIntArray(all);
                        }
                        return true;
                    }
                    case LARGE_REPLY: {
                        reply.writeNoException();
                        reply.writeByteArray(new byte[2 * 1024 * 1024]);
                        return true;
                    }
                    case EXIT: {
                        Runtime.getRuntime().halt(0);
                        return true;
                    }
                    default:
                        return super.onTransact(code, data, reply, flags);
                }
            } catch (RemoteException | RuntimeException e) {
                throw e;
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }
    }

    private static int sFailures;

    static void check(String name, boolean ok, String detail) {
        System.out.println((ok ? "PASS " : "FAIL ") + name + (detail == null ? "" : " (" + detail + ")"));
        if (!ok) sFailures++;
    }

    interface Check {
        void run() throws Exception;
    }

    static void run(String name, Check check) {
        try {
            check.run();
        } catch (Throwable t) {
            check(name, false, "threw " + t);
        }
    }

    static Parcel request() {
        Parcel data = Parcel.obtain();
        data.writeInterfaceToken(DESCRIPTOR);
        return data;
    }

    static void runChecks(IBinder service) throws Exception {
        run("descriptor", () -> check("descriptor", DESCRIPTOR.equals(service.getInterfaceDescriptor()), null));
        run("no local interface", () -> check("no local interface", service.queryLocalInterface(DESCRIPTOR) == null, null));
        run("alive", () -> check("alive", service.isBinderAlive() && service.pingBinder(), null));

        run("echo", () -> {
            Parcel d = request();
            Parcel r = Parcel.obtain();
            byte[] big = new byte[200 * 1024];
            for (int i = 0; i < big.length; i++) big[i] = (byte) (i * 7);
            d.writeString("hello 你好");
            d.writeByteArray(big);
            boolean returned = service.transact(ECHO, d, r, 0);
            r.readException();
            String text = r.readString();
            byte[] back = r.createByteArray();
            check("echo 200 KB", returned && "hello 你好".equals(text) && java.util.Arrays.equals(big, back), null);
            d.recycle();
            r.recycle();
        });

        run("binder", () -> {
            Parcel d = request();
            Parcel r = Parcel.obtain();
            service.transact(MAKE_OBJECT, d, r, 0);
            r.readException();
            IBinder made = r.readStrongBinder();
            Parcel d2 = Parcel.obtain();
            Parcel r2 = Parcel.obtain();
            made.transact(MADE_CODE, d2, r2, 0);
            check("binder returned and callable", r2.readInt() == MADE_CODE + 1, null);
            Parcel d3 = request();
            Parcel r3 = Parcel.obtain();
            d3.writeStrongBinder(made);
            service.transact(IS_SAME_OBJECT, d3, r3, 0);
            r3.readException();
            check("binder sent back is the original object", r3.readInt() == 1, null);
            d.recycle(); r.recycle(); d2.recycle(); r2.recycle(); d3.recycle(); r3.recycle();
        });

        run("release", () -> {
            for (int i = 0; i < 5; i++) {
                Runtime.getRuntime().gc();
                System.runFinalization();
                Thread.sleep(200);
            }
            Parcel d = request();
            Parcel r = Parcel.obtain();
            service.transact(MADE_OBJECT_ALIVE, d, r, 0);
            r.readException();
            check("dropped binder is released in the peer", r.readInt() == 0, null);
            d.recycle(); r.recycle();
        });

        run("callback", () -> {
            ReentrantLock lock = new ReentrantLock();
            Thread caller = Thread.currentThread();
            boolean[] sameThread = {false};
            boolean[] lockHeld = {false};
            Binder callback = new Binder() {
                @Override
                protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                    sameThread[0] = Thread.currentThread() == caller;
                    lockHeld[0] = lock.tryLock();
                    if (lockHeld[0]) lock.unlock();
                    reply.writeInt(data.readInt() * 2);
                    return true;
                }
            };
            Parcel d = request();
            Parcel r = Parcel.obtain();
            d.writeStrongBinder(callback);
            d.writeInt(21);
            lock.lock();
            try {
                service.transact(CALL_BACK, d, r, 0);
            } finally {
                lock.unlock();
            }
            r.readException();
            check("callback returns its answer", r.readInt() == 42, null);
            check("callback runs on the waiting thread", sameThread[0], null);
            check("callback can take the caller's lock", lockHeld[0], null);
            d.recycle(); r.recycle();
        });

        run("fd sent", () -> {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
            Parcel d = request();
            Parcel r = Parcel.obtain();
            d.writeFileDescriptor(pipe[1].getFileDescriptor());
            pipe[1].close();
            d.writeString("written by the peer");
            service.transact(WRITE_TO_FD, d, r, 0);
            r.readException();
            d.recycle(); // Closes our copy of the write end, or the read would wait.
            r.recycle();
            check("file descriptor sent", "written by the peer".equals(readAll(pipe[0])), null);
        });

        run("fd received", () -> {
            Parcel d = request();
            Parcel r = Parcel.obtain();
            d.writeString("from the peer's pipe");
            service.transact(GIVE_FD, d, r, 0);
            r.readException();
            ParcelFileDescriptor fd = r.readInt() != 0 ? ParcelFileDescriptor.CREATOR.createFromParcel(r) : null;
            check("file descriptor returned", fd != null && "from the peer's pipe".equals(readAll(fd)), null);
            d.recycle(); r.recycle();
        });

        run("exception", () -> {
            Parcel d = request();
            Parcel r = Parcel.obtain();
            d.writeString("expected text");
            service.transact(THROW, d, r, 0);
            try {
                r.readException();
                check("exception crosses", false, "none thrown");
            } catch (IllegalArgumentException e) {
                check("exception crosses", "expected text".equals(e.getMessage()), e.getMessage());
            }
            d.recycle(); r.recycle();
        });

        run("bundle", () -> {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
            Binder callback = new Binder() {
                @Override
                protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                    reply.writeInt(data.readInt() + 1);
                    return true;
                }
            };
            Bundle in = new Bundle();
            in.putBinder("callback", callback);
            in.putParcelable("fd", pipe[1]);
            in.putString("text", "bundle text");
            Parcel d = request();
            Parcel r = Parcel.obtain();
            d.writeBundle(in);
            pipe[1].close();
            service.transact(BUNDLE, d, r, 0);
            r.readException();
            Bundle out = r.readBundle(SocketBinderProbe.class.getClassLoader());
            check("binder inside a Bundle", out.getInt("answer") == 6, null);
            check("own proxy inside a Bundle is the same proxy", out.getBinder("service") == service, null);
            d.recycle(); r.recycle();
            check("file descriptor inside a Bundle", "bundle text".equals(readAll(pipe[0])), null);
        });

        run("oneway", () -> {
            int count = 2000;
            for (int i = 0; i < count; i++) {
                Parcel d = request();
                d.writeInt(i);
                service.transact(RECORD, d, null, IBinder.FLAG_ONEWAY);
                d.recycle();
            }
            int[] seen = new int[0];
            long deadline = System.currentTimeMillis() + 10000;
            while (seen.length < count && System.currentTimeMillis() < deadline) {
                Parcel d = request();
                Parcel r = Parcel.obtain();
                service.transact(RECORDED, d, r, 0);
                r.readException();
                seen = r.createIntArray();
                d.recycle(); r.recycle();
                if (seen.length < count) Thread.sleep(50);
            }
            boolean ordered = seen.length == count;
            for (int i = 0; ordered && i < count; i++) ordered = seen[i] == i;
            check("2000 oneway calls arrive in order", ordered, seen.length + " seen");
        });

        run("too large", () -> {
            Parcel d = request();
            d.writeByteArray(new byte[2 * 1024 * 1024]);
            Parcel r = Parcel.obtain();
            try {
                service.transact(ECHO, d, r, 0);
                check("large call refused as on Android", false, "not refused");
            } catch (TransactionTooLargeException e) {
                check("large call refused as on Android", true, null);
            }
            d.recycle(); r.recycle();
            Parcel d2 = request();
            Parcel r2 = Parcel.obtain();
            boolean failed = false;
            try {
                service.transact(LARGE_REPLY, d2, r2, 0);
            } catch (RemoteException e) {
                failed = !(e instanceof DeadObjectException);
            }
            check("large reply fails without ending the channel", failed && service.isBinderAlive(), null);
            d2.recycle(); r2.recycle();
        });

        run("speed", () -> {
            long start = System.nanoTime();
            int calls = 5000;
            for (int i = 0; i < calls; i++) {
                Parcel d = request();
                Parcel r = Parcel.obtain();
                d.writeString("x");
                d.writeByteArray(new byte[64]);
                service.transact(ECHO, d, r, 0);
                r.readException();
                d.recycle(); r.recycle();
            }
            long micros = (System.nanoTime() - start) / 1000 / calls;
            check("speed", true, micros + " us per call");
        });

        run("death", () -> {
            CountDownLatch died = new CountDownLatch(1);
            IBinder.DeathRecipient recipient = died::countDown;
            service.linkToDeath(recipient, 0);
            Parcel d = request();
            Parcel r = Parcel.obtain();
            boolean dead = false;
            try {
                service.transact(EXIT, d, r, 0);
            } catch (DeadObjectException e) {
                dead = true;
            }
            check("call fails when the peer dies", dead, null);
            check("death recipient fires", died.await(5, TimeUnit.SECONDS), null);
            Parcel d2 = request();
            Parcel r2 = Parcel.obtain();
            boolean deadAfter = false;
            try {
                service.transact(ECHO, d2, r2, 0);
            } catch (DeadObjectException e) {
                deadAfter = true;
            }
            check("calls after death fail", deadAfter && !service.isBinderAlive(), null);
        });
    }

    static void serve(int listener) throws Exception {
        System.out.println("listening");
        while (true) {
            int client = SocketBinder.accept(listener);
            new SocketBinder(client, new Service(), "client");
        }
    }

    /** Called by the test launcher, which has already loaded the native library. */
    public static int main2(String[] args) throws Exception {
        String name = args[1];
        if (args[0].equals("server")) {
            serve(SocketBinder.listen(name));
        }
        SocketBinder channel = new SocketBinder(SocketBinder.connect(name), null, "server");
        runChecks(channel.getRemoteRoot());
        System.out.println(sFailures == 0 ? "ALL PASSED" : sFailures + " FAILED");
        return sFailures;
    }

    public static void main(String[] args) throws Exception {
        System.load(args[2]);
        System.exit(main2(args));
    }
}
