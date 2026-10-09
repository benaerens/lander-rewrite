// Combat helpers: PSX-style matrix helpers, the projectile segment query with the landers' bodies,
// sparks, and the sprite drawing of projectiles and explosions (glow sprites 0x44a330 / 0x44a4a0
// through FwSpriteDrawFX 0x43cb10, streaks, particle billboards) as camera-facing quads.
#include "weapons_internal.h"
#include "effects.h"

// ---------------------------------------------------------------- maths

int32_t w_catan(int32_t v) { // catan 0x4354b0: trunc(atan(v / 4096) * 4096 / 2pi)
	return (int32_t)(atan(v / 4096.0) * 4096.0 * 0.15915494309189535);
}

void wm_orient_z(FwMatrix *m) { // 0x42d360: Z = column 2 normalised, X = Y x Z, Y = Z x X
	int32_t y[3] = {m->m[0][1], m->m[1][1], m->m[2][1]}, z[3] = {m->m[0][2], m->m[1][2], m->m[2][2]}, x[3];
	phys_normalise(z);
	wv_cross(y, z, x);
	phys_normalise(x);
	wv_cross(z, x, y);
	for (int r = 0; r < 3; r++) {
		m->m[r][0] = (int16_t)x[r];
		m->m[r][1] = (int16_t)y[r];
		m->m[r][2] = (int16_t)z[r];
	}
}

void wm_rotate_x(FwMatrix *m, int32_t a) { // 0x431a30: m = m * Rx(a), Rx = [1 0 0; 0 c -s; 0 s c]
	FwMatrix r;
	fw_identity(&r);
	int32_t s = fx_rsin(a), c = fx_rcos(a);
	r.m[1][1] = (int16_t)c;
	r.m[1][2] = (int16_t)-s;
	r.m[2][1] = (int16_t)s;
	r.m[2][2] = (int16_t)c;
	fw_mul(m, &r, m);
}

// ---------------------------------------------------------------- landers in the segment query

typedef struct {
	const model_t *model;
	phys_mesh_t mesh;
	int num_tris;      // for freeing without touching the (possibly reloaded) model
} lander_mesh_t;

static lander_mesh_t lander_meshes[16];
static int (*saved_query)(phys_world_t *, const int32_t *, phys_cand_t *, int);

static phys_mesh_t *lander_mesh(const model_t *m) {
	if (!m) return NULL;
	for (int i = 0; i < (int)len(lander_meshes); i++) {
		if (lander_meshes[i].model == m) return lander_meshes[i].mesh.octree ? &lander_meshes[i].mesh : NULL;
	}
	for (int i = 0; i < (int)len(lander_meshes); i++) {
		if (!lander_meshes[i].model) {
			lander_meshes[i].model = m;
			lander_meshes[i].num_tris = m->num_tris;
			phys_mesh_init(&lander_meshes[i].mesh, m, i);
			return lander_meshes[i].mesh.octree ? &lander_meshes[i].mesh : NULL;
		}
	}
	return NULL;
}

static void box_to_local(const int32_t *box, const FwMatrix *m, int32_t *out) { // FUN_004456c0
	int64_t c[3], h[3];
	for (int i = 0; i < 3; i++) {
		c[i] = ((int64_t)box[i] + box[4 + i]) / 2 - m->t[i];
		h[i] = ((int64_t)box[4 + i] - box[i]) / 2 + 1;
	}
	for (int i = 0; i < 3; i++) {
		int64_t lc = 0, lh = 0;
		for (int j = 0; j < 3; j++) {
			lc += m->m[j][i] * c[j];
			lh += (m->m[j][i] < 0 ? -m->m[j][i] : m->m[j][i]) * h[j];
		}
		out[i] = (int32_t)((lc - lh) >> 12);
		out[4 + i] = (int32_t)((lc + lh + 0xfff) >> 12);
	}
	out[3] = out[7] = 0;
}

// Mob_QueryBox 0x446a30 takes lander mobs (type 1) although their record has no mission model:
// the collision volume is the one of their render item's model (0x445620)
static int query_with_landers(phys_world_t *w, const int32_t *box, phys_cand_t *out, int max) {
	int n = saved_query ? saved_query(w, box, out, max) : 0;
	if (!(w->query_mask & 0x10) || !G.level) return n;
	const mission_t *m = G.level->mission;
	for (int i = 0; i < m->num_rec[REC_MOB] && n < max; i++) {
		const map_rec_t *r = &m->rec[REC_MOB][i];
		if (r->type != OT_LANDER || r->model != -1 || !eng_mob_is_awake(i)) continue;
		ObjId id = MOB_ID(OT_LANDER, i);
		if ((int32_t)id == phys_g.exclude_owner) continue;
		int16_t inst = Object_GetInstance(id);
		if (inst < 0 || !gInstances[inst].used) continue;
		phys_mesh_t *mesh = lander_mesh(gInstances[inst].model);
		if (!mesh) continue;
		int32_t lb[8];
		box_to_local(box, coord_world(gInstances[inst].coord), lb);
		n += phys_collect_part(mesh, lb, NULL, (uint32_t)inst, id, out + n, max - n);
	}
	return n;
}

int fx_segment_hit(const int32_t *p0, const int32_t *p1, int flags, phys_hit_t *hit, ObjId exclude) {
	int32_t old_ex = phys_g.exclude_owner;
	phys_g.exclude_owner = (int32_t)exclude;     // DAT_00498434
	saved_query = G.world.query_objects;
	G.world.query_objects = query_with_landers;
	int r = phys_segment_hit(p0, p1, flags, hit);
	G.world.query_objects = saved_query;
	phys_g.exclude_owner = old_ex;
	return r;
}

void fx_reset(void) {
	for (int i = 0; i < (int)len(lander_meshes); i++) {
		lander_mesh_t *l = &lander_meshes[i];
		if (l->mesh.polys) {
			for (int k = 0; k < l->num_tris; k++) free(l->mesh.polys[k]);
			free(l->mesh.polys);
		}
		memset(l, 0, sizeof(*l));
	}
}

// ---------------------------------------------------------------- sparks (0x454ef0)

void fx_sparks(const int32_t pos[3], const int32_t vel[3], int n, int life) {
	fx_spark_burst(pos, vel, n, (uint8_t)life);   // effects.c: FUN_00454ef0 with the particle flags (+0x17)
}

// ---------------------------------------------------------------- drawing

const texture_t *fx_global_texture(int index) {
	if (!G.global_tex || index < 0 || index >= G.global_tex->count) return NULL;
	return G.global_tex->entries[index].frame;
}

static vec3_t cam_right, cam_up;

void fx_draw_begin(void) {
	mat4_t v = render_view();
	cam_right = vec3(v.m[0], v.m[4], v.m[8]);
	cam_up = vec3(v.m[1], v.m[5], v.m[9]);
	render_set_model(mat4_identity());
	render_set_cull(CULL_NONE);
	render_set_depth(true, false);
}

void fx_draw_end(void) {
	render_set_cull(CULL_BACK);
	render_set_depth(true, true);
}

float fx_view_depth(const int32_t pos[3]) {
	mat4_t v = render_view();
	vec4_t p = mat4_mul_vec4(v, (vec4_t){(float)pos[0], (float)pos[1], (float)pos[2], 1});
	return p.z;
}

static rgba_t colour_of(uint32_t rgb) { return rgba(rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff, 255); }

static void sprite_quad(const int32_t pos[3], float hx, float hy, float angle, uint32_t rgb, const texture_t *tex,
	float u0, float v0, float u1, float v1) {
	if (hx <= 0 || hy <= 0) return;
	float a = angle * 2 * PI / 4096, c = cosf(a), s = sinf(a);
	vec3_t r = vec3_add(vec3_mulf(cam_right, c * hx), vec3_mulf(cam_up, -s * hx));
	vec3_t u = vec3_add(vec3_mulf(cam_right, s * hy), vec3_mulf(cam_up, c * hy));
	vec3_t p = vec3((float)pos[0], (float)pos[1], (float)pos[2]);
	rgba_t col = colour_of(rgb);
	vertex_t q[6];
	vec3_t c00 = vec3_sub(vec3_sub(p, r), u), c10 = vec3_sub(vec3_add(p, r), u);
	vec3_t c11 = vec3_add(vec3_add(p, r), u), c01 = vec3_add(vec3_sub(p, r), u);
	q[0] = (vertex_t){c01, u0, v0, col};
	q[1] = (vertex_t){c11, u1, v0, col};
	q[2] = (vertex_t){c10, u1, v1, col};
	q[3] = (vertex_t){c01, u0, v0, col};
	q[4] = (vertex_t){c10, u1, v1, col};
	q[5] = (vertex_t){c00, u0, v1, col};
	render_tris(q, 6, tex, BLEND_ADD);
}

void fx_sprite(const int32_t pos[3], float half, float angle, uint32_t rgb, const texture_t *tex,
	float u0, float v0, float u1, float v1) {
	sprite_quad(pos, half, half, angle, rgb, tex, u0, v0, u1, v1);
}

// the effect textures of misc\standard (TexFile slot 3, DAT_00577c68): 1 flare streak, 5 round glow,
// made additive by 0x44a2b0
static texfile_t *standard_tex;

static const texture_t *standard_texture(int index) {
	static bool tried;
	if (!tried) {
		tried = true;
		standard_tex = texfile_load("MISC\\STANDARD\\PCPURE\\STANDARD");
	}
	if (!standard_tex || index < 0 || index >= standard_tex->count) return NULL;
	return standard_tex->entries[index].frame;
}

// one FwSpriteDrawFX sprite: the texture drawn at `scale` (4.12 per texel; world units here because
// the exe's scale is divided by the same depth as the geometry)
static void fx_spritefx(const int32_t pos[3], int tex_index, float angle, float scale, uint32_t rgb) {
	const texture_t *t = standard_texture(tex_index);
	if (!t) return;
	// half extents = width * size >> 13, height * size >> 13 (0x43d4b0)
	sprite_quad(pos, t->width * scale / 8192.0f, t->height * scale / 8192.0f, angle, rgb, t, 0, 0, 1, 1);
}

static uint32_t grey_of_max(uint32_t c) {
	uint32_t m = (c >> 16) & 0xff;
	if (m < ((c >> 8) & 0xff)) m = (c >> 8) & 0xff;
	if (m < (c & 0xff)) m = c & 0xff;
	return (m << 16) | (m << 8) | m;
}

void fx_glow(const int32_t pos[3], uint32_t rgb, float size, bool rotate) {
	if (!rotate) { // 0x44a4a0: explosion flash
		fx_spritefx(pos, 5, 0, size, (rgb >> 1) & 0x7f7f7f);
		fx_spritefx(pos, 5, 0, size / 4, grey_of_max(rgb));
		return;
	}
	// 0x44a330: two rotated flares (angle from the screen position) and two round glows
	mat4_t v = render_view(), pr = render_projection();
	vec4_t e = mat4_mul_vec4(v, (vec4_t){(float)pos[0], (float)pos[1], (float)pos[2], 1});
	vec4_t c = mat4_mul_vec4(pr, e);
	// screen position in the original's 640x480 units; a wider frame extends them sideways (Hor+)
	int32_t sx = 320, sy = 240;
	if (c.w > 1) {
		float half_w = 240.0f * render_width() / render_height();   // 320 at 4:3
		sx = (int32_t)(320 + c.x / c.w * half_w);
		sy = (int32_t)((1 - c.y / c.w) * 240);
	}
	fx_spritefx(pos, 1, (float)fx_ratan2(sx + 300, sy + 300), size, rgb);
	fx_spritefx(pos, 1, (float)fx_ratan2(sx - 400, sy + 100), size / 2, rgb);
	fx_spritefx(pos, 5, 0, size, (rgb & 0xfefefe) >> 1);
	fx_spritefx(pos, 5, 0, size / 4, grey_of_max(rgb));
}

void fx_streak(const int32_t head[3], const int32_t tail[3], float half_head, float half_tail,
	uint32_t rgb_head, uint32_t rgb_tail) {
	vec3_t h = vec3((float)head[0], (float)head[1], (float)head[2]);
	vec3_t t = vec3((float)tail[0], (float)tail[1], (float)tail[2]);
	vec3_t mid = vec3_mulf(vec3_add(h, t), 0.5f);
	vec3_t side = vec3_cross(vec3_sub(t, h), vec3_sub(mid, G.cam_pos));
	float l = vec3_len(side);
	if (l < 1e-3f) return;
	side = vec3_mulf(side, 1.0f / l);
	rgba_t ch = colour_of(rgb_head), ct = colour_of(rgb_tail);
	vec3_t h0 = vec3_add(h, vec3_mulf(side, half_head)), h1 = vec3_sub(h, vec3_mulf(side, half_head));
	vec3_t t0 = vec3_add(t, vec3_mulf(side, half_tail)), t1 = vec3_sub(t, vec3_mulf(side, half_tail));
	vertex_t q[6] = {
		{h0, 0, 0, ch}, {h1, 0, 0, ch}, {t1, 0, 0, ct},
		{h0, 0, 0, ch}, {t1, 0, 0, ct}, {t0, 0, 0, ct},
	};
	render_tris(q, 6, NULL, BLEND_ADD);
}
