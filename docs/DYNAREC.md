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
