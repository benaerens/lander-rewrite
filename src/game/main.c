// The program: Game_Main 0x416020 / Game_Startup 0x4101d0 / Game_ModeLoop 0x410440 (campaign mode 5)
// as a per-frame state machine: start-up films and pictures, the front end (src/fe), the mission
// loading screen, the mission (game.c), the level-end films, back to the front end.
// args: [<disc image> | --image <disc image>] [--data <dir>] [--mission WORLD/N] [--skip-intro] [--cheats N] [--language 0|1|2]
//       [--debug-result N] [--test-frames N] [--test-shot file.png] [--demo N]
//       [--aspect auto|4:3|16:9|W:H] [--render-height N]
// --aspect / --render-height: the missions' frame (see screen_apply; LANDER.CFG "Aspect", "Render Height").
// --demo N: plays attract demo N (0 Mercury 1, 1 Venus 1, 2 Earth 2, 3 Venus 2, 4 Europa 2) first.
// --cheats: the registry's cheat bits (gSoftFruit: 0x20 +$24,995,000, 0x200 + E held at New Game =
// every mail); --debug-result: an aborted mission counts as result N (to test de-brief / save).
// --mission plays one mission directly and then opens the front end's title screen.
// LANDER_CHECK_STRINGS=1: compares the text arrays with lander.EXE and the missions' DLLs and exits
// (gametext_check; LANDER_STRINGS_DUMP=file also writes the text out).
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "gametext.h"
#include "font.h"
#include "media.h"
#include "../fe/fe.h"
#include "../platform.h"
#include "../fs.h"
#include "../png.h"
#include "../discimage.h"
#include "../model.h"
#include "input_map.h"

enum { ST_STARTUP, ST_FRONTEND, ST_LOADING, ST_MISSION, ST_FILMS, ST_QUIT };

static int state = ST_STARTUP;
static bool loading_shown;
static const mission_def_t *mission_def;
static bool direct_mission;
static int test_frames = -1, frame_count;
static const char *test_shot;

static char data_problem[700];   // why a folder with game data was not used

static bool has_data(const char *dir) {
	fs_set_root(dir);
	if (!fs_exists("MISSIONS")) return false;
	char missing[128];
	if (gametext_missing(missing, sizeof(missing))) {   // the game's text is read from its programs
		snprintf(data_problem, sizeof(data_problem), "The game data in %s is not complete: %s is missing or damaged.",
			dir, missing);
		return false;
	}
	return true;
}

static bool disc_progress(const char *what, uint64_t done, uint64_t total, void *user) {
	(void)user;
	platform_show_status(what, total ? (float)((double)done / (double)total) : 0);
	return !platform_should_quit();   // the window was closed: stop copying
}

// Written into the data folder when a copy finished (holds the image's path): a copy that was stopped
// half way is not used.
#define INSTALLED_MARK "INSTALLED.TXT"

static bool installed_from(const char *data_dir, char *image, size_t size) {
	char path[700];
	snprintf(path, sizeof(path), "%s/%s", data_dir, INSTALLED_MARK);
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	size_t n = fread(image, 1, size - 1, f);
	image[n] = 0;
	fclose(f);
	return has_data(data_dir);
}

// The player's own disc image -> <save folder>/data (the game ships without any game data)
static bool install_from_image(const char *image, const char *data_dir, char *err, size_t err_size) {
	char mark[700], from[1024];
	if (installed_from(data_dir, from, sizeof(from)) && !strcmp(from, image)) {
		return true;   // already copied from this image (it was given again)
	}
	snprintf(mark, sizeof(mark), "%s/%s", data_dir, INSTALLED_MARK);
	remove(mark);
	fs_make_dir(platform_save_dir());   // data_dir is in it
	platform_show_status("Reading your Lander disc image...", 0);
	if (!disc_extract(image, data_dir, disc_progress, NULL, err, err_size)) {
		if (platform_should_quit()) exit(0);
		return false;
	}
	if (!has_data(data_dir)) {
		snprintf(err, err_size, "The disc image was copied, but the game files are not complete.");
		return false;
	}
	FILE *f = fopen(mark, "wb");
	if (f) {
		fputs(image, f);
		fclose(f);
	}
	return true;
}

// ---------------------------------------------------------------- the disc folder
// The download holds a folder next to the program for the player's disc image: an image there (or the
// CD's files) is found at start-up, also in a folder or two below it (an unpacked download keeps its own
// folders).
#define DISC_FOLDER "Put your Lander disc image here"

typedef struct {
	char image[1024];     // the best disc image found
	int rank;
	char archive[256];    // a .zip / .rar / .7z that still has to be unpacked
	char cd[1024];        // a folder holding the CD's files (it has MISSIONS)
} disc_find_t;

typedef struct {
	disc_find_t *find;
	const char *dir;
	int depth;
} disc_scan_t;

static bool ends_with(const char *s, const char *end) {   // case-insensitive
	size_t n = strlen(s), m = strlen(end);
	if (n < m) return false;
	for (size_t i = 0; i < m; i++) {
		if (tolower((unsigned char)s[n - m + i]) != tolower((unsigned char)end[i])) return false;
	}
	return true;
}

static int image_rank(const char *name) { // the images that hold the music first; -1 = no image
	static const char *const ext[] = {".cue", ".mds", ".iso", ".img", ".bin", ".mdf"};
	for (int i = 0; i < (int)len(ext); i++) {
		if (ends_with(name, ext[i])) return i;
	}
	return -1;
}

static void disc_scan(disc_find_t *find, const char *dir, int depth);

static void disc_scan_entry(const char *name, bool is_dir, void *user) {
	disc_scan_t *s = user;
	disc_find_t *f = s->find;
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", s->dir, name);
	if (is_dir) {
		if (ends_with(name, "MISSIONS") && strlen(name) == 8) {
			if (!f->cd[0]) snprintf(f->cd, sizeof(f->cd), "%s", s->dir);
		} else if (s->depth < 2) {
			disc_scan(f, path, s->depth + 1);
		}
		return;
	}
	int rank = image_rank(name);
	if (rank >= 0 && (!f->image[0] || rank < f->rank)) {
		snprintf(f->image, sizeof(f->image), "%s", path);
		f->rank = rank;
	} else if (rank < 0 && !f->archive[0] && (ends_with(name, ".zip") || ends_with(name, ".rar") ||
		ends_with(name, ".7z"))) {
		snprintf(f->archive, sizeof(f->archive), "%s", name);
	}
}

static void disc_scan(disc_find_t *find, const char *dir, int depth) {
	disc_scan_t s = {find, dir, depth};
	fs_list(dir, disc_scan_entry, &s);
}

static void find_data(const char *arg, const char *image) {
	if (arg && has_data(arg)) {
		return;
	}
	char installed[600], from[1024];
	snprintf(installed, sizeof(installed), "%s/data", platform_save_dir());
	if (!image && installed_from(installed, from, sizeof(from))) {
		return;
	}
	// the disc folder next to the program: the CD's files are used where they are, an image is copied
	char disc_folder[600], err[700] = "";
	snprintf(disc_folder, sizeof(disc_folder), "%s/%s", platform_program_dir(), DISC_FOLDER);
	disc_find_t found = {0};
	if (!image) {
		disc_scan(&found, disc_folder, 0);
		if (found.cd[0] && has_data(found.cd)) {
			return;
		}
		if (found.image[0]) {
			image = found.image;
		} else if (found.archive[0]) {
			snprintf(err, sizeof(err), "Please unpack %s first: the game needs the disc image inside it.",
				found.archive);
		}
	}
	// a folder with the CD's files next to the program or the working folder (for development)
	const char *rel[] = {"data", "cd", "../cd", "../../cd", "../data", "../../data"};
	char path[600];
	for (int base = 0; !image && base < 2; base++) {
		for (size_t i = 0; i < len(rel); i++) {
			snprintf(path, sizeof(path), "%s/%s", base ? platform_program_dir() : ".", rel[i]);
			if (has_data(path)) {
				return;
			}
		}
	}
	// no game data yet: the player's disc image (from the disc folder, given on the command line or
	// dropped on the program, or asked for)
	char chosen[1024];
	if (image && install_from_image(image, installed, err, sizeof(err))) {
		return;
	}
	if (!err[0] && data_problem[0]) snprintf(err, sizeof(err), "%s", data_problem);
	fs_make_dir(disc_folder);   // so the player sees where an image can go
	for (;;) {
		if (!platform_ask_disc_image(chosen, sizeof(chosen), disc_folder, err[0] ? err : NULL)) {
			fprintf(stderr, "Lander needs the game files of your own Lander CD: start it with your disc image "
				"(.iso, .cue/.bin or .mds/.mdf) or --data <folder with the CD's files>.%s%s\n", err[0] ? "\n" : "", err);
			exit(1);
		}
		if (!disc_is_image(chosen)) {   // a folder or a drive: the CD's files (used where they are) or an image
			data_problem[0] = 0;
			disc_find_t in = {0};
			if (has_data(chosen)) {
				return;
			}
			disc_scan(&in, chosen, 0);
			if (in.cd[0] && has_data(in.cd)) {
				return;
			}
			if (!in.image[0]) {
				snprintf(err, sizeof(err), "%s", data_problem[0] ? data_problem : in.archive[0] ?
					"Please unpack the archive first: the game needs the disc image inside it." :
					"No Lander disc image or Lander CD files were found there.");
				continue;
			}
			snprintf(chosen, sizeof(chosen), "%s", in.image);
		}
		if (install_from_image(chosen, installed, err, sizeof(err))) {
			return;
		}
	}
}

// ---------------------------------------------------------------- the frame (widescreen)
// The front end, the films, the slides and the loading screen are the original's 640x480: the platform
// shows them with black bars on a wider display. A mission is drawn in the display's aspect (or the one
// asked for), Hor+: the original's vertical view, more at the sides (level.c), the HUD spread to the edges
// (hud.c), the windows in a 640x480 area in the middle (ui.c). Its height is the render height: 480 is
// the original's look, more draws the 3D sharper with the 2D layers scaled by height / 480.
// Settings: LANDER.CFG "Aspect" (auto | W:H) and "Render Height", --aspect / --render-height for one run.
#define CFG_ASPECT "Aspect"
#define CFG_HEIGHT "Render Height"

static struct {
	char aspect[16];        // "auto" or "W:H" (as in LANDER.CFG)
	int height;
	char cfg_aspect[16];    // the LANDER.CFG values (written back at the end: Config_Save rewrites the file)
	int cfg_height;
} scr = {"auto", 480, "auto", 480};

static double parse_aspect(const char *s) { // 0 = the display's
	double a, b;
	if (sscanf(s, "%lf:%lf", &a, &b) == 2 && a > 0 && b > 0) return a / b;
	if (sscanf(s, "%lf", &a) == 1 && a > 0) return a;
	return 0;
}

static void cfg_path(char *out, size_t n) { snprintf(out, n, "%s/LANDER.CFG", platform_save_dir()); }

static void screen_config_load(void) { // the two lines among Config_Load's (it skips names it does not know)
	char path[600], line[128];
	cfg_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		char *v = eq + 1;
		v[strcspn(v, "\r\n")] = 0;
		if (!strcmp(line, CFG_ASPECT)) snprintf(scr.cfg_aspect, sizeof(scr.cfg_aspect), "%s", parse_aspect(v) > 0 ? v : "auto");
		else if (!strcmp(line, CFG_HEIGHT)) scr.cfg_height = atoi(v);
	}
	fclose(f);
	if (scr.cfg_height < 480 || scr.cfg_height > 2160) scr.cfg_height = 480;
	snprintf(scr.aspect, sizeof(scr.aspect), "%s", scr.cfg_aspect);
	scr.height = scr.cfg_height;
}

static void screen_config_save(void) { // after Config_Save, which writes only the front end's values
	char path[600];
	cfg_path(path, sizeof(path));
	FILE *f = fopen(path, "a");
	if (!f) return;
	fprintf(f, "%s=%s\n%s=%d\n", CFG_ASPECT, scr.cfg_aspect, CFG_HEIGHT, scr.cfg_height);
	fclose(f);
}

// every frame: the frame for this state (the display can change: Alt+Enter, a window resize)
static void screen_apply(bool mission) {
	int w = 640, h = 480;
	if (mission) {
		double a = parse_aspect(scr.aspect);
		if (a <= 0) {
			int dw, dh;
			platform_display_size(&dw, &dh);
			a = dh > 0 ? (double)dw / dh : 4.0 / 3.0;
		}
		// never narrower than the original (a 5:4 display shows 4:3 with bars), at most 32:9 and a bit
		if (a < 4.0 / 3.0) a = 4.0 / 3.0;
		if (a > 4.0) a = 4.0;
		h = scr.height;
		w = (int)(h * a / 2 + 0.5) * 2;
	}
	render_resize(w, h);
	render_set_2d_scale((float)h / 480.0f);
}

static void mission_name(const mission_def_t *m, char *out, size_t size) {
	snprintf(out, size, "%c%s %s", m->world[0], m->world + 1, m->mission);
	for (char *p = out + 1; *p && *p != ' '; p++) {
		if (*p >= 'A' && *p <= 'Z') *p += 32;
	}
}

static void start_frontend(void) { // Campaign_FrontEnd 0x410370
	platform_set_title("Lander");
	fe_begin(gFrontEndEntry, gLastMissionResult);
	state = ST_FRONTEND;
}

static void cheats_apply(void) { // Cheats_Apply 0x410410
	if (gSoftFruit & 1) gCheatInvulnerable = 1;
	if (gSoftFruit & 2) gCheatInfiniteFuel = 1;
	if (gSoftFruit & 0x80) gStableCutOffTime = 0x400;
}

static void start_loading(const mission_def_t *m) { // Game_ModeLoop: the loading picture, then Mission_Load
	mission_def = m;
	state = ST_LOADING;
}

// ---------------------------------------------------------------- the attract demo (FE result 6)
// Game_ModeLoop: the next mission of gDemoSequence, Campaign_NewGameForDemo, gDemoPlayback; the mission
// loads its DEMO.DAT (Demo_Init / Demo_Poll: the recording's player state, stable cut-off, view distance,
// cheats replace the current ones until Demo_End) and plays its control words; then the title again.
static const uint8_t demo_sequence[][2] = {{0, 0}, {1, 0}, {2, 1}, {1, 1}, {6, 1}};   // gDemoSequence 0x490ba0
static int demo_next;                                // DAT_0049bf68
static bool demo_running;                            // gDemoPlayback (main.c's side)
static struct {                                      // Demo_Poll's backup (DAT_0049be60..)
	LanderPlayerState player;
	int32_t world, mission, stable, fog, cheat_fuel, cheat_inv;
} demo_saved;

static bool demo_begin(void) {
	if (demo_next >= (int)len(demo_sequence)) demo_next = 0;
	gCurWorld = demo_sequence[demo_next][0];
	gCurMission = demo_sequence[demo_next][1];
	demo_next++;
	const mission_def_t *m = fe_mission_def(gCurWorld, gCurMission);
	if (!m) return false;
	Player_NewGame();                                // Campaign_NewGameForDemo 0x4103f0
	gPlayerState.secondary = 0x16;
	gPlayerState.missiles = 0x1e;
	char path[160];
	snprintf(path, sizeof(path), "MISSIONS\\%s\\%s\\DEMO.DAT", m->world, m->mission);
	uint32_t size;
	uint8_t *d = fs_load(path, &size);
	if (!d || !im_demo_start(d, size)) {
		free(d);
		return false;
	}
	demo_saved.player = gPlayerState;                // Demo_Poll 0x40ebb0
	demo_saved.world = gCurWorld;
	demo_saved.mission = gCurMission;
	demo_saved.stable = gStableCutOffTime;
	demo_saved.fog = gFogDistance;
	demo_saved.cheat_fuel = gCheatInfiniteFuel;
	demo_saved.cheat_inv = gCheatInvulnerable;
	int32_t *ps = (int32_t *)&gPlayerState;
	for (int i = 0; i < 11; i++) ps[i] = (int32_t)get_u32(d + 4 + 4 * i);
	gStableCutOffTime = (int32_t)get_u32(d + 0x38);
	// (gravity 0x3c, air resistance 0x40, throttle power 0x44 are set again by every mission's start)
	gFogDistance = (int32_t)get_u32(d + 0x48);       // DAT_00490cac: Env_SetViewDistance in Mission_Run
	gCheatInfiniteFuel = (int32_t)get_u32(d + 0x4c); // DAT_004a6dec
	gCheatInvulnerable = (int32_t)get_u32(d + 0x50); // DAT_004a6df0
	free(d);
	demo_running = true;
	gUiShowPointer = 0;                              // DAT_0049a2a8 = 0
	start_loading(m);
	return true;
}

static void demo_end(void) { // Demo_End 0x40ed80
	if (!demo_running) return;
	im_demo_stop();
	gPlayerState = demo_saved.player;
	gCurWorld = demo_saved.world;
	gCurMission = demo_saved.mission;
	gStableCutOffTime = demo_saved.stable;
	gFogDistance = demo_saved.fog;
	gCheatInfiniteFuel = demo_saved.cheat_fuel;
	gCheatInvulnerable = demo_saved.cheat_inv;
	demo_running = false;
}

static bool load_mission(void) {
	const mission_def_t *m = mission_def;
	char name[64], title[96];
	mission_name(m, name, sizeof(name));
	gPlayerCashAtLaunch = gPlayerState.cash;
	gPlayerStateBackup = gPlayerState;                 // restored when the mission fails
	if (!game_load_mission(m)) {
		fprintf(stderr, "Could not load %s\n", name);
		return false;
	}
	snprintf(title, sizeof(title), "Lander - %s", name);
	platform_set_title(title);
	if (getenv("LANDER_DEBUG")) {
		const LanderPlayerState *p = &gPlayerState;
		printf("player state: lander %d primary %d secondary %d engine %d armour %d missiles %d fuel %d/%d shield %d cash %d\n",
			p->landerType, p->primary, p->secondary, p->engine, p->armour, p->missiles, p->fuel, p->maxFuel, p->shield, p->cash);
	}
	return true;
}

static int32_t debug_result;   // --debug-result N: a mission ended without success counts as result N (tests)
static int demo_arg = -1;      // --demo N

static void mission_ended(void) { // the rest of the Game_ModeLoop campaign iteration
	if (demo_running) {            // after the attract demo: Mission_Unload (Demo_End), the title again
		game_unload_mission();
		demo_end();
		gLastMissionResult = 0;
		if (gExitLanderRequested || platform_should_quit()) {
			state = ST_QUIT;
			return;
		}
		gFrontEndEntry = FE_ENTRY_FRESH;
		start_frontend();
		return;
	}
	if (debug_result && G.mission_result < 1 && !gExitLanderRequested) G.mission_result = gMissionResult = debug_result;
	int32_t r = G.mission_result;
	gLastMissionResult = r;
	if (mission_def->world_index >= 0 && mission_def->world_index < 23 && mission_def->mission_index < 6) {
		gMissionResults[mission_def->world_index * 6 + mission_def->mission_index] = (int8_t)r;
	}
	const char *movie = game_level_end_movie();
	char film[64] = "";
	if (movie) snprintf(film, sizeof(film), "%s", movie);
	game_unload_mission();
	if (r < 1) gPlayerState = gPlayerStateBackup;     // a failed mission undoes the purchases
	if (gExitLanderRequested || platform_should_quit()) {
		state = ST_QUIT;
		return;
	}
	gFrontEndEntry = FE_ENTRY_AFTER_MISSION;
	if (direct_mission) {                              // --mission: then the title screen
		direct_mission = false;
		gFrontEndEntry = FE_ENTRY_FRESH;
		gLastMissionResult = -1;
	}
	// the level-end film (SetLevelEndMovie), and the credits after Pluto
	if (media_level_end_films(film, mission_def->world_index)) {
		state = ST_FILMS;
		return;
	}
	start_frontend();
}

void game_init(int argc, char **argv) { // Game_Main 0x416020
	const char *data = NULL, *mission = NULL, *aspect = NULL, *image = NULL;
	int render_height = 0;
	bool skip_intro = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
		else if (!strcmp(argv[i], "--mission") && i + 1 < argc) mission = argv[++i];
		else if (!strcmp(argv[i], "--skip-intro")) skip_intro = true;
		else if (!strcmp(argv[i], "--cheats") && i + 1 < argc) gSoftFruit = (uint32_t)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--language") && i + 1 < argc) gLanguage = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--debug-result") && i + 1 < argc) debug_result = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--test-frames") && i + 1 < argc) test_frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--test-shot") && i + 1 < argc) test_shot = argv[++i];
		else if (!strcmp(argv[i], "--demo") && i + 1 < argc) demo_arg = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--aspect") && i + 1 < argc) aspect = argv[++i];
		else if (!strcmp(argv[i], "--render-height") && i + 1 < argc) render_height = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--image") && i + 1 < argc) image = argv[++i];
		else if (argv[i][0] != '-' && disc_is_image(argv[i])) image = argv[i];   // dropped on the program
	}
	if (gLanguage < 0 || gLanguage > 2) gLanguage = 0;
	Config_Load();                // Config_LoadRegistry: the options' values of the last run
	screen_config_load();
	if (aspect) snprintf(scr.aspect, sizeof(scr.aspect), "%s", parse_aspect(aspect) > 0 ? aspect : "auto");
	if (render_height) scr.height = render_height < 480 ? 480 : render_height > 2160 ? 2160 : render_height;
	find_data(data, image);
	if (getenv("LANDER_CHECK_STRINGS")) {   // test: the text tables against the player's files
		exit(gametext_check() ? 1 : 0);
	}
	char icon_from[1024];   // the window shows the original's icon (from the player's program)
	if (fs_resolve("LNDRD3DR\\lander.EXE", icon_from, sizeof(icon_from)) ||
		fs_resolve("LNDRGLDR\\lander.EXE", icon_from, sizeof(icon_from))) {
		platform_set_icon(icon_from);
	}
	gametext_init();              // lander.EXE's text (the missions' is read when they load)
	render_init(640, 480); // the front end's frame; a mission's is set by screen_apply
	font_init();
	model_set_brightness(200);    // Game_Main sets the brightness table to 200
	platform_set_title("Lander");
	fe_init_once();
	if (mission) {
		char world[64], num[16];
		const mission_def_t *m = NULL;
		if (sscanf(mission, "%63[^/\\]%*[/\\]%15s", world, num) == 2) {
			m = mission_find(world, num);
		}
		if (!m) {
			fprintf(stderr, "unknown mission %s\n", mission);
			exit(1);
		}
		gCurWorld = gFESelWorld = m->world_index;
		gCurMission = gFESelMission = m->mission_index;
		direct_mission = true;
		mission_def = m;
		if (!load_mission()) exit(1);
		state = ST_MISSION;
		return;
	}
	if (demo_arg >= 0) {   // --demo N: the attract demo N of the sequence, then the title
		demo_next = demo_arg;
		if (demo_begin()) return;
	}
	if (skip_intro) {
		start_frontend();
		return;
	}
	state = ST_STARTUP;
	media_startup_begin(false);
}

static void startup_frame(void) { // Game_Startup 0x4101d0: intro film, soundtrack picture, logo (media.c)
	if (!media_startup_frame()) start_frontend();   // the logo stays in the render buffer meanwhile
}

void game_frame(void) {
	screen_apply(state == ST_MISSION);
	switch (state) {
		case ST_STARTUP:
			startup_frame();
			break;
		case ST_FRONTEND: {
			int r = fe_frame();
			if (r == FE_RESULT_NONE) break;
			fe_end();
			if (r == FE_RESULT_PLAY) {
				cheats_apply();
				const mission_def_t *m = fe_mission_def(gCurWorld, gCurMission);
				if (m) {
					start_loading(m);
					break;
				}
				gFrontEndEntry = FE_ENTRY_FRESH;
				start_frontend();
				break;
			}
			if (r == FE_RESULT_DEMO) {   // the attract demo; without a DEMO.DAT back to the title
				if (demo_begin()) break;
				gFrontEndEntry = FE_ENTRY_FRESH;
				start_frontend();
				break;
			}
			state = ST_QUIT;
			break;
		}
		case ST_LOADING: // the loading picture is shown first (one frame), then the mission loads
			if (loading_shown) {
				bool ok = load_mission();
				media_loading_done();
				loading_shown = false;
				if (ok) {
					state = ST_MISSION;
				} else {
					gFrontEndEntry = FE_ENTRY_FRESH;
					start_frontend();
				}
				break;
			}
			media_loading_draw(0);
			loading_shown = true;
			break;
		case ST_MISSION:
			if (!game_mission_frame()) mission_ended();
			break;
		case ST_FILMS:
			if (!media_film_frame()) {
				media_film_close();
				start_frontend();
			}
			break;
		default:
			break;
	}
	platform_present();
	input_frame_end();

	if (state == ST_QUIT) {
		game_cleanup();
		exit(0);
	}
	if (test_frames >= 0 && ++frame_count >= test_frames) {
		if (test_shot) {
			png_write(test_shot, render_pixels(), render_width(), render_height());
		}
		printf("self test: %d frames, data %s\n", frame_count, fs_root());
		game_cleanup();
		exit(0);
	}
}

void game_cleanup(void) { // the end of Game_Main (also when the window is closed)
	Config_Save();                // Config_SaveRegistry
	screen_config_save();
	game_unload_mission();
	render_cleanup();
}
