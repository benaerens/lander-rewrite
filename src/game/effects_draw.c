// Drawing core of the effects (see effects_draw.h).
#include <stdlib.h>
#include <string.h>

#include "effects_draw.h"
#include "camera.h"

fx_cam_t fxc;
rgba_t fx_flash;
bool fx_flash_pending;
bool fx_mirror;

// the Venus reflection pass: world y negated (gMirrorCamera = the view with row 1 and t.y negated)
static mat4_t mirrored(mat4_t view) { return mat4_mul(view, mat4_scale(vec3(1, -1, 1))); }

// ---------------------------------------------------------------- camera

#define VIEW_HISTORY 8
static struct { uint32_t t; mat4_t view; } hist[VIEW_HISTORY];
static int hist_n;

static mat4_t mat4_invert_rigid(mat4_t m) { // rotation + translation
	mat4_t r = mat4_identity();
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r.m[j * 4 + i] = m.m[i * 4 + j];
		}
	}
	for (int i = 0; i < 3; i++) {
		r.m[12 + i] = -(r.m[i] * m.m[12] + r.m[4 + i] * m.m[13] + r.m[8 + i] * m.m[14]);
	}
	return r;
}

static rgba_t lerp_inout(rgba_t in, rgba_t ex, int f) { // Env_LerpInOut 0x40f940 (as level.c)
	if (f == 0) return in;
	if (f == 0x1000) return ex;
	return rgba((uint8_t)(in.r + (((ex.r - in.r) * f) >> 12)), (uint8_t)(in.g + (((ex.g - in.g) * f) >> 12)),
		(uint8_t)(in.b + (((ex.b - in.b) * f) >> 12)), 255);
}

void fx_cam_capture(uint32_t now) {
	fxc.view = render_view();
	fxc.inv = mat4_invert_rigid(fxc.view);
	fxc.eye = vec3(fxc.inv.m[12], fxc.inv.m[13], fxc.inv.m[14]);
	float s = render_2d_scale();
	fxc.w = render_2d_width();
	fxc.ht = render_2d_height();
	fxc.cx = (float)render_width() * 0.5f / s;
	fxc.cy = (float)render_height() * 0.5f / s;
	fxc.hw = (float)((fxc.ht * 4 / 3) >> 1);          // Hor+: the vertical view of the 4:3 screen
	fxc.h = (float)cam_projection_h(900, (int32_t)fxc.hw); // FwSetScreenAngle(900)
	fxc.now = now;
	const level_t *lv = G.level;
	if (lv) {
		const mission_t *ms = lv->mission;
		float vd = lv->view_distance;
		rgba_t ext = ms->exterior;
		if (fx_flash_pending && !level_exterior_override) { // level_begin_frame used the flash colour
			ext = fx_flash;
			fx_flash_pending = false;
		}
		fxc.fog = lerp_inout(ms->interior, ext, lv->outside_factor);
		fxc.fog_start = ms->fog_far ? vd * ms->fog_near / ms->fog_far : vd * 0.75f;
		fxc.fog_end = vd;
	}
	fxc.valid = true;
	if (fx_mirror) return; // the history keeps the real camera (fx_view_at mirrors it)
	// view history for fx_view_at
	if (hist_n && hist[hist_n - 1].t == now) {
		hist[hist_n - 1].view = fxc.view;
	} else {
		if (hist_n == VIEW_HISTORY) {
			memmove(hist, hist + 1, sizeof(hist[0]) * (VIEW_HISTORY - 1));
			hist_n--;
		}
		hist[hist_n].t = now;
		hist[hist_n++].view = fxc.view;
	}
}

static mat4_t view_at(uint32_t t) {
	if (!hist_n) return fxc.view;
	if ((int32_t)(t - hist[0].t) <= 0) return hist[0].view;
	for (int i = 1; i < hist_n; i++) {
		if ((int32_t)(t - hist[i].t) <= 0) {
			float f = (float)(t - hist[i - 1].t) / (float)(hist[i].t - hist[i - 1].t);
			mat4_t r;
			for (int k = 0; k < 16; k++) r.m[k] = hist[i - 1].view.m[k] + (hist[i].view.m[k] - hist[i - 1].view.m[k]) * f;
			return r;
		}
	}
	return hist[hist_n - 1].view;
}

// FUN_004599e0 negates the past camera's row 1 and t.y in the reflection pass
mat4_t fx_view_at(uint32_t t) {
	if (fx_mirror && hist_n) return mirrored(view_at(t));
	return view_at(t);
}

static vec3_t cam_dir(vec3_t w) { // world direction -> PSX camera space
	const float *m = fxc.view.m;
	return vec3(m[0] * w.x + m[4] * w.y + m[8] * w.z, -(m[1] * w.x + m[5] * w.y + m[9] * w.z),
		m[2] * w.x + m[6] * w.y + m[10] * w.z);
}

void fx_xform_world(const FwMatrix *wm, fx_xform_t *out) {
	for (int c = 0; c < 3; c++) { // column c = image of the local axis c
		vec3_t axis = cam_dir(vec3(wm->m[0][c] / 4096.0f, wm->m[1][c] / 4096.0f, wm->m[2][c] / 4096.0f));
		out->m[0][c] = axis.x;
		out->m[1][c] = axis.y;
		out->m[2][c] = axis.z;
	}
	vec3_t t = fx_to_cam(vec3((float)wm->t[0], (float)wm->t[1], (float)wm->t[2]));
	out->t[0] = t.x;
	out->t[1] = t.y;
	out->t[2] = t.z;
}

void fx_xform_rot_only(const FwMatrix *rot, const fx_xform_t *trans_from, fx_xform_t *out) {
	fx_xform_t r;
	fx_xform_world(rot, &r);
	memcpy(out->m, r.m, sizeof(r.m));
	memcpy(out->t, trans_from->t, sizeof(out->t));
}

vec3_t fx_rt(const fx_xform_t *x, vec3_t l) {
	return vec3(x->m[0][0] * l.x + x->m[0][1] * l.y + x->m[0][2] * l.z + x->t[0],
		x->m[1][0] * l.x + x->m[1][1] * l.y + x->m[1][2] * l.z + x->t[1],
		x->m[2][0] * l.x + x->m[2][1] * l.y + x->m[2][2] * l.z + x->t[2]);
}

bool fx_rtp(const fx_xform_t *x, vec3_t l, float *sx, float *sy, float *z) {
	vec3_t c = fx_rt(x, l);
	*z = c.z;
	if (c.z <= 0) {
		*sx = *sy = 0;
		return false;
	}
	*sx = fxc.cx + c.x * fxc.h / c.z;
	*sy = fxc.cy + c.y * fxc.h / c.z;
	// RotTransPers flags results outside the signed 16-bit screen range
	return *sx > -32768 && *sx < 32767 && *sy > -32768 && *sy < 32767;
}

// ---------------------------------------------------------------- primitive list

typedef struct {
	int first, n;
	const texture_t *tex;
	int blend;
	bool fog, wrap;
	float depth;
	int seq;
	int bucket;           // OT_InsertDepthSorted 0x434f50: trunc(z / 8) in 0..4095 (32768 / 4096 units)
	int16_t clip[4];
} prim_t;

bool fx_wrap;

static fxv_t *verts;
static int nverts, cap_verts;
static prim_t *prims;
static int nprims, cap_prims;
static int16_t cur_clip[4] = {0, 0, 0x7fff, 0x7fff};

void fx_frame_reset(void) {
	nverts = nprims = 0;
	fx_clip_reset();
}

void fx_clip_reset(void) {
	cur_clip[0] = cur_clip[1] = 0;
	cur_clip[2] = cur_clip[3] = 0x7fff;
}

bool fx_clip_zone(int zone) { // Zones_GetClipSet(zone) + FUN_0043e9b0
	fx_clip_reset();
	const level_t *lv = G.level;
	if (zone < -1 || !lv || !lv->zonedata || !lv->zonedata->num_zones) return true;
	const vis_record_t *r = vis_find(&lv->vis, zone);
	if (!r) return false;
	cur_clip[0] = r->x0; cur_clip[1] = r->y0; cur_clip[2] = r->x1; cur_clip[3] = r->y1;
	return true;
}

static const texture_t *tex_for(const texture_t *tex, int blend) {
	return blend == FX_ADD && tex ? fx_tex_opaque_alpha(tex) : tex;
}

void fx_queue(const fxv_t *v, int n, const texture_t *tex, int blend, bool fog, float depth) {
	if (n < 3) return;
	if (nverts + n > cap_verts) {
		cap_verts = max(cap_verts * 2, nverts + n + 4096);
		verts = realloc(verts, sizeof(fxv_t) * cap_verts);
	}
	if (nprims + 1 > cap_prims) {
		cap_prims = max(cap_prims * 2, 1024);
		prims = realloc(prims, sizeof(prim_t) * cap_prims);
	}
	memcpy(verts + nverts, v, sizeof(fxv_t) * n);
	int bucket = depth >= FX_LAYER_3D ? 4096 : depth < 8.0f ? 0 : depth >= 4095 * 8.0f ? 4095 : (int)(depth / 8.0f);
	prims[nprims] = (prim_t){nverts, n, tex, blend, fog, fx_wrap, depth, nprims, bucket,
		{cur_clip[0], cur_clip[1], cur_clip[2], cur_clip[3]}};
	nprims++;
	nverts += n;
}

void fx_queue_fan(const fxv_t *v, int n, const texture_t *tex, int blend, bool fog, float depth) {
	fxv_t t[3 * 64];
	int k = 0;
	for (int i = 1; i + 1 < n && k + 3 <= (int)len(t); i++) {
		t[k++] = v[0];
		t[k++] = v[i];
		t[k++] = v[i + 1];
	}
	fx_queue(t, k, tex, blend, fog, depth);
}

void fx_queue_strip(const fxv_t *v, const uint16_t *idx, int n, const texture_t *tex, int blend, bool fog, float depth) {
	fxv_t t[3 * 64];
	int k = 0;
	for (int i = 0; i + 2 < n && k + 3 <= (int)len(t); i++) {
		t[k++] = v[idx[i]];
		t[k++] = v[idx[i + 1]];
		t[k++] = v[idx[i + 2]];
	}
	fx_queue(t, k, tex, blend, fog, depth);
}

// D3DPT_LINELIST: a one-pixel wide quad between the screen positions of a and b
void fx_queue_line(vec3_t a, vec3_t b, uint32_t ca, uint32_t cb, int blend, bool fog, float depth) {
	vec3_t pa = fx_to_cam(a), pb = fx_to_cam(b);
	if (pa.z <= 1 || pb.z <= 1) return;
	float ax = fxc.cx + pa.x * fxc.h / pa.z, ay = fxc.cy + pa.y * fxc.h / pa.z;
	float bx = fxc.cx + pb.x * fxc.h / pb.z, by = fxc.cy + pb.y * fxc.h / pb.z;
	float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
	float nx = 0.5f, ny = 0;
	if (l > 0.001f) {
		nx = -dy / l * 0.5f;
		ny = dx / l * 0.5f;
	}
	fxv_t q[4] = {
		{fx_unproject(ax - nx, ay - ny, pa.z), 0, 0, ca}, {fx_unproject(ax + nx, ay + ny, pa.z), 0, 0, ca},
		{fx_unproject(bx + nx, by + ny, pb.z), 0, 0, cb}, {fx_unproject(bx - nx, by - ny, pb.z), 0, 0, cb},
	};
	fxv_t t[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
	fx_queue(t, 6, NULL, blend, fog, depth);
}

// the 3D layer first in submission order, then the sort table back to front: SortTable_Flush 0x435360
// runs the buckets from 4095 down, each bucket's list from its last insertion (OT_InsertDepthSorted
// prepends)
static int prim_cmp(const void *a, const void *b) {
	const prim_t *p = a, *q = b;
	if (p->bucket != q->bucket) return q->bucket - p->bucket;
	return p->bucket == 4096 ? p->seq - q->seq : q->seq - p->seq;
}

static void draw_prim(const fxv_t *v, int n, const texture_t *tex, int blend, bool fog) {
	static vertex_t buf[3 * 256];
	rgba_t fogc = blend == FX_ADD ? rgba(0, 0, 0, 255) : fxc.fog;
	render_set_fog(fog, fogc, fxc.fog_start, fxc.fog_end);
	tex = tex_for(tex, blend);
	blend_t b = blend == FX_ADD ? BLEND_ADD : blend == FX_PCP_ADD ? BLEND_ONE_ONE : blend == FX_OPAQUE ? BLEND_OPAQUE : BLEND_ALPHA;
	for (int i = 0; i < n;) {
		int k = 0;
		for (; k < (int)len(buf) && i < n; k++, i++) {
			uint32_t c = v[i].c;
			buf[k].pos = v[i].pos;
			buf[k].u = v[i].u;
			buf[k].v = v[i].v;
			// ONE/ONE adds the colour whatever its alpha
			buf[k].color = rgba((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, blend == FX_ADD ? 255 : c >> 24);
		}
		render_tris(buf, k - k % 3, tex, b);
	}
}

static void prim_states(const prim_t *p) {
	bool pcp = p->blend == FX_PCP_ALPHA || p->blend == FX_PCP_ADD;
	render_set_depth(true, pcp);              // effects turn z writes off for any blend; PCP ones keep them
	render_set_cull(pcp ? CULL_BACK : CULL_NONE);
	render_set_wrap(p->wrap);
}

void fx_draw_now(const fxv_t *v, int n, const texture_t *tex, int blend, bool fog) {
	render_set_model(mat4_identity());
	render_set_depth(true, blend == FX_OPAQUE);
	render_set_cull(CULL_NONE);
	draw_prim(v, n, tex, blend, fog);
	render_set_depth(true, true);
	render_set_cull(CULL_BACK);
	render_set_fog(true, fxc.fog, fxc.fog_start, fxc.fog_end);
}

void fx_flush(void) {
	if (!nprims) return;
	qsort(prims, nprims, sizeof(prim_t), prim_cmp);
	render_set_model(mat4_identity());
	for (int i = 0; i < nprims; i++) {
		const int16_t *c = prims[i].clip;
		render_set_scissor(c[0], c[1], c[2], c[3]);
		prim_states(&prims[i]);
		draw_prim(verts + prims[i].first, prims[i].n, prims[i].tex, prims[i].blend, prims[i].fog);
	}
	render_reset_scissor();
	render_set_depth(true, true);
	render_set_cull(CULL_BACK);
	render_set_wrap(false);
	render_set_fog(true, fxc.fog, fxc.fog_start, fxc.fog_end);
	fx_frame_reset();
}

// ---------------------------------------------------------------- sprites

void fx_sprite_section(float x, float y, float z, const texture_t *tex, int u0, int v0, int u1, int v1,
	unsigned flags, uint32_t bgr, int32_t scale, int32_t angle, int blend, bool fog) { // 0x43d180
	if (!tex || z > 32768.0f || z <= 10) {
		return;
	}
	int du = u1 - u0, dv = v1 - v0;
	float px[4], py[4];
	if (!(flags & 4)) { // FUN_0043d4b0
		if (!(flags & 8)) {
			px[0] = x; py[0] = y; px[1] = x + du; py[1] = y; px[2] = x + du; py[2] = y + dv; px[3] = x; py[3] = y + dv;
		} else {
			int32_t c = fx_rcos(angle), s = fx_rsin(angle);
			int32_t a = c * du >> 13, b = s * dv >> 13, e = s * du >> 13, f = c * dv >> 13;
			px[0] = x - a - b; py[0] = y + e - f;
			px[1] = x + a - b; py[1] = y - e - f;
			px[2] = x + b + a; py[2] = y + f - e;
			px[3] = x + b - a; py[3] = y + f + e;
		}
	} else {
		int32_t hx = du * scale >> 13, hy = dv * scale >> 13;
		if (!(flags & 8)) {
			px[0] = x - hx; py[0] = y - hy; px[1] = x + hx; py[1] = y - hy;
			px[2] = x + hx; py[2] = y + hy; px[3] = x - hx; py[3] = y + hy;
		} else {
			int32_t c = fx_rcos(angle), s = fx_rsin(angle);
			int32_t a = c * hx >> 12, b = s * hy >> 12, e = s * hx >> 12, f = c * hy >> 12;
			px[0] = x - a - b; py[0] = y + e - f;
			px[1] = x - b + a; py[1] = y - e - f;
			px[2] = x + b + a; py[2] = y + f - e;
			px[3] = x - a + b; py[3] = y + f + e;
		}
	}
	float tu0 = (u0 + 0.5f) / tex->width, tu1 = (u1 + 0.5f) / tex->width;
	float tv0 = (v0 + 0.5f) / tex->height, tv1 = (v1 + 0.5f) / tex->height;
	float us[4] = {tu0, tu1, tu1, tu0}, vs[4] = {tv0, tv0, tv1, tv1};
	uint32_t c = (flags & 1) ? fx_bgr_to_argb(bgr) : 0xffffffffu;
	fxv_t q[4];
	for (int i = 0; i < 4; i++) {
		q[i] = (fxv_t){fx_unproject(px[i], py[i], z), us[i], vs[i], c};
	}
	fxv_t t[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
	fx_queue(t, 6, tex, blend, fog, z);
}

// ---------------------------------------------------------------- textures

const texture_t *fx_global_tex(int index) {
	texfile_t *tf = G.global_tex;
	return tf && index >= 0 && index < tf->count && tf->entries[index].frame ? &tf->entries[index].frame[0] : NULL;
}

const texture_t *fx_world_tex(int index) {
	texfile_t *tf = G.level ? G.level->textures : NULL;
	return tf && index >= 0 && index < tf->count && tf->entries[index].frame ? &tf->entries[index].frame[0] : NULL;
}

#define MAX_TEX_COPIES 64
static struct { const texture_t *src; const uint32_t *pixels; texture_t copy; } copies[MAX_TEX_COPIES];
static int ncopies;

const texture_t *fx_tex_opaque_alpha(const texture_t *t) {
	for (int i = 0; i < ncopies; i++) {
		if (copies[i].src == t && copies[i].pixels == t->pixels) return &copies[i].copy;
	}
	if (ncopies == MAX_TEX_COPIES) return t;
	texture_t *c = &copies[ncopies].copy;
	copies[ncopies].src = t;
	copies[ncopies].pixels = t->pixels;
	ncopies++;
	memset(c, 0, sizeof(*c));
	c->width = t->width;
	c->height = t->height;
	c->mip_ok = t->mip_ok; // the same D3D surface: mipmapped like the texture
	c->pixels = malloc(sizeof(uint32_t) * t->width * t->height);
	for (int i = 0; i < t->width * t->height; i++) c->pixels[i] = t->pixels[i] | 0xff000000u;
	return c;
}

void fx_textures_reset(void) {
	for (int i = 0; i < ncopies; i++) {
		free(copies[i].copy.pixels);
		render_texture_free_mips(&copies[i].copy);
	}
	ncopies = 0;
}

// ---------------------------------------------------------------- helpers

uint32_t fx_colour_scale(uint32_t c, int32_t k) { // 0x44a5b0
	uint32_t r = 0;
	for (int sh = 24; sh >= 0; sh -= 8) {
		uint32_t v = (uint32_t)(((c >> sh) & 0xff) * k) >> 12;
		if (v > 0xff) v = 0xff;
		r |= v << sh;
	}
	return r;
}

uint32_t fx_colour_mul(uint32_t a, uint32_t b) { // 0x44a720
	uint32_t r = 0;
	for (int sh = 24; sh >= 0; sh -= 8) {
		uint32_t v = ((a >> sh) & 0xff) * ((b >> sh) & 0xff) >> 8;
		r |= v << sh;
	}
	return r;
}

uint32_t fx_colour_pulse(uint32_t c, uint32_t t, uint32_t period) { // 0x44a570
	int32_t s = fx_rsin((int32_t)(((t % period) << 12) / period));
	return fx_colour_scale(c, (s >> 1) + 0x800);
}

uint32_t fx_hue_colour(uint32_t t, uint32_t period, int32_t sat, uint32_t val, bool swap) { // 0x44a5f0
	int32_t h = (int32_t)(((t % period) << 8) / period);
	uint32_t c0 = val, c1 = val, c2 = val; // uVar3, param_4, uVar4
	if (sat != 0) {
		uint32_t p = ((0x100 - sat) * (int32_t)val) >> 8;
		int32_t f = ((h % 0x2b) << 8) / 0x2b;
		uint32_t q = ((0x100 - ((f * sat) >> 8)) * (int32_t)val) >> 8;
		uint32_t r = ((0x100 - (((0x100 - f) * sat) >> 8)) * (int32_t)val) >> 8;
		switch (h / 0x2b) {
		case 0: c0 = p; c1 = r; break;
		case 1: c0 = p; c2 = q; break;
		case 2: c0 = r; c2 = p; break;
		case 3: c2 = p; c1 = q; break;
		case 4: c2 = r; c1 = p; break;
		case 5: c0 = q; c1 = p; break;
		}
	}
	return swap ? ((c2 << 8 | c1) << 8 | c0) : ((c0 << 8 | c1) << 8 | c2);
}

uint32_t fx_bgr_to_argb(uint32_t bgr) {
	return 0xff000000u | ((bgr & 0xff) << 16) | (bgr & 0xff00) | ((bgr >> 16) & 0xff);
}

static uint32_t rand_state = 1;
int32_t fx_rand(void) { // MSVC rand
	rand_state = rand_state * 214013u + 2531011u;
	return (int32_t)((rand_state >> 16) & 0x7fff);
}
void fx_srand(uint32_t seed) { rand_state = seed; }
