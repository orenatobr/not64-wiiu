## not64 for Wii U (preview 2)

Native Wii U (Aroma) port of not64 with the dborth-style interface.

New in this build: the GX2 3D renderer. glN64 now draws through a GX API layer on GX2, with the N64
combiner evaluated by a pixel shader written in Latte assembly.

Read this before testing:

* **The renderer has never run.** It compiles, links, and its logic passes host tests and a shader
  simulation, but no frame has been drawn on a Wii U or in an emulator. Expect a black or garbled
  picture on first contact. `docs/GX2_RENDERER.md` lists 14 open questions for hardware testing.
* The code recompiler is off (it needs the OS codegen area, see docs/DYNAREC.md), so games run on the
  interpreter and may be slow.
* Menus, ROM loading, saves, states and controls are as in the first preview.

See wiiu/README.md.
