// Shared internals of the combat modules (weapons*.c): projectiles, explosions, turrets, damage.
#ifndef WEAPONS_INTERNAL_H
#define WEAPONS_INTERNAL_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "weapons.h"
#include "lights.h"
#include "sound.h"
#include "camera.h"
#include "../mission.h"

// quality settings of the options screen (registry "Lighting Quality" / "Explosion Quality");
// the exe's defaults 0x490cbc / 0x490cc0
#define LIGHTING_QUALITY  0x200
#define EXPLOSION_QUALITY 0x400

// ---------------------------------------------------------------- PSX style vector maths (4.12)
static inline int32_t wv_w(int64_t v) { return (int32_t)(uint32_t)v; }            // 32-bit wrap
static inline void wv_set(int32_t *o, int32_t x, int32_t y, int32_t z) { o[0] = x; o[1] = y; o[2] = z; }
static inline void wv_copy(const int32_t *a, int32_t *o) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; }
static inline void wv_add(const int32_t *a, const int32_t *b, int32_t *o) { // FwVectorAdd 0x4368d0
	wv_set(o, a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}
static inline void wv_sub(const int32_t *a, const int32_t *b, int32_t *o) { // FwVectorSubtract 0x436900
	wv_set(o, a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}
static inline void wv_scale(const int32_t *v, int32_t s, int32_t *o) { // FwVectorScale 0x436960
	wv_set(o, wv_w(((int64_t)v[0] * s + 0x800) >> 12), wv_w(((int64_t)v[1] * s + 0x800) >> 12),
		wv_w(((int64_t)v[2] * s + 0x800) >> 12));
}
static inline int32_t wv_dot(const int32_t *a, const int32_t *b) { // FwVectorDotProduct 0x436800
	return wv_w(((int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] + (int64_t)a[2] * b[2] + 0x800) >> 12);
}
static inline void wv_cross(const int32_t *a, const int32_t *b, int32_t *o) { // 0x4367a0
	int32_t r[3] = {wv_w(((int64_t)b[2] * a[1] - (int64_t)a[2] * b[1] + 0x800) >> 12),
		wv_w(((int64_t)b[0] * a[2] - (int64_t)a[0] * b[2] + 0x800) >> 12),
		wv_w(((int64_t)a[0] * b[1] - (int64_t)b[0] * a[1] + 0x800) >> 12)};
	wv_copy(r, o);
}
static inline void wm_apply(const FwMatrix *m, const int32_t *v, int32_t *o) { // ApplyMatrixLV 0x436110
	int32_t r[3];
	for (int i = 0; i < 3; i++) {
		r[i] = wv_w(((int64_t)m->m[i][0] * v[0] + (int64_t)m->m[i][1] * v[1] + (int64_t)m->m[i][2] * v[2] + 0x800) >> 12);
	}
	wv_copy(r, o);
}
static inline void wm_apply_t(const FwMatrix *m, const int32_t *v, int32_t *o) { // invmatvec 0x436a10
	int32_t r[3];
	for (int i = 0; i < 3; i++) {
		r[i] = wv_w(((int64_t)m->m[0][i] * v[0] + (int64_t)m->m[1][i] * v[1] + (int64_t)m->m[2][i] * v[2] + 0x800) >> 12);
	}
	wv_copy(r, o);
}
static inline int32_t w_abs(int32_t v) { return v < 0 ? -v : v; }
int32_t w_catan(int32_t v);                          // catan 0x4354b0: atan(v / 4096) in 4096/turn
void wm_orient_z(FwMatrix *m);                       // 0x42d360: rebuild X/Y around the Z column
void wm_rotate_x(FwMatrix *m, int32_t a);            // 0x431a30: m = m * Rx(a)

// ---------------------------------------------------------------- projectiles (weapons_projectile.c)
// Create parameters (the lander's LanderShotParams layout is the one every creator fills)
typedef LanderShotParams proj_params_t;

ObjId projectile_create(const proj_params_t *p);    // Objects_Create(2, {-1, ?, p}) -> 0x452990
void projectile_register(void);
void projectile_reset(void);

// ---------------------------------------------------------------- explosions (weapons_explosion.c)
// Objects_Create(9, {-1, ?, {kind, size, matrix, source}}): at where->t; the debris of a big blast fly
// out in where's frame
void explosion_create(int kind, int size, const FwMatrix *where, ObjId source);
void explosion_register(void);
void explosion_reset(void);

// ---------------------------------------------------------------- turrets (weapons_turret.c)
void turret_register(void);
void turret_reset(void);                             // TurretTypes_Reset 0x4541d0

// ---------------------------------------------------------------- shared (weapons.c / weapons_fx.c)
extern bool weapons_debug;                           // LANDER_DEBUG: trace shots, hits and damage
void weapons_destroy_core(ObjId id, int32_t size);   // Object_Destroyed 0x459190
int32_t weapons_destroy_step(ObjId id);              // 0x449960: model destroy step (-1 -> 1)
int16_t weapons_object_instance(ObjId id);           // 0x449770: render item of a record object

// particle effects of the effects module (eng_particle_spawn), sparks 0x454ef0
void fx_sparks(const int32_t pos[3], const int32_t vel[3], int n, int life);
// lander bodies in the projectile's segment query (Mob_QueryBox includes landers through their
// model instance; objects.c's port skips records without a mission model)
int fx_segment_hit(const int32_t *p0, const int32_t *p1, int flags, phys_hit_t *hit, ObjId exclude);
void fx_reset(void);

// drawing (camera-facing quads in world space; texture = a global texture index)
const texture_t *fx_global_texture(int index);
void fx_draw_begin(void);
void fx_draw_end(void);
void fx_sprite(const int32_t pos[3], float half, float angle, uint32_t rgb, const texture_t *tex,
	float u0, float v0, float u1, float v1);
void fx_glow(const int32_t pos[3], uint32_t rgb, float size4096, bool rotate); // 0x44a330 / 0x44a4a0
void fx_streak(const int32_t head[3], const int32_t tail[3], float half_head, float half_tail,
	uint32_t rgb_head, uint32_t rgb_tail);
float fx_view_depth(const int32_t pos[3]);           // camera-space z (0 = behind)

#endif
