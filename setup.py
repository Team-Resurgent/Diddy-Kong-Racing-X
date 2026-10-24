#!/usr/bin/env python3
"""One command from your Diddy Kong Racing ROM to an Xbox disc image.

    python setup.py                 # everything -> out/Release/XISO/Diddy-Kong-Racing-X.iso
    python setup.py --check         # say what is there and what is missing
    python setup.py --skip-build    # stop before compiling (to build in Visual Studio)
    python setup.py --config Debug  # the debug configuration
    python setup.py --unlock-all    # a console with no save yet starts at 100%

Needs: Python 3.8+ (standard library only), the RXDK (its clang also builds
the ROM extractor), and your Diddy Kong Racing ROM (USA, v1.0 or v1.1) in this
folder as baserom.z64 (or in baseroms/). A .v64 / .n64 (byte-swapped) dump is
accepted and put in order.

Nothing that comes from the ROM is in this repository; all of it is made
here, into folders .gitignore keeps out:

  1. the ROM, checked (SHA-1) and put in big-endian order, where the
     decomp's asset tool looks for it: baseroms/baserom.us.v77.z64 (v1.0) or
     baserom.us.v80.z64 (v1.1); its version goes to build/dkr_version
  2. dkr_assets_tool, the decomp's asset extractor and builder
     (tools/dkr_assets_tool_src), built for Windows: build/tools/
  3. the tool run on the ROM: every asset extracted (assets/.vanilla/), then
     built back into assets/assets.bin and assets.lut.bin -- the ROM's own
     bytes, checked against it -- and include/asset_enums.h
  4. data/, the disc's data folder: the two asset files, as D:\\data
  5. the dashboard icons (tools/xbox/make_icons.py), rxdk.project.json
     (gen_manifest.py) and the Visual Studio project (gen_vcxproj.py)
  6. the build: Rxdk.Cli compiles, links, bundles the icons and packs the ISO
"""
import argparse
import glob
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "tools", "xbox"))
import xbconfig  # noqa: E402

NAME = "Diddy-Kong-Racing-X"
BUILD = os.path.join(ROOT, "build")
ROM_NAMES = ("baserom.z64", "dkr.z64", "baserom.v64", "dkr.v64", "baserom.n64", "dkr.n64")
# the decomp's (ver/splat/dkr.<version>.yaml), the .z64 in big-endian order
ROM_SHA1 = {
    "0cb115d8716dbbc2922fda38e533b9fe63bb9670": ("us.v77", "USA v1.0"),
    "6d96743d46f8c0cd0edb0ec5600b003c89b93755": ("us.v80", "USA v1.1"),
}
OTHER_SHA1 = {
    "dd5d64dd140cb7aa28404fa35abdcaba33c29260": "Europe v1.0",
    "b7f628073237b3d211d40406aa0884ff8fdd70d5": "Europe v1.1",
    "23ba3d302025153d111416e751027cef11213a19": "Japan",
}
# where the asset table and the assets are in each ROM (ver/splat/*.yaml):
# what the asset tool builds is checked against these bytes
ROM_ASSETS = {
    "us.v77": (0xECB60, 0xECC30, 0xAC9630),
    "us.v80": (0xED0E0, 0xED1B0, 0xB8CFD0),
}

TOOL_SRC = os.path.join(ROOT, "tools", "dkr_assets_tool_src")
TOOL_EXE = os.path.join(BUILD, "tools", "dkr_assets_tool.exe")
COMPAT = os.path.join(ROOT, "tools", "xbox", "hostcompat")
ASSETS = os.path.join(ROOT, "assets")
DATA = os.path.join(ROOT, "data")


def say(msg):
    print("setup: " + msg, flush=True)


def die(msg):
    print("setup: ERROR: " + msg, file=sys.stderr, flush=True)
    sys.exit(1)


def run(cmd, cwd=ROOT, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env)
    if r.returncode != 0:
        die("failed (%d): %s" % (r.returncode, " ".join(cmd)))


def remove_tree(path):
    def retry(func, p, _exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    shutil.rmtree(path, onerror=retry)


def newest(paths):
    return max(os.path.getmtime(p) for p in paths)


def write_if_changed(path, text):
    old = open(path, encoding="utf-8").read() if os.path.isfile(path) else None
    if old != text:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


# ------------------------------------------------------------------- ROM

def find_rom():
    for d in (ROOT, os.path.join(ROOT, "baseroms")):
        for n in ROM_NAMES:
            p = os.path.join(d, n)
            if os.path.isfile(p):
                return p
    return None


def to_z64(data):
    """Any of the three N64 byte orders, as big-endian .z64."""
    head = data[:4]
    if head == b"\x80\x37\x12\x40":
        return data, "z64"
    if head == b"\x37\x80\x40\x12":       # .v64: 16-bit swapped
        b = bytearray(data)
        b[0::2], b[1::2] = data[1::2], data[0::2]
        return bytes(b), "v64"
    if head == b"\x40\x12\x37\x80":       # .n64: 32-bit little-endian
        b = bytearray(len(data))
        b[0::4], b[1::4], b[2::4], b[3::4] = data[3::4], data[2::4], data[1::4], data[0::4]
        return bytes(b), "n64"
    return None, None


def rom_check(write=False):
    """(version or None, message, ROM bytes): the ROM found, put in order and
    checked; with `write`, put where the asset tool looks for it."""
    src = find_rom()
    if src is None:
        return None, ("no ROM: put your Diddy Kong Racing ROM (USA, v1.0 or v1.1) in %s as "
                      "baserom.z64" % ROOT), None
    with open(src, "rb") as f:
        raw = f.read()
    data, order = to_z64(raw)
    if data is None:
        return None, "%s is not an N64 ROM" % os.path.basename(src), None
    sha1 = hashlib.sha1(data).hexdigest()
    if sha1 not in ROM_SHA1:
        what = OTHER_SHA1.get(sha1)
        if what:
            return None, ("%s is Diddy Kong Racing (%s); this port takes the USA release (v1.0 or "
                          "v1.1)" % (os.path.basename(src), what)), None
        return None, ("%s is not a supported Diddy Kong Racing ROM (SHA-1 %s; USA v1.0 is %s, "
                      "v1.1 is %s)" % (os.path.basename(src), sha1, *ROM_SHA1.keys())), None
    version, label = ROM_SHA1[sha1]
    if write:
        dst = os.path.join(ROOT, "baseroms", "baserom.%s.z64" % version)
        old = open(dst, "rb").read() if os.path.isfile(dst) else None
        if old != data:
            with open(dst, "wb") as f:
                f.write(data)
    note = "" if order == "z64" else " (a .%s dump, put in .z64 order)" % order
    return version, "%s: Diddy Kong Racing %s%s" % (os.path.basename(src), label, note), data


# ------------------------------------------------------- dkr_assets_tool

def tool_sources():
    out = []
    for dp, dn, fn in os.walk(TOOL_SRC):
        dn[:] = [d for d in dn if d != "_build"]
        for f in fn:
            # PCRE2 is optional (the tool falls back to std::regex) and is not
            # there on Windows
            if f.endswith((".c", ".cpp")) and f != "regexPcre2.cpp":
                out.append(os.path.join(dp, f))
    return sorted(out)


def step_tool():
    """The decomp's asset tool, for Windows, with the RXDK's clang and its
    MinGW host sysroot: build/tools/dkr_assets_tool.exe."""
    srcs = tool_sources()
    hdrs = glob.glob(os.path.join(TOOL_SRC, "**", "*.h*"), recursive=True) + \
        glob.glob(os.path.join(COMPAT, "**", "*.h"), recursive=True)
    if os.path.isfile(TOOL_EXE) and os.path.getmtime(TOOL_EXE) >= newest(srcs + hdrs):
        return
    flags = xbconfig.host_flags()
    if flags is None:
        die("the RXDK's clang with its host sysroot was not found (update RXDK: RXDK > RXDK "
            "Window > Install Prerequisites)")
    say("building dkr_assets_tool, the ROM extractor (%d files)" % len(srcs))
    objdir = os.path.join(BUILD, "tools", "obj")
    os.makedirs(objdir, exist_ok=True)
    cc, cxx = xbconfig.clang(), xbconfig.clangxx()

    def comp(src):
        rel = os.path.relpath(src, TOOL_SRC).replace(os.sep, "_")
        obj = os.path.join(objdir, rel + ".o")
        if os.path.isfile(obj) and os.path.getmtime(obj) >= newest([src] + hdrs):
            return obj, None
        if src.endswith(".c"):
            cmd = [cc, "-std=gnu11"]
        else:
            cmd = [cxx, "-stdlib=libc++", "-std=c++17"]
        cmd += flags + ["-O2", "-w", "-I", COMPAT, "-include", os.path.join(COMPAT, "dkr_compat.h"),
                        "-I", os.path.join(ROOT, "tools"), "-I", TOOL_SRC, "-c", src, "-o", obj]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if r.returncode != 0:
            return None, "%s:\n%s" % (os.path.relpath(src, ROOT), (r.stdout + r.stderr).strip()[-2000:])
        return obj, None

    with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        res = list(ex.map(comp, srcs))
    errs = [e for _, e in res if e]
    if errs:
        print("\n".join(errs[:5]))
        die("dkr_assets_tool: %d of %d files failed to compile" % (len(errs), len(srcs)))
    r = subprocess.run([cxx, "-stdlib=libc++", "-static"] + flags + [o for o, _ in res] + ["-o", TOOL_EXE],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print((r.stdout + r.stderr)[-3000:])
        die("dkr_assets_tool did not link")


# ---------------------------------------------------------------- assets

def step_assets(version, rom):
    """Extract every asset from the ROM, then build them back into the
    files the game reads, when the ROM or the tool changed since."""
    stamp = os.path.join(BUILD, "assets.%s.stamp" % version)
    sha1 = hashlib.sha1(rom).hexdigest()
    outs = [os.path.join(ASSETS, "assets.bin"), os.path.join(ASSETS, "assets.lut.bin"),
            os.path.join(ROOT, "include", "asset_enums.h")]
    if (os.path.isfile(stamp) and open(stamp).read().strip() == sha1 and all(map(os.path.isfile, outs))
            and os.path.getmtime(stamp) >= os.path.getmtime(TOOL_EXE)):
        return
    vanilla = os.path.join(ASSETS, ".vanilla", version)
    if os.path.isdir(vanilla):
        remove_tree(vanilla)
    t = time.time()
    say("extracting the game from the ROM (about a minute)")
    r = subprocess.run([TOOL_EXE, "extract", "-dkrv", version], cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0 or not os.path.isdir(vanilla):
        print((r.stdout + r.stderr)[-3000:])
        die("dkr_assets_tool extract failed")
    say("building the asset files")
    r = subprocess.run([TOOL_EXE, "build", "-o", "assets/assets.bin", "-dkrv", version], cwd=ROOT,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0 or not all(map(os.path.isfile, outs)):
        print((r.stdout + r.stderr)[-3000:])
        die("dkr_assets_tool build failed")
    # an unmodified build is the ROM's own bytes
    lut0, data0, end = ROM_ASSETS[version]
    lut = open(outs[1], "rb").read()
    data = open(outs[0], "rb").read()
    if lut != rom[lut0:data0] or data != rom[data0:end]:
        say("WARNING: the built assets differ from the ROM's (the game may still run)")
    say("assets: %.1f MB, the ROM's own, in %.0f s" % (len(data) / 1048576.0, time.time() - t))
    with open(stamp, "w") as f:
        f.write(sha1 + "\n")


# ------------------------------------------------------------------ data

def copy_if_changed(src, dst):
    if os.path.isfile(dst):
        a, b = os.stat(src), os.stat(dst)
        if a.st_size == b.st_size and int(a.st_mtime) <= int(b.st_mtime):
            return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    return True


def step_data():
    """data/: the two asset files, the disc's D:\\data. Anything else in it is
    removed (it would end up on the disc)."""
    want = ("assets.bin", "assets.lut.bin")
    for f in want:
        copy_if_changed(os.path.join(ASSETS, f), os.path.join(DATA, f))
    for f in os.listdir(DATA):
        if f not in want:
            p = os.path.join(DATA, f)
            if os.path.isdir(p):
                remove_tree(p)
            else:
                os.remove(p)


# ------------------------------------------------------------------ build

def tool(name, *args):
    run([sys.executable, os.path.join(ROOT, "tools", "xbox", name)] + list(args))


def step_xbox(version, unlock):
    write_if_changed(xbconfig.VERSION_FILE, version + "\n")
    flag = os.path.join(BUILD, "dkr_unlock_all")
    if unlock and not os.path.isfile(flag):
        open(flag, "w").close()
    elif not unlock and os.path.isfile(flag):
        os.remove(flag)
    tool("make_icons.py")
    tool("gen_manifest.py")
    if os.path.isfile(xbconfig.rxdk_cli()):
        tool("gen_vcxproj.py")


def step_build(config):
    cli = xbconfig.rxdk_cli()
    if not os.path.isfile(cli):
        die("Rxdk.Cli.exe not found: install RXDK (RXDK > RXDK Window > Install Prerequisites)")
    out = os.path.join(ROOT, "out", config)
    # (built for another ROM version, or save start, the defines differ, and
    # Rxdk.Cli compiles everything afresh by itself)
    say("building %s" % config)
    # when this build was made, for the first line the game logs: a crash
    # report is read against the symbols of the build that crashed
    # (Platform/xbox/xbox_main.c; the header is new each build, so that file is
    # always recompiled)
    stamp = os.path.join(ROOT, "Platform", "xbox", "gen", "build_stamp.h")
    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    write_if_changed(stamp, '#define DKRX_BUILD_STAMP "%s"\n' % time.strftime("%Y-%m-%d %H:%M:%S"))
    run([cli, "build", "--project-root", ROOT, "--configuration", config])
    iso = os.path.join(out, "XISO", NAME + ".iso")
    if os.path.isfile(iso):
        say("done: %s (%.0f MB)" % (iso, os.path.getsize(iso) / 1048576.0))
    else:
        say("built; the ISO is under out/%s/XISO" % config)


# ----------------------------------------------------------------- check

def check():
    ok = True
    version, why, _ = rom_check()
    say("ROM: %s" % why)
    ok &= version is not None
    py = sys.version_info >= (3, 8)
    say("Python: %d.%d%s" % (sys.version_info[0], sys.version_info[1], "" if py else " (3.8 or later needed)"))
    ok &= py
    cli = xbconfig.rxdk_cli()
    there = os.path.isfile(cli)
    say("RXDK CLI: %s" % (cli if there else "missing (RXDK > RXDK Window > Install Prerequisites)"))
    ok &= there
    hc = xbconfig.host_cc_available()
    say("host C++ compiler (the asset tool): %s" % (hc or "missing (the RXDK's clang with its host sysroot)"))
    ok &= hc is not None
    for label, path in (("asset tool", TOOL_EXE), ("assets", os.path.join(ASSETS, "assets.bin")),
                        ("data", os.path.join(DATA, "assets.bin")),
                        ("manifest", os.path.join(ROOT, "rxdk.project.json"))):
        say("%s: %s" % (label, "built" if os.path.exists(path) else "not built yet"))
    return ok


def main():
    ap = argparse.ArgumentParser(description="ROM -> Xbox disc image for Diddy Kong Racing")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--config", default="Release", choices=["Release", "Debug"])
    ap.add_argument("--unlock-all", action="store_true",
                    help="with no save on the console yet, start from a 100%% completed one")
    a = ap.parse_args()
    if sys.version_info < (3, 8):
        die("Python 3.8 or later is needed")
    if a.check:
        sys.exit(0 if check() else 1)

    version, why, rom = rom_check(write=True)
    if version is None:
        die(why)
    say(why)
    os.makedirs(BUILD, exist_ok=True)
    step_tool()
    step_assets(version, rom)
    step_data()
    step_xbox(version, a.unlock_all)
    if not a.skip_build:
        step_build(a.config)


if __name__ == "__main__":
    main()
