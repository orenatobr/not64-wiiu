/*
 * Gx2Gx.h: public interface of the GX2 renderer for glN64_GX on Wii U.
 *
 * Lifetime: the app calls gx2gx::init() after GX2 and its own video driver are
 * initialised, and gx2gx::shutdown() before tearing them down. Between the two,
 * glN64_GX talks to the GX_* API from <gccore.h> (compat/gccore.h on Wii U).
 *
 * GX2 state contract (details in docs/GX2_RENDERER.md):
 *  - gx2gx renders into its own colour/depth buffers using its own GX2ContextState.
 *    The first GX call after a present (or after init) switches to that context
 *    with GX2SetContextState().
 *  - When a frame ends (GX_CopyDisp with a destination) gx2gx flushes, switches
 *    nothing back, and calls Wiiu_PresentEmuFrame(). The application must
 *    select its own context state again (WHBGfxBeginRenderTV/DRC does) before
 *    it issues any GX2 draw of its own, exactly as it already does after
 *    WutScaleFX::run().
 */
#ifndef GX2GX_GX2GX_H
#define GX2GX_GX2GX_H

#ifdef __cplusplus
extern "C" {
#endif

struct GX2Texture;

/*
 * Implemented by the application's video driver. `colour` is the offscreen RGBA8
 * colour buffer viewed as a texture (valid until the next frame is rendered,
 * the GPU may still be writing it: the app samples it in the same GX2 queue, so
 * ordering is guaranteed). width/height are the pixel size of the valid image
 * (EFB size times the scale factor). gx2gx provides a weak no-op default.
 */
void Wiiu_PresentEmuFrame(struct GX2Texture *colour, int width, int height);

#ifdef __cplusplus
}

namespace gx2gx {

/* Create the context, buffers and shaders. scale = 1..4 (EFB is 640*scale x 480*scale). */
bool init(int scale);
void shutdown();

/* Gamma the game requested for the frame just presented (1.0, 1.7 or 2.2). Not applied by gx2gx. */
float presentGamma();

} /* namespace gx2gx */
#endif

#endif
