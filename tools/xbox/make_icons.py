#!/usr/bin/env python3
"""The Xbox dashboard's two pictures: the title image (128x128) beside the
game and the save image (64x64) beside its save.

Both are Diddy's 40x40 portrait from the game's race HUD, three and (its
middle 32x32) two times over, pixel for pixel. Nothing of it is committed:
the asset tool extracts it from your ROM (setup.py,
assets/.vanilla/<version>/textures/2d/hud/diddy_icon.png), the BMPs land in
Platform/xbox/gen/ (gitignored), and RXDK's bundler turns them into the DXT1
.xbx the dashboard needs (Platform/xbox/titleimage.rdf, saveimage.rdf).

    python tools/xbox/make_icons.py
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbconfig  # noqa: E402

ROOT = xbconfig.ROOT
OUT = os.path.join(ROOT, "Platform", "xbox", "gen")
PORTRAIT = "textures/2d/hud/diddy_icon.png"


def read_png(path):
    """8-bit RGB or RGBA, non-interlaced: (w, h, rows of (r, g, b))."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("make_icons: %s is not a PNG" % path)
    p, idat, hdr = 8, [], None
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8])
        c = d[p + 8:p + 8 + n]
        p += 12 + n
        if t == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", c)
        elif t == b"IDAT":
            idat.append(c)
        elif t == b"IEND":
            break
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or ctype not in (2, 6) or interlace != 0:
        raise SystemExit("make_icons: %s is not 8-bit RGB(A) non-interlaced" % path)
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    st = w * bpp
    rows, prev, i = [], bytes(st), 0
    for _ in range(h):
        flt = raw[i]
        line = bytearray(raw[i + 1:i + 1 + st])
        i += 1 + st
        for x in range(st):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if flt == 1:
                line[x] = (line[x] + a) & 255
            elif flt == 2:
                line[x] = (line[x] + b) & 255
            elif flt == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif flt == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if (pa <= pb and pa <= pc) else (b if pb <= pc else c))) & 255
        prev = bytes(line)
        px = []
        for x in range(w):
            r, g, bl = line[x * bpp:x * bpp + 3]
            al = line[x * bpp + 3] if bpp == 4 else 255
            px.append((r * al // 255, g * al // 255, bl * al // 255))  # over black
        rows.append(px)
    return w, h, rows


def scaled(rows, x0, y0, n, k, size):
    """n x n pixels of `rows` from (x0, y0), each k x k, centred in size x
    size of black."""
    out = [[(0, 0, 0)] * size for _ in range(size)]
    off = (size - n * k) // 2
    for y in range(n * k):
        for x in range(n * k):
            out[off + y][off + x] = rows[y0 + y // k][x0 + x // k]
    return out


def write_bmp(path, pix):
    h, w = len(pix), len(pix[0])
    pad = (4 - (w * 3) % 4) % 4
    data = bytearray()
    for row in reversed(pix):
        for r, g, b in row:
            data += bytes((b, g, r))
        data += b"\0" * pad
    hdr = struct.pack("<2sIHHI", b"BM", 54 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(hdr + info + data)
    os.replace(tmp, path)


def main():
    src = os.path.join(ROOT, "assets", ".vanilla", xbconfig.rom_version(), *PORTRAIT.split("/"))
    if not os.path.isfile(src):
        raise SystemExit("make_icons: %s is not there (setup.py extracts it from your ROM)" % src)
    os.makedirs(OUT, exist_ok=True)
    outs = [os.path.join(OUT, "titleimage.bmp"), os.path.join(OUT, "saveimage.bmp")]
    stamp = max(os.path.getmtime(src), os.path.getmtime(os.path.abspath(__file__)))
    if all(os.path.isfile(p) and os.path.getmtime(p) >= stamp for p in outs):
        print("make_icons: up to date")
        return
    w, h, rows = read_png(src)
    if (w, h) != (40, 40):
        raise SystemExit("make_icons: %s is %dx%d, not the 40x40 portrait" % (src, w, h))
    write_bmp(outs[0], scaled(rows, 0, 0, 40, 3, 128))
    write_bmp(outs[1], scaled(rows, 4, 4, 32, 2, 64))
    print("make_icons: Diddy's portrait -> titleimage.bmp (128x128), saveimage.bmp (64x64)")


if __name__ == "__main__":
    main()
