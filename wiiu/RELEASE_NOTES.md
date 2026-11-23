## not64 for Wii U (preview 4.1)

* **Majora's Mask now starts** (it showed a black screen). The graphics plugin used 4 MB of memory instead of
  8 MB and dropped display-list data above 4 MB. This can also fix effects in other games.
* Games see the controller, and the optional Auto resolution is there (2x on 720p, 3x on 1080p, default 2x).

Read this before testing:

* **Tested in Cemu only, never on a Wii U.** Four of five heavy PAL games run, three at full speed; Conker at
  about half speed. See docs/ACCURACY.md.
* Games run on the interpreter. The recompiler is off, and on a console it probably cannot run in a plain
  homebrew app at all (it needs a codegen permission), see docs/DYNAREC.md.
* Known picture issues: grain (partly the games' own dithering) and blocks of flat colour in some
  Banjo-Kazooie scenes.

See wiiu/README.md.
