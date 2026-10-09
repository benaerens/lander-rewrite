// The object system: object types and properties (0x4496d0-0x449a90), map
// records (statics, objects, mobs) with their model instances (0x444ec0,
// 0x445080), waking and sleeping (0x445b70-0x447560) and proximity
// activation (0x447dd0). See re/specs/mission.md §3 and mission-api.md §4.
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "objtypes.h"
#include "physobjs.h"

object_type_t gObjectTypes[MAX_OBJECT_TYPES];
ObjId gCameraObject = OBJ_NONE;

#define REC_MAX_ZONES 16

typedef struct {
	uint8_t flags;         // record +0x15: bit0 awake (or "in range" while dead), bit1 dead
	task_t *proc;          // record +0x18
	int16_t inst;          // static/object: active-list node instance; mob: record +0x20
	int8_t nzones;         // MapRec_ComputeZones result (-1 = no zone: always active)
	int16_t zones[REC_MAX_ZONES];
	int32_t ext[3];        // half size of the record's world box (its volume in the map octree)
	int16_t prev, next;    // mobs: sleep/awake lists (record +0x1c / +0x1e)
} rec_rt_t;

static rec_rt_t *rt[3];
static int mob_awake_head = -1, mob_sleep_head = -1;

static mission_t *M(void) { return G.level ? G.level->mission : NULL; }

// ---------------------------------------------------------------- object types

void ObjectType_Register(int type, void (*update)(task_t *), void (*render)(task_t *, uint32_t),
	void (*create)(task_t *, void *), void (*destroy)(task_t *), int cls) { // 0x4496d0
	object_type_t *ot = &gObjectTypes[type];
	ot->task.update = update;
	ot->task.render = render;
	ot->task.init = create;
	ot->task.destroy = destroy;
	ot->task.period = 0x28;
	ot->task.flags = ((uint32_t)cls << 16) | (render ? 1 : 0);
	ot->cls = cls;
	ot->registered = true;
}

void ObjectType_SetFrequency(int type, uint32_t period_ms) { gObjectTypes[type].task.period = period_ms; }
void ObjectProperty_RegisterGet(int type, int prop, prop_get_fn fn) { gObjectTypes[type].get[prop] = fn; }
void ObjectProperty_RegisterSet(int type, int prop, prop_set_fn fn) { gObjectTypes[type].set[prop] = fn; }
int ObjectType_GetClass(int type) { return type >= 0 && type < MAX_OBJECT_TYPES ? gObjectTypes[type].cls : 0; }

intptr_t Object_GetProperty(ObjId id, int32_t prop) { // 0x449a30
	int type = OBJ_TYPE(id);
	if (type >= MAX_OBJECT_TYPES || prop < 0 || prop >= 12 || !gObjectTypes[type].get[prop]) {
		return 0;
	}
	return gObjectTypes[type].get[prop](id);
}

void Object_SetProperty(ObjId id, int32_t prop, intptr_t value) { // 0x449a60 (no NULL check there)
	int type = OBJ_TYPE(id);
	if (type < MAX_OBJECT_TYPES && prop >= 0 && prop < 12 && gObjectTypes[type].set[prop]) {
		gObjectTypes[type].set[prop](id, value);
	}
}

// ---------------------------------------------------------------- records and instances

static int table_of(ObjId id) {
	switch (id & 0x3000000) {
		case 0: return REC_OBJECT;
		case 0x1000000: return REC_MOB;
		default: return -1;
	}
}

map_rec_t *Object_GetRecord(ObjId id) {
	int t = table_of(id);
	if (t < 0 || !M() || (int)OBJ_INDEX(id) >= M()->num_rec[t]) {
		return NULL;
	}
	return &M()->rec[t][OBJ_INDEX(id)];
}

static rec_rt_t *rec_rt(int table, int index) { return &rt[table][index]; }

int16_t Object_GetInstance(ObjId id) {
	int t = table_of(id);
	if (t < 0 || !M() || (int)OBJ_INDEX(id) >= M()->num_rec[t]) {
		return -1;
	}
	return rt[t][OBJ_INDEX(id)].inst;
}

// MapRec_CreateInstance 0x444ec0: one instance per part; every part's node is in world space
static int16_t rec_create_instance(const map_rec_t *r) {
	if (r->model < 0 || !G.level->objects || r->model >= G.level->objects->count) {
		return -1;
	}
	const ppm_entry_t *e = &G.level->objects->entries[r->model];
	const model_info_t *mi = r->model < M()->num_models ? &M()->models[r->model] : NULL;
	FwMatrix root;
	fw_from_packed(r->rot, &root);
	root.t[0] = r->pos.x;
	root.t[1] = r->pos.y;
	root.t[2] = r->pos.z;
	int16_t first = -1, prev = -1;
	int parts = e->num_parts > 0 ? e->num_parts : 1;
	for (int k = 0; k < parts; k++) {
		int16_t i = inst_create(k < e->num_parts ? e->parts[k] : NULL, G.level->textures, NULL);
		if (i < 0) {
			break;
		}
		model_inst_t *in = &gInstances[i];
		in->zone = r->zone;
		if (parts < 2 || !mi || k >= mi->num_parts) {
			in->coord->local = root;
		} else {
			const model_part_t *p = &mi->parts[k];
			FwMatrix pm;
			fw_from_packed(p->rot, &pm);
			pm.t[0] = p->pos.x;
			pm.t[1] = p->pos.y;
			pm.t[2] = p->pos.z;
			fw_compose(&root, &pm, &in->coord->local);
		}
		if (prev >= 0) {
			gInstances[prev].next_part = i;
		} else {
			first = i;
		}
		prev = i;
	}
	return first;
}

static int32_t destroy_step(int model) {
	return model >= 0 && model < M()->num_models ? M()->destroy_step[model] : 0;
}

// MapRec_AdvanceDestroyLevel 0x445080: model += steps (or -1); rebuild the instance keeping the pose.
// While an animation is attached the parts are the animation's nodes under the placement nodes: the
// new parts take the old local poses and hang under the same nodes (the original re-parents every
// part under the first part's parent; the rewrite's parts each have their own placement node).
static void rec_advance_destroy_level(map_rec_t *r, int steps, int16_t *inst) {
	int step = destroy_step(r->model);
	FwMatrix saved[16];
	coord_t *parent[16];
	int nsaved = 0;
	if (inst && *inst >= 0) {
		for (int16_t i = *inst; i >= 0 && nsaved < 16; i = gInstances[i].next_part) {
			saved[nsaved] = gInstances[i].coord->local;
			parent[nsaved++] = gInstances[i].coord->parent;
		}
		inst_free(*inst);   // frees the parts' own nodes, not the placement nodes above them
		*inst = -1;
	}
	if (step == -1) {
		r->model = -1;
		return;
	}
	r->model = (int16_t)(r->model + steps);
	if (!inst) {
		return;
	}
	*inst = rec_create_instance(r);
	int n = 0;
	for (int16_t i = *inst; i >= 0; i = gInstances[i].next_part) {
		n++;
	}
	if (nsaved && (n == nsaved || n == 1)) {
		int k = 0;
		for (int16_t i = *inst; i >= 0 && k < nsaved; i = gInstances[i].next_part, k++) {
			gInstances[i].coord->local = saved[k];
			gInstances[i].coord->parent = parent[k];
		}
	}
}
// MapRec_CreateProcess 0x446030
static int16_t rec_create_process(int table, int index, ObjId id) {
	map_rec_t *r = &M()->rec[table][index];
	obj_args_t args = {id, -1, r, r->param >= 0 && r->param < M()->num_pool ? &M()->pool[r->param] : NULL,
		NULL, NULL};
	if (r->model != -1) {
		args.inst = rec_create_instance(r);
	}
	rt[table][index].inst = args.inst;
	Objects_Create(r->type, &args);
	rt[table][index].proc = args.proc;
	return args.inst;
}

// ---------------------------------------------------------------- statics and objects

static void static_wake(int index) { // 0x445b70
	map_rec_t *r = &M()->rec[REC_STATIC][index];
	rt[REC_STATIC][index].inst = rec_create_instance(r);
	rt[REC_STATIC][index].flags |= OBJ_FLAG_AWAKE;
}

static void static_sleep(int index) {
	inst_free(rt[REC_STATIC][index].inst);
	rt[REC_STATIC][index].inst = -1;
	rt[REC_STATIC][index].flags &= ~OBJ_FLAG_AWAKE;
}

static void mapobject_wake(int index) { // 0x445f50
	rec_rt_t *s = rec_rt(REC_OBJECT, index);
	uint8_t f = s->flags;
	s->flags = f | OBJ_FLAG_AWAKE;
	if (f & OBJ_FLAG_DEAD) {
		return;
	}
	map_rec_t *r = &M()->rec[REC_OBJECT][index];
	rec_create_process(REC_OBJECT, index, OBJ_ID(r->type, index));
}

static void mapobject_sleep(int index) { // 0x445fc0
	rec_rt_t *s = rec_rt(REC_OBJECT, index);
	uint8_t f = s->flags;
	s->flags = f & ~OBJ_FLAG_AWAKE;
	if (!(f & OBJ_FLAG_DEAD)) {
		if (s->proc) {
			Task_Kill(s->proc);
		}
		s->proc = NULL;
		if (M()->rec[REC_OBJECT][index].model != -1) {
			inst_free(s->inst);
		}
		s->inst = -1;
	}
}

// ---------------------------------------------------------------- mobs

static void mob_unlink(int index) {
	rec_rt_t *s = rec_rt(REC_MOB, index);
	if (s->prev == -1) {
		if (mob_awake_head == index) mob_awake_head = s->next;
		else if (mob_sleep_head == index) mob_sleep_head = s->next;
	} else {
		rt[REC_MOB][s->prev].next = s->next;
	}
	if (s->next != -1) {
		rt[REC_MOB][s->next].prev = s->prev;
	}
}

static void mob_push(int *head, int index) {
	rec_rt_t *s = rec_rt(REC_MOB, index);
	s->next = (int16_t)*head;
	if (*head != -1) {
		rt[REC_MOB][*head].prev = (int16_t)index;
	}
	s->prev = -1;
	*head = index;
}

void MobObj_Wake(int32_t index) { // 0x447360
	if (!M() || index < 0 || index >= M()->num_rec[REC_MOB]) {
		return;
	}
	rec_rt_t *s = rec_rt(REC_MOB, index);
	if (s->flags & OBJ_FLAG_AWAKE && s->proc) {
		return; // the original corrupts its lists here; refuse instead
	}
	mob_unlink(index);
	mob_push(&mob_awake_head, index);
	s->flags |= OBJ_FLAG_AWAKE;
	if (!(s->flags & OBJ_FLAG_DEAD)) { // Mob_CreateProcess 0x4472b0
		map_rec_t *r = &M()->rec[REC_MOB][index];
		s->inst = -1;
		obj_args_t args = {MOB_ID(r->type, index), -1, r,
			r->param >= 0 && r->param < M()->num_pool ? &M()->pool[r->param] : NULL, NULL, NULL};
		if (r->model != -1) {
			args.inst = rec_create_instance(r);
			s->inst = args.inst;
		}
		Objects_Create(r->type, &args);
		s->proc = args.proc;
	}
}

void MobObj_Sleep(int32_t index) { // 0x447410
	if (!M() || index < 0 || index >= M()->num_rec[REC_MOB]) {
		return;
	}
	rec_rt_t *s = rec_rt(REC_MOB, index);
	if (!(s->flags & OBJ_FLAG_AWAKE)) {
		return;
	}
	mob_unlink(index);
	mob_push(&mob_sleep_head, index);
	s->flags &= ~OBJ_FLAG_AWAKE;
	if (!(s->flags & OBJ_FLAG_DEAD)) { // Mob_DestroyProcess 0x447310
		if (s->proc) {
			Task_Kill(s->proc);
		}
		s->proc = NULL;
		if (M()->rec[REC_MOB][index].model != -1) {
			inst_free(s->inst);
		}
		s->inst = -1;
	}
}

void MobObj_WakeAll(void) {
	for (int i = 0; M() && i < M()->num_rec[REC_MOB]; i++) {
		if (!(rt[REC_MOB][i].flags & OBJ_FLAG_AWAKE)) {
			MobObj_Wake(i);
		}
	}
}

void eng_map_set_mob_model(ObjId id, int16_t inst) { // mob record +0x20
	if (table_of(id) == REC_MOB && M() && (int)OBJ_INDEX(id) < M()->num_rec[REC_MOB]) {
		rt[REC_MOB][OBJ_INDEX(id)].inst = inst;
	}
}

int eng_mob_is_awake(int32_t index) {
	return M() && index >= 0 && index < M()->num_rec[REC_MOB] && (rt[REC_MOB][index].flags & OBJ_FLAG_AWAKE);
}

int eng_map_find_sleeping_mob(int32_t type) {
	for (int i = mob_sleep_head; i != -1; i = rt[REC_MOB][i].next) {
		if (M()->rec[REC_MOB][i].type == type) {
			return i;
		}
	}
	return -1;
}

// ---------------------------------------------------------------- object API

ObjId Objects_Create(int type, obj_args_t *args) { // 0x449820
	if (type < 0 || type >= MAX_OBJECT_TYPES || !gObjectTypes[type].registered) {
		if (args) args->proc = NULL;
		return OBJ_NONE;
	}
	if (!args) {
		task_t *t = Task_CreateNow(&gObjectTypes[type].task, NULL);
		return t ? ((uint32_t)Task_ToIndex(t) | ((uint32_t)(type | 0x200) << 16)) : OBJ_NONE;
	}
	args->proc = Task_CreateNow(&gObjectTypes[type].task, args);
	return args->id;
}

void Objects_GetData(ObjId id, int32_t **params, task_t **proc) { // 0x449880
	int t = table_of(id);
	if (t >= 0) {
		map_rec_t *r = Object_GetRecord(id);
		if (!r) {
			return;
		}
		if (params) *params = r->param >= 0 && r->param < M()->num_pool ? &M()->pool[r->param] : NULL;
		if (proc) *proc = rt[t][OBJ_INDEX(id)].proc;
	} else if ((id & 0x3000000) == 0x2000000) {
		if (proc) *proc = Task_FromIndex(OBJ_INDEX(id));
		if (params) *params = NULL;
	}
}

bool Object_IsAwake(ObjId id) { // 0x4497e0
	int t = table_of(id);
	if (t < 0) {
		return (id & 0x3000000) == 0x2000000;
	}
	return Object_GetRecord(id) && (rt[t][OBJ_INDEX(id)].flags & OBJ_FLAG_AWAKE);
}

void Object_SetDead(ObjId id) { // 0x449930
	int t = table_of(id);
	if (t < 0 || !Object_GetRecord(id)) {
		return;
	}
	int i = OBJ_INDEX(id);
	rec_rt_t *s = rec_rt(t, i);
	if (s->flags & OBJ_FLAG_AWAKE) {
		if (t == REC_MOB) {
			MobObj_Sleep(i);
		} else {
			mapobject_sleep(i);
		}
		s->flags |= OBJ_FLAG_AWAKE; // stays "in range" so SetAlive re-wakes it
	}
	s->flags |= OBJ_FLAG_DEAD;
}

void Object_SetAlive(ObjId id) { // 0x449900
	int t = table_of(id);
	if (t < 0 || !Object_GetRecord(id)) {
		return;
	}
	int i = OBJ_INDEX(id);
	rec_rt_t *s = rec_rt(t, i);
	uint8_t f = s->flags;
	s->flags = f & ~OBJ_FLAG_DEAD;
	if (f & OBJ_FLAG_AWAKE) {
		if (t == REC_MOB) {
			s->flags &= ~(OBJ_FLAG_AWAKE | OBJ_FLAG_DEAD);
			MobObj_Wake(i);
		} else {
			mapobject_wake(i);
		}
	}
}

void Object_AdvanceDestroyLevel(ObjId id, int32_t steps) { // 0x449990
	int t = table_of(id);
	map_rec_t *r = Object_GetRecord(id);
	if (!r) {
		return;
	}
	rec_rt_t *s = rec_rt(t, OBJ_INDEX(id));
	rec_advance_destroy_level(r, steps, s->proc ? &s->inst : NULL);
	// the animation state (property 10) follows the new instance (Object_AdvanceDestroyLevel 0x449990)
	objstate_t *st = (objstate_t *)Object_GetProperty(id, PROP_ANIMSTATE);
	if (st) {
		st->inst = s->inst;
	}
}

// ---------------------------------------------------------------- per level

static bool zone_active(int zone) {
	// active zones: the camera zone's PVS within view range (Zones streaming 0x484840)
	level_t *lv = G.level;
	if (!lv->zonedata || lv->zonedata->num_zones == 0) {
		return zone == -1;
	}
	int cz = lv->cam_set_valid && lv->cam_set.num_zones ? lv->cam_set.zones[0] : -1;
	const int16_t *pvs = lv->zonedata->pvs[cz + 1];
	int n = lv->zonedata->pvs_count[cz + 1];
	float range = lv->view_distance + 0x2000;
	for (int k = 0; k < n; k++) {
		if (pvs[k] != zone) {
			continue;
		}
		if (zone < 0) {
			return true;
		}
		ivec3_t o = lv->zonedata->origins[zone];
		return fabsf(o.x - G.cam_pos.x) < range && fabsf(o.y - G.cam_pos.y) < range && fabsf(o.z - G.cam_pos.z) < range;
	}
	return zone == cz;
}

static void add_zones(rec_rt_t *s, const zone_set_t *zs) { // FUN_004452f0: merge a located set
	for (int i = 0; i < zs->num_zones; i++) {
		int k = 0;
		while (k < s->nzones && s->zones[k] != zs->zones[i]) k++;
		if (k == s->nzones && s->nzones < REC_MAX_ZONES) s->zones[s->nzones++] = zs->zones[i];
	}
}

// MapRec_ComputeZones 0x4453b0: a record whose zone field is 0 / -2 gets the zones of its position
// and of 17 points of its rotated bounding box (the 8 corners, the 8 points half-way from the centre
// to them, the centre), so an object spanning several zones wakes in any of them
static void compute_zones(map_rec_t *r, rec_rt_t *s) {
	const zonedata_t *zd = G.level->zonedata;
	s->nzones = 0;
	if (r->zone == -3) {
		s->nzones = -1;
		return;
	}
	if (r->zone == -1 || !zd || zd->num_zones == 0) {
		s->zones[s->nzones++] = -1;
		return;
	}
	if (r->zone > 0) {
		s->zones[s->nzones++] = r->zone;
		return;
	}
	int mode = r->zone == -2 ? -3 : -2;
	zone_set_t zs;
	zoneset_init(zd, &zs, r->pos, mode);
	add_zones(s, &zs);
	const mission_t *m = M();
	if (r->model < 0 || r->model >= m->num_models || !m->bbox) {
		return;
	}
	const int16_t *bb = m->bbox[r->model];
	FwMatrix rot;
	fw_from_packed(r->rot, &rot);
	for (int k = 0; k < 17; k++) {
		int32_t p[3];
		for (int i = 0; i < 3; i++) {
			int32_t mid = (bb[i] + bb[3 + i]) >> 1;
			if (k < 16) {
				p[i] = (k >> i) & 1 ? bb[3 + i] : bb[i];
				if (k > 7) p[i] = (p[i] + mid) >> 1;
			} else {
				p[i] = mid;
			}
		}
		fw_apply(&rot, p, p);
		ivec3_t at = {r->pos.x + p[0], r->pos.y + p[1], r->pos.z + p[2]};
		zoneset_init(zd, &zs, at, mode);
		add_zones(s, &zs);
	}
}
// The map octree (gMap+0x24) files a record in every cell its volume touches, so the wake-up query
// (0x447dd0) finds a record whose volume meets the range box even when its centre is outside: the
// half size of its world box - trigger boxes / planes from their half extents, models from their box.
static void compute_extent(int table, const map_rec_t *r, rec_rt_t *s) {
	int32_t lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
	const int32_t *params = r->param >= 0 && r->param < M()->num_pool ? &M()->pool[r->param] : NULL;
	bool box = table == REC_OBJECT && params && (r->type == OT_TRIGGERBOX || r->type == OT_TRIGGERPLANE);
	if (box) {
		int32_t h[3] = {params[0], r->type == OT_TRIGGERBOX ? params[1] : 0, r->type == OT_TRIGGERBOX ? params[2] : params[1]};
		for (int i = 0; i < 3; i++) { lo[i] = -abs(h[i]); hi[i] = abs(h[i]); }
	} else if (r->model >= 0 && r->model < M()->num_models && M()->bbox) {
		for (int i = 0; i < 3; i++) { lo[i] = M()->bbox[r->model][i]; hi[i] = M()->bbox[r->model][3 + i]; }
	} else {
		memset(s->ext, 0, sizeof(s->ext));
		return;
	}
	FwMatrix m;
	fw_from_packed(r->rot, &m);
	memset(s->ext, 0, sizeof(s->ext));
	for (int k = 0; k < 8; k++) {
		int32_t c[3] = {k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2]}, w[3];
		fw_apply(&m, c, w);
		for (int i = 0; i < 3; i++) s->ext[i] = max(s->ext[i], abs(w[i]));
	}
}

void Map_Reset(void) {
	for (int t = 0; t < 3; t++) {
		free(rt[t]);
		rt[t] = calloc(M()->num_rec[t] + 1, sizeof(rec_rt_t));
		for (int i = 0; i < M()->num_rec[t]; i++) {
			rt[t][i].inst = -1;
			compute_zones(&M()->rec[t][i], &rt[t][i]);
			compute_extent(t, &M()->rec[t][i], &rt[t][i]);
			rt[t][i].prev = (int16_t)(i - 1);
			rt[t][i].next = (int16_t)(i + 1 < M()->num_rec[t] ? i + 1 : -1);
		}
	}
	mob_awake_head = -1;
	mob_sleep_head = M()->num_rec[REC_MOB] ? 0 : -1;
}

void Map_SleepAll(void) { // 0x448130
	if (!M()) {
		return;
	}
	for (int i = 0; i < M()->num_rec[REC_STATIC]; i++) {
		if (rt[REC_STATIC][i].flags & OBJ_FLAG_AWAKE) static_sleep(i);
	}
	for (int i = 0; i < M()->num_rec[REC_OBJECT]; i++) {
		if (rt[REC_OBJECT][i].flags & OBJ_FLAG_AWAKE) mapobject_sleep(i);
	}
	while (mob_awake_head != -1) {
		MobObj_Sleep(mob_awake_head);
	}
}

// ObjActivation_UpdateRange 0x447dd0: wake statics/objects near the focus object, sleep the others
void Map_UpdateProximity(vec3_t focus, float radius) {
	for (int t = REC_STATIC; t <= REC_OBJECT; t++) {
		for (int i = 0; i < M()->num_rec[t]; i++) {
			map_rec_t *r = &M()->rec[t][i];
			rec_rt_t *s = &rt[t][i];
			bool in = false;
			float dx = fabsf(r->pos.x - focus.x), dy = fabsf(r->pos.y - focus.y), dz = fabsf(r->pos.z - focus.z);
			if (dx < radius + s->ext[0] && dy < radius + s->ext[1] && dz < radius + s->ext[2]) {
				if (s->nzones < 0) {
					in = true;
				} else if (min(dx, dz) < radius) {
					for (int k = 0; k < s->nzones && !in; k++) {
						in = zone_active(s->zones[k]);
					}
				}
			}
			bool awake = (s->flags & OBJ_FLAG_AWAKE) != 0;
			if (in && !awake) {
				if (t == REC_STATIC) static_wake(i); else mapobject_wake(i);
			} else if (!in && awake) {
				if (t == REC_STATIC) static_sleep(i); else mapobject_sleep(i);
			}
		}
	}
}

// Objects_QueueVisible 0x447d20 (ObjList_QueueInRange: min(|dx|,|dz|) < distance)
void Objects_QueueVisible(vec3_t from, float distance) {
	for (int t = REC_STATIC; t <= REC_OBJECT; t++) {
		for (int i = 0; i < M()->num_rec[t]; i++) {
			rec_rt_t *s = &rt[t][i];
			map_rec_t *r = &M()->rec[t][i];
			if (!(s->flags & OBJ_FLAG_AWAKE) || (s->flags & OBJ_FLAG_DEAD) || s->inst < 0 || r->model == -1) {
				continue;
			}
			float d = min(fabsf(r->pos.x - from.x), fabsf(r->pos.z - from.z));
			if (d < distance) {
				inst_queue(s->inst);
			}
		}
	}
	for (int i = mob_awake_head; i != -1; i = rt[REC_MOB][i].next) {
		if (M()->rec[REC_MOB][i].model != -1 && rt[REC_MOB][i].inst != -1) {
			inst_queue(rt[REC_MOB][i].inst);
		}
	}
}

// ---------------------------------------------------------------- collision (World_QueryBox kinds 2-4)

// collision meshes of the objects.ppm entries' parts, built when first needed
static phys_mesh_t **part_mesh;
static int part_mesh_count;

// the landers' models (misc\landers.ppm, no model in their mob records): one mesh per model
#define MAX_LANDER_MESHES 16
static phys_mesh_t lander_mesh[MAX_LANDER_MESHES];

static phys_mesh_t *mesh_for_model(const model_t *m) {
	if (!m) return NULL;
	for (int i = 0; i < MAX_LANDER_MESHES; i++) {
		if (!lander_mesh[i].model) phys_mesh_init(&lander_mesh[i], m, -1 - i);
		if (lander_mesh[i].model == m) return lander_mesh[i].octree ? &lander_mesh[i] : NULL;
	}
	return NULL;
}

static void free_part_meshes(void) {
	for (int i = 0; i < MAX_LANDER_MESHES; i++) {
		if (lander_mesh[i].model) phys_mesh_free(&lander_mesh[i]);
	}
	for (int e = 0; e < part_mesh_count; e++) {
		if (!part_mesh[e]) continue;
		for (int k = 0; part_mesh[e][k].model; k++) phys_mesh_free(&part_mesh[e][k]);
		free(part_mesh[e]);
	}
	free(part_mesh);
	part_mesh = NULL;
	part_mesh_count = 0;
}

static phys_mesh_t *mesh_for(int model, int part) { // FUN_00445620: the render item's collision volume
	const ppm_t *objs = G.level->objects;
	if (!objs || model < 0 || model >= objs->count || part >= objs->entries[model].num_parts) {
		return NULL;
	}
	if (!part_mesh) {
		part_mesh_count = objs->count;
		part_mesh = calloc(part_mesh_count, sizeof(*part_mesh));
	}
	if (!part_mesh[model]) {
		const ppm_entry_t *e = &objs->entries[model];
		part_mesh[model] = calloc(e->num_parts + 1, sizeof(phys_mesh_t)); // ends with model == NULL
		for (int k = 0; k < e->num_parts; k++) {
			if (e->parts[k]) phys_mesh_init(&part_mesh[model][k], e->parts[k], model);
		}
	}
	phys_mesh_t *m = &part_mesh[model][part];
	return m->octree ? m : NULL;
}

// 0x445690 + 0x468b30: the part's bounding sphere (world) against the query box
static bool sphere_meets_box(const FwMatrix *m, const model_part_t *p, const int32_t *box) {
	float c[3] = {p->center.x, p->center.y, p->center.z};
	for (int i = 0; i < 3; i++) {
		float w = m->t[i] + (m->m[i][0] * c[0] + m->m[i][1] * c[1] + m->m[i][2] * c[2]) / 4096.0f;
		if (w + p->radius < box[i] || w - p->radius > box[4 + i]) return false;
	}
	return true;
}

// FUN_004456c0: world box -> box around it in the part's frame
static void box_to_local(const int32_t *box, const FwMatrix *m, int32_t *out) {
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

static const phys_matrix_t *object_matrix(uint32_t handle, int32_t time);

// Mob_QueryBox 0x446a30 (and the object version): a moving (class 2) part is tested at both ends
// of the step (property 2 at the step's start and end); the query box is the union of the two
// boxes (the original maps box A to the start pose and box B to the end pose: a superset here)
// Landers have no model in their records: Mob_QueryBox takes them anyway, with the default sphere
// (0x498958: radius 0x280 at the origin) and their instance's model.
static const model_part_t lander_sphere = {.radius = 0x280};

static int query_record(int table, int index, const int32_t *box, phys_cand_t *out, int max) {
	map_rec_t *r = &M()->rec[table][index];
	rec_rt_t *s = &rt[table][index];
	bool lander = table == REC_MOB && r->type == OT_LANDER;
	if ((r->model == -1 && !lander) || s->inst < 0 || !(s->flags & OBJ_FLAG_AWAKE) || (s->flags & OBJ_FLAG_DEAD)) {
		return 0;
	}
	ObjId id = OBJ_NONE;
	int cls = 0;
	if (table != REC_STATIC) {
		cls = ObjectType_GetClass(r->type);
		if (!cls) return 0;
		id = table == REC_MOB ? MOB_ID(r->type, index) : OBJ_ID(r->type, index);
		if ((int32_t)id == phys_g.exclude_owner) return 0;
	}
	const model_info_t *info = r->model >= 0 && r->model < M()->num_models ? &M()->models[r->model] : NULL;
	int n = 0, k = 0;
	for (int16_t i = s->inst; i >= 0 && n < max; i = gInstances[i].next_part, k++) {
		phys_mesh_t *mesh = lander ? mesh_for_model(gInstances[i].model) : mesh_for(r->model, k);
		if (!mesh) continue;
		uint32_t handle = ((uint32_t)k << 26) | id;
		const FwMatrix *m = coord_world(gInstances[i].coord), *m1 = m;
		if (cls == 2) {
			m = (const FwMatrix *)object_matrix(handle, phys_g.in_physics ? phys_g.step_start : gGameUpdateTime);
			m1 = phys_g.in_physics ? (const FwMatrix *)object_matrix(handle, phys_g.step_end) : m;
		}
		const model_part_t *sphere = lander ? &lander_sphere : info && k < info->num_parts ? &info->parts[k] : NULL;
		if (sphere && !sphere_meets_box(m, sphere, box) && (m1 == m || !sphere_meets_box(m1, sphere, box))) {
			continue;
		}
		int32_t lb[8];
		box_to_local(box, m, lb);
		if (m1 != m) {
			int32_t lb1[8];
			box_to_local(box, m1, lb1);
			for (int a = 0; a < 3; a++) {
				if (lb1[a] < lb[a]) lb[a] = lb1[a];
				if (lb1[4 + a] > lb[4 + a]) lb[4 + a] = lb1[4 + a];
			}
		}
		n += phys_collect_part(mesh, lb, table == REC_STATIC ? (const phys_matrix_t *)m : NULL, (uint32_t)i,
			handle, out + n, max - n);
	}
	return n;
}

// statics (mask 4), objects (8), awake mobs (0x10); the original finds statics and objects through
// the map octree (0x4478e0), a plain scan finds the same records
static int query_objects(phys_world_t *w, const int32_t *box, phys_cand_t *out, int max) {
	int n = 0;
	for (int t = REC_STATIC; t <= REC_OBJECT; t++) {
		if (!(w->query_mask & (t == REC_STATIC ? 4 : 8))) continue;
		for (int i = 0; i < M()->num_rec[t] && n < max; i++) {
			n += query_record(t, i, box, out + n, max - n);
		}
	}
	if (w->query_mask & 0x10) {
		for (int i = mob_awake_head; i != -1 && n < max; i = rt[REC_MOB][i].next) {
			n += query_record(REC_MOB, i, box, out + n, max - n);
		}
	}
	return n;
}

static int object_class(uint32_t handle) { return ObjectType_GetClass(OBJ_TYPE(handle)); }

// class 1: the part's node. Class 2 (Frame_Select 0x42ba30, Mob_QueryBox 0x446a30): property 2
// asked with the query slot and time set (DAT_0058b3c0 / DAT_0058b394): slot 0 at the step's
// start (or the game time outside the step), slot 1 at its end. A body (lander, pod) answers with
// the body interpolated to that time, an animated object with its animation evaluated at it; the
// two answers are different pointers, which makes the physics treat the part as a moving frame. A
// still object answers its node both times (the same pointer: a still frame).
static FwMatrix body_pose[PHYS_MAX_BODIES][2];

static const phys_matrix_t *object_matrix(uint32_t handle, int32_t time) {
	static FwMatrix identity = {{{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}, 0, {0, 0, 0}};
	if (ObjectType_GetClass(OBJ_TYPE(handle)) == 2) {
		if (!phys_g.in_physics) time = gGameUpdateTime;
		gPhysQuerySlot = phys_g.in_physics && time == phys_g.step_end ? 1 : 0;
		gPhysQueryTime = time;
		phys_body_t *b = physobjs_object_body(handle);
		if (b && phys_g.in_physics) {
			FwMatrix *m = &body_pose[b - phys_bodies][gPhysQuerySlot];
			phys_body_interpolate(b, (phys_matrix_t *)m, time);
			for (int k = 0; k < 3; k++) m->t[k] >>= 3;
			return (const phys_matrix_t *)m;
		}
		const FwMatrix *m = b ? (const FwMatrix *)Object_GetProperty(handle, PROP_MATRIX)
			: anim_pose_at(handle, time, gPhysQuerySlot);
		if (m) return (const phys_matrix_t *)m;
	}
	int16_t i = Object_GetInstance(handle & 0x03ffffff);
	for (int part = (handle >> 26) & 0x3f; part > 0 && i >= 0; part--) {
		i = gInstances[i].next_part;
	}
	return (const phys_matrix_t *)(i >= 0 ? coord_world(gInstances[i].coord) : &identity);
}

static int object_surface(uint32_t handle) { // MapRec_GetPartAux 0x4475b0
	map_rec_t *r = Object_GetRecord(handle & 0x03ffffff);
	int part = (handle >> 26) & 0x3f;
	if (!r || r->model < 0 || r->model >= M()->num_models || part >= M()->models[r->model].num_parts) {
		return 0;
	}
	return M()->models[r->model].parts[part].aux;
}

static void touch_report(int32_t owner, uint32_t handle, int32_t impulse) { // 0x413af0
	if (impulse == 0 || impulse > 0x80) {
		LanderEvent ev = {EV_COLLISION, {(intptr_t)owner, (intptr_t)handle, (intptr_t)impulse}};
		Event_Raise(&ev);
	}
}

// at mission unload, while the models the meshes read are still loaded
void Objects_FreeCollision(void) { free_part_meshes(); }

void Objects_SetupCollision(phys_world_t *w) {
	free_part_meshes();
	w->query_objects = query_objects;
	w->object_class = object_class;
	w->object_matrix = object_matrix;
	w->object_body = physobjs_object_body;
	w->object_surface = object_surface;
	w->touch_report = touch_report;
}