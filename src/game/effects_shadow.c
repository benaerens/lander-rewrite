// The local player's projected shadow (Shadow_Create 0x41d810, Shadow_Destroy 0x41d970, Shadow_Render
// 0x41d9f0). The original renders the lander model's silhouette into a 128x128 texture as seen along
// the shadow light (FUN_0041e810 + the software triangle fill 0x41ecd0: 1/8 unit per texel, +-512
// units around the ship), then draws every zone / tile collision polygon in a volume along the light
// with that texture projected on it (FUN_0041fe00 / 0x41f0c0): white vertices whose alpha is
// facing x strength (at most 0x80), alpha-modulated (blend mode 3), depth-sorted, pulled towards the
// camera by rhw + 0.0002.
//
// The shadow light: the first directional light whose ray from the ship (0x2000 units) hits the
// ground; the strength fades over 0x1000..0x2000 units of that distance (FUN_0041fcf0) and scales with
// the light's red intensity. Inside, Lander_Render adds a white light straight down for it. Point-light
// shadows (lights with flag 0x20, perspective shadow camera FUN_0041e040) need a light flag nothing in
// the data or code sets; they are left out.
#include <stdlib.h>
#include <string.h>

#include "effects_int.h"

#define SH_SIZE 128

static bool shadow_on;                    // DAT_004a6d94
static uint32_t mask_px[SH_SIZE * SH_SIZE];
static texture_t mask = {.width = SH_SIZE, .height = SH_SIZE, .pixels = mask_px};

void eng_shadow_create(void) { shadow_on = true; }   // the D3D surfaces of 0x41d810
void eng_shadow_destroy(void) { shadow_on = false; }

static int32_t dot12(const int32_t *a, const int32_t *b) { // FwVectorDotProduct
	return (int32_t)(((int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] + (int64_t)a[2] * b[2]) >> 12);
}

static void cross12(const int32_t *a, const int32_t *b, int32_t *out) { // FwVectorCrossProduct
	int32_t r[3] = {(int32_t)(((int64_t)a[1] * b[2] - (int64_t)a[2] * b[1]) >> 12),
		(int32_t)(((int64_t)a[2] * b[0] - (int64_t)a[0] * b[2]) >> 12),
		(int32_t)(((int64_t)a[0] * b[1] - (int64_t)a[1] * b[0]) >> 12)};
	memcpy(out, r, sizeof(r));
}

// FUN_0041ecd0: fill a triangle of texel coordinates (opaque black = DAT_004a6d54)
static void fill_tri(const float *x, const float *y) {
	float ymin = min(y[0], min(y[1], y[2])), ymax = max(y[0], max(y[1], y[2]));
	int y0 = max(0, (int)ceilf(ymin - 0.5f)), y1 = min(SH_SIZE - 1, (int)ceilf(ymax - 0.5f) - 1);
	for (int py = y0; py <= y1; py++) {
		float yc = py + 0.5f, lo = 1e30f, hi = -1e30f;
		for (int e = 0; e < 3; e++) {
			int f = (e + 1) % 3;
			if ((y[e] <= yc) == (y[f] <= yc)) continue;
			float xc = x[e] + (yc - y[e]) * (x[f] - x[e]) / (y[f] - y[e]);
			lo = min(lo, xc);
			hi = max(hi, xc);
		}
		int x0 = max(0, (int)ceilf(lo - 0.5f)), x1 = min(SH_SIZE - 1, (int)ceilf(hi - 0.5f) - 1);
		for (int px = x0; px <= x1; px++) mask_px[py * SH_SIZE + px] = 0xff000000u;
	}
}

typedef struct {
	vec3_t org;            // the ship's position (DAT_004a6d58 + 0x14)
	int32_t a[3], b[3];    // texture axes (DAT_0049d718 / DAT_0049d728), 4.12, not normalised
	int32_t dir[3];        // light direction (DAT_0049d738)
	int32_t strength;      // DAT_0049d70c
	vec3_t vmin, vmax;     // receiver volume (DAT_00493388)
} shadow_t;

static void texel(const shadow_t *s, vec3_t p, float *u, float *v) {
	int32_t r[3] = {(int32_t)(p.x - s->org.x), (int32_t)(p.y - s->org.y), (int32_t)(p.z - s->org.z)};
	*u = (float)((dot12(r, s->a) >> 3) + 0x40);
	*v = (float)((dot12(r, s->b) >> 3) + 0x40);
}

typedef struct { vec3_t p; float u, v; } svert_t;

// clamp addressing: clip the receiver to the texture square (the rewrite's sampler repeats)
static float uv_dist(const svert_t *a, int plane) {
	return plane == 0 ? a->u : plane == 1 ? SH_SIZE - a->u : plane == 2 ? a->v : SH_SIZE - a->v;
}

static int clip_uv(const svert_t *in, int n, svert_t *out) {
	svert_t buf[2][16];
	memcpy(buf[0], in, sizeof(svert_t) * n);
	int cur = 0;
	for (int plane = 0; plane < 4 && n >= 3; plane++) {
		const svert_t *src = buf[cur];
		svert_t *dst = buf[cur ^ 1];
		int m = 0;
		for (int i = 0; i < n && m < 14; i++) {
			const svert_t *a = &src[i], *b = &src[(i + 1) % n];
			float da = uv_dist(a, plane), db = uv_dist(b, plane);
			if (da >= 0) dst[m++] = *a;
			if ((da >= 0) != (db >= 0)) {
				float t = da / (da - db);
				dst[m].p = vec3_add(a->p, vec3_mulf(vec3_sub(b->p, a->p), t));
				dst[m].u = a->u + (b->u - a->u) * t;
				dst[m++].v = a->v + (b->v - a->v) * t;
			}
		}
		n = m;
		cur ^= 1;
	}
	if (n >= 3) memcpy(out, buf[cur], sizeof(svert_t) * n);
	return n;
}

// one receiver triangle (world vertices, outward normal)
static void receiver(const shadow_t *s, const vec3_t *w, vec3_t n) {
	svert_t v[3];
	int out_all = 0xf;
	for (int i = 0; i < 3; i++) {
		v[i].p = w[i];
		texel(s, w[i], &v[i].u, &v[i].v);
		int c = (v[i].u < 0 ? 8 : 0) | (v[i].u > SH_SIZE ? 2 : 0) | (v[i].v < 0 ? 1 : 0) | (v[i].v > SH_SIZE ? 4 : 0);
		out_all &= c;
	}
	if (out_all) return;
	int32_t ni[3] = {(int32_t)(n.x * 4096), (int32_t)(n.y * 4096), (int32_t)(n.z * 4096)};
	int32_t f = -dot12(s->dir, ni);
	if (f < 0) return;                  // facing away from the light
	f >>= 4;
	if (f > 0x80) f = 0x80;
	uint32_t c = (uint32_t)(f * s->strength) * 0x20000u | 0xffffffu;
	svert_t poly[16];
	int k = clip_uv(v, 3, poly);
	if (k < 3) return;
	fxv_t fv[16];
	for (int i = 0; i < k; i++) {
		// rhw + 0.0002: z' = z / (1 + 0.00002 z) towards the eye
		vec3_t d = vec3_sub(poly[i].p, fxc.eye);
		float z = fx_to_cam(poly[i].p).z;
		float scale = z > 0 ? 1.0f / (1.0f + 0.00002f * z) : 1.0f;
		fv[i] = (fxv_t){vec3_add(fxc.eye, vec3_mulf(d, scale)), poly[i].u / SH_SIZE, poly[i].v / SH_SIZE, c};
	}
	fx_queue_fan(fv, k, &mask, FX_MODULATE, true, fx_to_cam(s->org).z);
}

static bool box_overlap(vec3_t a0, vec3_t a1, vec3_t b0, vec3_t b1) {
	return a0.x <= b1.x && a1.x >= b0.x && a0.y <= b1.y && a1.y >= b0.y && a0.z <= b1.z && a1.z >= b0.z;
}

static void receivers_of(const shadow_t *s, const model_t *m, mat4_t t) {
	vec3_t bmin = mat4_mul_point(t, m->bbox_min), bmax = mat4_mul_point(t, m->bbox_max);
	vec3_t lo = vec3(min(bmin.x, bmax.x), min(bmin.y, bmax.y), min(bmin.z, bmax.z));
	vec3_t hi = vec3(max(bmin.x, bmax.x), max(bmin.y, bmax.y), max(bmin.z, bmax.z));
	if (!box_overlap(lo, hi, s->vmin, s->vmax)) return;
	mat4_t rot = t;
	rot.m[12] = rot.m[13] = rot.m[14] = 0;
	for (int i = 0; i < m->num_tris; i++) {
		const pcp_tri_t *tr = &m->tris[i];
		if (tr->flags & TRI_INVISIBLE) continue;
		vec3_t w[3], n = vec3(0, 0, 0);
		bool in = false;
		for (int k = 0; k < 3; k++) {
			const pcp_wedge_t *wd = &m->wedges[tr->w[k] < m->num_wedges ? tr->w[k] : 0];
			w[k] = mat4_mul_point(t, m->vertices[wd->vertex >= 0 && wd->vertex < m->num_vertices ? wd->vertex : 0]);
			if (wd->normal >= 0 && wd->normal < m->num_normals) n = vec3_add(n, m->normals[wd->normal]);
			in |= w[k].x >= s->vmin.x && w[k].x <= s->vmax.x && w[k].y >= s->vmin.y && w[k].y <= s->vmax.y &&
				w[k].z >= s->vmin.z && w[k].z <= s->vmax.z;
		}
		if (!in) {
			vec3_t tl = vec3(min(w[0].x, min(w[1].x, w[2].x)), min(w[0].y, min(w[1].y, w[2].y)), min(w[0].z, min(w[1].z, w[2].z)));
			vec3_t th = vec3(max(w[0].x, max(w[1].x, w[2].x)), max(w[0].y, max(w[1].y, w[2].y)), max(w[0].z, max(w[1].z, w[2].z)));
			if (!box_overlap(tl, th, s->vmin, s->vmax)) continue;
		}
		receiver(s, w, vec3_normalize(mat4_mul_point(rot, n)));
	}
}

// The directional lights of FUN_00437460's gather with flag 0x10, in list order (Light_Create puts a new
// light at the head): direction = the light node's z axis, level = light +0x2c, the largest of its
// float colour components (Light_SetColour 0x437080, c * (float)(1/255); ftol(level * 255.0) gives c
// back for every byte). The game has two kinds: the mission's (Lights_CreateDirectional 0x4515a0, flags
// 0x18, made in record order whenever the outside factor leaves 0, so the last record is the newest;
// destroyed at factor 0; ExteriorLights_SetFactor 0x451c00 colours them colour * factor >> 12) and
// Lander_Render's white light straight down (flags 0x11) while the outside factor is 0.
typedef struct { int32_t dir[3]; int32_t level; } sun_t;

static int shadow_lights(sun_t *out) {
	if (gOutsideFactor == 0) {
		out[0] = (sun_t){{0, -0x1000, 0}, 0xff};
		return 1;
	}
	const mission_t *ms = G.level->mission;
	int n = 0;
	for (int i = min(ms->num_lights[0], 4) - 1; i >= 0; i--) {
		const light_dir_t *l = &ms->dir_lights[i];
		// the record's packed direction negated (exact integers), FwVectorNormalise'd by FUN_00431920
		int32_t d[3] = {(int32_t)lrintf(l->dir.x * 4096), (int32_t)lrintf(l->dir.y * 4096), (int32_t)lrintf(l->dir.z * 4096)};
		phys_normalise(d);
		int32_t r = (l->color.r * gOutsideFactor) >> 12, g = (l->color.g * gOutsideFactor) >> 12;
		int32_t b = (l->color.b * gOutsideFactor) >> 12;
		out[n].dir[0] = (int16_t)d[0];
		out[n].dir[1] = (int16_t)d[1];
		out[n].dir[2] = (int16_t)d[2];
		out[n++].level = max(r, max(g, b));
	}
	return n;
}

// FUN_0041fcf0: the ground along the light within 0x2000 (FwVectorScale(dir, 0x2000)); the strength is
// 0x80 up to 0x1000 away, fading to 0 at 0x2000; -1 when nothing is hit
static int32_t ground_strength(const int32_t p0[3], const int32_t dir[3]) {
	int32_t p1[3];
	for (int k = 0; k < 3; k++) p1[k] = p0[k] + ((dir[k] * 0x2000 + 0x800) >> 12);
	phys_hit_t hit;
	int old = G.world.query_mask;
	G.world.query_mask = 3;
	int found = phys_segment_hit(p0, p1, 2, &hit);
	G.world.query_mask = old;
	if (!found) return -1;
	int32_t dx = p0[0] - hit.point[0], dy = p0[1] - hit.point[1], dz = p0[2] - hit.point[2];
	int32_t dist = fx_sqrt(dx * dx + dy * dy + dz * dz);
	return dist - 0x1000 < 1 ? 0x80 : (0x1000 - ((dist - 0x1000) * 0x1000) / 0x1000) >> 5;
}

void eng_shadow_render(void *model_coord) { // 0x41d9f0
	coord_t *mc = model_coord;
	if (!shadow_on || !mc || !fxc.valid || !G.level) return;
	const model_t *m = NULL;
	for (int i = 0; i < MAX_INSTANCES && !m; i++) {
		if (gInstances[i].used && gInstances[i].coord == mc) m = gInstances[i].model;
	}
	if (!m) return;
	const FwMatrix *wm = coord_world(mc);
	shadow_t s;
	s.org = vec3((float)wm->t[0], (float)wm->t[1], (float)wm->t[2]);
	// the shadow light: the first directional light of the gathered list whose ray hits the ground
	sun_t suns[5];
	int nsuns = shadow_lights(suns), sun = -1;
	int32_t p0[3] = {wm->t[0], wm->t[1], wm->t[2]};
	for (int i = 0; i < nsuns && sun < 0; i++) {
		int32_t strength = ground_strength(p0, suns[i].dir);
		if (strength < 0) continue;
		sun = i;
		memcpy(s.dir, suns[i].dir, sizeof(s.dir));
		s.strength = ((suns[i].level >> 1) * strength) >> 7; // ftol(light +0x2c * 255.0)
	}
	if (sun < 0 || s.strength <= 0) return;
	// FUN_0041de40: texture axes across the light, from the axis of its smallest component
	int k = abs(s.dir[1]) < abs(s.dir[0]) ? 1 : 0;
	if (abs(s.dir[2]) < abs(s.dir[k])) k = 2;
	int32_t helper[3] = {0, 0, 0};
	helper[k] = 0x1000;
	cross12(s.dir, helper, s.a);
	cross12(s.dir, s.a, s.b);
	// the silhouette (FUN_0041e810): every triangle of the ship, 1/8 unit per texel
	memset(mask_px, 0, sizeof(mask_px));
	mat4_t t = fw_to_mat4(wm);
	for (int i = 0; i < m->num_tris; i++) {
		const pcp_tri_t *tr = &m->tris[i];
		float x[3], y[3];
		int outc = 0x3f, c = 0;
		for (int j = 0; j < 3; j++) {
			const pcp_wedge_t *wd = &m->wedges[tr->w[j] < m->num_wedges ? tr->w[j] : 0];
			vec3_t p = mat4_mul_point(t, m->vertices[wd->vertex >= 0 && wd->vertex < m->num_vertices ? wd->vertex : 0]);
			vec3_t r = vec3_sub(p, s.org);
			x[j] = (r.x * s.a[0] + r.y * s.a[1] + r.z * s.a[2]) / 4096.0f * 0.125f + 64.0f;
			y[j] = (r.x * s.b[0] + r.y * s.b[1] + r.z * s.b[2]) / 4096.0f * 0.125f + 64.0f;
			c |= ((x[j] > 128.0f || x[j] < 0) ? 1 : 0) << (j * 2);
			c |= ((y[j] > 128.0f || y[j] < 0) ? 2 : 0) << (j * 2);
		}
		if (c != outc) fill_tri(x, y);
	}
	// the receivers: zone and tile polygons in a volume from the ship along the light (FUN_00468250)
	vec3_t e = vec3_add(s.org, vec3(s.dir[0] * 2.0f, s.dir[1] * 2.0f, s.dir[2] * 2.0f)); // 0x2000 along
	s.vmin = vec3(min(s.org.x - 0x400, e.x - 0x800), min(s.org.y - 0x400, e.y - 0x800), min(s.org.z - 0x400, e.z - 0x800));
	s.vmax = vec3(max(s.org.x + 0x400, e.x + 0x800), max(s.org.y + 0x400, e.y + 0x800), max(s.org.z + 0x400, e.z + 0x800));
	const level_t *lv = G.level;
	if (lv->zonedata && lv->zones) {
		for (int z = 0; z < lv->zonedata->num_zones && z < lv->zones->count; z++) {
			if (!lv->zones->entries[z].num_parts) continue;
			ivec3_t o = lv->zonedata->origins[z];
			receivers_of(&s, lv->zones->entries[z].parts[0], mat4_translate(vec3((float)o.x, (float)o.y, (float)o.z)));
		}
	}
	if (lv->tiledata && lv->tiles) {
		int tx0 = tile_index((int32_t)s.vmin.x), tx1 = tile_index((int32_t)s.vmax.x);
		int tz0 = tile_index((int32_t)s.vmin.z), tz1 = tile_index((int32_t)s.vmax.z);
		for (int tz = tz0; tz <= tz1; tz++) {
			for (int tx = tx0; tx <= tx1; tx++) {
				tile_cell_t cells[8];
				int n = tiledata_lookup(lv->tiledata, tx, tz, cells, 8);
				for (int i = 0; i < n; i++) {
					if (cells[i].model >= lv->tiles->count || !lv->tiles->entries[cells[i].model].num_parts) continue;
					float ax, az, bx, bz;
					tile_rotate(cells[i].rotation, 1, 0, &ax, &az);
					tile_rotate(cells[i].rotation, 0, 1, &bx, &bz);
					mat4_t r = mat4_identity();
					r.m[0] = ax; r.m[2] = az;
					r.m[8] = bx; r.m[10] = bz;
					mat4_t tt = mat4_mul(mat4_translate(vec3((float)tile_center(tx), (float)cells[i].y, (float)tile_center(tz))),
						mat4_mul(r, mat4_scale(vec3(1.0009766f, 1.0009766f, 1.0009766f))));
					receivers_of(&s, lv->tiles->entries[cells[i].model].parts[0], tt);
				}
			}
		}
	}
}
