// Films, pictures and music test, run by the headless platform (platform_headless.c drives the frames).
//
// film_test --data <dir> --mode bench [--film NAME] --out <dir>
//     decodes every film (or one) as fast as possible on a 25 fps film clock, mixing the sound like
//     the platform would; prints decode speed and sound sample counts; PNGs of the intro at points.
// film_test --data <dir> --mode play --film NAME --frames N --fps F --shot-at a,b --out <dir>
//     plays a film through media_film_frame() on the platform clock (Esc in --script skips it).
// film_test --data <dir> --mode boot --frames N --fps F --shot-at ... --out <dir> [--script keys]
//     the start-up sequence (intro, soundtrack picture, logo).
// film_test --data <dir> --mode slides --out <dir>   every picture + the loading screen as PNGs
// film_test --data <dir> --mode music --frames N --fps F [--track n]
//     Media_AudioPlay(n) and Media_Poll on the platform clock: prints the CD track changes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "../src/platform.h"
#include "../src/render.h"
#include "../src/mixer.h"
#include "../src/fs.h"
#include "../src/png.h"
#include "../src/game/media.h"

static const char *mode = "bench", *film = NULL, *out_dir = ".";
static int track = 0, world = -1, frame_no, failures;
static long mixed_frames; // stream frames consumed (bench)

static double wall(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void shot(const char *name) {
	char path[600];
	snprintf(path, sizeof(path), "%s/%s.png", out_dir, name);
	png_write(path, render_pixels(), render_width(), render_height());
	printf("  wrote %s\n", path);
}

static void check(bool ok, const char *what) {
	printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

// ---------------------------------------------------------------- bench

static void bench_film(const char *name, const double *shots, int num_shots) {
	if (!media_film_open(name)) {
		printf("%s: not on the disc\n", name);
		return;
	}
	media_film_stats_t st;
	media_film_stats(&st);
	printf("%s: %dx%d %.2f fps, %.1f s, sound %d Hz\n", name, st.width, st.height, st.framerate, st.duration,
		st.samplerate);
	int16_t mix[2 * 2048];
	double dt = 1.0 / 25, t = 0, t0 = wall(), mix_due = 0;
	int next_shot = 0;
	long before = 0;
	for (;;) {
		t += dt;
		bool more = media_film_update(t);
		media_film_stats(&st);
		// the platform takes dt of sound from the mixer after each frame
		before = mixer_stream_queued();
		mix_due += mixer_rate() * dt;
		while (mix_due >= 1) {
			int n = (int)min(mix_due, 2048.0);
			mixer_render(mix, n);
			mix_due -= n;
		}
		mixed_frames += before - mixer_stream_queued();
		if (next_shot < num_shots && t >= shots[next_shot]) {
			char n[64];
			snprintf(n, sizeof(n), "%s_%03ds", name, (int)shots[next_shot]);
			shot(n);
			next_shot++;
		}
		if (!more) {
			break;
		}
	}
	double secs = wall() - t0;
	long expected = (long)(st.duration * st.samplerate);
	printf("  decoded %ld frames in %.2f s wall: %.0f frames/s, %.1fx real time (film %.1f s)\n",
		st.video_frames, secs, st.video_frames / secs, st.duration / secs, st.duration);
	printf("  processor time: decode %.2f s (%.2f ms/frame), scale to 640x480 %.2f s (%.2f ms/frame)\n",
		st.decode_cpu, st.decode_cpu * 1000 / max(st.video_frames, 1), st.scale_cpu,
		st.scale_cpu * 1000 / max(st.video_frames, 1));
	printf("  sound: %ld frames decoded (%.2f s), expected ~%ld (%.2f s); pushed %ld, dropped %ld, mixed %ld, left %d\n",
		st.audio_frames, (double)st.audio_frames / st.samplerate, expected, st.duration, st.audio_pushed,
		st.audio_dropped, mixed_frames, mixer_stream_queued());
	mixed_frames = 0;
	check(st.video_frames >= (long)(st.duration * st.framerate) - 2, "all video frames decoded");
	check(labs(st.audio_frames - expected) < st.samplerate / 2, "sound length matches the film (within 0.5 s)");
	check(st.audio_dropped == 0 && st.audio_pushed == st.audio_frames, "every sound frame went to the mixer");
	check(st.duration / secs > 2.0, "decodes faster than 2x real time");
	media_film_close();
}

// A 1 s hitch in the program: the audio output went on (silence after the queued sound), then the
// decoder catches up. The sound must be cut back to the lead so it stays in step with the picture.
static void stall_test(void) {
	if (!media_film_open("CUT2")) {
		return;
	}
	int16_t mix[2 * 2048];
	double t = 0;
	for (int i = 0; i < 50; i++) { // 2 s of normal play
		t += 0.04;
		media_film_update(t);
		mixer_render(mix, (int)(mixer_rate() * 0.04));
	}
	for (int i = 0; i < 25; i++) { // the hitch: 1 s of output, no update
		mixer_render(mix, (int)(mixer_rate() * 0.04));
	}
	t += 1.0;
	media_film_update(t);
	media_film_stats_t st;
	media_film_stats(&st);
	int queued = mixer_stream_queued();
	printf("stall: after a 1 s hitch %d frames queued (%.3f s), %ld dropped\n", queued, (double)queued / st.samplerate,
		st.audio_dropped);
	check(st.audio_dropped > 0 && queued <= st.samplerate * 0.15 + 2 * 1152, "sound cut back to the lead after a hitch");
	check(st.video_frames == 75 || st.video_frames == 76, "picture caught up (3 s = 75 frames)");
	media_film_close();
}

static void bench(void) {
	static const double intro_shots[] = {1, 5, 10, 20, 30, 40, 50, 60};
	static const double other_shots[] = {3, 30};
	const char *names[] = {"INTRO", "CUT1", "CUT2", "CUT3", "OUTRO", "CREDITS"};
	for (size_t i = 0; i < len(names); i++) {
		if (film && strcasecmp(film, names[i])) {
			continue;
		}
		bool intro = !strcmp(names[i], "INTRO");
		bench_film(names[i], intro ? intro_shots : other_shots, intro ? (int)len(intro_shots) : 2);
	}
	if (!film || !strcasecmp(film, "none")) { // --film none: only these checks
		stall_test();
		check(!media_film_open("CS4"), "CS4 (slot 0x2f) is not on the disc");
		check(media_film_slot_name(FILM_CREDITS) && !strcmp(media_film_slot_name(FILM_CREDITS), "CREDITS"), "slot names");
	}
}

// ---------------------------------------------------------------- slides

static void slides(void) {
	for (int set = 0; set < 2; set++) {
		for (int k = 0; k < (set ? 6 : 12); k++) {
			char name[32];
			snprintf(name, sizeof(name), "%s/%d", set ? "slides" : "screens", k);
			bool ok = media_slide_load(name);
			render_frame_begin(rgba(255, 0, 255, 255));
			media_slide_draw();
			snprintf(name, sizeof(name), "%s%d", set ? "slides" : "screens", k);
			check(ok, name);
			if (k == 0 || k == 2 || k == 11) {
				shot(name);
			}
			media_slide_unload();
		}
	}
	check(!media_slide_load("slides/6"), "slides/6 does not exist");
	media_slide_select_dir(0);
	check(media_slide_open(-1), "random front-end background");
	media_slide_unload();
	render_frame_begin(rgba(0, 0, 0, 255));
	media_loading_draw(0);
	shot("loading_000");
	media_loading_draw(160);
	shot("loading_160");
	media_loading_done();
}

// ---------------------------------------------------------------- game entry points

void game_init(int argc, char **argv) {
	const char *data = "../cd";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
		else if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
		else if (!strcmp(argv[i], "--film") && i + 1 < argc) film = argv[++i];
		else if (!strcmp(argv[i], "--track") && i + 1 < argc) track = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--level-end-world") && i + 1 < argc) world = atoi(argv[++i]);
	}
	out_dir = platform_save_dir(); // the headless platform's --out
	fs_set_root(data);
	render_init(640, 480);
	mixer_init(44100);
	if (!strcmp(mode, "bench")) {
		bench();
	} else if (!strcmp(mode, "slides")) {
		slides();
	} else if (!strcmp(mode, "play")) {
		bool ok = world >= 0 ? media_level_end_films(film, world) : media_film_open(film ? film : "INTRO");
		if (!ok) {
			printf("film %s not found\n", film ? film : "INTRO");
			exit(1);
		}
		printf("frame 0: film %s\n", media_film_name());
		return;
	} else if (!strcmp(mode, "boot")) {
		media_startup_begin(false);
		return;
	} else if (!strcmp(mode, "music")) {
		srand(1234567890);
		media_music_play(track);
		printf("t=0.0 Media_AudioPlay(%d): CD track %d\n", track, media_music_track());
		return;
	}
	printf("%s\n", failures ? "FAILED" : "all ok");
	exit(failures ? 1 : 0);
}

void game_frame(void) {
	static int last_track = -1;
	if (!strcmp(mode, "play")) {
		static char last[32];
		if (!media_film_frame()) {
			printf("film ended at frame %d (t=%.2f)\n", frame_no, platform_now());
			game_cleanup();
			exit(0);
		}
		if (strcmp(last, media_film_name())) {
			if (last[0]) printf("frame %d (t=%.2f): film %s\n", frame_no, platform_now(), media_film_name());
			snprintf(last, sizeof(last), "%s", media_film_name());
		}
		media_film_stats_t st;
		if (media_film_stats(&st) && frame_no % 250 == 0) {
			printf("frame %d t=%.2f: %ld pictures, sound %ld decoded, %ld dropped, %d queued\n", frame_no,
				platform_now(), st.video_frames, st.audio_frames, st.audio_dropped, mixer_stream_queued());
		}
	} else if (!strcmp(mode, "boot")) {
		if (!media_startup_frame()) {
			printf("start-up done at frame %d (t=%.2f)\n", frame_no, platform_now());
			platform_present(); // the logo frame
			game_cleanup();
			exit(0);
		}
	} else if (!strcmp(mode, "music")) {
		media_music_poll();
		int t = media_music_track();
		if (t != last_track) {
			printf("t=%.1f CD track %d\n", platform_now(), t);
			last_track = t;
		}
	}
	frame_no++;
	platform_present();
	input_frame_end();
}

void game_cleanup(void) {
	media_film_close();
	media_music_stop();
	render_cleanup();
}
