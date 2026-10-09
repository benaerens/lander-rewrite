// Pods, force boxes, inertial frames and the scripted cameras: object types 12, 16, 20, 5 and 7
// (ObjectTypes_RegisterAll 0x448760). Spec: re/specs/physobjs.md; the lander's side of the
// tractor beam is lander.md §6.
#include <stdlib.h>
#include <string.h>

#include "physobjs.h"
#include "lights.h"
#include "game.h"
#include "../platform.h"

int32_t gPhysQuerySlot, gPhysQueryTime;   // DAT_0058b3c0, DAT_0058b394
FwMatrix gInertialFrameInterp;            // 0x61f3e0 (InertialFrame render)

// ---------------------------------------------------------------- PSX fixed-point helpers
// 32-bit wrapping arithmetic like the original (products formed in 64 bits, then truncated)

static int32_t w32(int64_t v) { return (int32_t)(uint32_t)(uint64_t)v; }

static void v_scale(const int32_t *v, int32_t s, int32_t *o) { // FwVectorScale 0x436960
	for (int k = 0; k < 3; k++) o[k] = w32((int64_t)v[k] * s + 0x800) >> 12;
}

static int32_t v_dot(const int32_t *a, const int32_t *b) { // FwVectorDotProduct 0x436800
	return w32((int64_t)a[1] * b[1] + (int64_t)a[2] * b[2] + 0x800 + (int64_t)b[0] * a[0]) >> 12;
}

static void v_cross(const int32_t *a, const int32_t *b, int32_t *o) { // FwVectorCrossProduct 0x4367a0
	int32_t x = w32((int64_t)b[2] * a[1] - (int64_t)a[2] * b[1] + 0x800) >> 12;
	int32_t y = w32((int64_t)b[0] * a[2] - (int64_t)a[0] * b[2] + 0x800) >> 12;
	int32_t z = w32((int64_t)a[0] * b[1] - (int64_t)b[0] * a[1] + 0x800) >> 12;
	o[0] = x; o[1] = y; o[2] = z;
}

static void apply_lv(const FwMatrix *m, const int32_t *v, int32_t *o) { // ApplyMatrixLV 0x436110
	int32_t r[3];
	for (int i = 0; i < 3; i++) {
		r[i] = w32((int64_t)m->m[i][0] * v[0] + (int64_t)m->m[i][1] * v[1] + (int64_t)m->m[i][2] * v[2] + 0x800) >> 12;
	}
	memcpy(o, r, sizeof(r));
}

static void inv_apply(const FwMatrix *m, const int32_t *v, int32_t *o) { // invmatvec(2) 0x436a10/0x436aa0: M^T v
	int32_t r[3];
	for (int i = 0; i < 3; i++) {
		r[i] = w32((int64_t)m->m[0][i] * v[0] + (int64_t)m->m[1][i] * v[1] + (int64_t)m->m[2][i] * v[2] + 0x800) >> 12;
	}
	memcpy(o, r, sizeof(r));
}

static void mul_matrix0(const FwMatrix *a, const FwMatrix *b, FwMatrix *out) { // MulMatrix0 0x435790 (rotation)
	int16_t r[3][3];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r[i][j] = (int16_t)((a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + 0x800) >> 12);
		}
	}
	memcpy(out->m, r, sizeof(r));
}

static void mul_rot_y_neg(const FwMatrix *in, FwMatrix *out, int32_t a) { // 0x42d1e0: out = in * Ry(-a)
	FwMatrix r;
	fw_identity(&r);
	int32_t c = fx_rcos(-a), s = fx_rsin(-a);
	r.m[0][0] = r.m[2][2] = (int16_t)c;
	r.m[0][2] = (int16_t)s;
	r.m[2][0] = (int16_t)-s;
	mul_matrix0(in, &r, out);
}

static void identity_rot(FwMatrix *m) { // 0x431730
	memset(m->m, 0, sizeof(m->m));
	m->m[0][0] = m->m[1][1] = m->m[2][2] = 0x1000;
}

// Coord_SetAxes 0x431830: column 2 = a, column 1 = -b, column 0 = a x b (truncated)
static void set_axes(FwMatrix *m, const int32_t *a, const int32_t *b) {
	if (!a[0] && !a[1] && !a[2]) {
		identity_rot(m);
		return;
	}
	m->m[0][2] = (int16_t)a[0]; m->m[1][2] = (int16_t)a[1]; m->m[2][2] = (int16_t)a[2];
	m->m[0][1] = (int16_t)-b[0]; m->m[1][1] = (int16_t)-b[1]; m->m[2][1] = (int16_t)-b[2];
	m->m[0][0] = (int16_t)(w32((int64_t)a[1] * b[2] - (int64_t)b[1] * a[2]) >> 12);
	m->m[1][0] = (int16_t)(w32((int64_t)b[0] * a[2] - (int64_t)a[0] * b[2]) >> 12);
	m->m[2][0] = (int16_t)(w32((int64_t)a[0] * b[1] - (int64_t)b[0] * a[1]) >> 12);
}

static void copy_matrix(FwMatrix *dst, const FwMatrix *src) { *dst = *src; dst->pad = 0; }

// Tractor_InRange 0x44c800: d = lander - point (world units); modes 1-2 normalise d in place
static int32_t tractor_in_range(int32_t *d, int mode) {
	if (abs(d[0]) > 0x800 || abs(d[1]) > 0x800 || abs(d[2]) > 0x800) return 0;
	if (mode == 0) {
		if (d[1] <= gTractorRange && d[1] >= 0) {
			int32_t r = (d[1] << 8) / gTractorRange + 0x40;
			if (d[2] * d[2] + d[0] * d[0] < r * r) return d[1];
		}
		return 0;
	}
	int32_t range = gTractorRange;
	if (mode != 1) {
		if (mode != 2) return 0;
		range = gTractorRange + 0x100;
	}
	int32_t l = phys_normalise(d);
	return l < range + 0x100 ? l : 0;
}

// 0x44c750: (body position - point) >> 3, both x8
static void tractor_delta(const int32_t *p8, const phys_body_t *b, int32_t *d) {
	for (int k = 0; k < 3; k++) d[k] = (b->cur->pos[k] - p8[k]) >> 3;
}

// ================================================================ ForceBox body list (0x44f0b0..)

#define FB_LIST 128
static uint16_t fb_next[FB_LIST], fb_size[FB_LIST], fb_head = 0xffff, fb_free;
static ObjId fb_id[FB_LIST];

static void fb_list_reset(void) { // 0x44f0b0
	for (int i = 0; i < FB_LIST; i++) fb_next[i] = (uint16_t)(i + 1);
	fb_next[FB_LIST - 1] = 0xffff;
	fb_free = 0;
	fb_head = 0xffff;
}

void eng_forcebox_add_body(ObjId id, int32_t size) { // ForceBox_AddBody 0x44f0f0
	if (fb_free == 0xffff) return;
	uint16_t i = fb_free;
	fb_free = fb_next[i];
	fb_next[i] = fb_head;
	fb_head = i;
	fb_id[i] = id;
	fb_size[i] = (uint16_t)size;
}

void eng_forcebox_remove_body(ObjId id) { // ForceBox_RemoveBody 0x44f150
	uint16_t *link = &fb_head;
	while (*link != 0xffff) {
		uint16_t i = *link;
		if (fb_id[i] == id) {
			*link = fb_next[i];
			fb_next[i] = fb_free;
			fb_free = i;
			return;
		}
		link = &fb_next[i];
	}
}

// ================================================================ Pod (type 12)

enum {
	POD_ATTACHED = 1,     // towed: tether + spring
	POD_HOOK_CENTRE = 2,  // hook point (0,0,0): the spring acts at the centre of mass
	POD_DROPPED = 4,      // Pod_SetTarget(-1): the beam ignores it until -2/-3/a lander
	POD_TARGETED = 8,     // a lander's beam has locked on (lander +0x34c = pod | 0x80000000)
	POD_PHYSICAL = 0x10,  // has a rigid body
	POD_DYNAMICS = 0x20,  // the body came from the model's 0xFACCFACE block
};

typedef struct {
	bool used;
	task_t *task;
	phys_body_t *body;        // +0x10 while physical (the original reuses the instance field)
	phys_body_t *lander_body; // +0x14 while attached
	coord_t *node;            // +0x18 the model node the body drives
	ObjId lander;             // +0x1c lander that targets / tows the pod
	int16_t tether;           // +0x20 tether render item
	uint16_t hook[3];         // +0x22 hook point in the pod frame (default 0, 0x80, 0)
	uint8_t flags;            // +0x28 POD_*
	uint8_t spring;           // +0x29 spring constant >> 5 (default 0x80)
	dyn_light_t *light;       // +0x2a tether light
	FwMatrix slot[2];         // property 2 answers during the physics step (0x5534a0)
} pod_t;

#define MAX_PODS 64
static pod_t pods[MAX_PODS];
static int32_t pod_drag = 0x2000;           // DAT_004989fc: Pod_Script_SetupDrag is global, never reset
static const int32_t pod_mass = 0x578;      // DAT_004989f8 mass of a pod without a dynamics block

static pod_t *pod_of(ObjId id) {
	ObjId real = id & 0x03ffffffu;
	if (OBJ_TYPE(real) != OT_POD) return NULL;
	task_t *t = NULL;
	Objects_GetData(real, NULL, &t);
	if (!t || t->type != &gObjectTypes[OT_POD].task) return NULL;
	return objstate(t)->extra;
}

// the pod's position while it has no body (AnimObject property 0, FUN_00455fc0)
static const int32_t *pod_anim_position(objstate_t *o) {
	coord_t *c = objstate_coord(o);
	if (c) return coord_world(c)->t;
	return o->rec ? (const int32_t *)&o->rec->pos : NULL;
}

// property 0 (0x44dcf0): body: cur matrix t = pos >> 3 (written), else the model's position
static intptr_t pod_get_position(ObjId id) {
	task_t *t = NULL;
	Objects_GetData(id & 0x03ffffffu, NULL, &t);
	if (!t || !t->type) return 0;
	objstate_t *o = objstate(t);
	pod_t *p = o->extra;
	if (p && (p->flags & POD_PHYSICAL)) {
		phys_state_t *s = p->body->cur;
		for (int k = 0; k < 3; k++) s->mat.t[k] = s->pos[k] >> 3;
		return (intptr_t)s->mat.t;
	}
	return (intptr_t)pod_anim_position(o);
}

// 0x44c790: hook point in world x8 for a body state
static void pod_hook_point(const pod_t *p, const phys_state_t *s, int32_t *out) {
	if (p->flags & POD_HOOK_CENTRE) {
		memcpy(out, s->pos, 3 * sizeof(int32_t));
		return;
	}
	int32_t h[3] = {(int32_t)((uint32_t)p->hook[0] << 3), (int32_t)((uint32_t)p->hook[1] << 3),
		(int32_t)((uint32_t)p->hook[2] << 3)};
	apply_lv((const FwMatrix *)&s->mat, h, h);
	for (int k = 0; k < 3; k++) out[k] = h[k] + s->pos[k];
}

// 0x44cb00: a cuboid hull: 8 corner contact points, mass in 8 points at 0.707 of the corners
static void pod_cuboid(phys_body_t *b, const int32_t *size, int32_t mass) {
	b->num_points = 0;
	b->inertia[0] = b->inertia[1] = b->inertia[2] = 0;
	int32_t x = size[0] >> 1, y = size[1] >> 1, z = size[2] >> 1;
	phys_body_add_point(b, x, -y, z);
	phys_body_add_point(b, -x, -y, z);
	phys_body_add_point(b, -x, y, z);
	phys_body_add_point(b, x, y, z);
	phys_body_add_point(b, x, y, -z);
	phys_body_add_point(b, -x, y, -z);
	phys_body_add_point(b, -x, -y, -z);
	phys_body_add_point(b, x, -y, -z);
	b->mass = 0;
	int32_t pt[3] = {(x * 0xb50) >> 12, (y * 0xb50) >> 12, (z * 0xb50) >> 12};
	int32_t m = mass >> 3;
	phys_body_add_mass(b, pt, m); pt[0] = -pt[0];
	phys_body_add_mass(b, pt, m); pt[1] = -pt[1];
	phys_body_add_mass(b, pt, m); pt[0] = -pt[0];
	phys_body_add_mass(b, pt, m); pt[2] = -pt[2];
	phys_body_add_mass(b, pt, m); pt[0] = -pt[0];
	phys_body_add_mass(b, pt, m); pt[1] = -pt[1];
	phys_body_add_mass(b, pt, m); pt[0] = -pt[0];
	phys_body_add_mass(b, pt, m);
}

// 0x44ceb0: the pod gets a rigid body where its model is (the first time the beam hooks it)
static bool pod_make_physical(task_t *t, pod_t *p) {
	objstate_t *o = objstate(t);
	const int32_t *pp = pod_anim_position(o);
	int32_t pos[3] = {pp ? pp[0] : 0, pp ? pp[1] : 0, pp ? pp[2] : 0};
	if (o->inst < 0) return false;
	phys_body_t *b = phys_body_alloc();
	if (!b) return false;          // the original does not check (8 bodies)
	animobj_destroy(t);             // anim word saved, animation detached (0x455a50)
	eng_forcebox_add_body(o->id, 0x80);
	p->node = gInstances[o->inst].coord;
	int32_t p8[3] = {pos[0] << 3, pos[1] << 3, pos[2] << 3};
	p->body = b;
	phys_body_init(b, 0x1000);
	if (phys_body_setup_dynamics(b, gInstances[o->inst].model, 0, -1) == 0) {
		p->flags |= POD_DYNAMICS;
		phys_body_set_position(b, p8);
	} else {
		static const int32_t size[3] = {0x100, 0x100, 0x100};
		phys_body_init(b, pod_mass);   // 0x44ccf0
		phys_body_set_position(b, p8);
		pod_cuboid(b, size, pod_mass);
	}
	b->coord = (phys_matrix_t *)&p->node->local;
	b->owner = (int32_t)o->id;
	p->flags |= POD_PHYSICAL;
	return true;
}

// 0x44ca20: point a node's -Y axis along dir (the tractor beam / tether frame)
static void beam_orient(FwMatrix *m, int32_t *dir) {
	if (dir[0] == 0 && dir[2] == 0) {
		identity_rot(m);
		if (dir[1] > 0) {   // m * Rz(0x800)
			m->m[0][0] = m->m[1][1] = -0x1000;
		}
		return;
	}
	int32_t up[3] = {0, 0x1000, 0}, x[3];
	phys_normalise(dir);
	v_cross(dir, up, x);
	phys_normalise(x);
	v_cross(dir, x, up);
	set_axes(m, up, dir);
}

// tether draw callback 0x44c980: the tractor beam drawer on the tether node
static void pod_tether_draw(model_inst_t *mi) {
	pod_t *p = mi->user;
	if (!p || (gReflectionPass && !gLanderAboveLiquid)) return;
	if (p->flags & POD_HOOK_CENTRE) {
		eng_fx_tractor_beam(0x300, 0x96, (int32_t)game_now(), 0, mi->coord);
	} else {
		eng_fx_tractor_beam(0x2e4, 0x20, (int32_t)game_now(), 1, mi->coord);
	}
}

// 0x44c8c0: drop the tether
static void pod_detach(pod_t *p) {
	dyn_light_destroy(p->light);
	p->light = NULL;
	inst_free(p->tether);
	p->tether = -1;
	Lander_SetHookTarget(p->lander, OBJ_NONE);
	p->flags &= ~POD_ATTACHED;
}

// 0x44c940: back to a plain model
static void pod_make_static(objstate_t *o, pod_t *p) {
	if (p->flags & POD_ATTACHED) pod_detach(p);
	phys_body_free(p->body);
	p->body = NULL;
	eng_forcebox_remove_body(o->id);
	p->flags &= ~POD_PHYSICAL;
}

// Pod_Attach 0x44d040: tether render item with an orange light, hook the lander, event 0xC
static void pod_attach(task_t *t, pod_t *p, phys_body_t *lander_body, ObjId lander) {
	objstate_t *o = objstate(t);
	if (!(p->flags & POD_PHYSICAL) && !pod_make_physical(t, p)) return;
	p->lander = OBJ_NONE;
	p->tether = inst_create(NULL, NULL, NULL);
	if (p->tether >= 0) {
		gInstances[p->tether].user = p;
		gInstances[p->tether].draw = pod_tether_draw;
		gInstances[p->tether].zone = -1;
	}
	p->lander_body = lander_body;
	p->lander = lander;
	p->flags |= POD_ATTACHED;
	p->light = dyn_light_create(1, p->tether >= 0 ? gInstances[p->tether].coord : NULL);
	if (p->light) {
		p->light->flags |= 0x10;
		p->light->color = vec3(1.0f, 100 / 255.0f, 0);
		p->light->inner = 300;     // Light_SetRange(300, 600)
		p->light->falloff = 600;
		p->light->pos[1] = -350;
	}
	Lander_SetHookTarget(lander, o->id);
	LanderEvent ev = {EV_POD_HOOKED, {(intptr_t)o->id, (intptr_t)lander}};
	Event_Raise(&ev);
}

static pod_t *pod_alloc(void) {
	for (int i = 0; i < MAX_PODS; i++) {
		if (!pods[i].used) {
			memset(&pods[i], 0, sizeof(pods[i]));
			pods[i].used = true;
			return &pods[i];
		}
	}
	return NULL;
}

static void pod_create(task_t *t, void *arg) { // Pod_Create 0x44d180
	obj_args_t *a = arg;
	animobj_create(t, arg);            // AnimState_Init
	objstate_t *o = objstate(t);
	pod_t *p = pod_alloc();
	o->extra = p;
	if (!p) return;
	p->task = t;
	p->hook[0] = 0;
	p->hook[1] = 0x80;
	p->hook[2] = 0;
	p->spring = 0x80;
	p->flags = a && a->params ? ((const uint8_t *)a->params)[3] : 0;
	p->lander = OBJ_NONE;
	p->tether = -1;
	if (gPhysicsTask) t->wake = gPhysicsTask->wake;   // in step with the physics task
}

static void pod_destroy(task_t *t) { // 0x44d1d0
	objstate_t *o = objstate(t);
	pod_t *p = o->extra;
	if (!p) {
		animobj_destroy(t);
		return;
	}
	if (o->params) ((uint8_t *)o->params)[3] = p->flags & 0x44;
	if (p->flags & POD_PHYSICAL) {
		pod_make_static(o, p);
	} else {
		animobj_destroy(t);
	}
	p->used = false;
	o->extra = NULL;
}

static void pod_update(task_t *t) { // 0x44d220
	objstate_t *o = objstate(t);
	pod_t *p = o->extra;
	if (!p) {
		animobj_update(t);
		return;
	}
	int32_t pt[3], d[3];
	if (!(p->flags & (POD_ATTACHED | POD_DROPPED))) {
		if (!(p->flags & POD_PHYSICAL)) {
			const int32_t *pos = pod_anim_position(o);
			for (int k = 0; k < 3; k++) pt[k] = pos ? (int32_t)((uint32_t)pos[k] << 3) : 0;
		} else {
			memcpy(pt, p->body->cur->pos, sizeof(pt));
		}
		for (int k = 0; k < 3; k++) pt[k] += (int32_t)((uint32_t)p->hook[k] * 8);   // not rotated
		if (!(p->flags & POD_TARGETED)) {
			for (int i = 0; i < gNbPlayers; i++) {
				ObjId pl = gPlayerObjects[i];
				phys_body_t *lb = (phys_body_t *)Object_GetProperty(pl, PROP_BODY);
				if (!lb || !Lander_IsTractorOn(pl)) continue;
				tractor_delta(pt, lb, d);
				if (tractor_in_range(d, 0) && Lander_SetHookTarget(pl, o->id | 0x80000000u)) {
					p->lander = pl;
					p->flags |= POD_TARGETED;
					break;
				}
			}
		} else {
			phys_body_t *lb = (phys_body_t *)Object_GetProperty(p->lander, PROP_BODY);
			int32_t r = 0;
			if (lb) {
				tractor_delta(pt, lb, d);
			}
			if (!lb || !Lander_IsTractorOn(p->lander) || !(r = tractor_in_range(d, 1))) {
				p->flags &= ~POD_TARGETED;
				Lander_SetHookTarget(p->lander, OBJ_NONE);
			} else if (gTractorRange + 0x80 < r) {   // the ship pulled away: hooked
				p->flags &= ~POD_TARGETED;
				pod_attach(t, p, lb, p->lander);
			}
		}
	}
	if (p->flags & POD_ATTACHED) {   // two-way spring, rest length gTractorRange
		phys_body_t *pb = p->body, *lb = p->lander_body;
		int32_t f[3], dv[3];
		pod_hook_point(p, pb->cur, pt);
		tractor_delta(pt, lb, d);
		int32_t len = phys_normalise(d) - gTractorRange;
		for (int k = 0; k < 3; k++) dv[k] = lb->vel[k] - pb->vel[k];
		int32_t damp = v_dot(d, dv);
		v_scale(d, (w32((int64_t)damp * -0x100) >> 9) - ((int32_t)((uint32_t)len * p->spring) >> 4), f);
		phys_body_add_force(lb, f, 1, NULL);
		for (int k = 0; k < 3; k++) f[k] = -f[k];
		phys_body_add_force(pb, f, p->flags & POD_HOOK_CENTRE, pt);
	}
	if (p->flags & POD_PHYSICAL) {   // gravity, drag, angular drag
		phys_body_t *pb = p->body;
		int32_t f[3], drag[3], tq[3];
		v_scale(phys_g.gravity, pb->mass << 3, f);
		v_scale(pb->vel, (pod_drag * gAirResistance) >> 9, drag);
		for (int k = 0; k < 3; k++) f[k] -= drag[k];
		phys_body_add_force(pb, f, 1, NULL);
		v_scale(pb->angvel, -(pod_drag * gAirResistance) >> 3, tq);
		inv_apply((const FwMatrix *)&pb->cur->mat, tq, tq);
		for (int k = 0; k < 3; k++) pb->torque[k] += tq[k];
		phys_body_write_transform(pb, pb->coord, 0);   // Phys_SyncCoord(body, now, 0)
		return;
	}
	animobj_update(t);   // AnimState_Step
}

static void pod_render(task_t *t, uint32_t time) { // 0x44d5f0
	objstate_t *o = objstate(t);
	pod_t *p = o->extra;
	if (!p || !(p->flags & POD_PHYSICAL)) {
		animobj_render(t, time);
		return;
	}
	uint32_t period = t->type->period;
	int32_t frac = (int32_t)(((time - t->wake + period) * 0x1000u) / period);
	phys_body_write_transform(p->body, p->body->coord, frac);
	if (!(p->flags & POD_ATTACHED)) return;
	phys_body_t *lb = p->lander_body;
	phys_body_write_transform(lb, lb->coord, frac);
	int32_t h[3];
	const FwMatrix *nm = &p->node->local;
	if (!(p->flags & POD_HOOK_CENTRE)) {
		int32_t hk[3] = {p->hook[0], p->hook[1], p->hook[2]};
		apply_lv(nm, hk, h);
		for (int k = 0; k < 3; k++) h[k] += nm->t[k];
	} else {
		memcpy(h, nm->t, sizeof(h));
	}
	if (p->tether < 0) return;
	FwMatrix *tm = &gInstances[p->tether].coord->local;
	memcpy(tm->t, lb->cur->mat.t, sizeof(tm->t));
	int32_t dir[3] = {h[0] - tm->t[0], h[1] - tm->t[1], h[2] - tm->t[2]};
	phys_normalise(dir);
	beam_orient(tm, dir);
	inst_queue(p->tether);   // DrawGroup_Add(0x491be0): drawn every frame
}

// property 1 set: Pod_SetTarget 0x44d9e0 (-1 drop, -2 enable, -3 detach, else attach if in range)
static void Pod_SetTarget(ObjId id, intptr_t target) {
	task_t *t = NULL;
	Objects_GetData(id & 0x03ffffffu, NULL, &t);
	if (!t || t->type != &gObjectTypes[OT_POD].task) return;
	objstate_t *o = objstate(t);
	pod_t *p = o->extra;
	if (!p) return;
	int32_t v = (int32_t)target;
	if (v == -1) {
		if (p->flags & POD_PHYSICAL) pod_make_static(o, p);
		p->flags |= POD_DROPPED;
		o->flags = 0;            // the model stays where the body left it; animation reset
		o->frame = 0;
		if (o->params) {
			((uint8_t *)o->params)[0] = 0;
			o->anim = ((uint8_t *)o->params)[1];
		}
		return;
	}
	if (v == -2) {
		p->flags &= ~POD_DROPPED;
		return;
	}
	if (v == -3) {
		if (p->flags & POD_ATTACHED) pod_detach(p);   // the original detaches unconditionally
		p->flags &= ~POD_DROPPED;
		return;
	}
	const int32_t *pos = (const int32_t *)pod_get_position(o->id);
	int32_t pt[3], d[3];
	for (int k = 0; k < 3; k++) pt[k] = (int32_t)(((uint32_t)p->hook[k] + (uint32_t)(pos ? pos[k] : 0)) * 8);
	p->flags &= ~POD_DROPPED;
	phys_body_t *lb = (phys_body_t *)Object_GetProperty((ObjId)v, PROP_BODY);
	if (!lb) return;
	tractor_delta(pt, lb, d);
	if (tractor_in_range(d, 2)) {
		if (p->flags & POD_ATTACHED) pod_detach(p);
		pod_attach(t, p, lb, (ObjId)v);
	}
}

void eng_pod_release(ObjId pod) { // Lander_Destroy: Pod_SetTarget(hookTarget, -3)
	if (OBJ_TYPE(pod) == OT_POD) Pod_SetTarget(pod & 0x7fffffffu, -3);
}

static intptr_t pod_get_target(ObjId id) { // 0x44db40
	pod_t *p = pod_of(id);
	return p ? (intptr_t)(int32_t)p->lander : 0;
}

static intptr_t pod_get_body(ObjId id) { // 0x44db60
	pod_t *p = pod_of(id);
	return p && (p->flags & POD_PHYSICAL) ? (intptr_t)p->body : 0;
}

static intptr_t pod_get_flags(ObjId id) { // 0x44dd50
	pod_t *p = pod_of(id);
	return p ? p->flags : 0;
}

// property 2 (0x44db90): with a body its state (during the step: interpolated to the query time,
// one buffer per slot); else the animation's pose at the query time, or the model node
static intptr_t pod_get_matrix(ObjId id) {
	pod_t *p = pod_of(id);
	if (p && (p->flags & POD_PHYSICAL)) {
		if (phys_g.in_physics) {
			FwMatrix *m = &p->slot[gPhysQuerySlot & 1];
			phys_body_interpolate(p->body, (phys_matrix_t *)m, gPhysQueryTime);
			for (int k = 0; k < 3; k++) m->t[k] >>= 3;
			return (intptr_t)m;
		}
		phys_state_t *s = p->body->cur;
		for (int k = 0; k < 3; k++) s->mat.t[k] = s->pos[k] >> 3;
		return (intptr_t)&s->mat;
	}
	const FwMatrix *m = phys_g.in_physics ? anim_pose_at(id, gPhysQueryTime, gPhysQuerySlot)
		: anim_pose_at(id, gGameUpdateTime, 0);
	if (m) return (intptr_t)m;
	int16_t i = Object_GetInstance(id & 0x03ffffffu);
	for (int part = (int)((id >> 26) & 0x3f); part > 0 && i >= 0; part--) i = gInstances[i].next_part;
	return i >= 0 ? (intptr_t)coord_world(gInstances[i].coord) : 0;
}

static void pod_set_position(ObjId id, intptr_t v) { // 0x44dd70
	const int32_t *pos = (const int32_t *)v;
	pod_t *p = pod_of(id);
	if (!pos) return;
	if (p && (p->flags & POD_PHYSICAL)) {
		int32_t p8[3] = {pos[0] << 3, pos[1] << 3, pos[2] << 3};
		phys_body_set_position(p->body, p8);
		memcpy(p->body->cur->mat.t, pos, 3 * sizeof(int32_t));
		memcpy(p->body->next->mat.t, pos, 3 * sizeof(int32_t));
	} else {
		map_rec_t *r = Object_GetRecord(id & 0x03ffffffu);   // 0x447110: the mob record moves
		if (r) {
			r->pos.x = pos[0];
			r->pos.y = pos[1];
			r->pos.z = pos[2];
		}
	}
	if (Object_IsAwake(id & 0x03ffffffu)) {
		coord_t *c = p && p->node ? p->node : NULL;
		if (!c) {   // the original writes its +0x18 node only (set once the pod was physical)
			objstate_t *o = objstate_from_id(id & 0x03ffffffu);
			c = o ? objstate_coord(o) : NULL;
		}
		if (c) memcpy(c->local.t, pos, 3 * sizeof(int32_t));
	}
}

static void pod_set_matrix(ObjId id, intptr_t v) { // 0x44dc40
	const FwMatrix *m = (const FwMatrix *)v;
	pod_t *p = pod_of(id);
	if (!m) return;
	if (p && (p->flags & POD_PHYSICAL)) {
		phys_body_t *b = p->body;
		for (int s = 0; s < 2; s++) memcpy(&b->state[s].mat, m, sizeof(phys_matrix_t));
		memset(b->vel, 0, 3 * sizeof(int32_t));
		memset(b->angvel, 0, 3 * sizeof(int32_t));
	}
	if (Object_IsAwake(id & 0x03ffffffu)) {
		coord_t *c = p && p->node ? p->node : NULL;
		if (!c) {
			objstate_t *o = objstate_from_id(id & 0x03ffffffu);
			c = o ? objstate_coord(o) : NULL;
		}
		if (c) copy_matrix(&c->local, m);
	}
}

static void pod_set_flags(ObjId id, intptr_t v) { // 0x44de60
	pod_t *p = pod_of(id);
	if (!p) return;
	uint32_t f = (uint32_t)v;
	if (f & 0x80000000u) p->flags &= (uint8_t)~f;
	else p->flags |= (uint8_t)f;
}

// ---------------------------------------------------------------- Pod_Script_* (mission DLL exports)
// They act on the pod's body, so the scripts call them once the pod is physical (EV_POD_HOOKED).
// The original does not check that and would write through the instance field: ignored here.

void Pod_Script_SetupDynamicsCuboid(ObjId pod, int32_t sx, int32_t sy, int32_t sz, int32_t mass) { // 0x44cd20
	pod_t *p = pod_of(pod);
	if (!p || !(p->flags & POD_PHYSICAL)) return;
	if (p->flags & POD_DYNAMICS) {
		p->body->mass = mass;
		phys_body_inertia_from_points(p->body);
		return;
	}
	int32_t size[3] = {sx, sy, sz};
	pod_cuboid(p->body, size, mass);
}

void Pod_Script_SetupDrag(ObjId pod, int32_t drag) { // 0x44cda0: global
	(void)pod;
	pod_drag = drag;
}

void Pod_Script_SetupMass(ObjId pod, int32_t mass) { // 0x44cdb0
	pod_t *p = pod_of(pod);
	if (!p || !(p->flags & POD_PHYSICAL)) return;
	if (p->flags & POD_DYNAMICS) {
		int16_t i = Object_GetInstance(pod & 0x03ffffffu);
		phys_body_setup_dynamics(p->body, i >= 0 ? gInstances[i].model : NULL, 0, mass);
		return;
	}
	p->body->mass = mass;
	phys_body_inertia_from_points(p->body);
}

void Pod_Script_SetupHookPoint(ObjId pod, int32_t x, int32_t y, int32_t z) { // 0x44ce20
	pod_t *p = pod_of(pod);
	if (!p) return;
	p->hook[0] = (uint16_t)x;
	p->hook[1] = (uint16_t)y;
	p->hook[2] = (uint16_t)z;
	if (x == 0 && y == 0 && z == 0) p->flags |= POD_HOOK_CENTRE;
	else p->flags &= ~POD_HOOK_CENTRE;
}

void Pod_Script_SetupSpringConstant(ObjId pod, int32_t k) { // 0x44ce80
	pod_t *p = pod_of(pod);
	if (p) p->spring = (uint8_t)(k >> 5);
}

// ================================================================ ForceBox (type 16)

typedef struct {
	bool used;
	int32_t half[3];      // +0x10 half extents (local frame)
	int16_t strength;     // +0x20 (property 5)
	uint8_t mode;         // +0x22 0 push along local +Y, 1 push out from the centre, 2 turbulence outside
	uint8_t flags;        // +0x23 1/2/4: no fade along x / y / z (property 8)
	int16_t dir[3];       // +0x24 mode 2: wandering direction
} fbox_t;

#define MAX_FBOX 64
static fbox_t fboxes[MAX_FBOX];

static void forcebox_create(task_t *t, void *arg) { // ForceBox_Create 0x44f590
	obj_args_t *a = arg;
	objstate_init(t, a);
	objstate_t *o = objstate(t);
	o->coord = coord_alloc(NULL);
	if (o->coord && o->rec) {
		fw_from_packed(o->rec->rot, &o->coord->local);
		o->coord->local.t[0] = o->rec->pos.x;
		o->coord->local.t[1] = o->rec->pos.y;
		o->coord->local.t[2] = o->rec->pos.z;
	}
	fbox_t *f = NULL;
	for (int i = 0; i < MAX_FBOX; i++) {
		if (!fboxes[i].used) {
			f = &fboxes[i];
			break;
		}
	}
	o->extra = f;
	if (!f || !o->params) return;
	memset(f, 0, sizeof(*f));
	f->used = true;
	f->half[0] = o->params[0];
	f->half[1] = o->params[1];
	f->half[2] = o->params[2];
	f->strength = (int16_t)(o->params[3] & 0xffff);
	f->mode = ((const uint8_t *)&o->params[3])[2];
	f->flags = ((const uint8_t *)&o->params[3])[3];
}

static void forcebox_destroy(task_t *t) { // 0x44f600
	objstate_t *o = objstate(t);
	coord_free(o->coord);
	o->coord = NULL;
	if (o->extra) ((fbox_t *)o->extra)->used = false;
	o->extra = NULL;
}

static void forcebox_update(task_t *t) { // ForceBox_Update 0x44f1b0
	objstate_t *o = objstate(t);
	fbox_t *f = o->extra;
	if (!f || !o->coord) return;
	const FwMatrix *m = &o->coord->local;
	for (uint16_t i = fb_head; i != 0xffff; i = fb_next[i]) {
		const int32_t *pos = (const int32_t *)Object_GetProperty(fb_id[i], PROP_POSITION);
		if (!pos) continue;
		int32_t d[3] = {pos[0] - m->t[0], pos[1] - m->t[1], pos[2] - m->t[2]};
		inv_apply(m, d, d);
		int32_t s = fb_size[i];
		int32_t hx = s + f->half[0], hy = s + f->half[1], hz = s + f->half[2];
		int32_t ax = abs(d[0]), ay = abs(d[1]), az = abs(d[2]);
		int32_t v[3];
		if (ax < hx && ay < hy && az < hz) {
			if (s == 0) continue;
			phys_body_t *b = (phys_body_t *)Object_GetProperty(fb_id[i], PROP_BODY);
			int32_t k = f->strength * 2;
			if (f->mode == 0) {           // along the box's +Y, fading to 0 at the top
				if (!(f->flags & 2)) k = ((hy - d[1]) * k) / (hy * 2);
				v[0] = m->m[0][1]; v[1] = m->m[1][1]; v[2] = m->m[2][1];
			} else if (f->mode == 1) {    // away from the centre
				if (!(f->flags & 2)) k = ((hy - ay) * k) / hy;
				v[0] = pos[0] - m->t[0]; v[1] = pos[1] - m->t[1]; v[2] = pos[2] - m->t[2];
				phys_normalise(v);
			} else {
				continue;   // mode 2 does nothing inside; other modes use an uninitialised vector
			}
			if (!(f->flags & 1)) k = ((hx - ax) * k) / hx;
			if (!(f->flags & 4)) k = ((hz - az) * k) / hz;
			v_scale(v, k, v);
			if (b) phys_body_add_force(b, v, 1, NULL);
		} else if (f->mode == 2) {      // outside a mode-2 box: random buffeting growing with the distance
			int32_t e = ax - hx;
			if (e < 1) e = 0;
			if (ay - hy > 0) e += ay - hy;
			if (az - hz > 0) e += az - hz;
			if (e > 0x400) e = 0x400;
			int32_t g = e * 2 * f->strength;
			int32_t mag = (g + ((g >> 31) & 0x1fff)) >> 13;
			v[0] = (f->dir[0] >> 1) - 0x400 + eng_rand() % 0x7ff;
			v[1] = (f->dir[1] >> 1) - 0x400 + eng_rand() % 0x7ff;
			v[2] = (f->dir[2] >> 1) - 0x400 + eng_rand() % 0x7ff;
			phys_normalise(v);
			f->dir[0] = (int16_t)v[0];
			f->dir[1] = (int16_t)v[1];
			f->dir[2] = (int16_t)v[2];
			v_scale(v, mag, v);
			v[1] >>= 2;
			phys_body_t *b = (phys_body_t *)Object_GetProperty(fb_id[i], PROP_BODY);
			if (!b) continue;
			phys_body_add_force(b, v, 1, NULL);
			int32_t tq[3];
			tq[0] = eng_rand() % 0x1fff - 0x1000;
			tq[1] = eng_rand() % 0x1fff - 0x1000;
			tq[2] = eng_rand() % 0x1fff - 0x1000;
			v_scale(tq, mag << 8, tq);
			for (int k = 0; k < 3; k++) b->torque[k] += tq[k];
		}
	}
}

static void forcebox_set_strength(ObjId id, intptr_t v) { // 0x44f710
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(id, &params, &t);
	if (t && t->type == &gObjectTypes[OT_FORCEBOX].task && objstate(t)->extra) {
		((fbox_t *)objstate(t)->extra)->strength = (int16_t)v;
	}
	if (params) params[3] = (int32_t)(((uint32_t)params[3] & 0xffff0000u) | ((uint32_t)v & 0xffff));
}

static void forcebox_set_flags(ObjId id, intptr_t v) { // 0x44f750
	int32_t *params = NULL;
	task_t *t = NULL;
	Objects_GetData(id, &params, &t);
	if (!params) return;
	uint8_t *fl = &((uint8_t *)&params[3])[3];
	uint32_t f = (uint32_t)v;
	uint8_t nf = (f & 0x80000000u) ? (uint8_t)(~f & *fl) : (uint8_t)(f | *fl);
	if (t && t->type == &gObjectTypes[OT_FORCEBOX].task && objstate(t)->extra) {
		((fbox_t *)objstate(t)->extra)->flags = nf;
	}
	*fl = nf;
}

// ================================================================ InertialFrame (type 20)
// Pool word 1 byte 0 = mode: 0 the frame follows its own (animated) node; 1/2 its Y axis points
// toward / away from the target lander's position, the lander's heading folded into the frame.

static int32_t if_target_pos[3], if_target_delta[3];   // DAT_0055fcd0 / DAT_0055fce0

static int if_mode(const objstate_t *o) { return o->params ? ((const uint8_t *)o->params)[4] : 0; }

// 0x457c80: point the node's Y axis at the target (extrapolated by frac), Z near the frame's Z
static void if_orient(objstate_t *o, int32_t frac, int mode) {
	int32_t up[3] = {gInertialFrame.m[0][2], gInertialFrame.m[1][2], gInertialFrame.m[2][2]};
	int32_t p[3], d[3], x[3];
	if (frac == 0) {
		memcpy(p, if_target_pos, sizeof(p));
	} else {
		v_scale(if_target_delta, frac, p);
		for (int k = 0; k < 3; k++) p[k] += if_target_pos[k];
	}
	FwMatrix *m = &o->coord->local;
	for (int k = 0; k < 3; k++) d[k] = p[k] - m->t[k];
	if (mode == 2) {
		for (int k = 0; k < 3; k++) d[k] = -d[k];
	}
	phys_normalise(d);
	v_cross(d, up, x);
	phys_normalise(x);
	v_cross(x, d, up);
	for (int k = 0; k < 3; k++) {
		m->m[k][2] = (int16_t)up[k];
		m->m[k][1] = (int16_t)d[k];
		m->m[k][0] = (int16_t)x[k];
	}
}

static void if_create(task_t *t, void *arg) { // InertialFrame_Create 0x457fb0
	obj_args_t *a = arg;
	obj_args_t a2 = a ? *a : (obj_args_t){OBJ_NONE, -1, NULL, NULL, NULL, NULL};
	int mode = a2.params ? ((const uint8_t *)a2.params)[4] : 0;
	coord_t *node = NULL;
	if (mode == 0) {   // a render item on the node carries the animation (mob record +0x20)
		int16_t inst = inst_create(NULL, NULL, NULL);
		node = inst >= 0 ? gInstances[inst].coord : NULL;
		if (inst >= 0) eng_map_set_mob_model(a2.id, inst);
		a2.inst = inst;
		animobj_create(t, &a2);
	} else {
		objstate_init(t, &a2);
		node = coord_alloc(NULL);
		if (mode < 3) {
			memset(if_target_pos, 0, sizeof(if_target_pos));
			memset(if_target_delta, 0, sizeof(if_target_delta));
		}
	}
	objstate_t *o = objstate(t);
	o->coord = node;
	o->mask = OBJ_NONE;   // +0x1c target
	if (!node) return;
	if (a2.rec) {
		fw_from_packed(a2.rec->rot, &node->local);
		node->local.t[0] = a2.rec->pos.x;
		node->local.t[1] = a2.rec->pos.y;
		node->local.t[2] = a2.rec->pos.z;
	}
	gUseInertialFrame = 1;
	copy_matrix(&gInertialFrameInterp, coord_world(node));
	copy_matrix(&gInertialFrame, coord_world(node));
}

static void if_update(task_t *t) { // InertialFrame_Update 0x457df0
	objstate_t *o = objstate(t);
	int mode = if_mode(o);
	if (!o->coord) return;
	if (mode == 0) {
		animobj_update(t);
		if (o->flags & 0x10) animobj_apply(t);
		copy_matrix(&gInertialFrame, coord_world(o->coord));
		SetGravity(gGravity);
	} else if (mode < 3) {
		int32_t old = Lander_SwapHeading((ObjId)o->mask, 0);
		mul_rot_y_neg(&gInertialFrame, &gInertialFrame, -old);
		if ((ObjId)o->mask != OBJ_NONE) {
			const int32_t *p = (const int32_t *)Object_GetProperty((ObjId)o->mask, PROP_POSITION);
			if (p) {
				for (int k = 0; k < 3; k++) {
					if_target_delta[k] = p[k] - if_target_pos[k];
					if_target_pos[k] = p[k];
				}
			}
			if_orient(o, 0, mode);
			copy_matrix(&gInertialFrame, coord_world(o->coord));
			SetGravity(gGravity);
		}
	}
}

static void if_render(task_t *t, uint32_t time) { // 0x457f20
	objstate_t *o = objstate(t);
	int mode = if_mode(o);
	if (!o->coord) return;
	if (mode == 0) {
		animobj_render(t, time);
	} else if (mode < 3 && (ObjId)o->mask != OBJ_NONE) {
		uint32_t period = t->type->period;
		if_orient(o, (int32_t)(((time - t->wake + period) * 0x1000u) / period), mode);
	}
	copy_matrix(&gInertialFrameInterp, coord_world(o->coord));
}

static void if_destroy(task_t *t) { // 0x4580b0
	objstate_t *o = objstate(t);
	int mode = if_mode(o);
	if (mode == 0) {
		animobj_destroy(t);
		eng_map_set_mob_model(o->id, -1);
		inst_free(o->inst);   // frees the node with it
		o->inst = -1;
		o->coord = NULL;
	} else if (mode < 3) {
		if (o->mask != 0) {   // the frame's yaw goes back into the lander's heading
			Lander_SwapHeading((ObjId)o->mask, fx_ratan2(gInertialFrame.m[0][2], gInertialFrame.m[2][2]));
		}
		gUseInertialFrame = 0;
		SetGravity(gGravity);
	}
	coord_free(o->coord);
	o->coord = NULL;
	gUseInertialFrame = 0;
}

static void if_set_target(ObjId id, intptr_t v) { // 0x4581a0
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	if (!t || t->type != &gObjectTypes[OT_INERTIALFRAME].task) return;
	objstate_t *o = objstate(t);
	ObjId target = (ObjId)v;
	o->mask = target;
	const int32_t *p = (const int32_t *)Object_GetProperty(target, PROP_POSITION);
	if (p) memcpy(if_target_pos, p, sizeof(if_target_pos));
	int mode = if_mode(o);
	if (mode != 0 && mode < 3 && o->coord) {
		const FwMatrix *m = (const FwMatrix *)Object_GetProperty(target, PROP_MATRIX);
		if (m) mul_rot_y_neg(m, &gInertialFrame, 0x200);   // the logical frame back to the body frame
		Lander_SwapHeading(target, 0x200);
		if_orient(o, 0, mode);
		copy_matrix(&gInertialFrameInterp, coord_world(o->coord));
		copy_matrix(&gInertialFrame, coord_world(o->coord));
		SetGravity(gGravity);
	}
}

// ================================================================ AnimCam (type 5)
// The view node gView is the animated render item's node: the animation (played with the clone
// flag 0x40 so the node itself moves) writes it, then the camera's look axis is turned from +X to
// +Z (Ry 0x400, Rz 0x800), as the original does every frame on the node.

static cam_view_t animcam_node;   // the node's rotation counter (gView +0)

static void animcam_create(task_t *t, void *arg) { // AnimCam_Create 0x454fd0
	obj_args_t *a = arg;
	obj_args_t a2 = a ? *a : (obj_args_t){OBJ_NONE, -1, NULL, NULL, NULL, NULL};
	int16_t inst = inst_create(NULL, NULL, NULL);
	coord_t *node = inst >= 0 ? gInstances[inst].coord : NULL;
	if (node && a2.rec) {
		fw_from_packed(a2.rec->rot, &node->local);
		node->local.t[0] = a2.rec->pos.x;
		node->local.t[1] = a2.rec->pos.y;
		node->local.t[2] = a2.rec->pos.z;
	}
	a2.inst = inst;
	animobj_create(t, &a2);
	objstate_t *o = objstate(t);
	o->coord = node;
	cam_view_t *view = game_view();
	if (node) {
		memcpy(view->mat.m, node->local.m, sizeof(view->mat.m));
		memcpy(view->mat.t, node->local.t, sizeof(view->mat.t));
	}
	animcam_node.counter = 0;
	gCameraObject = o->id;
}

static void animcam_render(task_t *t, uint32_t time) { // 0x455050
	objstate_t *o = objstate(t);
	animobj_render(t, time);
	coord_t *node = o->coord;
	if (!node) return;
	memcpy(animcam_node.mat.m, node->local.m, sizeof(animcam_node.mat.m));
	animcam_post(&animcam_node);
	memcpy(node->local.m, animcam_node.mat.m, sizeof(node->local.m));
	const FwMatrix *w = coord_world(node);
	cam_view_t *view = game_view();
	memcpy(view->mat.m, w->m, sizeof(view->mat.m));
	memcpy(view->mat.t, w->t, sizeof(view->mat.t));
}

static void animcam_destroy(task_t *t) { // 0x455020
	objstate_t *o = objstate(t);
	if (gCameraObject == o->id) gCameraObject = OBJ_NONE;   // the original writes 0 unconditionally
	animobj_destroy(t);
	inst_free(o->inst);
	o->inst = -1;
	o->coord = NULL;
}

// ================================================================ FlyCam (type 7)

static flycam_t gFlyCam;
static int32_t flycam_player = -1;

static void flycam_obj_create(task_t *t, void *arg) { // FlyCam_Create 0x459700
	obj_args_t *a = arg;
	objstate_t *o = objstate(t);
	cam_view_t *view = game_view();
	memset(o, 0, sizeof(*o));
	o->inst = -1;
	if (!a || !a->rec) {
		flycam_create(&gFlyCam, view, NULL, 0);
		o->id = (ObjId)Task_ToIndex(t) | 0x02070000u;
	} else {
		cvec3_t pos = {a->rec->pos.x, a->rec->pos.y, a->rec->pos.z};
		flycam_create(&gFlyCam, view, &pos, (int32_t)(a->rec->rot << 11) >> 21);
		o->id = a->id;
		o->rec = a->rec;
	}
	flycam_player = eng_player_add();
	eng_player_set_object(flycam_player, o->id);
	gCameraObject = o->id;   // Camera_SetTarget
	gCameraTask = t;
}

static void flycam_obj_update(task_t *t) { // Flycam_Update 0x4593e0
	(void)t;
	flycam_input_t in = {
		input_down(BTN_MOUSE_LEFT), input_down(BTN_MOUSE_RIGHT), input_down(BTN_KEY(KEY_RSHIFT)),
		(int32_t)input_axis(AXIS_MOUSE_DX), (int32_t)input_axis(AXIS_MOUSE_DY),
	};
	flycam_update(&gFlyCam, game_view(), &in);
	// X fires a test projectile (Objects_Create(2)) in the original: debug only, not ported
}

static void flycam_obj_render(task_t *t, uint32_t time) { // 0x4596a0
	flycam_frame(&gFlyCam, game_view(), time, t->wake, t->type->period);
}

static void flycam_obj_destroy(task_t *t) { // 0x4597e0
	(void)t;
	if (flycam_player >= 0) eng_player_remove(flycam_player);   // the original passes the object id
	flycam_player = -1;
	gCameraObject = OBJ_NONE;   // Camera_Clear
	gCameraTask = NULL;
}

static intptr_t flycam_get_position(ObjId id) { (void)id; return (intptr_t)&gFlyCam.pos; } // 0x459800

// ================================================================ registration, per mission

phys_body_t *physobjs_object_body(uint32_t handle) { // 0x413880
	return (phys_body_t *)Object_GetProperty(handle, PROP_BODY);
}

void physobjs_register_types(void) {
	ObjectType_Register(OT_ANIMCAM, animobj_update, animcam_render, animcam_create, animcam_destroy, 0);
	ObjectType_Register(OT_FLYCAM, flycam_obj_update, flycam_obj_render, flycam_obj_create, flycam_obj_destroy, 0);
	ObjectProperty_RegisterGet(OT_FLYCAM, 0, flycam_get_position);

	ObjectType_Register(OT_POD, pod_update, pod_render, pod_create, pod_destroy, 2);
	ObjectType_SetFrequency(OT_POD, 0x28);
	ObjectProperty_RegisterGet(OT_POD, 0, pod_get_position);
	ObjectProperty_RegisterGet(OT_POD, 2, pod_get_matrix);
	ObjectProperty_RegisterGet(OT_POD, 6, pod_get_body);
	ObjectProperty_RegisterGet(OT_POD, 1, pod_get_target);
	ObjectProperty_RegisterGet(OT_POD, 8, pod_get_flags);
	ObjectProperty_RegisterSet(OT_POD, 1, Pod_SetTarget);
	ObjectProperty_RegisterSet(OT_POD, 0, pod_set_position);
	ObjectProperty_RegisterSet(OT_POD, 2, pod_set_matrix);
	ObjectProperty_RegisterSet(OT_POD, 8, pod_set_flags);

	ObjectType_Register(OT_FORCEBOX, forcebox_update, NULL, forcebox_create, forcebox_destroy, 0);
	ObjectProperty_RegisterSet(OT_FORCEBOX, 5, forcebox_set_strength);
	ObjectProperty_RegisterSet(OT_FORCEBOX, 8, forcebox_set_flags);

	ObjectType_Register(OT_INERTIALFRAME, if_update, if_render, if_create, if_destroy, 0);
	ObjectProperty_RegisterSet(OT_INERTIALFRAME, 1, if_set_target);
}

void physobjs_test_reset(void);   // physobjs_test.c (LANDER_TEST_PHYSOBJS)

void physobjs_reset(void) {
	memset(pods, 0, sizeof(pods));
	memset(fboxes, 0, sizeof(fboxes));
	fb_list_reset();
	gUseInertialFrame = 0;
	fw_identity(&gInertialFrame);
	fw_identity(&gInertialFrameInterp);
	gPhysQuerySlot = 0;
	gPhysQueryTime = 0;
	flycam_player = -1;
	physobjs_test_reset();
}
