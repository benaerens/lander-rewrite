// Headless platform for tests: runs the game for a number of frames on a
// simulated clock, replays scripted input and saves frames as PNG files.
//
// usage: lander_headless --data <dir> [--frames N] [--fps F] [--shot-every K]
//                        [--shot-at F1,F2,...] [--out <dir>] [--script <file>] [--pad] [--size WxH] [game args...]
// --pad: a joystick / game pad is connected from the start
// --size: the display size (default 640x480): its aspect is the one the game makes its missions in; a
//         frame of another aspect is saved with black bars to the display's aspect (as a platform shows it)
// script lines:  <frame> <button> down|up      e.g.  "120 KEY_SPACE down", "130 UP up", "90 a down"
//                <frame> MOUSE <x> <y>          pointer position (render pixels), e.g. "40 MOUSE 284 46"
//                                               (the move is also the frame's relative mouse motion)
//                <frame> TEXT <word>            typed characters ('_' = space), e.g. "50 TEXT Mars_save"
//                <frame> PAD connect|disconnect a joystick / game pad comes or goes
//                <frame> AXIS <axis> <value>    pad axis LX LY Z RX RY RZ in -1..1, e.g. "60 AXIS LY -1"
//                pad buttons: PAD_A PAD_B PAD_X PAD_Y PAD_L1 PAD_R1 PAD_L2 PAD_R2 PAD_SELECT PAD_START
//                (= joystick buttons 0..9), PAD_UP PAD_DOWN PAD_LEFT PAD_RIGHT (the hat)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(__linux__)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "platform.h"
#include "render.h"
#include "mixer.h"
#include "png.h"

static struct {
	double now;
	bool quit;
	int frame;
	char out[512];
	char program_dir[512];
	int display_w, display_h;
} P = {.out = ".", .program_dir = ".", .display_w = 640, .display_h = 480};

double platform_now(void) { return P.now; }
void platform_pump_events(void) {}
bool platform_should_quit(void) { return P.quit; }
void platform_set_fullscreen(bool on) {}
void platform_display_size(int *w, int *h) {
	*w = P.display_w;
	*h = P.display_h;
}
const char *platform_save_dir(void) { return P.out; }
const char *platform_program_dir(void) { return P.program_dir; }
void platform_set_title(const char *title) { printf("[title] %s\n", title); }
void platform_set_icon(const char *program_file) { printf("[icon] %s\n", program_file); }
bool platform_ask_disc_image(char *path, size_t size, const char *disc_folder, const char *message) {
	(void)path; (void)size;
	fprintf(stderr, "[setup] no game data; disc folder: %s\n", disc_folder);
	if (message) fprintf(stderr, "%s\n", message);
	return false;   // tests pass --image or --data, or put an image into the disc folder
}
void platform_show_status(const char *text, float progress) {
	static int last = -1;
	int pct = (int)(progress * 100);
	if (pct / 10 != last) {
		last = pct / 10;
		printf("[setup] %s %d%%\n", text, pct);
	}
}
void platform_audio_lock(void) {}
void platform_audio_unlock(void) {}

static int shot_every = 0;
static int shots_at[64], num_shots_at;

void platform_present(void) {
	bool shoot = shot_every > 0 && P.frame % shot_every == 0;
	for (int i = 0; i < num_shots_at; i++) {
		shoot |= shots_at[i] == P.frame;
	}
	if (shoot) {
		char path[600];
		snprintf(path, sizeof(path), "%s/frame%05d.png", P.out, P.frame);
		int w = render_width(), h = render_height();
		// the frame in the display's aspect at its own scale: black bars at the sides or above and below
		int ow = (int)((double)h * P.display_w / P.display_h + 0.5), oh = (int)((double)w * P.display_h / P.display_w + 0.5);
		if (ow > w) oh = h;
		else if (oh > h) ow = w;
		else ow = w, oh = h;
		if (ow == w && oh == h) {
			png_write(path, render_pixels(), w, h);
		} else {
			uint32_t *img = malloc(sizeof(uint32_t) * ow * oh);
			const uint32_t *px = render_pixels();
			int dx = (ow - w) / 2, dy = (oh - h) / 2;
			for (int i = 0; i < ow * oh; i++) img[i] = 0xff000000u;
			for (int y = 0; y < h; y++) memcpy(img + (y + dy) * ow + dx, px + y * w, sizeof(uint32_t) * w);
			png_write(path, img, ow, oh);
			free(img);
		}
	}
}

// a script event: a button, the pointer (MOUSE x y), typed text (TEXT word), PAD, AXIS
typedef struct { int frame; button_t button; bool down; int kind, x, y; char text[32]; float value; } event_t;
enum { EV_BUTTON, EV_MOUSE, EV_TEXT, EV_PAD, EV_AXIS };

static int parse_axis(const char *s) {
	static const struct { const char *name; axis_t a; } names[] = {
		{"LX", AXIS_PAD_LX}, {"LY", AXIS_PAD_LY}, {"Z", AXIS_PAD_Z}, {"RX", AXIS_PAD_RX}, {"RY", AXIS_PAD_RY},
		{"RZ", AXIS_PAD_RZ},
	};
	for (size_t i = 0; i < len(names); i++) {
		if (!strcmp(s, names[i].name)) return (int)names[i].a;
	}
	fprintf(stderr, "unknown axis '%s'\n", s);
	return -1;
}

static button_t parse_button(const char *s) {
	static const struct { const char *name; button_t b; } names[] = {
		{"UP", BTN_UP}, {"DOWN", BTN_DOWN}, {"LEFT", BTN_LEFT}, {"RIGHT", BTN_RIGHT},
		{"ACCEPT", BTN_ACCEPT}, {"BACK", BTN_BACK}, {"PAUSE", BTN_PAUSE},
		{"KEY_ESCAPE", BTN_KEY(KEY_ESCAPE)}, {"KEY_RETURN", BTN_KEY(KEY_RETURN)},
		{"KEY_SPACE", BTN_KEY(KEY_SPACE)}, {"KEY_TAB", BTN_KEY(KEY_TAB)},
		{"KEY_UP", BTN_KEY(KEY_UP)}, {"KEY_DOWN", BTN_KEY(KEY_DOWN)},
		{"KEY_LEFT", BTN_KEY(KEY_LEFT)}, {"KEY_RIGHT", BTN_KEY(KEY_RIGHT)},
		{"KEY_LSHIFT", BTN_KEY(KEY_LSHIFT)}, {"KEY_LCTRL", BTN_KEY(KEY_LCTRL)},
		{"KEY_LALT", BTN_KEY(KEY_LALT)}, {"MOUSE_LEFT", BTN_MOUSE_LEFT}, {"MOUSE_RIGHT", BTN_MOUSE_RIGHT},
		{"KEY_BACKSPACE", BTN_KEY(KEY_BACKSPACE)}, {"KEY_RSHIFT", BTN_KEY(KEY_RSHIFT)}, {"KEY_RCTRL", BTN_KEY(KEY_RCTRL)},
		{"KEY_PAGEUP", BTN_KEY(KEY_PAGEUP)}, {"KEY_PAGEDOWN", BTN_KEY(KEY_PAGEDOWN)}, {"KEY_F1", BTN_KEY(KEY_F1)},
		{"KEY_F2", BTN_KEY(KEY_F2)}, {"KEY_DELETE", BTN_KEY(KEY_DELETE)}, {"KEY_HOME", BTN_KEY(KEY_HOME)}, {"KEY_END", BTN_KEY(KEY_END)},
		{"PAD_A", BTN_PAD_A}, {"PAD_B", BTN_PAD_B}, {"PAD_X", BTN_PAD_X}, {"PAD_Y", BTN_PAD_Y}, {"PAD_L1", BTN_PAD_L1},
		{"PAD_R1", BTN_PAD_R1}, {"PAD_L2", BTN_PAD_L2}, {"PAD_R2", BTN_PAD_R2}, {"PAD_SELECT", BTN_PAD_SELECT},
		{"PAD_START", BTN_PAD_START}, {"PAD_UP", BTN_PAD_UP}, {"PAD_DOWN", BTN_PAD_DOWN}, {"PAD_LEFT", BTN_PAD_LEFT},
		{"PAD_RIGHT", BTN_PAD_RIGHT}, {"KEY_KP_ENTER", BTN_KEY(KEY_KP_ENTER)}, {"KEY_RALT", BTN_KEY(KEY_RALT)},
	};
	for (size_t i = 0; i < len(names); i++) {
		if (!strcmp(s, names[i].name)) {
			return names[i].b;
		}
	}
	if (strlen(s) == 1) {
		return BTN_KEY(tolower((unsigned char)s[0]));
	}
	fprintf(stderr, "unknown button '%s'\n", s);
	return BTN_NONE;
}

#if defined(__linux__)
static void on_crash(int sig) { // backtrace (link with -rdynamic for names; addr2line for lines)
	void *frames[64];
	int n = backtrace(frames, 64);
	fprintf(stderr, "crash: signal %d at frame %d\n", sig, P.frame);
	backtrace_symbols_fd(frames, n, 2);
	_exit(128 + sig);
}
#endif

int main(int argc, char **argv) {
#if defined(__linux__)
	signal(SIGSEGV, on_crash);
	signal(SIGFPE, on_crash);
	signal(SIGABRT, on_crash);
#endif
	int frames = 300;
	double fps = 60;
	const char *script = NULL;
	char *game_argv[64];
	int game_argc = 0;
	game_argv[game_argc++] = argv[0];
	snprintf(P.program_dir, sizeof(P.program_dir), "%s", argv[0]);   // the program's folder
	char *slash = strrchr(P.program_dir, '/');
	if (slash) *slash = 0; else strcpy(P.program_dir, ".");
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = atof(argv[++i]);
		else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--shot-at") && i + 1 < argc) {
			for (char *t = strtok(argv[++i], ","); t && num_shots_at < 64; t = strtok(NULL, ",")) {
				shots_at[num_shots_at++] = atoi(t);
			}
		}
		else if (!strcmp(argv[i], "--out") && i + 1 < argc) snprintf(P.out, sizeof(P.out), "%s", argv[++i]);
		else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
		else if (!strcmp(argv[i], "--pad")) input_set_pad_connected(true);   // a game pad from the start
		else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
			int w, h;
			if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
				P.display_w = w;
				P.display_h = h;
			}
		}
		else if (game_argc < 63) game_argv[game_argc++] = argv[i];
	}
	game_argv[game_argc] = NULL;

	event_t *events = NULL;
	int num_events = 0;
	if (script) {
		FILE *f = fopen(script, "r");
		if (!f) {
			fprintf(stderr, "cannot open script %s\n", script);
			return 1;
		}
		char line[256];
		events = calloc(4096, sizeof(event_t));
		while (fgets(line, sizeof(line), f) && num_events < 4096) {
			int frame, x, y;
			char btn[64], state[32];
			if (line[0] == '#') {
				continue;
			}
			if (sscanf(line, "%d MOUSE %d %d", &frame, &x, &y) == 3) {
				events[num_events++] = (event_t){.frame = frame, .kind = EV_MOUSE, .x = x, .y = y};
				continue;
			}
			if (sscanf(line, "%d TEXT %31s", &frame, state) == 2) {
				event_t e = {.frame = frame, .kind = EV_TEXT};
				snprintf(e.text, sizeof(e.text), "%s", state);
				events[num_events++] = e;
				continue;
			}
			float value;
			if (sscanf(line, "%d %63s %15s", &frame, btn, state) == 3 && !strcmp(btn, "PAD")) {
				events[num_events++] = (event_t){.frame = frame, .kind = EV_PAD, .down = !strcmp(state, "connect")};
				continue;
			}
			if (sscanf(line, "%d %63s %15s %f", &frame, btn, state, &value) == 4 && !strcmp(btn, "AXIS")) {
				int a = parse_axis(state);
				if (a >= 0) events[num_events++] = (event_t){.frame = frame, .kind = EV_AXIS, .x = a, .value = value};
				continue;
			}
			if (sscanf(line, "%d %63s %15s", &frame, btn, state) != 3) {
				continue;
			}
			events[num_events++] = (event_t){frame, parse_button(btn), !strcmp(state, "down"), EV_BUTTON, 0, 0, "", 0};
		}
		fclose(f);
	}

	game_init(game_argc, game_argv);
	int16_t audio[2 * 2048];
	double audio_due = 0;
	for (P.frame = 0; P.frame < frames && !P.quit; P.frame++) {
		for (int i = 0; i < num_events; i++) {
			if (events[i].frame == P.frame) {
				if (events[i].kind == EV_MOUSE) {
					input_set_axis(AXIS_MOUSE_DX, (float)(events[i].x - input_mouse_x()));
					input_set_axis(AXIS_MOUSE_DY, (float)(events[i].y - input_mouse_y()));
					input_set_mouse_pos(events[i].x, events[i].y);
				} else if (events[i].kind == EV_PAD) {
					input_set_pad_connected(events[i].down);
				} else if (events[i].kind == EV_AXIS) {
					input_set_axis((axis_t)events[i].x, events[i].value);
				} else if (events[i].kind == EV_TEXT) {
					for (const char *c = events[i].text; *c; c++) input_text(*c == '_' ? ' ' : *c);
				} else {
					input_set_button(events[i].button, events[i].down);
				}
			}
		}
		game_frame();
		P.now += 1.0 / fps;
		// keep the mixer running in step with simulated time (output discarded)
		audio_due += mixer_rate() / fps;
		while (audio_due >= 1) {
			int n = (int)min(audio_due, 2048.0);
			mixer_render(audio, n);
			audio_due -= n;
		}
	}
	game_cleanup();
	free(events);
	return 0;
}
