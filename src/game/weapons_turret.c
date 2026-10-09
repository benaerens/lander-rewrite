// Turrets: Turret (type 0: update 0x454210, create 0x454260, destroy 0x454280, properties
// 0x4542c0..0x454440) and AnimTurret (type 14: 0x458a10 / 0x458a60 / 0x458aa0 / 0x458ae0,
// properties 0x458b40..0x458dd0) sharing Turret_InitParams 0x4540b0, the aim 0x453940 and the
// gun update 0x453c30. Behaviour records gTurretTypes (TurretTypes_Reset 0x4541d0).
// re/specs/weapons.md §4, mission-api.md §6.3.
#include "weapons_internal.h"

// the built-in records 0x498f20 (20 x 0x1c): muzzle, weapon, twin, burst, shot delay, burst delay,
// turn rate, elevation limit, range
static const TurretType default_types[20] = {
	{{75, 0, 281, 0}, 0, 1, 1, 4, 70, 0x20, 1024, 8192, 0},
	{{0, 0, 145, 0}, 1, 0, 1, 0, 75, 0x20, 1024, 12000, 0},
	{{0, 0, 396, 0}, 0, 0, 4, 1, 25, 0x20, 800, 6500, 0},
	{{0, 18, 158, 0}, 0, 0, 1, 0, 25, 0x20, 800, 6500, 0},
	{{0, 24, 240, 0}, 0, 0, 1, 0, 25, 0x20, 800, 6500, 0},
	{{85, 434, 461, 0}, 2, 2, 2, 60, 60, 0x20, 512, 12000, 0},
	{{166, 243, 576, 0}, 2, 1, 1, 0, 50, 0x20, 512, 12000, 0},
	{{-19, 0, 793, 0}, 0, 0, 1, 0, 15, 0x4b, 400, 6500, 0},
	{{0, 0, 544, 0}, 0, 0, 1, 0, 10, 0x4b, 400, 6500, 0},
	{{0, 128, 342, 0}, 1, 0, 4, 2, 20, 0x32, 700, 8000, 0},
	{{112, 0, 601, 0}, 0, 1, 1, 0, 10, 0x4b, 400, 6500, 0},
	{{0, 216, 442, 0}, 2, 0, 1, 0, 25, 0x20, 512, 12000, 0},
	{{0, 0, 544, 0}, 0, 0, 1, 0, 15, 0x3c, 400, 6500, 0},
	{{61, 0, 198, 0}, 5, 2, 2, 4, 25, 0x20, 800, 6500, 0},
	{{0, 54, 333, 0}, 0, 0, 1, 0, 25, 0x20, 800, 6500, 0},
	{{61, 0, 198, 0}, 0, 2, 4, 5, 50, 0x20, 800, 6500, 0},
	{{0, 0, 134, 0}, 1, 0, 6, 5, 50, 0x20, 800, 6500, 0},
	{{61, 0, 198, 0}, 0, 2, 4, 5, 40, 0x20, 800, 6500, 0},
	{{0, 0, 0, 0}, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{{0, 0, 0, 0}, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

// turret state (0x14 bytes at task +0x0c / +0x18 in the exe), kept per process
typedef struct {
	ObjId id;            // +0x00 (packed in the exe)
	coord_t *gun;        // +0x02 gun node (the last part)
	int16_t yaw, pitch;  // +0x04 / +0x06 current
	int16_t tyaw, tpitch;// +0x08 / +0x0a wanted
	uint8_t timer;       // +0x0c ticks to the next shot / burst
	uint8_t shots;       // +0x0d shots left in the burst
	uint8_t type;        // +0x0e gTurretTypes slot
	uint8_t parts;       // +0x0f
	int32_t sound;       // +0x10 turning loop
} turret_t;

static turret_t turrets[MAX_TASKS];
static int32_t single_aim[3];   // 0x553b48: direction of a one-part turret's shot

static turret_t *turret_of(task_t *t) { return &turrets[Task_ToIndex(t)]; }

// the record's pool bytes {hp, armour, explosion size, type | 0x80 inactive} and the target word
static uint8_t *turret_pool(task_t *t) {
	objstate_t *o = objstate(t);
	if (!o->params) return NULL;
	return (uint8_t *)(OBJ_TYPE(o->id) == OT_ANIMTURRET ? o->params + 1 : o->params);
}

static int effective_twin(const TurretType *tt) { // Turret_InitParams clears twin 2 of one-shot bursts
	return tt->twin == 2 && tt->burst == 1 ? 0 : tt->twin;
}

// 0x453810: hang `child` (a world-space part node) under `parent`
static void reparent(coord_t *child, coord_t *parent) {
	const FwMatrix *pw = coord_world(parent);
	int32_t d[3];
	wv_sub(child->local.t, parent->local.t, d);
	wm_apply_t(pw, d, child->local.t);
	int32_t t3[3];
	wv_copy(child->local.t, t3);
	fw_identity(&child->local);
	wv_copy(t3, child->local.t);
	child->parent = parent;
}

static void turret_init(turret_t *s, const uint8_t *pool, ObjId id, int16_t inst) { // Turret_InitParams 0x4540b0
	memset(s, 0, sizeof(*s));
	s->id = id;
	s->sound = 0;
	if (!pool || pool[0] == 0) return;
	s->type = pool[3] & 0x7f;
	const TurretType *tt = gTurretTypes[s->type];
	coord_t *part[3] = {NULL, NULL, NULL};
	int n = 0;
	for (int16_t i = inst; i >= 0 && i < MAX_INSTANCES; i = gInstances[i].next_part) {
		if (n < 3) part[n] = gInstances[i].coord;
		n++;
	}
	if (!n) return;
	coord_t *gun = part[0];
	if (n > 1) {
		gun = part[1];
		if (n > 2) {
			gun = part[2];
			reparent(part[2], part[1]);
		}
		reparent(part[1], part[0]);
		fw_rotate_y(&part[1]->local, 0x400);
	}
	s->gun = gun;
	s->timer = tt ? tt->burstDelay : 0;
	s->yaw = 0x400;
	s->pitch = 0;
	s->shots = 0;
	s->parts = (uint8_t)n;
}

// 0x426150: cos of the angle whose sine is s (4.12)
static int32_t cos_from_sin(int32_t s) {
	int32_t v = 0x1000 - ((s * s) >> 12);
	return v < 0 ? 0 : fx_sqrt(v) << 6;
}

// 0x453940: wanted yaw (in A's frame) and gun pitch (in B's frame); nonzero = target in reach
static int turret_aim(const FwMatrix *A, const FwMatrix *B, ObjId target, turret_t *s, const TurretType *tt) {
	const int32_t *tp = NULL;
	int32_t dist = 0x7fffffff;
	if (target == OBJ_NONE) {
		dist = 0x7fff0000;
		for (int i = 0; i < gNbPlayers; i++) { // 0x4538e0: nearest player
			const int32_t *p = (const int32_t *)Object_GetProperty(gPlayerObjects[i], PROP_POSITION);
			int32_t d = p ? w_abs(p[2] - A->t[2]) + w_abs(p[1] - A->t[1]) + w_abs(p[0] - A->t[0]) : 0x7fffffff;
			if (d < dist) {
				dist = d;
				tp = p;
			}
		}
	} else {
		tp = (const int32_t *)Object_GetProperty(target, PROP_POSITION);
		if (tp) dist = w_abs(tp[2] - A->t[2]) + w_abs(tp[1] - A->t[1]) + w_abs(tp[0] - A->t[0]);
	}
	if (!tp || dist >= tt->range) {
		s->tyaw = 0x400;
		s->tpitch = 0;
		return 0;
	}
	int32_t v[3], g[3], w[3];
	wv_sub(tp, B->t, v);
	if (s->parts == 1) {
		wv_copy(v, single_aim);
		phys_normalise(single_aim);
	}
	wm_apply(B, s->gun->local.t, g);
	wv_sub(v, g, w);
	wm_apply_t(A, v, v);
	wm_apply_t(B, w, w);
	if (w_abs(v[0]) + w_abs(v[2]) > 0x80) {
		s->tyaw = (int16_t)fx_ratan2(v[0], v[2]);
		int32_t m = s->gun->local.t[0] + tt->muzzle[0];
		if (m != 0 && effective_twin(tt) == 0) { // an off-axis gun: aim the muzzle, not the pivot
			int32_t r = fx_sqrt(v[0] * v[0] + v[2] * v[2]);
			int32_t sn = r ? (w_abs(m) << 12) / r : 0;
			if (sn) {
				int16_t a = (int16_t)(0x400 - w_catan((cos_from_sin(sn) << 12) / sn));
				s->tyaw = (int16_t)(m < 0 ? s->tyaw + a : s->tyaw - a);
			}
		}
	} else {
		s->tyaw = 0x400;
	}
	if (w_abs(w[2]) + w_abs(w[1]) <= 0x80) {
		s->tpitch = 0;
		return 0;
	}
	s->tpitch = (int16_t)(fx_ratan2(w[2], w[1]) - 0x400);
	int32_t m2 = tt->muzzle[1];
	if (m2) {
		int32_t r = fx_sqrt(w[1] * w[1] + w[2] * w[2]);
		int32_t sn = r ? (w_abs(m2) << 12) / r : 0;
		if (sn) {
			int16_t a = (int16_t)(0x400 - w_catan((cos_from_sin(sn) << 12) / sn));
			s->tpitch = (int16_t)(m2 < 0 ? s->tpitch - a : s->tpitch + a);
		}
	}
	if (s->tpitch < -(int32_t)tt->maxPitch) {
		s->tpitch = (int16_t)-tt->maxPitch;
		return 1;
	}
	if (s->tpitch > 0) { // below the gun: no shot
		s->tpitch = 0;
		return 0;
	}
	return 1;  // the exe returns the limit word here (nonzero for every record on the disc)
}

static int32_t turn_step(int32_t from, int32_t to, int32_t rate) { // 0x453c00
	int32_t d = (int32_t)((uint32_t)(to - from) << 20) >> 20;
	return d > rate ? rate : d < -rate ? -rate : d;
}

static void turret_fire(turret_t *s, const TurretType *tt, const uint8_t *pool) {
	proj_params_t p;
	memset(&p, 0, sizeof(p));
	int kind = tt->weapon & 0xf, variant = tt->weapon >> 4;
	p.kind = (uint8_t)kind;
	p.zero = kind == 5 ? 3 : 1;   // muzzle flash (and bouncing for kind 5)
	p.f04 = 0x96;
	p.f08 = 1;
	p.arg2 = 0x10;
	switch (kind) {
	case 0: case 8: p.arg2 = variant == 1 ? 0x20 : variant == 2 ? 0x30 : variant == 3 ? 0x40 : 0x13; break;
	case 1: case 2: p.arg2 = 0x40; break;
	case 5: case 6: p.arg2 = 8; break;
	case 9:
		p.arg3 = variant;
		p.arg2 = variant > 5 ? 0x10 : (variant % 3 == 0 ? 0x18 : variant % 3 == 1 ? 0x28 : 0x40);
		break;
	}
	p.muzzle[0] = tt->muzzle[0];
	p.muzzle[1] = tt->muzzle[1];
	p.muzzle[2] = tt->muzzle[2];
	FwMatrix single;
	if (s->parts == 1) { // only the Z column (the aim) and t are set in the exe
		const FwMatrix *gw = coord_world(s->gun);
		fw_identity(&single);
		single.m[0][2] = (int16_t)single_aim[0];
		single.m[1][2] = (int16_t)single_aim[1];
		single.m[2][2] = (int16_t)single_aim[2];
		wm_orient_z(&single);
		wv_copy(gw->t, single.t);
		p.world = &single;
	} else {
		p.world = coord_world(s->gun);
	}
	p.target = (ObjId)((const int32_t *)pool)[1];
	p.owner = s->id;
	int twin = effective_twin(tt);
	if (twin == 2 && (s->shots & 1)) p.muzzle[0] = -p.muzzle[0];
	projectile_create(&p);
	if (twin == 1) { // mirrored pair
		p.muzzle[0] = -p.muzzle[0];
		gProjectileSecondShot = 1;
		projectile_create(&p);
		gProjectileSecondShot = 0;
	}
	if (--s->shots == 0) s->timer = tt->burstDelay;
	else s->timer = tt->shotDelay;
}

static void turret_tick(turret_t *s, const uint8_t *pool) { // 0x453c30
	if (!pool) return;
	if (pool[0] == 0) { // destroyed
		if (s->sound) eng_snd_stop(s->sound);
		s->sound = 0;
		return;
	}
	const TurretType *tt = s->type < TURRET_TYPE_SLOTS ? gTurretTypes[s->type] : NULL;
	if (!tt || !s->gun) return;  // slots 20..31 the mission does not set (a NULL read in the exe)
	coord_t *A, *B;
	switch (s->parts) {
	case 1: A = B = s->gun; break;
	case 2: A = B = s->gun->parent; break;
	case 3: B = s->gun->parent; A = B ? B->parent : NULL; break;
	default: return;   // the exe uses stack garbage for > 3 parts
	}
	if (!A || !B) return;
	int fire_ok;
	if (pool[3] & 0x80) { // disabled (property 5)
		s->tyaw = 0x400;
		s->tpitch = 0;
		fire_ok = 0;
	} else {
		const FwMatrix *bw = coord_world(B);
		FwMatrix bcopy = *bw;
		fire_ok = turret_aim(coord_world(A), &bcopy, (ObjId)((const int32_t *)pool)[1], s, tt);
	}
	int32_t yaw_step = 0, pitch_step = 0;   // (the exe leaves the yaw step uninitialised for 2 parts)
	bool yaw_max = false, pitch_max = false;
	if (s->parts == 3) {
		yaw_step = turn_step(s->yaw, s->tyaw, tt->turnRate);
		if (yaw_step) {
			s->yaw = (int16_t)(s->yaw + yaw_step);
			if (w_abs(yaw_step) == tt->turnRate) yaw_max = true;
			if (s->yaw == s->tyaw) yaw_step = 0;
			int32_t t3[3];
			wv_copy(B->local.t, t3);
			fw_identity(&B->local);
			wv_copy(t3, B->local.t);
			fw_rotate_y(&B->local, s->yaw);
		}
	}
	if (s->parts > 1) {
		pitch_step = turn_step(s->pitch, s->tpitch, tt->turnRate);
		if (pitch_step) {
			s->pitch = (int16_t)(s->pitch + pitch_step);
			if (w_abs(pitch_step) == tt->turnRate) pitch_max = true;
			if (s->pitch == s->tpitch) pitch_step = 0;
			int32_t t3[3];
			wv_copy(s->gun->local.t, t3);
			fw_identity(&s->gun->local);
			wv_copy(t3, s->gun->local.t);
			wm_rotate_x(&s->gun->local, s->pitch);
		}
	}
	if (!yaw_max && !pitch_max) { // turning sound while a joint turns at full rate
		if (s->sound) eng_snd_stop(s->sound);
		s->sound = 0;
	} else if (!s->sound) {
		eng_snd_once(s->id, 0x78);
		s->sound = eng_snd_loop(s->id, 0x79, -1);
	}
	if (s->shots == 0) {
		if (s->timer == 0) s->shots = tt->burst;
		if (s->shots == 0) goto end;
	}
	if (s->timer == 0 && fire_ok && !yaw_step && !pitch_step) turret_fire(s, tt, pool);
end:
	if (s->timer) s->timer--;
}

// ---------------------------------------------------------------- Turret (type 0)

static void turret_create(task_t *t, void *arg) { // Turret_Create 0x454260
	obj_args_t *a = arg;
	objstate_init(t, a);
	turret_init(turret_of(t), turret_pool(t), objstate(t)->id, objstate(t)->inst);
}

static void turret_update(task_t *t) { turret_tick(turret_of(t), turret_pool(t)); } // 0x454210

static void turret_destroy(task_t *t) { // 0x454280
	turret_t *s = turret_of(t);
	if (s->sound) eng_snd_stop(s->sound);
	s->sound = 0;
}

static turret_t *turret_from_id(ObjId id, task_t **out) {
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	if (out) *out = t && t->type ? t : NULL;
	return t && t->type ? turret_of(t) : NULL;
}

static uint8_t *pool_of(ObjId id) {
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (!params) return NULL;
	return (uint8_t *)(OBJ_TYPE(id) == OT_ANIMTURRET ? params + 1 : params);
}

static intptr_t turret_get_pos(ObjId id) { // 0x4542c0: the head (or the gun of a one/two-part turret)
	task_t *t;
	turret_t *s = turret_from_id(id, &t);
	uint8_t *pool = pool_of(id);
	if (s && pool && pool[0] && s->gun) {
		coord_t *c = s->parts < 2 ? s->gun : s->gun->parent;
		if (c) return (intptr_t)coord_world(c)->t;
	}
	map_rec_t *r = Object_GetRecord(id);
	return r ? (intptr_t)&r->pos : 0;
}

static intptr_t turret_get_target(ObjId id) { uint8_t *p = pool_of(id); return p ? ((int32_t *)p)[1] : 0; }
static void turret_set_target(ObjId id, intptr_t v) { uint8_t *p = pool_of(id); if (p) ((int32_t *)p)[1] = (int32_t)v; }
static intptr_t turret_get_flags(ObjId id) { // 0x4543e0
	uint8_t *p = pool_of(id);
	return p && p[0] && !(p[3] & 0x80) ? 0xc000 : 0;
}
static void turret_set_enable(ObjId id, intptr_t v) { // 0x454440 / 0x458d70
	uint8_t *p = pool_of(id);
	if (!p) return;
	if (v) p[3] &= 0x7f;
	else p[3] |= 0x80;
}

// ---------------------------------------------------------------- AnimTurret (type 14)

static uint32_t anim_period(task_t *t) {
	objstate_t *o = objstate(t);
	uint8_t ft = o->params ? ((uint8_t *)o->params)[2] : 0;
	return ft ? ft * 10u : 100u;
}

static void animturret_create(task_t *t, void *arg) { // AnimTurret_Create 0x458aa0
	obj_args_t *a = arg;
	const uint8_t *pool = a && a->params ? (const uint8_t *)(a->params + 1) : NULL;
	turret_init(turret_of(t), pool, a ? a->id : OBJ_NONE, a ? a->inst : -1);   // before the anim nodes
	animobj_create(t, arg);
}

static void animturret_update(task_t *t) { // 0x458a10
	objstate_t *o = objstate(t);
	if (o->flags & 8) animobj_render(t, t->wake - anim_period(t));  // AnimState_Apply(state, 0)
	turret_tick(turret_of(t), turret_pool(t));
	animobj_update(t);                                                 // AnimState_Step
}

static void animturret_destroy(task_t *t) { // 0x458ae0
	turret_destroy(t);
	animobj_destroy(t);
}

static intptr_t animturret_get_pos(ObjId id) { // 0x458b40
	static int32_t pos[3];
	task_t *t;
	turret_t *s = turret_from_id(id, &t);
	objstate_t *o = t ? objstate(t) : NULL;
	if (o && o->inst >= 0 && s && s->parts > 1 && s->gun) {
		if (o->flags & 0x10) { // animating: the object's matrix applied to the head's offset
			coord_t *head = s->parts > 2 ? s->gun->parent : s->gun;
			const FwMatrix *m = coord_world(objstate_coord(o));
			if (head) {
				wm_apply(m, head->local.t, pos);
				wv_add(m->t, pos, pos);
				return (intptr_t)pos;
			}
		}
		int16_t second = gInstances[o->inst].next_part;  // part 1's position
		if (second >= 0) return (intptr_t)coord_world(gInstances[second].coord)->t;
	}
	if (o) {
		coord_t *c = objstate_coord(o);
		if (c) return (intptr_t)coord_world(c)->t;
	}
	map_rec_t *r = Object_GetRecord(id);
	return r ? (intptr_t)&r->pos : 0;
}

static intptr_t animturret_get_hp(ObjId id) { uint8_t *p = pool_of(id); return p ? p[0] : 0; }
static intptr_t animturret_get_armour(ObjId id) { uint8_t *p = pool_of(id); return p ? p[1] : 0; }
static void animturret_set_armour(ObjId id, intptr_t v) { uint8_t *p = pool_of(id); if (p) p[1] = (uint8_t)v; }
static void animturret_set_size(ObjId id, intptr_t v) { uint8_t *p = pool_of(id); if (p) p[2] = (uint8_t)v; }

static intptr_t animturret_get_flags(ObjId id) { // 0x458dd0: 0xc000 still, 0x4000 while animating
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	uint8_t *p = pool_of(id);
	if (p && p[0] && t && t->type && !(p[3] & 0x80)) {
		return (intptr_t)(((~objstate(t)->flags & 0x10) | 8) << 11);
	}
	return 0;
}

static void animturret_set_hp(ObjId id, intptr_t v) { // 0x458c40: the wreck keeps the pose
	uint8_t *p = pool_of(id);
	if (!p) return;
	p[0] = (uint8_t)v;
	if (v != 0) return;
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	objstate_t *o = t && t->type ? objstate(t) : NULL;
	FwMatrix pose;
	bool have_pose = false;
	if (o) {
		coord_t *c = o->inst >= 0 && gInstances[o->inst].used ? gInstances[o->inst].coord : NULL;
		if (c) {
			pose = *coord_world(c);
			have_pose = true;
		}
		if (o->flags & 8) AnimStop(id, 0);
	} else if (((uint8_t *)(p - 4))[0] & 8) {
		AnimStop(id, 0);
	}
	weapons_destroy_core(id, p[2]);
	if (o) {
		o->inst = Object_GetInstance(id);
		if (have_pose && o->inst >= 0 && gInstances[o->inst].used) gInstances[o->inst].coord->local = pose;
	}
}

// ---------------------------------------------------------------- registration

void turret_register(void) {
	ObjectType_Register(OT_TURRET, turret_update, NULL, turret_create, turret_destroy, 1);
	objprops_common(OT_TURRET);
	objprops_destructible(OT_TURRET, 0);
	ObjectProperty_RegisterGet(OT_TURRET, PROP_POSITION, turret_get_pos);
	ObjectProperty_RegisterGet(OT_TURRET, PROP_TARGET, turret_get_target);
	ObjectProperty_RegisterSet(OT_TURRET, PROP_TARGET, turret_set_target);
	ObjectProperty_RegisterGet(OT_TURRET, PROP_FLAGS, turret_get_flags);
	ObjectProperty_RegisterSet(OT_TURRET, PROP_STATE, turret_set_enable);
	ObjectProperty_RegisterSet(OT_TURRET, PROP_POSITION, objprop_ignore);
	ObjectProperty_RegisterSet(OT_TURRET, PROP_MATRIX, objprop_ignore);

	ObjectType_Register(OT_ANIMTURRET, animturret_update, animobj_render, animturret_create, animturret_destroy, 2);
	ObjectType_SetFrequency(OT_ANIMTURRET, 100);
	objprops_common(OT_ANIMTURRET);
	ObjectProperty_RegisterGet(OT_ANIMTURRET, PROP_POSITION, animturret_get_pos);
	ObjectProperty_RegisterGet(OT_ANIMTURRET, PROP_HITPOINTS, animturret_get_hp);
	ObjectProperty_RegisterGet(OT_ANIMTURRET, PROP_ARMOUR, animturret_get_armour);
	ObjectProperty_RegisterGet(OT_ANIMTURRET, PROP_FLAGS, animturret_get_flags);
	ObjectProperty_RegisterSet(OT_ANIMTURRET, PROP_HITPOINTS, animturret_set_hp);
	ObjectProperty_RegisterSet(OT_ANIMTURRET, PROP_ARMOUR, animturret_set_armour);
	ObjectProperty_RegisterSet(OT_ANIMTURRET, PROP_EXPLOSION, animturret_set_size);
	ObjectProperty_RegisterSet(OT_ANIMTURRET, PROP_STATE, turret_set_enable);
	ObjectProperty_RegisterSet(OT_ANIMTURRET, PROP_TARGET, turret_set_target);
	ObjectProperty_RegisterSet(OT_ANIMTURRET, PROP_POSITION, objprop_ignore);
}

void turret_reset(void) { // TurretTypes_Reset 0x4541d0
	for (int i = 0; i < TURRET_TYPE_SLOTS; i++) gTurretTypes[i] = i < 20 ? &default_types[i] : NULL;
	memset(turrets, 0, sizeof(turrets));
}
