#ifndef _TYPES_H_
#define _TYPES_H_

#include <PR/ultratypes.h>

#ifdef GBI_FLOAT_MTX
typedef float MtxF[4][4] __attribute__((aligned(8)));
#else
typedef float MtxF[4][4];
#endif
typedef s32 MtxS[4][4];
typedef s16 VertexList;
typedef u8 TriangleList;
// Platform code (dreamcast/, linux/) pulls the system headers, whose newlib
// (DC) already declares uintptr_t and sets this guard; redefining it is an
// error under C23. Game code compiles against the N64 libc stubs, which never
// declare it, so the typedef below still applies there.
#ifndef _UINTPTR_T_DECLARED
typedef u32 uintptr_t;
#define _UINTPTR_T_DECLARED
#endif

#if defined(TARGET_XBOX) && !defined(DKRX_LOG)
// The Xbox's Release build: the game's debug messages compile away (the Debug
// build, and setup.py --log, keep them; tools/xbox/xbconfig.py).
void isv_printf(const char *fmt, ...);
#define stubbed_printf(...) ((void) 0)
#elif defined(NON_MATCHING)
void isv_printf(const char *fmt, ...);
#define stubbed_printf isv_printf
#else
#define stubbed_printf
#endif

#endif
