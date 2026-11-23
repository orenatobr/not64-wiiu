#ifndef WIIU_AUDIO_H
#define WIIU_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* C interface to the Wii U audio output used by the audio plugin.
 * Implemented by wiiu/source/drivers/wut/WutN64Audio.cpp. */

/* Returns 0 on success. Safe to call more than once. */
int  wiiu_audio_open(void);
void wiiu_audio_close(void);

/* Empties the queue and rewinds the playback position. */
void wiiu_audio_reset(void);
void wiiu_audio_stop(void);
void wiiu_audio_pause(int paused);

/* The rate the game programmed into the AI DAC, in Hz. */
void wiiu_audio_set_source_rate(unsigned int hz);

/* Queues `frames` stereo frames of big-endian signed 16-bit samples
 * (left, right, left, right ...) that were produced at `hz`.
 * `speed` is the emulation speed relative to real time (1.0 = normal). */
void wiiu_audio_push(const short *stereo_be, unsigned int frames, unsigned int hz, float speed);

#ifdef __cplusplus
}
#endif

#endif
