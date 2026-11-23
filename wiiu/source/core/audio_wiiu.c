/* Audio plugin for the native Wii U.
 *
 * The N64's audio interface (AI) hands the emulator a block of 16-bit stereo
 * samples in RDRAM every time the game starts a DMA. This plugin forwards each
 * block to the Wii U audio driver (wiiu/source/drivers/wut/WutN64Audio.cpp,
 * which plays through AX) together with the DAC rate the game programmed.
 */
#include <string.h>

#include "../../../main/winlnxdefs.h"
#include "../../../gc_audio/AudioPlugin.h"
#include "../../../gc_audio/Audio_#1.1.h"
#include "../../../main/timers.h"
#include "../../../main/wii64config.h"

#include "audio_wiiu.h"

#define DEFAULT_FREQUENCY 33600

AUDIO_INFO AudioInfo;
extern float VILimit;

char audioEnabled;
char scalePitch;

static unsigned int freq = DEFAULT_FREQUENCY;

EXPORT void CALL AiDacrateChanged(int SystemType)
{
	switch (SystemType) {
	case SYSTEM_NTSC:
		freq = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	case SYSTEM_PAL:
		freq = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	case SYSTEM_MPAL:
		freq = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1);
		break;
	default:
		freq = DEFAULT_FREQUENCY;
	}

	wiiu_audio_set_source_rate(freq);
}

EXPORT void CALL AiLenChanged(void)
{
	const unsigned char *stream;
	unsigned int length;
	float speed = 1.0f;

	if (!audioEnabled)
		return;

	stream = (const unsigned char *)AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF);
	length = *AudioInfo.AI_LEN_REG & 0x3FFFF;

	/* The game runs slower than real time when the host cannot keep up (or
	 * faster with the frame limiter off): play the audio at the same speed so
	 * the queue neither starves nor grows. */
	if (scalePitch || Timers.vis > VILimit)
		speed = (VILimit > 0.0f) ? (float)Timers.vis / VILimit : 1.0f;

	/* 4 bytes per stereo frame: 16-bit left, 16-bit right, big endian. */
	wiiu_audio_push((const short *)stream, length / 4, freq, speed);
}

EXPORT void CALL CloseDLL(void)
{
	wiiu_audio_close();
}

EXPORT BOOL CALL InitiateAudio(AUDIO_INFO Audio_Info)
{
	AudioInfo = Audio_Info;
	return wiiu_audio_open() == 0 ? TRUE : FALSE;
}

EXPORT void CALL RomOpen(void)
{
	wiiu_audio_reset();
}

EXPORT void CALL RomClosed(void)
{
	wiiu_audio_stop();
}

EXPORT void CALL ProcessAlist(void)
{
}

void pauseAudio(void)
{
	wiiu_audio_pause(1);
}

void resumeAudio(void)
{
	wiiu_audio_pause(0);
}
