/* host_rt.c -- the GNU runtime helpers the harness's objects call, for an
 * MSVC link (build.py compiles this one file for i686-pc-windows-msvc).
 *
 * The game is compiled for i686-pc-windows-gnu, as the RXDK compiles it, so
 * its 64-bit divisions call libgcc's __divdi3 family, and clang turns its
 * memcmp() == 0 tests into bcmp(); Visual Studio ships neither for 32-bit x86.
 * Compiled for the MSVC target, the divisions here become the C runtime's own
 * (_alldiv and friends) rather than calls back into themselves. And the
 * x86 C runtime has floorf and ceilf only as inline functions in math.h,
 * which a GNU-mode compile does not see as such. */
#include <stddef.h>
#include <string.h>

double floor(double);
double ceil(double);

float floorf(float x)
{
    return (float)floor(x);
}

float ceilf(float x)
{
    return (float)ceil(x);
}

long long __divdi3(long long a, long long b)
{
    return a / b;
}

long long __moddi3(long long a, long long b)
{
    return a % b;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b)
{
    return a / b;
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    return a % b;
}

int bcmp(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n);
}

/* --threaded (host_main.c): the sound drained by a thread of its own, as on
 * the Xbox (Platform/xbox/xbox_audio.c). Here, where windows.h can be had. */
#include <stdio.h>
#include <windows.h>

static void (*sThreadFn)(void);

static DWORD WINAPI rt_thread_main(LPVOID arg)
{
    (void)arg;
    sThreadFn();
    return 0;
}

void rt_thread_start(void (*fn)(void))
{
    HANDLE h;

    sThreadFn = fn;
    h = CreateThread(NULL, 256 * 1024, rt_thread_main, NULL, 0, NULL);
    SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL);
    CloseHandle(h);
}

void rt_sleep_ms(unsigned ms)
{
    Sleep(ms);
}

/* A crash's address and the program addresses on its stack, as offsets from
 * the program's base (tools/xbox/hosttest/symbolize.py names them). */
static LONG WINAPI rt_fault(EXCEPTION_POINTERS *ep)
{
    DWORD base = (DWORD)(UINT_PTR)GetModuleHandleA(NULL);
    const DWORD *sp = (const DWORD *)(UINT_PTR)ep->ContextRecord->Esp;
    char line[2048];
    int i, n;

    n = sprintf(line, "FAULT %08lx at +%08lx (address %08lx)\n  stack:", ep->ExceptionRecord->ExceptionCode,
                (unsigned long)((DWORD)(UINT_PTR)ep->ExceptionRecord->ExceptionAddress - base),
                (unsigned long)(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0));
    for (i = 0; i < 1024 && n < (int)sizeof(line) - 16; i++)
        if (!IsBadReadPtr(&sp[i], 4) && sp[i] >= base + 0x1000 && sp[i] < base + 0x200000)
            n += sprintf(line + n, " +%08lx", (unsigned long)(sp[i] - base));
    sprintf(line + n, "\n");
    fputs(line, stdout);
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

void rt_fault_report(void)
{
    SetUnhandledExceptionFilter(rt_fault);
}
