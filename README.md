# Diddy Kong Racing for the original Xbox

A native original-Xbox port of *Diddy Kong Racing* (N64, 1997), built from
[Bruceleeto/Diddy-Kong-Racing](https://github.com/Bruceleeto/Diddy-Kong-Racing),
the decompilation with its Dreamcast and Linux ports. The game is the
decompiled N64 code, compiled for the Xbox's Pentium III; what it ran on --
the N64's RSP and RDP, its audio microcode, controllers, EEPROM and rumble
paks -- is in `Platform/xbox/`:

* the display lists the game builds every frame are interpreted on the CPU
  (the Linux port's F3DDKR interpreter: transforms, clipping, the RDP's
  colour combiner, texture decoding) and drawn by the NV2A through Direct3D 8;
* the game's own N64 audio manager runs after every frame on the game's
  thread, its audio command lists interpreted in C (the Linux port's HLE
  microcode), and a thread of its own streams the sound out through
  DirectSound;
* the game's physics keep their N64 step, and the frames between are drawn
  part-way between two steps (the Dreamcast port's frame interpolation), so
  it runs at 60 frames a second.

**No game data is included.** You need your own Diddy Kong Racing ROM: the
**USA v1.0 or v1.1** release (`.z64`, or a byte-swapped `.v64` / `.n64`,
which the build puts in order). Everything taken from it is made on your own
machine.

| Version | SHA-1 (`.z64`) |
| --- | --- |
| USA v1.0 | `0cb115d8716dbbc2922fda38e533b9fe63bb9670` |
| USA v1.1 | `6d96743d46f8c0cd0edb0ec5600b003c89b93755` |

## Features

* The whole game: Adventure, Tracks, Trophy races, Time Trial, Battle
  arenas, and four players in split screen
* 60 frames a second, with the game's own 30 Hz physics
* Drawn at the television's resolution, not the N64's 320x240: 720p and 480p
  (progressive or interlaced) following the dashboard, the 4:3 picture with
  black bars on a 16:9 screen, or true widescreen from the settings menu
* The N64's full sound: music, effects, every racer's engine (which the
  decomp's Linux port had left silent) and the reverb in caves and tunnels
  (which its Dreamcast port drops to save CPU)
* Four controllers, rumble on every pad
* Time-trial ghosts kept between sessions: every pad has a Controller Pak
  in it too, kept on the hard disk
* A chase camera that would slip into a wall (in some tunnels) is drawn in
  closer to your racer instead of looking through it
* A settings menu of the port's own, on the right stick's click:
  widescreen, the camera at walls, inverted plane controls, rumble
  strength, 30 or 60 FPS, sharp or smooth textures
* Saves (the three adventure files, best times, options) kept on the Xbox's
  hard disk (dashboard > Memory > Diddy Kong Racing X)
* Optional: start from a 100%-completed save (`--unlock-all`, below)

## Requirements

* Windows 10 or 11
* the RXDK toolchain: in Visual Studio, **RXDK > RXDK Window > Install
  Prerequisites** (or RXDK's own installer), in `C:\ProgramData\RXDK`
* [Python](https://www.python.org/downloads/) 3.8 or later

Nothing else -- no Git, CMake, Make, MSYS, WSL or Docker: the decomp's asset
tool (`tools/dkr_assets_tool_src`, C++) is compiled directly with the RXDK's
own clang and its Windows host libraries, and nothing is downloaded.

`python setup.py --check` lists what is there and what is missing.

## Building

1. Download this repository (**Code > Download ZIP**, or `git clone`).
2. Put your ROM in its folder, named `baserom.z64`.
3. Double-click **`build.bat`**, or in a console:

```bash
python setup.py
```

The result is **`out\Release\XISO\Diddy-Kong-Racing-X.iso`** (about 22 MB).
The first build takes a few minutes; later ones only redo what changed.

The build runs, in order:

1. checks the ROM (SHA-1), puts it in `.z64` order and where the asset tool
   looks for it (`baseroms/baserom.us.v77.z64` or `baserom.us.v80.z64`), and
   notes its version: the game is compiled for it;
2. builds `dkr_assets_tool`, the decomp's asset extractor, for Windows
   (`build/tools/`);
3. runs it on the ROM: every asset extracted (`assets/.vanilla/`), then built
   back into `assets/assets.bin` and `assets.lut.bin` -- checked to be the
   ROM's own bytes -- and `include/asset_enums.h`;
4. stages `data/`, the disc's `D:\data`: the two asset files;
5. makes the dashboard icons from Diddy's portrait in the ROM, and writes
   `rxdk.project.json` and the Visual Studio project;
6. builds with `Rxdk.Cli` and packs the ISO.

```bash
python setup.py --check         # what is present and what is missing
python setup.py --skip-build    # everything but the compile, e.g. to build in Visual Studio
python setup.py --config Debug  # the debug configuration
python setup.py --unlock-all    # a console with no save yet starts from a 100% one
python setup.py --log           # the Release build prints the debug log too
```

The Debug build prints the game's debug log to xbWatson: level loads, every
sound asked for and how it plays, dropped audio events. The Release build
prints only errors, crash reports and its build stamp, unless built with
`--log`. Like `--unlock-all`, the option holds until you build without it.

`--unlock-all` builds in the decomp's 100%-completion EEPROM image
(`include/eeprom_preset.h`): every character, track and mode open. It is only
used when the console has no Diddy Kong Racing save yet; delete the save in
the dashboard to start from it. Build without the option again for the
ordinary game.

### In Visual Studio

Run `python setup.py --skip-build` once, open `Diddy-Kong-Racing-X.sln`,
choose the **Xbox** platform and Build. **F5** runs it on your devkit with the
Xbox Debugger (RXDK). The build lands where the command-line one does:
`out\<Configuration>\XISO\Diddy-Kong-Racing-X.iso`, and the disc's files in
`out\<Configuration>\Build\Diddy-Kong-Racing-X\`.

## Running

* **Disc image:** boot `out\Release\XISO\Diddy-Kong-Racing-X.iso` on a modded
  console or in [xemu](https://xemu.app/).
* **Hard disk:** copy the folder `out\Release\Build\Diddy-Kong-Racing-X\`
  (`default.xbe` and its `data` folder) to your Xbox and launch
  `default.xbe`.

## Controls

| Xbox | N64 | In a race |
| --- | --- | --- |
| Left stick | stick | steer |
| D-pad | D-pad | steer (and the menus) |
| A | A | accelerate; select |
| B | B | brake, reverse; back |
| X or left trigger | Z | fire your weapon |
| Right trigger | R | hop; hold to powerslide |
| Y | C-up | change the camera |
| BLACK | C-down | course directions; the split-screen HUD |
| WHITE | C-right | the speedometer |
| Right stick | C buttons | as above, by direction |
| Right stick click | | the port's settings menu (below) |
| BACK | L | |
| START | START | pause |

Player 1 is the first controller plugged in, player 2 the next, and so on.
**BACK + START + both triggers** returns to the dashboard.

## Settings menu

**Click the right stick** at any time for the port's own settings. The game
stops exactly where it is while the menu is open, mid-race included, and
carries on from there when it closes (**B**, the right stick again, or *Back
to the game*). Up and down choose a setting, left and right (or **A**)
change it. Every setting starts as the game plays without it, and they are
kept on the hard disk (`settings.bin`, beside the save).

| Setting | Choices | |
| --- | --- | --- |
| Widescreen | **Off**, on | on a 16:9 display (the dashboard's widescreen or 720p), the 3D fills the screen and sees more to each side, the race HUD and map move out to the edges (with three or four players, each player's HUD stays over their own view), and menus stay 4:3 in the middle with their backgrounds carried out to the edges (the HUD and fades follow [Golden Balloon](https://github.com/akratch/goldenballoon)'s layout) |
| Camera at walls | **Keep out**, original | a chase camera that would slip into a wall is drawn in closer to your racer; *original* lets it look through |
| Plane up/down | **Normal**, inverted | inverted: push up to climb |
| Rumble | **High**, medium, low, off | |
| Frame rate | **60 FPS**, 30 FPS | 30 is the N64's: each frame a whole physics step, none drawn in between |
| Textures | **Smooth**, sharp | sharp: every texture unfiltered |

## Saving

Diddy Kong Racing saves to the N64 cartridge's EEPROM: the three adventure
files, the best times and the options. Here that is `eeprom.bin` in the save
game **Diddy Kong Racing** on the hard disk, written a moment after the game
saves. The dashboard lists it under **Diddy Kong Racing X** with the game's
icon and can copy or delete it; deleting it starts the game afresh.

On the N64 a controller held a Controller Pak (where the game keeps
time-trial ghosts) or a Rumble Pak, not both. Here every controller has a
pak that is both: it rumbles, and its notes are kept beside the EEPROM as
`cpak1.bin` to `cpak4.bin`, one for each controller port, made the first time
the game saves to that pak. Ghosts are saved to controller 1's pak, as on the
N64. Hold **START** while the game starts for the game's own Controller Pak
screen, to see or delete what is on each pak.

## Testing without a console

`tools/xbox/hosttest/` builds the same game and host layer as a 32-bit
Windows program (with Visual Studio's clang) that runs with no window,
drawing with a software rasteriser, and writes screenshots, the sound and
the EEPROM to files. Scripts drive its controllers, by frame or by menu:

```bash
python tools/xbox/hosttest/build.py
build\hosttest\hosttest.exe --frames 3300 --script tools\xbox\hosttest\race1p.txt --every 150 --out build\shots
python tools/xbox/hosttest/sheet.py build\shots 1500
```

`race1p.txt` goes from the title to a race on Ancient Lake; `race4p.txt`
does the same with four players. `--threaded` plays the sound from a thread
of its own as the Xbox does; `--time` reports the game's own CPU time a
frame. `--wav file.wav` records the sound, `--music N` plays the audio
options' music test N from the title screen, and `--wide` draws a 16:9
display for the widescreen setting.

## How it is put together

| | |
| --- | --- |
| `src/`, `libultra/`, `include/` | the decompilation, compiled as its Linux port compiles it (`TARGET_PC`) |
| `Platform/xbox/dkr_dl.c` | the display-list interpreter (from `linux/main.c`) |
| `Platform/xbox/dkr_host.c` | the N64 OS underneath: the asset "ROM", controllers, EEPROM, Controller and Rumble Paks, timers |
| `Platform/xbox/dkr_audio.c` | the audio interface, running the game's audio manager after each frame |
| `Platform/xbox/dkr_scene.c`, `dkr_boot.c` | the game's hooks, and the way in |
| `Platform/xbox/dkr_menu.c`, `dkr_overlay.c`, `dkr_settings.c` | the settings menu: freezing the game, drawing in HD, and the settings kept on disk |
| `linux/audio_hle.c` | the RSP audio microcode, in C (the Linux port's, unchanged) |
| `dreamcast/frame_interp.c` | 60 fps interpolation (the Dreamcast port's, unchanged) |
| `Platform/xbox/gfx_xbox.c` | Direct3D 8 |
| `Platform/xbox/xbox_*.c` | the entry point and pacer, DirectSound, the pads, the save |
| `tools/xbox/` | `xbconfig.py` (how it is compiled), the manifest and project writers, the icons, `cc_check.py` (every compile error at once), `release_check.py`, `xbe_symbolize.py` (names the addresses in a crash report from the console's debug output) |

The decompilation's own README, with its N64, Dreamcast and Linux builds, is
[README.dreamcast.md](README.dreamcast.md).

## Publishing a fork

`python tools/xbox/release_check.py` fails if anything that would be
committed is a ROM, game data, a build output, or a path on your own machine.

## Credits

* The Diddy Kong Racing decompilation team, and Bruceleeto for the Dreamcast
  and Linux ports this is built from
* The KallistiOS and enDjinn projects behind the Dreamcast port
* The RXDK
* The settings menu's font, [Fredoka](https://github.com/hafontia/Fredoka-One)
  by the Fredoka Project Authors, under the SIL Open Font License 1.1
  (`Platform/xbox/fonts/OFL.txt`)
* [stb_truetype](https://github.com/nothings/stb) by Sean Barrett, which
  draws it (public domain or MIT, `Platform/xbox/third_party/stb_truetype.h`)
* [Golden Balloon](https://github.com/akratch/goldenballoon), the native PC
  port of the same decompilation, whose widescreen this follows: the HUD's
  anchors, the framed views kept 4:3, the fades that cover the display
* Rare, for the game

Diddy Kong Racing is a trademark of Nintendo and Rare. This project is not
affiliated with or endorsed by either.
