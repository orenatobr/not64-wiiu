/****************************************************************************
 * not64 for Wii U
 *
 * Based on dborth's WutEmulatorVideo (Snes9x GX), Daryl Borth 2026.
 *
 * WutEmulatorVideo.cpp
 ***************************************************************************/
#include <math.h>
#include <string.h>
#include <algorithm>

#include <coreinit/memdefaultheap.h>
#include <gx2/mem.h>
#include <gx2/event.h>
#include <whb/gfx.h>

#include "WutEmulatorVideo.h"
#include "WutVideoDriver.h"
#include "WutScaleFX.h"
#include "WutOutputFilter.h"
#include "WutUpscaleFilters.h"
#include "shaders/Texture2DShader.h"
#include "../../not64gx.h"
#include "../../video.h"

// Provided here, called by the renderer at the end of each emulated frame
extern "C" void Wiiu_PresentEmuFrame(GX2Texture* colour, int width, int height);

namespace
{
	// Darkness of the scanline gaps (0..1) when Scanline Overlay is on
	const float SCANLINE_STRENGTH = 0.5f;

	// Emulator video is placed in the physical pixels of each output target.
	// The constants below are NOT a design canvas - they are the units the
	// saved settings are defined in.

	// Units of the Screen Position (videoXshift/videoYshift) setting: one unit
	// is 1/640 of the screen width, 1/480 of the screen height. Same on every
	// platform, so a saved shift looks the same everywhere.
	const float SHIFT_UNITS_X = 640.0f;
	const float SHIFT_UNITS_Y = 480.0f;

	// The N64 picture is 4:3. 4:3 (Fixed Pixel Ratio) is for sharp scaling: the
	// largest whole number N such that (640 * N) x (480 * N) fits the target, drawn
	// in the middle with bars around it.
	const float PICTURE_ASPECT = 4.0f / 3.0f;
	const float PICTURE_WIDTH = 640.0f;
	const float PICTURE_HEIGHT = 480.0f;

	void PixelRectToNdc(float x, float y, float w, float h, int designWidth, int designHeight, float offset[3], float scale[3])
	{
		float centerPxX = x + w * 0.5f;
		float centerPxY = y + h * 0.5f;

		offset[0] = (centerPxX / designWidth) * 2.0f - 1.0f;
		offset[1] = 1.0f - (centerPxY / designHeight) * 2.0f;
		offset[2] = 0.0f;

		scale[0] = w / designWidth;
		scale[1] = h / designHeight;
		scale[2] = 1.0f;
	}

	WutEmulatorVideo * instance = nullptr;
}

WutEmulatorVideo::WutEmulatorVideo()
	: videoDriver(nullptr), frameTexture(nullptr)
	, vwidth(640), vheight(480), oldvwidth(0), oldvheight(0)
	, checkVideo(0), haveFrame(false)
	, placement{ {0, 0, 0, 0}, {0, 0, 0, 0} }
	, quadX(0), quadY(0), quadWidth(0), quadHeight(0)
{
	GX2InitSampler(&sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
	instance = this;
}

WutEmulatorVideo::~WutEmulatorVideo()
{
	if(instance == this)
		instance = nullptr;
}

WutEmulatorVideo * WutEmulatorVideo::current()
{
	return instance;
}

void WutEmulatorVideo::init(VideoDriver* driver)
{
	videoDriver = static_cast<WutVideoDriver*>(driver);
	vwidth = 640;
	vheight = 480;
}

/****************************************************************************
 * forceVideoUpdate
 *
 * Forces the next presentFrame() to rebuild scaling state, and forgets any
 * frame from a previous game so nothing stale is drawn before the new game
 * has produced one (eg. right after a ROM load).
 ***************************************************************************/
void WutEmulatorVideo::forceVideoUpdate()
{
	checkVideo = 2;
	haveFrame = false;
	frameTexture = nullptr;
}

/****************************************************************************
 * resetVideo
 *
 * Computes where the game quad goes on each output target, in that target's
 * own physical pixels (placement[]), from the current settings.
 *
 *   None:             fill the target (stretch)
 *   4:3:              the picture's own 4:3, fitted inside the target
 *   4:3 Fixed Ratio:  largest whole-number scale that fits, square pixels
 *                     (e.g. 1080p: 2x = 1280x960; 480p: 1x = 640x480)
 *
 * quadX/Y/Width/Height (and gameScreenPng) are the TV placement expressed in
 * UI-canvas pixels. They only exist for the menu's game screenshot, which
 * lives in canvas space; nothing is drawn from them.
 ***************************************************************************/
void WutEmulatorVideo::resetVideo()
{
	for (int i = 0; i < OUTPUT_TARGET_COUNT; i++)
	{
		const OutputTarget target = static_cast<OutputTarget>(i);
		const float targetW = (float) videoDriver->getTargetWidth(target);
		const float targetH = (float) videoDriver->getTargetHeight(target);

		float w, h;
		bool pixelExact = false;

		if (EmuSettings.videoAspectRatioCorrection == VIDEO_ASPECT_RATIO_CORRECTION_4_3_FIXED)
		{
			// Largest whole scale that fits both axes (never below 1x)
			const float scale = std::max(1.0f,
				std::min(floorf(targetW / PICTURE_WIDTH), floorf(targetH / PICTURE_HEIGHT)));
			w = PICTURE_WIDTH * scale;
			h = PICTURE_HEIGHT * scale;
			pixelExact = true;
		}
		else
		{
			if (EmuSettings.videoAspectRatioCorrection == VIDEO_ASPECT_RATIO_CORRECTION_4_3)
			{
				// Fit the picture inside the target at its own aspect ratio
				const float targetAspect = targetW / targetH;

				if (targetAspect > PICTURE_ASPECT)
				{
					h = targetH;
					w = targetH * PICTURE_ASPECT;
				}
				else
				{
					w = targetW;
					h = targetW / PICTURE_ASPECT;
				}
			}
			else
			{
				// No correction: the picture fills the whole target
				w = targetW;
				h = targetH;
			}

			w *= EmuSettings.videoZoomHor;
			h *= EmuSettings.videoZoomVert;
		}

		// Positive shift moves the picture right / down
		float x = (targetW - w) * 0.5f + EmuSettings.videoXshift * (targetW / SHIFT_UNITS_X);
		float y = (targetH - h) * 0.5f + EmuSettings.videoYshift * (targetH / SHIFT_UNITS_Y);

		if (pixelExact)
		{
			// Pixel-exact only if the quad also starts on a pixel boundary
			x = floorf(x + 0.5f);
			y = floorf(y + 0.5f);
		}

		placement[i].x = x;
		placement[i].y = y;
		placement[i].w = w;
		placement[i].h = h;
	}

	// The same placement in UI-canvas pixels (menu screenshot), from the TV's placement
	const TargetPlacement& tv = placement[static_cast<int>(OutputTarget::TV)];
	const float toCanvasX = (float) videoDriver->getScreenWidth()  / (float) videoDriver->getTargetWidth(OutputTarget::TV);
	const float toCanvasY = (float) videoDriver->getScreenHeight() / (float) videoDriver->getTargetHeight(OutputTarget::TV);

	quadX      = tv.x * toCanvasX;
	quadY      = tv.y * toCanvasY;
	quadWidth  = tv.w * toCanvasX;
	quadHeight = tv.h * toCanvasY;

	// Record where/how big the quad is so we can composite gameScreenPng
	// back at the exact spot and size it was actually drawn at.
	gameScreenPng.width  = vwidth;
	gameScreenPng.height = vheight;
	gameScreenPng.scaleX = quadWidth  / (float) vwidth;
	gameScreenPng.scaleY = quadHeight / (float) vheight;
	gameScreenPng.xoffset = (int) ((quadX + quadWidth  / 2.0f) - videoDriver->getScreenWidth()  / 2.0f);
	gameScreenPng.yoffset = (int) ((quadY + quadHeight / 2.0f) - videoDriver->getScreenHeight() / 2.0f);
}

void WutEmulatorVideo::setFrame(const GX2Texture* frame, int width, int height)
{
	frameTexture = frame;
	vwidth = width;
	vheight = height;
	haveFrame = frame != nullptr;
}

/****************************************************************************
 * drawQuad
 ***************************************************************************/
void WutEmulatorVideo::drawQuad()
{
	videoDriver->flushDrawQueue();

	if (!frameTexture || !videoDriver->isForeground())
		return;

	const GX2Texture* texture = frameTexture;

	// Cheap enough to just re-set every frame rather than tracking whether
	// the setting changed since the last draw.
	GX2InitSampler(&sampler, GX2_TEX_CLAMP_MODE_CLAMP,
		EmuSettings.videoBilinearFilter ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);

	float colorIntensity[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	Texture2DShader* shader = Texture2DShader::instance();

	// NDC placement of the game quad on a target, from its physical-pixel rect
	auto placementNdc = [&](OutputTarget target, float offset[3], float scale[3]) {
		const TargetPlacement& p = placement[static_cast<int>(target)];
		PixelRectToNdc(p.x, p.y, p.w, p.h, videoDriver->getTargetWidth(target), videoDriver->getTargetHeight(target), offset, scale);
	};

	auto drawPass = [&](OutputTarget target) {
		float offset[3];
		float scale[3];
		placementNdc(target, offset, scale);

		shader->setShaders();
		shader->setAttributeBuffer();
		shader->setAngle(0.0f);
		shader->setOffset(offset);
		shader->setScale(scale);
		shader->setColorIntensity(colorIntensity);
		shader->clearBlur();
		shader->setTextureAndSampler(texture, &sampler);
		shader->draw(GX2_PRIMITIVE_MODE_QUADS, 4);
	};

	const bool sharp = EmuSettings.videoUpscalingFilter == UPSCALE_SHARP_BILINEAR;
	const float scanlines = EmuSettings.videoScanlines ? SCANLINE_STRENGTH : 0.0f;

	// Output filter: sharp bilinear and/or scanlines. Returns false if it is unavailable.
	auto outputFilterPass = [&](OutputTarget target, const GX2Texture* tex, bool linear, bool sharpSampling) {
		const TargetPlacement& p = placement[static_cast<int>(target)];

		WutOutputFilter::Params pp;
		pp.texture = tex;
		placementNdc(target, pp.offset, pp.scale);
		pp.outWidth = p.w;
		pp.outHeight = p.h;
		pp.linear = linear;
		pp.sharp = sharpSampling;
		pp.scanlineStrength = scanlines;
		pp.sourceLines = (float) vheight;
		return WutOutputFilter::instance()->draw(pp);
	};

	// The frame texture on a target: plain textured quad, or the output filter when it has work to do
	auto drawGame = [&](OutputTarget target) {
		if ((sharp || scanlines > 0.0f) && outputFilterPass(target, texture, EmuSettings.videoBilinearFilter, sharp))
			return;
		drawPass(target);
	};

	// ScaleFX (TV output only)
	WutScaleFX* scalefx = WutScaleFX::instance();
	bool useScaleFX = false;

	if (EmuSettings.videoUpscalingFilter == UPSCALE_SCALEFX)
	{
		if (scalefx->prepare(texture->surface.width, texture->surface.height))
		{
			scalefx->run(texture);
			useScaleFX = true;
		}
	}
	else
	{
		scalefx->release();
	}

	WHBGfxBeginRenderTV();
	if (useScaleFX)
	{
		// Scanlines go through the output filter, otherwise the ScaleFX final stage draws it
		if (scanlines <= 0.0f || !outputFilterPass(OutputTarget::TV, scalefx->outputTexture(), true, false))
		{
			float offset[3];
			float scale[3];
			placementNdc(OutputTarget::TV, offset, scale);
			scalefx->drawTV(offset, scale);
		}
	}
	else
	{
		drawGame(OutputTarget::TV);
	}
	WHBGfxBeginRenderDRC(); drawGame(OutputTarget::DRC);
}

/****************************************************************************
 * presentFrame
 ***************************************************************************/
void WutEmulatorVideo::presentFrame(int width, int height)
{
	if(width > 0 && height > 0)
	{
		vwidth = width;
		vheight = height;
	}

	if (!haveFrame)
		return; // the game has not produced a frame yet, so there is nothing to draw

	videoDriver->prepareFrame();

	if (oldvwidth != vwidth || oldvheight != vheight) // if the picture size changes, update scaling
		checkVideo = 1;

	if (checkVideo)
	{
		resetVideo(); // reset scaling to the current settings

		oldvwidth = vwidth;
		oldvheight = vheight;
		checkVideo = 0;
	}

	drawQuad();

	videoDriver->presentBuffer();
}

/****************************************************************************
 * readFrameRGB24
 *
 * Reads the frame back for the menu's screenshot. Only possible when the
 * renderer's colour buffer is linear (row after row), which Gx2Gx guarantees;
 * anything else is reported as a black picture rather than read wrongly.
 ***************************************************************************/
void WutEmulatorVideo::readFrameRGB24(uint8_t* dst)
{
	const int width = gameScreenPng.width;
	const int height = gameScreenPng.height;

	if (!frameTexture || !frameTexture->surface.image ||
		frameTexture->surface.tileMode != GX2_TILE_MODE_LINEAR_ALIGNED ||
		frameTexture->surface.format != GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8 ||
		width > (int) frameTexture->surface.width || height > (int) frameTexture->surface.height)
	{
		memset(dst, 0, (size_t) width * (size_t) height * 3);
		return;
	}

	// the GPU wrote it: finish, then drop the CPU's stale view of that memory
	GX2DrawDone();
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, frameTexture->surface.image, frameTexture->surface.imageSize);

	// the picture is the top-left width x height part of the buffer
	const uint8_t* src = static_cast<const uint8_t*>(frameTexture->surface.image);
	const uint32_t srcStride = frameTexture->surface.pitch * 4;

	for (int y = 0; y < height; y++)
	{
		const uint8_t* srcRow = src + (size_t) y * srcStride;

		for (int x = 0; x < width; x++)
		{
			const int outIdx = (y * width + x) * 3;
			dst[outIdx]     = srcRow[x * 4 + 0];
			dst[outIdx + 1] = srcRow[x * 4 + 1];
			dst[outIdx + 2] = srcRow[x * 4 + 2];
		}
	}
}

/****************************************************************************
 * C entry point for the renderer
 *
 * Called on the emulation thread at the end of every emulated frame.
 ***************************************************************************/
extern "C" void Wiiu_PresentEmuFrame(GX2Texture* colour, int width, int height)
{
	WutEmulatorVideo * video = WutEmulatorVideo::current();
	if(!video)
		return;

	video->setFrame(colour, width, height);
	video->presentFrame(width, height);
}
