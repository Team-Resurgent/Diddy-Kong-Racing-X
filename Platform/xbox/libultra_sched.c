// libultra_sched.c -- libultra's scheduler (libultra/src/sc/sched.c), compiled
// through this file.
//
// sched.c includes its header as <sched.h>, and the RXDK searches its own SDK
// headers first, where POSIX's sched.h is; the game's (include/PR/sched.h) is
// included here by its own path beforehand, so its guard is set and the
// scheduler's types are there.

#include "PR/sched.h"
#include "../../libultra/src/sc/sched.c"
