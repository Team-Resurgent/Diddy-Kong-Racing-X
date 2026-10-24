"""The Xbox build's single description of how Diddy Kong Racing is compiled.

Rxdk.Cli compiles the game from the manifest tools/xbox/gen_manifest.py
writes; tools/xbox/hosttest/build.py compiles the same game the same way for
the PC test harness, and tools/xbox/cc_check.py lists every compile error at
once. All of them take the sources, defines, include paths and flags from here.

The game is the decompilation's, built the way the repository's Linux port
builds it (Makefile.pc): the N64 code with TARGET_PC, its libultra minus the
hardware drivers, and a host layer underneath. The host layer is the port's
own (Platform/xbox/dkr_*.c, shared with the harness), and so is everything the
Xbox draws, plays and reads pads and saves with (Platform/xbox/xbox_*.c,
gfx_xbox.c). The Linux port's audio microcode interpreter, linux/audio_hle.c,
is used as it is.

The ROM's version picks the game's: setup.py writes it to build/dkr_version
("us.v77" for US 1.0, "us.v80" for US 1.1), and the decomp's VERSION_* define
follows it (include/common.h).

The host side (dkr_assets_tool, which extracts the ROM and builds the asset
files and include/asset_enums.h) is built here too, with the RXDK's clang
against its MinGW host sysroot.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSION_FILE = os.path.join(ROOT, "build", "dkr_version")

# ------------------------------------------------------------------ sources

# Makefile.pc's game sources, without linux/ (the Linux host layer).
GAME_SOURCES = [
    "src/asset_loading.c", "src/audio.c", "src/audiomgr.c", "src/audiosfx.c",
    "src/audio_spatial.c", "src/audio_vehicle.c", "src/borders.c", "src/camera.c",
    "src/fade_transition.c", "src/font.c", "src/game.c", "src/game_text.c",
    "src/game_ui.c", "src/get_stack_pointer.c", "src/gzip.c", "src/joypad.c",
    "src/lights.c", "src/main.c", "src/memory.c", "src/menu.c",
    "src/object_functions.c", "src/object_models.c", "src/objects.c",
    "src/os_yield_data.c", "src/particles.c", "src/printf.c", "src/racer.c",
    "src/rcp_dkr.c", "src/save_data.c", "src/screen_asset.c",
    "src/set_rsp_segment.c", "src/textures_sprites.c", "src/thread0_epc.c",
    "src/thread30_bgload.c", "src/thread3_main.c", "src/tracks.c",
    "src/unused_string.c", "src/vehicle_bluey.c", "src/vehicle_bubbler.c",
    "src/vehicle_rocket.c", "src/vehicle_smokey.c", "src/vehicle_tricky.c",
    "src/vehicle_wizpig.c", "src/video.c", "src/waves.c", "src/weather.c",
    "src/hasm/collision.c", "src/hasm/math_util.c", "src/hasm/obj_animate.c",
    "src/hasm/obj_shade_fast.c",
]

# Makefile.pc's libultra, less libc/string.c: the C library's own string
# functions are used, as the Dreamcast build does.
LIBULTRA_SOURCES = ["libultra/src/" + s for s in [
    "audio/bnkf.c", "audio/cents2ratio.c", "audio/copy.c", "audio/cseq.c",
    "audio/cspgetchlvol.c", "audio/cspgetstate.c", "audio/cspgettempo.c",
    "audio/cspplay.c", "audio/cspsetbank.c", "audio/cspsetchlvol.c",
    "audio/cspsetpan.c", "audio/cspsetseq.c", "audio/cspsettempo.c",
    "audio/cspsetvol.c", "audio/cspstop.c", "audio/env.c", "audio/event.c",
    "audio/filter.c", "audio/heapalloc.c", "audio/heapinit.c", "audio/load.c",
    "audio/mainbus.c", "audio/mips1/auxbus.c", "audio/mips1/cspgetfadein.c",
    "audio/mips1/csplayer.c", "audio/mips1/cspsetfadein.c", "audio/mips1/drvrnew.c",
    "audio/mips1/reverb.c", "audio/mips1/seqchannel.c", "audio/mips1/seqplayer.c",
    "audio/mips1/synallocfx.c", "audio/mips1/synsetpan.c",
    "audio/mips1/synstartvoiceparam.c", "audio/mips1/synthesizer.c",
    "audio/mips1/unknown_064690.c", "audio/resample.c", "audio/save.c",
    "audio/seqpgetfxmix.c", "audio/sl.c", "audio/synaddplayer.c",
    "audio/synallocvoice.c", "audio/syndelete.c", "audio/synfreevoice.c",
    "audio/synsetfxmix.c", "audio/synsetpitch.c", "audio/synsetpriority.c",
    "audio/synsetvol.c", "audio/synstartvoice.c", "audio/synstopvoice.c",
    "debug/assert.c", "debug/kdebugserver.c", "gu/mtxutil.c", "gu/perspective.c",
    "io/crc.c", "io/devmgr.c", "io/dp.c", "io/dpgetstat.c", "io/dpsetnextbuf.c",
    "io/dpsetstat.c", "io/epirawdma.c", "io/epirawwrite.c", "io/leointerrupt.c",
    "io/piacs.c", "io/pidma.c", "io/pigetcmdq.c", "io/pimgr.c", "io/pirawdma.c",
    "io/pirawread.c", "io/si.c", "io/siacs.c", "io/sirawread.c", "io/sirawwrite.c",
    "io/sp.c", "io/spgetstat.c", "io/sprawdma.c", "io/spsetpc.c", "io/spsetstat.c",
    "io/sptask.c", "io/sptaskyield.c", "io/sptaskyielded.c", "io/vi.c",
    "io/viblack.c", "io/vigetcurrcontext.c", "io/vigetcurrframebuf.c",
    "io/vigetnextframebuf.c", "io/vimgr.c", "io/visetevent.c", "io/visetmode.c",
    "io/visetspecial.c", "io/viswapbuf.c", "io/viswapcontext.c", "io/vitbl.c",
    "libc/ldiv.c", "libc/ll.c", "libc/llcvt.c", "libc/rmonPrintf.c",
    "libc/xldtob.c", "libc/xlitob.c", "libc/xprintf.c",
    "os/createmesgqueue.c", "os/createthread.c", "os/destroythread.c",
    "os/getactivequeue.c", "os/getthreadpri.c", "os/gettime.c", "os/initialize.c",
    "os/jammesg.c", "os/recvmesg.c", "os/resetglobalintmask.c", "os/sendmesg.c",
    "os/seteventmesg.c", "os/sethwinterrupt.c", "os/setthreadpri.c",
    "os/settime.c", "os/settimer.c", "os/startthread.c", "os/stopthread.c",
    "os/thread.c", "os/timerintr.c", "os/virtualtophysical.c",
    "os/yieldthread.c",
]] + [
    # libultra/src/sc/sched.c, through a file that includes its header first
    # (see it: the SDK's POSIX sched.h is found before libultra's)
    "Platform/xbox/libultra_sched.c",
]

# The host layer the Xbox and the harness share, with the two pieces of the
# other ports it uses as they are: the Linux port's audio microcode interpreter
# and the Dreamcast port's 60 fps frame interpolation (the game's physics wants
# a 30 Hz step; the frames between are drawn part-way between two steps).
HOST_SOURCES = [
    "Platform/xbox/dkr_audio.c", "Platform/xbox/dkr_boot.c",
    "Platform/xbox/dkr_dl.c", "Platform/xbox/dkr_host.c",
    "Platform/xbox/dkr_menu.c", "Platform/xbox/dkr_overlay.c",
    "Platform/xbox/dkr_scene.c", "Platform/xbox/dkr_settings.c",
    "linux/audio_hle.c", "dreamcast/frame_interp.c",
]

# The Xbox's own.
XBOX_SOURCES = [
    "Platform/xbox/gfx_xbox.c", "Platform/xbox/xbox_audio.c",
    "Platform/xbox/xbox_input.c", "Platform/xbox/xbox_main.c",
    "Platform/xbox/xbox_save.c",
]

# ------------------------------------------------------------- the version

def rom_version():
    """"us.v77" or "us.v80": the ROM's, as setup.py found it (US 1.0 when it
    has not run yet)."""
    try:
        with open(VERSION_FILE, encoding="utf-8") as f:
            v = f.read().strip()
        if v in ("us.v77", "us.v80"):
            return v
    except OSError:
        pass
    return "us.v77"


def unlock_all():
    """setup.py --unlock-all: a console with no save yet starts from the
    100%-completion EEPROM image (build/dkr_unlock_all exists)."""
    return os.path.isfile(os.path.join(ROOT, "build", "dkr_unlock_all"))


# ------------------------------------------------------- compile settings

# Makefile.pc's defines (VERSION_* from the ROM), plus TARGET_XBOX for the
# port's own. 60 frames a second: src/video.c steps the game by the retrace
# periods each frame covers, as the Dreamcast build's FRAME_FIELDS=1 does.
def defines():
    d = [
        "TARGET_PC",
        "TARGET_XBOX=1",
        "_LANGUAGE_C",
        "_FINALROM",
        "NDEBUG",
        "F3DDKR_GBI",
        "VERSION_" + rom_version().replace(".", "_"),
        "AVOID_UB",
        "NON_MATCHING",
        "BUILD_VERSION=VERSION_G",
        "_MIPS_SZLONG=32",
        # the game's heap (src/memory.c): the N64 has 4 MB in all, and the
        # Dreamcast port runs in 3; the Linux port's 16 would be a quarter of
        # the Xbox's memory (the races, one and four players, run in 4)
        "PC_MAIN_POOL_BYTES=8388608",
        "GBI_FLOAT_MTX",
        # (Makefile.pc's AUDIO_NO_REVERB left out: the N64's reverb -- the
        # echo in caves and tunnels -- was dropped upstream to save the
        # Dreamcast's CPU; here it costs next to nothing)
        "DKR_LOGIC_RATE=1",
        # src/printf.c has the game's own sprintf and vsprintf, which the C
        # library's printf module defines too (pulled in by vsnprintf): the
        # game's are renamed, and the game keeps using them
        "sprintf=dkr_sprintf",
        "vsprintf=dkr_vsprintf",
    ]
    if unlock_all():
        d.append("EEPROM_PRESET_100")
    return d


# Makefile.pc's INCLUDES_REIMPL (the game's own libc headers left out: the C
# library's are used, as on the Dreamcast), then the platform's.
INCLUDE_PATHS = [
    ".", "include", "include/PR", "include/sys", "assets", "src", "src/hasm",
    "libultra", "libultra/src/gu", "libultra/src/io", "libultra/src/sc",
    "libultra/src/audio", "libultra/src/os", "Platform/xbox",
]

# For the game, Makefile.pc's flags in clang's words; for the Xbox, RXDK's.
COMPILE_FLAGS = [
    # The RXDK searches its SDK headers before the project's, and two of the
    # game's own share a name with the C library's: libultra's sched.h
    # (include/PR) and the game's memory.h (src). The game includes both in
    # quotes, so these, searched before any -I, put the game's first.
    # Relative: compiles run in the project root.
    "-iquote", "include/PR", "-iquote", "src",
    # the decomp is C23 (alignas, as Makefile.pc's -std=gnu23)
    "-std=gnu23",
    # RXDK's -fms-compatibility switches clang to MSVC bitfield layout, which
    # changes the size of a struct mixing bitfield types; the game's structs
    # (and the asset data they map) need GCC's.
    "-mno-ms-bitfields",
    "-funsigned-char",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-common",
    # Single-precision float maths in SSE (the P3 has SSE but not SSE2).
    "-msse", "-mfpmath=sse",
    # The decomp is GCC-clean; clang makes several of these errors.
    "-Wno-int-conversion",
    "-Wno-incompatible-pointer-types",
    "-Wno-incompatible-function-pointer-types",
    "-Wno-error=implicit-function-declaration",
    "-Wno-implicit-function-declaration",
    "-Wno-implicit-int",
    "-Wno-unused-variable",
    "-Wno-unused-but-set-variable",
    "-Wno-unused-value",
    "-Wno-parentheses",
    "-Wno-missing-braces",
    "-Wno-switch",
    "-Wno-unknown-pragmas",
    "-Wno-unknown-warning-option",
    "-Wno-pragma-pack",
    "-Wno-microsoft-anon-tag",
    "-Wno-macro-redefined",
    "-Wno-int-to-pointer-cast",
    "-Wno-pointer-to-int-cast",
    "-Wno-deprecated-declarations",
    "-Wno-deprecated-non-prototype",
    "-Wno-implicit-fallthrough",
    "-Wno-format",
    "-Wno-c23-extensions",
    "-Wno-gnu-folding-constant",
    "-Wno-shift-count-overflow",
    "-Wno-constant-conversion",
    "-Wno-tautological-constant-out-of-range-compare",
    "-Wno-dangling-else",
    "-Wno-return-type",
    # i686-pc-windows-gnu predefines these; nothing in this game is Win32.
    "-U_WIN32", "-U_WIN64", "-U__MINGW32__",
]

# --------------------------------------------------------------- the RXDK


def rxdk_root():
    """%ProgramData%\\RXDK, or $RXDK_ROOT."""
    if os.environ.get("RXDK_ROOT"):
        return os.environ["RXDK_ROOT"]
    pd = os.environ.get("ProgramData") or r"C:\ProgramData"
    return os.path.join(pd, "RXDK")


def rxdk_cli():
    staged = os.environ.get("RXDK_STAGED_TOOLS")
    if staged:
        return os.path.join(staged, "Rxdk.Cli.exe")
    return os.path.join(rxdk_root(), "tools", "Rxdk.Cli.exe")


def _rxdk_roots():
    roots = [rxdk_root()]
    local = os.environ.get("LOCALAPPDATA")
    if local:
        roots.append(os.path.join(local, "RXDK"))
    return [r for r in roots if os.path.isdir(r)]


_clang_cache = []


def clang():
    """The RXDK's own clang: $RXDK_CLANG, the known layouts, then any
    clang.exe under the RXDK's folders. A path that does not exist when there
    is none."""
    if _clang_cache:
        return _clang_cache[0]
    found = None
    env = os.environ.get("RXDK_CLANG", "").strip()
    if env and os.path.isfile(env):
        found = env
    if found is None:
        for root in _rxdk_roots():
            for pattern in (os.path.join(root, "llvm", "*", "bin", "clang.exe"),
                            os.path.join(root, "llvm", "bin", "clang.exe"),
                            os.path.join(root, "tools", "llvm", "*", "bin", "clang.exe"),
                            os.path.join(root, "tools", "llvm", "bin", "clang.exe")):
                hits = sorted(glob.glob(pattern))
                if hits:
                    found = hits[-1]
                    break
            if found:
                break
    if found is None:
        for root in _rxdk_roots():
            for dirpath, dirnames, filenames in os.walk(root):
                if dirpath[len(root):].count(os.sep) >= 5:
                    dirnames[:] = []
                if "clang.exe" in filenames:
                    found = os.path.join(dirpath, "clang.exe")
                    break
            if found:
                break
    if found is None:
        found = os.path.join(rxdk_root(), "llvm", "xbox-windows-x64", "bin", "clang.exe")
    _clang_cache.append(found)
    return found


def clangxx():
    return os.path.join(os.path.dirname(clang()), "clang++.exe")


def llvm_root():
    """The LLVM install the RXDK's clang is in (the folder above its bin)."""
    return os.path.dirname(os.path.dirname(clang()))


def sdk_include():
    return os.path.join(rxdk_root(), "sdk", "include")


# What Rxdk.Cli puts in front of every C compile (its build log), minus the
# optimisation level and debug info. cc_check.py uses it when it cannot read
# the installed RXDK's own line.
def rxdk_base_flags():
    return ["-fno-sanitize=undefined", "-ffreestanding", "-fno-stack-protector",
            "-fms-extensions", "-fms-compatibility", "-nostdinc",
            "-include", "picolibc.h", "-march=pentium3",
            "-D_XBOX", "-DXBOX", "-fno-builtin", "-U_DEBUG", "-D__ASSERT_VERBOSE",
            "-femulated-tls", "-mlong-double-64",
            "-I", sdk_include(),
            "-Wno-c++11-narrowing", "-Wno-address-of-temporary",
            "-Wno-ignored-pragma-intrinsic", "-Wno-multichar",
            "-Wno-unused-command-line-argument", "-Wno-deprecated-enum-enum-conversion"]


def _split_cmdline(line):
    """A command line as Rxdk.Cli echoes it, into arguments (Windows rules,
    as far as its echo needs: quotes group, and are dropped)."""
    args, cur, quoted, have = [], [], False, False
    for ch in line:
        if ch == '"':
            quoted = not quoted
            have = True
        elif ch in " \t" and not quoted:
            if have:
                args.append("".join(cur))
            cur, have = [], False
        else:
            cur.append(ch)
            have = True
    if have:
        args.append("".join(cur))
    return args


_probe_lock = threading.Lock()
_probe_cache = []


def rxdk_compile_prefix():
    """The compiler and the flags the INSTALLED RXDK compiles C with, read
    off its own build of a one-line probe (Rxdk.Cli build --compile-only
    echoes each compile as "$ <command>"): everything in front of its "-x c
    -c <source>". None when the probe fails (rxdk_base_flags() is used then)."""
    with _probe_lock:
        if _probe_cache:
            return _probe_cache[0]
        prefix = None
        cli = rxdk_cli()
        tmp = tempfile.mkdtemp(prefix="dkrx_rxdk_probe")
        try:
            if os.path.isfile(cli):
                with open(os.path.join(tmp, "probe.c"), "w", encoding="utf-8") as f:
                    f.write("int dkrx_rxdk_probe;\n")
                with open(os.path.join(tmp, "rxdk.project.json"), "w", encoding="utf-8") as f:
                    f.write('{ "name": "probe", "defaultConfiguration": "Release", "configurations": '
                            '{ "Release": { "configuration": "release", "sources": ["probe.c"], '
                            '"libraries": [] } } }\n')
                r = subprocess.run([cli, "build", "--project-root", tmp, "--configuration", "Release",
                                    "--compile-only"], capture_output=True, text=True,
                                   encoding="utf-8", errors="replace")
                for line in (r.stdout + "\n" + r.stderr).splitlines():
                    line = line.strip()
                    if not line.startswith("$ ") or "probe.c" not in line:
                        continue
                    args = _split_cmdline(line[2:])
                    if "-x" in args and args and os.path.isfile(args[0]):
                        prefix = args[:args.index("-x")]
                    break
        except OSError:
            prefix = None
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        _probe_cache.append(prefix)
        return prefix


def compile_command(src, obj, extra=()):
    """The clang command line RXDK compiles `src` with (cc_check.py)."""
    cmd = rxdk_compile_prefix()
    if cmd is not None:
        cmd = list(cmd)
    else:
        if not os.path.isfile(clang()):
            raise SystemExit("no Xbox C compiler: the RXDK's clang (%s) was not found" % clang())
        res = None
        try:
            r = subprocess.run([clang(), "-print-resource-dir"], capture_output=True, text=True)
            res = os.path.join(r.stdout.strip(), "include")
        except OSError:
            pass
        cmd = [clang(), "-target", "i686-pc-windows-gnu"] + (["-isystem", res] if res else [])
        cmd += rxdk_base_flags()
    for p in INCLUDE_PATHS:
        cmd += ["-I", os.path.join(ROOT, p)]
    cmd += ["-D" + d for d in defines()]
    cmd += COMPILE_FLAGS + list(extra)
    cmd += ["-x", "c", "-c", src, "-o", obj]
    return cmd


# ------------------------------------------------------------ host tools

def host_sysroot():
    sr = os.path.join(llvm_root(), "host-sysroot")
    return sr if os.path.isdir(sr) else None


def host_flags():
    """clang flags that build a 64-bit Windows program through the RXDK's
    MinGW host sysroot (compiler-rt and libunwind: it ships no libgcc)."""
    sr = host_sysroot()
    if sr is None or not os.path.isfile(clang()):
        return None
    flags = ["--target=x86_64-w64-mingw32", "-fuse-ld=lld", "--sysroot=" + sr,
             "--rtlib=compiler-rt", "--unwindlib=libunwind"]
    rd = sorted(glob.glob(os.path.join(sr, "lib", "clang", "*")))
    if rd:
        flags.append("-resource-dir=" + rd[-1])
    return flags


def host_cc_available():
    """What builds the host tools, for --check."""
    return "RXDK clang (MinGW host sysroot)" if host_flags() is not None else None
