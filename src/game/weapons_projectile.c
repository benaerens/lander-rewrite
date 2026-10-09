// Projectile (object type 2): Projectile_Create 0x452990, update 0x451ec0, render 0x452830,
// destroy 0x453470, properties 0x4537a0 / 0x4537c0, reflection 0x451d80. re/specs/weapons.md §1.
#include <stdio.h>

#include "weapons_internal.h"
#include "game.h"

// task data (+0x0c.. in the original, 0x20 bytes there); shooter and target are full ids here
// (the original packs them into a u16 record index + mob bit and re-reads the type)
typedef struct {
	int16_t inst;          // +0x0c render item
	int16_t pad;
	dyn_light_t *light;    // +0x0e light (-1 = none)
	int32_t pos[3];        // +0x10
	int16_t vel[3];        // +0x1c units per 40 ms
	uint8_t arg2;          // +0x26 damage / size
	uint8_t life;          // +0x27 ticks left
	uint8_t kind;          // +0x28
	uint8_t flags;         // +0x29 1 muzzle flash, 2 bounces
	uint8_t colour;        // +0x2a index into proj_colours
	uint8_t arg3;          // +0x2b sub type
	ObjId shooter;         // +0x22
	ObjId target;          // +0x24
	int32_t model;         // mission model of kinds 1/2 (released by the destroy), -1 otherwise
} proj_t;

_Static_assert(sizeof(proj_t) <= sizeof(((task_t *)0)->data), "projectile fits in task data");

static proj_t *proj(task_t *t) { return (proj_t *)t->data; }
static ObjId proj_id(task_t *t) { return (ObjId)Task_ToIndex(t) | 0x02020000u; }
static coord_t *proj_node(proj_t *p) { return p->inst >= 0 ? gInstances[p->inst].coord : NULL; }

static const uint32_t proj_colours[6] = {0x00c000, 0x00a0a0, 0x60a0ff, 0x0090d0, 0xd05070, 0xd0d0d0}; // 0x498f08
static const int32_t push_strength[10] = {0x600, 0x333, 0x666, 0, 0x600, 0x266, 0x100, 0, 0x266, 0x100}; // 0x498ee0
// DAT_0061e980: global models of the solid rounds, filled by Mission_Load (0x41414a): Model_AddRef
// 0xb, 0xc, 0xd, 0xb, -, 0xf, 0xa, 0xa, 0xa. Slot 4 (Flechette) is never written in the exe (it
// shows model record 0); the port uses 0xb for it.
static const int32_t round_models[9] = {0xb, 0xc, 0xd, 0xb, 0xb, 0xf, 0xa, 0xa, 0xa};

static task_type_t *proj_type(void) { return &gObjectTypes[OT_PROJECTILE].task; }

static void set_light(proj_t *p, coord_t *node, int r, int g, int b, int inner, int falloff) {
	dyn_light_t *l = dyn_light_create(1, node);   // Light_Create(1, node), flags |= 0x10
	if (!l) return;
	l->flags |= 0x10;
	l->color = vec3(r / 255.0f, g / 255.0f, b / 255.0f);
	l->inner = inner;
	l->falloff = falloff;
	p->light = l;
}

static void light_colour_table(proj_t *p, coord_t *node, int idx, int inner, int falloff) {
	uint32_t c = proj_colours[idx < 6 ? idx : 0];
	set_light(p, node, c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, inner, falloff);
}

// nearest player lander by |dx|+|dy|+|dz| (0x4538e0 / 0x453880)
static const int32_t *nearest_player(const int32_t *from, int32_t *dist) {
	const int32_t *best = NULL;
	int32_t bd = 0x7fff0000;
	for (int i = 0; i < gNbPlayers; i++) {
		const int32_t *p = (const int32_t *)Object_GetProperty(gPlayerObjects[i], PROP_POSITION);
		int32_t d = p ? w_abs(p[0] - from[0]) + w_abs(p[2] - from[2]) + w_abs(p[1] - from[1]) : 0x7fffffff;
		if (d < bd) {
			bd = d;
			best = p;
		}
	}
	if (dist) *dist = bd;
	return best;
}

// ---------------------------------------------------------------- drawing (render item callbacks)

static void draw_glow(model_inst_t *mi) { // 0x451c80: kinds 0, 3, 4, 7
	task_t *t = mi->user;
	if (!t || !t->type) return;
	proj_t *p = proj(t);
	const FwMatrix *w = coord_world(mi->coord);
	uint32_t c = proj_colours[p->colour < 6 ? p->colour : 0];
	int k = rand() % 0x1fff;  // flicker (not while paused)
	if (k > 0x1000) k = 0x1000;
	uint32_t s = 0;
	for (int sh = 0; sh < 32; sh += 8) { // 0x44a5b0
		uint32_t b = (((c >> sh) & 0xff) * (uint32_t)k) >> 12;
		s |= (b > 0xff ? 0xff : b) << sh;
	}
	int32_t size = p->kind == 3 ? p->arg2 << 10 : p->arg2 << 11;
	fx_draw_begin();
	fx_glow(w->t, s, (float)((size >> 4) * 12), true);
	fx_draw_end();
}

static void draw_streak(model_inst_t *mi) { // 0x45abd0: kinds 5, 6 (white head, fading tail 0x200 back)
	task_t *t = mi->user;
	if (!t || !t->type) return;
	const FwMatrix *w = coord_world(mi->coord);
	int32_t back[3] = {0, 0, -0x200}, tail[3];
	wm_apply(w, back, tail);
	wv_add(tail, w->t, tail);
	// screen half width (320 << 4) / z at the head: about 13 world units at the game's projection
	fx_draw_begin();
	fx_streak(w->t, tail, 13.0f, 0.0f, 0xffffff, 0x000000);
	fx_draw_end();
}

static void draw_beam(model_inst_t *mi) { // 0x45ae50: kind 8, half width arg2 * 320 / z pixels
	task_t *t = mi->user;
	if (!t || !t->type) return;
	proj_t *p = proj(t);
	const FwMatrix *w = coord_world(mi->coord);
	int32_t back[3] = {0, 0, -0x200}, tail[3];
	wm_apply(w, back, tail);
	wv_add(tail, w->t, tail);
	float half = p->arg2 * 0.83f;  // [A] the exe's 14-vertex beam strips: a red glow with a white core
	fx_draw_begin();
	fx_streak(w->t, tail, half, half, 0x3232c8, 0x101060);
	fx_streak(w->t, tail, half * 0.35f, half * 0.35f, 0xc0c0ff, 0x404080);
	fx_draw_end();
}

// ---------------------------------------------------------------- create

static void proj_create(task_t *t, void *arg) { // Projectile_Create 0x452990
	obj_args_t *a = arg;
	const proj_params_t *sp = a ? a->extra : NULL;
	proj_t *p = proj(t);
	memset(p, 0, sizeof(*p));
	p->inst = -1;
	p->model = -1;
	if (!sp) return;
	bool by_lander = OBJ_TYPE(sp->owner) == OT_LANDER && sp->owner != OBJ_NONE;
	int32_t speed = 0x10;
	inst_draw_fn draw = NULL;
	bool model = false;
	int32_t mission_model = -1, global = -1;
	switch (sp->kind) {
	case 0: case 4: case 7: speed = 0x96; draw = draw_glow; break;
	case 1: if (sp->arg3) global = round_models[sp->arg3 % 9]; else mission_model = G.level->mission->projectile_model[0];
		model = true; break;
	case 2: if (sp->arg3) global = round_models[sp->arg3 % 9]; else mission_model = G.level->mission->projectile_model[1];
		model = true; break;
	case 3: speed = (rand() & 0x2f) + 0x20; draw = draw_glow; break;
	case 5: case 6: draw = draw_streak; speed = 0x15e; break;
	case 8: draw = draw_beam; speed = 0x1c2; break;
	case 9:
		speed = sp->arg3 <= 2 ? 0x400 : sp->arg3 <= 5 ? 0x800 : 0x10;
		global = round_models[sp->arg3 % 9];
		model = true;
		break;
	}
	if (model) {
		const ppm_t *objs = G.level->objects;
		if (mission_model >= 0 && objs && mission_model < objs->count && objs->entries[mission_model].num_parts) {
			p->inst = inst_create(objs->entries[mission_model].parts[0], G.level->textures, NULL);
			p->model = mission_model;
		} else {
			if (global < 0) global = round_models[0];
			p->inst = inst_create(global_model(global), G.global_tex, NULL);
		}
		if (p->inst >= 0) gInstances[p->inst].zone = -1;
	} else if (draw) {
		p->inst = inst_create(NULL, NULL, NULL);   // Coord_Alloc + RenderItem_Alloc, draw callback
		if (p->inst >= 0) {
			gInstances[p->inst].draw = draw;
			gInstances[p->inst].user = t;
			gInstances[p->inst].zone = -1;
		}
	}
	coord_t *node = proj_node(p);
	const FwMatrix *f = sp->world;
	int32_t m[3], dir[3];
	wm_apply(f, sp->muzzle, m);
	wv_add(f->t, m, p->pos);
	p->target = sp->target;
	p->arg2 = (uint8_t)sp->arg2;
	p->arg3 = (uint8_t)sp->arg3;
	if (sp->kind == 3) { // burning debris: a random direction in the upper hemisphere of the frame
		int32_t r[3] = {(rand() & 0x1fff) - 0x1000, 0x1000, (rand() & 0x1fff) - 0x1000};
		wm_apply(f, r, dir);
		phys_normalise(dir);
		int32_t o[3];
		wv_scale(dir, speed << 4, o);
		wv_add(p->pos, o, p->pos);
	} else {
		if (node) node->local = *f;
		wv_set(dir, f->m[0][2], f->m[1][2], f->m[2][2]);
		if (by_lander && sp->target != OBJ_NONE) { // every lander shot aims at the weapon lock
			const int32_t *tp = (const int32_t *)Object_GetProperty(sp->target, PROP_POSITION);
			if (tp) {
				wv_sub(tp, p->pos, dir);
				phys_normalise(dir);
			}
		}
	}
	int32_t v[3];
	wv_scale(dir, speed, v);
	wv_add(v, sp->vel, v);
	p->vel[0] = (int16_t)v[0];
	p->vel[1] = (int16_t)v[1];
	p->vel[2] = (int16_t)v[2];
	p->kind = sp->kind;
	p->flags = sp->zero;
	p->light = NULL;
	p->colour = (uint8_t)sp->f08;
	p->shooter = sp->owner;
	ObjId owner = sp->owner, self = proj_id(t);
	bool second = gProjectileSecondShot != 0;
	int Q = LIGHTING_QUALITY;
	switch (sp->kind) {
	case 0:
		p->life = 0x30;
		if (!second) eng_snd_once(owner, 0x96);
		if (Q > 0x400) light_colour_table(p, node, p->colour, (Q << 9) >> 12, (Q << 10) >> 12);
		break;
	case 1: case 2:
		p->life = sp->kind == 1 ? 0x40 : 0x60;
		if (!second) eng_snd_once(owner, sp->kind == 1 && by_lander ? 0x40 : 0xa0);
		eng_snd_loop(self, 0x9f, -1);
		set_light(p, node, 0xff, 200, 100, 0x100, 0x400);
		break;
	case 3:
		if (Q > 0x200) light_colour_table(p, node, p->colour, ((Q << 9) >> 12) + 300, ((Q << 10) >> 12) + 800);
		p->life = (uint8_t)((rand() & 0xf) + 0x10);
		break;
	case 4: {
		int sid = 0x96;
		if (by_lander) {
			switch (sp->arg2) {
			case 0x10: sid = 0x32; break;
			case 0x18: sid = 0x33; break;
			case 0x20: sid = 0x34; break;
			case 0x28: sid = 0x35; break;
			case 0x30: sid = 0x36; break;
			default: sid = 0; break;
			}
		}
		if (sid) eng_snd_once(owner, sid);
		p->life = 0x40;
		set_light(p, node, 0, 0xff, 0, 300, 800);
		break;
	}
	case 5: case 6:
		if (!second) eng_snd_once(owner, 0x98);
		p->life = 0x28;
		if (Q > 0x400) set_light(p, node, 0xff, 0xff, 0, (Q << 9) >> 12, (Q << 10) >> 12);
		break;
	case 7:
		if (!second) eng_snd_once(owner, 0x41);
		p->life = 0xff;
		p->flags |= 2;
		p->colour = 5;
		set_light(p, node, 0xff, 0xff, 0xff, 0, 0x2000);
		break;
	case 8: {
		int sid = 0x97;
		if (by_lander) sid = sp->arg2 == 0x10 ? 0x37 : sp->arg2 == 0x20 ? 0x38 : sp->arg2 == 0x30 ? 0x39 : 0;
		if (sid) eng_snd_once(owner, sid);
		set_light(p, node, 0xff, 100, 100, ((Q << 9) >> 12) + 300, ((Q << 10) >> 12) + 800);
		p->life = 0x28;
		break;
	}
	case 9: {
		static const int16_t lander_sid[6] = {0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};
		static const int16_t other_sid[6] = {0x98, 0x98, 0x98, 0x3d, 0x99, 0x98};
		if (sp->arg3 <= 5) {
			if (!second) eng_snd_once(owner, by_lander ? lander_sid[sp->arg3] : other_sid[sp->arg3]);
			p->life = sp->arg3 <= 2 ? 0x10 : 8;
		}
		break;
	}
	}
	if (weapons_debug && sp->kind != 3) {
		printf("weapons: shot %08x kind %d by %08x at %d %d %d vel %d %d %d target %08x\n", self, sp->kind, owner,
			p->pos[0], p->pos[1], p->pos[2], p->vel[0], p->vel[1], p->vel[2], sp->target);
	}
	if (p->flags & 1) { // muzzle flash
		if (sp->kind == 0 || sp->kind == 5 || sp->kind == 6 || sp->kind == 8 || sp->kind == 9) {
			eng_particle_spawn(2, p->pos, 0);
		} else if (sp->kind == 1 || sp->kind == 2) {
			eng_particle_spawn(1, p->pos, 0);
		}
	}
}

ObjId projectile_create(const proj_params_t *sp) { // Objects_Create(2, {-1, ?, params})
	if (!gObjectTypes[OT_PROJECTILE].registered) return OBJ_NONE;
	obj_args_t a = {OBJ_NONE, -1, NULL, NULL, (void *)sp, NULL};
	task_t *t = Task_CreateNow(proj_type(), &a);
	return t ? proj_id(t) : OBJ_NONE;
}

void weapons_fire_projectile(const LanderShotParams *sp) { projectile_create(sp); }

// ---------------------------------------------------------------- update

// 0x451d80: v' = v + 2|v.n| n (the hit normal), kinds 1 2 5 6 8 9 turn their model with it
static void reflect(proj_t *p, const phys_hit_t *h, coord_t *node) {
	int32_t v[3] = {p->vel[0], p->vel[1], p->vel[2]}, v0[3] = {p->vel[0], p->vel[1], p->vel[2]}, n2[3], o[3];
	int32_t len = phys_normalise(v);
	int32_t d = wv_dot(h->normal, v);
	wv_scale(h->normal, (w_abs(d) * len) >> 11, n2);
	wv_add(h->point, n2, o);
	wv_add(o, v0, o);
	wv_sub(o, h->point, o);
	p->vel[0] = (int16_t)o[0];
	p->vel[1] = (int16_t)o[1];
	p->vel[2] = (int16_t)o[2];
	switch (p->kind) {
	case 1: case 2: case 5: case 6: case 8: case 9:
		if (node) {
			phys_normalise(o);
			node->local.m[0][2] = (int16_t)o[0];
			node->local.m[1][2] = (int16_t)o[1];
			node->local.m[2][2] = (int16_t)o[2];
			wm_orient_z(&node->local);
		}
		break;
	}
}

static void hit_sound(task_t *t, int sid) { eng_snd_once_excl(proj_id(t), sid); } // 0x47bf20

// the projectile node's matrix moved to t (the exe writes the node's t, then passes node + 0xc)
static const FwMatrix *node_at(coord_t *node, const int32_t at[3]) {
	static FwMatrix m;
	if (node) m = node->local;
	else fw_identity(&m);
	wv_copy(at, m.t);
	if (node) wv_copy(at, node->local.t);
	return &m;
}

static void proj_update(task_t *t) { // 0x451ec0
	proj_t *p = proj(t);
	coord_t *node = proj_node(p);
	if (--p->life == 0) {
		if (p->kind == 6) { // timed out: a small explosion
			hit_sound(t, 0x6f);
			explosion_create(0, 0x10, node_at(node, p->pos), p->shooter);
		}
		Task_Kill(t);
		return;
	}
	if (p->kind == 2 && p->life < 0x5d && p->life > 0x20) { // homing missile
		const int32_t *tp = p->target == OBJ_NONE ? nearest_player(p->pos, NULL)
			: (const int32_t *)Object_GetProperty(p->target, PROP_POSITION);
		if (tp) {
			int32_t d[3], v[3] = {p->vel[0], p->vel[1], p->vel[2]};
			wv_sub(tp, p->pos, d);
			phys_normalise(d);
			int32_t len = phys_normalise(v);
			int32_t w = (wv_dot(v, d) + 0x1000) >> 2;
			if (w < 0xd99) w = 0xd99;
			wv_scale(v, w, v);
			wv_scale(d, 0x1000 - w, d);
			wv_add(v, d, v);
			phys_normalise(v);
			if (node) {
				node->local.m[0][2] = (int16_t)v[0];
				node->local.m[1][2] = (int16_t)v[1];
				node->local.m[2][2] = (int16_t)v[2];
				wm_orient_z(&node->local);
			}
			wv_scale(v, len, v);
			p->vel[0] = (int16_t)v[0];
			p->vel[1] = (int16_t)v[1];
			p->vel[2] = (int16_t)v[2];
		}
	}
	int32_t next[3] = {p->pos[0] + p->vel[0], p->pos[1] + p->vel[1], p->pos[2] + p->vel[2]};
	if (((p->kind == 1 || p->kind == 2) && p->life > 0x20) || (p->kind == 3 && (rand() & 3) == 0)) {
		eng_particle_spawn(0, p->pos, 0);  // smoke trail
	}
	if (p->kind != 3) {
		ObjId shooter = p->shooter;
		phys_hit_t h;
		memset(&h, 0, sizeof(h));
		h.frame_kind = -1;
		if (fx_segment_hit(p->pos, next, 7, &h, shooter)) {
			if (weapons_debug) {
				printf("weapons: shot %08x kind %d hits %s %08x at %d %d %d (surface class %d)\n", proj_id(t), p->kind,
					h.frame_kind == PHYS_FRAME_OBJECT ? "object" : "world", h.frame_kind == PHYS_FRAME_OBJECT ? (uint32_t)h.object : 0,
					h.point[0], h.point[1], h.point[2], eng_surface_class(h.surface));
			}
			eng_snd_stop_object(proj_id(t));
			int ending = 0;   // 0 = continue (moved to next), 1 = end of the projectile
			if (p->kind == 7 || h.frame_kind != PHYS_FRAME_OBJECT) { // the world
				int cls = eng_surface_class(h.surface);
				bool bounce_now = false;
				switch (cls) {
				case 0: hit_sound(t, 0x3ea); break;
				case 1: hit_sound(t, 0x48); break;
				case 2:
					hit_sound(t, 0x3d6);
					eng_particle_spawn(6, h.point, 0);
					if (p->kind == 7) bounce_now = true; else ending = 1;
					break;
				case 3: hit_sound(t, 0x5c); break;
				case 4:
					hit_sound(t, p->kind == 1 || p->kind == 2 ? 0x54 : 0x53);
					eng_particle_spawn(3, h.point, 0);
					eng_particle_spawn(4, h.point, 0x80);
					ending = 1;
					break;
				case 5: hit_sound(t, 0x66); break;
				case 6: hit_sound(t, 0x3e0); break;
				}
				if (!ending && !bounce_now && !(p->flags & 2)) ending = 1;
				if (!ending) { // bounce; the move below still goes to `next` (as in the exe)
					p->pos[0] = (h.normal[0] >> 9) + h.point[0];
					p->pos[1] = (h.normal[1] >> 9) + h.point[1];
					p->pos[2] = (h.normal[2] >> 9) + h.point[2];
					reflect(p, &h, node);
					if (cls == 3) eng_particle_spawn(0, h.point, 0);
					if (p->kind == 7) {
						p->vel[0] >>= 1;
						p->vel[1] >>= 1;
						p->vel[2] >>= 1;
					}
				}
			} else { // an object
				ObjId obj = (ObjId)h.object;
				LanderEvent ev = {EV_PROJECTILE_HIT, {(intptr_t)obj, (intptr_t)(int8_t)((p->arg2 * 10) >> 4),
					(intptr_t)shooter, (intptr_t)p->kind}};
				bool raise = true;
				if (OBJ_TYPE(obj) == OT_LANDER) {
					Lander *L = Lander_FromHandle(obj);
					if (L && L->armourShift0 && (p->kind == 0 || (p->kind > 2 && p->kind <= 8))) {
						reflect(p, &h, node);   // reflective armour: no hit
						raise = false;
					}
				} else if (p->kind != 9) {
					int cls = eng_surface_class(h.surface);
					int32_t hp = (int32_t)Object_GetProperty(obj, PROP_HITPOINTS);
					int32_t ar = (int32_t)Object_GetProperty(obj, PROP_ARMOUR);
					int dead = hp == 0 || ar == 0xff;
					static const int16_t sids[7] = {0x3eb, 0x49, 0x3d7, 0x5d, 0, 0x67, 0x3e1};
					if (cls == 4) hit_sound(t, p->kind == 1 || p->kind == 2 ? 0x54 : 0x53);
					else if (cls >= 0 && cls <= 6) hit_sound(t, sids[cls] - dead);
				}
				if (raise) {
					Event_Raise(&ev);
					phys_body_t *b = (phys_body_t *)Object_GetProperty(obj, PROP_BODY);
					if (b) { // push the body at the hit point
						int32_t f[3] = {p->vel[0] << 3, p->vel[1] << 3, p->vel[2] << 3}, at[3];
						wv_sub(f, b->vel, f);
						wv_scale(f, (push_strength[p->kind % 10] * p->arg2 * 2) >> 4, f);
						wv_set(at, h.point[0] << 3, h.point[1] << 3, h.point[2] << 3);
						phys_body_add_force(b, f, 0, at);
						b->impulse_sum = 1;
						int32_t vel[3] = {p->vel[0], p->vel[1], p->vel[2]};
						fx_sparks(h.point, vel, push_strength[p->kind % 10] >> 8, 7);
					}
					ending = 1;
				}
			}
			if (ending) {
				bool small_puff = p->kind == 0 || p->kind == 4 || p->kind == 5 || p->kind == 8 || p->kind == 9;
				bool explodes = p->kind == 1 || p->kind == 2 || p->kind == 6;
				if (small_puff && p->arg3 == 5) explodes = true;   // explosive rounds
				if (explodes) explosion_create(p->kind == 2, 5, node_at(node, h.point), shooter);
				else if (small_puff) eng_particle_spawn(0, h.point, 0);
				Task_Kill(t);
				return;
			}
		}
	}
	// gravity and motor (0x452322)
	int16_t g = (int16_t)(gGravity >> 3);
	bool thrust = false;
	switch (p->kind) {
	case 1:
		if (p->life > 0x38) thrust = true;
		else if (p->life < 0x20) p->vel[1] -= g;
		break;
	case 2:
		if (p->life < 0x20) p->vel[1] -= g;
		if (p->life > 0x58) thrust = true;
		break;
	case 3: case 7:
		p->vel[1] -= g;
		break;
	}
	if (thrust && node) {
		p->vel[0] += node->local.m[0][2] >> 7;
		p->vel[1] += node->local.m[1][2] >> 7;
		p->vel[2] += node->local.m[2][2] >> 7;
	}
	wv_copy(next, p->pos);
}

// ---------------------------------------------------------------- render / destroy / properties

static void proj_render(task_t *t, uint32_t time) { // 0x452830
	proj_t *p = proj(t);
	coord_t *node = proj_node(p);
	uint32_t period = t->type->period;
	int32_t frac = (int32_t)(((uint32_t)(time - t->wake + period) << 12) / period);
	int32_t v[3] = {p->vel[0], p->vel[1], p->vel[2]}, d[3];
	wv_scale(v, frac, d);
	if (node) {
		node->local.t[0] = p->pos[0] + d[0];
		node->local.t[1] = p->pos[1] + d[1];
		node->local.t[2] = p->pos[2] + d[2];
		inst_queue(p->inst);
	}
	if (p->kind == 7 && p->light) { // flare flicker, fading out
		int c = rand() % 0x3f + 0xc0;
		if (p->life < 0x10) {
			c = (c * p->life) >> 4;
			p->light->inner = 0;
			p->light->falloff = (p->life << 13) >> 4;
		}
		p->light->color = vec3(c / 255.0f, c / 255.0f, c / 255.0f);
	}
	if (p->kind == 3 && p->light) {
		int c = rand() % 0xff;
		if (p->life < 0x10) c = (c * p->life) >> 4;
		p->light->color = vec3(c / 255.0f, c / 255.0f, c / 255.0f);
	}
}

static void proj_destroy(task_t *t) { // 0x453470
	proj_t *p = proj(t);
	dyn_light_destroy(p->light);
	p->light = NULL;
	// [A] the exe leaves the missile's engine loop (0x9f) to the sound system when the life ends
	if (p->kind == 1 || p->kind == 2) eng_snd_stop_object(proj_id(t));
	if (p->inst >= 0) inst_free(p->inst);
	p->inst = -1;
}

static intptr_t proj_get_pos(ObjId id) { // 0x4537a0
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	return t && t->type == proj_type() ? (intptr_t)proj(t)->pos : 0;
}

static intptr_t proj_get_vel(ObjId id) { // 0x4537c0: units per second
	static int32_t v[3];
	task_t *t = NULL;
	Objects_GetData(id, NULL, &t);
	if (!t || t->type != proj_type()) return 0;
	for (int i = 0; i < 3; i++) v[i] = proj(t)->vel[i] * 0x19;
	return (intptr_t)v;
}

void projectile_register(void) {
	ObjectType_Register(OT_PROJECTILE, proj_update, proj_render, proj_create, proj_destroy, 0);
	ObjectProperty_RegisterGet(OT_PROJECTILE, PROP_POSITION, proj_get_pos);
	ObjectProperty_RegisterGet(OT_PROJECTILE, PROP_VELOCITY, proj_get_vel);
	for (int prop = 0; prop < 12; prop++) ObjectProperty_RegisterSet(OT_PROJECTILE, prop, objprop_ignore);
}

void projectile_reset(void) {}
