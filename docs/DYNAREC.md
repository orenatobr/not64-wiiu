# The PowerPC recompiler on the Wii U

not64's fast CPU core translates MIPS code into PowerPC code. The Wii U's Espresso is a PowerPC 750
derivative, so the translation target is the native instruction set and the existing recompiler
(`r4300/ppc`) is compiled into the Wii U build. It is **switched off** (`wiiu_jit.c`,
`RECOMPILER_USES_CODEGEN_AREA`), for the reasons below.

## What is in place

* `r4300/ppc` compiles with the Wii U toolchain.
* Generated code addresses emulator state relative to `r13 = &reg` (the Wii build used the small data
  area, which the wut linker script does not provide). `dyna_run()` loads r13 on entry. This is
  `SDAREL` in `r4300/ppc/Wrappers.h`.
* The code cache allocator (`wiiu_heap.c`) keeps its bookkeeping outside the area it manages, which a
  codegen area requires. It has host tests.
* `n64_dynarec_available()` asks `wiiu_jit_available()`, so the UI offers "Recompiler" and the
  automatic choice picks it only where it can work.

## What is missing

1. **Executable memory.** An application cannot make heap memory executable. The OS lends a "codegen"
   area (`OSGetCodegenVirtAddrRange`) to titles whose `cos.xml` allows it, usable only from the core
   `OSGetCodegenCore()` names, and switchable between writable and executable but never both
   (`OSSwitchSecCodeGenMode`, `OSCodegenCopy`). Whether a title started through Aroma gets the area
   depends on the console setup and could not be checked during development.
2. **Using it.** The code cache heap would be created over that area, the emulation thread pinned to
   the codegen core, and every place that writes generated code (block compilation, block linking,
   invalidation) wrapped in a switch to writable and back to executable.
3. **Fast memory access.** The Wii build maps the N64's RDRAM at the same address on the PowerPC
   (`FASTMEM`), which the Wii U cannot do. The recompiler uses its slower memory helper instead. A
   base register plus a range check in the generated loads and stores would recover most of the speed.

## How to find out if it can work on your console

`wiiu_jit_query()` reports the codegen area the OS lends (address, size, core). It is not shown in
the UI yet. If the OS lends no area, the recompiler cannot run there and the interpreters are what
you have.

## What was tried in Cemu

`make DYNAREC_TEST=1` offers the recompiler and puts its code cache in the codegen area. In Cemu 2.6 that
area is 16 MiB at 0x0223E000, core 0, writable (no execute-only switching). Hello World (a CPU-only
homebrew) runs on it. Banjo-Kazooie and Conker boot into generated code and never submit a display list, so
the recompiler produces wrong behaviour in real games and is off. Finding why needs a differential check
of the interpreter against the recompiler. See `docs/ACCURACY.md`.

## What the community does (research, not tested)

* The codegen area belongs to titles whose `cos.xml` asks for it: its size and the one core allowed to use it
  come from there (WiiUBrew: the area starts at 0x01800000; Virtual Console titles carry `codegen_core`
  40000001). Most processes that are not the browser have none, so a plain Aroma `.wuhb` probably gets
  `OSGetCodegenVirtAddrRange` = 0, and then the recompiler cannot run there at all.
* The N64 Virtual Console titles that people inject ROMs into (UWUVCI, 64Inject) run Nintendo's own N64
  emulator at full speed on the Espresso, which shows a PowerPC recompiler fits the hardware; they get their
  codegen from the VC title's own permissions, not from the injected ROM. Packaging this port as such a title
  needs base game files that cannot be shipped here.
* Plan: treat the interpreter as the real target on a console, and speed up glN64 and the interpreter instead.
  The recompiler stays a test switch until a title with codegen permission is available.

Sources: [UWUVCI N64 inject guide](https://uwuvci-prime.github.io/UWUVCI-Resources/n64/n64compat.html), [64Inject thread](https://gbatemp.net/threads/64inject-definitive-n64-wii-u-virtual-console-injector.535520/), [WiiUBrew coreinit](https://wiiubrew.org/wiki/Coreinit.rpl), [WiiUBrew Cafe OS](https://wiiubrew.org/wiki/Cafe_OS).
