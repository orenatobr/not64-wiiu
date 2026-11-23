# not64 for Wii U

A native Wii U (Aroma) port of [not64](https://github.com/extremscorner/not64), the Nintendo 64
emulator for GameCube and Wii that descends from Wii64 and Mupen64GC. The interface is built on the
framework of dborth's GX emulator ports ([snes9xgx](https://github.com/dborth/snes9xgx),
[fceugx](https://github.com/dborth/fceugx), [vbagx](https://github.com/dborth/vbagx)), so it looks
and behaves like them.

Until now not64 ran on the Wii U only inside vWii, with the Wii's 729 MHz CPU and its fixed-function
GPU. This port runs on the Wii U's own Espresso CPU and Latte GPU.

## Status

This is a work in progress. Read this section before downloading anything.

| Part | State |
|------|-------|
| Build, packaging (`.wuhb`), CI, release | done, builds in CI |
| dborth UI: game browser, previews, settings, button mapping, save states, languages | done, compiles and links |
| Emulator core (R4300 interpreters, RSP HLE, memory, saves, states) on wut | done, compiles and links, ROM loading and save backends have host unit tests |
| Audio (AI to AX), controllers (GamePad, Pro, Wiimote, Classic, GameCube, USB) | written, not run on hardware (audio device is not exercised in Cemu either) |
| 3D renderer (glN64 on GX2, TEV ubershader in Latte assembly) | **runs in Cemu**: a homebrew demo and the Banjo-Kazooie intro and first level draw with correct colours and textures. Known artefacts: grainy textures and a few stray green rectangles. See `docs/GX2_RENDERER.md` |
| Code recompiler (PowerPC to PowerPC) | compiled in but **off**: it needs the OS "codegen" area, see below |

**Verified in Cemu 2.6 (macOS, Apple Silicon, under Rosetta), never on a Wii U.** The menu, ROM browser, game
start and rendering work there at 60 FPS in the menu and 16 to 20 FPS in 3D games (the interpreter is slow
and the recompiler is off). Real hardware can differ: Cemu is not a Wii U. How to repeat the Cemu runs is in
`docs/TESTING_CEMU.md`.

### About the recompiler

The Wii U does not let an application make memory executable. The only place for generated code is
the "codegen" area, which the OS lends to titles whose `cos.xml` permits it, and only on one
specific CPU core. Whether a title launched through Aroma gets it depends on your setup. The port
therefore ships with the interpreters (the pure interpreter is the default), and the recompiler
turns on by itself only on a system that reports a usable codegen area and only once the
recompiler allocates from it. Until then expect slower games than on a native recompiler.

## Install

1. Set up Aroma on your Wii U (see https://wiiu.hacks.guide).
2. Copy `wiiu/apps/not64.wuhb` from the release zip to `sd:/wiiu/apps/` on the SD card.
3. Put your ROMs in `sd:/not64/roms/` (`.z64`, `.n64`, `.v64`, `.rom`, also zipped or in 7z).
4. Start not64 from the Aroma menu.

Folders (all under `sd:/not64/`, or the same on USB):

| Folder | What |
|--------|------|
| `roms` | your games |
| `saves` | game saves (`.eep`, `.sra`, `.fla`, `.mpk`) and save states (`.st0` to `.st9`) |
| `screenshots`, `covers`, `artwork` | pictures shown beside the game list |
| `settings.xml` | the settings |

Game saves use the same file names as not64 on the Wii, so you can copy them across.

## Controls

The menu is the same as in the other dborth emulators. In a game:

| N64 | Pro Controller / GamePad |
|-----|--------------------------|
| Analog stick | left stick |
| A, B | A, B |
| Z | ZL or ZR |
| Start | + |
| L, R | L, R |
| C buttons | right stick (or X and Y) |
| D-pad | D-pad |
| Open the menu | HOME, or L + R + + |

Every mapping can be changed in Settings, Button Mappings. Up to four players, each with a Memory
Pak, a Rumble Pak or none.

## Build

You need Docker. Nothing else is installed on the host.

    docker build -f docker/Dockerfile.wiiu -t not64-wiiu-builder docker
    docker run --rm -v "$PWD":/project not64-wiiu-builder make -f Makefile.wiiu -j8
    # executables/not64-wiiu.wuhb

    docker run --rm -v "$PWD":/project not64-wiiu-builder make -f Makefile.wiiu test

The image is the one dborth's CI uses (devkitPPC, libogc2, wut) plus libmocha, libdvm and libsmb2,
each pinned to a commit.

## How it is put together

    wiiu/source/            app, menus, settings, input mapping (dborth framework, adapted)
      drivers/              platform layer (GX2 video, AX audio, VPAD/KPAD input, filesystem)
      core/                 glue between the app and the N64 core (see below)
    r4300/ gc_memory/ ...   the original emulator core, shared with the Wii and GameCube builds
    Makefile.wiiu           the Wii U build

`wiiu/source/core` holds what replaces the Wii hardware layer: a libogc compatibility layer
(`compat/`), a run-time arena in place of the Wii's fixed MEM2 map, a ROM cache that keeps the whole
ROM in RAM (no streaming), save callbacks on stdio, the audio and controller plugins, and
`core_wiiu.c`, the only interface the app uses. The original core files carry only small
`#ifdef __WIIU__` changes so they still build for the Wii and GameCube.

## Credits

See `wiiu/THIRD_PARTY.md`. not64: Extrems and the Wii64 team. Interface and Wii U drivers: Daryl
Borth. This port: orenatobr. Licensed under the GNU GPL v2.
