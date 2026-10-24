#!/usr/bin/env python3
"""Name the addresses in an Xbox crash report: `dkrx: FAULT ...` lines, or an
xbWatson exception, from the debug channel.

imagebld lays the XBE's sections out a page or so higher than the linker put
them in the .exe, so an address from the console is moved into its section in
the .exe before the RXDK's llvm-symbolizer looks it up in the .exe's line
tables. Use the .exe and XBE of the build that crashed.

    python tools/xbox/xbe_symbolize.py 0x000DC2B0 0x0004A3F1 ...
    python tools/xbox/xbe_symbolize.py --config Debug < xbwatson.txt
"""
import argparse
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xbconfig  # noqa: E402

ROOT = xbconfig.ROOT
NAME = "Diddy-Kong-Racing-X"


def xbe_sections(path):
    d = open(path, "rb").read()
    base, = struct.unpack_from("<I", d, 0x104)
    count, table = struct.unpack_from("<II", d, 0x11C)
    out = []
    for i in range(count):
        _, va, size, _, _, name_at = struct.unpack_from("<IIIIII", d, table - base + i * 56)
        name = d[name_at - base:name_at - base + 16].split(b"\0")[0].decode("latin-1")
        out.append((name, va, size))
    return out


def exe_sections(path):
    d = open(path, "rb").read()
    pe, = struct.unpack_from("<I", d, 0x3C)
    count, = struct.unpack_from("<H", d, pe + 6)
    opt_size, = struct.unpack_from("<H", d, pe + 20)
    image_base, = struct.unpack_from("<I", d, pe + 24 + 28)
    table = pe + 24 + opt_size
    out = []
    for i in range(count):
        raw = d[table + i * 40:table + i * 40 + 40]
        name = raw[:8].split(b"\0")[0].decode("latin-1")
        size, rva = struct.unpack_from("<II", raw, 8)
        out.append((name, image_base + rva, size))
    return out


def to_exe(addr, xbe, exe):
    """The .exe address of a console address: the same offset into the
    section of the same place in the list (the names repeat, e.g. .XBLD)."""
    for i, (name, va, size) in enumerate(xbe):
        if va <= addr < va + size and i < len(exe) and exe[i][0] == name:
            return exe[i][1] + (addr - va), name
    return None, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("addresses", nargs="*", help="hex addresses (read from stdin when none are given)")
    ap.add_argument("--config", default="Release", help="the build that crashed (default Release)")
    ap.add_argument("--exe", help="its .exe (default out/<config>/%s.exe)" % NAME)
    ap.add_argument("--xbe", help="its XBE (default out/<config>/Build/%s/default.xbe)" % NAME)
    args = ap.parse_args()

    exe_path = args.exe or os.path.join(ROOT, "out", args.config, NAME + ".exe")
    xbe_path = args.xbe or os.path.join(ROOT, "out", args.config, "Build", NAME, "default.xbe")
    text = " ".join(args.addresses) if args.addresses else sys.stdin.read()
    addrs = [int(a, 16) for a in re.findall(r"\b(?:0x)?([0-9A-Fa-f]{8})\b", text)]
    xbe, exe = xbe_sections(xbe_path), exe_sections(exe_path)
    symbolizer = os.path.join(xbconfig.rxdk_root(), "llvm", "xbox-windows-x64", "bin", "llvm-symbolizer.exe")

    for a in addrs:
        e, section = to_exe(a, xbe, exe)
        if e is None:
            print("%08x  (not in the program)" % a)
            continue
        r = subprocess.run([symbolizer, "--obj=" + exe_path, "0x%x" % e], capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if l.strip()]
        where = " ".join(lines[:2]).replace(ROOT + os.sep, "") if lines else "?"
        print("%08x  %-8s %s" % (a, section, where))


if __name__ == "__main__":
    main()
