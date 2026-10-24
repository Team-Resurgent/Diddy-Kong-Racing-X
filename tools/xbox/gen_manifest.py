#!/usr/bin/env python3
"""Write rxdk.project.json: what Rxdk.Cli builds Diddy-Kong-Racing-X from.

The sources are tools/xbox/xbconfig.py's: the game and its libultra (the
Linux port's lists, Makefile.pc), the host layer the Xbox shares with the PC
test harness, and the Xbox's own files. So are the compile settings.

    python tools/xbox/gen_manifest.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbconfig  # noqa: E402

ROOT = xbconfig.ROOT
PROJECT_NAME = "Diddy-Kong-Racing-X"  # the ISO is out/<Config>/XISO/Diddy-Kong-Racing-X.iso
# Team Resurgent TitleID-DB: the next after Metal Gear X (54520017)
TITLE_ID = "0x54520018"
TITLE_NAME = "Diddy Kong Racing X"

LIBS_RELEASE = ["libd3d8.lib", "libd3dx8.lib", "libxgraphics.lib", "libdsound.lib",
                "libxapi.lib", "libc.lib", "libcpp.lib", "libkernel.lib"]
LIBS_DEBUG = ["libxbdmd.lib", "libd3d8d.lib", "libd3dx8d.lib", "libxgraphicsd.lib",
              "libdsoundd.lib", "libxapid.lib", "libcd.lib", "libcppd.lib", "libkerneld.lib"]


def main():
    sources = (xbconfig.XBOX_SOURCES + xbconfig.HOST_SOURCES + xbconfig.GAME_SOURCES
               + xbconfig.LIBULTRA_SOURCES)
    for s in sources:
        if not os.path.isfile(os.path.join(ROOT, s)):
            raise SystemExit("gen_manifest: %s is missing" % s)

    # RXDK's Visual Studio build names each object after its file alone
    seen = {}
    for s in sources:
        b = os.path.basename(s).lower()
        if b in seen:
            raise SystemExit("gen_manifest: %s and %s share a file name; RXDK's Visual Studio "
                             "build would overwrite one object with the other" % (seen[b], s))
        seen[b] = s

    def cfg(debug):
        return {
            "configuration": "debug" if debug else "release",
            # the dashboard icons: BMPs (tools/xbox/make_icons.py) bundled
            # to DXT1 .xbx before the link
            "resources": ["Platform/xbox/titleimage.rdf", "Platform/xbox/saveimage.rdf"],
            "sources": sources,
            "libraries": LIBS_DEBUG if debug else LIBS_RELEASE,
            "includePaths": xbconfig.INCLUDE_PATHS,
            # DKRX_DEBUG_BUILD: so the program can say which build it is
            "defines": xbconfig.defines() + (["DKRX_DEBUG_BUILD"] if debug else []),
            "compileFlags": xbconfig.COMPILE_FLAGS,
            "cppStandard": "c++20",
            "exceptions": False,
            "deployPaths": ["data"],
            "forceCopy": True,
            "createIso": True,
            "outputDir": "out/Debug" if debug else "out/Release",
            "imageBuild": {
                "stackSize": 1048576,
                "debug": debug,
                "noLogo": True,
                "noLibWarn": True,
                "testId": TITLE_ID,
                "testName": TITLE_NAME,
                "testVersion": "1",
                "titleImage": "Platform/xbox/gen/titleimage.xbx",
                "defaultSaveImage": "Platform/xbox/gen/saveimage.xbx",
            },
        }

    manifest = {
        "name": PROJECT_NAME,
        "type": "executable",
        "defaultConfiguration": "Release",
        "configurations": {"Debug": cfg(True), "Release": cfg(False)},
    }
    out = os.path.join(ROOT, "rxdk.project.json")
    text = json.dumps(manifest, indent=2) + "\n"
    old = open(out, encoding="utf-8").read() if os.path.isfile(out) else None
    if old != text:  # unchanged, it keeps its time (the RXDK extension watches it)
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("gen_manifest: %d sources (%d game, %d libultra, %d platform), %s"
          % (len(sources), len(xbconfig.GAME_SOURCES), len(xbconfig.LIBULTRA_SOURCES),
             len(xbconfig.XBOX_SOURCES) + len(xbconfig.HOST_SOURCES),
             xbconfig.rom_version()))


if __name__ == "__main__":
    main()
