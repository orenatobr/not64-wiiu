## not64 for Wii U 0.6.0: textures and colours are scaled correctly

**The fix.** The renderer converted integers to floats with Gekko paired-single instructions that rely on
registers a Wii U title never sets up. Texture scales stayed at 32768 instead of 0.5 and 8 bit colours stayed in
0..255 instead of 0..1, so textured triangles sampled far outside their textures and lighting saturated. That is
the cause of the "blocks of flat colour" in Banjo-Kazooie and Banjo-Tooie that earlier notes listed as not found,
and part of the grain. The conversions are now plain C (`glN64_GX/GXcast.h`) with a host test.

Seen in Cemu 2.6 on an Apple M series Mac: the Banjo-Tooie title screen shows its logo and textured terrain
with no coloured blocks, and the Jet Force Gemini title screen is textured and lit. Conker was not clearly
different in the frames captured. Other games were not re-checked.

Read this before testing:

* **Tested in Cemu only, never on a Wii U.**
* Lighting changes everywhere (colours were saturated before), so scenes look darker and more contrasted than
  in earlier releases. That is expected; nobody compared them to a real console.
* Speed is unchanged: the interpreter is still the limit and the recompiler is off (docs/DYNAREC.md).
* The zips hold the title in the Cemu/Loadiine layout, not an encrypted install package; encrypting needs your
  own Wii U common key (wiiu/README.md). No game is ever put in the title or in a release.

See wiiu/README.md and docs/ACCURACY.md.
