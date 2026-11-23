## not64 for Wii U (preview 5): installable title (WUP)

* **New: `not64-wiiu-wup.zip`**, the app as a Wii U title (`code`, `content`, `meta`, with a `cos.xml` that
  asks the OS for a codegen area). Cemu and Loadiine read it as it is. To install it on a console, encrypt it
  with NUSPacker and your own common key (`tools/make_wup.py`, see wiiu/README.md); the key is not shipped.
* **New: `not64-conker-wup.zip`**, the same title named Conker's Bad Fur Day (title id 000500001e64c0ab) with
  `sd/not64/autoboot.txt` pointing at `roms/Conker.z64`. Copy your own ROM there; none is included.
* **Start straight in a game.** If `sd:/not64/autoboot.txt` names a ROM (for example `roms/Conker.z64`), the
  app opens it without the game browser. `tools/make_wup.py --rom` writes that file and a copy of your ROM for
  the SD card. No game is ever put in the title or in a release.
* Why not a Virtual Console inject: the UWUVCI N64 list reports Conker's Bad Fur Day as not working there
  (black screen or no picture). This port runs it on its own renderer instead.

Read this before testing:

* The zips hold the title in the Cemu/Loadiine layout. An installable, encrypted WUP needs your own Wii U common
  key, so it cannot be built or shipped here (steps in wiiu/README.md).
* **Tested in Cemu only, never on a Wii U.** The WUP layout and `cos.xml` were written from the public format;
  the title itself was not started in Cemu in this release, and the autoboot path was only compiled.
* Conker runs at about half speed on the interpreter. The recompiler is still off: it has a bug in real games
  (docs/DYNAREC.md).

See wiiu/README.md.
