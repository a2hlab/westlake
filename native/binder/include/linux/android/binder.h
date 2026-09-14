// Select the pinned Android 15 Binder UAPI without replacing the OH libc or
// other kernel headers. The kernel reports ENOTTY for unsupported ioctls.
#pragma once
#include <binder.h>
