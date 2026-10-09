// Internal interface between the effects modules (effects*.c).
#ifndef EFFECTS_INT_H
#define EFFECTS_INT_H

#include "effects.h"
#include "effects_draw.h"
#include "camera.h"

// effects_particles.c: the particle pool and its task
void fx_particles_reset(void);
void fx_particles_start(void);
void fx_particles_draw(void);
void fx_spark_burst(const int32_t pos[3], const int32_t v[3], int n, uint8_t flags); // FUN_00454ef0

// effects_emitter.c: FwParticles systems, ParticleEmitter (13), the lightning bolt
void fx_emitter_register(void);
void fx_emitter_reset(void);
void fx_emitter_draw(void);
// FUN_0040b7e0: a jagged bolt from a to b (camera-frame points of x) seeded by seed; age in ms
int32_t fx_bolt(const fx_xform_t *x, vec3_t a, vec3_t b, uint32_t seed, int32_t age, uint32_t jitter);
int fx_record_zone(const map_rec_t *r);

// effects_lander.c: flame, jets, beam, headlight glow, shadow
void fx_lander_reset(void);

// effects_sky.c: gradient, AmbientFX (21)
void fx_sky_register(void);
void fx_sky_reset(void);
void fx_sky_draw_background(void);   // gradient, stars, sky models (before the world)
void fx_sky_draw(void);              // rain, snow, lightning, cloud layer (after the objects)

// effects_misc.c: ShortFX (27), Segmented (23), texture animation
void fx_misc_register(void);
void fx_misc_reset(void);
void fx_misc_draw(void);
void fx_texanim_update(uint32_t now);

// effects_hud.c
void fx_hud_reset(void);

#endif
