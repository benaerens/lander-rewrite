// Headless test hooks for the physobjs types, driven by $LANDER_TEST_PHYSOBJS (nothing happens
// without it). Comma-separated actions, each run once at its time (ms of game time):
//   lift@T          Training 1: play the stage-1 lift (0x3008f, animation 1) as the script does
//   pod:M@T         wake pod mob M (if asleep) and put the player's lander 400 above its hook point
//   frame:M@T       wake InertialFrame mob M with the player's lander as its target
//   fbox:O@T        put the player's lander in the middle of force box object O (hex index)
//   anim:O:A:F@T    AnimPlay(object O (hex id), A, F)
//   tp:X:Y:Z@T      put the player's lander at X, Y, Z
//   wake:M@T        MobObj_Wake(M)
//   prop:O:P:V@T    Object_SetProperty(O (hex id), P, V)
//   above:M@T       put mob M (a physical pod, say) 600 above the player's lander
//   trace:M         print the lander and pod/frame mob M every 200 ms
//   hold:X:Y:Z@T    from T on, keep the player's lander at X, Y, Z (velocity 0) every 40 ms tick
//   at:O@T          from T on, keep it at object O's position (hex id; follows moving objects)
//   between:A:B@T   from T on, keep its x/z and put it half-way between objects A and B (hex ids) in y
//   release@T       stop holding
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physobjs.h"

typedef struct {
	char kind[8];
	uint32_t a, b, c;
} test_action_t;

#define MAX_ACTIONS 128
static test_action_t actions[MAX_ACTIONS];
static int trace_mob = -1;
static ObjId trace_obj = OBJ_NONE;
static struct { int mode; int32_t pos[3]; ObjId a, b; } hold;   // 0 off, 1 point, 2 object, 3 between

static ObjId player_lander(void) { return gNbPlayers > 0 ? gPlayerObjects[0] : OBJ_NONE; }

static ObjId mob_id(int index) {
	const mission_t *m = G.level->mission;
	return index >= 0 && index < m->num_rec[REC_MOB] ? MOB_ID(m->rec[REC_MOB][index].type, index) : OBJ_NONE;
}

static void test_fire(intptr_t n) {
	test_action_t *t = &actions[n];
	ObjId lander = player_lander();
	if (!strcmp(t->kind, "lift")) {
		AnimPlay(OBJ_ID(OT_ANIMOBJECT, 0x8f), 1, 0);
		printf("test: t=%d lift 0x3008f anim 1\n", gGameUpdateTime);
	} else if (!strcmp(t->kind, "pod")) {
		ObjId pod = mob_id((int)t->a);
		if (!Object_IsAwake(pod)) MobObj_Wake((int32_t)t->a);
		const int32_t *p = (const int32_t *)Object_GetProperty(pod, PROP_POSITION);
		if (p && lander != OBJ_NONE) {
			int32_t pos[3] = {p[0], p[1] + 0x80 + 400, p[2]};
			Object_SetProperty(lander, PROP_POSITION, (intptr_t)pos);
			printf("test: t=%d pod %08x at %d %d %d, lander to %d %d %d\n", gGameUpdateTime, pod, p[0], p[1], p[2],
				pos[0], pos[1], pos[2]);
		}
	} else if (!strcmp(t->kind, "frame")) {
		ObjId f = mob_id((int)t->a);
		if (!Object_IsAwake(f)) MobObj_Wake((int32_t)t->a);
		Object_SetProperty(f, PROP_TARGET, (intptr_t)lander);
		printf("test: t=%d frame %08x target %08x\n", gGameUpdateTime, f, lander);
	} else if (!strcmp(t->kind, "fbox")) {
		map_rec_t *r = Object_GetRecord(OBJ_ID(OT_FORCEBOX, t->a));
		if (r && lander != OBJ_NONE) {
			int32_t pos[3] = {r->pos.x, r->pos.y, r->pos.z};
			Object_SetProperty(lander, PROP_POSITION, (intptr_t)pos);
			printf("test: t=%d lander into force box %x at %d %d %d\n", gGameUpdateTime, t->a, pos[0], pos[1], pos[2]);
		}
	} else if (!strcmp(t->kind, "prop")) {
		Object_SetProperty((ObjId)t->a, (int32_t)t->b, (intptr_t)(int32_t)t->c);
		printf("test: t=%d Object_SetProperty(%08x, %u, %d)\n", gGameUpdateTime, t->a, t->b, (int32_t)t->c);
	} else if (!strcmp(t->kind, "above")) {   // object mob A put 600 above the lander (PROP_POSITION set)
		ObjId id = mob_id((int)t->a);
		const int32_t *lp = lander != OBJ_NONE ? (const int32_t *)Object_GetProperty(lander, PROP_POSITION) : NULL;
		if (lp) {
			int32_t pos[3] = {lp[0], lp[1] + 600, lp[2]};
			Object_SetProperty(id, PROP_POSITION, (intptr_t)pos);
			printf("test: t=%d %08x to %d %d %d\n", gGameUpdateTime, id, pos[0], pos[1], pos[2]);
		}
	} else if (!strcmp(t->kind, "wake")) {
		MobObj_Wake((int32_t)t->a);
		printf("test: t=%d wake mob %u (%08x)\n", gGameUpdateTime, t->a, mob_id((int)t->a));
	} else if (!strcmp(t->kind, "tp")) {
		int32_t pos[3] = {(int32_t)t->a, (int32_t)t->b, (int32_t)t->c};
		if (lander != OBJ_NONE) Object_SetProperty(lander, PROP_POSITION, (intptr_t)pos);
		printf("test: t=%d lander to %d %d %d\n", gGameUpdateTime, pos[0], pos[1], pos[2]);
	} else if (!strcmp(t->kind, "hold")) {
		hold.mode = 1;
		hold.pos[0] = (int32_t)t->a; hold.pos[1] = (int32_t)t->b; hold.pos[2] = (int32_t)t->c;
		printf("test: t=%d hold lander at %d %d %d\n", gGameUpdateTime, hold.pos[0], hold.pos[1], hold.pos[2]);
	} else if (!strcmp(t->kind, "at")) {
		hold.mode = 2;
		hold.a = (ObjId)t->a;
		printf("test: t=%d hold lander at object %08x\n", gGameUpdateTime, hold.a);
	} else if (!strcmp(t->kind, "between")) {
		hold.mode = 3;
		hold.a = (ObjId)t->a;
		hold.b = (ObjId)t->b;
		printf("test: t=%d hold lander between %08x and %08x\n", gGameUpdateTime, hold.a, hold.b);
	} else if (!strcmp(t->kind, "release")) {
		hold.mode = 0;
		printf("test: t=%d release lander\n", gGameUpdateTime);
	} else if (!strcmp(t->kind, "anim")) {
		AnimPlay((ObjId)t->a, (int32_t)t->b, (int32_t)t->c);
		printf("test: t=%d AnimPlay(%08x, %u, %x)\n", gGameUpdateTime, t->a, t->b, t->c);
	}
}

static void trace_update(task_t *t) {
	(void)t;
	ObjId lander = player_lander();
	const int32_t *lp = lander != OBJ_NONE ? (const int32_t *)Object_GetProperty(lander, PROP_POSITION) : NULL;
	Lander *L = lander != OBJ_NONE ? Lander_FromHandle(lander) : NULL;
	printf("trace t=%d lander %d %d %d", gGameUpdateTime, lp ? lp[0] : 0, lp ? lp[1] : 0, lp ? lp[2] : 0);
	if (L && L->body) {
		printf(" vel %d %d %d hook %08x fuel %d touched %08x imp %d", L->body->vel[0], L->body->vel[1], L->body->vel[2],
			L->hookTarget, L->ps.fuel, L->body->touched, L->body->impulse_sum);
	}
	if (trace_obj != OBJ_NONE) {
		int16_t i = Object_GetInstance(trace_obj);
		if (i >= 0) {
			const FwMatrix *w = coord_world(gInstances[i].coord);
			printf(" | obj %08x node %d %d %d", trace_obj, w->t[0], w->t[1], w->t[2]);
		}
	}
	if (trace_mob >= 0) {
		ObjId id = mob_id(trace_mob);
		const int32_t *p = Object_IsAwake(id) ? (const int32_t *)Object_GetProperty(id, PROP_POSITION) : NULL;
		phys_body_t *b = Object_IsAwake(id) ? (phys_body_t *)Object_GetProperty(id, PROP_BODY) : NULL;
		printf(" | %08x %s", id, Object_IsAwake(id) ? "awake" : "asleep");
		if (p) printf(" pos %d %d %d", p[0], p[1], p[2]);
		if (OBJ_TYPE(id) == OT_POD && Object_IsAwake(id)) printf(" flags %x", (int)Object_GetProperty(id, PROP_FLAGS));
		if (b) printf(" vel %d %d %d mass %d", b->vel[0], b->vel[1], b->vel[2], b->mass);
		if (gUseInertialFrame) {
			const FwMatrix *f = &gInertialFrame;
			printf(" frame x %d %d %d y %d %d %d z %d %d %d grav %d %d %d", f->m[0][0], f->m[1][0], f->m[2][0], f->m[0][1],
				f->m[1][1], f->m[2][1], f->m[0][2], f->m[1][2], f->m[2][2], phys_g.gravity[0], phys_g.gravity[1],
				phys_g.gravity[2]);
		}
	}
	printf("\n");
}

static const task_type_t trace_type = {0, trace_update, NULL, NULL, NULL, NULL, NULL, 200};

static void hold_update(task_t *t) {
	(void)t;
	ObjId lander = player_lander();
	if (!hold.mode || lander == OBJ_NONE || !Object_IsAwake(lander)) return;
	const int32_t *lp = (const int32_t *)Object_GetProperty(lander, PROP_POSITION);
	int32_t pos[3] = {hold.pos[0], hold.pos[1], hold.pos[2]};
	if (hold.mode == 2 || hold.mode == 3) {
		const int32_t *a = (const int32_t *)Object_GetProperty(hold.a, PROP_POSITION);
		if (!a || !lp) return;
		memcpy(pos, a, sizeof(pos));
		if (hold.mode == 3) {
			const int32_t *b = (const int32_t *)Object_GetProperty(hold.b, PROP_POSITION);
			if (!b) return;
			pos[0] = lp[0];
			pos[1] = (a[1] + b[1]) / 2;
			pos[2] = lp[2];
		}
	}
	Object_SetProperty(lander, PROP_POSITION, (intptr_t)pos);
	Lander *L = Lander_FromHandle(lander);
	if (L && L->body) memset(L->body->vel, 0, sizeof(L->body->vel));
}

static const task_type_t hold_type = {0, hold_update, NULL, NULL, NULL, NULL, NULL, 40};

void physobjs_test_reset(void) {
	memset(actions, 0, sizeof(actions));
	memset(&hold, 0, sizeof(hold));
	trace_mob = -1;
	trace_obj = OBJ_NONE;
	const char *env = getenv("LANDER_TEST_PHYSOBJS");
	if (!env) return;
	char buf[2048];
	snprintf(buf, sizeof(buf), "%s", env);
	int n = 0;
	bool trace = false;
	for (char *tok = strtok(buf, ","); tok && n < MAX_ACTIONS; tok = strtok(NULL, ",")) {
		if (!strncmp(tok, "trace", 5)) {
			trace = true;
			if (tok[5] == ':' && tok[6] == 'o') trace_obj = (ObjId)strtoul(tok + 7, NULL, 16);
			else if (tok[5] == ':') trace_mob = (int)strtol(tok + 6, NULL, 0);
			continue;
		}
		char *at = strchr(tok, '@');
		int32_t when = at ? (int32_t)strtol(at + 1, NULL, 0) : 1000;
		if (at) *at = 0;
		test_action_t *t = &actions[n];
		char *arg = strchr(tok, ':');
		if (arg) *arg++ = 0;
		snprintf(t->kind, sizeof(t->kind), "%s", tok);
		if (arg) {
			bool hex = !strcmp(tok, "fbox") || !strcmp(tok, "anim") || !strcmp(tok, "prop") || !strcmp(tok, "at") ||
				!strcmp(tok, "between");
			t->a = (uint32_t)strtoul(arg, &arg, hex ? 16 : 0);
			if (*arg == ':') t->b = (uint32_t)strtoul(arg + 1, &arg, !strcmp(tok, "between") ? 16 : 0);
			if (*arg == ':') t->c = (uint32_t)strtoul(arg + 1, &arg, 0);
		}
		InTime1(when, test_fire, (intptr_t)n);
		n++;
	}
	if (trace) Task_Create(&trace_type, NULL, 0);
	Task_Create(&hold_type, NULL, 0);
}
