# Third-party code in the Wii U port

The native Wii U port (`wiiu/` and `Makefile.wiiu`) builds on the work of several projects.
Everything here is used under the GPL, as the original projects state. The emulator itself keeps
its original `LICENCE.TXT` (GPL v2 or later).

| Part | Where | Origin |
|------|-------|--------|
| Emulator core (r4300, RSP HLE, memory, glN64 renderer, controller pak logic) | repository root | [extremscorner/not64](https://github.com/extremscorner/not64), itself based on Wii64 / Mupen64GC (Mike Slegeir, sepp256, emu_kidid, Extrems) and glN64 (Orkin) |
| UI framework (`libgui`, widgets, theme, menus) | `wiiu/source/libgui`, `widgets`, `menu` | [dborth/snes9xgx](https://github.com/dborth/snes9xgx), [dborth/fceugx](https://github.com/dborth/fceugx), [dborth/vbagx](https://github.com/dborth/vbagx) by Daryl Borth |
| Wii U platform drivers (GX2 video, AX audio, VPAD/KPAD input, filesystem, SMB) | `wiiu/source/drivers` | the same dborth projects |
| Fonts, icons, sounds, translations | `wiiu/data`, `wiiu/meta` | the same dborth projects |
| `libmocha`, `libdvm`, `libsmb2` (build dependencies, not vendored) | `docker/Dockerfile.wiiu` | forks maintained by dborth of wiiu-env/libmocha, extremscorner/libdvm, sahlberg/libsmb2 |

Notes:

* `main/wii_vm.c` is **not** used by the Wii U build. Its header says it may not be used without
  its author's permission, and the native Wii U port does not need it.
* The decaf-emu `latte-assembler` (GPL-3.0) is used only as a build-time tool for GPU shaders. No
  decaf-emu code is included in the binary.
