#!/usr/bin/env python3
"""Name the addresses stacksample.exe prints (+offsets from the program's
base) against a linker map of the same link (build.py --map writes
build/hosttest/hosttest.map).

    stacksample.exe PID | python tools/xbox/hosttest/symbolize.py build/hosttest/hosttest.map
"""
import re
import sys


def load(path):
    syms = []
    for line in open(path, encoding="utf-8", errors="replace"):
        # " 0001:000abcde       _name     00401abc f   obj"
        m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s", line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
    return syms


def name(syms, rva, base=0x400000):
    va = base + rva
    lo, hi = 0, len(syms)
    while lo < hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= va:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return "?"
    addr, sym = syms[lo - 1]
    return "%s+0x%x" % (sym, va - addr)


def main():
    syms = load(sys.argv[1])
    for line in sys.stdin:
        line = line.rstrip("\n")
        out = re.sub(r"\+([0-9a-f]{8})", lambda m: name(syms, int(m.group(1), 16)) if int(m.group(1), 16) < 0x2000000 else m.group(0), line)
        print(out)


if __name__ == "__main__":
    main()
