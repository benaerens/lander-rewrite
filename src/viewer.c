// Level viewer: loads a mission and flies a free camera through it.
// args: [--data <dir>] [--mission WORLD/N] [--cam x,y,z,yaw,pitch] [--zone N]
//       [--view-distance d] [--size WxH]
// keys: arrows turn, W/S forward/back, A/D strafe, R/F up/down, shift = fast,
//       PageUp/PageDown = previous/next mission, +/- = view distance, Esc = quit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "render.h"
#include "fs.h"
#include "level.h"
#include "png.h"

// Missions on the disc in campaign order (program-map.md §6)
static const char *missions[][2] = {
	{"MERCURY", "1"}, {"VENUS", "1"}, {"VENUS", "2"}, {"EARTH", "1"}, {"EARTH", "2"}, {"EARTH", "3"},
	{"EARTH", "4"}, {"MARS", "1A"}, {"MARS", "1B"}, {"MARS", "2"}, {"MARS", "3"}, {"MARS", "4"},
	{"MARS", "5"}, {"EUROPA", "1"}, {"EUROPA", "2"}, {"CALLISTO", "1"}, {"CALLISTO", "2"},
	{"GANYMEDE", "1"}, {"IO", "1"}, {"IO", "2"}, {"TITAN", "1"}, {"TITAN", "2"}, {"TITAN", "3"},
	{"MIMAS", "1"}, {"RHEA", "1"}, {"RHEA", "2"}, {"NEPTUNE", "1"}, {"NEPTUNE", "2"}, {"PLUTO", "1"},
	{"TRAINING", "1"}, {"TRAINING", "2"}, {"DEATHMAT", "1"}, {"DEATHMAT", "3"},
};
#define NUM_MISSIONS ((int)len(missions))

static level_t *level;
static int mission_index = 7; // MARS 1A
static camera_t cam;
static double last_time, fps_time;
static int fps_frames;
static float view_distance_override;
static int test_frames = -1, frame_count; // --frames N --shot file: self test
static const char *test_shot;

static void default_camera(void) {
	const mission_t *m = level->mission;
	cam.pos = vec3(0, 2000, 0);
	cam.yaw = 0;
	cam.pitch = -0.2f;
	for (int i = 0; i < m->num_rec[REC_MOB]; i++) {
		const map_rec_t *r = &m->rec[REC_MOB][i];
		if (r->type == OBJ_LANDER) {
			int ax, ay, az;
			rot_unpack(r->rot, &ax, &ay, &az);
			cam.yaw = ay * (2 * PI / 4096);
			vec3_t fwd = vec3(sinf(cam.yaw), 0, cosf(cam.yaw));
			cam.pos = vec3_add(vec3(r->pos.x, r->pos.y + 700, r->pos.z), vec3_mulf(fwd, -2200));
			cam.pitch = -0.25f;
			break;
		}
	}
}

static bool load_mission(int index) {
	level_t *lv = level_load(missions[index][0], missions[index][1]);
	if (!lv) {
		return false;
	}
	level_free(level);
	level = lv;
	mission_index = index;
	if (view_distance_override > 0) {
		level->view_distance = view_distance_override;
	}
	default_camera();
	return true;
}

static bool same_name(const char *a, const char *b) {
	for (; *a && *b; a++, b++) {
		if ((*a | 32) != (*b | 32)) {
			return false;
		}
	}
	return *a == *b;
}

static bool has_data(const char *dir) {
	fs_set_root(dir);
	return fs_exists("MISSIONS");
}

static void find_data(const char *arg, const char *exe) {
	if (arg && has_data(arg)) {
		return;
	}
	char exe_dir[512];
	snprintf(exe_dir, sizeof(exe_dir), "%s", exe);
	char *slash = strrchr(exe_dir, '\\');
	char *slash2 = strrchr(exe_dir, '/');
	if (slash2 > slash) slash = slash2;
	if (slash) *slash = 0; else strcpy(exe_dir, ".");
	const char *rel[] = {"data", "cd", "../cd", "../../cd", "../data", "../../data"};
	char path[600];
	for (int base = 0; base < 2; base++) {
		for (size_t i = 0; i < len(rel); i++) {
			snprintf(path, sizeof(path), "%s/%s", base ? exe_dir : ".", rel[i]);
			if (has_data(path)) {
				return;
			}
		}
	}
	fprintf(stderr, "Lander data not found: put the CD's files in a folder called 'data' next to the program "
		"(tools/prepare_data.py makes it from your disc image), or pass --data <folder>\n");
	exit(1);
}

void game_init(int argc, char **argv) {
	const char *data = NULL, *mission = NULL, *cam_arg = NULL;
	int w = 640, h = 480, zone = -1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
		else if (!strcmp(argv[i], "--mission") && i + 1 < argc) mission = argv[++i];
		else if (!strcmp(argv[i], "--cam") && i + 1 < argc) cam_arg = argv[++i];
		else if (!strcmp(argv[i], "--view-distance") && i + 1 < argc) view_distance_override = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
		else if (!strcmp(argv[i], "--zone") && i + 1 < argc) zone = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--test-frames") && i + 1 < argc) test_frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--test-shot") && i + 1 < argc) test_shot = argv[++i];
	}
	find_data(data, argv[0]);
	if (mission) {
		char world[64], num[16];
		if (sscanf(mission, "%63[^/\\]%*[/\\]%15s", world, num) == 2) {
			for (int i = 0; i < NUM_MISSIONS; i++) {
				if (same_name(world, missions[i][0]) && same_name(num, missions[i][1])) {
					mission_index = i;
				}
			}
		}
	}
	if (!load_mission(mission_index)) {
		fprintf(stderr, "could not load %s/%s from %s\n", missions[mission_index][0], missions[mission_index][1], fs_root());
		exit(1);
	}
	render_init(w, h);
	if (zone >= 0 && level->zonedata && zone < level->zonedata->num_zones && level->zones &&
		zone < level->zones->count && level->zones->entries[zone].num_parts) {
		const model_t *zm = level->zones->entries[zone].parts[0];
		ivec3_t o = level->zonedata->origins[zone];
		vec3_t c = vec3_mulf(vec3_add(zm->bbox_min, zm->bbox_max), 0.5f);
		cam.pos = vec3(o.x + c.x, o.y + c.y, o.z + c.z);
		cam.yaw = 0;
		cam.pitch = 0;
	}
	if (cam_arg) {
		float x, y, z, yaw = 0, pitch = 0;
		if (sscanf(cam_arg, "%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch) >= 3) {
			cam.pos = vec3(x, y, z);
			cam.yaw = yaw * PI / 180;
			cam.pitch = pitch * PI / 180;
		}
	}
	const mission_t *m = level->mission;
	printf("%s: %d statics, %d objects, %d mobs, %d models, %d zones, lights %d/%d/%d/%d\n", level->dir,
		m->num_rec[0], m->num_rec[1], m->num_rec[2], m->num_models,
		level->zonedata ? level->zonedata->num_zones : 0,
		m->num_lights[0], m->num_lights[1], m->num_lights[2], m->num_lights[3]);
	printf("camera %.0f %.0f %.0f yaw %.1f, interior rgb %d %d %d, exterior rgb %d %d %d\n",
		cam.pos.x, cam.pos.y, cam.pos.z, cam.yaw * 180 / PI, m->interior.r, m->interior.g, m->interior.b,
		m->exterior.r, m->exterior.g, m->exterior.b);
	last_time = fps_time = platform_now();
}

void game_frame(void) {
	double now = platform_now();
	float dt = (float)(now - last_time);
	last_time = now;
	if (dt > 0.25f) {
		dt = 0.25f;
	}

	if (input_pressed(BTN_KEY(KEY_ESCAPE))) {
		exit(0);
	}
	if (input_pressed(BTN_KEY(KEY_PAGEDOWN))) {
		load_mission((mission_index + 1) % NUM_MISSIONS);
	}
	if (input_pressed(BTN_KEY(KEY_PAGEUP))) {
		load_mission((mission_index + NUM_MISSIONS - 1) % NUM_MISSIONS);
	}
	if (input_pressed(BTN_KEY('=')) || input_pressed(BTN_KEY(KEY_KP_PLUS))) {
		level->view_distance = view_distance_override = min(level->view_distance * 1.5f, 100000.0f);
	}
	if (input_pressed(BTN_KEY('-')) || input_pressed(BTN_KEY(KEY_KP_MINUS))) {
		level->view_distance = view_distance_override = max(level->view_distance / 1.5f, 4000.0f);
	}

	bool fast = input_down(BTN_KEY(KEY_LSHIFT)) || input_down(BTN_KEY(KEY_RSHIFT));
	float speed = (fast ? 8000.0f : 2000.0f) * dt;
	float turn = 1.6f * dt;
	if (input_down(BTN_KEY(KEY_LEFT))) cam.yaw += turn;
	if (input_down(BTN_KEY(KEY_RIGHT))) cam.yaw -= turn;
	if (input_down(BTN_KEY(KEY_UP))) cam.pitch += turn;
	if (input_down(BTN_KEY(KEY_DOWN))) cam.pitch -= turn;
	cam.pitch = clamp(cam.pitch, -1.5f, 1.5f);
	vec3_t fwd = vec3(sinf(cam.yaw) * cosf(cam.pitch), sinf(cam.pitch), cosf(cam.yaw) * cosf(cam.pitch));
	vec3_t right = vec3_normalize(vec3_cross(fwd, vec3(0, 1, 0))); // screen right
	if (input_down(BTN_KEY('w'))) cam.pos = vec3_add(cam.pos, vec3_mulf(fwd, speed));
	if (input_down(BTN_KEY('s'))) cam.pos = vec3_sub(cam.pos, vec3_mulf(fwd, speed));
	if (input_down(BTN_KEY('d'))) cam.pos = vec3_add(cam.pos, vec3_mulf(right, speed));
	if (input_down(BTN_KEY('a'))) cam.pos = vec3_sub(cam.pos, vec3_mulf(right, speed));
	if (input_down(BTN_KEY('r'))) cam.pos.y += speed;
	if (input_down(BTN_KEY('f'))) cam.pos.y -= speed;

	level_draw(level, &cam);
	platform_present();
	input_frame_end();

	if (test_frames >= 0 && ++frame_count >= test_frames) {
		if (test_shot) {
			png_write(test_shot, render_pixels(), render_width(), render_height());
		}
		printf("self test: %d frames in %.2f s, data %s\n", frame_count, platform_now(), fs_root());
		exit(0);
	}

	fps_frames++;
	if (now - fps_time >= 1.0) {
		char title[256];
		snprintf(title, sizeof(title), "Lander viewer - %s %s - %.1f fps - view %.0f - WASD/RF move, arrows look, "
			"shift fast, PgUp/PgDn mission, +/- view distance",
			missions[mission_index][0], missions[mission_index][1], fps_frames / (now - fps_time), level->view_distance);
		platform_set_title(title);
		fps_frames = 0;
		fps_time = now;
	}
}

void game_cleanup(void) {
	level_free(level);
	render_cleanup();
}
