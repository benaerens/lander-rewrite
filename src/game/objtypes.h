// Map object types (ObjectTypes_RegisterAll 0x448760): the common object state, the animation
// system and the simple types (objtypes.c). Combat types live in weapons.c, visual effects in
// effects.c, pods / force boxes / frames / scripted cameras in physobjs.c.
#ifndef OBJTYPES_H
#define OBJTYPES_H

#include "engine.h"
#include "lander.h"

// Per-process state of a map object, kept in the task's data words (task +0x0c.. in the original).
// Types that need more keep it elsewhere and point at it with `extra`.
typedef struct {
	ObjId id;          // +0x0c
	int16_t inst;      // +0x10 model instance (-1 none)
	uint8_t flags;     // +0x12 anim flags: 0-1 mode, 2 reverse, 3 attached, 4 playing, 5 first part only, 6 clone, 7 lerp rot
	uint8_t anim;      // +0x13 anim index
	uint16_t frame;    // +0x14
	uint16_t pad;
	map_rec_t *rec;    // record (NULL for dynamic objects)
	int32_t *params;   // pool words of the record
	coord_t *coord;    // own transform (triggers, markers, lights)
	uint32_t mask;     // trigger inside bits / connected lander
	void *extra;       // type-specific
} objstate_t;

objstate_t *objstate(task_t *t);
objstate_t *objstate_from_id(ObjId id);          // NULL when the object has no process
void objstate_init(task_t *t, obj_args_t *a);    // id, inst, rec, params from the create args
coord_t *objstate_coord(objstate_t *o);          // the first part's node, else o->coord

// generic properties and callbacks
void objprops_common(int type);                  // get/set 0 position, 2 matrix
void objprops_destructible(int type, int word);  // 3 hp, 4 armour, 9 explosion (u8s of pool word `word`), 8
void objprop_ignore(ObjId id, intptr_t value);   // a setter that does nothing
void objtype_no_update(task_t *t);
void objtype_plain_create(task_t *t, void *arg); // objstate_init only
void objtypes_register_inert(int type);          // inert placeholder: common props, setters ignored

// the animation system (AnimState_* 0x455090-0x455f70) as object-type callbacks: the pool's first
// word is the anim word {flags, anim, frameTime/10}
void animobj_create(task_t *t, void *arg);
void animobj_update(task_t *t);
void animobj_render(task_t *t, uint32_t time);
void animobj_destroy(task_t *t);

void objtypes_register_all(void);     // all types but the lander and the chase camera
void objtypes_reset_mission(void);

#endif
