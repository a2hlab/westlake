/* log/log.h for AOSP's camera_metadata.c built outside Android: its ALOGE/ALOGW/ALOGV and the security event log
 * go to stderr (the SN event log has no OH counterpart). */
#pragma once
#include <stdio.h>
#define ALOGE(...) (fprintf(stderr, "[camera_metadata] E " __VA_ARGS__), fputc('\n', stderr))
#define ALOGW(...) (fprintf(stderr, "[camera_metadata] W " __VA_ARGS__), fputc('\n', stderr))
#define ALOGI(...) ((void) 0)
#define ALOGD(...) ((void) 0)
#define ALOGV(...) ((void) 0)
#define android_errorWriteLog(tag, subtag) (fprintf(stderr, "[camera_metadata] security event %s\n", (subtag)))
