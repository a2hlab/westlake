// Compatibility for AOSP's generated GL JNI sources: they need only NELEM from here.
#pragma once
#ifndef NELEM
#define NELEM(x) ((int) (sizeof(x) / sizeof((x)[0])))
#endif
