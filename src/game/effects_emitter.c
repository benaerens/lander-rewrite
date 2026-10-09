// FwParticles systems (0x430670-0x4325e0: 128 systems sharing 2048 particles, defined by the 0x50-byte
// records of gParticleEffectTable 0x498a08), the ParticleEmitter object (type 13, 0x44dea0-0x44e2e0)
// and the jagged lightning bolt (0x40b7e0) that emitters with effect -1, AmbientFX lightning and
// nothing else draw.
#include <stdlib.h>
#include <string.h>

#include "effects_int.h"

// ---------------------------------------------------------------- FwParticles

typedef fx_psys_def_t fwp_def_t;

// gParticleEffectTable 0x498a08 (the five effects ParticleEmitters use)
static fwp_def_t defs[5] = {
	{0x3c, 0, 0x20, 1, 0xb4, 0x808080, 10, 30, {0, 0, 0, 0}, {100, 25, 100, 0}, 5, 0, 0x3f, NULL, 0, 0},
	{0x3b, 0, 0x14, 0, 0x200, 0x323232, 0x12, 0x14, {0, 0x4e20, 0, 0}, {0, 0, 0, 0}, 0x8c, 0, 0x1af, NULL, 0, 0},
	{0x3b, 0, 0x0c, 2, 0x200, 0x808080, 0x2d, 0x41, {0, 0, 0x7fff, 0}, {0x40, 0x40, 0, 0}, 0x30, 0, 0x1bf, NULL, 0, 0},
	{0x3b, 0, 0x28, -4, 0x3c, 0x808080, 0x1e, 0x1e, {0, 0x7fff, 0, 0}, {0, 0, 0, 0}, 0x41, 0, 0xaf, NULL, 0, 0},
	{0x3c, 0, 0x14, 0, 0x200, 0x323232, 0x12, 0x14, {0, 0x4e20, 0, 0}, {0, 0, 0, 0}, 0x64, 0, 0xaf, NULL, 0, 0},
};

#define FWP_PARTS 0x800
#define FWP_SYSTEMS 0x80

static struct { int16_t p[3], life; } fwp_pos[FWP_PARTS];  // 0x606ac0
static struct { int16_t v[3], used; } fwp_vel[FWP_PARTS];  // 0x60aac0
static struct { fwp_def_t *def; int16_t first, count, alive, used; void *user; } fwp_sys[FWP_SYSTEMS]; // 0x60eac0 (+0xc user)

void fx_psys_register(fx_psys_def_t *d) { // FwParticlesRegisterDef 0x4325e0 (its bounding box only culls)
	d->life_range = (uint16_t)(d->life1 - d->life0);
	d->frame_k = (int16_t)(0x800 / (uint16_t)d->life1);
}

static void defs_register(void) { // FUN_0044e380
	for (int i = 0; i < (int)len(defs); i++) fx_psys_register(&defs[i]);
}

static void fwp_reset(void) { // FUN_00430670
	memset(fwp_pos, 0, sizeof(fwp_pos));
	memset(fwp_vel, 0, sizeof(fwp_vel));
	memset(fwp_sys, 0, sizeof(fwp_sys));
}

static void fwp_init_particle(int i, const fwp_def_t *d) { // FUN_00430810
	int32_t rx = (fx_rand() & 0x1fff) - 0x1000, ry = (fx_rand() & 0x1fff) - 0x1000, rz = (fx_rand() & 0x1fff) - 0x1000;
	fwp_pos[i].p[0] = (int16_t)((d->ext[0] * rx) >> 12);
	fwp_pos[i].p[1] = (int16_t)((d->ext[1] * ry) >> 12);
	fwp_pos[i].p[2] = (int16_t)((d->ext[2] * rz) >> 12);
	if (d->flags & 8) {
		cvec3_t v = {d->dir[0] + (fx_rand() & 0x1fff) - 0x1000, 0, 0};
		v.y = d->dir[1] + (fx_rand() & 0x1fff) - 0x1000;
		v.z = d->dir[2] + (fx_rand() & 0x1fff) - 0x1000;
		cvec_normalise(&v);
		fwp_vel[i].v[0] = (int16_t)((v.x * d->speed) >> 12);
		fwp_vel[i].v[1] = (int16_t)((d->speed * v.y) >> 12);
		fwp_vel[i].v[2] = (int16_t)((d->speed * v.z) >> 12);
	}
	if (d->life_range) {
		fwp_pos[i].life = (int16_t)(fx_rand() % d->life_range + d->life0);
	} else {
		fwp_pos[i].life = d->life0;
	}
}

static int fwp_create(fwp_def_t *d) { // FUN_00430930: best-fitting run of free particles
	int sys = 0;
	while (fwp_sys[sys].used) {
		if (++sys >= FWP_SYSTEMS) return -1;
	}
	int need = d->count, best = 0, start = 0, cursor = 0;
	for (int i = 0; i < FWP_PARTS;) {
		if (!fwp_vel[i].used) {
			int run = 1;
			while (i + run < FWP_PARTS && !fwp_vel[i + run].used) run++;
			if (best < need) {
				if (best < run) { best = run; start = cursor; }
			} else if (run < best && need <= run) {
				best = run;
				start = cursor;
			}
			cursor += run;
			i += run;
		}
		i++;
		cursor++;
	}
	if (!best) return -1;
	if (best > need) best = need;
	fwp_sys[sys].def = d;
	fwp_sys[sys].first = (int16_t)start;
	fwp_sys[sys].count = fwp_sys[sys].alive = (int16_t)best;
	fwp_sys[sys].used = 1;
	for (int k = 0; k < best; k++) {
		fwp_vel[start + k].used = 1;
		fwp_init_particle(start + k, d);
		if (d->flags & 0x80) fwp_pos[start + k].life = (int16_t)(fx_rand() % (uint16_t)d->life1);
	}
	return sys;
}

static void fwp_free(int sys) { // FUN_00430aa0
	fwp_sys[sys].used = 0;
	for (int k = 0; k < fwp_sys[sys].count; k++) fwp_vel[fwp_sys[sys].first + k].used = 0;
}

static void fwp_update(int sys) { // FUN_00430b00
	const fwp_def_t *d = fwp_sys[sys].def;
	int first = fwp_sys[sys].first, n = fwp_sys[sys].count;
	if (d->flags & 8) {
		for (int k = first; k < first + n; k++) {
			if (fwp_pos[k].life) {
				for (int a = 0; a < 3; a++) fwp_pos[k].p[a] = (int16_t)(fwp_pos[k].p[a] + fwp_vel[k].v[a]);
				fwp_vel[k].v[1] = (int16_t)(fwp_vel[k].v[1] + d->gravity);
			}
		}
	}
	for (int k = first; k < first + n; k++) {
		if (fwp_pos[k].life && --fwp_pos[k].life == 0) {
			if (!(d->flags & 4)) {
				if (--fwp_sys[sys].alive == 0 && d->done) {
					d->done(sys); // explosions: FUN_0044e530 ends the object
					return;
				}
			} else {
				fwp_init_particle(k, d);
			}
		}
	}
	if (d->flags & 0x10) {
		for (int k = first; k < first + n; k++) {
			if (fwp_pos[k].life) {
				for (int a = 0; a < 3; a++) fwp_pos[k].p[a] = (int16_t)(fwp_pos[k].p[a] + (fx_rand() & 0xf) - 8);
			}
		}
	}
	if (d->flags & 0x100) {
		for (int k = first; k < first + n; k++) {
			for (int a = 0; a < 3; a++) fwp_vel[k].v[a] = (int16_t)(fwp_vel[k].v[a] - (fwp_vel[k].v[a] >> 3));
		}
	}
}

// Particles_Draw 0x4321a0: additive camera-facing squares, frame (life * frame_k) >> 8 of a 4 x 2 sheet
static void fwp_draw(int sys, const fx_xform_t *x) {
	const fwp_def_t *d = fwp_sys[sys].def;
	if ((d->flags & 3) != 3) return;
	const texture_t *tex = fx_global_tex(d->tex);
	if (!tex) return;
	uint32_t c = 0xff000000u | d->colour;
	for (int k = fwp_sys[sys].first; k < fwp_sys[sys].first + fwp_sys[sys].count; k++) {
		if (!fwp_pos[k].life) continue;
		float sx, sy, z;
		fx_rtp(x, vec3(fwp_pos[k].p[0], fwp_pos[k].p[1], fwp_pos[k].p[2]), &sx, &sy, &z);
		if (z <= 10.0f || z >= fxc.fog_end * 1.25f) continue;
		float h = (d->size * fxc.h / z + 1.0f) * 0.5f;
		int f = (d->frame_k * fwp_pos[k].life) >> 8;
		int u0 = (f & 3) << 5, v0 = (f & 4) << 3;
		float tu0 = (float)u0 / tex->width, tu1 = (float)(u0 + 0x1f) / tex->width;
		float tv0 = (float)v0 / tex->height, tv1 = (float)(v0 + 0x1f) / tex->height;
		fxv_t q[4] = {
			{fx_unproject(sx - h, sy - h, z), tu0, tv0, c}, {fx_unproject(sx + h, sy - h, z), tu1, tv0, c},
			{fx_unproject(sx + h, sy + h, z), tu1, tv1, c}, {fx_unproject(sx - h, sy + h, z), tu0, tv1, c},
		};
		fxv_t t[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
		fx_queue(t, 6, tex, FX_ADD, true, FX_LAYER_3D);
	}
}

// ---------------------------------------------------------------- public FwParticles interface

int fx_psys_create(fx_psys_def_t *def, void *user) {
	int sys = fwp_create(def);
	if (sys >= 0) fwp_sys[sys].user = user;
	return sys;
}

void *fx_psys_user(int sys) { return sys >= 0 && sys < FWP_SYSTEMS ? fwp_sys[sys].user : NULL; }

void fx_psys_free(int sys) {
	if (sys >= 0 && sys < FWP_SYSTEMS && fwp_sys[sys].used) fwp_free(sys);
}

void fx_psys_update(int sys) {
	if (sys >= 0 && sys < FWP_SYSTEMS && fwp_sys[sys].used) fwp_update(sys);
}

void fx_psys_draw(int sys, const FwMatrix *world, int zone) {
	if (sys < 0 || sys >= FWP_SYSTEMS || !fwp_sys[sys].used || !fxc.valid) return;
	if (!fx_clip_zone(zone)) {
		fx_clip_reset();
		return;
	}
	fx_xform_t x;
	fx_xform_world(world, &x);
	fwp_draw(sys, &x);
	fx_clip_reset();
}

// ---------------------------------------------------------------- the lightning bolt (0x40b7e0)

static cvec3_t vscale(cvec3_t v, int32_t s) { return cvec_scale(v, s); }
static cvec3_t vadd(cvec3_t a, cvec3_t b) { return (cvec3_t){a.x + b.x, a.y + b.y, a.z + b.z}; }

int32_t fx_bolt(const fx_xform_t *x, vec3_t fa, vec3_t fb, uint32_t seed, int32_t age, uint32_t jitter) {
	cvec3_t a = {(int32_t)fa.x, (int32_t)fa.y, (int32_t)fa.z}, b = {(int32_t)fb.x, (int32_t)fb.y, (int32_t)fb.z};
	int32_t base = (int32_t)(jitter & 0xfff) - age * 4 + 0x800, inten = base;
	int32_t saved = fx_rand();
	fx_srand(seed);
	cvec3_t d = {b.x - a.x, b.y - a.y, b.z - a.z}, n = d;
	int32_t length = cvec_normalise(&n);
	cvec3_t w = {0x1000 - abs(n.x), 0x1000 - abs(n.y), 0x1000 - abs(n.z)};
	int segs = ((length + ((length >> 31) & 0xff)) >> 8) * 2;
	if (segs > 0x20) segs = 0x20;
	if (segs == 0) {
		fx_srand((uint32_t)saved);
		return 0;
	}
	if (segs < 4) segs = 4;
	int npts = segs + 1, half = npts >> 1;
	cvec3_t pt[33], prev = {0, 0, 0};
	int32_t level[33];
	cvec3_t drift = {(fx_rand() & 0x1fff) - 0x1000, 0, 0};
	drift.y = (fx_rand() & 0x1fff) - 0x1000;
	drift.z = (fx_rand() & 0x1fff) - 0x1000;
	int32_t stretch = age * 4 + 0x1000;
	for (int i = 0; i < npts; i++) {
		pt[i] = vadd(vscale(d, (i << 12) / segs), a);
		int32_t f = i < half ? ((half - i - 1) * 0x1000) / (half - 1) : ((i - half) * 0x1000) / (npts - half);
		if (i == 0) prev = pt[i];
		int32_t g = 0x1000 - f;
		prev = vscale(prev, g);
		pt[i] = vadd(vscale(pt[i], f), prev);
		int32_t l = ((inten * g) >> 12) + ((f * 0xa8c) >> 12);
		level[i] = l;
		if (g > 0x100) g = 0x100;
		if (g == 0) {
			prev = pt[i];
			inten = l;
		} else {
			inten = 0;
			level[i] = (fx_rand() & 0xfff) - 0x800 + l;
			int32_t hg = g >> 1;
			cvec3_t o;
			o.x = fx_rand() % g - hg;
			o.y = fx_rand() % g - hg;
			int32_t oz = fx_rand() % g - hg;
			o.x = (o.x * w.x) >> 12;
			o.y = (o.y * w.y) >> 12;
			o.z = (oz * w.z) >> 12;
			prev = vadd(o, pt[i]);
			pt[i] = vadd(o, pt[i]);
			drift.x += -0x400 + (fx_rand() & 0x7ff);
			drift.y += -0x400 + (fx_rand() & 0x7ff);
			int32_t rz = fx_rand();
			drift.x = (drift.x * w.x) >> 12;
			drift.y = (drift.y * w.y) >> 12;
			drift.z = ((drift.z - 0x400 + (rz & 0x7ff)) * w.z) >> 12;
			cvec_normalise(&drift);
			pt[i] = vadd(vscale(drift, (age * 4) >> 4), pt[i]);
		}
		int32_t v = base + (((level[i] - 0x800) * stretch) >> 12);
		level[i] = clamp(v, 0, 0x1000);
	}
	fx_srand((uint32_t)saved);
	// project; any point off screen or behind cancels the bolt
	float sx[33], sy[33], sz[33];
	for (int i = 0; i < npts; i++) {
		if (!fx_rtp(x, vec3((float)(int16_t)pt[i].x, (float)(int16_t)pt[i].y, (float)(int16_t)pt[i].z), &sx[i], &sy[i], &sz[i])) return 0;
		sz[i] = (float)(((int32_t)sz[i] >> 2) << 2);
		if (sz[i] <= 0) return 0;
	}
	// a ribbon five vertices wide across the screen direction: black, blue, white-blue, blue, black
	fxv_t v[33 * 5];
	float pdx = 0, pdy = 0;
	for (int i = 0; i < npts; i++) {
		float dx, dy;
		if (i == segs) { dx = sx[i] - sx[i - 1]; dy = sy[i] - sy[i - 1]; }
		else { dx = sx[i + 1] - sx[i]; dy = sy[i + 1] - sy[i]; }
		float ax = dx, ay = dy;
		if (i != 0) { ax = (pdx + dx) * 0.5f; ay = (pdy + dy) * 0.5f; }
		float wv = level[i] * ((float)((int32_t)fxc.h << 7) / sz[i]) * (1.0f / 4096.0f), ox = 0, oy = 0;
		float l2 = ax * ax + ay * ay;
		if (l2 > 0.1f) {
			float k = wv / sqrtf(l2);
			ox = k * ax;   // along x
			oy = k * ay;
		} else {
			oy = wv;       // the original keeps the width on one axis here
			ox = 0;
		}
		uint32_t cin = fx_colour_scale(0xffc8c8ffu, level[i]), cout = fx_colour_scale(0xff000028u, level[i]);
		float px[5] = {sx[i] + oy, sx[i] + oy * 0.5f, sx[i], sx[i] - oy * 0.5f, sx[i] - oy};
		float py[5] = {sy[i] - ox, sy[i] - ox * 0.5f, sy[i], sy[i] + ox * 0.5f, sy[i] + ox};
		uint32_t cc[5] = {0xff000000u, cout, cin, cout, 0xff000000u};
		for (int k = 0; k < 5; k++) v[i * 5 + k] = (fxv_t){fx_unproject(px[k], py[k], sz[i]), 0, 0, cc[k]};
		pdx = dx;
		pdy = dy;
	}
	for (int col = 0; col < 4; col++) {
		uint16_t idx[66];
		for (int i = 0; i < npts; i++) {
			idx[i * 2] = (uint16_t)(i * 5 + col + (col >= 2));
			idx[i * 2 + 1] = (uint16_t)(i * 5 + col + (col < 2));
		}
		fx_queue_strip(v, idx, npts * 2, NULL, FX_ADD, false, FX_LAYER_3D);
	}
	return base > 0x1000 ? 0x1000 : base < 1 ? 0 : base;
}

// ---------------------------------------------------------------- ParticleEmitter (type 13)

#define MAX_EMITTERS 512

typedef struct {
	bool used;
	int16_t zone;          // +0x0e clip zone (-3 = mob: none)
	int16_t dir[3];        // +0x10 effect -1: bolt end point
	uint8_t countdown;     // +0x16 ticks to the next bolt
	int8_t effect;         // +0x17
	uint32_t time;         // +0x18 bolt start
	uint32_t seed;         // +0x1c
	int sys;               // FwParticles system (effect >= 0)
	coord_t *coord;
	bool queued;
} emitter_t;

static emitter_t emitters[MAX_EMITTERS];

static emitter_t *em_of(task_t *t) { return objstate(t)->extra; }

// the record's zone after MapRec_ComputeZones 0x4453b0: 0 / -2 are located from the position; a set
// of several zones (stored as -5 - offset) is not clipped by the draw
int fx_record_zone(const map_rec_t *r) {
	const level_t *lv = G.level;
	if (r->zone == -3 || r->zone == -1 || r->zone > 0) return r->zone;
	if (!lv->zonedata || !lv->zonedata->num_zones) return -1;
	zone_set_t zs;
	zoneset_init(lv->zonedata, &zs, r->pos, -2);
	return zs.num_zones == 1 ? zs.zones[0] : -5;
}

static void emitter_create(task_t *t, void *arg) { // ParticleEmitter_Create 0x44dfc0
	obj_args_t *a = arg;
	objstate_init(t, a);
	emitter_t *e = NULL;
	for (int i = 0; i < MAX_EMITTERS && !e; i++) {
		if (!emitters[i].used) e = &emitters[i];
	}
	objstate(t)->extra = e;
	if (!e) return;
	memset(e, 0, sizeof(*e));
	e->used = true;
	e->sys = -1;
	e->zone = a && a->rec && (a->id & 0x3000000) != 0x1000000 ? (int16_t)fx_record_zone(a->rec) : -3;
	const int8_t *w = a && a->params ? (const int8_t *)a->params : NULL;
	e->effect = w ? w[0] : 0;
	e->coord = coord_alloc(NULL);
	if (e->coord && a && a->rec) {
		fw_from_packed(a->rec->rot, &e->coord->local);
		e->coord->local.t[0] = a->rec->pos.x;
		e->coord->local.t[1] = a->rec->pos.y;
		e->coord->local.t[2] = a->rec->pos.z;
	}
	objstate(t)->coord = e->coord;
	if (e->effect < 0) {
		if (e->effect == -1) {
			e->dir[0] = (int16_t)(w[1] << 4);
			e->dir[1] = (int16_t)(w[2] << 4);
			e->dir[2] = (int16_t)(w[3] << 4);
			if (!e->dir[0] && !e->dir[1] && !e->dir[2]) e->dir[1] = 0x1000;
			uint32_t r = (uint32_t)fx_rand() ^ fxc.now;
			e->seed = r << 16 ^ (uint32_t)fx_rand() ^ fxc.now;
			e->time = fxc.now;
			e->countdown = 5;
		}
	} else if (e->effect < (int)len(defs)) {
		e->sys = fwp_create(&defs[(int)e->effect]);
	}
}

static void emitter_destroy(task_t *t) { // 0x44e120
	emitter_t *e = em_of(t);
	if (!e) return;
	coord_free(e->coord);
	if (e->effect >= 0 && e->sys >= 0) fwp_free(e->sys);
	e->used = false;
	objstate(t)->extra = NULL;
	objstate(t)->coord = NULL;
}

static void emitter_update(task_t *t) { // ParticleEmitter_Update 0x44e170
	emitter_t *e = em_of(t);
	if (!e) return;
	if (e->effect >= 0) {
		if (e->sys >= 0) fwp_update(e->sys);
		return;
	}
	if (e->effect == -2) { // sparks along the local y axis, 2 per tick
		const FwMatrix *m = coord_world(e->coord);
		int32_t v[3] = {m->m[0][1] >> 6, m->m[1][1] >> 6, m->m[2][1] >> 6};
		fx_spark_burst(m->t, v, 2, 7);
	} else if (e->effect == -1 && --e->countdown == 0) { // a new bolt every 1-4 ticks
		uint32_t r = (uint32_t)fx_rand() ^ fxc.now;
		e->seed = r << 16 ^ (uint32_t)fx_rand() ^ fxc.now;
		e->time = fxc.now;
		e->countdown = (uint8_t)((fx_rand() & 3) + 1);
	}
}

static void emitter_render(task_t *t, uint32_t time) { // 0x44e240: queue the render item (0x44dea0)
	(void)time;
	emitter_t *e = em_of(t);
	if (!e || !fxc.valid) return;
	if (!fx_clip_zone(e->zone)) {
		fx_clip_reset();
		return;
	}
	fx_xform_t x;
	fx_xform_world(coord_world(e->coord), &x);
	if (e->effect == -1) {
		fx_bolt(&x, vec3(0, 0, 0), vec3(e->dir[0], e->dir[1], e->dir[2]), e->seed, (int32_t)(fxc.now - e->time),
			(uint32_t)fx_rand() & 0xfff);
	} else if (e->effect >= 0 && e->sys >= 0) {
		fwp_draw(e->sys, &x);
	}
	fx_clip_reset();
}

static intptr_t emitter_get_pos(ObjId id) { // FUN_004470e0: the record position
	map_rec_t *r = Object_GetRecord(id & 0x03ffffff);
	return r ? (intptr_t)&r->pos : 0;
}

// FUN_0044e3f0: aim a bolt at a point (part 0 of the id), or with a part number in bits 26-31 move the
// start while keeping the end where it was
static void emitter_set_pos(ObjId id, intptr_t v) {
	ObjId base = id & 0x03ffffff;
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(base, &params, &t);
	map_rec_t *r = Object_GetRecord(base);
	const int32_t *p = (const int32_t *)v;
	if (!r || !p) return;
	int32_t d[3] = {p[0] - r->pos.x, p[1] - r->pos.y, p[2] - r->pos.z};
	int8_t *w = (int8_t *)params;
	emitter_t *e = t && t->type ? em_of(t) : NULL;
	if (((id >> 26) & 0x3f) == 0) {
		if (w) { w[1] = (int8_t)(d[0] >> 4); w[2] = (int8_t)(d[1] >> 4); w[3] = (int8_t)(d[2] >> 4); }
		if (e) { e->dir[0] = (int16_t)d[0]; e->dir[1] = (int16_t)d[1]; e->dir[2] = (int16_t)d[2]; }
	} else {
		r->pos.x += d[0]; r->pos.y += d[1]; r->pos.z += d[2];
		if (w) {
			w[1] = (int8_t)(w[1] - (int8_t)(d[0] >> 4)); w[2] = (int8_t)(w[2] - (int8_t)(d[1] >> 4));
			w[3] = (int8_t)(w[3] - (int8_t)(d[2] >> 4));
		}
		if (e) {
			if (e->coord) { e->coord->local.t[0] = r->pos.x; e->coord->local.t[1] = r->pos.y; e->coord->local.t[2] = r->pos.z; }
			e->dir[0] = (int16_t)(e->dir[0] - d[0]); e->dir[1] = (int16_t)(e->dir[1] - d[1]); e->dir[2] = (int16_t)(e->dir[2] - d[2]);
		}
	}
}

void fx_emitter_register(void) { // ObjectTypes_RegisterAll: type 13, property 0 get/set only
	ObjectType_Register(OT_PARTICLEEMITTER, emitter_update, emitter_render, emitter_create, emitter_destroy, 0);
	ObjectProperty_RegisterGet(OT_PARTICLEEMITTER, 0, emitter_get_pos);
	ObjectProperty_RegisterSet(OT_PARTICLEEMITTER, 0, emitter_set_pos);
}

void fx_emitter_reset(void) {
	defs_register();
	fwp_reset();
	memset(emitters, 0, sizeof(emitters));
}
