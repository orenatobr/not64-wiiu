## not64 for Wii U (preview)

Native Wii U (Aroma) port of not64 with the dborth-style interface.

What is in this build: the full menu (game browser, previews, settings, button mapping, save
states, languages), the N64 emulator core with the interpreters, game saves, audio and controller
plugins.

What is not in this build yet:

* **3D rendering.** The GX2 renderer is still being integrated, so games run but draw no picture.
  This preview is for testing the menus, ROM loading, saves and controls.
* The code recompiler (it needs the OS codegen area, see docs/DYNAREC.md).

Nothing here has been run on a Wii U or in an emulator, it is verified by building, linking and unit
tests. See wiiu/README.md.
