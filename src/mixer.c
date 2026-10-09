#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mixer.h"
#include "fs.h"

#define MAX_VOICES 48

typedef struct {
	const sfx_t *sfx;
	double pos; // in source frames
	float volume, pan, pitch;
	bool loop, paused, active;
	uint32_t generation;
	int remaining;    // looping voice: plays left including this one (0 = forever)
} voice_t;

static struct {
	int rate;
	voice_t voices[MAX_VOICES];
	uint32_t generation;
	float sfx_volume, music_volume;

	FILE *music;
	long music_data_start;
	uint32_t music_data_len, music_read;
	int music_channels, music_rate;
	bool music_loop, music_paused;
	int16_t music_buf[4096 * 2];
	int music_buf_frames, music_buf_pos;
	double music_frac;

	struct { // streamed PCM voice (stereo ring buffer)
		int16_t *buf;
		int cap, head, count, rate, channels;
		float volume;
		bool paused;
		double frac;
	} stream;
} M;

// ---------------------------------------------------------------- WAV files

static bool parse_wav(const uint8_t *d, uint32_t size, int *channels, int *rate, int *bits,
	uint32_t *data_off, uint32_t *data_len) {
	if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
		return false;
	}
	bool fmt = false;
	uint32_t o = 12;
	while (o + 8 <= size) {
		uint32_t len = get_u32(d + o + 4);
		if (!memcmp(d + o, "fmt ", 4) && o + 8 + 16 <= size) {
			if (get_u16(d + o + 8) != 1) {
				return false; // PCM only
			}
			*channels = get_u16(d + o + 10);
			*rate = get_u32(d + o + 12);
			*bits = get_u16(d + o + 22);
			fmt = true;
		} else if (!memcmp(d + o, "data", 4)) {
			*data_off = o + 8;
			*data_len = min(len, size - (o + 8));
			return fmt;
		}
		o += 8 + len + (len & 1);
	}
	return false;
}

sfx_t *sfx_from_wav_memory(const uint8_t *data, uint32_t size) {
	int channels, rate, bits;
	uint32_t off, len;
	if (!parse_wav(data, size, &channels, &rate, &bits, &off, &len) || channels < 1 || channels > 2) {
		return NULL;
	}
	sfx_t *s = calloc(1, sizeof(sfx_t));
	s->channels = channels;
	s->rate = rate;
	if (bits == 16) {
		s->frames = len / (2 * channels);
		s->samples = malloc(s->frames * channels * sizeof(int16_t) + 1);
		for (uint32_t i = 0; i < s->frames * channels; i++) {
			s->samples[i] = get_i16(data + off + i * 2);
		}
	} else if (bits == 8) {
		s->frames = len / channels;
		s->samples = malloc(s->frames * channels * sizeof(int16_t) + 1);
		for (uint32_t i = 0; i < s->frames * channels; i++) {
			s->samples[i] = (int16_t)((data[off + i] - 128) << 8);
		}
	} else {
		free(s);
		return NULL;
	}
	return s;
}

sfx_t *sfx_load_wav(const char *game_path) {
	uint32_t size;
	uint8_t *d = fs_load(game_path, &size);
	if (!d) {
		return NULL;
	}
	sfx_t *s = sfx_from_wav_memory(d, size);
	free(d);
	return s;
}

void sfx_free(sfx_t *s) {
	if (s) {
		for (int i = 0; i < MAX_VOICES; i++) {
			if (M.voices[i].sfx == s) {
				M.voices[i].active = false;
			}
		}
		free(s->samples);
		free(s);
	}
}

// ---------------------------------------------------------------- voices

void mixer_init(int out_rate) {
	memset(&M, 0, sizeof(M));
	M.rate = out_rate;
	M.sfx_volume = M.music_volume = 1;
}

int mixer_rate(void) { return M.rate; }

void mixer_set_master(float sfx_volume, float music_volume) {
	M.sfx_volume = sfx_volume;
	M.music_volume = music_volume;
}

// Handles encode the slot and a generation so stale handles are ignored.
static voice_t *voice_get(int handle) {
	if (handle < 0) {
		return NULL;
	}
	voice_t *v = &M.voices[handle % MAX_VOICES];
	return v->active && v->generation == (uint32_t)(handle / MAX_VOICES) ? v : NULL;
}

// count plays: 1 once, n > 1 that many times back to back, <= 0 forever
int mixer_play_n(const sfx_t *s, float volume, float pan, float pitch, int count) {
	int h = mixer_play(s, volume, pan, pitch, count != 1);
	if (h >= 0 && count > 1) {
		M.voices[h % MAX_VOICES].remaining = count;
	}
	return h;
}

int mixer_play(const sfx_t *s, float volume, float pan, float pitch, bool loop) {
	if (!s || !s->frames) {
		return -1;
	}
	for (int i = 0; i < MAX_VOICES; i++) {
		voice_t *v = &M.voices[i];
		if (!v->active) {
			M.generation = (M.generation + 1) & 0xffffff;
			*v = (voice_t){s, 0, volume, pan, pitch, loop, false, true, M.generation, 0};
			return M.generation * MAX_VOICES + i;
		}
	}
	return -1;
}

void mixer_set(int voice, float volume, float pan, float pitch) {
	voice_t *v = voice_get(voice);
	if (v) {
		v->volume = volume;
		v->pan = pan;
		v->pitch = pitch;
	}
}

void mixer_stop(int voice) {
	voice_t *v = voice_get(voice);
	if (v) {
		v->active = false;
	}
}

void mixer_stop_all(void) {
	for (int i = 0; i < MAX_VOICES; i++) {
		M.voices[i].active = false;
	}
}

void mixer_pause(int voice, bool paused) {
	voice_t *v = voice_get(voice);
	if (v) {
		v->paused = paused;
	}
}

bool mixer_playing(int voice) { return voice_get(voice) != NULL; }

// ---------------------------------------------------------------- music

void mixer_music_stop(void) {
	if (M.music) {
		fclose(M.music);
		M.music = NULL;
	}
}

bool mixer_music_play(const char *real_path, bool loop) {
	mixer_music_stop();
	FILE *f = fopen(real_path, "rb");
	if (!f) {
		return false;
	}
	uint8_t hdr[256];
	size_t n = fread(hdr, 1, sizeof(hdr), f);
	int channels, rate, bits;
	uint32_t off, len;
	if (!parse_wav(hdr, (uint32_t)n, &channels, &rate, &bits, &off, &len) || bits != 16) {
		fclose(f);
		return false;
	}
	// parse_wav clamps len to the header buffer; take the real size from the chunk
	len = get_u32(hdr + off - 4);
	M.music = f;
	M.music_data_start = off;
	M.music_data_len = len;
	M.music_read = 0;
	M.music_channels = channels;
	M.music_rate = rate;
	M.music_loop = loop;
	M.music_paused = false;
	M.music_buf_frames = M.music_buf_pos = 0;
	M.music_frac = 0;
	fseek(f, off, SEEK_SET);
	return true;
}

void mixer_music_pause(bool paused) { M.music_paused = paused; }
bool mixer_music_playing(void) { return M.music != NULL; }

static bool music_fill(void) {
	uint32_t frame_bytes = 2 * M.music_channels;
	uint32_t left = M.music_data_len - M.music_read;
	if (left < frame_bytes) {
		if (!M.music_loop) {
			mixer_music_stop();
			return false;
		}
		fseek(M.music, M.music_data_start, SEEK_SET);
		M.music_read = 0;
		left = M.music_data_len;
	}
	uint32_t want = min((uint32_t)(len(M.music_buf) / M.music_channels), left / frame_bytes);
	uint8_t raw[sizeof(M.music_buf)];
	size_t got = fread(raw, frame_bytes, want, M.music);
	if (got == 0) {
		mixer_music_stop();
		return false;
	}
	for (size_t i = 0; i < got * M.music_channels; i++) {
		M.music_buf[i] = get_i16(raw + i * 2);
	}
	M.music_read += got * frame_bytes;
	M.music_buf_frames = (int)got;
	M.music_buf_pos = 0;
	return true;
}

// ---------------------------------------------------------------- stream

void mixer_stream_close(void) {
	free(M.stream.buf);
	memset(&M.stream, 0, sizeof(M.stream));
}

bool mixer_stream_open(int rate, int channels, float volume, int capacity_frames) {
	mixer_stream_close();
	if (rate <= 0 || channels < 1 || channels > 2 || capacity_frames <= 0) {
		return false;
	}
	M.stream.buf = malloc(sizeof(int16_t) * 2 * capacity_frames);
	if (!M.stream.buf) {
		return false;
	}
	M.stream.cap = capacity_frames;
	M.stream.rate = rate;
	M.stream.channels = channels;
	M.stream.volume = volume;
	return true;
}

int mixer_stream_push(const int16_t *samples, int frames) {
	int n = min(frames, M.stream.cap - M.stream.count);
	for (int i = 0; i < n; i++) {
		int16_t *d = &M.stream.buf[((M.stream.head + M.stream.count + i) % M.stream.cap) * 2];
		d[0] = samples[i * M.stream.channels];
		d[1] = samples[i * M.stream.channels + M.stream.channels - 1];
	}
	M.stream.count += n > 0 ? n : 0;
	return n > 0 ? n : 0;
}

int mixer_stream_queued(void) { return M.stream.count; }

void mixer_stream_drop(int frames) {
	int n = clamp(frames, 0, M.stream.count);
	if (M.stream.cap) {
		M.stream.head = (M.stream.head + n) % M.stream.cap;
	}
	M.stream.count -= n;
}

void mixer_stream_pause(bool paused) { M.stream.paused = paused; }

// ---------------------------------------------------------------- mixing

void mixer_render(int16_t *out, int frames) {
	static float acc[8192 * 2];
	while (frames > 0) {
		int n = min(frames, 8192);
		memset(acc, 0, sizeof(float) * n * 2);

		for (int vi = 0; vi < MAX_VOICES; vi++) {
			voice_t *v = &M.voices[vi];
			if (!v->active || v->paused) {
				continue;
			}
			const sfx_t *s = v->sfx;
			double step = (double)s->rate * v->pitch / M.rate;
			float vol = v->volume * M.sfx_volume;
			float pan = clamp(v->pan, -1.0f, 1.0f);
			float gl = vol * (pan > 0 ? 1 - pan : 1), gr = vol * (pan < 0 ? 1 + pan : 1);
			for (int i = 0; i < n; i++) {
				uint32_t p = (uint32_t)v->pos;
				if (p >= s->frames) {
					if (!v->loop || (v->remaining > 0 && --v->remaining == 0)) {
						v->active = false;
						break;
					}
					v->pos -= s->frames;
					p = (uint32_t)v->pos;
				}
				float t = (float)(v->pos - p);
				uint32_t q = p + 1 < s->frames ? p + 1 : (v->loop ? 0 : p);
				float l, r;
				if (s->channels == 2) {
					l = s->samples[p * 2] + (s->samples[q * 2] - s->samples[p * 2]) * t;
					r = s->samples[p * 2 + 1] + (s->samples[q * 2 + 1] - s->samples[p * 2 + 1]) * t;
				} else {
					l = r = s->samples[p] + (s->samples[q] - s->samples[p]) * t;
				}
				acc[i * 2] += l * gl;
				acc[i * 2 + 1] += r * gr;
				v->pos += step;
			}
		}

		if (M.music && !M.music_paused) {
			double step = (double)M.music_rate / M.rate;
			for (int i = 0; i < n && M.music; i++) {
				if (M.music_buf_pos >= M.music_buf_frames && !music_fill()) {
					break;
				}
				int16_t *f = &M.music_buf[M.music_buf_pos * M.music_channels];
				float l = f[0], r = M.music_channels == 2 ? f[1] : f[0];
				acc[i * 2] += l * M.music_volume;
				acc[i * 2 + 1] += r * M.music_volume;
				M.music_frac += step;
				while (M.music_frac >= 1) {
					M.music_frac -= 1;
					M.music_buf_pos++;
				}
			}
		}

		if (M.stream.count && !M.stream.paused) { // linear interpolation between ring frames
			double step = (double)M.stream.rate / M.rate;
			float vol = M.stream.volume;
			for (int i = 0; i < n && M.stream.count; i++) {
				const int16_t *a = &M.stream.buf[M.stream.head * 2];
				const int16_t *b = M.stream.count > 1 ? &M.stream.buf[((M.stream.head + 1) % M.stream.cap) * 2] : a;
				float t = (float)M.stream.frac;
				acc[i * 2] += (a[0] + (b[0] - a[0]) * t) * vol;
				acc[i * 2 + 1] += (a[1] + (b[1] - a[1]) * t) * vol;
				M.stream.frac += step;
				while (M.stream.frac >= 1 && M.stream.count) {
					M.stream.frac -= 1;
					M.stream.head = (M.stream.head + 1) % M.stream.cap;
					M.stream.count--;
				}
			}
		}

		for (int i = 0; i < n * 2; i++) {
			float s = acc[i];
			out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
		}
		out += n * 2;
		frames -= n;
	}
}
