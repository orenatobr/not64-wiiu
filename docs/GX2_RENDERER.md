# GX2 renderer for glN64_GX (Wii U)

The Wii/GameCube HW renderer `glN64_GX/` talks to the GX API of libogc. Wii U has no GX, so the
logic of glN64_GX is kept as it is and the GX layer underneath it is replaced:

```
glN64_GX/*.cpp  --(<gccore.h>, <ogc/gx.h>)-->  compat headers (libogc-identical names/enums/signatures)
                                                   |
                                         Gx2Core.cpp   GX state machine, vertex assembly, texture cache,
                                                   |   TEV packing (Gx2Tev.cpp), texture detiling (Gx2Tex.cpp)
                                         Backend interface (Gx2Backend.h): resolved draw packets
                                                   |
                                         Gx2Wiiu.cpp   GX2: EFB, context, shaders, draws, copies
                                                   |
                                         Wiiu_PresentEmuFrame(GX2Texture*, w, h)   (app, video driver)
```

Everything is under `wiiu/source/gx2gx/`. Only `Gx2Wiiu.cpp` needs wut; the rest is plain C++ and
is unit tested on the host (`tests/gx2gx/run.sh`).

## Using it from the app

1. After GX2 and the main video driver are initialised: `gx2gx::init(scale)` (scale 1..4, EFB is
   `640*scale x 480*scale`). `gx2gx::shutdown()` before tearing them down.
2. Implement `extern "C" void Wiiu_PresentEmuFrame(GX2Texture *colour, int width, int height)`
   (declared in `Gx2Gx.h`; a weak no-op is provided). The texture is a private RGBA8 copy of the
   finished frame, valid until the second following present; width/height are the valid pixel size.
3. In the makefile: `include wiiu/gx2gx.mk`, add `GX2GX_CPPFILES` and `GLN64_CPPFILES` to the
   sources. The `.mk` adds the compat include directory and `-DGLN64_GX -D__GX__ -D__LINUX__
   -D__WIIU__` **only to the glN64_GX objects** (target-specific variables), so nothing leaks into
   the app. `HW_RVL` must not be defined.
4. glN64_GX needs its usual externals (ROM/RDRAM/registers, `DEBUG_print`, ...) from the core; the
   GX layer needs nothing from the app apart from `Wiiu_PresentEmuFrame`. `rmode`, `vmode` and
   `GX_xfb_offset` have weak defaults (640x480) in `Gx2Wiiu.cpp`.
5. All GX calls must come from the thread that submits GX2 commands.

### GX2 state contract

* gx2gx owns one `GX2ContextState` and the colour/depth buffers it renders to. The first GX call
  after `init()` or after a present selects that context (`GX2SetContextState`), sets
  `GX2_SHADER_MODE_UNIFORM_REGISTER`, RT0 and the depth buffer.
* `init()` leaves gx2gx's context selected. After `init()` and after every
  `Wiiu_PresentEmuFrame()` the application must select its own context state again
  (`WHBGfxBeginRenderTV/DRC` does), exactly as it does after `WutScaleFX::run()`. gx2gx saves and
  restores nothing else: the GX2 context state is the only global it touches.
* Per frame gx2gx never touches the TV/DRC scan buffers.
* Resources (textures, vertex ring) use `MEMAllocFromDefaultHeapEx`; freed textures are retired for
  4 presents before the memory is released (GPU may still read them).

## What each GX call maps to

| GX call | GX2 / shim behaviour |
|---|---|
| `GX_ClearVtxDesc/SetVtxDesc/SetVtxAttrFmt` | kept as state; the component types follow from the `GX_Position*/Color4u8/TexCoord2f32` call used. Only DIRECT attributes; matrix-index attributes (`PNMTXIDX`, `TEXnMTXIDX`) are ignored (libogc does not put them in the stream for `GX_PNMTXn` either). Normals are not supported (glN64 does not use them). |
| `GX_Begin/Position*/Color4u8/TexCoord2f32/End` | vertices collected in a CPU `std::vector<Vertex>` (32 bytes: xyz, rgba8, s0 t0 s1 t1). A vertex is complete when pos, colour (if enabled) and all enabled texcoords arrived. Vertices beyond `vtxcnt` are dropped, incomplete primitives are truncated to whole primitives. `GX_End` issues one `GX2DrawEx` through an 8 MiB ring buffer. Texcoord attributes are routed to the two shader texture slots through the TEV order and `GX_SetTexCoordGen` (2x4 matrices applied on the CPU). |
| `GX_LoadProjectionMtx/LoadPosMtxImm/SetCurrentMtx/LoadTexMtxImm` | stored. The vertex shader gets the columns of `Q * [M;0001]` where `Q` is built from exactly the matrix elements the GX transform unit reads (perspective: `w = -z`, ortho: `w = 1`), so glN64's partially filled matrices behave as on GX. |
| `GX_SetViewport` | `GX2SetViewport(x,y,w,h,0,1)` scaled by the EFB factor; depth range is applied in the vertex shader: `z_out = zc*(far-near) + wc*far` (GX NDC z is in [-1,0] and depth = far + ndc*(far-near)), clip space is configured with `GX2SetRasterizerClipControlHalfZ(TRUE, zclip, TRUE)`. |
| `GX_SetScissor` | `GX2SetScissor`, clamped to the target. |
| `GX_SetCullMode` | `GX2SetCullOnlyControl`. Front face is clockwise (`GX2GX_FRONT_FACE`), because glN64 flips N64's front face for GX. See open questions. |
| `GX_SetClipMode` | z clipping on/off in `GX2SetRasterizerClipControlHalfZ`. |
| `GX_SetZMode` | `GX2SetDepthOnlyControl` (compare enums have identical numbering; update only with test enabled). |
| `GX_SetZCompLoc` | ignored (GX2 chooses early/late Z; the shader sets `Z_ORDER = EARLY_Z_THEN_LATE_Z` like dborth's compiled shaders, alpha test is the hardware one). |
| `GX_SetZTexture` | glN64 only uses it with the all-zero dummy texture bound to the last TEV stage, so ADD/REPLACE become a constant added to / replacing the depth in the vertex shader (`zOffset = far + bias/0xFFFFFF`, `zScale = 0` for REPLACE). No per-pixel Z texture. |
| `GX_SetBlendMode` | BLEND: `GX2SetBlendControl` ADD (colour and alpha use the same factors; SRCCLR on the source side is the destination colour and vice versa, see `Gx2Map.h`). SUBTRACT: reverse subtract ONE/ONE. LOGIC: all 16 GX logic ops map 1:1 to ROP3 via `GX2SetColorControl`. NONE: copy. Nothing is left unmapped. |
| `GX_SetColorUpdate/SetAlphaUpdate` | `GX2SetTargetChannelMasks`. |
| `GX_SetDstAlpha` | shader: `a_out = lerp(a, value, enable)` (applied before the hardware alpha test, which glN64 never combines with it). |
| `GX_SetAlphaCompare` | `GX2SetAlphaTest`. One real comparison (the other `GX_ALWAYS` with AND) maps exactly with a half-step shift of the reference for the 8-bit compare (`>`: ref+0.5, `>=`: ref-0.5, ...). EQUAL/NOT_EQUAL compare floats against ref/255. Two real comparisons: only comparison 0 is honoured (glN64 never does this). |
| `GX_SetFog` | uniforms: `f = clamp(zscreen*A + B)*enable`, `zscreen` from the interpolated `(z_out, w)`, `A = (far-near)/(end-start)`, `B = (near-start)/(end-start)`; colour is lerped towards the fog colour. Types other than NONE are treated as linear (glN64 only uses ORTHO_LIN). |
| `GX_SetNumChans/NumTexGens/NumTevStages/TexCoordGen/SetTevOrder/Op/ColorIn/AlphaIn/ColorOp/AlphaOp/Color/KColor/KColorSel/KAlphaSel` | recorded in `TevState`, packed into the ubershader uniforms per draw (below). `GX_SetTevOp` is a bit-identical port of libogc's. |
| `GX_SetTevSwapMode/SwapModeTable` | the first stage using a texmap picks the swap table, which becomes the texture's component swizzle (`compMap`). Raster swaps are not applied (glN64 only ever uses the identity table). |
| `GX_SetLineWidth` | `GX2SetLineWidth(width/6 * scale)`. |
| `GX_InitTexObj/InitTexObjLOD/LoadTexObj` | see texture path; wrap CLAMP/REPEAT/MIRROR and NEAR/LINEAR min/mag map to a GX2 sampler (cached). Mip-mapping and LOD limits are ignored (glN64 never mipmaps). |
| `GX_InvalidateTexAll` | drops all CPU-decoded textures. |
| `GX_SetTexCopySrc/Dst`, `GX_CopyTex` | creates (once per destination address) an RGBA8 render texture and draws the EFB rectangle into it with the 1-stage TEV program (nearest when the copy is 1:1, bilinear when downscaling from a larger EFB); the clear flag clears the EFB. The "format" argument only sizes the guest-memory block used for cache invalidation. |
| `GX_SetCopyClear/CopyDisp` | `CopyDisp(NULL, clear)` only clears; with a destination the frame is presented. Clear honours the colour/alpha/Z update flags like GX does: full mask uses `GX2ClearColor`, a partial mask draws a masked full-screen quad, depth uses `GX2ClearDepthStencilEx`. |
| `GX_SetDispCopySrc/Dst/Frame2Field/Gamma/SetCopyFilter/PixModeSync/SetCoPlanar` | recorded or ignored. Gamma is exposed through `gx2gx::presentGamma()` but not applied. |
| `GX_SetDrawSync/SetDrawSyncCallback` | the callback runs synchronously from `GX_SetDrawSync` (GX2 submission is in order; the present already happened). |
| `GX_DrawDone/Flush` | `GX2DrawDone/GX2Flush`. |
| `GX_RedirectWriteGatherPipe/Restore` | no-ops; the one glN64 user (CPU framebuffer retile in `VI.cpp`) has a portable `#ifdef __WIIU__` replacement. |

libogc non-GX calls: `VIDEO_*` are inline no-ops (`ogc/video.h`), `DCFlushRange/DCStoreRange/DCZeroRange`
are remapped by macro to shim functions that invalidate cached textures overlapping the range (the real
coreinit symbols stay untouched for the rest of the app), `__lwp_heap_*` is a budgeted `memalign`
(`ogc/lwp_heap.h`), `_sync()` is the PPC instruction. `guMtx*/guVec*/guOrtho` are reimplemented
with libogc's conventions (`Gx2Math.cpp`).

### Edits in `glN64_GX/` (all under `#ifdef __WIIU__`, or plain portability fixes)

* `Textures.h`, `Textures.cpp`, `FrameBuffer.cpp`, `VI.cpp`: no MEM2 area (`HW_RVL` branches are skipped,
  texture heap budget 16 MiB through the shim).
* `VI.cpp`: GX font overlay (FPS/debug text) and the loading icon are compiled out (they use libgui
  and an embedded GX-format texture; the app draws its own overlay); dummy XFB tokens so that
  `GX_CopyDisp` always has a non-NULL destination; the PPC write-gather retile loop has a C version;
  two `abs()` calls on unsigned differences got an `(int)` cast (ambiguous on current libstdc++).
* `RSP.cpp`: restores glN64's `min` macro after `<math.h>` (current libstdc++ `#undef`s it).

## Texture path (decision)

glN64 writes textures in the GX tiled layout (I4, IA4, I8, IA8, RGB565, RGB5A3, RGBA8). The shim
**detiles on the CPU at first use and uploads linear RGBA8** (`Gx2Tex.cpp`) instead of changing
glN64's conversion code: no edits in the 2000-line `Textures.cpp`, one decoder to test, and RGBA8
is the one format whose GX2 swizzle/endianness is certain. Cost: 4 bytes per texel on the GPU
and one decode per texture upload (N64 textures are at most 4 KiB of TMEM, so this is small).

Cache: one GX2 texture per guest address, keyed by the pointer glN64 passes to `GX_InitTexObj`
(plus size/format). An entry is dropped when the heap block is allocated or freed
(`__lwp_heap_*`), when `DCFlushRange/DCStoreRange/DCZeroRange` touch it, or on `GX_InvalidateTexAll`.
glN64 only writes a texture right after allocating it and flushes the cache range afterwards, which is
what makes this sound without hashing. EFB copies (`GX_CopyTex`) create GPU-resident entries that are
never re-decoded and survive `DCFlushRange` (only freeing the block drops them).

## Pixel shader: TEV ubershader

One vertex shader (`vs_gx.asm`) and seven pixel shaders `ps_tev1..7.asm`, unrolled for 1..7 stages and
selected per draw (`tevEffectiveStages`: a trailing pass-through stage, which glN64 appends for the Z
texture, is dropped; more than 7 real stages are truncated, glN64 does not produce them in practice).
All TEV data is in pixel uniform registers (256 vec4):

```
c0..c2   TEV registers 0..2 (initial values, rgba)            PREV starts at zero
c3       fog (A, B, enable, -)        c4  fog colour rgb      c5  dst alpha (enable, value)
stage s (0..6) at c(8 + 32*s):
  [i*7 + j]  i = input a,b,c,d ; j = candidate prev, reg0, reg1, reg2, tex0, tex1, ras
             .x weight of cand.rgb into the colour input   .y weight of cand.aaa into the colour input
             .z weight of cand.a into the alpha input
             .w pad lanes: 0..3 clamp lo/hi colour/alpha, 4..7 colour output one-hot (prev,reg0,reg1,reg2),
                8..11 alpha output one-hot
  [28+i]     constant added to input i (rgb = colour part, a = alpha part)
```

The CPU (`tevPack`) resolves everything that is static for a draw: which candidate each of the 16
colour / 8 alpha input selectors names, `TEXMAP/COLOR` NULLs, the konst selection (`KCSEL/KASEL`),
`ONE/HALF/ZERO`, the add/sub sign, scale (folded into a, b, d), bias (folded into d's constant) and the
clamp range. The shader evaluates per stage, per lane (rgb and alpha lanes in parallel):

```
a,b,c,d = const + sum_j w_j * cand_j
out     = clamp(d + c*(b - a) + a)        ( = d + (1-c)*a + c*b , the TEV equation )
reg_r  += onehot_r * (out - reg_r)         for the four output registers
```
then dst alpha and fog. Each stage is about 79 ALU groups; the whole thing needs no relative
addressing, no control flow and no literals except 0.0 (so none of the hardware hazards of those
features). Code lives in `shaders/gen_shaders.py` (it also holds a Latte ALU simulator used by the
tests), the assembly in `shaders/asm/`, the arrays in `Gx2ShaderData.h`.

**Float approximation of the 8-bit TEV.** Real TEV runs on 8-bit inputs with 10/11-bit signed
intermediates. This version works in float: inputs are `value/255`; `c` weighs with `c` (not
`c*256/255`-style fixed point); results are not rounded to 8 bits between stages (the colour buffer is 8
bit, so the final write rounds once); intermediate range is [-1024/255, 1023/255] when clamping is off
and [0,1] when on, like the hardware; inputs are not wrapped to 8 bits. Comparison ops
(`GX_TEV_COMP_*`) are not implemented (glN64 only uses ADD/SUB). Differences are at most a few
LSB per stage.

## Toolchain

`docker/Dockerfile.shaders` builds decaf-emu's `latte-assembler` (GPL-3.0, build time only, nothing of
it is in the emulator) from commit `e6c528a20a41c34e0f9eb91dd3da40f119db2dee` with a small CMake
wrapper (only common/libgfd/glslang/latte-assembler; decaf's x86-only `common` parts are skipped so it
also builds on arm64 hosts). `tools/build-shaders.sh` runs `gen_shaders.py gen`, assembles the
GFD files and converts them with `tools/gfd2h.py` into `Gx2ShaderData.h` (same uint32 program + register
array format as dborth's headers). The assembly and the header are committed; normal CI builds do not
need the image.

`tools/validate-assembler.sh` assembles a functional equivalent of dborth's `Texture2DShader` and
compares the register arrays with the compiler-generated ones in
`Texture2DShader.cpp`: vertex regs (52 words) and pixel regs (41 words) are identical, and so is the
CALL_FS control-flow word pair.

## Known deviations and limits

* TEV: float evaluation (above), 7 stages, 2 texture maps (texmap 0/1; others read as zero, which
  includes glN64's unused noise texture), one raster colour channel (`COLOR1A1` reads as zero), no
  raster swap tables, no indirect texturing, no comparison ops.
* Z texture: constant-depth replacement/offset only. Alpha compare with two real comparisons: only the
  first. EQUAL alpha compares are approximate. Gamma is reported, not applied.
* Texture memory: converted RGBA8 on the GPU, 4x the size of I4/IA4; very large mip-less textures only.
* EFB is always RGBA8 + D24S8 (no RGB565 dithered EFB, no 6-bit alpha).
* `GX_SetCopyFilter` (AA/deflicker) is ignored; there is no MSAA.

## Verification status

Honest account of what was done. The unit tests and the shader simulation ran on a macOS host with the
Docker images. **The renderer has since run in Cemu 2.6 (macOS, Apple Silicon, under Rosetta), never on a
Wii U.** In Cemu, `tests/gx2gx/rpx` shows the expected textured two-stage triangle and a full-screen
textured quad, and the app renders a homebrew demo and the Banjo-Kazooie intro and first level with correct
colours and textures. Known artefacts there: grainy textures and a few stray green rectangles (not yet
investigated). Cemu is not a console: hardware behaviour can still differ.

* Compiled: all five `gx2gx` sources, and all 29 `glN64_GX` translation units listed in `gx2gx.mk`, with the
  devkitPPC/wut toolchain of `not64-wiiu-builder` (`-Wall -Wextra` clean for the gx2gx sources; glN64's
  historical warnings are masked). `tests/gx2gx/rpx` links the renderer with a test `main` (init, one
  frame with a textured triangle through 2 TEV stages, shutdown) into `gx2gx_test.rpx`
  (`make -C tests/gx2gx/rpx` in the builder image).
* Unit tests (`tests/gx2gx/run.sh`, g++ with ASan/UBSan, also run with the container's gcc): hand-computed TEV
  vectors (MODULATE/REPLACE/DECAL/BLEND, lerp, SUB, bias+scale+clamp, output registers, KONST selection),
  4000 random TEV states comparing the spec-level evaluator with an evaluator that reads the packed
  uniforms, GX texture detiling for RGBA8/RGB565/RGB5A3/IA4/I4/IA8 including size/bounds failures, GX state to
  draw-packet translation with a mock backend (vertex assembly and bounds, matrices and depth mapping,
  alpha test, blend/cull/scissor/line width, fog uniforms, texture cache invalidation, EFB copy,
  clear and present), blend/logic-op/wrap enum tables, guMtx/guOrtho/inverse. 205 checks.
* Shader logic: `gen_shaders.py sim` interprets the generated ALU groups with idealised Latte semantics
  on the packed uniforms of 300 random TEV states and matches the C++ reference to 2e-3, plus a fog and
  dst-alpha check. `gen_shaders.py lint` enforces conservative encoding rules (one constant register per
  group, unique slots/destinations, PS only right after a t-slot producer).
* Assembler: the pinned latte-assembler builds and assembles all 8 shaders without error; the GPR counts
  declared in the headers are checked by the assembler against the code. The register-word comparison
  with dborth's compiler output (above) is the only check of the produced encodings against real
  hardware-targeted output; the ALU/TEX instruction words themselves have not been compared with a
  disassembler or a GPU.
* Not verified: anything on a real GPU, instruction scheduling rules of the Latte ALU that the assembler does
  not check (see open questions), the grain and rectangle artefacts, other games.

## Open questions for hardware testing

Each of these is a documented constant or a deliberate "safest" choice.

1. **Front face / winding.** `GX2GX_FRONT_FACE` (default `GX2_FRONT_FACE_CW`, derived from glN64's
   "GC vertex winding is backwards" comment). If culling is inverted, flip it.
2. **Viewport orientation and depth.** The shim assumes `GX2SetViewport` y is measured from the top and NDC +y is
   up (as in dborth's quad code), and that `GX2SetRasterizerClipControlHalfZ(.., TRUE)` gives clip z in [0,w] so
   that depth = `z_out/w`. If depth looks inverted, adjust `zScale/zOffset` in `Gx2Core.cpp`.
3. **Vertex fetch.** Attribute `location` L arrives in `R(L+1)` after the fetch shader (as in decaf's
   skeleton shader); `UNORM_8_8_8_8` colour is read as r,g,b,a bytes like dborth's `ColorShader`.
4. **ALU encoding rules not checked by the assembler:** bank swizzle (default `VEC_012`), at most one constant
   register per group (enforced by lint), the previous-scalar `PS` register consumed in the group right after the
   `RECIP_IEEE` in the t slot (fog), `CLAMP` result modifier on `MULADD`. The ALU clauses are split at 120 slots.
5. **POSITION/z in PS.** Fog uses a varying `(z_out, w)` and a per-pixel reciprocal instead of the fragment position input.
6. **Depth buffer bound during copies.** EFB-to-texture passes render into a smaller colour buffer while the full-size
   depth buffer stays bound (depth test off). If GX2 objects, bind a dummy depth buffer there.
7. **Context state after `GX2Clear*`.** The context is re-selected after every clear (as WHB does); confirm it is needed/sufficient.
8. **Uniform registers across shader switches** are re-uploaded whenever the pixel shader changes (dborth's driver
   observed they do not persist); the VS constants and PS rows are otherwise cached.
9. **Texture sampling of a colour buffer view** (EFB copies, present buffers) uses `GX2_TILE_MODE_DEFAULT` and
   `GX2Invalidate(COLOR_BUFFER)` + `GX2Invalidate(TEXTURE)` like `WutScaleFX`.
10. **Linear-aligned small textures** (width not a multiple of 64) rely on `GX2CalcSurfaceSizeAndAlignment` pitch handling as in the UI code.
11. **Vertex ring** is 8 MiB; when full the GPU is drained (`GX2DrawDone`) before it is reused. A bigger ring or timestamps would avoid the stall.
12. **Texture budget.** 16 MiB of guest-format texture heap (`GX_TEXTURE_CACHE_SIZE`) plus 4x that on the GPU in the worst case.
13. **Threading.** GX2 command submission thread (see "Using it from the app").
14. **Alpha test precision.** Hardware compare of the float alpha with the half-step reference; confirm no off-by-one on the 8-bit edge.
