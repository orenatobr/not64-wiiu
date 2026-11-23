## not64 for Wii U (preview 3)

Native Wii U (Aroma) port of not64 with the dborth-style interface, now in N64 green.

New in this build:

* **Games draw.** The GX2 renderer is wired into the app and has now run: a homebrew demo and the
  Banjo-Kazooie intro and first level render in Cemu with correct colours and textures.
* ROMs larger than the first few bytes now load (the file browser did not report file sizes, so every ROM was
  refused with "Unsupported file size!").
* New N64 green theme and an N64 console picture instead of the SNES one.

Read this before testing:

* **Tested in Cemu only, never on a Wii U.** Cemu is not a console, so real hardware can differ.
* Known rendering artefacts: grainy textures and a few stray green rectangles in 3D games.
* Games run on the interpreter (the code recompiler is off, it needs the OS codegen area, see
  docs/DYNAREC.md), about 16 to 20 FPS in 3D in Cemu on an Apple Silicon Mac. Expect better on real hardware
  but do not count on full speed.
* No game audio check in Cemu yet.

See wiiu/README.md and docs/TESTING_CEMU.md.
