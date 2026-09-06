#pragma once
/* libc differences at the OpenHarmony host boundary. Do not impersonate Bionic. */
#if defined(__OHOS__) && !defined(__ASSEMBLER__)
#ifdef __cplusplus
extern "C" {
#endif
extern char* __progname;
#ifdef __cplusplus
}
#ifndef _NULLPTR_T_DEFINED
namespace std { typedef decltype(nullptr) nullptr_t; }
using ::std::nullptr_t;
#define _NULLPTR_T_DEFINED
#endif
#endif
#define program_invocation_short_name __progname
#endif
