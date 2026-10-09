// The sky: the fog-coloured horizon gradient (Sky_DrawGradient 0x44bcb0, from
// Environment_ApplyAmbientFog 0x40f9f0) and the AmbientFX object (type 21, 0x459810-0x45a6e0), whose byte
// 0 selects: 0 lightning strikes with a flash and thunder, 1 rain, 2 snow, 3 a scrolling cloud layer,
// 4 the starfield (Sky_DrawStars 0x44aa70), 5 a sky model (the record's model drawn around the camera
// behind everything). Stars and sky models are the sky layer (OT+8): drawn by effects_begin_frame
// before the world; the others go into the 3D layer with the other effects.
#include <stdlib.h>
#include <string.h>

#include "effects_int.h"
#include "sound.h"

// ---------------------------------------------------------------- state

#define MAX_AMB 32
#define RAIN_PARTS 500

typedef struct { int32_t p[3], on; } rain_part_t;   // 0x20 bytes in the original (+ screen x, y, z)

typedef struct {
	bool used;
	ObjId id;
	uint8_t kind;        // +0x14
	int32_t snd;         // +0x10 rain loop
	int16_t timer;       // +0x18 kind 0 tick counter; kinds 1/2/4: particle count (500)
	int16_t speed;       // +0x1a kind 1/2: 75..99; kind 0: flash state (+0x1a u8)
	int16_t flash;       // kind 0 +0x28
	uint32_t seed, time; // kind 0 bolt (+0x20, +0x24)
	coord_t *coord;      // the render item's node (strike point, cloud plane, origin)
	coord_t *prev;       // +0x24 kinds 1/2/4: node for the camera of 40 ms ago
	rain_part_t *parts;  // kinds 1/2
	float (*stars)[4];   // kind 4: 512 unit directions + clip flags
	int32_t height;      // kind 3: plane height (record y)
	int tex;             // kind 3: environment texture
	int16_t inst;        // kind 5: the record's model instance
	task_t *task;
} amb_t;

static amb_t ambs[MAX_AMB];
static cvec3_t wind;                    // DAT_00499240 (normalised at the first create)
static const cvec3_t wind0 = {-0xed8, -0xed8, -0x7d0};
static bool wind_ready;
static cvec3_t rain_step;               // DAT_0061e890: rain movement per tick
static cvec3_t rain_cur, rain_prev;     // DAT_0061e8b0 / DAT_0061e8a0: interpolated offsets
static uint8_t flash_rgb[3] = {0x96, 0x96, 0xff}; // DAT_00499238

static amb_t *amb_of(task_t *t) { return objstate(t)->extra; }

// the render item's draw function of a sky model (0x459db0) draws it in the sky layer, which this
// rewrite draws before the world (fx_sky_draw_background): the render list itself draws nothing
static void sky_inst_queued(model_inst_t *mi) { (void)mi; }

// the camera node as a PSX matrix (columns right, down, forward; t = eye) from the view of this frame
static void cam_world(FwMatrix *m) {
	if (!fxc.valid) { // before the first frame (objects woken by the mission start): world axes
		fw_identity(m);
		return;
	}
	const float *v = fxc.inv.m;
	for (int i = 0; i < 3; i++) {
		m->m[i][0] = (int16_t)(v[i] * 4096);
		m->m[i][1] = (int16_t)(-v[4 + i] * 4096);
		m->m[i][2] = (int16_t)(v[8 + i] * 4096);
	}
	m->t[0] = (int32_t)fxc.eye.x;
	m->t[1] = (int32_t)fxc.eye.y;
	m->t[2] = (int32_t)fxc.eye.z;
}

// ---------------------------------------------------------------- the object type

static void amb_create(task_t *t, void *arg) { // AmbientFX_Create 0x45a280
	obj_args_t *a = arg;
	objstate_init(t, a);
	amb_t *s = NULL;
	for (int i = 0; i < MAX_AMB && !s; i++) {
		if (!ambs[i].used) s = &ambs[i];
	}
	objstate(t)->extra = s;
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->used = true;
	s->task = t;
	s->id = a ? a->id : OBJ_NONE;
	s->inst = -1;
	const uint8_t *w = a && a->params ? (const uint8_t *)a->params : NULL;
	s->kind = w ? w[0] : 0;
	FwMatrix cam;
	cam_world(&cam);
	switch (s->kind) {
	case 0:
		s->timer = 0x19;
		s->speed = 0;
		s->coord = coord_alloc(NULL);
		break;
	case 1:
	case 2:
	case 4:
		s->timer = 500;
		s->speed = (int16_t)(fx_rand() % 0x19 + 0x4b);
		s->coord = coord_alloc(NULL);
		if (!wind_ready) { // FwVectorNormalise(&DAT_00499240) on every create: idempotent after the first
			wind = wind0;
			cvec_normalise(&wind);
			wind_ready = true;
		}
		s->prev = coord_alloc(NULL);
		if (s->kind == 4) { // FUN_0044a820: 512 random directions of the upper half
			s->stars = malloc(sizeof(float[4]) * 512);
			for (int i = 0; i < 512; i++) {
				float x = (float)((fx_rand() & 0x7ff) - 0x400), y = (float)(fx_rand() & 0x3ff);
				float z = (float)((fx_rand() & 0x7ff) - 0x400);
				float l = sqrtf(x * x + y * y + z * z);
				if (l > 0) { x /= l; y /= l; z /= l; }
				s->stars[i][0] = x; s->stars[i][1] = y; s->stars[i][2] = z; s->stars[i][3] = 0;
			}
		} else { // a 2048-unit cube of drops around the camera, along its axes
			s->parts = calloc(4000 / 8, sizeof(rain_part_t));
			for (int i = 0; i < RAIN_PARTS; i++) {
				int32_t *p = s->parts[i].p;
				memcpy(p, cam.t, sizeof(int32_t) * 3);
				int32_t r = (fx_rand() & 0x7ff) - 0x400;
				p[0] += (r * cam.m[0][2]) >> 12; p[1] += (r * cam.m[1][2]) >> 12; p[2] += (r * cam.m[2][2]) >> 12;
				r = (fx_rand() & 0x7ff) - 0x400;
				p[0] += (cam.m[0][0] * r) >> 12; p[1] += (r * cam.m[1][0]) >> 12; p[2] += (r * cam.m[2][0]) >> 12;
				r = (fx_rand() & 0x7ff) - 0x400;
				p[0] += (r * cam.m[0][1]) >> 12; p[1] += (r * cam.m[1][1]) >> 12; p[2] += (r * cam.m[2][1]) >> 12;
				s->parts[i].on = 0;
			}
		}
		break;
	case 3:
		s->coord = coord_alloc(NULL);
		if (s->coord && a && a->rec) {
			fw_from_packed(a->rec->rot, &s->coord->local);
			s->coord->local.t[0] = a->rec->pos.x;
			s->coord->local.t[1] = a->rec->pos.y;
			s->coord->local.t[2] = a->rec->pos.z;
		}
		s->height = a && a->rec ? a->rec->pos.y : 0;
		s->tex = w ? (int16_t)(w[2] | w[3] << 8) : 0;
		break;
	case 5:
		s->inst = a ? a->inst : -1;
		if (s->inst >= 0) gInstances[s->inst].draw = sky_inst_queued; // drawn by the sky layer instead
		break;
	}
}

static void amb_destroy(task_t *t) { // 0x45a5b0
	amb_t *s = amb_of(t);
	if (!s) return;
	if (s->snd) {
		eng_snd_stop(s->snd);
		s->snd = 0;
	}
	coord_free(s->coord);
	coord_free(s->prev);
	free(s->parts);
	free(s->stars);
	s->used = false;
	objstate(t)->extra = NULL;
}

static void amb_update(task_t *t) { // 0x459e90
	amb_t *s = amb_of(t);
	if (!s) return;
	if (s->kind == 0) {
		if ((s->speed & 0xff) == 1) { // flashing: the exterior colour brightened at random
			uint32_t r = (uint32_t)fx_rand() & 0xfff;
			s->flash = (int16_t)r;
			const rgba_t ex = G.level->mission->exterior;
			uint32_t k = (r * 0x96) >> 12;
			flash_rgb[0] = (uint8_t)min(ex.r + k, 0xffu);
			flash_rgb[1] = (uint8_t)min(ex.g + k, 0xffu);
			flash_rgb[2] = (uint8_t)min(ex.b + ((r * 0xff) >> 12), 0xffu);
		}
		if (--s->timer != 0) return;
		if ((s->speed & 0xff) == 0) {
			if (gOutsideFactor == 0) {
				s->timer = 0x32;
				return;
			}
			// a strike on the terrain somewhere around the point 4096 ahead of the camera
			FwMatrix cam;
			cam_world(&cam);
			int32_t x = cam.t[0] + cam.m[0][2], z = cam.t[2] + cam.m[2][2];
			x += (fx_rand() & 0x1fff) - 0x1000;
			z += (fx_rand() & 0x1fff) - 0x1000;
			int32_t a[3] = {x, cam.t[1] + 0x2000, z}, b[3] = {x, cam.t[1] - 0x2000, z};
			phys_hit_t hit;
			int old = G.world.query_mask;
			G.world.query_mask = 2;
			int found = phys_segment_hit(a, b, 2, &hit);
			G.world.query_mask = old;
			if (!found) {
				s->timer = 10;
				return;
			}
			Sound_PlayOnce(OBJ_NONE, 0xdc);
			if (s->coord) {
				s->coord->local.t[0] = x;
				s->coord->local.t[1] = hit.point[1];
				s->coord->local.t[2] = z;
			}
			s->speed = 1;
			s->timer = 0x32;
			uint32_t r = (uint32_t)fx_rand() ^ fxc.now;
			s->seed = r << 16 ^ (uint32_t)fx_rand() ^ fxc.now;
			s->time = fxc.now;
		} else if (--s->speed == 0) {
			s->timer = (int16_t)(fx_rand() % 0x7d + 0x32);
		}
		return;
	}
	if (s->kind == 1 || s->kind == 2) {
		if (((gOutsideFactor * 0x7d) & 0x3ffffc00) == 0) { // under a roof: silence
			if (s->snd) {
				eng_snd_stop(s->snd);
				s->snd = 0;
			}
			return;
		}
		uint32_t u = (uint16_t)s->speed;
		if (s->kind == 1) {
			rain_step = cvec_scale(wind, (int32_t)u);
			for (int i = 0; i < RAIN_PARTS; i++) {
				s->parts[i].p[0] += rain_step.x; s->parts[i].p[1] += rain_step.y; s->parts[i].p[2] += rain_step.z;
			}
		} else { // snow: a different drift per flake
			for (int i = 0; i < RAIN_PARTS; i++) {
				uint32_t k = u & 3;
				int32_t *p = s->parts[i].p;
				p[1] += (int32_t)(((u & 0xf) + 4) * wind.y) >> 12;
				if (k) p[0] += (int32_t)((k + 2) * wind.x) >> 12;
				if (u & 0xc) p[2] += (int32_t)((k + 2) * wind.z) >> 12;
				u = ((u >> 1) ^ u) + 3;
			}
		}
		if (!s->snd) s->snd = eng_snd_loop(OBJ_NONE, 0xde, -1);
	}
}

static void amb_render(task_t *t, uint32_t time) { // 0x45a1f0
	amb_t *s = amb_of(t);
	if (!s) return;
	int32_t period = (int32_t)t->type->period;
	int32_t frac = ((int32_t)(time - t->wake) + period) * 0x1000 / period;
	if (s->kind == 0) {
		if ((s->speed & 0xff) == 1) { // DAT_0049bf20: the exterior colour of the next environment update
			fx_flash = rgba(flash_rgb[0], flash_rgb[1], flash_rgb[2], 255);
			fx_flash_pending = true;
			level_exterior_override = &fx_flash;
		}
	} else if (s->kind == 1) {
		rain_cur = cvec_scale(rain_step, frac);
		rain_prev = cvec_scale(rain_step, frac - 0x1000);
	}
}

void fx_sky_register(void) {
	ObjectType_Register(OT_AMBIENTFX, amb_update, amb_render, amb_create, amb_destroy, 0);
}

void fx_sky_reset(void) {
	memset(ambs, 0, sizeof(ambs));
	wind_ready = false;
	rain_step = rain_cur = rain_prev = (cvec3_t){0, 0, 0};
}

// ---------------------------------------------------------------- the gradient (0x44bcb0)

// colour of a screen point: the fog colour below the horizon, darkening towards the zenith
static uint32_t sky_colour(float sx, float sy, rgba_t fog) {
	vec3_t d = vec3((sx - fxc.cx), (sy - fxc.cy), fxc.h);
	d = vec3_normalize(d);
	vec3_t w = vec3_sub(fx_from_cam(d), fxc.eye);
	uint32_t c = (uint32_t)fog.r << 16 | (uint32_t)fog.g << 8 | fog.b;
	if (w.y > 0.001f) c = fx_colour_scale(c, (int32_t)((1.0f - w.y) * 4096.0f));
	return 0xff000000u | c;
}

static void draw_gradient(void) {
	rgba_t fog = fxc.fog;
	if (!fog.r && !fog.g && !fog.b) return; // black fog: plain clear
	// the screen rectangle with the horizon's crossings of its edges, as one fan
	float W = (float)fxc.w, H = (float)fxc.ht;
	float cx[4] = {0, W, W, 0}, cy[4] = {0, 0, H, H};
	vec3_t up = vec3_sub(fx_to_cam(vec3_add(fxc.eye, vec3(0, 1, 0))), fx_to_cam(fxc.eye));
	float hv[4];
	for (int i = 0; i < 4; i++) hv[i] = up.x * (cx[i] - fxc.cx) + up.y * (cy[i] - fxc.cy) + up.z * fxc.h;
	float px[8], py[8];
	int n = 0;
	for (int i = 0; i < 4; i++) {
		int j = (i + 1) & 3;
		px[n] = cx[i]; py[n++] = cy[i];
		if ((hv[i] > 0) != (hv[j] > 0) && hv[i] != hv[j]) {
			float f = hv[i] / (hv[i] - hv[j]);
			px[n] = cx[i] + (cx[j] - cx[i]) * f;
			py[n++] = cy[i] + (cy[j] - cy[i]) * f;
		}
	}
	fxv_t v[8];
	for (int i = 0; i < n; i++) {
		v[i] = (fxv_t){fx_unproject(px[i], py[i], 20.0f), 0, 0, sky_colour(px[i], py[i], fog)};
	}
	render_set_model(mat4_identity());
	render_set_depth(false, false);
	render_set_cull(CULL_NONE);
	render_set_fog(false, fog, 0, 1);
	static vertex_t buf[3 * 8];
	int k = 0;
	for (int i = 1; i + 1 < n; i++) {
		const fxv_t *t[3] = {&v[0], &v[i], &v[i + 1]};
		for (int j = 0; j < 3; j++) {
			buf[k].pos = t[j]->pos;
			buf[k].u = buf[k].v = 0;
			uint32_t c = t[j]->c;
			buf[k++].color = rgba((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
		}
	}
	render_tris(buf, k, NULL, BLEND_OPAQUE);
	render_set_depth(true, true);
	render_set_cull(CULL_BACK);
	render_set_fog(true, fxc.fog, fxc.fog_start, fxc.fog_end);
}

// ---------------------------------------------------------------- stars (0x44aa70)

// FUN_0044a8a0: screen position of a direction; flags 0x10 = not drawn, 1/4 left/right, 2/8 top/bottom
static int star_project(const float *m, const float *d, float *sx, float *sy) {
	float z = m[6] * d[0] + m[7] * d[1] + m[8] * d[2];
	if (z < 0.1f) return 0x10;
	float k = fxc.h / z;
	int f = 0;
	*sx = fxc.cx + k * (m[0] * d[0] + m[1] * d[1] + m[2] * d[2]);
	if (*sx < 0) {
		if (*sx < -128) return 0x10;
		f = 1;
	}
	if (*sx > fxc.w) {
		if (*sx > fxc.w + 128) return 0x10;
		f = 4;
	}
	*sy = fxc.cy + k * (m[3] * d[0] + m[4] * d[1] + m[5] * d[2]);
	if (*sy < 0) {
		if (*sy < -128) return 0x10;
		f |= 2;
	}
	if (*sy > fxc.ht) {
		if (*sy > fxc.ht + 128) return 0x10;
		f |= 8;
	}
	return f;
}

static void view_rows(mat4_t v, float m[9]) { // world -> PSX camera rotation rows
	for (int c = 0; c < 3; c++) {
		m[c] = v.m[c * 4 + 0];
		m[3 + c] = -v.m[c * 4 + 1];
		m[6 + c] = v.m[c * 4 + 2];
	}
}

// a white point, or a line to where the camera of 40 ms ago saw the star, fainter the longer it is
static void draw_stars(amb_t *s) {
	float cur[9], prev[9];
	view_rows(fxc.view, cur);
	view_rows(fx_view_at(fxc.now - 0x28), prev);
	static fxv_t pts[512 * 6];
	int n = 0;
	for (int i = 0; i < 512; i++) {
		float sx, sy, px, py;
		int f0 = star_project(cur, s->stars[i], &sx, &sy);
		s->stars[i][3] = (float)f0;
		if (f0 & 0x10) continue;
		int f1 = star_project(prev, s->stars[i], &px, &py);
		if ((f1 & 0x10) || (f0 & f1)) continue;
		float l = fabsf(sx - px) + fabsf(sy - py);
		const float z = 8192.0f; // rhw 0.00122: at the back
		if (l < 1.0f) {
			vec3_t a = fx_unproject(sx, sy, z), b = fx_unproject(sx + 1, sy, z);
			vec3_t c = fx_unproject(sx + 1, sy + 1, z), d = fx_unproject(sx, sy + 1, z);
			fxv_t q[6] = {{a, 0, 0, 0xffffffffu}, {b, 0, 0, 0xffffffffu}, {c, 0, 0, 0xffffffffu},
				{a, 0, 0, 0xffffffffu}, {c, 0, 0, 0xffffffffu}, {d, 0, 0, 0xffffffffu}};
			memcpy(&pts[n], q, sizeof(q));
			n += 6;
		} else {
			uint32_t c = (uint32_t)((int32_t)(255.0f / l)) << 24 | 0xffffffu;
			float dx = px - sx, dy = py - sy, nx = -dy / l * 0.5f, ny = dx / l * 0.5f;
			vec3_t a = fx_unproject(sx - nx, sy - ny, z), b = fx_unproject(sx + nx, sy + ny, z);
			vec3_t c2 = fx_unproject(px + nx, py + ny, z), d = fx_unproject(px - nx, py - ny, z);
			fxv_t q[6] = {{a, 0, 0, c}, {b, 0, 0, c}, {c2, 0, 0, c}, {a, 0, 0, c}, {c2, 0, 0, c}, {d, 0, 0, c}};
			memcpy(&pts[n], q, sizeof(q));
			n += 6;
		}
	}
	if (n) {
		render_set_model(mat4_identity());
		render_set_depth(false, false);
		render_set_cull(CULL_NONE);
		render_set_fog(false, fxc.fog, 0, 1);
		static vertex_t buf[512 * 6];
		for (int i = 0; i < n; i++) {
			buf[i].pos = pts[i].pos;
			buf[i].u = buf[i].v = 0;
			uint32_t c = pts[i].c;
			buf[i].color = rgba((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, c >> 24);
		}
		render_tris(buf, n, NULL, BLEND_ALPHA);
		render_set_depth(true, true);
		render_set_cull(CULL_BACK);
		render_set_fog(true, fxc.fog, fxc.fog_start, fxc.fog_end);
	}
}

// ---------------------------------------------------------------- sky models (0x459db0)

static void draw_sky_model(amb_t *s) {
	if (s->inst < 0 || !gInstances[s->inst].used) return;
	const level_t *lv = G.level;
	if (lv->zonedata && lv->zonedata->num_zones && !vis_find(&lv->vis, -1)) return; // exterior not visible
	const vis_record_t *r = lv->zonedata && lv->zonedata->num_zones ? vis_find(&lv->vis, -1) : NULL;
	if (r) render_set_scissor(r->x0, r->y0, r->x1, r->y1);
	for (int16_t i = s->inst; i >= 0; i = gInstances[i].next_part) {
		model_inst_t *mi = &gInstances[i];
		if (!mi->model) continue;
		coord_t *c = mi->coord;
		while (c->parent) c = c->parent;
		c->local.t[0] = (int32_t)fxc.eye.x; // the node follows the camera (DAT_004e9198)
		c->local.t[1] = (int32_t)fxc.eye.y;
		c->local.t[2] = (int32_t)fxc.eye.z;
		mat4_t t = fw_to_mat4(coord_world(mi->coord));
		render_set_depth(false, false);
		render_set_fog(false, fxc.fog, 0, 1);
		model_draw(mi->model, t, mi->tex, &G.base_light);
	}
	render_set_depth(true, true);
	render_set_fog(true, fxc.fog, fxc.fog_start, fxc.fog_end);
	render_set_model(mat4_identity());
	render_reset_scissor();
}

// ---------------------------------------------------------------- rain and snow (0x459b50 -> 0x44acd0)

static void draw_rain(amb_t *s) {
	int32_t count = (gOutsideFactor * 500) >> 12;
	if (!count || !s->parts) return;
	FwMatrix cam;
	cam_world(&cam);
	int32_t c[3] = {cam.t[0] + ((cam.m[0][2] << 10) >> 12), cam.t[1] + ((cam.m[1][2] << 10) >> 12),
		cam.t[2] + ((cam.m[2][2] << 10) >> 12)};
	int n = 0;
	for (int i = 0; i < RAIN_PARTS; i++) { // wrap into the 2048 cube in front of the camera
		rain_part_t *p = &s->parts[i];
		if (n < count) {
			p->on = 1;
			for (int a = 0; a < 3; a++) {
				int32_t d = p->p[a] - c[a], w = (int32_t)(((uint32_t)(d - 0x400) & 0x7ff)) - 0x400;
				if (d != w) p->p[a] = w + c[a];
			}
			n++;
		} else {
			p->on = 0;
		}
	}
	mat4_t prev = fx_view_at(fxc.now - 0x28);
	for (int i = 0; i < RAIN_PARTS; i++) {
		rain_part_t *p = &s->parts[i];
		if (!p->on) continue;
		if (s->kind == 2) { // snow: additive grey squares, 3 pixels near, none at 3072
			vec3_t cam3 = fx_to_cam(vec3((float)p->p[0], (float)p->p[1], (float)p->p[2]));
			int32_t z = ((int32_t)cam3.z >> 2) * 4;
			if (cam3.z <= 0 || z <= 10) continue;
			float sx = (float)(int16_t)(fxc.cx + cam3.x * fxc.h / cam3.z), sy = (float)(int16_t)(fxc.cy + cam3.y * fxc.h / cam3.z);
			float sz = 3.0f - (float)(z + z) * (1.0f / 2048.0f);
			if (sz <= 0) continue; // the original's quad turns inside out (culled) beyond 3072
			fxv_t q[4] = {
				{fx_unproject(sx, sy, (float)z), 0, 0, 0x00646464}, {fx_unproject(sx + sz, sy, (float)z), 0, 0, 0x00646464},
				{fx_unproject(sx + sz, sy + sz, (float)z), 0, 0, 0x00646464}, {fx_unproject(sx, sy + sz, (float)z), 0, 0, 0x00646464},
			};
			fxv_t t[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
			fx_queue(t, 6, NULL, FX_ADD, false, FX_LAYER_3D);
		} else { // rain: a line from the drop of one tick ago (camera of 40 ms ago, clear) to now (half grey)
			vec3_t a = vec3((float)(p->p[0] + rain_cur.x), (float)(p->p[1] + rain_cur.y), (float)(p->p[2] + rain_cur.z));
			vec3_t ca = fx_to_cam(a);
			int32_t za = ((int32_t)ca.z >> 2) * 4;
			if (ca.z <= 0 || za == 0) continue;
			vec3_t b = vec3((float)(p->p[0] + rain_prev.x), (float)(p->p[1] + rain_prev.y), (float)(p->p[2] + rain_prev.z));
			vec4_t pb = mat4_mul_vec4(prev, (vec4_t){b.x, b.y, b.z, 1});
			int32_t zb = ((int32_t)pb.z >> 2) * 4;
			if (pb.z <= 0 || zb == 0) continue;
			vec3_t wa = fx_unproject(fxc.cx + ca.x * fxc.h / ca.z, fxc.cy + ca.y * fxc.h / ca.z, (float)za);
			vec3_t wb = fx_unproject(fxc.cx + pb.x * fxc.h / pb.z, fxc.cy - pb.y * fxc.h / pb.z, (float)zb);
			fx_queue_line(wb, wa, 0x00646464, 0x80646464u, FX_ALPHA, false, FX_LAYER_3D);
		}
	}
}

// ---------------------------------------------------------------- cloud layer (0x459d20 -> 0x44b5d0)

static void draw_clouds(amb_t *s) {
	const texture_t *tex = fx_world_tex(s->tex);
	if (!tex || !s->coord) return;
	const level_t *lv = G.level;
	const vis_record_t *r = NULL;
	if (lv->zonedata && lv->zonedata->num_zones) {
		r = vis_find(&lv->vis, -1);
		if (!r) return;
	}
	float far = lv->view_distance * 1.25f; // DAT_00577c84
	float dy = (float)s->height - fxc.eye.y;
	if (fabsf(dy) > far) return;
	// scroll: the record's x / z are the speeds (units per 1024 ms)
	uint32_t su = (uint32_t)(s->coord->local.t[0] * (int32_t)fxc.now) >> 10;
	uint32_t sv = (uint32_t)(s->coord->local.t[2] * (int32_t)fxc.now) >> 10;
	float ex = fxc.eye.x, ez = fxc.eye.z, h = (float)s->height;
	vec3_t p[4] = {vec3(ex - far, h, ez - far), vec3(ex + far, h, ez - far), vec3(ex + far, h, ez + far), vec3(ex - far, h, ez + far)};
	fxv_t q[4];
	for (int i = 0; i < 4; i++) { // FUN_0044c450: 8192 units per repeat
		q[i] = (fxv_t){p[i], ((float)(su & 0x3fff) + p[i].x) * (1.0f / 8192.0f), ((float)(sv & 0x3fff) + p[i].z) * (1.0f / 8192.0f),
			0x20ffffffu};
	}
	if (r) fx_clip_zone(-1);
	fxv_t t[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
	fx_wrap = true; // render states {TEXTUREADDRESS WRAP} before the draw, {CLAMP} after
	fx_queue(t, 6, tex, FX_MODULATE, true, FX_LAYER_3D);
	fx_wrap = false;
	fx_clip_reset();
}

// ---------------------------------------------------------------- per frame

void fx_sky_draw_background(void) {
	draw_gradient();
	for (int i = 0; i < MAX_AMB; i++) {
		amb_t *s = &ambs[i];
		if (!s->used) continue;
		if (s->kind == 4 && s->stars && ((gOutsideFactor * 500) >> 12)) draw_stars(s);
		else if (s->kind == 5) draw_sky_model(s);
	}
}

void fx_sky_draw(void) {
	for (int i = 0; i < MAX_AMB; i++) {
		amb_t *s = &ambs[i];
		if (!s->used) continue;
		switch (s->kind) {
		case 0:
			if ((s->speed & 0xff) && s->coord) { // FUN_00459aa0: a bolt from 0x2000 above down to the strike
				fx_xform_t x;
				fx_xform_world(coord_world(s->coord), &x);
				fx_bolt(&x, vec3(0, 0x2000, 0), vec3(0, 0, 0), s->seed, (int32_t)(fxc.now - s->time), (uint16_t)s->flash);
			}
			break;
		case 1:
		case 2:
			draw_rain(s);
			break;
		case 3:
			draw_clouds(s);
			break;
		}
	}
}
