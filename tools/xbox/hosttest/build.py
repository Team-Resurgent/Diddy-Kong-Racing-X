#!/usr/bin/env python3
"""Build the PC test harness: build/hosttest/hosttest.exe.

The game and the host layer the Xbox build compiles (tools/xbox/xbconfig.py),
with the harness's main and software rasteriser in place of the Xbox's own
files, as a 32-bit Windows program: the decomp keeps pointers in 32-bit
integers, so it must be one. Compiled by Visual Studio's clang (its "C++ Clang
tools" component) against Visual Studio's 32-bit headers and libraries.

It compiles as the RXDK does, for i686-pc-windows-gnu in Microsoft
compatibility mode: the MSVC target has no GCC struct layout
(-mno-ms-bitfields), which the game's structs need. The objects are linked
as an MSVC program (the C ABI is the same on 32-bit Windows).

    python tools/xbox/hosttest/build.py [-j N]
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import xbconfig  # noqa: E402

ROOT = xbconfig.ROOT
OUT = os.path.join(ROOT, "build", "hosttest")
HARNESS = ["tools/xbox/hosttest/host_main.c", "tools/xbox/hosttest/gfx_soft.c"]


def vs_install():
    """A Visual Studio with its clang: (its folder, its clang.exe)."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.isfile(vswhere):
        r = subprocess.run([vswhere, "-all", "-prerelease", "-products", "*", "-property",
                            "installationPath"], capture_output=True, text=True)
        for line in r.stdout.splitlines():
            c = os.path.join(line.strip(), "VC", "Tools", "Llvm", "x64", "bin", "clang.exe")
            if os.path.isfile(c):
                return line.strip(), c
    raise SystemExit("hosttest: Visual Studio's clang was not found (Visual Studio Installer: "
                     "\"C++ Clang tools for Windows\")")


def vs_env32(vs):
    """INCLUDE and LIB as vcvars32.bat sets them."""
    bat = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvars32.bat")
    r = subprocess.run('cmd /d /s /c ""%s" >nul && set"' % bat, capture_output=True, text=True,
                       shell=True)
    env = {}
    for line in r.stdout.splitlines():
        k, _, v = line.partition("=")
        if k.upper() in ("INCLUDE", "LIB"):
            env[k.upper()] = v
    if "INCLUDE" not in env or "LIB" not in env:
        raise SystemExit("hosttest: vcvars32.bat did not set INCLUDE and LIB (the 32-bit MSVC "
                         "libraries: Visual Studio Installer, \"MSVC ... x64/x86 build tools\")")
    return env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    vs, cc = vs_install()
    env32 = vs_env32(vs)
    res_dir = subprocess.run([cc, "-print-resource-dir"], capture_output=True, text=True).stdout.strip()
    os.makedirs(os.path.join(OUT, "obj"), exist_ok=True)
    flags = ["--target=i686-pc-windows-gnu", "-fms-extensions", "-fms-compatibility",
             "-fms-compatibility-version=19.40", "-mno-stack-arg-probe", "-mlong-double-64",
             "-nostdinc", "-isystem", os.path.join(res_dir, "include"),
             "-D_M_IX86=600", "-D_X86_", "-D_INTEGRAL_MAX_BITS=64", "-D_NO_CRT_STDIO_INLINE",
             "-DWIN32_LEAN_AND_MEAN", "-O2", "-g", "-D_CRT_SECURE_NO_WARNINGS",
             "-Wno-everything", "-include", os.path.join(HERE, "host_compat.h")]
    for p in env32["INCLUDE"].split(";"):
        if p:
            flags += ["-isystem", p]
    flags += [f for f in xbconfig.COMPILE_FLAGS if not f.startswith("-U")]
    flags += ["-D" + d for d in xbconfig.defines()]
    for p in xbconfig.INCLUDE_PATHS:
        flags += ["-I", os.path.join(ROOT, p)]
    sources = xbconfig.GAME_SOURCES + xbconfig.LIBULTRA_SOURCES + xbconfig.HOST_SOURCES + HARNESS
    stamp = " ".join([cc] + flags)

    def comp(src):
        obj = os.path.join(OUT, "obj", src.replace("/", "_") + ".obj")
        dep = obj + ".flags"
        path = os.path.join(ROOT, src)
        if (os.path.isfile(obj) and os.path.getmtime(obj) >= os.path.getmtime(path)
                and os.path.isfile(dep) and open(dep).read() == stamp):
            return obj, None
        r = subprocess.run([cc] + flags + ["-c", path, "-o", obj], capture_output=True, text=True,
                           cwd=ROOT)
        if r.returncode != 0:
            return None, "%s:\n%s" % (src, (r.stdout + r.stderr).strip())
        with open(dep, "w") as f:
            f.write(stamp)
        return obj, None

    with ThreadPoolExecutor(a.j) as ex:
        res = list(ex.map(comp, sources))
    # host_rt.c: compiled for the MSVC target (see the file)
    rt_obj = os.path.join(OUT, "obj", "host_rt.obj")
    r = subprocess.run([cc, "--target=i686-pc-windows-msvc", "-O2", "-c",
                        os.path.join(HERE, "host_rt.c"), "-o", rt_obj], capture_output=True,
                       text=True, env=dict(os.environ, INCLUDE=env32["INCLUDE"]))
    res.append((rt_obj, None) if r.returncode == 0 else (None, "host_rt.c:\n" + r.stdout + r.stderr))
    errs = [e for _, e in res if e]
    for e in errs:
        print(e)
    if errs:
        raise SystemExit("hosttest: %d of %d files failed" % (len(errs), len(sources)))
    exe = os.path.join(OUT, "hosttest.exe")
    env = dict(os.environ)
    env["LIB"] = env32["LIB"]
    # /FORCE:MULTIPLE: the UCRT's x86 math.h defines sinf, cosf and sqrtf
    # __inline, which a GNU-mode compile emits in every file that includes it;
    # the copies are identical
    r = subprocess.run([cc, "--target=i686-pc-windows-msvc", "-fuse-ld=lld", "-g",
                        "-Wl,/STACK:8388608", "-Wl,/FORCE:MULTIPLE"] + [o for o, _ in res]
                       + ["-lkernel32", "-llegacy_stdio_definitions", "-o", exe],
                       capture_output=True, text=True, env=env)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        raise SystemExit("hosttest: link failed")
    print("hosttest: %s" % exe)


if __name__ == "__main__":
    main()
