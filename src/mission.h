// mission.dat: map objects, animations, environment, surface classes, lights.
// See re/specs/mission.md.
#ifndef MISSION_H
#define MISSION_H

#include "types.h"
#include "terrain.h"

#define MISSION_MAGIC 0xB16BA115

enum {
	OBJ_TURRET = 0, OBJ_LANDER = 1, OBJ_PROJECTILE = 2, OBJ_ANIM_OBJECT = 3, OBJ_TRIGGER_BOX = 4,
	OBJ_ANIM_CAM = 5, OBJ_CAMERA = 6, OBJ_FLY_CAM = 7, OBJ_DESTRUCTIBLE = 8, OBJ_EXPLOSION = 9,
	OBJ_SWITCH = 10, OBJ_TYPE11 = 11, OBJ_POD = 12, OBJ_PARTICLE_EMITTER = 13, OBJ_ANIM_TURRET = 14,
	OBJ_ANIM_DESTRUCTIBLE = 15, OBJ_FORCE_BOX = 16, OBJ_FUEL_DUMP = 17, OBJ_TYPE18 = 18,
	OBJ_PICKUP = 19, OBJ_INERTIAL_FRAME = 20, OBJ_AMBIENT_FX = 21, OBJ_LIGHT_OBJECT = 22,
	OBJ_SEGMENTED = 23, OBJ_MARKER = 25, OBJ_TRIGGER_PLANE = 26, OBJ_SHORT_FX = 27,
	OBJ_TYPE_MAX = 40
};

// Record tables
enum { REC_STATIC, REC_OBJECT, REC_MOB };

typedef struct {
	ivec3_t pos;
	uint32_t rot;   // packed rotation, see rot_unpack()
	int16_t zone;   // 0/-2 compute, -1 exterior, -3 none, >0 zone index
	int16_t model;  // objects.ppm entry, -1 = none
	uint8_t type;   // object type (objects and mobs)
	int16_t param;  // index into the parameter pool
} map_rec_t;

typedef struct {
	uint16_t radius, aux;
	vec3_t center;  // bounding sphere centre (model space)
	ivec3_t pos;    // part transform (multi-part models)
	uint32_t rot;
} model_part_t;

typedef struct {
	int num_parts;
	model_part_t *parts;
} model_info_t;

typedef struct {
	int32_t x, y, z;
	uint32_t rot;
} anim_key_t;

typedef struct {
	int num_frames, num_parts;
	anim_key_t *keys; // num_frames * num_parts
} anim_t;

typedef struct { vec3_t dir; rgba_t color; } light_dir_t;
typedef struct { ivec3_t pos; rgba_t color; int32_t inner, falloff; } light_point_t;
typedef struct { ivec3_t pos; uint32_t rot; rgba_t color; int32_t inner, falloff; uint16_t cone_inner, cone_outer; } light_spot_t;
typedef struct { ivec3_t pos; uint32_t rot; rgba_t color; int32_t box[8]; } light_box_t;

typedef struct {
	int num_rec[3];
	map_rec_t *rec[3];   // statics, objects, mobs
	int num_pool;
	int32_t *pool;
	int num_models;
	model_info_t *models;
	int16_t *destroy_step;
	int16_t (*bbox)[6];
	int32_t projectile_model[2];

	int num_anims;
	anim_t *anims;

	rgba_t interior, exterior; // ambient + fog colours
	int32_t fog_near, fog_far;

	int num_surface;
	uint8_t *surface_class;

	int num_lights[4]; // directional, point, spot, box
	light_dir_t *dir_lights;
	light_point_t *point_lights;
	light_spot_t *spot_lights;
	light_box_t *box_lights;
} mission_t;

// model_parts[m] = sub-part count of objects.ppm entry m (num_models entries)
mission_t *mission_load(const char *path, int num_models, const int *model_parts);
void mission_free(mission_t *m);

// 4096 = 360 degrees
void rot_unpack(uint32_t packed, int *ax, int *ay, int *az);
mat4_t rot_matrix(uint32_t packed); // Rz * Ry * Rx (rotation only)
vec3_t packed_vec(uint32_t packed); // packedCentre layout (x4 each)
int32_t mission_object_id(int table, int type, int index);

#endif
