# Testing in Cemu

How the Cemu runs behind the README were done, so you can repeat them. Nothing here needs a Wii U.

1. Build: `docker run --rm -v "$PWD":/project not64-wiiu-builder make -f Makefile.wiiu TEST_INPUT=1`.
   `TEST_INPUT=1` adds a scripted GamePad (see below). Release builds never include it.
2. Cemu 2.6. Its audio API must be set (Options, General settings, Audio: Cubeb). With none set, Cemu calls
   `exit()` while starting the title and sits on "loading...".
3. Run the RPX: `Cemu -g executables/not64-wiiu.rpx`. The emulated SD card is
   `<Cemu data dir>/sdcard/`, so put ROMs in `sdcard/not64/roms/`.
4. Scripted input: `sdcard/not64/test_input.txt`, one step per line, `<first frame> <frames held> <BUTTON>`
   with BUTTON one of A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT HOME. The first DOWN wakes the pointer.
   Example that opens the SD card, then `roms`, then the first ROM:

       240 6 DOWN
       300 6 A
       360 6 DOWN
       400 6 DOWN
       440 6 DOWN
       500 6 A
       600 6 DOWN
       660 6 A

5. Logs: Debug, Logging, "Coreinit Logging (OSReport/OSConsole)" shows what the app prints. Debug, Dump,
   Shaders writes the decompiled shaders (delete Cemu's shader cache first so they are recompiled).

The renderer alone (no ROM, no menu) has its own test, `tests/gx2gx/rpx`: it shows a textured triangle and a
full-screen textured quad on the TV. Run it the same way.

Lessons from the first Cemu runs: GX2 keeps its context and command buffer per core, so the renderer must be
created on the thread that draws; a Wii U app has no frame buffer in RAM, so the dummy buffers and the EFB
window the Wii app sets must be set by the glue; a game picture is opaque and must not be blended.

## Measuring and dumping (test builds)

* The profiler prints, every 30 VIs, the VI rate against the region's target (50 or 60) and the time spent in
  the display lists, draws, present and texture upload; the audio plugin prints the queue level and drops.
  Turn on Debug, Logging, "Coreinit Logging (OSReport/OSConsole)" and read Cemu's `log.txt`.
* The first 60 decoded textures are written as PPM files to `sdcard/not64/zdump/` (create the folder first; the
  name sorts last so it does not move the entries the scripted input counts).
* `make DYNAREC_TEST=1` offers the recompiler (off by default, see `docs/DYNAREC.md`).
