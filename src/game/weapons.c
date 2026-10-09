// Combat: damage (event 9 handler 0x453700, Switch_OnHit 0x459020), destruction
// (Object_Destroyed 0x459190), Destructible (type 8), the lander's target scan (0x4269f0,
// 0x426970) and the module set-up. Projectiles, explosions and turrets are in weapons_*.c.
// re/specs/weapons.md.
#include <stdio.h>

#include "weapons_internal.h"

bool weapons_debug;   // LANDER_DEBUG: print the damage done

// ---------------------------------------------------------------- damage

// event 9 {object, damage, shooter, kind} (0x453700)
static void on_projectile_hit(const LanderEvent *ev) {
	ObjId id = (ObjId)ev->arg[0];
	int32_t dmg = (int32_t)ev->arg[1];
	if (OBJ_TYPE(id) == OT_LANDER) {
		dmg = Lander_ArmourScaleDamage(id, dmg, (int32_t)ev->arg[3]);
	} else if (OBJ_TYPE(id) == OT_SWITCH) {
		return;
	}
	int32_t hp = (int32_t)Object_GetProperty(id, PROP_HITPOINTS);
	if (!hp) return;
	int32_t ar = (int32_t)Object_GetProperty(id, PROP_ARMOUR);
	if (ar > 0xfe) return;  // indestructible
	if (ar) {
		if (ar > 0x80) ar = ar * 4 - 0x180;
		hp -= (dmg << 7) / ar;
		if (hp < 0) hp = 0;
	} else {
		hp = 0;
	}
	if (weapons_debug) {
		printf("weapons: %08x hit by %08x (kind %d, damage %d): hp %d -> %d\n", id, (uint32_t)ev->arg[2],
			(int)ev->arg[3], dmg, (int)Object_GetProperty(id, PROP_HITPOINTS), hp);
	}
	gDamageSource = (int32_t)ev->arg[2];
	Object_SetProperty(id, PROP_HITPOINTS, hp);
	gDamageSource = -1;
}

// Switch_OnHit 0x459020: a shot toggles an unlocked switch, at most once a second. The exe keeps
// the last toggle time in the switch's process (+0x10, set to the wake time by Switch_Create); the
// port keeps it in the process's free `mask` word, 0 until the first toggle [A].
static void on_switch_hit(const LanderEvent *ev) {
	ObjId id = (ObjId)ev->arg[0];
	if (OBJ_TYPE(id) != OT_SWITCH) return;
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(id, &params, &t);
	if (!params || !t || !t->type || ((uint8_t *)params)[1]) return;
	objstate_t *o = objstate(t);
	if ((uint32_t)(o->mask + 1000) > (uint32_t)gGameUpdateTime && o->mask) return;
	int state = 1 - ((uint8_t *)params)[0];
	Object_SetProperty(id, PROP_STATE, state);
	o->mask = (uint32_t)gGameUpdateTime;
	LanderEvent sw = {EV_SWITCH, {(intptr_t)id, (intptr_t)state, ev->arg[2]}};
	Event_Raise(&sw);
}

// ---------------------------------------------------------------- destruction

int16_t weapons_object_instance(ObjId id) { return Object_GetInstance(id); } // 0x449770

int32_t weapons_destroy_step(ObjId id) { // 0x449960 -> Map/Mob_GetDestroyStep -> Model_GetDestroyStep
	map_rec_t *r = Object_GetRecord(id);
	const mission_t *m = G.level ? G.level->mission : NULL;
	if (!r || !m) return 0;
	int32_t d = r->model >= 0 && r->model < m->num_models ? m->destroy_step[r->model] : 0;
	return d == -1 ? 1 : d;
}

void weapons_destroy_core(ObjId id, int32_t size) { // Object_Destroyed 0x459190
	if (weapons_object_instance(id) != -1 && size != 0) {
		const FwMatrix *m = (const FwMatrix *)Object_GetProperty(id & 0x3ffffff, PROP_MATRIX);
		if (m) explosion_create(0, size, m, (ObjId)gDamageSource);
	}
	int32_t step = weapons_destroy_step(id);
	if (step) Object_AdvanceDestroyLevel(id, step);
	LanderEvent ev = {EV_DESTROYED, {(intptr_t)id, (intptr_t)gDamageSource, (intptr_t)size}};
	Event_Raise(&ev);
}

// The hit-point setters call this when the hit points reach 0. AnimDestructible / Pickup (types 15,
// 19, setter 0x458940 in the exe) first stop their animation and take the rebuilt model instance.
void Object_Destroyed(ObjId id, int32_t size) {
	int type = OBJ_TYPE(id);
	if (type != OT_ANIMDESTRUCTIBLE && type != OT_PICKUP) {
		weapons_destroy_core(id, size);
		return;
	}
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(id, &params, &t);
	objstate_t *o = t && t->type ? objstate(t) : NULL;
	if (o ? (o->flags & 8) : (params && (((uint8_t *)params)[0] & 8))) AnimStop(id, 0);
	weapons_destroy_core(id, size);
	if (o) o->inst = Object_GetInstance(id);
}

// ---------------------------------------------------------------- targeting (Lander_ScanTargets)

// volumes in the model frame (PTR 0x497d30): corners and outward normals; plane i passes through
// corner plane_corner[i] (0x4996f0)
static const int32_t vol_corners[2][8][3] = {
	{{-256, -256, 0}, {-4096, -4096, 12800}, {-256, 256, 0}, {-4096, 4096, 12800},  // 0x497b70 homing
	 {256, -256, 0}, {4096, -4096, 12800}, {256, 256, 0}, {4096, 4096, 12800}},
	{{-200, -200, 0}, {-2400, -2400, 12800}, {-200, 200, 0}, {-2400, 2400, 12800},  // 0x497c50 guns
	 {200, -200, 0}, {2400, -2400, 12800}, {200, 200, 0}, {2400, 2400, 12800}},
};
static const int32_t vol_normals[2][6][3] = {
	{{0xf53, 0, -0x499}, {-0xf53, 0, -0x499}, {0, 0xf53, -0x499}, {0, -0xf53, -0x499}, {0, 0, 0x1000}, {0, 0, -0x1000}},
	{{0xff1, 0, -0x15f}, {-0xff1, 0, -0x15f}, {0, 0xff1, -0x15f}, {0, -0xff1, -0x15f}, {0, 0, 0x1000}, {0, 0, -0x1000}},
};
static const uint8_t plane_corner[6] = {4, 0, 2, 0, 1, 0};

static int32_t vol_c[8][3], vol_n[6][3], vol_min[3], vol_max[3];  // 0x4a6f70 / 0x4a6f10
static int32_t target_fwd[4] = {0, 0, 0x1000, 0};

static bool in_volume(const int32_t *p) { // 0x4686e0
	for (int k = 0; k < 3; k++) {
		if (p[k] < vol_min[k] || p[k] > vol_max[k]) return false;
	}
	for (int i = 0; i < 6; i++) {
		const int32_t *c = vol_c[plane_corner[i]];
		int64_t d = (int64_t)vol_n[i][0] * (p[0] - c[0]) + (int64_t)vol_n[i][1] * (p[1] - c[1]) +
			(int64_t)vol_n[i][2] * (p[2] - c[2]);
		if (d > 0) return false;
	}
	return true;
}

static bool targetable(int mode, ObjId id) { // 0x426970
	uint32_t f = (uint32_t)Object_GetProperty(id, PROP_FLAGS);
	if (!((mode == 0 && (f & 0x4000)) || (mode == 1 && (f & 0x8000)))) return false;
	if (OBJ_TYPE(id) != OT_SWITCH && Object_GetProperty(id, PROP_HITPOINTS) == 0) return false;
	const int32_t *p = (const int32_t *)Object_GetProperty(id, PROP_POSITION);
	return p && in_volume(p);
}

void eng_targets_scan(int32_t mode, const FwMatrix *w, int16_t *out, ObjId owner) { // 0x4269f0
	int n = 0;
	const int max = 48;   // Lander.targetList holds 49 entries (the exe does not check)
	mode = mode ? 1 : 0;
	for (int i = 0; i < 8; i++) {
		wm_apply(w, vol_corners[mode][i], vol_c[i]);
		wv_add(w->t, vol_c[i], vol_c[i]);
		for (int k = 0; k < 3; k++) {
			if (!i || vol_c[i][k] < vol_min[k]) vol_min[k] = vol_c[i][k];
			if (!i || vol_c[i][k] > vol_max[k]) vol_max[k] = vol_c[i][k];
		}
	}
	for (int i = 0; i < 6; i++) wm_apply(w, vol_normals[mode][i], vol_n[i]);
	wv_copy(vol_n[4], target_fwd);
	const mission_t *m = G.level ? G.level->mission : NULL;
	if (m) {
		// map objects (the exe walks the map octree with the volume; objects without a process are
		// asleep beyond the view range or dead)
		for (int i = 0; i < m->num_rec[REC_OBJECT] && n < max; i++) {
			ObjId id = OBJ_ID(m->rec[REC_OBJECT][i].type, i);
			task_t *t = NULL;
			Objects_GetData(id, NULL, &t);
			if (!t || !t->type) continue;
			if (targetable(mode, id)) out[n++] = (int16_t)i;
		}
		for (int i = 0; i < m->num_rec[REC_MOB] && n < max; i++) {
			if (!eng_mob_is_awake(i)) continue;
			ObjId id = MOB_ID(m->rec[REC_MOB][i].type, i);
			if (id == owner) continue;
			if (targetable(mode, id)) out[n++] = (int16_t)(i | 0x8000);
		}
	}
	out[n] = -1;
}

ObjId eng_target_list_id(int16_t entry) {
	const mission_t *m = G.level ? G.level->mission : NULL;
	uint16_t e = (uint16_t)entry;
	if (!m) return OBJ_NONE;
	if (e & 0x8000) {
		int i = e & 0x7fff;
		return i < m->num_rec[REC_MOB] ? MOB_ID(m->rec[REC_MOB][i].type, i) : OBJ_NONE;
	}
	return e < m->num_rec[REC_OBJECT] ? OBJ_ID(m->rec[REC_OBJECT][e].type, e) : OBJ_NONE;
}

const int32_t *eng_target_forward(void) { return target_fwd; }

// ---------------------------------------------------------------- set-up

// Debug aid for the headless tests (not in the exe): LANDER_WEAPONS_TEST="x,y,z,yaw" moves the
// player's lander there 1 ms into the mission, facing yaw (4096 = 360 degrees).
static void test_place(void) {
	const char *s = getenv("LANDER_WEAPONS_TEST");
	int32_t x, y, z, yaw = 0;
	if (!s || sscanf(s, "%d,%d,%d,%d", &x, &y, &z, &yaw) < 3 || gNbPlayers < 1) return;
	ObjId lander = gPlayerObjects[0];
	FwMatrix m;
	fw_identity(&m);
	fw_rotate_y(&m, yaw);
	int32_t pos[3] = {x, y, z};
	Object_SetProperty(lander, PROP_MATRIX, (intptr_t)&m);
	Object_SetProperty(lander, PROP_POSITION, (intptr_t)pos);
	G.level->cam_set_valid = false;   // the camera's zone tracker cannot follow a jump through walls
}

// Debug aid: LANDER_WEAPONS_PROBE="x0,y0,z0,x1,y1,z1" traces a projectile ray 2 s into the mission
static void test_probe(void) {
	const char *probe = getenv("LANDER_WEAPONS_PROBE");
	int32_t a[3], b[3];
	if (probe && sscanf(probe, "%d,%d,%d,%d,%d,%d", &a[0], &a[1], &a[2], &b[0], &b[1], &b[2]) == 6) {
		phys_hit_t h;
		memset(&h, 0, sizeof(h));
		int r = fx_segment_hit(a, b, 7, &h, OBJ_NONE);
		printf("weapons: probe hit %d kind %d object %08x at %d %d %d\n", r, h.frame_kind, (uint32_t)h.object,
			h.point[0], h.point[1], h.point[2]);
	}
	const char *ids = getenv("LANDER_WEAPONS_PROBE_ID");
	if (ids) {
		ObjId id = (ObjId)strtoul(ids, NULL, 16);
		map_rec_t *r = Object_GetRecord(id);
		int16_t inst = Object_GetInstance(id);
		const model_t *mdl = inst >= 0 ? gInstances[inst].model : NULL;
		printf("weapons: probe %08x awake %d inst %d model %d octree %p class %d; camera zones %d: %d %d\n", id,
			Object_IsAwake(id), inst, r ? r->model : -1,
			mdl ? (const void *)model_tagged_block(mdl, TAG_POLY_OCTREE, NULL) : NULL, ObjectType_GetClass(OBJ_TYPE(id)),
			G.level->cam_set.num_zones, G.level->cam_set.zones[0], G.level->cam_set.zones[1]);
	}
}

void weapons_register_types(void) {
	turret_register();
	ObjectType_Register(OT_DESTRUCTIBLE, NULL, NULL, objtype_plain_create, NULL, 1); // Destructible_Create 0x4590d0
	objprops_common(OT_DESTRUCTIBLE);
	objprops_destructible(OT_DESTRUCTIBLE, 0);
	projectile_register();
	explosion_register();
}

void weapons_reset(void) { // per mission, before the script starts
	turret_reset();
	projectile_reset();
	explosion_reset();
	fx_reset();
	gDamageSource = -1;
	weapons_debug = getenv("LANDER_DEBUG") != NULL;
	// Events_RegisterAll 0x4493e0 adds these two before any script handler
	EventType_AddHandler(EV_PROJECTILE_HIT, NULL, on_projectile_hit);
	EventType_AddHandler(EV_PROJECTILE_HIT, NULL, on_switch_hit);
	if (getenv("LANDER_WEAPONS_TEST")) InTime0(1, test_place);
	if (getenv("LANDER_WEAPONS_PROBE")) InTime0(2000, test_probe);
	const char *arms = getenv("LANDER_WEAPONS_ARMS");  // debug: "primary,secondary,missiles"
	int pri, sec, mis;
	if (arms && sscanf(arms, "%d,%d,%d", &pri, &sec, &mis) == 3) {
		gPlayerState.primary = pri;
		gPlayerState.secondary = sec;
		gPlayerState.missiles = mis;
	}
}
