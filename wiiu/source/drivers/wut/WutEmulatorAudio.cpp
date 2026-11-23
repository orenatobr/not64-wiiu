/****************************************************************************
 * not64 for Wii U
 *
 * Based on dborth's WutEmulatorAudio (Snes9x GX), Daryl Borth 2008-2026.
 *
 * WutEmulatorAudio.cpp
 ***************************************************************************/
#include <coreinit/cache.h>
#ifdef NOT64_TEST_INPUT
#include <coreinit/debug.h>
#endif
#include <sndcore2/core.h>
#include <string.h>
#include <algorithm>

#include "WutEmulatorAudio.h"
#include "../../core/audio_wiiu.h"

#ifdef NOT64_TEST_INPUT
static unsigned audioDrops = 0; // blocks thrown away because the queue was full
#endif

namespace
{
	//! Speed correction while the queue is too full / too empty (0.5 percent
	//! is inaudible but enough to walk the queue back into its band)
	const float CORRECT_DRAIN = 1.005f;
	const float CORRECT_FILL = 0.995f;

	//! The emulation speed is only followed within these limits, so a stall
	//! or a fast-forward can not turn the audio into noise.
	const float SPEED_MIN = 0.5f;
	const float SPEED_MAX = 2.0f;

	//! The single instance behind the C interface in audio_wiiu.h
	WutEmulatorAudio * instance = nullptr;

	template <int N>
	void buildChannelMix(AXVoiceDeviceMixData (&mix)[N], bool left, bool right)
	{
		memset(mix, 0, sizeof(mix));
		if(left)
			mix[0].bus[0].volume = 0x8000;
		if(right && N > 1)
			mix[1].bus[0].volume = 0x8000;
	}

	constexpr int AX_TV_CHANNELS = 6;
	constexpr int AX_DRC_CHANNELS = 4;
}

WutEmulatorAudio::WutEmulatorAudio() :
	voiceL(nullptr), voiceR(nullptr), writeOffset(0), queuedFrames(0), lastHwFrame(0),
	sourceRate(32000), speed(1.0f), correction(1.0f), started(false), paused(false)
{
	memset(ringL, 0, sizeof(ringL));
	memset(ringR, 0, sizeof(ringR));
	instance = this;
}

WutEmulatorAudio::~WutEmulatorAudio()
{
	if(voiceL) AXFreeVoice(voiceL);
	if(voiceR) AXFreeVoice(voiceR);
	if(instance == this)
		instance = nullptr;
}

void WutEmulatorAudio::init()
{
	if(voiceL && voiceR)
	{
		resetAudio();
		return;
	}

	voiceL = AXAcquireVoice(31, nullptr, nullptr);
	voiceR = AXAcquireVoice(31, nullptr, nullptr);

	AXVoiceDeviceMixData tvMixL[AX_TV_CHANNELS], drcMixL[AX_DRC_CHANNELS];
	AXVoiceDeviceMixData tvMixR[AX_TV_CHANNELS], drcMixR[AX_DRC_CHANNELS];
	buildChannelMix(tvMixL, true, false);   // hard-pan left
	buildChannelMix(drcMixL, true, false);
	buildChannelMix(tvMixR, false, true);   // hard-pan right
	buildChannelMix(drcMixR, false, true);

	AXVoice * voices[2] = { voiceL, voiceR };
	AXVoiceDeviceMixData * tv[2] = { tvMixL, tvMixR };
	AXVoiceDeviceMixData * drc[2] = { drcMixL, drcMixR };

	for(int i = 0; i < 2; i++)
	{
		if(!voices[i])
			continue;

		AXVoiceBegin(voices[i]);
		AXSetVoiceType(voices[i], 0);
		AXSetVoiceDeviceMix(voices[i], AX_DEVICE_TYPE_TV, 0, tv[i]);
		AXSetVoiceDeviceMix(voices[i], AX_DEVICE_TYPE_DRC, 0, drc[i]);

		AXVoiceVeData veData;
		veData.volume = 0x8000;
		veData.delta = 0;
		AXSetVoiceVe(voices[i], &veData);
		AXVoiceEnd(voices[i]);
	}

	resetAudio();
}

void WutEmulatorAudio::resetAudio()
{
	if(voiceL) { AXSetVoiceState(voiceL, AX_VOICE_STATE_STOPPED); AXSetVoiceCurrentOffset(voiceL, 0); }
	if(voiceR) { AXSetVoiceState(voiceR, AX_VOICE_STATE_STOPPED); AXSetVoiceCurrentOffset(voiceR, 0); }

	writeOffset = 0;
	queuedFrames = 0;
	lastHwFrame = 0;
	correction = 1.0f;
	started = false;

	memset(ringL, 0, sizeof(ringL));
	memset(ringR, 0, sizeof(ringR));
	DCFlushRange(ringL, sizeof(ringL));
	DCFlushRange(ringR, sizeof(ringR));
}

void WutEmulatorAudio::stop()
{
	if(voiceL) AXSetVoiceState(voiceL, AX_VOICE_STATE_STOPPED);
	if(voiceR) AXSetVoiceState(voiceR, AX_VOICE_STATE_STOPPED);
	started = false;
	queuedFrames = 0;
}

void WutEmulatorAudio::shutdown()
{
	stop();
	if(voiceL) { AXFreeVoice(voiceL); voiceL = nullptr; }
	if(voiceR) { AXFreeVoice(voiceR); voiceR = nullptr; }
}

void WutEmulatorAudio::pause(bool value)
{
	if(value == paused)
		return;
	paused = value;

	if(!started)
		return;

	const AXVoiceState state = paused ? AX_VOICE_STATE_STOPPED : AX_VOICE_STATE_PLAYING;
	if(voiceL) AXSetVoiceState(voiceL, state);
	if(voiceR) AXSetVoiceState(voiceR, state);
	if(!paused)
		lastHwFrame = AXGetVoiceCurrentOffsetEx(voiceL, ringL);
}

void WutEmulatorAudio::setSourceRate(uint32_t hz)
{
	if(hz < 4000 || hz > 96000)
		return;
	sourceRate = hz;
	applyRate();
}

/****************************************************************************
 * applyRate
 *
 * The voice plays at AX's native rate, so the sample rate conversion ratio is
 * the rate the game produced samples at over that rate, scaled by how fast the
 * emulation is currently running and the small queue-level correction.
 ***************************************************************************/
void WutEmulatorAudio::applyRate()
{
	if(!voiceL || !voiceR)
		return;

	const float axRate = (float) AXGetInputSamplesPerSec();
	const float ratio = ((float) sourceRate / axRate) * speed * correction;

	AXVoiceSrc src;
	memset(&src, 0, sizeof(src));
	src.ratio = (uint32_t) (0x00010000 * ratio);

	AXSetVoiceSrc(voiceL, &src);
	AXSetVoiceSrc(voiceR, &src);

	const uint16_t srcType = (src.ratio == 0x00010000) ? 0 : 1; // 0 bypasses the SRC
	AXSetVoiceSrcType(voiceL, srcType);
	AXSetVoiceSrcType(voiceR, srcType);
}

void WutEmulatorAudio::armAndStartVoices()
{
	if(!voiceL || !voiceR)
		return;

	// The oldest sample still queued: playback begins there
	const uint32_t startFrame = (writeOffset + RING_FRAMES - (queuedFrames % RING_FRAMES)) % RING_FRAMES;

	AXVoiceOffsets offsets;
	memset(&offsets, 0, sizeof(offsets));
	offsets.dataType = AX_VOICE_FORMAT_LPCM16;
	offsets.loopingEnabled = AX_VOICE_LOOP_ENABLED;
	offsets.loopOffset = 0;
	offsets.endOffset = RING_FRAMES - 1;
	offsets.currentOffset = startFrame;

	offsets.data = ringL;
	AXSetVoiceOffsets(voiceL, &offsets);
	offsets.data = ringR;
	AXSetVoiceOffsets(voiceR, &offsets);

	applyRate();
	lastHwFrame = startFrame;

	if(!paused)
	{
		AXSetVoiceState(voiceL, AX_VOICE_STATE_PLAYING);
		AXSetVoiceState(voiceR, AX_VOICE_STATE_PLAYING);
	}
}

/****************************************************************************
 * syncQueuedFrames
 *
 * Refreshes queuedFrames against the hardware read position. It is computed
 * as a bounded delta off the previous position (never more than what is known
 * to be queued), not as a raw subtraction, so a late or early reading cannot
 * wrap into a bogus value. What the hardware has consumed is cleared, so an
 * underrun plays silence instead of the ring's old contents.
 ***************************************************************************/
void WutEmulatorAudio::syncQueuedFrames()
{
	if(!started || !voiceL || paused)
		return;

	const uint32_t hwFrame = AXGetVoiceCurrentOffsetEx(voiceL, ringL) % RING_FRAMES;
	uint32_t consumed = (hwFrame + RING_FRAMES - lastHwFrame) % RING_FRAMES;

	if(consumed > queuedFrames)
		consumed = queuedFrames;

	for(uint32_t i = 0; i < consumed; i++)
	{
		const uint32_t at = (lastHwFrame + i) % RING_FRAMES;
		ringL[at] = 0;
		ringR[at] = 0;
	}
	if(consumed > 0)
	{
		// the cleared span may wrap, flush the whole ring's touched part
		const uint32_t first = std::min(consumed, RING_FRAMES - lastHwFrame);
		DCFlushRange(&ringL[lastHwFrame], first * sizeof(int16_t));
		DCFlushRange(&ringR[lastHwFrame], first * sizeof(int16_t));
		if(first < consumed)
		{
			DCFlushRange(&ringL[0], (consumed - first) * sizeof(int16_t));
			DCFlushRange(&ringR[0], (consumed - first) * sizeof(int16_t));
		}
	}

	queuedFrames -= consumed;
	lastHwFrame = hwFrame;
}

uint32_t WutEmulatorAudio::getQueuedFrames()
{
	syncQueuedFrames();
	return queuedFrames;
}

void WutEmulatorAudio::push(const int16_t * stereoBE, uint32_t frames, uint32_t hz, float emuSpeed)
{
	if(!voiceL || !voiceR || !stereoBE || frames == 0)
		return;

	if(hz != sourceRate)
		setSourceRate(hz);

	const float clamped = std::min(SPEED_MAX, std::max(SPEED_MIN, emuSpeed));

	syncQueuedFrames();

	// Gentle correction keeps the queue inside its band
	float wanted = 1.0f;
	if(queuedFrames > HIGH_WATER)
		wanted = CORRECT_DRAIN;
	else if(queuedFrames < LOW_WATER)
		wanted = CORRECT_FILL;

	if(wanted != correction || clamped != speed)
	{
		correction = wanted;
		speed = clamped;
		if(started)
			applyRate();
	}

	// Too much queued already: the oldest audio is no use, drop the new block
	if(queuedFrames + frames > MAX_QUEUED_FRAMES)
	{
#ifdef NOT64_TEST_INPUT
		audioDrops++;
		OSReport("[n64] audio block dropped, queue full (%u + %u)\n", (unsigned) queuedFrames, (unsigned) frames);
#endif
		return;
	}

	for(uint32_t i = 0; i < frames; i++)
	{
		ringL[writeOffset] = stereoBE[i * 2];
		ringR[writeOffset] = stereoBE[i * 2 + 1];
		writeOffset = (writeOffset + 1) % RING_FRAMES;
	}

	// flush the span just written (it may wrap)
	const uint32_t startAt = (writeOffset + RING_FRAMES - frames) % RING_FRAMES;
	const uint32_t first = std::min(frames, RING_FRAMES - startAt);
	DCFlushRange(&ringL[startAt], first * sizeof(int16_t));
	DCFlushRange(&ringR[startAt], first * sizeof(int16_t));
	if(first < frames)
	{
		DCFlushRange(&ringL[0], (frames - first) * sizeof(int16_t));
		DCFlushRange(&ringR[0], (frames - first) * sizeof(int16_t));
	}

	queuedFrames += frames;

#ifdef NOT64_TEST_INPUT
	{
		static unsigned pushes = 0, pushedFrames = 0;
		const unsigned drops = audioDrops;
		pushes++;
		pushedFrames += frames;
		if(pushes % 100 == 0)
			OSReport("[n64] audio pushes=%u frames=%u drops=%u queued=%u rate=%u speed=%.2f corr=%.3f started=%d\n",
				pushes, pushedFrames, drops, (unsigned) queuedFrames, (unsigned) sourceRate, speed, correction, (int) started);
	}
#endif

	if(!started && queuedFrames >= START_FRAMES)
	{
		armAndStartVoices();
		started = true;
	}
}

/****************************************************************************
 * C interface used by the N64 audio plugin (core/audio_wiiu.c)
 ***************************************************************************/
extern "C"
{
	int wiiu_audio_open(void)
	{
		if(!instance)
			return -1;
		instance->init();
		return 0;
	}

	void wiiu_audio_close(void)
	{
		if(instance)
			instance->stop();
	}

	void wiiu_audio_reset(void)
	{
		if(instance)
			instance->resetAudio();
	}

	void wiiu_audio_stop(void)
	{
		if(instance)
			instance->stop();
	}

	void wiiu_audio_pause(int paused)
	{
		if(instance)
			instance->pause(paused != 0);
	}

	void wiiu_audio_set_source_rate(unsigned int hz)
	{
		if(instance)
			instance->setSourceRate(hz);
	}

	void wiiu_audio_push(const short * stereo_be, unsigned int frames, unsigned int hz, float speed)
	{
		if(instance)
			instance->push(stereo_be, frames, hz, speed);
	}
}
