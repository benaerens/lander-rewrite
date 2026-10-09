// Software audio mixer: 16-bit stereo output at a fixed rate. The platform
// asks for samples with mixer_render(); the game starts/stops voices.
// Not thread safe: call everything from one thread (or under the platform's
// audio lock).
#ifndef MIXER_H
#define MIXER_H

#include "types.h"

typedef struct {
	int16_t *samples; // interleaved if channels == 2
	uint32_t frames;
	int channels;
	int rate;
} sfx_t;

sfx_t *sfx_load_wav(const char *game_path); // through fs (case-insensitive)
sfx_t *sfx_from_wav_memory(const uint8_t *data, uint32_t size);
void sfx_free(sfx_t *s);

void mixer_init(int out_rate);
int mixer_rate(void);

// Returns a voice handle (>= 0) or -1 if no voice is free.
int mixer_play(const sfx_t *s, float volume, float pan, float pitch, bool loop);
// count plays: 1 once, n > 1 that many times back to back (no gap), <= 0 forever
int mixer_play_n(const sfx_t *s, float volume, float pan, float pitch, int count);
void mixer_set(int voice, float volume, float pan, float pitch);
void mixer_stop(int voice);
void mixer_stop_all(void);
void mixer_pause(int voice, bool paused);
bool mixer_playing(int voice);
void mixer_set_master(float sfx_volume, float music_volume);

// Music: streams a PCM WAV file from disk (real file system path).
bool mixer_music_play(const char *real_path, bool loop);
void mixer_music_stop(void);
void mixer_music_pause(bool paused);
bool mixer_music_playing(void);

// Streamed PCM voice (film sound): the game pushes interleaved 16-bit samples, the mixer plays
// them in order at their own rate. One stream at a time; capacity in frames.
bool mixer_stream_open(int rate, int channels, float volume, int capacity_frames);
int mixer_stream_push(const int16_t *samples, int frames);  // returns the frames accepted
int mixer_stream_queued(void);                               // frames waiting to be played
void mixer_stream_drop(int frames);                          // discard the oldest frames
void mixer_stream_pause(bool paused);
void mixer_stream_close(void);

void mixer_render(int16_t *out, int frames); // stereo interleaved

#endif
