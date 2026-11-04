package adapter.core;

import android.os.Parcel;

/**
 * What SocketBinder needs from native code: a Parcel's raw bytes and object table, which Java can't
 * read while the Parcel holds objects (Parcel.marshall refuses), and socket calls that carry file
 * descriptors. See framework/javacore-shim/socketbinder_natives.c.
 */
final class SocketBinderNative {
    private SocketBinderNative() {}

    /** Object types, as flat_binder_object's hdr.type holds them (uapi/linux/android/binder.h). */
    static final int TYPE_BINDER = 0x73622a85;
    static final int TYPE_WEAK_BINDER = 0x77622a85;
    static final int TYPE_HANDLE = 0x73682a85;
    static final int TYPE_WEAK_HANDLE = 0x77682a85;
    static final int TYPE_FD = 0x66642a85;

    /** The Parcel's bytes, whatever it holds. */
    static native byte[] parcelBytes(Parcel parcel);

    /** Pairs of offset and type for each object in the Parcel, in the Parcel's order. */
    static native int[] parcelObjects(Parcel parcel);

    /** Two connected stream sockets. */
    static native int[] socketPair();

    /** A listening socket in the abstract namespace; throws IOException on failure. */
    static native int listen(String name) throws java.io.IOException;

    static native int accept(int fd) throws java.io.IOException;

    static native int connect(String name) throws java.io.IOException;

    /** Sends part of buf, with fds attached if not null. Returns the bytes sent. */
    static native int send(int fd, byte[] buf, int off, int len, int[] fds) throws java.io.IOException;

    /**
     * Receives into buf. fds[0] returns how many descriptors arrived, in fds[1..]. Returns the bytes
     * read, or 0 at end of stream.
     */
    static native int recv(int fd, byte[] buf, int off, int len, int[] fds) throws java.io.IOException;

    /** Unblocks a thread in recv on this socket, without closing it. */
    static native void shutdown(int fd);

    static native void close(int fd);
}
