/* cutils/compiler.h for AOSP's camera_metadata built outside Android: the macros it uses, and the standard
 * headers Android's cutils brings in with them (offsetof, bool). ANDROID_API stays empty: the library is linked
 * into libwl_missing_natives.so, whose version script exports none of it. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef ANDROID_API
#define ANDROID_API
#endif
#define CC_LIKELY(exp) (__builtin_expect(!!(exp), true))
#define CC_UNLIKELY(exp) (__builtin_expect(!!(exp), false))
