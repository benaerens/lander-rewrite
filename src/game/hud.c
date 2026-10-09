// Lander (1999) in-game HUD - faithful C translation. See hud.h and re/specs/camera-input-hud.md §3.
// Addresses are lander.EXE (D3D build) virtual addresses. Screen coordinates are pixels at the current
// resolution; the original does not scale the HUD (positions below are the 640x480 values).
#include <stdio.h>
#include <string.h>

#include "camera.h" // fx_rsin / fx_rcos / fx_ratan2 / fx_sqrt / cvec_normalise
#include "hud.h"
#include "gametext.h"

hud_draw_t hud_host;

#define LINE(x0, y0, x1, y1, c) do { if (hud_host.line) hud_host.line((x0), (y0), (x1), (y1), (c), HUD_BLEND_ADD); } while (0)
#define TEXT(s, x, y) do { if (hud_host.text) hud_host.text((s), (x), (y), HUD_GREEN_TEXT, 0); } while (0)

static int text_w(const char *s) { return hud_host.text_width ? hud_host.text_width(s, 0) : 0; }

static void sprite(int x, int y, int16_t top, int16_t left, int16_t bottom, int16_t right) {
	// fx flags 2 (blend only): unmodulated texture, additive
	if (hud_host.sprite) hud_host.sprite(x, y, HUD_TEX_HUD, (hud_rect_t){top, left, bottom, right}, false, 0, HUD_BLEND_ADD);
}

// per-lander sight origin (table 0x4933f8 + 0x30, stride 0x54): Hopper, Sky Diver, Big Red, Destria, Roche 81
static const int16_t sight_origin[5][2] = {{90, 45}, {0, -38}, {166, 58}, {120, 83}, {0, -83}};

static uint32_t g_now; // gNow for the blinking warning lights

// ---------------------------------------------------------------------------------------------
// shared drawing helpers

// Hud_DrawLineXf 0x411f60: gauge-local line; flags 1 = rotate (vertical scale), 2 = mirror x
static void line_xf(int ox, int oy, int x0, int y0, int x1, int y1, uint32_t flags, uint32_t colour) {
	int a0, b0, a1, b1;
	if (flags & 1) { a0 = y0; a1 = y1; b0 = -x0; b1 = -x1; }
	else { a0 = x0; a1 = x1; b0 = y0; b1 = y1; }
	if (flags & 2) { a0 = 0x80 - a0; a1 = 0x80 - a1; }
	LINE(a0 + ox, b0 + oy, a1 + ox, b1 + oy, colour);
}

// Hud_DrawWarnLight 0x411e70: 15x5 outline, 14x4 fill while colour != 0 and (gNow & mask)
static void warn_light(int x, int y, uint32_t colour, uint32_t mask) {
	LINE(x, y, x + 15, y, HUD_GREEN_LINE);
	LINE(x, y + 5, x + 15, y + 5, HUD_GREEN_LINE);
	LINE(x, y, x, y + 5, HUD_GREEN_LINE);
	LINE(x + 15, y, x + 15, y + 5, HUD_GREEN_LINE);
	if (colour != 0 && (g_now & mask) != 0 && hud_host.tile) {
		hud_host.tile(x, y, 14, 4, colour, HUD_BLEND_ALPHA); // FwTileDraw blend arg 2: alpha, colour opaque
	}
}

// ---------------------------------------------------------------------------------------------
// HUD windows

// HudGauge_Draw 0x412030 (Shield / Fuel). flags 2 = mirrored (fills from the right).
static void draw_gauge(int x, int y, const char *label, int32_t value, int32_t max, uint32_t flags) {
	LINE(x, y + 10, x + 0x80, y + 10, HUD_GREEN_LINE);
	line_xf(x, y, 0, 0, 0, 0x18, flags, HUD_GREEN_LINE);
	line_xf(x, y, 0x80, 0, 0x80, 0xa, flags, HUD_GREEN_LINE);
	line_xf(x, y, 0, 0x18, 0x40, 0x18, flags, HUD_GREEN_LINE);
	int fill = max ? (int16_t)((value << 7) / max) : 0;
	uint32_t colour = 0, mask = 0x300;
	if (fill < 0x2c) { colour = 0xa0; mask = 0x100; }   // red, 256 ms blink
	else if (fill < 0x4a) { colour = 0x90a0; }           // amber, on 768 of 1024 ms
	warn_light((flags & 2) ? x + 0x71 : x, y - 7, colour, mask);
	if (fill != 0) {
		if (flags & 2) sprite(x + 0x80 - fill, y, 8, (int16_t)(0x80 - fill), 0x10, 0x80);
		else sprite(x, y, 0, 0, 8, (int16_t)fill);
	}
	if (label) {
		if (flags & 2) TEXT(label, x + 0x80 - text_w(label) - 5, y + 12);
		else TEXT(label, x + 5, y + 12);
	}
}

// HudScale_Draw 0x412470 (Roll, Pitch, VS). flags 1 vertical, 2 mirror, 8 label, 0x10 centred
// (range is already doubled for 0x10, as HudScale_Create stores it).
static void draw_scale(int x, int y, const char *label, int32_t value, int32_t range, uint32_t flags) {
	int32_t v = value;
	if (flags & 0x10) v += range >> 1;
	int pos = (int16_t)((v * 100) / range);
	line_xf(x, y, pos, -10, pos, 10, flags, HUD_RED_NEEDLE);
	line_xf(x, y, 0, 0, 100, 0, flags, HUD_GREEN_LINE);
	line_xf(x, y, 50, -5, 50, 5, flags, HUD_GREEN_LINE);
	if ((flags & 8) && label) {
		int lx = x, ly = y - 12;
		if (flags & 1) { ly -= 100; lx -= text_w(label) >> 1; }
		TEXT(label, lx, ly);
	}
}

// HudDial_Draw 0x4125c0 (the radar disc "WP"). value NULL = no needle.
static void draw_dial(int x, int y, const char *label, const int32_t *value, int32_t max, uint32_t flags) {
	if ((flags & 8) && label) TEXT(label, x - (text_w(label) >> 1) + 0x20, y - 12);
	if (value && (flags & 4)) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", *value);
		TEXT(buf, x + 0x38, y);
	}
	sprite(x, y, 0x30, 0x30, 0x70, 0x70); // 64x64 radar ring
	if (value && max) {
		int32_t a = (*value << 12) / max;
		int cx = x + 0x20, cy = y + 0x20;
		LINE(cx, cy, cx + (fx_rsin(a) * 29 >> 12), cy + ((-29 * fx_rcos(a)) >> 12), HUD_GREEN_LINE);
	}
}

// HudBlip_Draw 0x4128d0: alpha-blended, coloured dot (+ halo for objects)
static void draw_blip(const hud_marker_t *m) {
	if (!hud_host.sprite) return;
	hud_host.sprite(m->bx - 2, m->by - 2, HUD_TEX_HUD, (hud_rect_t){0x68, 0x10, 0x6d, 0x15}, true, m->blip_colour, HUD_BLEND_ALPHA);
	if (m->halo) {
		hud_host.sprite(m->bx - 3, m->by - 3, HUD_TEX_HUD, (hud_rect_t){0x60, 0x10, 0x67, 0x17}, true, m->halo_colour, HUD_BLEND_ALPHA);
	}
}

// Hud_DrawLockMarker 0x4118d0
static void draw_lock_marker(uint32_t target) {
	int32_t p[3];
	int sx, sy, sz;
	if (target == 0xffffffffu || !hud_host.object_pos || !hud_host.object_pos(target, p) || !hud_host.project) return;
	if (hud_host.project(p, &sx, &sy, &sz) & 1) {
		sprite(sx - 0x10, sy - 0x10, 0x12, 6, 0x32, 0x28);
	}
}

// Hud_DrawWeaponPanel 0x4119a0 (top right, selected weapon)
static const char *weapon_label[3][2];        // 0x4918c0: per language the heat and the ammo label
enum { KIND_MISSILES, KIND_MISSILES_DE, KIND_FLARES_FR, KIND_FLARES_DE, KIND_FLARES, KIND_PRIMARY_FR, KIND_PRIMARY_DE,
	KIND_PRIMARY };
static const char *kind_label[8];              // the weapon kind's label per language: 0x491974..0x4919c8

static void draw_weapon_panel(const hud_t *h) {
	const hud_weapon_desc_t *wd = h->weapon;
	if (!wd) return;
	int lang = (h->language >= 0 && h->language < 3) ? h->language : 0;
	int xr = h->w - 0x14;
	LINE(xr, 0xc, xr, 0xa8, HUD_GREEN_LINE);
	LINE(h->w - 0x4c, 0x18, xr, 0x18, HUD_GREEN_LINE);
	LINE(h->w - 0x44, 0x9c, xr, 0x9c, HUD_GREEN_LINE);
	LINE(h->w - 0x44, 0xa8, xr, 0xa8, HUD_GREEN_LINE);
	const char *label2;
	if (!(wd->flags & 1)) {
		for (int i = 0; i < h->weapon_ammo; i++) { // ammo pips, 16 per column
			int16_t top = i > 4 ? 0x1c : 0x24;
			sprite(xr - ((i >> 4) + 1) * 0x1c, 0x92 - (i & 0xf) * 8, top, 0x66, (int16_t)(top + 8), 0x80);
		}
		if ((wd->flags & 4) && h->lock_target != 0) draw_lock_marker(h->lock_target);
		label2 = weapon_label[lang][1];
	} else {
		int a = h->weapon_ammo; // heat * 0x70 >> 14
		sprite(h->w - 0x20, 0x92 - a, (int16_t)(0x80 - a), 0, 0x80, 5);
		label2 = weapon_label[lang][0];
	}
	TEXT(label2, xr - text_w(label2) - 8, 0x9c);
	const char *cat;
	if (wd->kind == 2) cat = kind_label[lang == 2 ? KIND_MISSILES_DE : KIND_MISSILES];
	else if (wd->kind == 7) cat = kind_label[lang == 1 ? KIND_FLARES_FR : lang == 2 ? KIND_FLARES_DE : KIND_FLARES];
	else cat = kind_label[lang == 1 ? KIND_PRIMARY_FR : lang == 2 ? KIND_PRIMARY_DE : KIND_PRIMARY];
	TEXT(cat, xr - text_w(cat) - 8, 0xc);
}

// HudSight_Draw 0x4122b0: a square at a lander-local point, side (size << 11) / z
static void draw_sight(const hud_t *h, int i) {
	if (!hud_host.project) return;
	const int32_t *o = h->sight_off[i];
	int32_t p[3];
	for (int r = 0; r < 3; r++) { // ApplyMatrixLV(lander world matrix, offset) + t
		p[r] = ((h->lander_rot[r][0] * o[0] + h->lander_rot[r][1] * o[1] + h->lander_rot[r][2] * o[2] + 0x800) >> 12) + h->lander_t[r];
	}
	int sx, sy, sz;
	if (!(hud_host.project(p, &sx, &sy, &sz) & 1) || sz == 0) return;
	int size = (int16_t)((h->sight_size << 11) / sz);
	int half = size >> 1;
	int x0 = sx - half, y0 = sy - half, x1 = x0 + size, y1 = y0 + size;
	LINE(x0, y0, x1, y0, h->sight_colour[i]);
	LINE(x0, y1, x1, y1, h->sight_colour[i]);
	LINE(x0, y0, x0, y1, h->sight_colour[i]);
	LINE(x1, y0, x1, y1, h->sight_colour[i]);
}

// ---------------------------------------------------------------------------------------------
// radar markers (0x412dc0 .. 0x413170)

static uint32_t colour_scale(uint32_t c, int32_t w) { // 0x44a5b0
	uint32_t r = 0;
	for (int sh = 24; sh >= 0; sh -= 8) {
		uint32_t v = ((c >> sh) & 0xff) * (uint32_t)w >> 12;
		if (v > 0xff) v = 0xff;
		r |= v << sh;
	}
	return r;
}

static uint32_t colour_gradient(uint32_t below, uint32_t level, uint32_t above, int32_t t) { // 0x44a770
	uint32_t r = 0;
	if (t >= 0) r = colour_scale(above, t) + colour_scale(level, 0x1000 - t);
	if (t < 0) r = r + colour_scale(below, -t) + colour_scale(level, t + 0x1000);
	return r;
}

static bool marker_measure(hud_t *h, hud_marker_t *m) { // RadarMarker_Measure 0x412dc0
	cvec3_t d;
	if (!(m->flags & 1)) {
		int32_t p[3];
		if (!hud_host.object_pos || !hud_host.object_pos(m->id, p)) return false;
		d = (cvec3_t){p[0] - h->player_pos[0], p[1] - h->player_pos[1], p[2] - h->player_pos[2]};
	} else {
		if (!h->waypoints || (int)m->id >= h->num_waypoints) return false;
		d = (cvec3_t){h->waypoints[m->id][0] - h->player_pos[0], 0, h->waypoints[m->id][1] - h->player_pos[2]};
	}
	int32_t a = fx_ratan2(d.x, d.z);
	m->bearing = (h->player_yaw + 0x200 - a) & 0xfff;
	m->dy = d.y > 0x1900 ? 0x1900 : d.y < -0x1900 ? -0x1900 : d.y;
	int32_t adx = d.x < 0 ? -d.x : d.x, adz = d.z < 0 ? -d.z : d.z;
	int32_t dist = 0x3200;
	if (adx + adz < 0xb3b1) {
		int32_t l = cvec_normalise(&d);
		if (l < 0x3201) dist = l;
	}
	m->dist = dist;
	return true;
}

static void marker_update(hud_t *h, hud_marker_t *m) { // RadarMarker_Update 0x412ec0
	if (!marker_measure(h, m)) {
		m->has_blip = false;
		return;
	}
	int32_t r = m->dist * 0x1d / 0x3200;
	int32_t s = fx_rsin(m->bearing), c = fx_rcos(m->bearing);
	uint32_t halo = m->colour;
	if (!(m->flags & 1)) {
		halo = m->dist == 0x3200 ? 0xff0000u // out of range: blue
			: colour_gradient(0xff00, 0xffff, 0xff, (m->dy << 12) / 0x1900); // below green, level yellow, above red
	}
	if (!m->has_blip) {
		m->has_blip = true;
		m->halo = !(m->flags & 1);
	}
	m->bx = (s * r >> 12) + 0x34;
	m->by = (-(c * r) >> 12) - 0x34 + h->h;
	m->blip_colour = m->colour;
	m->halo_colour = halo;
}

int hud_radar_add(hud_t *h, uint32_t id, uint32_t colour, uint32_t flags) { // 0x413010
	for (int i = 0; i < HUD_MAX_MARKERS; i++) {
		hud_marker_t *m = &h->markers[i];
		if (m->id == 0xffffffffu) {
			memset(m, 0, sizeof(*m));
			m->id = id;
			m->colour = colour;
			m->flags = flags;
			marker_update(h, m);
			return i;
		}
	}
	return -1;
}

void hud_radar_remove(hud_t *h, int i) { // 0x4130f0
	if (i >= 0 && i < HUD_MAX_MARKERS) {
		h->markers[i].id = 0xffffffffu;
		h->markers[i].has_blip = false;
	}
}

void hud_radar_remove_all(hud_t *h) { // 0x413140
	for (int i = 0; i < HUD_MAX_MARKERS; i++) hud_radar_remove(h, i);
}

// ---------------------------------------------------------------------------------------------
// create / tick / draw

void hud_create(hud_t *h, int screen_w, int screen_h, int lander_type, bool multiplayer, int language) { // 0x420770
	memset(h, 0, sizeof(*h));
	h->w = screen_w;
	h->h = screen_h;
	h->multiplayer = multiplayer;
	h->language = language;
	h->pitch_input = h->roll_input = 0x20; // 0x4933b8/bc initial values
	memcpy(h->wp_label, "WP  ", 5);       // 0x4933c0
	int t = (lander_type >= 0 && lander_type < 5) ? lander_type : 0;
	for (int i = 0; i < 3; i++) { // HudSight_Create(node, {x, y, 1500 + 500 i}, 0x3c, colour)
		h->sight_off[i][0] = sight_origin[t][0];
		h->sight_off[i][1] = sight_origin[t][1];
		h->sight_off[i][2] = 0x5dc + 500 * i;
	}
	h->sight_colour[0] = 0xa000;
	h->sight_colour[1] = 0x6000;
	h->sight_colour[2] = 0x2000;
	h->sight_size = 0x3c;
	h->lock_target = 0xffffffffu;
	h->wp_marker = -1;
	for (int i = 0; i < HUD_MAX_MARKERS; i++) h->markers[i].id = 0xffffffffu;
}

void hud_tick(hud_t *h, uint32_t w, const int32_t vel[3]) { // Lander_Update 0x423115 .. 0x4233a2
	// waypoint toggle (control word bit 29), single player only
	if (!h->multiplayer && h->num_waypoints != 0) {
		if (w & 0x20000000u) {
			if (++h->cur_waypoint >= h->num_waypoints) h->cur_waypoint = 0;
			if (h->wp_marker != -1) hud_radar_remove(h, h->wp_marker);
			h->wp_marker = hud_radar_add(h, (uint32_t)h->cur_waypoint, 0xff, 1);
		}
		if (h->wp_marker == -1) h->wp_marker = hud_radar_add(h, (uint32_t)h->cur_waypoint, 0xff, 1);
	}
	// gauges fed by the control word
	h->roll_input = ((int32_t)(w << 14) >> 26) + 0x20;
	h->pitch_input = ((int32_t)(w << 26) >> 26) + 0x20;
	if (!h->multiplayer) {
		int32_t bearing;
		if (h->waypoints) {
			if ((w & 0x20000000u) && hud_host.cockvox && hud_host.cockvox(1) && hud_host.voice_chain) {
				if (h->cur_waypoint < 3) {
					int sids[3] = {0x41f, 0x3fc + h->cur_waypoint, 0x418}; // WAYPOINT, "1".."3", SELECTED
					hud_host.voice_chain(3, sids);
				} else {
					int sids[2] = {0x41f, 0x418};
					hud_host.voice_chain(2, sids);
				}
			}
			h->wp_label[3] = (char)('1' + h->cur_waypoint);
			bearing = fx_ratan2(h->waypoints[h->cur_waypoint][0] - h->player_pos[0],
				h->waypoints[h->cur_waypoint][1] - h->player_pos[2]);
		} else {
			if ((w & 0x20000000u) && hud_host.cockvox && hud_host.cockvox(1) && hud_host.voice_chain) {
				int sids[2] = {0x41f, 0x41d}; // WAYPOINT, UNAVAIL
				hud_host.voice_chain(2, sids);
			}
			h->wp_label[3] = '-';
			bearing = fx_ratan2(-h->player_pos[0], h->player_pos[2]);
		}
		h->hidden_wp_bearing = (bearing + h->player_yaw - 0x600) & 0xfff;
	}
	h->hidden_heading = (h->player_yaw - 0x600) & 0xfff;
	h->hidden_ground_speed = fx_sqrt(vel[0] * vel[0] + vel[2] * vel[2]);
	h->vertical_speed = vel[1] > 0x800 ? 0x800 : vel[1] < -0x800 ? -0x800 : vel[1];
	// weapon panel values (weapon, weapon_ammo, lock_target) are set by the caller from the lander
}

static void mul_rot(int16_t out[3][3], const int16_t a[3][3], const int16_t b[3][3]) { // MulMatrix0
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out[i][j] = (int16_t)((a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + 0x800) >> 12);
}

// Text_Get(0xe0) / Text_Get(0x9b) per gLanguage (UI string tables 0x493870 + lang*0x418)
static const char *shield_label[3], *fuel_label[3];

// the labels' text in lander.EXE (gametext.c); UI_TEXT: an entry of the UI string tables
#define UI_TEXT(lang, id, p) {.kind = GT_POINTERS, .va = 0x493870 + (lang) * 0x418 + (id) * 4, .count = 1, .out = p, \
	.va_glide = 0x48c7a8 + (lang) * 0x418 + (id) * 4}
const GameText hud_exe_text[] = {
	{.kind = GT_POINTERS, .va = 0x4918c0, .count = 3 * 2, .out = weapon_label, .va_glide = 0x48a7f8},
	{.kind = GT_STRINGS, .va = 0x491974, .count = 1, .out = &kind_label[KIND_MISSILES], .va_glide = 0x48a8ac},
	{.kind = GT_STRINGS, .va = 0x491980, .count = 1, .out = &kind_label[KIND_MISSILES_DE], .va_glide = 0x48a8b8},
	{.kind = GT_STRINGS, .va = 0x491988, .count = 1, .out = &kind_label[KIND_FLARES_FR], .va_glide = 0x48a8c0},
	{.kind = GT_STRINGS, .va = 0x49199c, .count = 1, .out = &kind_label[KIND_FLARES_DE], .va_glide = 0x48a8d4},
	{.kind = GT_STRINGS, .va = 0x4919ac, .count = 1, .out = &kind_label[KIND_FLARES], .va_glide = 0x48a8e4},
	{.kind = GT_STRINGS, .va = 0x4919b4, .count = 1, .out = &kind_label[KIND_PRIMARY_FR], .va_glide = 0x48a8ec},
	{.kind = GT_STRINGS, .va = 0x4919c0, .count = 1, .out = &kind_label[KIND_PRIMARY_DE], .va_glide = 0x48a8f8},
	{.kind = GT_STRINGS, .va = 0x4919c8, .count = 1, .out = &kind_label[KIND_PRIMARY], .va_glide = 0x48a900},
	UI_TEXT(0, 0xe0, &shield_label[0]), UI_TEXT(1, 0xe0, &shield_label[1]), UI_TEXT(2, 0xe0, &shield_label[2]),
	UI_TEXT(0, 0x9b, &fuel_label[0]), UI_TEXT(1, 0x9b, &fuel_label[1]), UI_TEXT(2, 0x9b, &fuel_label[2]),
	{0},
};

void hud_draw(hud_t *h, const int16_t cam_rot[3][3]) {
	g_now = h->now;
	const int W = h->w, H = h->h;
	const int lang = (h->language >= 0 && h->language < 3) ? h->language : 0;

	// 3D attitude indicator (render items placed by the lander render callback 0x4237d9), camera space.
	// [A] widescreen: it sits in front of the 640x480 camera beside the bottom-right scales; on a wider
	// frame it moves right with them (half the extra width: it is drawn around the frame's centre)
	if (hud_host.model && cam_rot) {
		static const int32_t pos[3] = {0xf0, 0xb0, 0x200};
		int dx = W > 640 ? (W - 640) / 2 : 0;
		int16_t r[3][3];
		mul_rot(r, cam_rot, h->lander_rot);              // viewRot * lander rotation
		hud_host.model(HUD_MODEL_ATTITUDE, r, pos, 0, dx);   // solid
		hud_host.model(HUD_MODEL_ATTITUDE, r, pos, 0x40, dx); // wire, dark red (draw fn 0x4201c0)
		int16_t ry[3][3];
		int32_t c = fx_rcos(h->player_yaw), s = fx_rsin(h->player_yaw);
		const int16_t y[3][3] = {{(int16_t)c, 0, (int16_t)s}, {0, 0x1000, 0}, {(int16_t)-s, 0, (int16_t)c}};
		mul_rot(ry, cam_rot, y);                           // Transpose(cam world) then Coord_RotateY(yaw)
		hud_host.model(HUD_MODEL_FRAME, ry, pos, 0x4000, dx);  // wire, dark green (draw fn 0x420190)
	}

	// Hud_Draw 0x411d10: velocity cross + COMPASS.PCP need gHudVelocityVec (0x49c554), which the retail
	// exe never sets -> dead. Then the weapon panel and the radar markers.
	draw_weapon_panel(h);
	for (int i = 0; i < HUD_MAX_MARKERS; i++) {
		if (h->markers[i].id != 0xffffffffu) marker_update(h, &h->markers[i]);
	}

	// windows in creation order (Lander_CreateHud)
	for (int i = 0; i < 3; i++) draw_sight(h, i);
	draw_scale(W - 0x78, H - 0x14, "Roll", h->roll_input, 0x40, 0);            // anchor bottom-right
	draw_scale(W - 0x14, H - 0x14, "Pitch", h->pitch_input, 0x40, 1);
	draw_gauge(0x78, H - 0x40, shield_label[lang], h->shield, 0xff, 0);       // absolute (120, H-64)
	draw_gauge(0x10e, H - 0x40, fuel_label[lang], h->fuel, h->max_fuel, 2);   // absolute (270, H-64), mirrored
	draw_dial(0x14, H - 0x54, h->multiplayer ? NULL : h->wp_label, NULL, 0, h->multiplayer ? 0 : 8); // bottom-left
	draw_scale(W - 0x14, H - 0x8c, "VS", h->vertical_speed, 0x1000, 0x19);
	warn_light(W - 0x14, H - 0x14, h->contacts ? 0xff0000u : 0, h->contacts ? 0xffffffffu : 0); // contact light
	for (int i = 0; i < HUD_MAX_MARKERS; i++) {
		if (h->markers[i].id != 0xffffffffu && h->markers[i].has_blip) draw_blip(&h->markers[i]);
	}
}

// ---------------------------------------------------------------------------------------------
#ifdef HUD_WITH_REWRITE_RENDER
// Reference adapter for the rewrite's software renderer (rewrite/src/render.h). The integrator still
// supplies the global texture 58 (hud_tex58), the font (text/text_width), projection and model draw.
#include "render.h"

const texture_t *hud_tex58;

static rgba_t cref(uint32_t c) { return rgba((uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), 255); }

static void rw_line(int x0, int y0, int x1, int y1, uint32_t c, int blend) {
	blend_t b = blend == HUD_BLEND_ADD ? BLEND_ADD : blend == HUD_BLEND_ALPHA ? BLEND_ALPHA : BLEND_OPAQUE;
	if (y0 == y1) { // horizontal / vertical lines as 1-pixel quads, others stepped
		int a = x0 < x1 ? x0 : x1, n = (x0 < x1 ? x1 - x0 : x0 - x1) + 1;
		render_quad_2d((float)a, (float)y0, (float)n, 1, 0, 0, 0, 0, cref(c), NULL, b);
	} else if (x0 == x1) {
		int a = y0 < y1 ? y0 : y1, n = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
		render_quad_2d((float)x0, (float)a, 1, (float)n, 0, 0, 0, 0, cref(c), NULL, b);
	} else {
		int dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
		for (int i = 0; i <= n; i++) {
			render_quad_2d((float)(x0 + dx * i / n), (float)(y0 + dy * i / n), 1, 1, 0, 0, 0, 0, cref(c), NULL, b);
		}
	}
}

static void rw_tile(int x, int y, int w, int hh, uint32_t c, int blend) {
	(void)blend;
	render_quad_2d((float)x, (float)y, (float)w, (float)hh, 0, 0, 0, 0, cref(c), NULL, BLEND_OPAQUE);
}

static void rw_sprite(int x, int y, int tex, hud_rect_t r, bool colour_on, uint32_t c, int blend) {
	(void)tex;
	if (!hud_tex58) return;
	float tw = (float)hud_tex58->width, th = (float)hud_tex58->height;
	render_quad_2d((float)x, (float)y, (float)(r.right - r.left), (float)(r.bottom - r.top),
		r.left / tw, r.top / th, r.right / tw, r.bottom / th,
		colour_on ? cref(c) : rgba(255, 255, 255, 255), hud_tex58, blend == HUD_BLEND_ADD ? BLEND_ADD : BLEND_ALPHA);
}

void hud_use_rewrite_render(void) {
	hud_host.line = rw_line;
	hud_host.tile = rw_tile;
	hud_host.sprite = rw_sprite;
}
#endif
