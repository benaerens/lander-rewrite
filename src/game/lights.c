// Dynamic lights (Light_Create 0x436bd0 and friends): lights attached to a
// transform node, e.g. the lander's headlight and engine glow, light objects.
// They are added to the lighting of every model they can reach.
#include <string.h>

#include "engine.h"
#include "lights.h"

#define MAX_DYN_LIGHTS 64

static dyn_light_t lights[MAX_DYN_LIGHTS];

dyn_light_t *dyn_light_create(int kind, coord_t *coord) { // 0x436bd0 (1 point, 2 spot, 3 dir, 7 box)
	for (int i = 0; i < MAX_DYN_LIGHTS; i++) {
		if (!lights[i].used) {
			dyn_light_t *l = &lights[i];
			memset(l, 0, sizeof(*l));
			l->used = true;
			l->kind = kind;
			l->coord = coord;
			l->color = vec3(1, 1, 1);
			l->dir[2] = 0x1000;
			return l;
		}
	}
	return NULL;
}

void dyn_light_destroy(dyn_light_t *l) {
	if (l) {
		l->used = false;
	}
}

void dyn_lights_reset(void) {
	memset(lights, 0, sizeof(lights));
}

// light position/direction in world space
static void light_world(const dyn_light_t *l, vec3_t *pos, vec3_t *dir) {
	if (l->coord) {
		const FwMatrix *w = coord_world(l->coord);
		int32_t p[3];
		fw_apply(w, l->pos, p);
		*pos = vec3(p[0] + w->t[0], p[1] + w->t[1], p[2] + w->t[2]);
		int32_t d[3];
		fw_apply(w, l->dir, d);
		*dir = vec3(d[0] / 4096.0f, d[1] / 4096.0f, d[2] / 4096.0f);
	} else {
		*pos = vec3(l->pos[0], l->pos[1], l->pos[2]);
		*dir = vec3(l->dir[0] / 4096.0f, l->dir[1] / 4096.0f, l->dir[2] / 4096.0f);
	}
}

void dyn_lights_add(vec3_t center, float radius, lighting_t *L) {
	for (int i = 0; i < MAX_DYN_LIGHTS; i++) {
		dyn_light_t *l = &lights[i];
		if (!l->used || l->off) {
			continue;
		}
		vec3_t pos, dir;
		light_world(l, &pos, &dir);
		if (l->kind == 7) { // box light (LightObject kind 2): the node's axes, reach as level.c's box lights
			if (L->num_box >= MAX_BOX_LIGHTS) continue;
			const int32_t *e = l->box;
			float ry = (float)max(e[2] + e[3], e[6] + e[7]);
			float reach = sqrtf((float)(e[0] + e[1]) * (e[0] + e[1]) + ry * ry + (float)(e[4] + e[5]) * (e[4] + e[5]));
			vec3_t d = vec3_sub(pos, center);
			if (vec3_dot(d, d) > (reach + radius) * (reach + radius)) continue;
			int k = L->num_box++;
			FwMatrix id;
			fw_identity(&id);
			const FwMatrix *w = l->coord ? coord_world(l->coord) : &id;
			L->box[k].pos = pos;
			for (int a = 0; a < 3; a++) {
				L->box[k].axis[a] = vec3(w->m[0][a] / 4096.0f, w->m[1][a] / 4096.0f, w->m[2][a] / 4096.0f);
			}
			L->box[k].color = l->color;
			for (int a = 0; a < 8; a++) L->box[k].ext[a] = (float)e[a];
			continue;
		}
		float range = (float)l->inner + l->falloff;
		vec3_t d = vec3_sub(pos, center);
		if (vec3_dot(d, d) > (range + radius) * (range + radius)) {
			continue;
		}
		if (l->kind == 2 && L->num_spot < MAX_SPOT_LIGHTS) {
			int k = L->num_spot++;
			L->spot[k].pos = pos;
			L->spot[k].dir = dir;
			L->spot[k].color = l->color;
			L->spot[k].inner = (float)l->inner;
			L->spot[k].falloff = l->falloff > 0 ? (float)l->falloff : 1;
			L->spot[k].cos_inner = cosf(l->cone_inner * PI / 4096);
			L->spot[k].cos_outer = cosf(l->cone_outer * PI / 4096);
		} else if (l->kind != 2 && L->num_point < MAX_POINT_LIGHTS) {
			int k = L->num_point++;
			L->point_pos[k] = pos;
			L->point_color[k] = l->color;
			L->point_inner[k] = (float)l->inner;
			L->point_falloff[k] = l->falloff > 0 ? (float)l->falloff : 1;
		}
	}
}
