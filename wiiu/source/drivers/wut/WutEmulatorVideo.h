/****************************************************************************
 * not64 for Wii U
 *
 * Based on dborth's WutEmulatorVideo (Snes9x GX), Daryl Borth 2026.
 *
 * WutEmulatorVideo.h
 *
 * EmulatorVideoDriver implementation for Wii U. The N64 renderer draws each
 * frame into an off-screen GX2 colour buffer and hands it over with
 * Wiiu_PresentEmuFrame(). This class places that picture on the TV and the
 * GamePad (aspect ratio, zoom, position), filters it (bilinear, sharp bilinear,
 * scanlines, ScaleFX) and presents it.
 ***************************************************************************/
#pragma once
#include <stdint.h>
#include <gx2/sampler.h>
#include <gx2/texture.h>
#include "WutVideoDriver.h"
#include "../EmulatorVideoDriver.h"

class WutVideoDriver;

class WutEmulatorVideo : public EmulatorVideoDriver
{
	public:
		WutEmulatorVideo();
		~WutEmulatorVideo() override;

		void init(VideoDriver* videoDriver) override;
		void resetVideo() override;
		//! Draws the frame given to setFrame() and flips. width/height are the size of
		//! the picture in the texture, which the N64 changes on its own (320x240, 640x480 ...).
		void presentFrame(int width, int height) override;
		void readFrameRGB24(uint8_t* dst) override;
		void forceVideoUpdate() override;

		//! The finished frame. The texture is owned by the renderer and stays valid
		//! until the next call.
		void setFrame(const GX2Texture* frame, int width, int height);

		//! The instance created by the video driver, for the C entry point.
		static WutEmulatorVideo* current();

	private:
		void drawQuad();

		WutVideoDriver* videoDriver;
		const GX2Texture* frameTexture;
		GX2Sampler sampler;

		int vwidth, vheight;
		int oldvwidth, oldvheight;
		int checkVideo;
		bool haveFrame;

		// Where the game quad is drawn: top-left x/y and size w/h in the pixels of each render target
		// Scaling/filtering uses it too (source-to-output scale = size / vwidth, vheight).
		struct TargetPlacement { float x, y, w, h; };
		TargetPlacement placement[OUTPUT_TARGET_COUNT];

		// The TV placement in UI-canvas pixels (top-left x/y, size w/h), derived
		// by resetVideo(). Not used for drawing - only the menu's game screenshot
		// background (gameScreenPng) needs it.
		float quadX, quadY, quadWidth, quadHeight;
};
