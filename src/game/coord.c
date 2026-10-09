// Transform nodes, model instances (render items) and the per-frame render list.
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "camera.h"

// ---------------------------------------------------------------- fixed-point matrices

void fw_identity(FwMatrix *m) {
	memset(m, 0, sizeof(*m));
	m->m[0][0] = m->m[1][1] = m->m[2][2] = 0x1000;
}

void fw_mul(const FwMatrix *a, const FwMatrix *b, FwMatrix *out) { // MulMatrix0
	FwMatrix r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			int32_t s = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
			r.m[i][j] = (int16_t)(s >> 12);
		}
	}
	r.pad = 0;
	memcpy(r.t, out->t, sizeof(r.t));
	*out = r;
}

void fw_apply(const FwMatrix *m, const int32_t v[3], int32_t out[3]) { // ApplyMatrixLV
	int32_t r[3];
	for (int i = 0; i < 3; i++) {
		int64_t s = (int64_t)m->m[i][0] * v[0] + (int64_t)m->m[i][1] * v[1] + (int64_t)m->m[i][2] * v[2];
		r[i] = (int32_t)(s >> 12);
	}
	memcpy(out, r, sizeof(r));
}

void fw_compose(const FwMatrix *parent, const FwMatrix *local, FwMatrix *out) { // CompMatrix
	FwMatrix r;
	int32_t t[3];
	fw_apply(parent, local->t, t);
	r.t[0] = t[0] + parent->t[0];
	r.t[1] = t[1] + parent->t[1];
	r.t[2] = t[2] + parent->t[2];
	fw_mul(parent, local, &r);
	r.t[0] = t[0] + parent->t[0];
	r.t[1] = t[1] + parent->t[1];
	r.t[2] = t[2] + parent->t[2];
	*out = r;
}

static int32_t sext(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }

void fw_from_packed(uint32_t packed, FwMatrix *m) { // RotPackToMatrix 0x456bd0: M = Rz*Ry*Rx
	int32_t ax = sext(packed >> 21, 11) * 2, ay = sext(packed >> 10, 11) * 2, az = sext(packed, 10) * 4;
	int32_t sx = fx_rsin(ax), cx = fx_rcos(ax), sy = fx_rsin(ay), cy = fx_rcos(ay);
	int32_t sz = fx_rsin(az), cz = fx_rcos(az);
	int32_t sxsy = (sx * sy) >> 12, cxsy = (cx * sy) >> 12;
	m->m[0][0] = (int16_t)((cz * cy) >> 12);
	m->m[0][1] = (int16_t)(((sxsy * cz) >> 12) - ((cx * sz) >> 12));
	m->m[0][2] = (int16_t)(((cxsy * cz) >> 12) + ((sx * sz) >> 12));
	m->m[1][0] = (int16_t)((cy * sz) >> 12);
	m->m[1][1] = (int16_t)(((sxsy * sz) >> 12) + ((cx * cz) >> 12));
	m->m[1][2] = (int16_t)(((cxsy * sz) >> 12) - ((sx * cz) >> 12));
	m->m[2][0] = (int16_t)-sy;
	m->m[2][1] = (int16_t)((cy * sx) >> 12);
	m->m[2][2] = (int16_t)((cy * cx) >> 12);
	m->pad = 0;
}

void fw_rotate_y(FwMatrix *m, int32_t a) { // m = m * Ry(a)
	FwMatrix r;
	fw_identity(&r);
	int32_t s = fx_rsin(a), c = fx_rcos(a);
	r.m[0][0] = (int16_t)c;
	r.m[0][2] = (int16_t)s;
	r.m[2][0] = (int16_t)-s;
	r.m[2][2] = (int16_t)c;
	fw_mul(m, &r, m);
}

// ---------------------------------------------------------------- transform nodes

#define MAX_COORDS 0x800
static coord_t coords[MAX_COORDS];

coord_t *coord_alloc(coord_t *parent) { // Coord_Alloc 0x431350
	for (int i = 0; i < MAX_COORDS; i++) {
		if (!coords[i].used) {
			coord_t *c = &coords[i];
			memset(c, 0, sizeof(*c));
			c->used = true;
			c->parent = parent;
			fw_identity(&c->local);
			fw_identity(&c->world);
			return c;
		}
	}
	return NULL;
}

void coord_free(coord_t *c) {
	if (c) {
		c->used = false;
	}
}

const FwMatrix *coord_world(coord_t *c) { // Coord_GetWorldMatrix 0x4314f0
	if (!c->parent) {
		c->world = c->local;
	} else {
		fw_compose(coord_world(c->parent), &c->local, &c->world);
	}
	return &c->world;
}

// ---------------------------------------------------------------- model instances

model_inst_t gInstances[MAX_INSTANCES];

int16_t inst_create(const model_t *m, texfile_t *tex, coord_t *parent) {
	for (int i = 0; i < MAX_INSTANCES; i++) {
		if (!gInstances[i].used) {
			model_inst_t *mi = &gInstances[i];
			memset(mi, 0, sizeof(*mi));
			mi->used = true;
			mi->next_part = -1;
			mi->model = m;
			mi->tex = tex;
			mi->coord = coord_alloc(parent);
			mi->zone = 0;
			return (int16_t)i;
		}
	}
	return -1;
}

void inst_free(int16_t i) {
	while (i >= 0 && i < MAX_INSTANCES && gInstances[i].used) {
		model_inst_t *mi = &gInstances[i];
		int16_t next = mi->next_part;
		if (!mi->shared_coord) {
			coord_free(mi->coord);
		}
		mi->used = false;
		i = next;
	}
}

// global models: misc\objects.ppm entries (gGlobalModels)
const model_t *global_model(int32_t index) {
	if (!G.global_models || index < 0 || index >= G.global_models->count ||
		!G.global_models->entries[index].num_parts) {
		return NULL;
	}
	return G.global_models->entries[index].parts[0];
}

// ---------------------------------------------------------------- render list

static int16_t render_list[MAX_INSTANCES];
static int render_count;

void render_list_reset(void) { render_count = 0; }

void inst_queue(int16_t i) {
	if (i >= 0 && render_count < MAX_INSTANCES) {
		render_list[render_count++] = i;
	}
}

void inst_draw_now(int16_t i) {
	for (; i >= 0 && i < MAX_INSTANCES && gInstances[i].used; i = gInstances[i].next_part) {
		model_inst_t *mi = &gInstances[i];
		if (mi->hidden || !mi->model) {
			continue;
		}
		mat4_t t = fw_to_mat4(coord_world(mi->coord));
		vec3_t c = mat4_mul_point(t, vec3_mulf(vec3_add(mi->model->bbox_min, mi->model->bbox_max), 0.5f));
		vec3_t d = vec3_sub(c, G.cam_pos);
		float vd = G.level ? G.level->view_distance * 1.25f : 20000;
		if (vec3_dot(d, d) > (vd + mi->model->radius) * (vd + mi->model->radius)) {
			continue;
		}
		lighting_t L;
		game_lighting_for(c, mi->model->radius, &L);
		model_draw(mi->model, t, mi->tex, &L);
	}
}

void render_list_run(void) {
	for (int k = 0; k < render_count; k++) {
		model_inst_t *mi = &gInstances[render_list[k]];
		if (!mi->used) {
			continue;
		}
		if (mi->draw) {
			mi->draw(mi);
		} else {
			inst_draw_now(render_list[k]);
		}
	}
	render_count = 0;
}
