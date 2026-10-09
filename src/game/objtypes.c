// Map object types (ObjectTypes_RegisterAll 0x448760) and the animation system
// (AnimState 0x455090-0x455f70, Anim_Evaluate 0x443e20). See re/specs/mission.md
// §4-5 and mission-api.md §4.
#include <stdlib.h>
#include <string.h>

#include "objtypes.h"
#include "weapons.h"
#include "effects.h"
#include "effects_draw.h"
#include "physobjs.h"
#include "lights.h"
#include "sound.h"

_Static_assert(sizeof(objstate_t) <= sizeof(((task_t *)0)->data), "object state fits in task data");

objstate_t *objstate(task_t *t) { return (objstate_t *)t->data; }

static bool force_any_player_box = true, force_any_player_plane = true; // DAT_00499200 / 00499204

objstate_t *objstate_from_id(ObjId id) {
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	return t && t->type ? objstate(t) : NULL;
}

void objstate_init(task_t *t, obj_args_t *a) {
	objstate_t *o = objstate(t);
	memset(o, 0, sizeof(*o));
	o->id = a ? a->id : OBJ_NONE;
	o->inst = a ? a->inst : -1;
	o->rec = a ? a->rec : NULL;
	o->params = a ? a->params : NULL;
}

coord_t *objstate_coord(objstate_t *o) {
	if (o->inst >= 0 && gInstances[o->inst].used) {
		return gInstances[o->inst].coord;
	}
	return o->coord;
}

// ---------------------------------------------------------------- generic properties

static intptr_t get_position(ObjId id) {
	objstate_t *o = objstate_from_id(id);
	if (o) {
		coord_t *c = objstate_coord(o);
		if (c) return (intptr_t)coord_world(c)->t;
		if (o->rec) return (intptr_t)&o->rec->pos;
	}
	map_rec_t *r = Object_GetRecord(id);
	return r ? (intptr_t)&r->pos : 0;
}

static intptr_t get_matrix(ObjId id) {
	objstate_t *o = objstate_from_id(id);
	if (o) {
		coord_t *c = objstate_coord(o);
		if (c) return (intptr_t)coord_world(c);
	}
	static FwMatrix m;
	map_rec_t *r = Object_GetRecord(id);
	if (!r) return 0;
	fw_from_packed(r->rot, &m);
	m.t[0] = r->pos.x; m.t[1] = r->pos.y; m.t[2] = r->pos.z;
	return (intptr_t)&m;
}

static void set_position(ObjId id, intptr_t v) {
	objstate_t *o = objstate_from_id(id);
	const int32_t *p = (const int32_t *)v;
	coord_t *c = o ? objstate_coord(o) : NULL;
	if (c && p) {
		while (c->parent) c = c->parent; // move the placement node
		c->local.t[0] = p[0]; c->local.t[1] = p[1]; c->local.t[2] = p[2];
	}
}

static void set_matrix(ObjId id, intptr_t v) {
	objstate_t *o = objstate_from_id(id);
	const FwMatrix *m = (const FwMatrix *)v;
	coord_t *c = o ? objstate_coord(o) : NULL;
	if (c && m) {
		while (c->parent) c = c->parent;
		memcpy(c->local.m, m->m, sizeof(m->m));
	}
}

// destructible header: u8 hp, u8 armour, u8 explosionSize, u8 extra (pool word)
static uint8_t *destr(ObjId id, int word) {
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	return params ? (uint8_t *)&params[word] : NULL;
}

static void object_destroyed(ObjId id, int word);

static intptr_t get_hp_w0(ObjId id) { uint8_t *d = destr(id, 0); return d ? d[0] : 0; }
static intptr_t get_armour_w0(ObjId id) { uint8_t *d = destr(id, 0); return d ? d[1] : 0; }
static intptr_t get_hp_w1(ObjId id) { uint8_t *d = destr(id, 1); return d ? d[0] : 0; }
static intptr_t get_armour_w1(ObjId id) { uint8_t *d = destr(id, 1); return d ? d[1] : 0; }
// Destructible_SetHP 0x459290: 0 destroys (again, if it already was: scripts rely on the event)
static void set_hp_w0(ObjId id, intptr_t v) {
	uint8_t *d = destr(id, 0);
	if (d) { d[0] = (uint8_t)v; if (v == 0) object_destroyed(id, 0); }
}
static void set_hp_w1(ObjId id, intptr_t v) {
	uint8_t *d = destr(id, 1);
	if (d) { d[0] = (uint8_t)v; if (v == 0) object_destroyed(id, 1); }
}
static void set_armour_w0(ObjId id, intptr_t v) { uint8_t *d = destr(id, 0); if (d) d[1] = (uint8_t)v; }
static void set_armour_w1(ObjId id, intptr_t v) { uint8_t *d = destr(id, 1); if (d) d[1] = (uint8_t)v; }
static void set_expl_w0(ObjId id, intptr_t v) { uint8_t *d = destr(id, 0); if (d) d[2] = (uint8_t)v; }
static void set_expl_w1(ObjId id, intptr_t v) { uint8_t *d = destr(id, 1); if (d) d[2] = (uint8_t)v; }
static intptr_t get_alive_flags_w0(ObjId id) { return get_hp_w0(id) ? 0x8000 : 0; }

static void object_destroyed(ObjId id, int word) {
	uint8_t *d = destr(id, word);
	Object_Destroyed(id, d ? d[2] : 0); // weapons.c
}

// ---------------------------------------------------------------- animation

static const anim_t *anim_get(int index) {
	const mission_t *m = G.level->mission;
	return index >= 0 && index < m->num_anims ? &m->anims[index] : NULL;
}

static bool f_finished, f_turn_flip, f_wrapped, f_turned; // DAT_0055fc80, fb78, fb74, fc98

static int anim_step_frame(int frame, int last, int step, int flags) { // Anim_StepFrame 0x4550f0
	if (!(flags & 4)) {
		frame += step;
		if ((flags & 3) == 0) {
			if (last < frame) { f_finished = f_turned = true; return last; }
			return frame < 0 ? 0 : frame;
		}
	} else {
		frame -= step;
		if ((flags & 3) == 0) {
			if (frame < 0) { f_finished = f_wrapped = true; return 0; }
			return frame <= last ? frame : last;
		}
	}
	if (last == 0) return 0;
	int q = frame / last, r = frame % last;
	if ((flags & 3) == 1) {
		if (q != 0) f_wrapped = true;
		if (r < 0) {
			if (flags & 4) f_wrapped = true;
			return r + last;
		}
		return r;
	}
	if ((flags & 3) == 2) {
		if (q < 1) {
			if (q < 0) f_wrapped = true;
		} else {
			q &= 1;
			if (q) f_turned = true;
		}
		if (r < 0) {
			if ((q & 1) != 1) { f_turn_flip = true; return -r; }
			return r + last;
		}
		if (q != 0) { f_turn_flip = true; return last - r; }
		return r;
	}
	return q;
}

static int anim_last_frame(const objstate_t *o) { // AnimState_LastFrame 0x4550c0
	const anim_t *a = anim_get(o->anim);
	int n = a ? a->num_frames : 1;
	return (o->flags & 3) == 1 ? n : n - 1;
}

static void anim_raise(objstate_t *o, int type) { // AnimState_RaiseEvent 0x455090
	LanderEvent ev = {type, {(intptr_t)o->id, (intptr_t)o->anim}};
	Event_Raise(&ev);
}

static int part_count(const objstate_t *o) {
	int n = 0;
	for (int16_t i = o->inst; i >= 0; i = gInstances[i].next_part) n++;
	return n;
}

static void anim_attach(objstate_t *o, uint8_t flags, int frame) { // AnimState_Attach 0x455200
	int n = (flags & 0x20) ? 1 : part_count(o);
	int k = 0;
	for (int16_t i = o->inst; i >= 0 && k < n; i = gInstances[i].next_part, k++) {
		model_inst_t *mi = &gInstances[i];
		if (flags & 0x40) {
			// clone: the part's own node becomes the animated one under a copy of its placement,
			// so anything attached to it moves along (first part only)
			coord_t *c = mi->coord, *place = coord_alloc(c->parent);
			if (place) {
				place->local = c->local;
				c->parent = place;
				fw_identity(&c->local);
			}
			break;
		}
		coord_t *c = coord_alloc(mi->coord);
		if (c) mi->coord = c;
	}
	o->frame = (uint16_t)frame;
	o->flags = (uint8_t)((o->flags & 0xf8) | flags | 8);
}

static void anim_detach(objstate_t *o) { // AnimState_Detach 0x455300
	int n = (o->flags & 0x20) ? 1 : part_count(o);
	int k = 0;
	for (int16_t i = o->inst; i >= 0 && k < n; i = gInstances[i].next_part, k++) {
		model_inst_t *mi = &gInstances[i];
		coord_t *c = mi->coord;
		if (!c || !c->parent) continue;
		coord_t *parent = c->parent;
		if (o->flags & 0x40) {
			c->local = *coord_world(c);
			c->parent = parent->parent;
			coord_free(parent);
			break;
		}
		if (o->frame != 0) {
			parent->local = *coord_world(c); // bake the pose (the original copies the world matrix too)
		}
		mi->coord = parent;
		coord_free(c);
	}
	o->flags &= 0x87;
}

// Anim_Evaluate 0x443e20: lerp positions between frame t>>12 and the next (wrapping), rotation of
// the first frame, or lerped matrices with the interpolate flag
static void anim_evaluate(const anim_t *a, int t, FwMatrix **out, int n, bool lerp_rot) {
	int f0 = t >> 12, f1 = f0 + 1;
	if (f1 == a->num_frames) f1 = 0;
	if (f0 < 0 || f0 >= a->num_frames) f0 = 0;
	if (f1 < 0 || f1 >= a->num_frames) f1 = 0;
	int fr = t & 0xfff, inv = 0x1000 - fr;
	int parts = min(a->num_parts, n);
	for (int p = 0; p < parts; p++) {
		const anim_key_t *k0 = &a->keys[f0 * a->num_parts + p], *k1 = &a->keys[f1 * a->num_parts + p];
		FwMatrix *m = out[p];
		m->t[0] = ((k0->x * inv) >> 12) + ((fr * k1->x) >> 12);
		m->t[1] = ((k0->y * inv) >> 12) + ((fr * k1->y) >> 12);
		m->t[2] = ((k0->z * inv) >> 12) + ((fr * k1->z) >> 12);
		int32_t t3[3] = {m->t[0], m->t[1], m->t[2]};
		fw_from_packed(k0->rot, m);
		memcpy(m->t, t3, sizeof(t3));
		if (lerp_rot) {
			FwMatrix m1;
			fw_from_packed(k1->rot, &m1);
			for (int r = 0; r < 3; r++) {
				for (int c = 0; c < 3; c++) {
					m->m[r][c] = (int16_t)(((m1.m[r][c] * fr) >> 12) + ((m->m[r][c] * inv) >> 12));
				}
			}
		}
	}
}

static void anim_apply(objstate_t *o, int frac) { // AnimState_Apply 0x455420
	int t = o->frame << 12;
	if (frac && (o->flags & 0x10)) {
		bool sf = f_finished, st = f_turn_flip, sw = f_wrapped, sn = f_turned;
		t = anim_step_frame(t, anim_last_frame(o) << 12, frac, o->flags);
		f_finished = sf; f_turn_flip = st; f_wrapped = sw; f_turned = sn;
	}
	if (!(o->flags & 8)) {
		return;
	}
	const anim_t *a = anim_get(o->anim);
	if (!a || !a->num_frames) {
		return;
	}
	int n = (o->flags & 0x20) ? 1 : part_count(o);
	FwMatrix *list[16];
	int k = 0;
	for (int16_t i = o->inst; i >= 0 && k < n && k < 16; i = gInstances[i].next_part) {
		list[k++] = &gInstances[i].coord->local;
	}
	n = k;
	if (!n) return;
	anim_evaluate(a, t, list, n, (o->flags & 0x80) != 0);
	if (a->num_parts != n && n > 1) {
		for (int i = 1; i < n; i++) *list[i] = *list[0];
	}
}

static void anim_step(task_t *t, objstate_t *o) { // AnimState_Step 0x455570
	if (!(o->flags & 0x10)) {
		return;
	}
	f_finished = f_turn_flip = f_wrapped = f_turned = false;
	o->frame = (uint16_t)anim_step_frame(o->frame, anim_last_frame(o), 1, o->flags);
	if (!f_finished) {
		if (f_turn_flip) o->flags ^= 4;
	} else {
		anim_apply(o, 0);
		o->flags &= 0xef;
		anim_raise(o, EV_ANIM_FINISHED);
	}
	if (f_wrapped) anim_raise(o, EV_ANIM_WRAPPED);
	else if (f_turned) anim_raise(o, EV_ANIM_TURNED);
	uint8_t ft = o->params ? ((uint8_t *)o->params)[2] : 0;
	if (ft) {
		t->wake += (uint32_t)ft * 10;
	}
}

// AnimState_Init 0x4558f0 from the anim word {flags, anim, frameTime}
static void anim_init(task_t *t, objstate_t *o, const uint8_t *word) {
	if (!word) return;
	o->flags = word[0];
	o->anim = word[1];
	if (!(o->flags & 8)) return;
	if ((o->flags & 3) == 0) {
		int frame;
		anim_raise(o, EV_ANIM_FINISHED);
		if (!(o->flags & 4)) {
			anim_raise(o, EV_ANIM_TURNED);
			frame = anim_last_frame(o);
		} else {
			anim_raise(o, EV_ANIM_WRAPPED);
			frame = 0;
		}
		anim_attach(o, o->flags, frame);
		anim_apply(o, 0);
		o->flags &= 0xef;
		return;
	}
	if (o->flags & 0x10) {
		int frame = 0;
		if ((o->id & 0x3000000) == 0) { // map objects: in step with the global clock
			uint32_t period = word[2] ? word[2] * 10u : 100u;
			if (t->wake % period) t->wake = ((t->wake + period) / period) * period;
			f_turn_flip = false;
			if ((o->flags & 3) == 2) o->flags &= 0xfb;
			frame = anim_step_frame(0, anim_last_frame(o), (int)(t->wake / period), o->flags);
			if (f_turn_flip) o->flags ^= 4;
		}
		anim_attach(o, o->flags, frame);
		return;
	}
	o->flags &= 0xf7;
}

static void anim_save_word(objstate_t *o) { // 0x455a50
	if (o->params) {
		((uint8_t *)o->params)[0] = o->flags;
		((uint8_t *)o->params)[1] = o->anim;
	}
	if (o->flags & 8) anim_detach(o);
}

void animobj_render(task_t *t, uint32_t time) { // 0x455af0 -> 0x455a80
	objstate_t *o = objstate(t);
	if (!(o->flags & 8)) return;
	uint8_t ft = o->params ? ((uint8_t *)o->params)[2] : 0;
	int period = ft ? ft * 10 : 100;
	int frac = (int)(((int64_t)(period - (int32_t)t->wake + (int32_t)time) << 12) / period);
	anim_apply(o, frac);
}

void animobj_apply(task_t *t) { anim_apply(objstate(t), 0); } // AnimState_Apply(state, 0)

// Property 2 of the animated types while the physics asks (FUN_004560f0 -> FUN_00455660): the
// world matrices of the parts with the animation evaluated at game time `time`, in a cache per
// object and query slot (0 = the step's start, 1 = its end) so the two answers are different
// pointers. NULL when the animation is not playing (the part's node is still).
// Deviation: each animated part is composed with its own parent node (the original uses the
// first part's for all, the same for the single-part models that animate on the disc), and parts
// the animation does not drive (flag 0x20) answer NULL, as they stay put in the rewrite.
#define POSE_CACHE 32
typedef struct {
	ObjId id;
	const objstate_t *o;
	int32_t time;
	int n;
	FwMatrix m[16];
} pose_cache_t;
static pose_cache_t pose_cache[2][POSE_CACHE];
static int pose_next[2];

const FwMatrix *anim_pose_at(ObjId handle, int32_t time, int slot) {
	ObjId id = handle & 0x03ffffffu;
	int part = (int)((handle >> 26) & 0x3f);
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(id, &params, &t);
	if (!t || !t->type) return NULL;
	objstate_t *o = objstate(t);
	if (!(o->flags & 0x10) || !(o->flags & 8)) return NULL;
	const anim_t *a = anim_get(o->anim);
	if (!a || !a->num_frames) return NULL;
	slot &= 1;
	pose_cache_t *c = NULL;
	for (int i = 0; i < POSE_CACHE; i++) {
		if (pose_cache[slot][i].o == o && pose_cache[slot][i].id == id) {
			c = &pose_cache[slot][i];
			break;
		}
	}
	if (c && c->time == time) {
		return part < c->n ? &c->m[part] : NULL;
	}
	if (!c) {
		c = &pose_cache[slot][pose_next[slot]];
		pose_next[slot] = (pose_next[slot] + 1) % POSE_CACHE;
	}
	c->id = id;
	c->o = o;
	c->time = time;
	c->n = 0;
	uint8_t ft = params ? ((uint8_t *)params)[2] : 0;
	int32_t period = ft ? ft * 10 : 100;
	int32_t frac = (int32_t)((uint32_t)(period - (int32_t)t->wake + time) * 0x1000u) / period;
	int ft12 = o->frame << 12;
	if (frac) {
		bool sf = f_finished, st = f_turn_flip, sw = f_wrapped, sn = f_turned;
		ft12 = anim_step_frame(ft12, anim_last_frame(o) << 12, frac, o->flags);
		f_finished = sf; f_turn_flip = st; f_wrapped = sw; f_turned = sn;
	}
	int n = (o->flags & 0x20) ? 1 : part_count(o);
	if (n > 16) n = 16;
	FwMatrix *list[16];
	for (int k = 0; k < n; k++) list[k] = &c->m[k];
	anim_evaluate(a, ft12, list, n, (o->flags & 0x80) != 0);
	if (a->num_parts != n && n > 1) {
		for (int k = 1; k < n; k++) c->m[k] = c->m[0];
	}
	int k = 0;
	for (int16_t i = o->inst; i >= 0 && k < n; i = gInstances[i].next_part, k++) {
		coord_t *node = gInstances[i].coord;
		if (node && node->parent) {
			FwMatrix local = c->m[k];
			fw_compose(coord_world(node->parent), &local, &c->m[k]);
		}
	}
	c->n = k;
	return part < c->n ? &c->m[part] : NULL;
}

void AnimPlay(ObjId id, int32_t anim, int32_t flags) { // 0x455e20
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(id, &params, &t);
	if (!t || !t->type) {
		if (params) {
			uint8_t *w = (uint8_t *)params;
			w[1] = (uint8_t)anim;
			w[0] = (uint8_t)((w[0] & 0xf8) | (uint8_t)flags | 0x18);
		}
		return;
	}
	objstate_t *o = objstate(t);
	if (!(o->flags & 8)) {
		o->anim = (uint8_t)anim;
		anim_attach(o, (uint8_t)flags, 0);
	} else if (o->anim == (uint8_t)anim) {
		o->flags = (uint8_t)((o->flags & 0xf8) | (uint8_t)flags);
	} else {
		anim_detach(o);
		o->anim = (uint8_t)anim;
		anim_attach(o, (uint8_t)flags, 0);
	}
	o->flags |= 0x10;
	uint8_t ft = params ? ((uint8_t *)params)[2] : 0;
	Task_SetWakeTime(t, (uint32_t)gGameUpdateTime + (ft ? ft * 10u : 100u));
}

void AnimStop(ObjId id, int32_t mode) { // 0x455f00
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(id, &params, &t);
	if (!t || !t->type) {
		if (params) ((uint8_t *)params)[0] &= 0xe7;
		return;
	}
	objstate_t *o = objstate(t);
	if (!(o->flags & 8)) return;
	if (mode == 1) {
		o->frame = 0;
		anim_detach(o);
		return;
	}
	anim_apply(o, 0);
	anim_detach(o);
}

void AnimSetUpdateInterval(ObjId id, int32_t frame_time) { // 0x455f70
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (params) ((uint8_t *)params)[2] = (uint8_t)frame_time;
}

// ---------------------------------------------------------------- anim object types (3, 5, 15, 19, 14)

void animobj_create(task_t *t, void *arg) { // AnimObject_Create 0x455b30
	obj_args_t *a = arg;
	objstate_init(t, a);
	anim_init(t, objstate(t), a && a->params ? (const uint8_t *)a->params : NULL);
}

void animobj_update(task_t *t) { anim_step(t, objstate(t)); }
void animobj_destroy(task_t *t) { anim_save_word(objstate(t)); }

// AnimDestructible (15): anim word + destructible header in word 1 (0x458870)
static void animdestr_create(task_t *t, void *arg) { animobj_create(t, arg); }

// Pickup (19): collected within 0x400 of a player lander with the tractor on (0x45a780)
static void pickup_update(task_t *t) {
	objstate_t *o = objstate(t);
	anim_step(t, o);
	coord_t *c = objstate_coord(o);
	const int32_t *p = c ? coord_world(c)->t : o->rec ? (const int32_t *)&o->rec->pos : NULL;
	if (!p) return;
	for (int i = 0; i < gNbPlayers; i++) {
		ObjId lander = gPlayerObjects[i];
		const int32_t *lp = (const int32_t *)Object_GetProperty(lander, PROP_POSITION);
		if (!lp) continue;
		double dx = lp[0] - p[0], dy = lp[1] - p[1], dz = lp[2] - p[2];
		if (dx * dx + dy * dy + dz * dz < 0x400 * 0x400 && Lander_IsTractorOn(lander)) {
			LanderEvent ev = {EV_PICKUP, {(intptr_t)o->id, (intptr_t)lander}};
			Event_Raise(&ev);
			Object_SetProperty(lander, PROP_FLAGS, 0x1000);
			Object_SetDead(o->id);
			return;
		}
	}
}

// ---------------------------------------------------------------- triggers (4, 26)

static void own_coord_from_record(objstate_t *o) {
	o->coord = coord_alloc(NULL);
	if (o->coord && o->rec) {
		fw_from_packed(o->rec->rot, &o->coord->local);
		o->coord->local.t[0] = o->rec->pos.x;
		o->coord->local.t[1] = o->rec->pos.y;
		o->coord->local.t[2] = o->rec->pos.z;
	}
}

static bool trigbox_contains(objstate_t *o, ObjId obj) { // TriggerBox_Contains 0x456d40
	if (obj == OBJ_NONE || !Object_IsAwake(obj)) return false;
	const int32_t *p = (const int32_t *)Object_GetProperty(obj, PROP_POSITION);
	if (!p || !o->coord) return false;
	const FwMatrix *m = &o->coord->local;
	int32_t d[3] = {p[0] - m->t[0], p[1] - m->t[1], p[2] - m->t[2]}, l[3];
	for (int i = 0; i < 3; i++) { // invmatvec: transpose * d
		int64_t s = (int64_t)m->m[0][i] * d[0] + (int64_t)m->m[1][i] * d[1] + (int64_t)m->m[2][i] * d[2];
		l[i] = (int32_t)(s >> 12);
	}
	const int32_t *half = o->params;
	return abs(l[0]) < half[0] && abs(l[1]) < half[1] && abs(l[2]) < half[2];
}

static ObjId *trig_list(int *n) {
	*n = gNbPlayers;
	return gPlayerObjects;
}

static void trigbox_create(task_t *t, void *arg) { // 0x456ef0
	obj_args_t *a = arg;
	objstate_init(t, a);
	objstate_t *o = objstate(t);
	own_coord_from_record(o);
	o->extra = (void *)(intptr_t)(force_any_player_box ? -1 : (o->params ? o->params[3] : -1));
	if ((intptr_t)o->extra == -1) {
		o->mask = 0;
		for (int i = 0; i < gNbPlayers; i++) {
			if (trigbox_contains(o, gPlayerObjects[i])) o->mask |= 1u << i;
		}
	} else {
		o->mask = trigbox_contains(o, (ObjId)(intptr_t)o->extra);
	}
}

static void trigbox_update(task_t *t) { // 0x456e20
	objstate_t *o = objstate(t);
	if (!o->params) return;
	ObjId single = (ObjId)(intptr_t)o->extra;
	int n;
	ObjId *list;
	if ((intptr_t)o->extra == -1) {
		list = trig_list(&n);
	} else {
		list = &single;
		n = 1;
	}
	for (int i = 0; i < n; i++) {
		bool was = (o->mask >> i) & 1, now = trigbox_contains(o, list[i]);
		if (was == now) continue;
		int type;
		if (!was) { o->mask |= 1u << i; type = EV_TRIGGER_ENTER; }
		else { o->mask &= ~(1u << i); type = EV_TRIGGER_LEAVE; }
		LanderEvent ev = {type, {(intptr_t)list[i], (intptr_t)o->id}};
		Event_Raise(&ev);
	}
}

static void trig_destroy(task_t *t) { coord_free(objstate(t)->coord); objstate(t)->coord = NULL; }

static void trigbox_set_target(ObjId id, intptr_t v) { // TriggerBox_SetTarget 0x457060
	force_any_player_box = false;
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (params) params[3] = (int32_t)v;
	objstate_t *o = objstate_from_id(id);
	if (!o) return;
	o->extra = (void *)v;
	if (v == -1) {
		o->mask = 0;
		for (int i = 0; i < gNbPlayers; i++) {
			if (trigbox_contains(o, gPlayerObjects[i])) o->mask |= 1u << i;
		}
	} else {
		o->mask = trigbox_contains(o, (ObjId)v);
	}
}

// TrigBox_Test 0x4570c0. The original reads the box's process without a check; a sleeping box (Europa 2
// tests the arrival box right after teleporting 200000 units, before the view-range task wakes it)
// gives a null process there. Here a sleeping box is tested from its record, which is what that
// code expects to see.
int32_t TrigBox_Test(ObjId trigger, ObjId obj) {
	objstate_t *o = objstate_from_id(trigger);
	if (o) {
		return trigbox_contains(o, obj) ? 1 : 0;
	}
	map_rec_t *r = Object_GetRecord(trigger);
	int32_t *params = NULL;
	Objects_GetData(trigger, &params, NULL);
	if (!r || !params) {
		return 0;
	}
	objstate_t tmp = {.id = trigger, .inst = -1, .rec = r, .params = params};
	coord_t c = {.used = true};
	fw_from_packed(r->rot, &c.local);
	c.local.t[0] = r->pos.x;
	c.local.t[1] = r->pos.y;
	c.local.t[2] = r->pos.z;
	tmp.coord = &c;
	return trigbox_contains(&tmp, obj) ? 1 : 0;
}

static intptr_t trig_get_pos(ObjId id) {
	objstate_t *o = objstate_from_id(id);
	return o && o->coord ? (intptr_t)o->coord->local.t : get_position(id);
}

static intptr_t trig_get_matrix(ObjId id) {
	objstate_t *o = objstate_from_id(id);
	return o && o->coord ? (intptr_t)&o->coord->local : get_matrix(id);
}

// TriggerPlane (26): rectangle in the local XZ plane; event 0x13 {player, plane, side} when crossed
static bool plane_side(objstate_t *o, const int32_t *p, int32_t *local) {
	const FwMatrix *m = &o->coord->local;
	int32_t d[3] = {p[0] - m->t[0], p[1] - m->t[1], p[2] - m->t[2]};
	for (int i = 0; i < 3; i++) {
		int64_t s = (int64_t)m->m[0][i] * d[0] + (int64_t)m->m[1][i] * d[1] + (int64_t)m->m[2][i] * d[2];
		local[i] = (int32_t)(s >> 12);
	}
	return local[1] >= 0;
}

static void trigplane_create(task_t *t, void *arg) {
	obj_args_t *a = arg;
	objstate_init(t, a);
	own_coord_from_record(objstate(t));
	objstate(t)->extra = (void *)(intptr_t)(force_any_player_plane ? -1 : (objstate(t)->params ? objstate(t)->params[2] : -1));
	objstate(t)->mask = 0xffffffff;
}

static void trigplane_update(task_t *t) { // 0x457300
	objstate_t *o = objstate(t);
	if (!o->params || !o->coord) return;
	for (int i = 0; i < gNbPlayers; i++) {
		ObjId lander = gPlayerObjects[i];
		if (lander == OBJ_NONE || !Object_IsAwake(lander)) continue;
		if ((intptr_t)o->extra != -1 && (ObjId)(intptr_t)o->extra != lander) continue;
		const int32_t *p = (const int32_t *)Object_GetProperty(lander, PROP_POSITION);
		const int32_t *prev = Lander_GetPrevPos(lander);
		if (!p || !prev) continue;
		int32_t a[3], b[3];
		bool sa = plane_side(o, prev, a), sb = plane_side(o, p, b);
		if (sa == sb || a[1] == b[1]) continue;
		int32_t s = (a[1] * -0x1000) / (b[1] - a[1]);
		int32_t x = a[0] + (int32_t)(((int64_t)(b[0] - a[0]) * s + 0x800) >> 12);
		int32_t z = a[2] + (int32_t)(((int64_t)(b[2] - a[2]) * s + 0x800) >> 12);
		if (abs(x) <= o->params[0] && abs(z) <= o->params[1]) {
			LanderEvent ev = {EV_TRIGGER_PLANE, {(intptr_t)i, (intptr_t)o->id, (intptr_t)sb}};
			Event_Raise(&ev);
		}
	}
}

static void trigplane_set_target(ObjId id, intptr_t v) {
	force_any_player_plane = false;
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (params) params[2] = (int32_t)v;
	objstate_t *o = objstate_from_id(id);
	if (o) o->extra = (void *)v;
}

// ---------------------------------------------------------------- simple types

void objtype_plain_create(task_t *t, void *arg) { objstate_init(t, arg); }

static void marker_create(task_t *t, void *arg) { objstate_init(t, arg); own_coord_from_record(objstate(t)); }

// Switch (10): property 5 set toggles the model (+-1 destroy level) unless locked
static void switch_set_state(ObjId id, intptr_t v) { // 0x458e20.. property 5
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (!params) return;
	uint8_t *w = (uint8_t *)params;
	if (v == -1) { w[1] = 1; return; }
	if (v < 0) { w[1] = 0; return; }
	if (w[1] || w[0] == (uint8_t)v) return;
	Object_AdvanceDestroyLevel(id, v ? 1 : -1);
	w[0] = (uint8_t)v;
}

static intptr_t switch_get_state(ObjId id) {
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	return params ? ((uint8_t *)params)[0] : 0;
}

// Tractor_InRange 0x44c800 (d = target - point, normalised in place by modes 1-2)
static int32_t tractor_in_range(int32_t d[3], int mode) {
	if (abs(d[0]) > 0x800 || abs(d[1]) > 0x800 || abs(d[2]) > 0x800) return 0;
	if (mode == 0) {
		if (d[1] <= gTractorRange && d[1] >= 0) {
			int32_t r = (d[1] << 8) / gTractorRange + 0x40;
			if (d[2] * d[2] + d[0] * d[0] < r * r) return d[1];
		}
		return 0;
	}
	int32_t range = mode == 1 ? gTractorRange : mode == 2 ? gTractorRange + 0x100 : -1;
	if (range < 0) return 0;
	int32_t l = FwVectorNormalise(d);
	return l < range + 0x100 ? l : 0;
}

// FuelDump (17): FuelDump_Create 0x45a900 / FuelDump_Update 0x45a960 (lander.md §6.2).
// State: pad point (record position, y + 0x100) and the connected lander.
static bool fuel_latch; // DAT_0055fdac
static void fueldump_create(task_t *t, void *arg) {
	obj_args_t *a = arg;
	objstate_init(t, a);
	objstate_t *o = objstate(t);
	o->mask = OBJ_NONE; // connected lander
}

static void fueldump_point(objstate_t *o, int32_t p[3]) {
	p[0] = o->rec ? o->rec->pos.x : 0;
	p[1] = (o->rec ? o->rec->pos.y : 0) + 0x100;
	p[2] = o->rec ? o->rec->pos.z : 0;
}

static void fueldump_update(task_t *t) {
	objstate_t *o = objstate(t);
	int32_t *params = o->params;
	if (!params) return;
	if (((uint8_t *)params)[0] == 0) params[1] = 0;
	int32_t pt[3], d[3];
	fueldump_point(o, pt);
	if (o->mask != OBJ_NONE) {
		ObjId lander = (ObjId)o->mask;
		const int32_t *lp = (const int32_t *)Object_GetProperty(lander, PROP_POSITION);
		if (lp) {
			d[0] = lp[0] - pt[0]; d[1] = lp[1] - pt[1]; d[2] = lp[2] - pt[2];
		}
		if (lp && tractor_in_range(d, 1)) {
			if (!Lander_TakeFuel(lander, &params[1])) return;
			Lander_SetHookTarget(lander, OBJ_NONE);
			Object_SetProperty(lander, PROP_FLAGS, 0x1000);
			o->mask = OBJ_NONE;
			fuel_latch = true;
			return;
		}
		Lander_SetHookTarget(lander, OBJ_NONE);
		o->mask = OBJ_NONE;
	}
	for (int i = 0; i < gNbPlayers; i++) {
		ObjId lander = gPlayerObjects[i];
		if (!Lander_IsTractorOn(lander)) {
			fuel_latch = false;
			continue;
		}
		if (fuel_latch) continue;
		const int32_t *lp = (const int32_t *)Object_GetProperty(lander, PROP_POSITION);
		if (!lp) continue;
		d[0] = lp[0] - pt[0]; d[1] = lp[1] - pt[1]; d[2] = lp[2] - pt[2];
		if (!tractor_in_range(d, 0)) continue;
		if (params[1] == 0) {
			sound_play(OBJ_NONE, 0x409, 1, -1);
			fuel_latch = true;
			continue;
		}
		Lander_SetHookTarget(lander, o->id | 0x80000000u);
		o->mask = lander;
		return;
	}
}

void FuelDump_SetFuel(ObjId id, int32_t amount) { // 0x45abb0
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (params) params[1] = amount;
}

// ---------------------------------------------------------------- LightObject (22)
// LightObject_Create 0x4578c0, update 0x457850, render 0x457880, draw callback 0x457670, destroy
// 0x457ac0, properties 0x457b90 (1 target), 0x457bf0 (0 position), 0x457c50 (5 state).
// Pool words: anim word; u8 mode, u8 period/100 ms, u8 phase/10 ms, u8 level; u8 r, g, b, kind
// (0 point, 1 spot, 2 box); point/spot: s32 inner, s32 falloff; spot: u16 cone inner, cone outer;
// box: 8 x s32 (Light_SetBox). The light hangs on a render item without a model (its node is the
// light's node, animated by the anim word like an AnimObject's part); the item's draw callback sets
// the light's colour every frame (blink modes) and draws the glow sprite or the spot's cone.
// The process words after the anim state (+0x18..+0x29), kept per task slot; o.extra points here.
typedef struct {
	dyn_light_t *light;  // +0x18
	uint32_t colour;     // +0x20 pool bytes 8..10 (0x00BBGGRR)
	uint16_t phase;      // +0x1c pool byte 6 * 10: phase of the pulse (ms)
	uint16_t period;     // +0x1e pool byte 5 * 100: period of the pulse (ms)
	uint8_t mode;        // +0x24 pool byte 4: 0 pulse, 1 random flicker, 2 wobbling light (kind 6), 3 steady
	uint8_t level;       // +0x25 pool byte 7: glow size; the spot's cone length / 64
	uint16_t glow;       // +0x26 spot: the cone's radius at level * 64 (0x4569a0)
	uint16_t intensity;  // +0x28 4.12: 0x1000 on, 0 off (mode 1 starts off); property 5
} lightobj_t;

static lightobj_t lightobjs[MAX_TASKS];

static lightobj_t *lightobj(task_t *t) { return &lightobjs[Task_ToIndex(t)]; }

static task_t *lightobj_task(ObjId id) {
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	return t && t->type == &gObjectTypes[OT_LIGHTOBJECT].task ? t : NULL;
}

static lightobj_t *lightobj_from_id(ObjId id) {
	task_t *t = lightobj_task(id);
	return t ? lightobj(t) : NULL;
}

// FUN_00457670: the render item's draw callback
static void lightobj_draw(model_inst_t *mi) {
	task_t *t = mi->user;
	if (!t || t->type != &gObjectTypes[OT_LIGHTOBJECT].task) return;
	lightobj_t *L = lightobj(t);
	dyn_light_t *light = L->light;
	if (!light) return;
	uint32_t c = L->colour;
	int32_t k = 0x1000;
	bool scale = true;
	switch (L->mode) {
	case 0: // pulse: 0.5 + sin / 2 over the period, times the intensity unless that is 0
		if (!L->period) { // (the exe divides by 0; no light on the disc has period 0)
			scale = false;
			break;
		}
		k = (fx_rsin((int32_t)((((fxc.now - L->phase) % L->period) << 12) / L->period)) >> 1) + 0x800;
		if (L->intensity) k = (k * L->intensity) >> 12;
		break;
	case 1: // random flicker: full one frame in three, else the intensity (frozen while paused)
		if (game_is_paused()) {
			scale = false;
			break;
		}
		k = fx_rand() % 3 == 0 ? 0x1000 : L->intensity;
		break;
	case 2:
	case 3:
		k = L->intensity;
		scale = k != 0x1000;
		break;
	default:
		scale = false;
		break;
	}
	if (scale) c = fx_colour_scale(c, k); // FUN_0044a5b0
	light->color = vec3((c & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, ((c >> 16) & 0xff) / 255.0f);
	if (!L->level) return;
	if (light->kind == 2) { // the additive cone in the item's frame (FUN_004569d0)
		effects_light_cone(mi->coord, NULL, (int16_t)L->glow, L->level, c);
		return;
	}
	if (light->kind != 1 && light->kind != 6) return;
	// FUN_00431d20: the item's position on the screen; then the glow with its two flare streaks
	// (FUN_0044a330) of size H * level * 0x600 / z
	int32_t sx, sy, z;
	if (!fxc.valid || !effects_project(coord_world(mi->coord)->t, &sx, &sy, &z)) return;
	if (z <= 0x40 || z > 0x7fff || sx >= fxc.w || sy >= fxc.ht) return; // (the exe keeps z in an s16)
	int32_t size = (int32_t)((int64_t)(int32_t)fxc.h * L->level * 0x600 / z);
	effects_glow_flares(sx, sy, z, c, size);
}

// LightObject_Create 0x4578c0
static void lightobj_create(task_t *t, void *arg) {
	obj_args_t *a = arg;
	lightobj_t *L = lightobj(t);
	memset(L, 0, sizeof(*L));
	objstate_init(t, a);
	objstate_t *o = objstate(t);
	o->extra = L;
	const uint8_t *w = o->params ? (const uint8_t *)o->params : NULL;
	if (!w) return;
	int kind = 1;
	switch (w[4]) {
	case 1: L->intensity = 0; break;
	case 2: kind = 6; // fall through
	case 0:
	case 3: L->intensity = 0x1000; break;
	}
	// the light's node: Light_Create allocates it, RenderItem_Alloc(node) puts the item on it
	int16_t inst = inst_create(NULL, NULL, NULL);
	coord_t *node = inst >= 0 ? gInstances[inst].coord : NULL;
	dyn_light_t *l = NULL;
	if (w[0xb] == 0) {
		l = dyn_light_create(kind, node);
		if (l) {
			l->inner = o->params[3];   // Light_SetRange(inner, falloff)
			l->falloff = o->params[4];
		}
	} else if (w[0xb] == 1) {
		l = dyn_light_create(2, node);
		if (l) {
			l->inner = o->params[3];
			l->falloff = o->params[4];
			l->cone_inner = (uint16_t)o->params[5];          // Light_SetCone(inner, outer)
			l->cone_outer = (uint16_t)((uint32_t)o->params[5] >> 16);
		}
		L->glow = (uint16_t)eng_headlight_glow(w[7], (uint16_t)o->params[5], (uint16_t)((uint32_t)o->params[5] >> 16));
	} else if (w[0xb] == 2) {
		l = dyn_light_create(7, node);
		if (l) memcpy(l->box, &o->params[3], sizeof(l->box)); // Light_SetBox
	}
	if (l) {
		l->color = vec3(w[8] / 255.0f, w[9] / 255.0f, w[10] / 255.0f); // Light_SetColour
		l->flags = 0x10;
	}
	if (inst >= 0) {
		model_inst_t *mi = &gInstances[inst];
		mi->user = t;
		mi->zone = -1;
		if (w[0xb] != 2) mi->draw = lightobj_draw; // box lights have no callback: their colour stays
	}
	o->inst = inst;
	if ((o->id & 0x3000000) == 0x1000000) eng_map_set_mob_model(o->id, inst); // FUN_004497a0
	anim_init(t, o, w);
	L->light = l;
	L->colour = (uint32_t)o->params[2] & 0xffffff; // (the exe keeps the kind byte as the top byte)
	L->mode = w[4];
	L->level = w[7];
	L->phase = (uint16_t)(w[6] * 10);
	L->period = (uint16_t)(w[5] * 100);
	if (node && o->rec) { // RotPackToMatrix(record, node)
		fw_from_packed(o->rec->rot, &node->local);
		node->local.t[0] = o->rec->pos.x;
		node->local.t[1] = o->rec->pos.y;
		node->local.t[2] = o->rec->pos.z;
	}
}

static void lightobj_update(task_t *t) { anim_step(t, objstate(t)); } // LightObject_Update 0x457850

static void lightobj_render(task_t *t, uint32_t time) { // 0x457880: the anim's in-between pose
	animobj_render(t, time);
	// the item is in the draw group DAT_00491be0, queued every frame (DrawGroup_Queue)
	if (objstate(t)->inst >= 0) inst_queue(objstate(t)->inst);
}

static void lightobj_destroy(task_t *t) { // 0x457ac0
	lightobj_t *L = lightobj(t);
	objstate_t *o = objstate(t);
	anim_save_word(o);
	dyn_light_destroy(L->light);
	L->light = NULL;
	if (o->inst >= 0) inst_free(o->inst);
	if ((o->id & 0x3000000) == 0x1000000) eng_map_set_mob_model(o->id, -1);
	o->inst = -1;
}

// the light's own node (the item's node before an animation put a node under it)
static coord_t *lightobj_node(lightobj_t *L) { return L->light ? L->light->coord : NULL; }

static void lightobj_set_target(ObjId id, intptr_t v) { // 0x457b90: hang the light on the object's node
	lightobj_t *L = lightobj_from_id(id);
	coord_t *node = L ? lightobj_node(L) : NULL;
	if (!node) return;
	int16_t inst = Object_GetInstance((ObjId)v);
	node->parent = inst >= 0 && gInstances[inst].used ? gInstances[inst].coord : NULL;
}

static void lightobj_set_position(ObjId id, intptr_t v) { // 0x457bf0
	lightobj_t *L = lightobj_from_id(id);
	coord_t *node = L ? lightobj_node(L) : NULL;
	const int32_t *p = (const int32_t *)v;
	if (!node || !p) return;
	node->local.t[0] = p[0];
	node->local.t[1] = p[1];
	node->local.t[2] = p[2];
}

static void lightobj_set_state(ObjId id, intptr_t v) { // 0x457c50: the intensity (4.12)
	lightobj_t *L = lightobj_from_id(id);
	if (L) L->intensity = (uint16_t)v;
}

void objtype_no_update(task_t *t) { (void)t; }

// ---------------------------------------------------------------- registration

void objprops_common(int type) {
	ObjectProperty_RegisterGet(type, 0, get_position);
	ObjectProperty_RegisterGet(type, 2, get_matrix);
	ObjectProperty_RegisterSet(type, 0, set_position);
	ObjectProperty_RegisterSet(type, 2, set_matrix);
}

void objprops_destructible(int type, int word) {
	ObjectProperty_RegisterGet(type, 3, word ? get_hp_w1 : get_hp_w0);
	ObjectProperty_RegisterGet(type, 4, word ? get_armour_w1 : get_armour_w0);
	ObjectProperty_RegisterSet(type, 3, word ? set_hp_w1 : set_hp_w0);
	ObjectProperty_RegisterSet(type, 4, word ? set_armour_w1 : set_armour_w0);
	ObjectProperty_RegisterSet(type, 9, word ? set_expl_w1 : set_expl_w0);
	if (!word) ObjectProperty_RegisterGet(type, 8, get_alive_flags_w0);
}

void objprop_ignore(ObjId id, intptr_t v) { (void)id; (void)v; }

// property 10 (types 3, 14, 15, 19, 22): the animation state, so Object_AdvanceDestroyLevel can give it
// the rebuilt instance
static intptr_t get_animstate(ObjId id) { return (intptr_t)objstate_from_id(id); }

void objtypes_register_inert(int type) {
	ObjectType_Register(type, objtype_no_update, NULL, objtype_plain_create, NULL, 0);
	objprops_common(type);
	for (int p = 1; p < 12; p++) {
		if (p != 2) ObjectProperty_RegisterSet(type, p, objprop_ignore);
	}
}

void objtypes_register_all(void) {
	// animated types
	ObjectType_Register(OT_ANIMOBJECT, animobj_update, animobj_render, animobj_create, animobj_destroy, 2);
	ObjectType_SetFrequency(OT_ANIMOBJECT, 100);
	objprops_common(OT_ANIMOBJECT);
	ObjectType_Register(OT_ANIMDESTRUCTIBLE, animobj_update, animobj_render, animdestr_create, animobj_destroy, 2);
	ObjectType_SetFrequency(OT_ANIMDESTRUCTIBLE, 100);
	objprops_common(OT_ANIMDESTRUCTIBLE);
	objprops_destructible(OT_ANIMDESTRUCTIBLE, 1);
	ObjectType_Register(OT_PICKUP, pickup_update, animobj_render, animobj_create, animobj_destroy, 2);
	ObjectType_SetFrequency(OT_PICKUP, 100);
	objprops_common(OT_PICKUP);
	objprops_destructible(OT_PICKUP, 1);

	// triggers
	ObjectType_Register(OT_TRIGGERBOX, trigbox_update, NULL, trigbox_create, trig_destroy, 0);
	ObjectProperty_RegisterGet(OT_TRIGGERBOX, 0, trig_get_pos);
	ObjectProperty_RegisterGet(OT_TRIGGERBOX, 2, trig_get_matrix);
	ObjectProperty_RegisterSet(OT_TRIGGERBOX, 1, trigbox_set_target);
	ObjectType_Register(OT_TRIGGERPLANE, trigplane_update, NULL, trigplane_create, trig_destroy, 0);
	ObjectProperty_RegisterGet(OT_TRIGGERPLANE, 0, trig_get_pos);
	ObjectProperty_RegisterGet(OT_TRIGGERPLANE, 2, trig_get_matrix);
	ObjectProperty_RegisterSet(OT_TRIGGERPLANE, 1, trigplane_set_target);

	// others
	ObjectType_Register(OT_SWITCH, NULL, NULL, objtype_plain_create, NULL, 1);
	objprops_common(OT_SWITCH);
	ObjectProperty_RegisterGet(OT_SWITCH, 5, switch_get_state);
	ObjectProperty_RegisterSet(OT_SWITCH, 5, switch_set_state);
	ObjectType_Register(OT_FUELDUMP, fueldump_update, NULL, fueldump_create, NULL, 1);
	objprops_common(OT_FUELDUMP);
	objprops_destructible(OT_FUELDUMP, 0);
	ObjectType_Register(OT_MARKER, NULL, NULL, marker_create, trig_destroy, 0);
	ObjectProperty_RegisterGet(OT_MARKER, 0, trig_get_pos);
	ObjectProperty_RegisterGet(OT_MARKER, 2, trig_get_matrix);
	ObjectType_Register(OT_LIGHTOBJECT, lightobj_update, lightobj_render, lightobj_create, lightobj_destroy, 0);
	ObjectType_SetFrequency(OT_LIGHTOBJECT, 100);
	ObjectProperty_RegisterGet(OT_LIGHTOBJECT, 0, get_position);     // FUN_00455fc0 (as AnimObject)
	ObjectProperty_RegisterSet(OT_LIGHTOBJECT, 1, lightobj_set_target);
	ObjectProperty_RegisterSet(OT_LIGHTOBJECT, 5, lightobj_set_state);
	ObjectProperty_RegisterSet(OT_LIGHTOBJECT, 0, lightobj_set_position);   // get 10: get_animstate below
	objtypes_register_inert(11); // unused by the missions
	objtypes_register_inert(18);

	// the other modules' types (turrets, projectiles, explosions; effects; pods, force boxes, cameras)
	weapons_register_types();
	effects_register_types();
	physobjs_register_types();

	static const int animstate_types[] = {OT_ANIMOBJECT, OT_ANIMTURRET, OT_ANIMDESTRUCTIBLE, OT_PICKUP, OT_LIGHTOBJECT};
	for (size_t i = 0; i < len(animstate_types); i++) {
		if (!gObjectTypes[animstate_types[i]].get[PROP_ANIMSTATE]) {
			ObjectProperty_RegisterGet(animstate_types[i], PROP_ANIMSTATE, get_animstate);
		}
	}
}

void objtypes_reset_mission(void) {
	force_any_player_box = force_any_player_plane = true;
	fuel_latch = false;
	weapons_reset();
	effects_reset();
	physobjs_reset();
}