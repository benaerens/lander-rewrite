// Glue between the lander port (lander.c) and the runtime: the Lander object
// type (type 1) and every eng_* adapter declared in lander.h.
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "lander.h"
#include "lights.h"
#include "sound.h"
#include "input_map.h"
#include "hud.h"
#include "game.h"
#include "weapons.h"
#include "physobjs.h"

// ---------------------------------------------------------------- globals the lander uses
LanderPlayerState gPlayerState = {0, 0x180000, 0x180000, 255, 5000, 10, 22, 9, 6, 0, 0};
LanderPlayerState gMPPlayerState[16];
int32_t gThisPlayerId = -1;
uint32_t gShipColours[16] = {0x0000ff, 0x00ff00, 0xff0000, 0xffffff, 0x0d0d0d, 0x002626, 0xff00ff, 0xffff00, // 0x4935a0
	0x00ffff, 0x80b3ff, 0x00ff80, 0x145514, 0x400040, 0x0033c0, 0x550000, 0x0080bf};
int32_t gUseInertialFrame;
FwMatrix gInertialFrame;
int32_t gDamageSource = -1;
int32_t gFFDevicePresent, gMouseStickPresent, gProjectileSecondShot, gLoadingGameState;
int32_t gReflectionPass, gMirrorProbe, gLanderAboveLiquid, gAutoCamQuery, gOutsideFactor = 0x1000;
uint32_t gCtrlWords[16];

// ---------------------------------------------------------------- the Lander object type

#define MAX_LANDER_PROCS 16
static LanderProc procs[MAX_LANDER_PROCS];
static task_t *proc_task[MAX_LANDER_PROCS];

static LanderProc *proc_of(task_t *t) { return (LanderProc *)t->data[0]; }

static void lander_create(task_t *t, void *arg) {
	obj_args_t *a = arg;
	int slot = -1;
	for (int i = 0; i < MAX_LANDER_PROCS; i++) {
		if (!proc_task[i]) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		return;
	}
	LanderProc *p = &procs[slot];
	memset(p, 0, sizeof(*p));
	proc_task[slot] = t;
	t->data[0] = (intptr_t)p;
	p->wakeTime = (int32_t)t->wake;
	p->period = (int32_t)t->type->period;
	if (a && a->rec) {
		LanderCreateArgs la = {a->id, (const int32_t *)&a->rec->pos, (int16_t *)a->params};
		Lander_Create(p, &la);
	} else {
		Lander_Create(p, NULL);
	}
}

static void lander_update(task_t *t) {
	LanderProc *p = proc_of(t);
	if (p) {
		p->wakeTime = (int32_t)t->wake;
		Lander_Update(p);
		if (p->flags & LPF_HUD) {
			game_hud_lander_tick(p->lander);
		}
	}
}

static void lander_render(task_t *t, uint32_t time) {
	LanderProc *p = proc_of(t);
	if (p) {
		p->wakeTime = (int32_t)t->wake;
		Lander_Render(p, (int32_t)time);
	}
}

static void lander_destroy(task_t *t) {
	LanderProc *p = proc_of(t);
	if (!p) {
		return;
	}
	Lander_Destroy(p);
	proc_task[p - procs] = NULL;
	t->data[0] = 0;
}

#define LANDER_PROP(n) \
	static intptr_t get##n(ObjId id) { return Lander_GetProperty(id, n); } \
	static void set##n(ObjId id, intptr_t v) { Lander_SetProperty(id, n, v); }
LANDER_PROP(0) LANDER_PROP(1) LANDER_PROP(2) LANDER_PROP(3) LANDER_PROP(4)
LANDER_PROP(6) LANDER_PROP(7) LANDER_PROP(8)

void lander_register_type(void) {
	ObjectType_Register(OT_LANDER, lander_update, lander_render, lander_create, lander_destroy, 2);
	prop_get_fn g[] = {get0, get1, get2, get3, get4, NULL, get6, get7, get8};
	prop_set_fn s[] = {set0, NULL, set2, set3, NULL, NULL, NULL, NULL, set8};
	for (int i = 0; i < 9; i++) {
		if (g[i]) ObjectProperty_RegisterGet(OT_LANDER, i, g[i]);
		if (s[i]) ObjectProperty_RegisterSet(OT_LANDER, i, s[i]);
	}
	(void)set1; (void)set4; (void)set6; (void)set7;
}

LanderProc *eng_lander_proc(ObjId id) {
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	if (!t || !t->type) {
		return NULL;
	}
	for (int i = 0; i < MAX_LANDER_PROCS; i++) {
		if (proc_task[i] == t) {
			return &procs[i];
		}
	}
	return NULL;
}

// ---------------------------------------------------------------- control words, players

uint32_t eng_ctrl_read_word(int32_t player) { // Ctrl_ReadWord 0x4258a0
	return im_read_word(&gCtrlWords[player & 15]);
}

int32_t eng_player_add(void) { // Player_Add 0x4440b0
	int p = gNbPlayers < 16 ? gNbPlayers++ : 15;
	gPlayerIds[p] = p;
	return p;
}

void eng_player_set_object(int32_t player, ObjId id) { gPlayerObjects[player & 15] = id; }
void eng_player_remove(int32_t player) { gPlayerObjects[player & 15] = OBJ_NONE; }

ObjId eng_task_dynamic_id(LanderProc *p) {
	return (ObjId)(Task_ToIndex(proc_task[p - procs]) | 0x02010000);
}

void eng_task_type_set_reflective(LanderProc *p) { (void)p; gObjectTypes[OT_LANDER].task.flags |= 8; }
void eng_objects_create(int32_t type, void *args) { Objects_Create(type, args); }

void eng_map_set_mob_param_hi(ObjId id, int16_t v) {
	int32_t *params = NULL;
	Objects_GetData(id, &params, NULL);
	if (params) {
		params[0] = (int32_t)(((uint32_t)(uint16_t)v << 16) | (params[0] & 0xffff));
	}
}

void eng_rot_pack_to_matrix(const int32_t *record, FwMatrix *out) { // RotPackToMatrix 0x456bd0
	fw_from_packed((uint32_t)record[3], out);
	out->t[0] = record[0];
	out->t[1] = record[1];
	out->t[2] = record[2];
}

void eng_mission_game_over(int32_t dead) { game_over(dead); }

// ---------------------------------------------------------------- transform nodes and models

void *eng_coord_alloc(void) { return coord_alloc(NULL); }
void eng_coord_free(void *c) { coord_free(c); }
const FwMatrix *eng_coord_world(void *c) { return coord_world(c); }
void eng_coord_identity(void *c) { fw_identity(&((coord_t *)c)->local); }
phys_matrix_t *eng_coord_matrix(void *c) { return (phys_matrix_t *)&((coord_t *)c)->local; }
FwMatrix *eng_coord_local(void *c) { return &((coord_t *)c)->local; }

int16_t eng_model_inst_create(int32_t index) { // 0x456410 (gGlobalModels)
	return inst_create(global_model(index), G.global_tex, NULL);
}

void eng_model_inst_release(int32_t global_model, int16_t inst) { (void)global_model; inst_free(inst); }
void *eng_model_inst_coord(int16_t inst) { return inst >= 0 ? gInstances[inst].coord : NULL; }
const model_t *eng_model_inst_model(int16_t inst) { return inst >= 0 ? gInstances[inst].model : NULL; }

static void lander_draw_cb(model_inst_t *mi) {
	Lander_DrawCallback((LanderProc *)mi->user);
}

// node attached to the body node, rotated by Ry(0x200), draw callback 0x425980
void eng_model_inst_setup_lander(int16_t inst, void *coord, int32_t lander_index, const uint32_t *team) {
	if (inst < 0) {
		return;
	}
	model_inst_t *mi = &gInstances[inst];
	mi->coord->parent = coord;
	fw_identity(&mi->coord->local);
	fw_rotate_y(&mi->coord->local, 0x200);
	mi->tint = team ? *team : 0;
	mi->zone = -1;
	for (int i = 0; i < MAX_LANDER_PROCS; i++) {
		if (proc_task[i] && procs[i].lander == &gLanders[lander_index]) {
			mi->user = &procs[i];
		}
	}
	if (!mi->user) { // during Lander_Create: the newest process
		for (int i = MAX_LANDER_PROCS - 1; i >= 0; i--) {
			if (proc_task[i]) {
				mi->user = &procs[i];
				break;
			}
		}
	}
	mi->draw = lander_draw_cb;
}

void eng_model_queue(int16_t inst) { inst_queue(inst); }

void eng_model_draw(LanderProc *p) {
	model_inst_t *mi = &gInstances[p->modelInst];
	inst_draw_fn d = mi->draw;
	mi->draw = NULL; // plain model draw
	inst_draw_now(p->modelInst);
	mi->draw = d;
}

int32_t eng_model_inst_tag(int16_t inst) { return inst >= 0 ? gInstances[inst].tag : -1; }
void eng_model_inst_set_tag(int16_t inst, int32_t tag) { if (inst >= 0) gInstances[inst].tag = tag; }

void eng_model_inst_attach(int16_t inst, void *parent, const int32_t t[3], int32_t rot_z) { // 0x4275d0
	if (inst < 0) {
		return;
	}
	coord_t *c = gInstances[inst].coord;
	c->parent = parent;
	fw_identity(&c->local);
	if (rot_z) {
		FwMatrix r;
		fw_identity(&r);
		int32_t s = (int32_t)(sinf(rot_z * 2 * PI / 4096) * 4096), co = (int32_t)(cosf(rot_z * 2 * PI / 4096) * 4096);
		r.m[0][0] = (int16_t)co; r.m[0][1] = (int16_t)-s;
		r.m[1][0] = (int16_t)s; r.m[1][1] = (int16_t)co;
		c->local = r;
	}
	c->local.t[0] = t[0];
	c->local.t[1] = t[1];
	c->local.t[2] = t[2];
}

int eng_model_inst_ready(int16_t inst) { return inst >= 0 && gInstances[inst].model != NULL; }
void eng_weapons_tint_mount(LanderWeapons *w, int32_t mount, const uint32_t *rgb) { (void)w; (void)mount; (void)rgb; }

// ---------------------------------------------------------------- lights

void *eng_light_create(int32_t kind, void *coord) { return dyn_light_create(kind, coord); }
void eng_light_destroy(void *l) { dyn_light_destroy(l); }

void eng_light_colour(void *l, int32_t r, int32_t g, int32_t b) {
	if (l) ((dyn_light_t *)l)->color = vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

void eng_light_range(void *l, int32_t inner, int32_t outer) { // Light_SetRange(inner, falloff)
	if (l) {
		((dyn_light_t *)l)->inner = inner;
		((dyn_light_t *)l)->falloff = outer;
	}
}

void eng_light_cone(void *l, int32_t inner, int32_t outer) {
	if (l) {
		((dyn_light_t *)l)->cone_inner = inner;
		((dyn_light_t *)l)->cone_outer = outer;
	}
}

void eng_light_position(void *l, int32_t x, int32_t y, int32_t z) {
	if (l) {
		dyn_light_t *d = l;
		d->pos[0] = x; d->pos[1] = y; d->pos[2] = z;
	}
}

void eng_light_direction(void *l, const int32_t dir[3]) {
	if (l) memcpy(((dyn_light_t *)l)->dir, dir, sizeof(int32_t) * 3);
}

void eng_light_set_flag10(void *l) { if (l) ((dyn_light_t *)l)->flags |= 0x10; }
// ---------------------------------------------------------------- sound

int32_t eng_snd_once(ObjId owner, int32_t sid) { return sound_play(owner, sid, 1, -1); }
int32_t eng_snd_once_vol(ObjId owner, int32_t sid, int32_t vol) { return sound_play(owner, sid, 1, vol); }
int32_t eng_snd_once_excl(ObjId owner, int32_t sid) { sound_stop_object(owner); return sound_play(owner, sid, 1, -1); }
int32_t eng_snd_loop(ObjId owner, int32_t sid, int32_t loops) { return sound_play(owner, sid, loops, -1); }
int32_t eng_snd_loop_vol(ObjId owner, int32_t sid, int32_t loops, int32_t vol) { return sound_play(owner, sid, loops, vol); }

int32_t eng_snd_chain(ObjId owner, int32_t n, ...) {
	int32_t sids[8];
	va_list ap;
	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		int32_t s = va_arg(ap, int32_t);
		if (i < 8) sids[i] = s;
	}
	va_end(ap);
	return sound_chain(owner, -1, 0, min(n, 8), sids);
}

int32_t eng_snd_loop_chain_vol(ObjId owner, int32_t vol, int32_t n, const int32_t *sids) {
	return sound_chain(owner, vol, 1, min(n, 8), sids);
}

void eng_snd_stop(int32_t h) { sound_stop(h); }
void eng_snd_stop_object(ObjId owner) { sound_stop_object(owner); }
void eng_snd_set_volume(int32_t h, int32_t vol) { sound_set_volume(h, vol); }
float eng_snd_get_freq(int32_t h) { return sound_get_freq(h); }
void eng_snd_set_freq(int32_t h, float f) { sound_set_freq(h, f); }
int eng_snd_enabled(void) { return 1; }

// ---------------------------------------------------------------- HUD, camera, input

void eng_hud_create(LanderProc *p) { // Lander_CreateHud 0x420770
	p->flags |= LPF_HUD;
	gThisPlayer = (int32_t)p->id;
	game_hud_create(p->lander->ps.landerType);
}

void eng_hud_destroy(void) { game_hud_destroy(); }
int16_t eng_hud_marker_add(int32_t what, uint32_t rgb, int32_t kind) { return (int16_t)game_hud_marker_add(what, rgb, kind); }
void eng_hud_marker_remove(int16_t marker) { game_hud_marker_remove(marker); }
void eng_hud_attitude_items(Lander *L, LanderProc *p) { (void)L; (void)p; }
void eng_input_reset_mouse_stick(void) { im_mouse_reset(); }
void eng_input_ff_fire(void) {}
void eng_ctrl_set_fire_autorepeat(int on) { im_set_fire_auto(on != 0); }

// ---------------------------------------------------------------- world, objects

int eng_surface_class(int32_t surface) { // Surface_GetClass 0x451320
	const mission_t *m = G.level ? G.level->mission : NULL;
	if (!m || surface < 0 || surface >= m->num_surface) {
		return 0;
	}
	return m->surface_class[surface];
}

int32_t eng_rand(void) { return rand(); }

void eng_world_set_query(int32_t mask, ObjId exclude) { // gWorldQueryMask 0x4989f4, 0x498434
	G.world.query_mask = mask == -1 ? 0x1f : mask;
	phys_g.exclude_owner = (int32_t)exclude;
}

int32_t eng_phys_query_slot(void) { return gPhysQuerySlot; }   // DAT_0058b3c0 (set by the collision queries)
int32_t eng_phys_query_time(void) { return gPhysQueryTime; }   // DAT_0058b394

void eng_fire_projectile(const LanderShotParams *sp) { weapons_fire_projectile(sp); }
