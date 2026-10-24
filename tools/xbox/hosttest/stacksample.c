/* stacksample.c -- where a hung harness is: each thread's instruction
 * pointer, and the words on its stack that point into the program (the
 * likely return addresses, nearest first), as offsets from the program's
 * base, for tools/xbox/hosttest/symbolize.py to name against hosttest.map.
 * A 32-bit Windows program, so it can read the harness's (32-bit) threads.
 *
 *   stacksample.exe PID */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <tlhelp32.h>

int main(int argc, char **argv)
{
    DWORD pid = argc > 1 ? (DWORD)atoi(argv[1]) : 0;
    HANDLE proc, snap;
    MODULEENTRY32 me;
    THREADENTRY32 te;
    DWORD base = 0, size = 0;

    proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (proc == NULL)
    {
        fprintf(stderr, "cannot open process %lu\n", (unsigned long)pid);
        return 1;
    }
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me))
    {
        base = (DWORD)(UINT_PTR)me.modBaseAddr;
        size = me.modBaseSize;
    }
    CloseHandle(snap);
    printf("base %08lx size %08lx\n", (unsigned long)base, (unsigned long)size);

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    {
        HANDLE th;
        CONTEXT c;
        DWORD stack[2048];
        SIZE_T got = 0;
        int i, n = 0;

        if (te.th32OwnerProcessID != pid)
            continue;
        th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
        if (th == NULL)
            continue;
        SuspendThread(th);
        c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(th, &c))
        {
            printf("thread %lu eip +%08lx\n  stack:", (unsigned long)te.th32ThreadID,
                   (unsigned long)(c.Eip - base));
            ReadProcessMemory(proc, (LPCVOID)(UINT_PTR)c.Esp, stack, sizeof(stack), &got);
            for (i = 0; i < (int)(got / 4) && n < 24; i++)
                if (stack[i] >= base && stack[i] < base + size)
                {
                    printf(" +%08lx", (unsigned long)(stack[i] - base));
                    n++;
                }
            printf("\n");
        }
        ResumeThread(th);
        CloseHandle(th);
    }
    CloseHandle(snap);
    CloseHandle(proc);
    return 0;
}
