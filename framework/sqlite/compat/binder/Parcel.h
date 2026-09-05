/* Declaration-only stand-in for AOSP frameworks/native/include/binder/Parcel.h.
 *
 * There is no Binder in this port, and the supplied JNI layer already knows it:
 * nativeCreateFromParcel and nativeWriteToParcel both hard-code
 *
 *     Parcel* parcel = nullptr;  // WESTLAKE: parcelForJavaObject unavailable
 *
 * so the pointer that reaches CursorWindow::createFromParcel/writeToParcel is
 * always null.  Those two methods start with `if (parcel->readString8(...))` and
 * bail to their `fail` label on any non-zero return, so defining every method to
 * return an error turns the whole path into a clean failure: the JNI layer
 * throws CursorWindowAllocationException / RuntimeException, which is the honest
 * answer to "unparcel this cursor" on a system with no IPC.
 *
 * The definitions are deliberately out-of-line in wl_compat.cpp and never touch
 * `this`.  Calling a non-virtual member through a null pointer is formally UB,
 * but with the definition invisible to the caller's translation unit the
 * compiler cannot infer non-nullness and simply emits the call, which our
 * definition ignores.  That reasoning depends on the two files being compiled
 * separately, so build_sqlite.sh must not enable LTO -- there is an assertion in
 * that script to keep it that way.
 */
#ifndef WL_BINDER_PARCEL_H
#define WL_BINDER_PARCEL_H

/* fcntl.h and unistd.h are not used by anything declared below; they are here
 * because AOSP's binder/Parcel.h drags them in transitively, and
 * wl_CursorWindow.cpp relies on that -- it calls ::close, ::fcntl and
 * F_DUPFD_CLOEXEC having included only androidfw/CursorWindow.h, which includes
 * this file.  Reproducing the transitive include here keeps that source
 * untouched, and both headers do belong with a class whose interface passes file
 * descriptors around. */
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils/Errors.h"
#include "utils/String8.h"

namespace android {

class Parcel {
public:
    Parcel() {}
    ~Parcel() {}

    status_t readString8(String8* out) const;
    status_t readUint32(uint32_t* out) const;
    status_t readBool(bool* out) const;
    status_t read(void* outData, size_t len) const;
    int readFileDescriptor() const;

    status_t writeString8(const String8& str);
    status_t writeUint32(uint32_t val);
    status_t writeBool(bool val);
    status_t writeDupFileDescriptor(int fd);
    void* writeInplace(size_t len);
};

}  // namespace android

#endif  // WL_BINDER_PARCEL_H
