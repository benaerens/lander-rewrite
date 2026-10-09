// Visual effects: particles (0x454ae0 and the effect table 0x499150), particle emitters (13) with the
// FwParticles systems of 0x498a08, AmbientFX (21: lightning, rain, snow, cloud layer, stars, sky
// models), Segmented (23), ShortFX (27), the sky gradient, the lander's engine flame / attitude jets /
// tractor beam / headlight glow / shadow (the eng_fx_* / eng_shadow_* adapters of lander.h), texture
// animation (TexAnimPlay), bump mapping and the HUD's 3D attitude models. Spec: re/specs/effects.md.
#ifndef EFFECTS_H
#define EFFECTS_H

#include "objtypes.h"

void effects_register_types(void);
void effects_reset(void);              // per mission
// Per frame, after level_begin_frame (view set, frame cleared) and before level_draw_world: the sky
// gradient and the sky layer (stars, sky models), the camera of this frame for the other effects.
void effects_begin_frame(uint32_t now);
void effects_render(uint32_t now);     // per frame, after the world and the objects are drawn
// the reflection pass (render_reflect.c): the effects camera follows the mirrored view in between;
// end draws the always-drawn group and flushes the pass's translucent effects
void effects_reflection_begin(uint32_t now);
void effects_reflection_end(uint32_t now);

// FUN_004569d0: the additive cone of a spot light (lander headlight, LightObject spots): node = the
// light's node, pos = the light's offset in it (or NULL), glow = eng_headlight_glow(level, cone),
// level = intensity >> 4, rgb = grey 0x00BBGGRR
void effects_light_cone(coord_t *node, const int32_t pos[3], int16_t glow, int32_t level, uint32_t rgb);

// ---------------------------------------------------------------- for the other streams
// Particles (eng_particle_spawn / eng_particle_set_velocity are declared in lander.h): byte +0x17 of a
// spawned particle (sparks: 1 other colour, 2 gravity, 4 slow drag), and FUN_00454ef0: n sparks at pos
// with velocity v +- 32 per axis (s8 world units per tick) and those flags
void eng_particle_set_flags(void *particle, uint8_t flags);
void fx_spark_burst(const int32_t pos[3], const int32_t v[3], int n, uint8_t flags);

// FwParticles systems (0x430670-0x4325e0): 128 systems sharing 2048 particles. Explosions (type 9) use
// their own tables (0x498d78 ground, 0x498d7c air) of these 0x50-byte records.
typedef struct fx_psys_def {
	int16_t tex;           // +0x00 gGlobalTextures entry (4 x 2 frames of 32 pixels)
	uint8_t texfile;       // +0x02 (set at registration in the original)
	uint8_t count;         // +0x03 particles
	int16_t gravity;       // +0x04 added to the y velocity every tick
	int16_t size;          // +0x06 sprite size (world units)
	uint32_t colour;       // +0x08 D3D diffuse 0x00RRGGBB (additive)
	int16_t life0, life1;  // +0x0c lifetime range (ticks)
	int32_t dir[4];        // +0x10 velocity direction (flag 8), randomised by +-0x1000 per axis
	int32_t ext[4];        // +0x20 spawn box half extents (local frame)
	int16_t speed, pad32;  // +0x30
	uint32_t flags;        // +0x34 3 drawn, 4 respawn, 8 move, 0x10 jitter, 0x80 random first life, 0x100 drag
	void (*done)(int sys); // +0x38 called when the last particle died (no respawn flag)
	uint16_t life_range;   // +0x3c set by fx_psys_register: life1 - life0
	int16_t frame_k;       // +0x3e set by fx_psys_register: 0x800 / life1
} fx_psys_def_t;

void fx_psys_register(fx_psys_def_t *def);            // FwParticlesRegisterDef 0x4325e0 (once per def)
int fx_psys_create(fx_psys_def_t *def, void *user);   // FUN_00430930: -1 when nothing is free
void *fx_psys_user(int sys);                          // the system's +0xc (the explosion's task)
void fx_psys_free(int sys);                           // FUN_00430aa0
void fx_psys_update(int sys);                         // FUN_00430b00, every 40 ms tick
// Particles_Draw 0x4321a0 this frame: particles relative to world, clipped to zone (< -1: none);
// call from a render callback (the frame's effects are drawn by effects_render)
void fx_psys_draw(int sys, const FwMatrix *world, int zone);
// FUN_0044a4a0: the additive glow (global texture 5) at a screen point and camera depth; size = scale
// of a 32-pixel sprite (4.12 x 2), colour 0x00BBGGRR. effects_project = FUN_00431d20.
void effects_glow(int32_t sx, int32_t sy, int32_t z, uint32_t bgr, int32_t size);
// FUN_0044a330: the same glow with two lens-flare streaks first (point LightObjects)
void effects_glow_flares(int32_t sx, int32_t sy, int32_t z, uint32_t bgr, int32_t size);
bool effects_project(const int32_t world[3], int32_t *sx, int32_t *sy, int32_t *z);

// ShortFX (type 27): the SHELL.PCP shock wave of a big explosion (Explosion_Create 0x44e610 creates it
// for size > 0xf0 at full explosion quality); also Objects_Create(OT_SHORTFX, args with extra = pos)
void effects_shockwave(const int32_t pos[3]);

void TexAnimStop(int32_t texture);     // export 0x444390

// HUD 3D attitude model (hud_host.model): model = misc\objects.ppm entry, rot = rows of the model ->
// camera rotation (4.12), pos = camera-space position, wire = colour (0 = lit model); screen_dx = 2D units
// the model is moved to the right on the screen (drawn with the projection's centre moved, so it looks as
// in the middle of a 640-wide screen: the HUD's right-hand group on a wider frame)
void effects_hud_model(int model, const int16_t rot[3][3], const int32_t pos[3], uint32_t wire, int screen_dx);

#endif
