## not64 for Wii U (preview 4)

New in this build:

* **Games see the controller.** Before, they reported "NO CONTROLLER". Only ports with a connected controller
  answer now.
* New **Auto** internal resolution (Settings, Video): 2x on a 720p TV, 3x on 1080p. The default stays 2x.
* Measured against five heavy PAL games in Cemu (Banjo-Tooie, Conker, Jet Force Gemini, Pokemon Stadium 2,
  Majora's Mask): four render, three run at full speed (50 VI/s), Conker at 20 to 25, and **Majora's Mask
  shows a black screen**. Details in docs/ACCURACY.md.

Read this before testing:

* **Tested in Cemu only, never on a Wii U.** Speeds and audio are from Cemu, audio was checked by the numbers
  and not by ear.
* Games run on the interpreter; the code recompiler is off (it still misbehaves in real games, see
  docs/DYNAREC.md). Heavy games such as Conker are slow.
* Known picture issues: grain (partly the games' own dithering, no video-interface filter yet) and a few stray
  green rectangles.

See wiiu/README.md.
