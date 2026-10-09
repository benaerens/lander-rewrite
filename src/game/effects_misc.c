// ShortFX (type 27, 0x4598a0-0x459990: the SHELL.PCP shock wave of big explosions), Segmented (type 23,
// 0x4582f0-0x4587f0 + the strip drawer 0x45bad0: a 32-point rope hanging from an animated head, mars\5),
// texture animation (TexAnimPlay 0x444370, TexAnim_Update 0x444880 from the pre-present hook) and
// SetBumpMapping (0x4443b0).
#include <stdlib.h>
#include <string.h>

#include "effects_int.h"

// ---------------------------------------------------------------- ShortFX (27)

#define MAX_SHORTFX 16

typedef struct {
	bool used;
	ObjId id;          // +0x0c
	uint32_t start;    // +0x10 gGameUpdateTime at creation
	coord_t *coord;    // +0x14
	int32_t frac;      // +0x1c 0..0x1000 over 500 ms
} shortfx_t;

static shortfx_t shortfx[MAX_SHORTFX];
static model_t *shell_model;   // DAT_00577c48: misc\SHELL.PCP (HudModels_Create 0x413b30, slot 0x21)

static shortfx_t *sfx_of(task_t *t) { return objstate(t)->extra; }

// args->extra = the world position (const int32_t[3]); Explosion_Create 0x44e610 passes its centre
static void shortfx_create(task_t *t, void *arg) { // 0x4598a0
	obj_args_t *a = arg;
	objstate_init(t, a);
	shortfx_t *s = NULL;
	for (int i = 0; i < MAX_SHORTFX && !s; i++) {
		if (!shortfx[i].used) s = &shortfx[i];
	}
	objstate(t)->extra = s;
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->used = true;
	s->id = a ? a->id : (ObjId)(Task_ToIndex(t) | 0x021b0000);
	s->start = (uint32_t)gGameUpdateTime;
	s->coord = coord_alloc(NULL);
	const int32_t *p = a ? (const int32_t *)a->extra : NULL;
	if (s->coord && p) {
		s->coord->local.t[0] = p[0];
		s->coord->local.t[1] = p[1];
		s->coord->local.t[2] = p[2];
	}
	if (!shell_model) shell_model = model_load_pcp("MISC\\SHELL.PCP");
}

static void shortfx_destroy(task_t *t) { // 0x459950
	shortfx_t *s = sfx_of(t);
	if (!s) return;
	coord_free(s->coord);
	s->used = false;
	objstate(t)->extra = NULL;
}

static void shortfx_update(task_t *t) { // 0x459970: dies after 500 ms
	shortfx_t *s = sfx_of(t);
	if (s && (uint32_t)gGameUpdateTime - s->start > 500) Task_Kill(t);
}

// 0x459990 + the draw function 0x459810: the shell grows from 1/8 to full size facing the camera,
// lit by an ambient that fades from 64 to 0
static void shortfx_render(task_t *t, uint32_t time) {
	shortfx_t *s = sfx_of(t);
	if (!s || !s->coord || !shell_model || !fxc.valid) return;
	uint32_t frac = ((time - s->start) * 0x1000u) / 500u;
	if (frac > 0x1000) return;
	s->frac = (int32_t)frac;
	FwMatrix *m = &s->coord->local;
	cam_view_t v = {0};
	memcpy(v.mat.t, m->t, sizeof(m->t));
	cam_view_look_dir(&v, (cvec3_t){(int32_t)fxc.eye.x - m->t[0], (int32_t)fxc.eye.y - m->t[1], (int32_t)fxc.eye.z - m->t[2]});
	memcpy(m->m, v.mat.m, sizeof(m->m));
	float scale = (float)((((int32_t)frac * 0xe00) >> 12) + 0x200) / 4096.0f; // DAT_004996f8
	mat4_t tr = mat4_mul(fw_to_mat4(coord_world(s->coord)), mat4_scale(vec3(scale, scale, scale)));
	lighting_t L;
	vec3_t c = vec3((float)m->t[0], (float)m->t[1], (float)m->t[2]);
	game_lighting_for(c, shell_model->radius * scale, &L);
	float amb = (float)((0x1000 - (int32_t)frac) >> 6) / 255.0f; // Light_SetAmbient
	L.ambient = vec3(amb, amb, amb);
	model_draw(shell_model, tr, G.global_tex, &L);
	render_set_model(mat4_identity());
}

// ---------------------------------------------------------------- Segmented (23)

#define SEG_POINTS 0x20
#define SEG_LENGTH 0x80      // DAT_0049920c rest length
#define SEG_STIFF 0x800      // DAT_00499210
#define SEG_DAMP 0           // DAT_0055fcf0 (never written)
#define SEG_ITER 1           // DAT_00499208
#define SEG_DRAG 0xe74       // DAT_00499214

typedef struct { int16_t p[3], pad, v[3], pad2; } seg_point_t;

typedef struct {
	bool used;
	int16_t count;       // +0x18 (0x20)
	uint8_t anchored;    // +0x20 the last point is held (property 5)
	uint8_t counter;     // +0x21 update counter: texture column (counter >> 2) & 3
	seg_point_t pts[SEG_POINTS];
	coord_t *base;       // +0x1c the rope's frame (the head's start position, no rotation)
	int16_t inst;        // the head: a render item without a model, animated by the anim word
} segmented_t;

#define MAX_SEGMENTED 8
static segmented_t segs[MAX_SEGMENTED];

static segmented_t *seg_of(task_t *t) { return objstate(t)->extra; }

static void seg_create(task_t *t, void *arg) { // Segmented_Create 0x4585d0
	obj_args_t *a = arg;
	obj_args_t a2 = a ? *a : (obj_args_t){OBJ_NONE, -1, NULL, NULL, NULL, NULL};
	// the head: a render item at the record (RotPackToMatrix), which the animation moves
	a2.inst = inst_create(NULL, NULL, NULL);
	if (a2.inst >= 0 && a2.rec) {
		coord_t *c = gInstances[a2.inst].coord;
		fw_from_packed(a2.rec->rot, &c->local);
		c->local.t[0] = a2.rec->pos.x;
		c->local.t[1] = a2.rec->pos.y;
		c->local.t[2] = a2.rec->pos.z;
	}
	if (a2.inst >= 0 && a) eng_map_set_mob_model(a->id, a2.inst); // FUN_004497a0
	animobj_create(t, &a2);                                       // AnimState_Init
	segmented_t *s = NULL;
	for (int i = 0; i < MAX_SEGMENTED && !s; i++) {
		if (!segs[i].used) s = &segs[i];
	}
	objstate(t)->extra = s;
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->used = true;
	s->inst = a2.inst;
	s->count = SEG_POINTS;
	s->anchored = 1;
	for (int i = 0, y = 0; i < s->count; i++, y -= SEG_LENGTH) s->pts[i].p[1] = (int16_t)y;
	s->base = coord_alloc(NULL);
	if (s->base && a2.inst >= 0) memcpy(s->base->local.t, gInstances[a2.inst].coord->local.t, sizeof(int32_t) * 3);
}

static void seg_destroy(task_t *t) { // 0x458720
	segmented_t *s = seg_of(t);
	animobj_destroy(t); // FUN_00455a50: save the anim word, detach
	if (!s) return;
	if (s->inst >= 0) {
		ObjId id = objstate(t)->id;
		if ((id & 0x3000000) == 0x1000000) eng_map_set_mob_model(id, -1);
		inst_free(s->inst);
	}
	coord_free(s->base);
	s->used = false;
	objstate(t)->extra = NULL;
}

static void seg_update(task_t *t) { // 0x4582f0
	segmented_t *s = seg_of(t);
	objstate_t *o = objstate(t);
	animobj_update(t);                                   // AnimState_Step
	if (o->flags & 0x10) animobj_render(t, t->wake - t->type->period); // AnimState_Apply(state, 0)
	if (!s || !s->base || s->inst < 0) return;
	s->counter++;
	seg_point_t *p = s->pts;
	const FwMatrix *hw = coord_world(gInstances[s->inst].coord), *bw = coord_world(s->base);
	int16_t head[3] = {(int16_t)(hw->t[0] - bw->t[0]), (int16_t)(hw->t[1] - bw->t[1]), (int16_t)(hw->t[2] - bw->t[2])};
	for (int a = 0; a < 3; a++) p[0].v[a] = (int16_t)((head[a] - p[0].p[a]) / SEG_ITER);
	for (int it = 0; it < SEG_ITER; it++) {
		for (int i = 1; i < s->count; i++) { // springs between neighbours
			seg_point_t *q = &p[i - 1], *r = &p[i];
			cvec3_t d = {q->p[0] - r->p[0], q->p[1] - r->p[1], q->p[2] - r->p[2]};
			cvec3_t dv = {q->v[0] - r->v[0], q->v[1] - r->v[1], q->v[2] - r->v[2]};
			int32_t l = cvec_normalise(&d);
			int32_t dot = cvec_dot(d, dv);
			d = cvec_scale(d, (((l - SEG_LENGTH) * SEG_STIFF) >> 12) - ((-dot * SEG_DAMP) >> 12));
			r->v[0] = (int16_t)(r->v[0] + d.x); r->v[1] = (int16_t)(r->v[1] + d.y); r->v[2] = (int16_t)(r->v[2] + d.z);
			if (i - 1 != 0) {
				q->v[0] = (int16_t)(q->v[0] - d.x); q->v[1] = (int16_t)(q->v[1] - d.y); q->v[2] = (int16_t)(q->v[2] - d.z);
			}
		}
		memcpy(p[0].p, head, sizeof(head));
		int end = s->count;
		if (s->anchored) {
			end--;
			p[end].v[0] = p[end].v[1] = p[end].v[2] = 0;
		}
		for (int i = 1; i < end; i++) {
			for (int a = 0; a < 3; a++) {
				p[i].v[a] = (int16_t)((SEG_DRAG * p[i].v[a]) >> 12);
				p[i].p[a] = (int16_t)(p[i].p[a] + p[i].v[a]);
			}
		}
	}
}

static void seg_set_anchor(ObjId id, intptr_t v) { // 0x4587d0: property 5
	objstate_t *o = objstate_from_id(id);
	segmented_t *s = o ? o->extra : NULL;
	if (s) s->anchored = (uint8_t)v;
}

static void seg_set_end(ObjId id, intptr_t v) { // 0x4587f0: property 0 = the last point (world)
	objstate_t *o = objstate_from_id(id);
	segmented_t *s = o ? o->extra : NULL;
	const int32_t *p = (const int32_t *)v;
	if (!s || !p || !s->base) return;
	const FwMatrix *b = coord_world(s->base);
	seg_point_t *e = &s->pts[s->count - 1];
	for (int a = 0; a < 3; a++) e->p[a] = (int16_t)(p[a] - b->t[a]);
}

// FUN_0045bad0: a camera-facing additive strip of global texture 3 through the visible runs of points,
// 256 units wide, u ping-ponging 0..1 every four points, v the quarter selected by the update counter
static void seg_draw(segmented_t *s) {
	const texture_t *tex = fx_global_tex(3);
	if (!tex || !s->base) return;
	fx_xform_t x;
	fx_xform_world(coord_world(s->base), &x);
	int n = s->count;
	float sx[SEG_POINTS], sy[SEG_POINTS];
	int32_t sz[SEG_POINTS];
	int runs[8][2], nruns = 0, cur = 0;
	runs[0][0] = runs[0][1] = 0;
	for (int i = 0; i < n; i++) {
		float z;
		bool ok = fx_rtp(&x, vec3(s->pts[i].p[0], s->pts[i].p[1], s->pts[i].p[2]), &sx[i], &sy[i], &z);
		sx[i] = (float)(int16_t)sx[i];
		sy[i] = (float)(int16_t)sy[i];
		sz[i] = ((int32_t)z >> 2) << 2;
		if (ok && sz[i] > 0x7f) {
			if (++runs[cur][1] == 2) {
				nruns++;
				runs[cur][0] = i - 1;
			}
		} else {
			if (runs[cur][1] > 1) {
				if (nruns == 8) break;
				cur++;
			}
			runs[cur][1] = 0;
		}
	}
	if (!nruns) return;
	float tu = 0, du = 0.25f;
	int k4 = 4;
	float v0 = (float)((s->counter >> 2) & 3) * 0.25f, v1 = v0 + 0.25f;
	for (int r = 0; r < nruns; r++) {
		int first = runs[r][0], cnt = runs[r][1];
		float px[2][SEG_POINTS], py[2][SEG_POINTS], us[SEG_POINTS];
		float pdx = 0, pdy = 0;
		for (int j = 0; j < cnt; j++) {
			int i = first + j;
			us[j] = tu;
			tu += du;
			if (--k4 == 0) {
				k4 = 4;
				du = -du;
			}
			float dx, dy;
			if (j == cnt - 1) { dx = sx[i] - sx[i - 1]; dy = sy[i] - sy[i - 1]; }
			else { dx = sx[i + 1] - sx[i]; dy = sy[i + 1] - sy[i]; }
			float ax = dx, ay = dy;
			if (j != 0) { ax = (pdx + dx) * 0.5f; ay = (pdy + dy) * 0.5f; }
			float w = (float)((int32_t)fxc.h << 7) / (float)sz[i], ox, oy;
			float l2 = ax * ax + ay * ay;
			if (l2 <= 0.1f) { ox = 0; oy = w; }
			else { float k = w / sqrtf(l2); ox = k * ax; oy = k * ay; }
			px[0][j] = sx[i] + oy; py[0][j] = sy[i] - ox;
			px[1][j] = sx[i] - oy; py[1][j] = sy[i] + ox;
			pdx = dx;
			pdy = dy;
		}
		// untwist: where an edge's quad folds over, both sides of the fold meet at their average
		for (int side = 0; side < 2; side++) {
			int fold[SEG_POINTS + 1] = {0};
			for (int j = 0; j + 1 < cnt; j++) {
				float a;
				if (side == 0) { // A = v0(j), B = v0(j+1), C = v1(j+1): cross(A - B, C - B)
					a = (px[0][j] - px[0][j + 1]) * (py[1][j + 1] - py[0][j + 1]) -
						(py[0][j] - py[0][j + 1]) * (px[1][j + 1] - px[0][j + 1]);
				} else {         // D = v1(j), E = v0(j+1), F = v1(j+1): cross(E - F, D - F)
					a = (px[0][j + 1] - px[1][j + 1]) * (py[1][j] - py[1][j + 1]) -
						(py[0][j + 1] - py[1][j + 1]) * (px[1][j] - px[1][j + 1]);
				}
				fold[j + 1] = 0;
				if (a > 0) fold[j] = fold[j + 1] = 1;
			}
			for (int j = 0; j < cnt;) {
				if (!fold[j]) { j++; continue; }
				float mx = 0, my = 0;
				int m = 0;
				while (j + m < cnt && fold[j + m]) { mx += px[side][j + m]; my += py[side][j + m]; m++; }
				for (int q = 0; q < m; q++) { px[side][j + q] = mx / m; py[side][j + q] = my / m; }
				j += m;
			}
		}
		fxv_t v[SEG_POINTS * 2];
		uint16_t idx[SEG_POINTS * 2];
		for (int j = 0; j < cnt; j++) {
			int i = first + j;
			v[j * 2] = (fxv_t){fx_unproject(px[0][j], py[0][j], (float)sz[i]), us[j], v0, 0xffffffffu};
			v[j * 2 + 1] = (fxv_t){fx_unproject(px[1][j], py[1][j], (float)sz[i]), us[j], v1, 0xffffffffu};
			idx[j * 2] = (uint16_t)(j * 2);
			idx[j * 2 + 1] = (uint16_t)(j * 2 + 1);
		}
		fx_queue_strip(v, idx, cnt * 2, tex, FX_ADD, false, FX_LAYER_3D);
	}
}

// ---------------------------------------------------------------- texture animation (0x444370..)

#define TEXANIM_SLOTS 16

typedef struct {
	uint32_t mode;       // 0 free, 1 loop | 0x10, 2 ping-pong | 0x10, 3 plasma
	uint16_t period;     // ms per frame
	uint8_t frames, cur;
	int tex;
	texture_t orig[64];  // the entry's frames in file order (the shown frame is swapped into 0)
	uint32_t *src;       // plasma: the texture as loaded
} texanim_t;

static texanim_t anims[TEXANIM_SLOTS];
static double plasma_phase[4]; // DAT_0054ae80..98, shared by every plasma texture

// FwTexFileAnimatePlasma 0x433a40 -> 0x4409d0, once per presented frame: where the texture is black, a
// purple plasma from two sine tables (red = v / 2, blue = v), the rest stays as loaded
static void plasma_update(texanim_t *a) {
	texfile_t *tf = G.level ? G.level->textures : NULL;
	if (!tf || a->tex < 0 || a->tex >= tf->count || !a->src) return;
	texture_t *t = &tf->entries[a->tex].frame[0];
	int w = t->width, h = t->height;
	if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return;
	int16_t ta[1024], tb[1024];
	double fx = 6.284 / w, fy = 6.284 / h;
	double p2 = plasma_phase[2], p3 = plasma_phase[3];
	for (int x = 0; x < w; x++, p2 += fx, p3 += 2 * fx) ta[x] = (int16_t)(32.0 * sin(p2) + 32.0 * sin(p3));
	double p0 = plasma_phase[0], p1 = plasma_phase[1];
	for (int y = 0; y < h; y++, p0 += 2 * fy, p1 += fy) tb[y] = (int16_t)(32.0 * sin(p0) + 32.0 * sin(p1));
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint32_t s = a->src[y * w + x];
			// a 16-bit 5:6:5 texel of 0: black
			if (((s >> 19) & 0x1f) || ((s >> 10) & 0x3f) || ((s >> 3) & 0x1f)) {
				t->pixels[y * w + x] = s;
				continue;
			}
			int v = ta[((tb[y] >> 2) + x + w) % w] + 0x80 + tb[((ta[x] >> 2) + y + h) % h];
			int r = min((v >> 1) & 0xf8, 0xff), b = min(v & 0x1f8, 0xff);
			t->pixels[y * w + x] = 0xff000000u | (uint32_t)r << 16 | (uint32_t)b;
		}
	}
	plasma_phase[0] -= fy * 1.9;
	plasma_phase[1] -= fy * -1.1;
	plasma_phase[2] -= fx * 1.2;
	plasma_phase[3] -= fx * -2.2;
}

static void show_frame(texanim_t *a, int f) { // FUN_00433960: frame f becomes the texture's frame 0
	texfile_t *tf = G.level ? G.level->textures : NULL;
	if (!tf || a->tex < 0 || a->tex >= tf->count) return;
	tex_entry_t *e = &tf->entries[a->tex];
	for (int i = 0; i < e->frames && i < 64; i++) e->frame[i] = a->orig[i];
	texture_t t = e->frame[0];
	e->frame[0] = e->frame[f];
	e->frame[f] = t;
}

void TexAnimPlay(int32_t texture, int32_t mode, int32_t period) { // 0x444370 -> 0x444940
	int i = 0;
	while (i < TEXANIM_SLOTS - 1 && anims[i].mode) i++;
	texanim_t *a = &anims[i];
	free(a->src);
	a->src = NULL;
	a->mode = (uint32_t)mode | 0x10;
	a->tex = texture;
	a->period = (uint16_t)period;
	a->cur = 0;
	a->frames = 0;
	texfile_t *tf = G.level ? G.level->textures : NULL;
	if (mode == 3) { // FwTexFileSetPlasma 0x433a10: a dynamic copy of the texture
		a->mode = 3;
		free(a->src);
		a->src = NULL;
		if (tf && texture >= 0 && texture < tf->count && tf->entries[texture].frame) {
			const texture_t *t = &tf->entries[texture].frame[0];
			a->src = malloc(sizeof(uint32_t) * t->width * t->height);
			memcpy(a->src, t->pixels, sizeof(uint32_t) * t->width * t->height);
		}
		return;
	}
	if (tf && texture >= 0 && texture < tf->count && tf->entries[texture].frames > 1) {
		tex_entry_t *e = &tf->entries[texture];
		a->frames = (uint8_t)min(e->frames, 64);
		for (int f = 0; f < a->frames; f++) a->orig[f] = e->frame[f];
	}
}

void TexAnimStop(int32_t texture) { // 0x444390 -> 0x4449b0
	for (int i = 0; i < TEXANIM_SLOTS; i++) {
		if (anims[i].mode && anims[i].tex == texture) {
			anims[i].mode = 0;
			return;
		}
	}
}

void fx_texanim_update(uint32_t now) { // TexAnim_Update 0x444880 (PrePresent_TexAnim 0x413910)
	for (int i = 0; i < TEXANIM_SLOTS; i++) {
		texanim_t *a = &anims[i];
		if (a->mode == 3) {
			plasma_update(a);
			continue;
		}
		if (!a->mode || a->frames < 2 || !a->period) continue;
		uint32_t f;
		if ((a->mode & 3) == 1) {
			f = (now / a->period) % a->frames;
		} else if ((a->mode & 3) == 2) {
			f = (now / a->period) % (uint32_t)(a->frames * 2 - 2);
			if ((int)(a->frames - 1) <= (int)f) f = a->frames * 2 - f - 2;
		} else {
			f = 0;
		}
		if (f != a->cur) {
			show_frame(a, (int)f);
			a->cur = (uint8_t)f;
		}
	}
}

// SetBumpMapping 0x4443b0: only on devices with D3DTOP_BUMPENVMAP (DAT_00536368, the bump-capable
// model path PcpRender_DrawV12Bump); the rewrite renders like a device without it, as the original
// does there: nothing happens.
void SetBumpMapping(int32_t texture, int32_t height, int32_t normal) {
	(void)texture; (void)height; (void)normal;
}

// ---------------------------------------------------------------- registration / per frame

void fx_misc_register(void) {
	ObjectType_Register(OT_SHORTFX, shortfx_update, shortfx_render, shortfx_create, shortfx_destroy, 0);
	ObjectType_Register(OT_SEGMENTED, seg_update, NULL, seg_create, seg_destroy, 0);
	ObjectProperty_RegisterSet(OT_SEGMENTED, 5, seg_set_anchor);
	ObjectProperty_RegisterSet(OT_SEGMENTED, 0, seg_set_end);
}

void fx_misc_reset(void) {
	memset(shortfx, 0, sizeof(shortfx));
	memset(segs, 0, sizeof(segs));
	for (int i = 0; i < TEXANIM_SLOTS; i++) free(anims[i].src);
	memset(anims, 0, sizeof(anims)); // TexAnim_Reset 0x444860
	if (shell_model) {
		model_free(shell_model);
		shell_model = NULL;
	}
}

void fx_misc_draw(void) {
	for (int i = 0; i < MAX_SEGMENTED; i++) {
		if (segs[i].used) seg_draw(&segs[i]); // in the always-drawn group (DrawGroup_Add)
	}
}

void effects_shockwave(const int32_t pos[3]) {
	obj_args_t a = {OBJ_NONE, -1, NULL, NULL, (void *)pos, NULL};
	Objects_Create(OT_SHORTFX, &a);
}
