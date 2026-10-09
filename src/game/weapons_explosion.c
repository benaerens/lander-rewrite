// Explosion (object type 9): Explosion_Create 0x44e610, update 0x44e970, render 0x44ea90 /
// draw 0x44e570, destroy 0x44e930, properties 0x44f000 / 0x44efe0, blast damage 0x44ee30 /
// 0x44ebd0, blast push 0x44ece0, Explosion_CreateDynamic 0x44f040. re/specs/weapons.md §3.
//
// The fire ball is a system of the framework's FwParticles (create 0x430930, step 0x430b00, draw
// Particles_Draw 0x4321a0) - the effects module's fx_psys_* - with the explosion's own defs
// 0x498b98..0x498d78 (registered by FUN_0044ef70 with the global textures).
#include "weapons_internal.h"
#include "effects.h"

static void expl_finished(int sys);

// 0x498b98 (kind 0, PTR 0x498d78) and 0x498cd8 (other kinds, PTR 0x498d7c): 0x50-byte FwParticles
// records {tex, texfile, count, gravity, size, colour, life0, life1, dir, ext, speed, flags, done}
static fx_psys_def_t defs_big[4] = {
	{0x46, 0, 0x08, 1, 0x0a0, 0x808080, 0x0a, 0x0f, {0, 0, 0, 0}, {0x008, 0x008, 0x008, 0}, 0x08, 0, 0x12b, expl_finished, 0, 0},
	{0x46, 0, 0x10, 1, 0x15e, 0x808080, 0x0f, 0x19, {0, 0, 0, 0}, {0x040, 0x040, 0x040, 0}, 0x10, 0, 0x12b, expl_finished, 0, 0},
	{0x46, 0, 0x14, 1, 0x2bc, 0xffffff, 0x0a, 0x1e, {0, 0, 0, 0}, {0x080, 0x080, 0x080, 0}, 0x28, 0, 0x12b, expl_finished, 0, 0},
	{0x46, 0, 0x1e, 1, 0x44c, 0x808080, 0x28, 0x32, {0, 0, 0, 0}, {0x180, 0x180, 0x180, 0}, 0x28, 0, 0x12b, expl_finished, 0, 0},
};
static fx_psys_def_t defs_small[2] = {
	{0x3d, 0, 0x08, 1, 0x0a0, 0x808080, 0x0a, 0x0f, {0, 0, 0, 0}, {0x008, 0x008, 0x008, 0}, 0x08, 0, 0x12b, expl_finished, 0, 0},
	{0x3d, 0, 0x10, 1, 0x15e, 0x808080, 0x0f, 0x19, {0, 0, 0, 0}, {0x040, 0x040, 0x040, 0}, 0x10, 0, 0x12b, expl_finished, 0, 0},
};

// ---------------------------------------------------------------- the explosion object

typedef struct {
	ObjId id;            // +0x0c
	ObjId source;        // +0x10 damage source (shooter)
	int16_t sys;         // +0x14 particle system
	int16_t inst;        // +0x16 render item (its node is the explosion's node, +0x18)
	uint16_t tick;       // +0x1c
	uint16_t size;       // +0x1e
	dyn_light_t *light;  // +0x20
	uint32_t glow_rgb;   // +0x24
	int32_t glow;        // +0x28
} expl_t;

_Static_assert(sizeof(expl_t) <= sizeof(((task_t *)0)->data), "explosion fits in task data");

typedef struct {         // the dynamic create parameters {u8 kind, u8 size, matrix*, source}
	int kind, size;
	FwMatrix m;          // position; its rotation orients the debris
	ObjId source;
} expl_args_t;

static expl_t *expl_of(task_t *t) { return (expl_t *)t->data; }
static coord_t *expl_node(expl_t *e) { return e->inst >= 0 ? gInstances[e->inst].coord : NULL; }

static void draw_explosion(model_inst_t *mi) { // 0x44e570: glow sprite, then the particles
	task_t *t = mi->user;
	if (!t || !t->type) return;
	expl_t *e = expl_of(t);
	const FwMatrix *w = coord_world(mi->coord);
	fx_draw_begin();
	if (e->glow && fx_view_depth(w->t) - 0x100 > 0x40) {
		fx_glow(w->t, e->glow_rgb, (float)(e->size * e->glow * 0x60), false);  // 0x44a4a0
	}
	fx_draw_end();
	fx_psys_draw(e->sys, w, -2);   // Particles_Draw (the item: no zone clip)
}

static void expl_create(task_t *t, void *arg) { // Explosion_Create 0x44e610
	obj_args_t *a = arg;
	expl_t *e = expl_of(t);
	memset(e, 0, sizeof(*e));
	e->inst = inst_create(NULL, NULL, NULL);     // Coord_Alloc + RenderItem_Alloc
	e->sys = -1;
	coord_t *node = expl_node(e);
	int kind = 0;
	FwMatrix where, debris;   // the node's matrix; the frame of the debris (param_2 in the exe)
	fw_identity(&where);
	if (a && a->rec) { // a mob woken by a script: pool word {u8 kind, u8 size}
		const uint8_t *w = a->params ? (const uint8_t *)a->params : NULL;
		e->id = a->id;
		kind = w ? w[0] : 0;
		e->size = w ? w[1] : 0;
		e->source = OBJ_NONE;  // [A] the exe reads the next pool word here
		fw_from_packed(a->rec->rot, &where);
		where.t[0] = a->rec->pos.x;
		where.t[1] = a->rec->pos.y;
		where.t[2] = a->rec->pos.z;
		debris = where;
	} else {
		const expl_args_t *x = a ? a->extra : NULL;
		e->id = (ObjId)Task_ToIndex(t) | 0x02090000u;
		kind = x ? x->kind : 0;
		e->size = x ? (uint16_t)(uint8_t)x->size : 0;
		e->source = x ? x->source : OBJ_NONE;
		if (x) wv_copy(x->m.t, where.t);
		debris = x ? x->m : where;
	}
	if (node) {
		node->local = where;
		gInstances[e->inst].draw = draw_explosion;
		gInstances[e->inst].user = t;
		gInstances[e->inst].zone = -1;
	}
	int n = kind == 0 ? 4 : 2;
	fx_psys_def_t *set = kind == 0 ? defs_big : defs_small;
	uint32_t i = min(e->size, 99u) * (uint32_t)n / 100;
	e->sys = (int16_t)fx_psys_create(&set[i], t);   // FUN_00430930, the system's +0xc = the process
	int EQ = EXPLOSION_QUALITY;
	if (((0x1000 - EQ) * 0x40 >> 12) < e->size) {
		int c = (int)((i + 1) * 0x100 / (uint32_t)n) + 0x80;
		dyn_light_t *l = dyn_light_create(1, node);
		if (l) {
			l->flags |= 0x10;
			l->inner = 0;
			l->falloff = (int32_t)((i + 1) * 0x2000 / (uint32_t)n) + 0x200 + ((EQ << 9) >> 12);
			l->color = vec3(c / 255.0f, c / 255.0f, c / 255.0f);
		}
		e->light = l;
		e->glow = 0x19;
		e->glow_rgb = 0x40d0ff;
	}
	if (((0x1000 - EQ) * 0x40 >> 12) < e->size && Sched_FreeCount() > 0x40) { // burning debris
		uint32_t cnt = (uint32_t)(rand() % (EQ >> 8)) + (uint32_t)(((e->size - 0x1e) * EQ) >> 14);
		if (cnt > 0x20) cnt = 0x20;
		if (cnt > 2) {
			proj_params_t d;
			memset(&d, 0, sizeof(d));
			d.kind = 3;
			d.world = &debris;
			d.arg2 = 0x10;
			d.target = d.owner = OBJ_NONE;
			for (; cnt; cnt--) {
				d.f08 = rand() % 3 + 2;
				projectile_create(&d);
			}
		}
	}
	if (EQ > 0xfff && e->size > 0xf0) { // ShortFX (type 27) at the centre
		obj_args_t fx = {OBJ_NONE, -1, NULL, NULL, where.t, NULL};
		if (gObjectTypes[OT_SHORTFX].registered) Task_CreateNow(&gObjectTypes[OT_SHORTFX].task, &fx);
	}
}

static void expl_finished(int sys) { // 0x44e530 (the defs' callback: the system's last particle died)
	task_t *t = fx_psys_user(sys);
	if (!t || !t->type) return;
	ObjId id = expl_of(t)->id;
	if ((id & 0x3000000) == 0x2000000) Task_Kill(t);
	else MobObj_Sleep((int32_t)(id & 0xffff));
}

// 0x44ebd0: damage of one object by the blast at `pos`
static void blast_object(const int32_t *pos, int32_t hp, ObjId id, int32_t size, int32_t r2) {
	const int32_t *p = (const int32_t *)Object_GetProperty(id, PROP_POSITION);
	if (!p) return;
	int32_t d[3];
	wv_sub(pos, p, d);
	for (int k = 0; k < 3; k++) {
		d[k] = w_abs(d[k]);
		if (d[k] > 0x1000) return;
	}
	int32_t dd = (d[2] * d[2] + d[1] * d[1] + d[0] * d[0]) >> 6;
	int32_t R = r2 >> 1;
	if (R <= dd) return;
	int32_t k = ((R - dd) * size) / R;
	if (!k) return;
	int32_t ar = (int32_t)Object_GetProperty(id, PROP_ARMOUR);
	if (ar > 0xfe) return;
	if (ar) {
		if (ar > 0x80) ar = ar * 4 - 0x180;
		hp -= (k << 5) / ar;
		if (hp < 0) hp = 0;
	} else {
		hp = 0;
	}
	Object_SetProperty(id, PROP_HITPOINTS, hp);
}

static int32_t blast_r2;  // DAT_005534e8 (set by the damage pass, read by the push)

static void blast_damage(const int32_t *pos, int32_t size) { // 0x44ee30
	int32_t r = size * 16;
	if (r > 0x10000) r = 0x10000;
	blast_r2 = (r * r) >> 6;
	const mission_t *m = G.level->mission;
	// map objects with a process near the blast (the exe asks the map octree for the box pos +- r)
	for (int i = 0; i < m->num_rec[REC_OBJECT]; i++) {
		const map_rec_t *rec = &m->rec[REC_OBJECT][i];
		ObjId id = OBJ_ID(rec->type, i);
		task_t *t = NULL;
		Objects_GetData(id, NULL, &t);
		if (!t || !t->type) continue;
		if (w_abs(rec->pos.x - pos[0]) > r || w_abs(rec->pos.y - pos[1]) > r || w_abs(rec->pos.z - pos[2]) > r) continue;
		int32_t hp = (int32_t)Object_GetProperty(id, PROP_HITPOINTS);
		if (hp) blast_object(pos, hp, id, size, blast_r2);
	}
	for (int i = 0; i < m->num_rec[REC_MOB]; i++) {
		if (!eng_mob_is_awake(i)) continue;
		ObjId id = MOB_ID(m->rec[REC_MOB][i].type, i);
		task_t *t = NULL;
		Objects_GetData(id, NULL, &t);
		if (!t || !t->type) continue;
		int32_t hp = (int32_t)Object_GetProperty(id, PROP_HITPOINTS);
		if (hp) blast_object(pos, hp, id, size, blast_r2);
	}
}

static void blast_push(const int32_t *pos, int32_t size) { // 0x44ece0: push the awake mobs' bodies
	const mission_t *m = G.level->mission;
	for (int i = 0; i < m->num_rec[REC_MOB]; i++) {
		if (!eng_mob_is_awake(i)) continue;
		phys_body_t *b = (phys_body_t *)Object_GetProperty(MOB_ID(m->rec[REC_MOB][i].type, i), PROP_BODY);
		if (!b || !b->cur) continue;
		int32_t c[3] = {b->cur->pos[0] >> 3, b->cur->pos[1] >> 3, b->cur->pos[2] >> 3}, d[3];
		wv_sub(c, pos, d);
		if (w_abs(d[0]) > 0x1000 || w_abs(d[1]) > 0x1000 || w_abs(d[2]) > 0x1000) continue;
		int32_t dd = (d[1] * d[1] + d[0] * d[0] + d[2] * d[2]) >> 6;
		if (dd >= blast_r2 || blast_r2 <= 0x80 || (blast_r2 >> 8) == 0) continue; // (the exe divides by 0 there)
		int32_t s = ((blast_r2 - dd) * size) / (blast_r2 >> 8);
		phys_normalise(d);
		wv_scale(d, s, d);
		phys_body_add_force(b, d, 1, NULL);
	}
}

static void expl_update(task_t *t) { // 0x44e970
	expl_t *e = expl_of(t);
	coord_t *node = expl_node(e);
	if ((e->tick & 7) == 0 && node) {
		const FwMatrix *w = coord_world(node);
		int32_t pos[3];
		wv_copy(w->t, pos);
		gDamageSource = (int32_t)e->source;
		blast_damage(pos, e->size);
		gDamageSource = -1;
		if (e->tick == 0) blast_push(pos, e->size);
		if (!t->type) return;  // a blast can end the mission objects' lives, not this task's
	}
	e->tick++;
	if (e->light) {
		int32_t k = 0x19 - e->tick;
		uint32_t c = 0, b = 0;
		if (k == 0) {
			dyn_light_destroy(e->light);
			e->light = NULL;
		} else if (k > 0) {
			e->light->inner = k * 0x40;
			e->light->falloff = k * 0x80;
			c = (uint32_t)k * 0x20;
			b = c >> 1;
			if (c > 0xff) c = 0xff;
			if (b > 0xff) b = 0xff;
			e->light->color = vec3(c / 255.0f, c / 255.0f, b / 255.0f);
		}
		e->glow = k;
		e->glow_rgb = (b << 16) | (c << 8) | c;
	}
	fx_psys_update(e->sys);   // FUN_00430b00 (may end this process through expl_finished)
}

static void expl_render(task_t *t, uint32_t time) { // 0x44ea90: queue the render item
	(void)time;
	expl_t *e = expl_of(t);
	if (e->inst >= 0) inst_queue(e->inst);
}

static void expl_destroy(task_t *t) { // 0x44e930
	expl_t *e = expl_of(t);
	dyn_light_destroy(e->light);
	e->light = NULL;
	if (e->inst >= 0) inst_free(e->inst);
	e->inst = -1;
	fx_psys_free(e->sys);
	e->sys = -1;
}

static intptr_t expl_get_pos(ObjId id) { // 0x44f000
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	if (t && t->type == &gObjectTypes[OT_EXPLOSION].task) {
		coord_t *node = expl_node(expl_of(t));
		if (node) return (intptr_t)node->local.t;
	}
	map_rec_t *r = Object_GetRecord(id);
	return r ? (intptr_t)&r->pos : 0;
}

static void expl_set_size(ObjId id, intptr_t v) { // 0x44efe0: pool byte 1
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (params) ((uint8_t *)params)[1] = (uint8_t)v;
}

void explosion_create(int kind, int size, const FwMatrix *where, ObjId source) {
	if (!gObjectTypes[OT_EXPLOSION].registered || !where) return;
	expl_args_t x = {kind, size, *where, source};
	obj_args_t a = {OBJ_NONE, -1, NULL, NULL, &x, NULL};
	Task_CreateNow(&gObjectTypes[OT_EXPLOSION].task, &a);
}

void Explosion_CreateDynamic(int32_t kind, int32_t size, const int32_t pos[3]) { // 0x44f040
	FwMatrix m;
	fw_identity(&m);
	wv_copy(pos, m.t);
	explosion_create(kind & 0xff, size & 0xff, &m, OBJ_NONE);  // [A] the exe passes no source
}

void explosion_register(void) {
	ObjectType_Register(OT_EXPLOSION, expl_update, expl_render, expl_create, expl_destroy, 0);
	ObjectProperty_RegisterGet(OT_EXPLOSION, PROP_POSITION, expl_get_pos);
	ObjectProperty_RegisterSet(OT_EXPLOSION, PROP_EXPLOSION, expl_set_size);
	for (int i = 0; i < (int)len(defs_big); i++) fx_psys_register(&defs_big[i]);   // FUN_0044ef70
	for (int i = 0; i < (int)len(defs_small); i++) fx_psys_register(&defs_small[i]);
}

void explosion_reset(void) {} // the systems are the effects module's (reset by effects_reset)
