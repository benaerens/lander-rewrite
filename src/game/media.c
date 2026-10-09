// Films: Media_VideoPlay 0x4645d0 and its players (MCI 0x466580-0x4670c0, DirectShow 0x4672e0-0x467f30),
// decoded with pl_mpeg (re/specs/media.md §1).
// The original hands the screen to MCI / DirectShow: Renderer_Shutdown, the film full screen (MCI:
// its window maximised in the game's full-screen window, "put destination" = whole window; DirectShow:
// put_FullScreenMode), Esc ends it, then Renderer_SetMode. Here the picture is scaled to the whole
// render buffer (352x288 -> 640x480, the films are 4:3) and the sound goes through the mixer stream.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define PL_MPEG_IMPLEMENTATION
#include "../lib/pl_mpeg.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "media.h"
#include "../fs.h"
#include "../mixer.h"
#include "../platform.h"
#include "../render.h"

#define FILM_QUEUE 4
#define AUDIO_LEAD 0.15    // seconds of sound decoded ahead of the picture (covers the output buffers)
#define AUDIO_SLACK 0.10   // more than lead + slack queued (after a stall): drop the excess to stay in sync
#define AUDIO_CAPACITY 2.0 // seconds the mixer stream can hold
#define DRAIN_MAX 1.0      // after the last picture, wait at most this long for the queued sound

static const struct { int slot; const char *name; } slot_names[] = { // FS_Init list 0x4908d8
	{FILM_INTRO, "INTRO"}, {FILM_OUTRO, "OUTRO"}, {FILM_CUT1, "CUT1"}, {FILM_CUT2, "CUT2"},
	{FILM_CUT3, "CUT3"}, {FILM_CS4, "CS4"}, {FILM_CREDITS, "CREDITS"},
};

static struct {
	plm_t *plm;
	char name[32];
	double start;          // platform time of film time 0
	double paused_at;
	bool paused, ended;
	double end_at;         // platform time the decoder ended
	uint32_t *frame;       // decoded picture, fw x fh, 0xAARRGGBB
	int fw, fh;
	uint32_t *screen;      // the picture scaled to sw x sh
	int sw, sh;
	bool have_frame, frame_dirty;
	media_film_stats_t st;
	char queue[FILM_QUEUE][32];
	int queued;
} F;

const char *media_film_slot_name(int slot) {
	for (size_t i = 0; i < len(slot_names); i++) {
		if (slot_names[i].slot == slot) {
			return slot_names[i].name;
		}
	}
	return NULL;
}

// "%s.mpg" of the slot name (FUN_00467280 kind 3), found like FS_AssignSlot: data folder, then MISC
static bool film_path(const char *name, char *out, size_t size) {
	char path[96];
	const char *dot = strrchr(name, '.');
	const char *ext = dot && !strchr(dot, '\\') && !strchr(dot, '/') ? "" : ".MPG";
	const char *dirs[] = {"", "MISC\\"};
	for (size_t i = 0; i < len(dirs); i++) {
		snprintf(path, sizeof(path), "%s%s%s", dirs[i], name, ext);
		if (fs_resolve(path, out, size)) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------- decoding

static void on_video(plm_t *plm, plm_frame_t *frame, void *user) {
	if ((int)frame->width != F.fw || (int)frame->height != F.fh) {
		free(F.frame);
		F.fw = (int)frame->width;
		F.fh = (int)frame->height;
		F.frame = malloc(sizeof(uint32_t) * F.fw * F.fh);
		for (int i = 0; i < F.fw * F.fh; i++) {
			F.frame[i] = 0xff000000; // the conversion leaves the alpha bytes alone
		}
	}
	plm_frame_to_bgra(frame, (uint8_t *)F.frame, F.fw * 4); // B,G,R,A bytes = 0xAARRGGBB little endian
	F.have_frame = F.frame_dirty = true;
	F.st.video_frames++;
}

static void on_audio(plm_t *plm, plm_samples_t *samples, void *user) {
	int16_t pcm[PLM_AUDIO_SAMPLES_PER_FRAME * 2];
	int n = (int)samples->count;
	for (int i = 0; i < n * 2; i++) {
		float s = samples->interleaved[i] * 32767.0f;
		pcm[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
	}
	platform_audio_lock();
	int got = mixer_stream_push(pcm, n);
	platform_audio_unlock();
	F.st.audio_frames += n;
	F.st.audio_pushed += got;
	F.st.audio_dropped += n - got;
}

// a + (b - a) * w / 256 for each colour byte (red and blue together, then green)
static inline uint32_t lerp_px(uint32_t a, uint32_t b, uint32_t w) {
	uint32_t rb = (((a & 0xff00ff) * (256 - w) + (b & 0xff00ff) * w) >> 8) & 0xff00ff;
	uint32_t g = (((a & 0x00ff00) * (256 - w) + (b & 0x00ff00) * w) >> 8) & 0x00ff00;
	return 0xff000000u | rb | g;
}

// source position of destination pixel i (centres aligned), as index + 8-bit weight
static void scale_map(int dst, int src, int *index, uint16_t *weight) {
	for (int i = 0; i < dst; i++) {
		int p = (int)(((i + 0.5) * src / dst - 0.5) * 256);
		p = clamp(p, 0, (src - 1) * 256);
		index[i] = p >> 8;
		weight[i] = (uint16_t)(p & 255);
	}
}

// bilinear scale of the decoded picture to the render size: every source row horizontally, then
// the rows blended vertically
static void scale_frame(void) {
	static uint32_t *rows;
	static int map_fw, map_fh, *xi, *yi;
	static uint16_t *xw, *yw;
	int sw = render_width(), sh = render_height(), fw = F.fw, fh = F.fh;
	if (sw != F.sw || sh != F.sh || !F.screen || map_fw != fw || map_fh != fh) {
		free(F.screen);
		free(rows);
		free(xi);
		free(yi);
		free(xw);
		free(yw);
		F.sw = sw;
		F.sh = sh;
		F.screen = malloc(sizeof(uint32_t) * sw * sh);
		map_fw = fw;
		map_fh = fh;
		rows = malloc(sizeof(uint32_t) * fh * sw);
		xi = malloc(sizeof(int) * sw);
		xw = malloc(sizeof(uint16_t) * sw);
		yi = malloc(sizeof(int) * sh);
		yw = malloc(sizeof(uint16_t) * sh);
		scale_map(sw, fw, xi, xw);
		scale_map(sh, fh, yi, yw);
	}
	for (int y = 0; y < fh; y++) {
		const uint32_t *s = F.frame + y * fw;
		uint32_t *d = rows + y * sw;
		for (int x = 0; x < sw; x++) {
			int a = xi[x];
			d[x] = lerp_px(s[a], s[a + 1 < fw ? a + 1 : a], xw[x]);
		}
	}
	for (int y = 0; y < sh; y++) {
		const uint32_t *r0 = rows + yi[y] * sw, *r1 = rows + min(yi[y] + 1, fh - 1) * sw;
		uint32_t *d = F.screen + y * sw;
		uint32_t w = yw[y];
		for (int x = 0; x < sw; x++) {
			d[x] = lerp_px(r0[x], r1[x], w);
		}
	}
	F.frame_dirty = false;
}

static void draw_current(void) {
	uint32_t *px = render_pixels();
	int n = render_width() * render_height();
	if (!F.have_frame) {
		for (int i = 0; i < n; i++) px[i] = 0xff000000;
		return;
	}
	if (F.frame_dirty || F.sw != render_width() || F.sh != render_height()) {
		clock_t c0 = clock();
		scale_frame();
		F.st.scale_cpu += (double)(clock() - c0) / CLOCKS_PER_SEC;
	}
	memcpy(px, F.screen, sizeof(uint32_t) * n);
}

// ---------------------------------------------------------------- open / close

static void film_stop(void) { // the current film only (the queue stays)
	if (F.plm) {
		plm_destroy(F.plm);
		F.plm = NULL;
	}
	platform_audio_lock();
	mixer_stream_close();
	platform_audio_unlock();
	F.have_frame = F.frame_dirty = false;
	F.paused = F.ended = false;
	F.name[0] = 0;
}

static bool film_start(const char *name) {
	film_stop();
	char real[1024];
	if (!name || !*name || !film_path(name, real, sizeof(real))) {
		return false;
	}
	plm_t *plm = plm_create_with_filename(real);
	if (!plm) {
		return false;
	}
	if (!plm_has_headers(plm) || !plm_get_num_video_streams(plm)) {
		plm_destroy(plm);
		return false;
	}
	F.plm = plm;
	snprintf(F.name, sizeof(F.name), "%s", name);
	memset(&F.st, 0, sizeof(F.st));
	F.st.width = plm_get_width(plm);
	F.st.height = plm_get_height(plm);
	F.st.framerate = plm_get_framerate(plm);
	F.st.duration = plm_get_duration(plm);
	plm_set_loop(plm, 0);
	plm_set_video_decode_callback(plm, on_video, NULL);
	if (plm_get_num_audio_streams(plm) > 0 && plm_get_samplerate(plm) > 0) {
		F.st.samplerate = plm_get_samplerate(plm);
		plm_set_audio_enabled(plm, 1);
		plm_set_audio_stream(plm, 0);
		plm_set_audio_lead_time(plm, AUDIO_LEAD);
		plm_set_audio_decode_callback(plm, on_audio, NULL);
		platform_audio_lock();
		mixer_stream_open(F.st.samplerate, 2, 1.0f, (int)(F.st.samplerate * AUDIO_CAPACITY));
		platform_audio_unlock();
	} else {
		plm_set_audio_enabled(plm, 0);
	}
	F.start = platform_now();
	return true;
}

// the next queued film that exists, or nothing
static bool film_next(void) {
	film_stop();
	while (F.queued > 0) {
		char name[32];
		snprintf(name, sizeof(name), "%s", F.queue[0]);
		memmove(F.queue[0], F.queue[1], sizeof(F.queue[0]) * (FILM_QUEUE - 1));
		F.queued--;
		if (film_start(name)) {
			return true;
		}
	}
	return false;
}

bool media_film_open(const char *name) { // Media_VideoPlay 0x4645d0
	media_music_stop();                   // "if (music playing) Music_Stop()"
	F.queued = 0;
	return film_start(name);
}

bool media_level_end_films(const char *movie, int world) {
	// if (gLevelEndMoviePending) { Media_VideoPlay(0x31); if (gCurWorld == 0x13) Media_VideoPlay(0x30); }
	if (!movie || !*movie) {
		return false;
	}
	bool ok = media_film_open(movie);
	if (world == 0x13) {
		ok |= media_film_queue(media_film_slot_name(FILM_CREDITS));
	}
	return ok;
}

bool media_film_queue(const char *name) {
	char real[1024];
	if (!name || !*name || !film_path(name, real, sizeof(real))) {
		return false;
	}
	if (!F.plm) {
		return media_film_open(name);
	}
	if (F.queued >= FILM_QUEUE) {
		return false;
	}
	snprintf(F.queue[F.queued++], sizeof(F.queue[0]), "%s", name);
	return true;
}

void media_film_close(void) {
	F.queued = 0;
	film_stop();
	free(F.frame);
	free(F.screen);
	F.frame = F.screen = NULL;
	F.fw = F.fh = F.sw = F.sh = 0;
}

bool media_film_playing(void) { return F.plm != NULL; }
const char *media_film_name(void) { return F.name; }

bool media_film_stats(media_film_stats_t *out) {
	if (!F.plm) {
		return false;
	}
	*out = F.st;
	return true;
}

// ---------------------------------------------------------------- playing

bool media_film_update(double t) {
	if (!F.plm) {
		return false;
	}
	if (!F.ended) {
		double now = plm_get_time(F.plm);
		if (t > now) {
			clock_t c0 = clock();
			plm_decode(F.plm, t - now);
			F.st.decode_cpu += (double)(clock() - c0) / CLOCKS_PER_SEC;
		}
		if (plm_has_ended(F.plm)) {
			F.ended = true;
		}
		if (F.st.samplerate) { // keep the queued sound within the lead (a stall left too much behind)
			platform_audio_lock();
			int queued = mixer_stream_queued();
			int keep = (int)(F.st.samplerate * AUDIO_LEAD) + PLM_AUDIO_SAMPLES_PER_FRAME;
			if (queued > (int)(F.st.samplerate * (AUDIO_LEAD + AUDIO_SLACK)) + PLM_AUDIO_SAMPLES_PER_FRAME) {
				mixer_stream_drop(queued - keep);
				F.st.audio_dropped += queued - keep;
			}
			platform_audio_unlock();
		}
	}
	draw_current();
	return !F.ended;
}

bool media_film_frame(void) {
	if (!F.plm) {
		draw_current();
		return false;
	}
	// keys of the film players: Esc ends the film (FUN_00466580 -> 1 / window proc VK_ESCAPE),
	// P pauses (MCI: P pause, R play; DirectShow: P toggles)
	if (input_pressed(BTN_KEY(KEY_ESCAPE))) {
		if (!film_next()) {
			draw_current();
			return false;
		}
	}
	if (input_pressed(BTN_KEY('p')) || (F.paused && input_pressed(BTN_KEY('r')))) {
		if (!F.paused) {
			F.paused = true;
			F.paused_at = platform_now();
		} else {
			F.paused = false;
			F.start += platform_now() - F.paused_at;
		}
		platform_audio_lock();
		mixer_stream_pause(F.paused);
		platform_audio_unlock();
	}
	double now = F.paused ? F.paused_at : platform_now();
	bool was_ended = F.ended;
	if (media_film_update(now - F.start)) {
		return true;
	}
	if (!was_ended) {
		F.end_at = platform_now();
	}
	// the picture has ended: let the sound decoded ahead finish (the original ends at "position >= length")
	platform_audio_lock();
	int queued = mixer_stream_queued();
	platform_audio_unlock();
	if (queued > 0 && platform_now() - F.end_at < DRAIN_MAX) {
		return true;
	}
	if (film_next()) {
		media_film_update(0);
		return true;
	}
	draw_current();
	return false;
}
