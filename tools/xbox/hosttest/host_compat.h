/* host_compat.h -- forced in front of every harness compile (build.py).
 *
 * The harness builds against Visual Studio's C headers, which the game was
 * never compiled with. Two things in them collide with the N64 headers:
 *  - errno is a macro, and libultra's OSContStatus has a field of that name
 *    (include/PR/os_cont.h); nothing in the game uses the C library's errno.
 *  - uintptr_t is declared under a guard of Microsoft's own name, so
 *    include/types.h declares it again (it checks newlib's).
 *  - _itoa is Microsoft's (stdlib.h), and src/printf.c has its own.
 * The headers are included here first (their inline functions use errno),
 * and the names put right. */
#ifndef HOST_COMPAT_H
#define HOST_COMPAT_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef errno
#define _UINTPTR_T_DECLARED
#define _itoa dkr_itoa

#endif
