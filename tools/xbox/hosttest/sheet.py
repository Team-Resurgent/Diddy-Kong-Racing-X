#!/usr/bin/env python3
"""A contact sheet of the harness's screenshots: every frame_*.png in a folder,
at half size, four to a row, as sheet.png (or the frames from FIRST on, at
most MAX of them).

    python tools/xbox/hosttest/sheet.py DIR [FIRST [MAX]]
"""
import glob
import os
import re
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    i, w, h, idat = 8, 0, 0, b""
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        t, c = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h = struct.unpack(">II", c[:8])
        elif t == b"IDAT":
            idat += c
        i += 12 + n
    raw = zlib.decompress(idat)
    return w, h, [raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h)]


def chunk(t, c):
    return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)


def main():
    folder = sys.argv[1]
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    most = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    files = [f for f in sorted(glob.glob(os.path.join(folder, "frame_*.png")))
             if int(re.findall(r"(\d+)", os.path.basename(f))[0]) >= first][:most]
    if not files:
        raise SystemExit("sheet: no frames")
    cw, ch, cols = 320, 240, 4
    rows_n = (len(files) + cols - 1) // cols
    out = [bytearray(cw * cols * 3) for _ in range(ch * rows_n)]
    for k, f in enumerate(files):
        w, h, rows = read_png(f)
        ox, oy = (k % cols) * cw, (k // cols) * ch
        for y in range(ch):
            src = rows[y * h // ch]
            for x in range(cw):
                sx = x * w // cw
                out[oy + y][(ox + x) * 3:(ox + x) * 3 + 3] = src[sx * 3:sx * 3 + 3]
    raw = b"".join(b"\0" + bytes(r) for r in out)
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", cw * cols, ch * rows_n, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    out_path = os.path.join(folder, "sheet.png")
    open(out_path, "wb").write(png)
    print("sheet: %s, %d frames (%s .. %s)" % (out_path, len(files), os.path.basename(files[0]),
                                                os.path.basename(files[-1])))


if __name__ == "__main__":
    main()
