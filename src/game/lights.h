#ifndef LIGHTS_H
#define LIGHTS_H

#include "engine.h"

typedef struct {
	bool used, off;
	int kind;            // 1 point, 2 spot, 3 directional, 6 flicker, 7 box
	coord_t *coord;      // attached node (NULL = world)
	int32_t pos[3];      // relative to the node
	int32_t dir[3];      // 4.12, spot axis relative to the node
	vec3_t color;        // 0..1
	int32_t inner, falloff;
	int32_t cone_inner, cone_outer; // 4096 = 360 deg (full angles)
	int32_t box[8];      // kind 7 (Light_SetBox): x in/fall, +y in/fall, z in/fall, -y in/fall; axes = node x/y/z
	uint32_t flags;
} dyn_light_t;

dyn_light_t *dyn_light_create(int kind, coord_t *coord);
void dyn_light_destroy(dyn_light_t *l);
void dyn_lights_reset(void);
void dyn_lights_add(vec3_t center, float radius, lighting_t *L);

#endif
