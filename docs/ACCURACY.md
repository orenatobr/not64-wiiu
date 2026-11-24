# Accuracy log

What was run, what it showed and what is still wrong. Everything here was measured in **Cemu 2.6 on a
MacBook (Apple Silicon, under Rosetta)**, never on a Wii U, so absolute speeds say little about a console
and relative ones are what matter. The games are European (PAL, 50 Hz) retail ROMs supplied by the
project owner for testing; they are not part of this repository.

How to repeat the runs: `docs/TESTING_CEMU.md`. The measurements come from the test-only profiler
(`make TEST_INPUT=1`): VI per second, time per part of a frame, audio queue.

## Results

| Game | What it did | Speed (50 VI/s is full speed) | Audio |
|------|-------------|-------------------------------|-------|
| Banjo-Kazooie | intro and first level render with correct colours | 50 in the intro, 16 to 20 in heavier scenes (earlier run) | not measured |
| Banjo-Tooie | in game | 50 | 22 kHz, no dropped blocks |
| Conker's Bad Fur Day | intro and game select, controller recognised | 20 to 25 (about 1500 draw calls per frame) | 22 kHz, played slower with the game |
| Jet Force Gemini | title and intro | 50 | 22 kHz, no dropped blocks |
| Pokemon Stadium 2 | title screen, expansion pak detected | 50 | 32 kHz, no dropped blocks |
| Zelda: Majora's Mask | intro renders (fixed, see below); about 36 VI/s in the first scene | 36 | not measured |

Audio was checked by the numbers (blocks pushed, queue level, sample rate, drops), not by ear.

## Fixed because of these runs

* **Majora's Mask booted to a black screen.** glN64 sized RDRAM at 4 MB (the Expansion Pak flag is only passed to the C core) and rejected every display-list segment base above it. Majora keeps display list data above 4 MB, so segment 1 stayed 0, the list jumped into low memory and read MIPS code as commands forever. Found by watching where the game thread was stuck in Cemu's thread list and tracing the commands. Now 8 MB.
* Games saw **no controller** ("NO CONTROLLER" in Conker). The core's PIF looks for a native GameCube or Wii
  pad unless the port is marked `RawData`; the Wii U port has no such pad. Ports now hand every command to
  the input plugin, and only ports with a connected controller answer (before, all four did).
* ROMs were refused ("Unsupported file size!"): the folder listing never filled in file sizes.
* The renderer never presented a frame in the app: it was created on a loader thread (GX2 keeps its context
  per core), glN64 never got the dummy frame buffers and the 640x480 window the Wii app sets, and the game
  picture was blended with a zero alpha.
* New **Auto** internal resolution: 2x (1280x960) on a 720p TV, 3x (1920x1440) on 1080p, so the 4:3 picture is
  rendered with at least as many lines as it is shown with. The default stays 2x until the 1080p path is
  measured (Cemu only offered 720p here).

## Graphics: what looks wrong and why

* **Fixed in 0.6.0: texture coordinates and colours were not scaled.** The GX build converted integers to
  floats with Gekko paired-single loads (`psq_l`) that depend on registers (GQR2, GQR3) set by the Wii GX
  initialisation. A Wii U title never runs it, so a `G_TEXTURE` scale of 0x8000 became 32768.0 instead of 0.5
  and 8 bit colours stayed in 0..255 instead of 0..1. Every textured triangle then sampled far outside its
  texture. With clamp that gave the **blocks of flat colour** seen in Banjo-Kazooie and Banjo-Tooie (a fixed
  grid of coloured squares), and with wrap it gave high-frequency noise. Found by logging the inputs of the
  texture coordinate formula in a Banjo-Tooie intro frame (`scales=32768.0`) and by forcing wrap, which turned the
  flat blocks into noise instead. The casts in `glN64_GX/GXcast.h` are now plain C, covered by
  `tests/test_gxcast.c`. In Cemu the Banjo-Tooie title screen now shows the logo and textured terrain with no
  blocks, and Jet Force Gemini's title screen is textured and lit. Other games were not re-checked frame by
  frame.
* **Grain** on textures and gradients was explained here as Rare's dithering. Part of it was the bug above (it
  was the same wrong sampling). How much grain is left after the fix, and whether it is the intended dither, was
  not measured. An optional video-interface filter would soften it; it is not written.
* Conker draws about 1500 primitives per frame, each a separate GX2 draw. Submitting them is cheap (about
  5 ms per frame); the glN64 vertex and combiner work in front of them is not.

## Speed: why heavy games are slow

The profiler splits a Conker frame (about 42 ms, so 24 VI/s) into roughly 20 ms of display-list processing
(glN64 on the CPU) and 20 ms of R4300 interpretation plus RSP. Both run on the guest CPU. The N64's CPU is
being **interpreted**: the recompiler is off.

* The cached interpreter is not an option: the Wii U build is compiled with `PPC_DYNAREC`, which leaves the
  cached interpreter without its dispatch table (it crashes on start).
* The recompiler can be tried with `make DYNAREC_TEST=1`. Cemu lends a 16 MiB codegen area, the code cache
  goes there and Hello World runs on it, but Banjo-Kazooie and Conker then never get past boot (the game
  thread sits inside generated code and no display list is ever submitted). It is left off. See
  `docs/DYNAREC.md`.
* On a console the codegen area is execute-only and needs `OSSwitchSecCodeGenMode`/`OSCodegenCopy` around
  writes, on one specific core. That is not implemented.

## Not done yet

* The recompiler (the big speed win), and fast memory access in generated code.
* A video-interface filter (dither, anti-alias, blur) for a closer look to a console picture.
* Everything on real hardware: audio by ear, 1080p output, controllers other than the GamePad.
