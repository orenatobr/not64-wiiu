/****************************************************************************
 * not64 for Wii U
 *
 * Based on dborth's WutEmulatorAudio (Snes9x GX), Daryl Borth 2008-2026.
 * Adapted to a push model for the N64's audio interface.
 *
 * WutEmulatorAudio.h
 *
 * EmulatorAudioDriver implementation for Wii U: a ring buffer played by two
 * AX (sndcore2) voices, left and right, hard-panned. The N64 audio plugin
 * pushes blocks of samples whenever the game starts an AI DMA; the voices read
 * through the ring on their own.
 *
 * Underruns play silence rather than looping stale audio: samples are cleared
 * as soon as the hardware has consumed them.
 *
 * Foreground/background handling is not done here, it belongs to
 * WutAudioDriver::startEmulatorAudio()/stopEmulatorAudio().
 ***************************************************************************/
#pragma once
#include <stdint.h>
#include <sndcore2/voice.h>
#include "../EmulatorAudioDriver.h"

class WutEmulatorAudio : public EmulatorAudioDriver
{
	public:
		WutEmulatorAudio();
		~WutEmulatorAudio() override;

		void init() override;
		void resetAudio() override;

		//! No-op. Voices are armed once enough audio has been queued by push().
		void start() {}

		//! Hard-stops both AX voices and forgets what was queued.
		void stop();
		//! Frees the two AX voices. Must be called before AXQuit().
		void shutdown();

		//! Silences or resumes playback without losing the queue position.
		void pause(bool paused);

		//! The AI DAC rate the game programmed, in Hz.
		void setSourceRate(uint32_t hz);

		//! Queues stereo frames (big-endian signed 16-bit, left then right).
		//! \param hz rate the samples were produced at
		//! \param speed emulation speed relative to real time (1.0 = normal)
		void push(const int16_t * stereoBE, uint32_t frames, uint32_t hz, float speed);

		//! Frames queued ahead of the hardware playhead (after syncing with it).
		uint32_t getQueuedFrames();

	private:
		void armAndStartVoices();
		void syncQueuedFrames();
		void applyRate();

		static constexpr uint32_t RING_FRAMES = 8192;
		//! Playback starts once this many frames are queued, to absorb jitter.
		static constexpr uint32_t START_FRAMES = 1024;
		//! Never queue more than this: older audio would only add latency.
		static constexpr uint32_t MAX_QUEUED_FRAMES = RING_FRAMES - 1024;
		//! Queue levels (in frames) for the gentle speed correction.
		static constexpr uint32_t HIGH_WATER = 4096;
		static constexpr uint32_t LOW_WATER = 1536;

		AXVoice* voiceL;
		AXVoice* voiceR;
		alignas(32) int16_t ringL[RING_FRAMES];
		alignas(32) int16_t ringR[RING_FRAMES];

		uint32_t writeOffset;   //!< next frame to write, 0 .. RING_FRAMES - 1
		uint32_t queuedFrames;  //!< software owned, always within [0, RING_FRAMES]
		uint32_t lastHwFrame;
		uint32_t sourceRate;
		float speed;
		float correction;
		bool started;
		bool paused;
};
