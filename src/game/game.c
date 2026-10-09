// The in-game program: Mission_Load (0x413d20), Mission_Run (0x4146c0) with its
// system tasks, Mission_Unload; the camera object types; HUD and input glue.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "camera.h"
#include "input_map.h"
#include "hud.h"
#include "sound.h"
#include "font.h"
#include "ui.h"
#include "lights.h"
#include "objtypes.h"
#include "effects.h"
#include "physobjs.h"
#include "render_reflect.h"
#include "render_translucent.h"
#include "../fe/fe.h"
#include "missions/missions.h"
#include "../platform.h"
#include "../fs.h"

game_t G;

// ---------------------------------------------------------------- missions

// world / mission indices of the exe's tables (program-map.md §6); deathmatch maps have no brief.dll
static const mission_def_t missions[] = {
	{"TRAINING", "1", 21, 0, &mission_training_1, &brief_training_1},
	{"TRAINING", "2", 21, 1, &mission_training_2, &brief_training_2},
	{"MERCURY", "1", 0, 0, &mission_mercury_1, &brief_mercury_1},
	{"VENUS", "1", 1, 0, &mission_venus_1, &brief_venus_1},
	{"VENUS", "2", 1, 1, &mission_venus_2, &brief_venus_2},
	{"EARTH", "1", 2, 0, &mission_earth_1, &brief_earth_1},
	{"EARTH", "2", 2, 1, &mission_earth_2, &brief_earth_2},
	{"EARTH", "3", 2, 2, &mission_earth_3, &brief_earth_3},
	{"EARTH", "4", 2, 3, &mission_earth_4, &brief_earth_4},
	{"MARS", "1A", 4, 0, &mission_mars_1a, &brief_mars_1a},
	{"MARS", "1B", 4, 1, &mission_mars_1b, &brief_mars_1b},
	{"MARS", "2", 4, 2, &mission_mars_2, &brief_mars_2},
	{"MARS", "3", 4, 3, &mission_mars_3, &brief_mars_3},
	{"MARS", "4", 4, 4, &mission_mars_4, &brief_mars_4},
	{"MARS", "5", 4, 5, &mission_mars_5, &brief_mars_5},
	{"EUROPA", "1", 6, 0, &mission_europa_1, &brief_europa_1},
	{"EUROPA", "2", 6, 1, &mission_europa_2, &brief_europa_2},
	{"CALLISTO", "1", 7, 0, &mission_callisto_1, &brief_callisto_1},
	{"CALLISTO", "2", 7, 1, &mission_callisto_2, &brief_callisto_2},
	{"GANYMEDE", "1", 8, 0, &mission_ganymede_1, &brief_ganymede_1},
	{"IO", "1", 9, 0, &mission_io_1, &brief_io_1},
	{"IO", "2", 9, 1, &mission_io_2, &brief_io_2},
	{"TITAN", "1", 11, 0, &mission_titan_1, &brief_titan_1},
	{"TITAN", "2", 11, 1, &mission_titan_2, &brief_titan_2},
	{"TITAN", "3", 11, 2, &mission_titan_3, &brief_titan_3},
	{"MIMAS", "1", 12, 0, &mission_mimas_1, &brief_mimas_1},
	{"RHEA", "1", 13, 0, &mission_rhea_1, &brief_rhea_1},
	{"RHEA", "2", 13, 1, &mission_rhea_2, &brief_rhea_2},
	{"NEPTUNE", "1", 15, 0, &mission_neptune_1, &brief_neptune_1},
	{"NEPTUNE", "2", 15, 1, &mission_neptune_2, &brief_neptune_2},
	{"PLUTO", "1", 19, 0, &mission_pluto_1, &brief_pluto_1},
	{"DEATHMAT", "1", 20, 0, &mission_deathmat_1, NULL},
	{"DEATHMAT", "3", 20, 2, &mission_deathmat_3, NULL},
};

bool mission_is_ported(const mission_def_t *m) { return m->script && m->script->start_lander; }
static bool same(const char *a, const char *b) {
	for (; *a && *b; a++, b++) {
		if ((*a | 32) != (*b | 32)) return false;
	}
	return *a == *b;
}

const mission_def_t *mission_find(const char *world, const char *mission) {
	for (size_t i = 0; i < len(missions); i++) {
		if (same(world, missions[i].world) && same(mission, missions[i].mission)) {
			return &missions[i];
		}
	}
	return NULL;
}

const mission_def_t *mission_list(int *count) {
	*count = (int)len(missions);
	return missions;
}

// Missions not ported yet: wake the first lander and camera so the level can be flown.
static void generic_start_lander(void) {
	const mission_t *m = G.level->mission;
	int lander = -1, cam = -1;
	for (int i = 0; i < m->num_rec[REC_MOB]; i++) {
		if (m->rec[REC_MOB][i].type == OT_LANDER && lander < 0) lander = i;
		if (m->rec[REC_MOB][i].type == OT_CAMERA && cam < 0) cam = i;
	}
	SetGravity(15);
	if (lander >= 0) MobObj_Wake(lander);
	if (cam >= 0) {
		MobObj_Wake(cam);
		if (lander >= 0) Object_SetProperty(MOB_ID(OT_CAMERA, cam), PROP_TARGET, MOB_ID(OT_LANDER, lander));
	}
	Media_AudioPlay(0);
}

// ---------------------------------------------------------------- input glue (DirectInput codes)

static uint8_t dik_key[256];
static bool dik_latched[256], dik_prev[256];
static bool mouse_latched[3], mouse_prev[3];
static bool joy_latched[10], joy_prev[10];      // joystick buttons 0..9 (c_dfDIJoystick)
static float mouse_pending[2];                  // mouse motion since the last InputPoll

static void dik_init(void) {
	static const char *row1 = "1234567890", *row2 = "qwertyuiop", *row3 = "asdfghjkl", *row4 = "zxcvbnm";
	for (int i = 0; row1[i]; i++) dik_key[0x02 + i] = (uint8_t)row1[i];
	for (int i = 0; row2[i]; i++) dik_key[0x10 + i] = (uint8_t)row2[i];
	for (int i = 0; row3[i]; i++) dik_key[0x1e + i] = (uint8_t)row3[i];
	for (int i = 0; row4[i]; i++) dik_key[0x2c + i] = (uint8_t)row4[i];
	dik_key[0x01] = KEY_ESCAPE; dik_key[0x0c] = '-'; dik_key[0x0d] = '='; dik_key[0x0e] = KEY_BACKSPACE;
	dik_key[0x0f] = KEY_TAB; dik_key[0x1c] = KEY_RETURN; dik_key[0x1d] = KEY_LCTRL; dik_key[0x2a] = KEY_LSHIFT;
	dik_key[0x33] = ','; dik_key[0x34] = '.'; dik_key[0x36] = KEY_RSHIFT; dik_key[0x37] = KEY_KP_MULTIPLY;
	dik_key[0x38] = KEY_LALT; dik_key[0x39] = KEY_SPACE;
	for (int i = 0; i < 10; i++) dik_key[0x3b + i] = (uint8_t)(KEY_F1 + i);
	dik_key[0x57] = KEY_F11; dik_key[0x58] = KEY_F12;
	dik_key[0x47] = KEY_KP7; dik_key[0x48] = KEY_KP8; dik_key[0x49] = KEY_KP9; dik_key[0x4a] = KEY_KP_MINUS;
	dik_key[0x4b] = KEY_KP4; dik_key[0x4c] = KEY_KP5; dik_key[0x4d] = KEY_KP6; dik_key[0x4e] = KEY_KP_PLUS;
	dik_key[0x4f] = KEY_KP1; dik_key[0x50] = KEY_KP2; dik_key[0x51] = KEY_KP3; dik_key[0x52] = KEY_KP0;
	dik_key[0x53] = KEY_KP_PERIOD; dik_key[0x9c] = KEY_KP_ENTER; dik_key[0x9d] = KEY_RCTRL;
	dik_key[0xb5] = KEY_KP_DIVIDE; dik_key[0xb8] = KEY_RALT; dik_key[0xc7] = KEY_HOME; dik_key[0xc8] = KEY_UP;
	dik_key[0xc9] = KEY_PAGEUP; dik_key[0xcb] = KEY_LEFT; dik_key[0xcd] = KEY_RIGHT; dik_key[0xcf] = KEY_END;
	dik_key[0xd0] = KEY_DOWN; dik_key[0xd1] = KEY_PAGEDOWN; dik_key[0xd2] = KEY_INSERT; dik_key[0xd3] = KEY_DELETE;
}

static bool code_down(uint32_t code) {
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE) {
		uint32_t n = IM_CODE_INDEX(code);
		return n < 3 && input_down((button_t)(BTN_MOUSE_LEFT + n));
	}
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_JOY) { // Joystick_IsDown: buttons 0..9 of the first pad
		return IM_CODE_INDEX(code) < 10 && input_pad_connected() && input_down(im_code_to_button(code));
	}
	if (code & IM_CODE_DEVICE_MASK) return false;
	uint8_t k = dik_key[code & 0xff];
	return k && input_down(BTN_KEY(k));
}

static bool im_is_down(uint32_t code) { return code_down(code); }

static bool im_pressed(uint32_t code) {
	uint32_t n = IM_CODE_INDEX(code);
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE) return n < 3 && mouse_latched[n];
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_JOY) return n < 10 && joy_latched[n];
	return !(code & IM_CODE_DEVICE_MASK) && dik_latched[code & 0xff];
}

static void im_consume(uint32_t code) {
	uint32_t n = IM_CODE_INDEX(code);
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE) {
		if (n < 3) mouse_latched[n] = false;
	} else if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_JOY) {
		if (n < 10) joy_latched[n] = false;
	} else if (!(code & IM_CODE_DEVICE_MASK)) {
		dik_latched[code & 0xff] = false;
	}
}

static int32_t im_axis(uint32_t code) {
	uint32_t n = IM_CODE_INDEX(code);
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE && n < 2) {
		return im_mouse_axes[n];
	}
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_JOY && n < 6) { // range -0x400..0x400, gJoyDeadZone[n]
		return input_pad_connected() ? im_joy_scale(input_axis(im_joy_axis((int)n)), -0x400, 0x400, im_joy_deadzone[n]) : 0;
	}
	return 0;
}

static bool im_device_present(int kind) { // the DirectInput device list
	return kind == IM_DEV_KEYBOARD || kind == IM_DEV_MOUSE || (kind == IM_DEV_JOYSTICK && input_pad_connected());
}

static void mouse_poll(void) { // Mouse_Poll 0x42a060 (Input_Poll): the motion since the last poll
	im_mouse_poll((int32_t)mouse_pending[0], (int32_t)mouse_pending[1]);
	mouse_pending[0] = mouse_pending[1] = 0;
}

static void input_latch(void) { // key edges survive until a 80 ms sampler consumes them
	for (int c = 1; c < 256; c++) {
		if (!dik_key[c]) continue;
		bool d = code_down((uint32_t)c);
		if (d && !dik_prev[c]) dik_latched[c] = true;
		dik_prev[c] = d;
	}
	for (int n = 0; n < 3; n++) {
		bool d = input_down((button_t)(BTN_MOUSE_LEFT + n));
		if (d && !mouse_prev[n]) mouse_latched[n] = true;
		mouse_prev[n] = d;
	}
	for (uint32_t n = 0; n < 10; n++) {
		bool d = code_down(IM_CODE_JOY | n);
		if (d && !joy_prev[n]) joy_latched[n] = true;
		joy_prev[n] = d;
	}
	mouse_pending[0] += input_axis(AXIS_MOUSE_DX);
	mouse_pending[1] += input_axis(AXIS_MOUSE_DY);
}

// ---------------------------------------------------------------- camera object types (5, 6, 7)

static cam_view_t gView;
static autocam_t gAutoCam;
static ObjId autocam_id = OBJ_NONE;

static bool host_target_matrix(uint32_t id, cmatrix_t *out) {
	if (!Object_IsAwake(id)) return false;
	const FwMatrix *m = (const FwMatrix *)Object_GetProperty(id, PROP_MATRIX);
	if (!m) return false;
	memcpy(out->m, m->m, sizeof(out->m));
	memcpy(out->t, m->t, sizeof(out->t));
	return true;
}

static cvec3_t host_target_velocity(uint32_t id) {
	const int32_t *v = (const int32_t *)Object_GetProperty(id, PROP_VELOCITY);
	return v ? (cvec3_t){v[0], v[1], v[2]} : (cvec3_t){0, 0, 0};
}

static bool host_lander_yaw(uint32_t id, int32_t *yaw) {
	if (OBJ_TYPE(id) != OT_LANDER) return false;
	*yaw = Lander_GetHeading(id);
	return true;
}

static bool host_segment_hit(cvec3_t p0, cvec3_t p1, uint32_t mask, uint32_t ignore, cvec3_t *hit) {
	int32_t a[3] = {p0.x, p0.y, p0.z}, b[3] = {p1.x, p1.y, p1.z};
	int old = G.world.query_mask;
	int32_t old_ex = phys_g.exclude_owner;
	G.world.query_mask = (int)mask;
	phys_g.exclude_owner = (int32_t)ignore;
	phys_hit_t h;
	int found = phys_segment_hit(a, b, 2, &h); // 1 = hit (flags != 0)
	G.world.query_mask = old;
	phys_g.exclude_owner = old_ex;
	if (!found) return false;
	*hit = (cvec3_t){h.point[0], h.point[1], h.point[2]};
	return true;
}

static const cmatrix_t *host_inertial(void) { return gUseInertialFrame ? (const cmatrix_t *)&gInertialFrame : NULL; }
// gInertialFrameInterp column 1 (physobjs.c, InertialFrame render 0x457f20)
static cvec3_t host_inertial_up(void) {
	return (cvec3_t){gInertialFrameInterp.m[0][1], gInertialFrameInterp.m[1][1], gInertialFrameInterp.m[2][1]};
}

// gTargetZoneTracker follows the camera target (ZoneTracker_Move 0x485b00); the camera's tracker is
// a copy of it walked from the target to the eye (0x485880), so the camera's zones stay right when
// the eye passes through geometry without a portal (a lift going down into a tube). The level's own
// tracking then continues from that eye to the rendered one.
static zone_set_t target_zones;
static ivec3_t target_zones_pos;
static bool target_zones_valid;

static void host_zones_track(cvec3_t p) {
	level_t *lv = G.level;
	if (!lv || !lv->zonedata) return;
	ivec3_t q = {p.x, p.y, p.z};
	// a teleport (the original relocates with ZoneLoc_Update when the target is (re)set) or the start
	if (!target_zones_valid || abs(q.x - target_zones_pos.x) > 0x2000 || abs(q.y - target_zones_pos.y) > 0x2000 ||
		abs(q.z - target_zones_pos.z) > 0x2000) {
		zoneset_init(lv->zonedata, &target_zones, q, -2);
		target_zones_valid = true;
	} else {
		zoneset_move(lv->zonedata, &target_zones, &target_zones_pos, q);
	}
	target_zones_pos = q;
}

static void host_zones_camera(cvec3_t from, cvec3_t to) {
	level_t *lv = G.level;
	if (!lv || !lv->zonedata || !target_zones_valid) return;
	ivec3_t a = {from.x, from.y, from.z}, b = {to.x, to.y, to.z};
	lv->cam_set = target_zones;
	zoneset_move(lv->zonedata, &lv->cam_set, &a, b);
	lv->cam_last = b;
	lv->cam_set_valid = true;
}

cam_view_t *game_view(void) { return &gView; } // the view node for the AnimCam / FlyCam types
// gUIKeys Left / Right / Up / Down (the AutoCam's orbit in demo playback)
static const uint8_t orbit_dik[4] = {0xcb, 0xcd, 0xc8, 0xd0};
static bool host_key_down(int k) { return k >= 0 && k < 4 && code_down(orbit_dik[k]); }
static bool host_key_pressed(int k) {
	if (k < 0 || k >= 4 || !im_pressed(orbit_dik[k])) return false;
	im_consume(orbit_dik[k]);
	return true;
}
static void host_set_occluded(bool o) { (void)o; }

static void camera_create(task_t *t, void *arg) { // Camera_Create 0x450e10 (one AutoCam)
	obj_args_t *a = arg;
	ObjId id = a ? a->id : (ObjId)(Task_ToIndex(t) | 0x02060000);
	bool from_record = a && a->rec;
	if (from_record) {
		FwMatrix m;
		fw_from_packed(a->rec->rot, &m);
		memcpy(gView.mat.m, m.m, sizeof(gView.mat.m));
		gView.mat.t[0] = a->rec->pos.x;
		gView.mat.t[1] = a->rec->pos.y;
		gView.mat.t[2] = a->rec->pos.z;
	}
	autocam_create(&gAutoCam, &gView, id, im_demo_active(), from_record);   // flags 0x802 in demo playback
	autocam_id = id;
	t->data[0] = (intptr_t)id;
	gCameraObject = id;   // Camera_SetTarget 0x449a90: focus object + camera task
	gCameraTask = t;
}

static void camera_update(task_t *t) { (void)t; autocam_update(&gAutoCam); }
static void camera_render(task_t *t, uint32_t time) { autocam_frame(&gAutoCam, &gView, time, t->wake); }

static void camera_destroy(task_t *t) {
	if (gCameraTask == t) gCameraTask = NULL;
	if (gCameraObject == (ObjId)t->data[0]) gCameraObject = OBJ_NONE;
	autocam_id = OBJ_NONE;
}

static intptr_t camera_get_pos(ObjId id) { (void)id; return (intptr_t)&gAutoCam.eye; }
static intptr_t camera_get_vel(ObjId id) {
	static cvec3_t v;
	(void)id;
	v = autocam_velocity_per_second(&gAutoCam);
	return (intptr_t)&v;
}
static intptr_t camera_get_flags(ObjId id) { (void)id; return (intptr_t)gAutoCam.flags; }
static void camera_set_pos(ObjId id, intptr_t v) {
	(void)id;
	const int32_t *p = (const int32_t *)v;
	autocam_set_pos(&gAutoCam, (cvec3_t){p[0], p[1], p[2]}, gAutoCam.target == (uint32_t)gThisPlayer);
}
static void camera_set_target(ObjId id, intptr_t v) {
	(void)id;
	autocam_set_target(&gAutoCam, &gView, (uint32_t)v);
}
static void camera_set_flags(ObjId id, intptr_t v) {
	(void)id;
	uint32_t f = (uint32_t)v;
	if (f & 0x80000000u) gAutoCam.flags &= ~f; else gAutoCam.flags |= f;
}

int32_t AutoCam_GetUserAngle(ObjId c) { (void)c; return autocam_get_user_angle(&gAutoCam); }
void AutoCam_SetUserAngle(ObjId c, int32_t i) { (void)c; autocam_set_user_angle(&gAutoCam, i); }
void AutoCam_SetLagY(ObjId c, int32_t lag) { (void)c; autocam_set_lag_y(&gAutoCam, lag); }
void AutoCam_SnapToTarget(ObjId c) { (void)c; autocam_snap(&gAutoCam, &gView); }

static void register_camera_types(void) {
	cam_host = (cam_host_t){host_target_matrix, host_target_velocity, host_lander_yaw, host_segment_hit,
		host_inertial, host_inertial_up, host_zones_track, host_zones_camera, host_key_down, host_key_pressed,
		host_set_occluded};
	ObjectType_Register(OT_CAMERA, camera_update, camera_render, camera_create, camera_destroy, 0);
	ObjectProperty_RegisterGet(OT_CAMERA, 0, camera_get_pos);
	ObjectProperty_RegisterGet(OT_CAMERA, 7, camera_get_vel);
	ObjectProperty_RegisterGet(OT_CAMERA, 8, camera_get_flags);
	ObjectProperty_RegisterSet(OT_CAMERA, 0, camera_set_pos);
	ObjectProperty_RegisterSet(OT_CAMERA, 1, camera_set_target);
	ObjectProperty_RegisterSet(OT_CAMERA, 8, camera_set_flags);
}

// ---------------------------------------------------------------- HUD glue

static hud_t gHud;
static bool hud_active;

static void hud_text(const char *s, int x, int y, uint32_t c, int font) { font_print(s, x, y, c, font); }
static int hud_text_width(const char *s, int font) { return font_width(s, font); }

static int hud_project(const int32_t w[3], int *sx, int *sy, int *sz) { // in the HUD's 2D units
	vec4_t v = mat4_mul_vec4(render_view(), (vec4_t){(float)w[0], (float)w[1], (float)w[2], 1});
	if (v.z < 10) return 0;
	vec4_t c = mat4_mul_vec4(render_projection(), v);
	float s = render_2d_scale();
	*sx = (int)((c.x / c.w + 1) * 0.5f * render_width() / s);
	*sy = (int)((1 - c.y / c.w) * 0.5f * render_height() / s);
	*sz = (int)v.z;
	return 1;
}

static void hud_model(int model, const int16_t rot[3][3], const int32_t pos[3], uint32_t wire, int screen_dx) {
	effects_hud_model(model, rot, pos, wire, screen_dx); // 3D attitude models (effects_hud.c)
}

static bool hud_object_pos(uint32_t id, int32_t out[3]) {
	if (!Object_IsAwake(id)) return false;
	const int32_t *p = (const int32_t *)Object_GetProperty(id, PROP_POSITION);
	if (!p) return false;
	memcpy(out, p, sizeof(int32_t) * 3);
	return true;
}

static bool hud_cockvox(int prio) { return CockVox(prio) != 0; }
static void hud_voice_chain(int n, const int *sids) { sound_chain(SND_COCKPIT_VOICE, -1, 0, n, (const int32_t *)sids); }

void game_hud_create(int lander_type) {
	hud_create(&gHud, render_2d_width(), render_2d_height(), lander_type, gMultiPlayerGame != 0, gLanguage);
	gHud.waypoints = (const int32_t(*)[2])gWaypoints;
	gHud.num_waypoints = gNbWaypoints;
	hud_active = true;
}

void game_hud_destroy(void) { hud_active = false; }
int game_hud_marker_add(int32_t what, uint32_t rgb, int32_t kind) { return hud_radar_add(&gHud, (uint32_t)what, rgb, (uint32_t)kind); }
void game_hud_marker_remove(int m) { if (m >= 0) hud_radar_remove(&gHud, m); }

static void hud_frame(void) {
	if (!hud_active || !gHudLander) {
		return;
	}
	Lander *L = gHudLander;
	gHud.w = render_2d_width();      // the frame (widescreen: wider than 640, changes with the display)
	gHud.h = render_2d_height();
	gHud.now = Clock_Now();
	gHud.shield = L->ps.shield;
	gHud.fuel = L->ps.fuel;
	gHud.max_fuel = L->ps.maxFuel;
	gHud.contacts = L->body ? L->body->impulse_sum : 0;
	gHud.weapon = (const hud_weapon_desc_t *)gLanderHud.weapon;
	gHud.weapon_ammo = gLanderHud.weaponLevel;
	gHud.lock_target = (uint32_t)gLanderHud.target;
	gHud.waypoints = (const int32_t(*)[2])gWaypoints;
	gHud.num_waypoints = gNbWaypoints;
	gHud.player_yaw = L->heading;
	memcpy(gHud.player_pos, L->world.t, sizeof(gHud.player_pos));
	memcpy(gHud.lander_rot, L->display.m, sizeof(gHud.lander_rot));
	memcpy(gHud.lander_t, L->display.t, sizeof(gHud.lander_t));
	// world -> camera rotation (rows = camera axes) for the attitude models
	int16_t cam_rot[3][3];
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			cam_rot[r][c] = gView.mat.m[c][r];
		}
	}
	hud_draw(&gHud, cam_rot);
}

void game_hud_lander_tick(Lander *L) { // the HUD part of Lander_Update, after Ctrl_ReadWord
	if (hud_active && L == gHudLander && L->body) {
		memcpy(gHud.player_pos, L->world.t, sizeof(gHud.player_pos));
		gHud.player_yaw = L->heading;
		hud_tick(&gHud, L->ctrlWord, L->body->vel);
	}
}

// ---------------------------------------------------------------- system tasks (Mission_Run)

static uint32_t ctrl_local;
static bool debug_trace;
static int timer_ticks;
static bool game_over_flag;
static uint32_t game_over_time;
static int game_over_dead;

task_t *gPhysicsTask;   // DAT_00577c38 (the pods phase their updates with it)

static void phys_task_update(task_t *t) { // PhysTask 0x413890
	(void)t;
	phys_g.game_time = gGameUpdateTime;
	if (!gTrainingMission) {
		phys_task(gGameUpdateTime);
	}
}

static void input_task_update(task_t *t) { // InputPoll 0x491ac0 (80 ms)
	(void)t;
	mouse_poll();   // Input_Poll: the mouse axes' running sums
	if (game_over_flag && Clock_Now() - game_over_time > 2000) {
		for (int b = BTN_KEY_FIRST; b < BTN_KEY_LAST; b++) {
			if (input_down((button_t)b)) {
				gLevelEndFlag = 1;
				break;
			}
		}
	}
}

static void timer_task_update(task_t *t) { // Timer 0x4136a0: event 3 every 100 ms, event 2 every second
	(void)t;
	LanderEvent ev = {EV_TICK_100MS, {0}};
	Event_Raise(&ev);
	if (++timer_ticks >= 10) {
		timer_ticks = 0;
		ev.type = EV_TICK_1S;
		Event_Raise(&ev);
	}
}

static void viewrange_task_update(task_t *t) { // ViewRange 0x413770: object waking, tile streaming
	(void)t;
	vec3_t focus = G.cam_pos;
	if (gCameraObject != OBJ_NONE && Object_IsAwake(gCameraObject)) {
		const int32_t *p = (const int32_t *)Object_GetProperty(gCameraObject, PROP_POSITION);
		if (p) focus = vec3((float)p[0], (float)p[1], (float)p[2]);
	}
	Map_UpdateProximity(focus, G.level->view_distance * 1.25f);
	int32_t pos[3] = {(int32_t)G.cam_pos.x, (int32_t)G.cam_pos.y, (int32_t)G.cam_pos.z};
	float vd = G.level->view_distance;
	phys_world_stream_tiles(&G.world, pos, (int32_t)(vd * 1.6f));
}

static void ctrl_sample_update(task_t *t) { // CtrlSample 0x4254e0 (80 ms)
	(void)t;
	if (im_demo_active()) return;   // gDemoPlayback: the words come from DEMO.DAT
	ctrl_local = im_sample_word(ctrl_local);
}

static void ctrl_dist_update(task_t *t) { // CtrlDist 0x4257d0 (80 ms, t = 40, 120, ...)
	(void)t;
	if (gThisPlayerId >= 0) {
		if (im_demo_active()) {     // Ctrl_PlaybackAll 0x425770 (one player: DAT_00491be8 = 1)
			if (!im_demo_next(gCtrlWords, 1)) gLevelEndFlag = 1;
			return;
		}
		gCtrlWords[gThisPlayerId & 15] = ctrl_local;
	}
}

static const task_type_t phys_task_type = {0, phys_task_update, NULL, NULL, NULL, NULL, NULL, 40};
static const task_type_t input_task_type = {0, input_task_update, NULL, NULL, NULL, NULL, NULL, 80};
static const task_type_t timer_task_type = {0, timer_task_update, NULL, NULL, NULL, NULL, NULL, 100};
static const task_type_t viewrange_task_type = {0, viewrange_task_update, NULL, NULL, NULL, NULL, NULL, 40};
static const task_type_t ctrlsample_task_type = {0, ctrl_sample_update, NULL, NULL, NULL, NULL, NULL, 80};
static const task_type_t ctrldist_task_type = {0, ctrl_dist_update, NULL, NULL, NULL, NULL, NULL, 80};

void game_over(int dead) { // Mission_GameOver 0x414680
	if (game_over_flag) return;
	game_over_flag = true;
	game_over_dead = dead;
	game_over_time = Clock_Now();
	GameOverMsg_Show(dead);   // out of fuel / dead, and the press-a-key line
}

// ---------------------------------------------------------------- pause (Mission_Pause 0x4145c0 / Mission_Resume 0x414610)

static bool game_paused;      // gPaused
static uint32_t pause_time;   // gPauseTime

static void game_pause(void) {
	game_paused = true;
	gUiShowPointer = 1;
	PauseMenu_Open();
	pause_time = Clock_Now();
	sound_set_volumes(0, gMusicVolume / 4096.0f);      // the SFX volume factor is 0 while paused
}

static void game_resume(void) {
	game_paused = false;
	gUiShowPointer = 0;
	PauseMenu_Close();
	Clock_Set(pause_time);
	sound_set_volumes(gSfxVolume / 4096.0f, gMusicVolume / 4096.0f);
}

void game_pause_select(int index) { // PauseMenu_OnSelect 0x4074e0
	if (index == 0) {
		game_resume();
	} else if (index == 1) {
		gLevelEndFlag = 1;          // Abort Mission
	} else if (index == 2) {
		PauseMenu_Close();          // Exit Lander: are you sure?
		QuitConfirm_Open();
	}
}

// ---------------------------------------------------------------- lighting

void game_lighting_for(vec3_t center, float radius, lighting_t *out) {
	level_lighting(G.level, &G.base_light, center, radius, out);
	dyn_lights_add(center, radius, out);
}

uint32_t game_now(void) { return Clock_Now(); }
bool game_is_paused(void) { return game_paused; }

// ---------------------------------------------------------------- load / run / unload

static void build_physics_world(void) {
	level_t *lv = G.level;
	phys_world_t *w = &G.world;
	memset(w, 0, sizeof(*w));
	if (lv->zonedata && lv->zonedata->num_zones && lv->zones) {
		w->zone_octree = lv->zonedata->raw + lv->zonedata->octree_offset;
		w->num_zones = lv->zonedata->num_zones;
		w->zone_origins = lv->zonedata->origins;
		G.zone_mesh = calloc(w->num_zones, sizeof(phys_mesh_t));
		for (int i = 0; i < w->num_zones; i++) {
			const model_t *m = i < lv->zones->count && lv->zones->entries[i].num_parts ? lv->zones->entries[i].parts[0] : NULL;
			if (m) phys_mesh_init(&G.zone_mesh[i], m, i);
		}
		w->zone_mesh = G.zone_mesh;
	}
	if (lv->tiledata && lv->tiles) {
		w->tiledata = lv->tiledata;
		w->num_tile_models = lv->tiles->count;
		G.tile_mesh = calloc(w->num_tile_models + 1, sizeof(phys_mesh_t));
		for (int i = 0; i < w->num_tile_models; i++) {
			const model_t *m = lv->tiles->entries[i].num_parts ? lv->tiles->entries[i].parts[0] : NULL;
			if (m) phys_mesh_init(&G.tile_mesh[i], m, i);
		}
		w->tile_mesh = G.tile_mesh;
	}
	w->surface_class = lv->mission->surface_class;
	w->num_surface_class = lv->mission->num_surface;
	w->query_mask = 0x1f;
	Objects_SetupCollision(w);
	phys_set_world(w);
	phys_reset();
}

static void reset_mission_globals(void) { // Players_Reset 0x444270 and friends
	gTrainingMission = 0;
	gWaypoints = NULL;
	gNbWaypoints = 0;
	gInOutOverride = 0;
	gAirResistance = 21;
	gMainThrottlePower = 48;
	gNbPlayers = 0;
	gThisPlayerId = -1;
	gThisPlayer = -1;
	gCameraObject = OBJ_NONE;
	gHudLander = NULL;
	gLevelEndFlag = 0;
	game_over_flag = false;
	timer_ticks = 0;
	ctrl_local = 0;
	memset(gCtrlWords, 0, sizeof(gCtrlWords));
	for (int i = 0; i < 16; i++) gPlayerObjects[i] = OBJ_NONE;
	memset(gLanders, 0, sizeof(gLanders));
	hud_active = false;
	target_zones_valid = false;
	objtypes_reset_mission();
	eng_api_reset_mission();
	SetGravity(26);
}

// Missions not ported yet, Mission_Run mode 1: every retail LevelStartLevel_Flycam wakes the FlyCam mob
static void generic_start_flycam(void) {
	const mission_t *m = G.level->mission;
	SetGravity(15);
	for (int i = 0; i < m->num_rec[REC_MOB]; i++) {
		if (m->rec[REC_MOB][i].type == OT_FLYCAM) {
			MobObj_Wake(i);
			break;
		}
	}
}

// Mission_Run 0x4146c0 start modes: 0/3 lander, 1 fly-through camera (LevelStartLevel_Flycam),
// 2 animated camera (LevelStartLevel_Animcam, missing export = nothing), 4 network game (Animcam,
// then Lander; the network set-up itself is not here)
static void start_mission_mode(int mode) {
	const mission_script_t *s = G.script;
	switch (mode) {
	case 1:
		if (s) {
			if (s->start_flycam) s->start_flycam();
		} else {
			generic_start_flycam();
		}
		break;
	case 2:
		if (s && s->start_animcam) s->start_animcam();
		break;
	case 4:
		if (s && s->start_animcam) s->start_animcam();
		// fall through
	default:
		if (s) s->start_lander();
		else generic_start_lander();
		break;
	}
}

static int start_mode;

// Mission_Run with a start mode (game_load_mission = mode 0, or LANDER_START_MODE for tests)
bool game_load_mission_mode(const mission_def_t *def, int mode) {
	start_mode = mode;
	return game_load_mission(def);
}

bool game_load_mission(const mission_def_t *def) {
	int mode = start_mode;
	start_mode = 0;
	if (!mode && getenv("LANDER_START_MODE")) mode = atoi(getenv("LANDER_START_MODE"));
	game_unload_mission();
	G.level = level_load(def->world, def->mission);
	if (!G.level) {
		return false;
	}
	G.level->view_distance = (float)gFogDistance;   // the "Fog Distance" option (front end)
	game_paused = false;
	G.world_index = def->world_index;
	G.mission_index = def->mission_index;
	G.script = mission_is_ported(def) ? def->script : NULL;
	G.global_tex = G.level->global_textures;
	G.global_models = ppm_load("MISC\\OBJECTS.PPM");
	static bool once;
	if (!once) {
		dik_init();
		im_host = (im_host_t){im_is_down, im_pressed, im_consume, im_axis, im_device_present};
		// CtrlDistTask_Init: CtrlConf_Select(gCtrlConfCurrent) (Config_Load read CTRLCONF.DAT); a config
		// whose device is missing falls back to Keyboard Default (the rewrite's start config)
		if (im_active_index() < 0 && im_config_select(im_config_current) < 0) im_config_select(0);
		font_init();
		sound_init();
		hud_use_rewrite_render();
		hud_host.text = hud_text;
		hud_host.text_width = hud_text_width;
		hud_host.project = hud_project;
		hud_host.model = hud_model;
		hud_host.object_pos = hud_object_pos;
		hud_host.cockvox = hud_cockvox;
		hud_host.voice_chain = hud_voice_chain;
		once = true;
	}
	hud_tex58 = G.global_tex && G.global_tex->count > 58 ? G.global_tex->entries[58].frame : NULL; // gGlobalTextures[0x3a]
	build_physics_world();
	dyn_lights_reset();
	ui_reset();
	ui_load_mission_text(G.level->dir, true);
	sound_load_database(G.level->dir);

	// Mission_Run set-up (0x4146c0)
	Events_Reset();
	Events_RegisterAll();
	Sched_Reset();
	reset_mission_globals();
	memset(gObjectTypes, 0, sizeof(gObjectTypes));
	lander_register_type();
	register_camera_types();
	objtypes_register_all();
	Map_Reset();
	memset(gView.mat.t, 0, sizeof(gView.mat.t));   // Mission_Run: view at (0, 0x800, 0)
	gView.mat.t[1] = 0x800;
	gPhysicsTask = Task_CreateNow(&phys_task_type, NULL);   // physics first
	if (G.script) {
		char dll[96];   // the DLL's text, the first time (gametext.c)
		snprintf(dll, sizeof(dll), "MISSIONS\\%s\\%s\\MISSIONR.DLL", def->world, def->mission);
		gametext_load(dll, G.script->text);
		G.script->load(G.level->dir);
	}
	start_mission_mode(mode);
	Task_CreateNow(&input_task_type, NULL);
	Task_CreateNow(&timer_task_type, NULL);
	Task_CreateNow(&viewrange_task_type, NULL);
	Task_CreateNow(&ctrlsample_task_type, NULL);
	Task_Create(&ctrldist_task_type, NULL, 40);
	G.mission_result = 0;
	debug_trace = gEventTrace = getenv("LANDER_DEBUG") != NULL;
	Clock_Set(0);
	return true;
}

void game_unload_mission(void) { // Mission_Unload 0x4143d0
	if (!G.level) {
		return;
	}
	sound_stop_all();
	music_stop();
	if (G.script && G.script->stop) {
		G.script->stop();
	}
	Sched_DestroyAll();
	Map_SleepAll();
	Objects_FreeCollision();
	for (int i = 0; G.zone_mesh && i < G.world.num_zones; i++) phys_mesh_free(&G.zone_mesh[i]);
	for (int i = 0; G.tile_mesh && i < G.world.num_tile_models; i++) phys_mesh_free(&G.tile_mesh[i]);
	free(G.zone_mesh);
	free(G.tile_mesh);
	G.zone_mesh = G.tile_mesh = NULL;
	ppm_free(G.global_models);
	G.global_models = NULL;
	level_free(G.level);
	G.level = NULL;
}

bool game_mission_frame(void) { // the Mission_Run loop body
	static int frame;
	uint32_t now = frame++ < 5 ? (Clock_Set(0), 0) : Clock_Update();
	input_latch();
	// P pauses / resumes; Esc pauses with the quit question (Mission_Run 0x4146c0)
	if (!game_paused) {
		if (input_pressed(BTN_KEY('p'))) game_pause();
	} else if (input_pressed(BTN_KEY('p'))) {
		game_resume();
	}
	if (input_pressed(BTN_KEY(KEY_ESCAPE)) && !game_paused) {
		game_pause();
		PauseMenu_Close();
		QuitConfirm_Open();
	}
	if (gLevelEndFlag || platform_should_quit()) {
		if (gExitLanderRequested) gMissionResult = 0;   // "Exit Lander": result 0 and the program ends
		G.mission_result = gMissionResult;
		gGame_ScoreData.timeMs = (int32_t)now;          // the mission time shown by the score sheet
		if (game_paused) game_resume();
		GameOverMsg_Hide();
		frame = 0;
		return false;
	}
	if (!game_paused) {
		Sched_RunUntil(now);
	} else {
		now = pause_time;
	}

	// camera
	Sched_RenderCamera(now);
	cam_basis_t b = cam_view_basis(&gView);
	camera_t cam = {.pos = vec3(b.eye[0], b.eye[1], b.eye[2]), .has_basis = true};
	cam.fwd = vec3(b.fwd[0], b.fwd[1], b.fwd[2]);
	cam.up = vec3(-b.down[0], -b.down[1], -b.down[2]);
	G.cam_pos = cam.pos;
	sound_set_listener(cam.pos, vec3(b.right[0], b.right[1], b.right[2]));
	if (debug_trace && frame % 30 == 0) { // LANDER_DEBUG=1: camera / lander trace
		const int32_t *lp = gHudLander ? gHudLander->world.t : NULL;
		printf("t=%u cam %.0f %.0f %.0f fwd %.2f %.2f %.2f target %08x lander %d %d %d fuel %d flags %x outside %d tasks free %d\n",
			now, cam.pos.x, cam.pos.y, cam.pos.z, cam.fwd.x, cam.fwd.y, cam.fwd.z, gCameraObject,
			lp ? lp[0] : 0, lp ? lp[1] : 0, lp ? lp[2] : 0, gHudLander ? gHudLander->ps.fuel : 0,
			gThisPlayerId >= 0 && Object_IsAwake(gPlayerObjects[gThisPlayerId & 15]) ? (int)Object_GetProperty(gPlayerObjects[gThisPlayerId & 15], PROP_FLAGS) : 0, G.level->outside_factor,
			Sched_FreeCount());
		printf("   ctrl %08x autocam eye %d %d %d look %d %d %d target %08x flags %x view.t %d %d %d\n", ctrl_local, gAutoCam.eye.x, gAutoCam.eye.y,
			gAutoCam.eye.z, gAutoCam.look.x, gAutoCam.look.y, gAutoCam.look.z, gAutoCam.target, gAutoCam.flags,
			gView.mat.t[0], gView.mat.t[1], gView.mat.t[2]);
	}

	// world, then objects and task renders
	level_begin_frame(G.level, &cam, &G.base_light);
	gOutsideFactor = G.level->outside_factor;
	render_defer(true);         // the frame is drawn at its end, by several threads
	effects_begin_frame(now);   // sky gradient and sky layer
	render_translucent_begin(); // PCP translucent triangles into the translucency table
	// Venus: the reflection pass is built after the main scene but drawn before it (OT+0x10)
	bool reflect = render_reflect_wanted(&cam);
	if (reflect) render_record_begin();
	level_draw_world(G.level, &cam, &G.base_light);
	render_list_reset();
	Objects_QueueVisible(cam.pos, G.level->view_distance);
	Sched_RenderAll(now);
	render_list_run();
	effects_render(now);
	if (reflect) {
		render_record_end();
		render_reflect_draw(&cam, now);
		render_replay();
	}
	render_translucent_end();

	// 2D
	render_reset_scissor();
	if (g2dRenderCallback) {
		// the scripts' Print / Font_Print are made for 640x480: in the middle of a wider frame, like the
		// in-game windows (ui.c)
		int ox, oy;
		ui_area_offset(&ox, &oy);
		render_set_2d_offset((float)ox, (float)oy);
		g2dRenderCallback(NULL);
		render_set_2d_offset(0, 0);
	}
	hud_frame();
	ui_draw();            // objective lines, pause menu, quit question, game-over message
	ui_draw_pointer();
	render_defer(false);
	sound_update();
	return true;
}
