// The lander's effects, called by its draw callback (Lander_DrawCallback 0x425980) through the eng_fx_*
// adapters of lander.h: engine glow light (0x40aeb0-0x40ac00), engine flame (0x40af70), tractor beam
// (0x40c030, orientation 0x44ca20), attitude jets (0x40ac30) and the headlight cone (0x4569a0/0x4569d0).
// The callback sets the GTE matrices before each: the model node for the flame, the world rotation (or
// the beam node's) with the model's translation for the beam, the body node for the jets.
#include <stdlib.h>
#include <string.h>

#include "effects_int.h"
#include "lights.h"
#include "ui.h"

static coord_t *cur_model;   // model node of the lander being drawn (eng_fx_engine_begin)

void fx_lander_reset(void) { cur_model = NULL; }

// ---------------------------------------------------------------- engine light

void eng_fx_engine_light_create(LanderEngineFx *fx, void *coord) { // 0x40aeb0
	dyn_light_t *l = dyn_light_create(1, coord);
	if (l) {
		l->flags |= 0x10;
		l->pos[0] = 0;
		l->pos[1] = fx->pos[1];
		l->pos[2] = fx->pos[2];
		l->inner = 0x200;          // Light_SetRange(0x200, 0x800) (inner, falloff as eng_light_range)
		l->falloff = 0x800;
		l->color = vec3(0, 0, 0);
	}
	fx->light = l;
}

void eng_fx_engine_light_destroy(LanderEngineFx *fx) { // 0x40af00
	dyn_light_destroy(fx->light);
	fx->light = NULL;
}

// 0x40ac00 (+ 0x40ab60) and the camera position in the model frame (FUN_00431c40) of the draw callback
void eng_fx_engine_begin(LanderEngineFx *fx, void *model_coord) {
	cur_model = model_coord;
	dyn_light_t *l = fx->light;
	if (l) {
		if (fx->level != 0) { // flame colour x (1 + flicker), range 1500 x (1 + flicker)
			int32_t k = (fx->flickerSel == 0 ? fx->flicker[0] : fx->flicker[1]) + 0x1000;
			uint32_t c = fx->colour[0];
			int32_t r = (int32_t)(k * (c & 0xff)) >> 12, g = (int32_t)(((c >> 8) & 0xff) * k) >> 12;
			int32_t b = (int32_t)(((c >> 16) & 0xff) * k) >> 12;
			l->color = vec3(min(r, 0xff) / 255.0f, min(g, 0xff) / 255.0f, min(b, 0xff) / 255.0f);
			l->inner = 0x200;
			l->falloff = (k * 0x5dc) >> 12;
		} else {
			l->color = vec3(0, 0, 0);
		}
		l->off = false;
	}
	if (cur_model && fxc.valid) {
		const FwMatrix *m = coord_world(cur_model);
		int32_t d[3] = {(int32_t)fxc.eye.x - m->t[0], (int32_t)fxc.eye.y - m->t[1], (int32_t)fxc.eye.z - m->t[2]};
		int64_t y = (int64_t)m->m[0][1] * d[0] + (int64_t)m->m[1][1] * d[1] + (int64_t)m->m[2][1] * d[2];
		fx->viewBelow = (int16_t)(y >> 12) < -0x3f;
	}
}

// ---------------------------------------------------------------- engine flame (0x40af70)

static int outcode(int32_t x, int32_t y, int32_t z) { // FUN_0040af20
	int c = 0;
	if (x < 0) c = 1;
	else if (x >= fxc.w) c = 2;
	if (y < 0) c |= 4;
	else if (y >= fxc.ht) c |= 8;
	if (z < 0x40) return c | 0x10;
	if (z >= (int32_t)(fxc.fog_end * 1.25f)) c |= 0x20;
	return c;
}

// the flame colours: a 0x00BBGGRR colour times i (4.12) as a D3D 0x00RRGGBB (the original's bit mixing)
static uint32_t flame_colour(uint32_t bgr, int32_t i) {
	uint32_t r = bgr & 0xff, g = (bgr >> 8) & 0xff, b = (bgr >> 16) & 0xff;
	uint32_t t = ((uint32_t)(g * i) & 0xfffff00fu) | ((uint32_t)(b * i) >> 8);
	return (t >> 4) | (((uint32_t)(r * i) & 0xfffff000u) << 4);
}

static uint32_t grey_of_max(uint32_t c) {
	uint32_t m = (c >> 16) & 0xff;
	if (m < ((c >> 8) & 0xff)) m = (c >> 8) & 0xff;
	if (m < (c & 0xff)) m = c & 0xff;
	return (m << 8 | m) << 8 | m;
}

// the glow textures: entries 1 (flare streak) and 5 (round glow) of misc\standard, TexFile slot 3
// (DAT_0061ea58 / DAT_0061ea5c, set by FUN_0044a2b0(DAT_00577c68 = TexFile_Open(3)))
static const texture_t *standard_tex(int index) {
	texfile_t *tf = gStandardTextures;
	return tf && index < tf->count && tf->entries[index].frame ? &tf->entries[index].frame[0] : NULL;
}

// FUN_0044a4a0 / FUN_0044a330: the engine glow, two lens-flare streaks more when seen from below
static void engine_glow(int32_t x, int32_t y, int32_t z, uint32_t bgr, int32_t size, bool below) {
	const texture_t *flare = standard_tex(1), *glow = standard_tex(5);
	if (below && flare) {
		fx_sprite_section((float)x, (float)y, (float)z, flare, 0, 0, flare->width, flare->height, 0xf, bgr, size,
			fx_ratan2(x + 300, y + 300), FX_ADD, true);
		fx_sprite_section((float)x, (float)y, (float)z, flare, 0, 0, flare->width, flare->height, 0xf, bgr, size >> 1,
			fx_ratan2(x - 400, y + 100), FX_ADD, true);
	}
	if (!glow) return;
	uint32_t half = below ? (bgr & 0xfefefefeu) >> 1 : (bgr >> 1) & 0x7f7f7f7fu;
	fx_sprite_section((float)x, (float)y, (float)z, glow, 0, 0, glow->width, glow->height, 7, half, size, 0, FX_ADD, true);
	fx_sprite_section((float)x, (float)y, (float)z, glow, 0, 0, glow->width, glow->height, 7, grey_of_max(bgr), size >> 2, 0,
		FX_ADD, true);
}

void effects_glow(int32_t sx, int32_t sy, int32_t z, uint32_t bgr, int32_t size) { // FUN_0044a4a0
	if (fxc.valid) engine_glow(sx, sy, z, bgr, size, false);
}

void effects_glow_flares(int32_t sx, int32_t sy, int32_t z, uint32_t bgr, int32_t size) { // FUN_0044a330
	if (fxc.valid) engine_glow(sx, sy, z, bgr, size, true);
}

bool effects_project(const int32_t world[3], int32_t *sx, int32_t *sy, int32_t *z) { // FUN_00431d20
	if (!fxc.valid) return false;
	vec3_t c = fx_to_cam(vec3((float)world[0], (float)world[1], (float)world[2]));
	if (c.z <= 0) return false;
	float x = fxc.cx + c.x * fxc.h / c.z, y = fxc.cy + c.y * fxc.h / c.z;
	if (x <= -32768 || x >= 32767 || y <= -32768 || y >= 32767) return false;
	*sx = (int32_t)x;
	*sy = (int32_t)y;
	*z = (int32_t)c.z;
	return true;
}

void eng_fx_engine_flame(LanderEngineFx *fx) {
	if (!cur_model || !fxc.valid) return;
	fx_xform_t x;
	fx_xform_world(coord_world(cur_model), &x);
	if (!(x.t[2] >= 1.0f)) return;
	float fsx, fsy, z0;
	vec3_t p0 = vec3(fx->pos[0], fx->pos[1], fx->pos[2]);
	if (!fx_rtp(&x, p0, &fsx, &fsy, &z0)) return;
	int32_t sx0 = (int32_t)fsx, sy0 = (int32_t)fsy, zn = (int32_t)z0;
	int32_t L = fx->level;
	vec3_t p1 = vec3((int16_t)((fx->dir[0] * L) >> 15) + fx->pos[0], (int16_t)((fx->dir[1] * L) >> 15) + fx->pos[1],
		(int16_t)((fx->dir[2] * L) >> 15) + fx->pos[2]);
	float z1;
	if (!fx_rtp(&x, p1, &fsx, &fsy, &z1)) return;
	int32_t sx1 = (int32_t)fsx, sy1 = (int32_t)fsy;
	// the flame axis in camera space: looking along it fades the flame and strengthens the glow
	int32_t dcz = (int32_t)(x.m[2][0] * fx->dir[0] + x.m[2][1] * fx->dir[1] + x.m[2][2] * fx->dir[2]);
	int32_t width = ((int32_t)fxc.hw * 0x78) / (int32_t)x.t[2];   // DAT_004985e0: the (4:3) half width
	int32_t adz = abs(dcz);
	int32_t fade = 0x1000;
	if (adz >= 0xfc1) {
		fade = fx_rcos((adz - 0xfc0) * 0x10);
		if (fade < 0) fade = 0;
	}
	int32_t s = fx_rsin(L >> 2);
	int32_t bright = (fade * L) >> 12;
	int32_t dx = sx1 - sx0, dy = sy1 - sy0;
	int32_t glow = (s * ((adz >> 1) + 0x800)) >> 12;
	int32_t gx, gy;
	if ((dx == 0 && dy == 0) || bright == 0) {
		gx = ((dx * 600) >> 12) + sx0;
		gy = ((dy * 600) >> 12) + sy0;
	} else {
		int32_t l = fx_sqrt(dx * dx + dy * dy);
		if (l == 0) return;
		int32_t k = (width << 12) / l;
		int32_t px = (k * dy) >> 12, py = -(k * dx) >> 12;     // local_198, local_178
		int32_t wstep = ((0x800 - L) / 4) * 0x800, w = 0x800000;
		int32_t vx[4][5], vy[4][5];
		for (int r = 0; r < 4; r++) {
			int32_t cx = ((r * dx * 0x48d) >> 12) + sx0, cy = ((r * dy * 0x48d) >> 12) + sy0;
			vx[r][2] = cx;
			vy[r][2] = cy;
			for (int j = 1; j <= 2; j++) {
				int32_t ox = ((w * j >> 12) * px) >> 12, oy = ((w * j >> 12) * py) >> 12;
				vx[r][2 - j] = cx - ox; vy[r][2 - j] = cy - oy;
				vx[r][2 + j] = cx + ox; vy[r][2 + j] = cy + oy;
			}
			w += wstep;
		}
		if (outcode(vx[0][0], vy[0][0], zn) & outcode(vx[0][4], vy[0][4], zn) & outcode(vx[3][0], vy[3][0], zn) &
			outcode(vx[3][4], vy[3][4], zn)) {
			return;
		}
		if (++fx->flickerSel == 4) { // a new flicker every fourth drawn frame
			fx->flickerSel = 0;
			int32_t rnd = fx_rand();
			fx->flicker[0] = rnd >> 5;
			fx->flicker[1] = rnd >> 6;
		}
		uint32_t col[4][5];
		int32_t i = bright << 12;
		for (int r = 0; r < 4; r++) {
			int32_t c = i >> 12;
			if (r == fx->flickerSel) c = ((fx->flicker[0] + 0x1000) * c) >> 12;
			uint32_t c0 = flame_colour(fx->colour[0], c), c1 = flame_colour(fx->colour[1], c);
			col[r][0] = 0; col[r][1] = c1; col[r][2] = c0; col[r][3] = c1; col[r][4] = 0;
			i -= bright * 0x555;
		}
		fxv_t v[20];
		for (int r = 0; r < 4; r++) {
			for (int c = 0; c < 5; c++) {
				v[r * 5 + c] = (fxv_t){fx_unproject((float)vx[r][c], (float)vy[r][c], (float)zn), 0, 0, col[r][c]};
			}
		}
		for (int c = 0; c < 4; c++) { // four strips across the flame, additive
			uint16_t idx[8];
			for (int r = 0; r < 4; r++) {
				idx[r * 2] = (uint16_t)(r * 5 + c + (c >= 2));
				idx[r * 2 + 1] = (uint16_t)(r * 5 + c + (c < 2));
			}
			fx_queue_strip(v, idx, 8, NULL, FX_ADD, false, (float)zn);
		}
		gx = vx[0][2];
		gy = vy[0][2];
	}
	if (fx->flickerSel == 0) glow = (((fx->flicker[0] >> 1) + 0x1000) * glow) >> 12;
	else if (abs(fx->flickerSel) == 1) glow = (((fx->flicker[1] >> 1) + 0x1000) * glow) >> 12;
	int32_t size = ((glow + 0xc00) * width) >> 5;
	uint32_t c = fx->colour[0];
	uint32_t bgr = (((uint32_t)((c >> 16) & 0xff) * glow & 0xfffff000u) << 4) | ((uint32_t)((c & 0xff) * glow) >> 12) |
		(uint32_t)((((c >> 8) & 0xff) * glow) >> 12 & 0xffffff) << 8;
	int32_t z = (int16_t)zn;
	if (fx->viewBelow) {
		engine_glow(gx, gy, z - 0x5c, bgr, size, true);
	} else {
		engine_glow(gx, gy, z, bgr, size, false);
	}
}

// ---------------------------------------------------------------- attitude jets (0x40ac30)

void eng_fx_attitude_jet(const LanderJet *jet) {
	if (!cur_model || !cur_model->parent || !fxc.valid) return;
	fx_xform_t x;
	fx_xform_world(coord_world(cur_model->parent), &x); // the body node
	cvec3_t dir = {jet->dir[0], jet->dir[1], jet->dir[2]};
	cvec3_t o = cvec_scale(dir, jet->intensity < 1 ? -jet->size : jet->size);
	int16_t px = (int16_t)(jet->pos[0] + o.x), py = (int16_t)(jet->pos[1] + o.y), pz = (int16_t)(jet->pos[2] + o.z);
	if (jet->intensity > 0) py = (int16_t)(py + jet->liftY);
	vec3_t cam = fx_rt(&x, vec3(px, py, pz));
	float sx0, sy0, z0, sx1, sy1, z1;
	if (!fx_rtp(&x, vec3(px, py, pz), &sx0, &sy0, &z0)) return;
	int32_t k = jet->intensity >> 5;
	k = clamp(k, -0xb4, 0xb4);
	o = cvec_scale(dir, k);
	if (!fx_rtp(&x, vec3((int16_t)(px + o.x), (int16_t)(py + o.y), (int16_t)(pz + o.z)), &sx1, &sy1, &z1)) return;
	int32_t a0 = (int32_t)sx0, b0 = (int32_t)sy0, a1 = (int32_t)sx1, b1 = (int32_t)sy1;	int32_t hx = (b1 - b0) >> 2, hy = (a1 - a0) >> 2;
	float z = cam.z;
	if (z <= 1) return;
	// a triangle from the nozzle (blue) to a base a quarter of its length wide (black); additive
	fxv_t v[3] = {
		{fx_unproject((float)a0, (float)b0, z), 0, 0, 0xff6464c8u},
		{fx_unproject((float)(a1 + hx), (float)(b1 - hy), z), 0, 0, 0},
		{fx_unproject((float)(a1 - hx), (float)(b1 + hy), z), 0, 0, 0},
	};
	fx_queue(v, 3, NULL, FX_ADD, false, z);
}

// ---------------------------------------------------------------- tractor beam (0x40c030)

static vec3_t beam_point(const fx_xform_t *x, int16_t px, int16_t py, int16_t pz, bool *ok, float *z) {
	float sx, sy;
	if (!fx_rtp(x, vec3(px, py, pz), &sx, &sy, z) || ((int32_t)*z >> 2) << 2 == 0) {
		*ok = false;
		return vec3(0, 0, 0);
	}
	*z = (float)(((int32_t)*z >> 2) << 2);
	return fx_unproject((float)(int32_t)sx, (float)(int32_t)sy, *z);
}

// 0x40c030 draws in the GTE frame its caller set. len, radius, time (ms), mode: 0 a cone from an apex
// 0x50 under the origin down its -y axis, 2 the same in blue (lander beam locked on a target), 1 a
// tube from a ring at the origin (pod tether). The callers' frames:
//   lander (Lander_DrawCallback, orient NULL / the beam node with mode 2): the world rotation or the
//     beam node's, with the lander model's translation;
//   pod tether (0x44c980, modes 0 and 1): the full transform of the tether node passed as orient.
// The returned colour (hue x pulse, FUN_0044a720) is not used by any caller.
void eng_fx_tractor_beam(int32_t len, int32_t radius, int32_t time, int32_t mode, void *orient) {
	if (!fxc.valid) return;
	fx_xform_t x;
	if (orient && mode != 2) {
		fx_xform_world(coord_world(orient), &x);
	} else {
		if (!cur_model) return;
		fx_xform_t model;
		fx_xform_world(coord_world(cur_model), &model);
		FwMatrix rot;
		if (orient) rot = *coord_world(orient);
		else fw_identity(&rot);
		fx_xform_rot_only(&rot, &model, &x);
	}
	bool apex = mode != 1;   // (the original turns mode 2 into 0 after picking its colours)
	uint32_t ring = mode == 2 ? 0x4010ffu : 0xff401000u;
	uint32_t hue = fx_hue_colour((uint32_t)time, 0x1000, 0x80, 0x20, true);
	uint32_t base = fx_colour_pulse(hue, (uint32_t)time, 0x300);
	fxv_t v[49];
	int n = 0, a = 0;
	bool ok = true;
	float zd = 0;
	if (apex) { // apex just under the ship
		v[n++] = (fxv_t){beam_point(&x, 0, -0x50, 0, &ok, &zd), 0, 0, base};
		if (!ok) return;
	} else { // ring at the origin
		for (int i = 0; i < 16; i++, a += 0x100) {
			int16_t px = (int16_t)((fx_rsin(a) * radius) >> 12), pz = (int16_t)((fx_rcos(a) * radius) >> 12);
			v[n++] = (fxv_t){beam_point(&x, px, 0, pz, &ok, &zd), 0, 0, base};
			if (!ok) return;
		}
	}
	// pulsing ring at the far end (mode 1: three quarters along)
	int16_t y1 = (int16_t)(apex ? -len : (len >> 2) - len);
	uint32_t t = (uint32_t)(a + time);
	float zfirst = zd;
	for (int i = 0; i < 16; i++, a += 0x100, t += 0x100) {
		int16_t px = (int16_t)((fx_rsin(a) * radius) >> 12), pz = (int16_t)((fx_rcos(a) * radius) >> 12);
		float z;
		v[n++] = (fxv_t){beam_point(&x, px, y1, pz, &ok, &z), 0, 0, fx_colour_pulse(ring, t, 0x400)};
		if (!ok) return;
	}
	// black outer ring
	int16_t y2, r2 = (int16_t)radius;
	a = -0x80;
	if (apex) {
		r2 = (int16_t)((radius * 0x11b5) >> 12);
		y2 = (int16_t)((len * -0x11b5) >> 12);
	} else {
		y2 = (int16_t)-len;
	}
	for (int i = 0; i < 16; i++, a += 0x100) {
		int16_t px = (int16_t)((fx_rsin(a) * r2) >> 12), pz = (int16_t)((fx_rcos(a) * r2) >> 12);
		float z;
		v[n++] = (fxv_t){beam_point(&x, px, y2, pz, &ok, &z), 0, 0, 0xff000000u};
		if (!ok) return;
	}
	uint16_t idx[34];
	if (apex) { // fan from the apex, then a strip to the outer ring
		fxv_t fan[18];
		for (int i = 0; i < 17; i++) fan[i] = v[i];
		fan[17] = v[1];
		fx_queue_fan(fan, 18, NULL, FX_ADD, false, zfirst);
		for (int i = 0; i < 16; i++) { idx[i * 2] = (uint16_t)(i + 17); idx[i * 2 + 1] = (uint16_t)(i + 1); }
		idx[32] = 17;
		idx[33] = 1;
		fx_queue_strip(v, idx, 34, NULL, FX_ADD, false, zfirst);
	} else { // two strips: ship ring -> pulsing ring -> outer ring
		for (int i = 0; i < 16; i++) { idx[i * 2] = (uint16_t)(i + 16); idx[i * 2 + 1] = (uint16_t)i; }
		idx[32] = 16;
		idx[33] = 0;
		fx_queue_strip(v, idx, 34, NULL, FX_ADD, false, zfirst);
		for (int i = 0; i < 16; i++) { idx[i * 2] = (uint16_t)(i + 32); idx[i * 2 + 1] = (uint16_t)(i + 16); }
		idx[32] = 32;
		idx[33] = 16;
		fx_queue_strip(v, idx, 34, NULL, FX_ADD, false, zfirst);
	}
}

// 0x44ca20: the beam node's -y axis towards dir (straight down / up when dir is vertical)
void eng_fx_beam_orient(void *beam_coord, const int32_t dir[3]) {
	coord_t *c = beam_coord;
	if (!c) return;
	FwMatrix *m = &c->local;
	if (dir[0] == 0 && dir[2] == 0) {
		int32_t t[3] = {m->t[0], m->t[1], m->t[2]};
		fw_identity(m);
		memcpy(m->t, t, sizeof(t));
		if (dir[1] > 0) { // m = m * Rz(0x800)
			m->m[0][0] = -0x1000;
			m->m[1][1] = -0x1000;
		}
		return;
	}
	cvec3_t d = {dir[0], dir[1], dir[2]}, up = {0, 0x1000, 0};
	cvec_normalise(&d);
	cvec3_t c1 = cvec_cross(d, up);
	cvec_normalise(&c1);
	cvec3_t z = cvec_cross(d, c1);
	cam_view_t v;
	cam_view_set_axes(&v, z, d); // 0x431830: column 2 = z, column 1 = -d
	memcpy(m->m, v.mat.m, sizeof(m->m));
}

// ---------------------------------------------------------------- headlight cone (0x4569a0 / 0x4569d0)

int16_t eng_headlight_glow(int32_t level, int32_t cone, int32_t outer) { // 0x4569a0: the cone radius at level*64
	(void)outer;
	return (int16_t)(int32_t)(tan(cone * 0.0007669903939428206) * (double)(level << 6));
}

// The draw callback sets the headlight's node (Light +0xc): here the lander's headlight is the spot light
// attached to the body node of the lander being drawn.
void eng_fx_headlight_glow(int16_t glow, int32_t level, uint32_t rgb) {
	if (!cur_model) return;
	for (int i = 0; i < LANDER_MAX; i++) {
		dyn_light_t *l = gLanders[i].headlight;
		if (l && l->coord == cur_model->parent) {
			effects_light_cone(l->coord, l->pos, glow, level, rgb);
			return;
		}
	}
}

// a fan from the light (dim grey) to a ring of 8 points of radius glow at z = level*64 along the light's
// axis (black); additive, in the light's node frame
void effects_light_cone(coord_t *node, const int32_t pos[3], int16_t glow, int32_t level, uint32_t rgb) {
	if (!node || !fxc.valid) return;
	FwMatrix w = *coord_world(node);
	if (pos) { // the light's offset in its node
		int32_t o[3];
		fw_apply(&w, pos, o);
		w.t[0] += o[0]; w.t[1] += o[1]; w.t[2] += o[2];
	}
	fx_xform_t x;
	fx_xform_world(&w, &x);
	float sx, sy, z;
	if (!fx_rtp(&x, vec3(0, 0, 0), &sx, &sy, &z) || ((int32_t)z >> 2) == 0) return;
	z = (float)(((int32_t)z >> 2) << 2);
	uint32_t c = ((((rgb & 0xff0000) >> 8) | ((rgb & 0xff00) << 8) | (rgb << 24)) >> 11) & 0x1f1f1f;
	fxv_t v[10];
	v[0] = (fxv_t){fx_unproject((float)(int32_t)sx, (float)(int32_t)sy, z), 0, 0, 0xff000000u | c};
	int16_t zr = (int16_t)(level << 6);
	for (int i = 0, a = 0; i < 8; i++, a += 0x200) {
		int16_t px = (int16_t)((fx_rsin(a) * glow) >> 12), py = (int16_t)((fx_rcos(a) * glow) >> 12);
		float rz;
		if (!fx_rtp(&x, vec3(px, py, zr), &sx, &sy, &rz) || ((int32_t)rz >> 2) == 0) return;
		rz = (float)(((int32_t)rz >> 2) << 2);
		v[i + 1] = (fxv_t){fx_unproject((float)(int32_t)sx, (float)(int32_t)sy, rz), 0, 0, 0xff000000u};
	}
	v[9] = v[1];
	fx_queue_fan(v, 10, NULL, FX_ADD, false, FX_LAYER_3D);
}
