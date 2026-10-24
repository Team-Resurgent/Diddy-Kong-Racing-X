// dkr_boot.c -- into the game.
//
// The game's main-thread entry (src/thread3_main.c): init_game() and the
// main_game_loop() forever-loop, called directly on the host's thread as the
// Linux and Dreamcast ports do. The N64 boot chain (mainproc -> thread1 ->
// thread3) is skipped, and with it libultra's thread machinery; code that asks
// about "the current thread" (osGetThreadPri(NULL) and the like) is answered by
// this thread posing as thread 3 at its real priority.

#include <ultra64.h>

void thread3_main(void *unused);

extern OSThread *__osRunningThread;
static OSThread sHostThread;

void dkr_boot(void) {
    sHostThread.id = 3;
    sHostThread.priority = 10;
    __osRunningThread = &sHostThread;
    thread3_main(NULL);
}
