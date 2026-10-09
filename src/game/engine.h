// The game runtime that ties the ported subsystems together: transform nodes,
// model instances and the per-frame render list, the object system (map
// objects, mobs, object types, properties), the level being played and the
// helpers the ports call through their eng_* / host adapters.
#ifndef ENGINE_H
#define ENGINE_H

#include "../types.h"
#include "../level.h"
#include "../render.h"
#include "mission_api.h"
#include "sched.h"
#include "events.h"
#include "physics.h"

// ---------------------------------------------------------------- fixed point helpers
static inline mat4_t fw_to_mat4(const FwMatrix *m) {
	mat4_t r = mat4_identity();
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 3; col++) {
			r.m[col * 4 + row] = m->m[row][col] / 4096.0f;
		}
	}
	r.m[12] = (float)m->t[0];
	r.m[13] = (float)m->t[1];
	r.m[14] = (float)m->t[2];
	return r;
}
void fw_identity(FwMatrix *m);
void fw_mul(const FwMatrix *a, const FwMatrix *b, FwMatrix *out);     // rotation a*b (>> 12)
void fw_compose(const FwMatrix *parent, const FwMatrix *local, FwMatrix *out); // CompMatrix
void fw_from_packed(uint32_t packed_rot, FwMatrix *out);             // RotPackToMatrix 0x456bd0
void fw_rotate_y(FwMatrix *m, int32_t angle);                        // m = m * Ry(angle)
void fw_apply(const FwMatrix *m, const int32_t v[3], int32_t out[3]); // rotation only

// ---------------------------------------------------------------- transform nodes (Coord_*)
typedef struct coord_t {
	FwMatrix local;            // node +0x0c (phys_matrix_t layout): relative to the parent
	struct coord_t *parent;    // node +0x6c
	FwMatrix world;            // computed by coord_world()
	bool used;
} coord_t;

coord_t *coord_alloc(coord_t *parent);
void coord_free(coord_t *c);
const FwMatrix *coord_world(coord_t *c);

// ---------------------------------------------------------------- model instances (render items)
#define MAX_INSTANCES 0x600

typedef struct model_inst_t model_inst_t;
typedef void (*inst_draw_fn)(model_inst_t *mi);

struct model_inst_t {
	bool used;
	int16_t next_part;          // multi-part model: next instance of the chain (-1)
	const model_t *model;
	texfile_t *tex;
	coord_t *coord;             // transform (owned unless shared_coord)
	bool shared_coord;
	int16_t zone;               // record zone (-1 exterior lights on)
	int32_t tag;                // render item +8 (lander weapon mount type)
	inst_draw_fn draw;          // custom draw callback (NULL = plain model draw)
	void *user;
	uint32_t tint;              // 0 = none (team colour)
	bool hidden;
};

extern model_inst_t gInstances[MAX_INSTANCES];
int16_t inst_create(const model_t *m, texfile_t *tex, coord_t *parent);
void inst_free(int16_t i);
void inst_queue(int16_t i);          // draw this instance (and its parts) this frame
void inst_draw_now(int16_t i);       // draw immediately with the current lighting
void render_list_reset(void);
void render_list_run(void);

// global models (misc\objects.ppm with misc\global textures)
const model_t *global_model(int32_t index);

// ---------------------------------------------------------------- the level being played
typedef struct {
	level_t *level;
	phys_world_t world;
	phys_mesh_t *zone_mesh, *tile_mesh;
	texfile_t *global_tex;
	ppm_t *global_models;
	int world_index, mission_index;
	const mission_script_t *script;
	int32_t mission_result;
	bool game_over_shown;
	// lighting for this frame
	lighting_t base_light;
	vec3_t cam_pos;
} game_t;

extern game_t G;

// ---------------------------------------------------------------- objects
#define MAX_OBJECT_TYPES 40
#define OBJ_FLAG_AWAKE 1
#define OBJ_FLAG_DEAD  2

typedef struct {
	ObjId id;
	int16_t inst;          // model instance (first part) or -1
	map_rec_t *rec;        // record (NULL for dynamic objects)
	int32_t *params;       // &pool[rec->param] (NULL for dynamic objects)
	void *extra;           // type-specific create argument (dynamic objects)
	task_t *proc;          // out: the process
} obj_args_t;

typedef intptr_t (*prop_get_fn)(ObjId id);
typedef void (*prop_set_fn)(ObjId id, intptr_t value);

typedef struct {
	task_type_t task;      // update/render/init(create)/destroy; period
	int cls;               // 0 none, 1 static model instance, 2 dynamic
	prop_get_fn get[12];
	prop_set_fn set[12];
	bool registered;
} object_type_t;

extern object_type_t gObjectTypes[MAX_OBJECT_TYPES];

void ObjectType_Register(int type, void (*update)(task_t *), void (*render)(task_t *, uint32_t),
	void (*create)(task_t *, void *), void (*destroy)(task_t *), int cls);
void ObjectType_SetFrequency(int type, uint32_t period_ms);
void ObjectProperty_RegisterGet(int type, int prop, prop_get_fn fn);
void ObjectProperty_RegisterSet(int type, int prop, prop_set_fn fn);
int ObjectType_GetClass(int type);

ObjId Objects_Create(int type, obj_args_t *args);       // 0x449820 (args NULL = dynamic)
void Objects_GetData(ObjId id, int32_t **params, task_t **proc);
map_rec_t *Object_GetRecord(ObjId id);
int16_t Object_GetInstance(ObjId id);
bool Object_IsAwake(ObjId id);

void Map_Reset(void);                    // per level, after the level is loaded
void Map_UpdateProximity(vec3_t focus, float radius); // wake/sleep statics and objects
void Map_SleepAll(void);
void Objects_QueueVisible(vec3_t from, float distance); // queue awake model instances
void Objects_SetupCollision(phys_world_t *w);           // statics/objects/mobs in the physics queries
void Objects_FreeCollision(void);                       // their collision meshes (before the models go)

extern ObjId gCameraObject;   // focus object (lander / camera target)

// ---------------------------------------------------------------- misc runtime
void game_lighting_for(vec3_t center, float radius, lighting_t *out);
uint32_t game_now(void);
bool game_is_paused(void);    // gPaused (the pause menu is open)

#endif
