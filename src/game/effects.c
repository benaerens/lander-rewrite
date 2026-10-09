// Visual effects: frame set-up, registration and the per-frame drawing order. The modules:
// effects_draw.c (camera, depth-sorted translucent primitives, sprites), effects_particles.c (the
// particle pool), effects_emitter.c (FwParticles systems, ParticleEmitter, lightning bolts),
// effects_lander.c (engine flame, attitude jets, tractor beam, headlight cone), effects_shadow.c (the
// lander's projected shadow), effects_sky.c (sky gradient, AmbientFX), effects_misc.c (ShortFX,
// Segmented, texture animation), effects_hud.c (the HUD's 3D attitude models).
#include <stdlib.h>

#include "effects_int.h"
#include "lights.h"

void effects_register_types(void) {
	fx_emitter_register();
	fx_sky_register();
	fx_misc_register();
}

void effects_reset(void) {
	fx_frame_reset();
	fx_textures_reset();
	fx_particles_reset();
	fx_emitter_reset();
	fx_lander_reset();
	fx_sky_reset();
	fx_misc_reset();
	fx_hud_reset();
	fxc.valid = false;
	fx_flash_pending = false;
	level_exterior_override = NULL;
}

// The sky hook (Environment_ApplyAmbientFog's gradient and the sky layer OT+8): after the view is set
// and the frame cleared, before the world is drawn.
void effects_begin_frame(uint32_t now) {
	fx_cam_capture(now);
	fx_particles_start();
	fx_texanim_update(now);   // PrePresent_TexAnim 0x413910 (once per frame)
	fx_sky_draw_background();
}

// LANDER_FXTEST=1 (debugging aid): every 200 ms a row of particles 0-7 and a spark burst in front of
// the camera, to look at the particle drawing without having to fly to a place that makes them.
// LANDER_FXTEST=2: instead, one ParticleEmitter of each effect (-2, -1, 0..4) in front of the camera.
static void fx_test_spawn(uint32_t now) {
	static uint32_t last;
	static int enabled = -1;
	if (enabled < 0) enabled = getenv("LANDER_FXTEST") ? atoi(getenv("LANDER_FXTEST")) : 0;
	if (enabled == 2 && now >= 400) {
		static map_rec_t recs[7];
		static int32_t words[7][2];
		enabled = 3;
		for (int i = 0; i < 7; i++) {
			vec3_t p = fx_from_cam(vec3((i - 3) * 500.0f, 300, 2500));
			recs[i] = (map_rec_t){0};
			recs[i].pos = (ivec3_t){(int32_t)p.x, (int32_t)p.y, (int32_t)p.z};
			recs[i].zone = -3;
			recs[i].model = -1;
			recs[i].type = OT_PARTICLEEMITTER;
			words[i][0] = ((i - 2) & 0xff) | (i == 1 ? 0x103000 : 0); // effect; -1: a bolt to (0x300, 0x100, 0)
			obj_args_t a = {(ObjId)(0x020d0000 | i), -1, &recs[i], words[i], NULL, NULL};
			Objects_Create(OT_PARTICLEEMITTER, &a);
		}
	}
	if (enabled != 1 || now - last < 200) return;
	last = now;
	vec3_t c = fx_from_cam(vec3(0, 0, 1800));
	for (int i = 0; i < 8; i++) {
		vec3_t p = fx_from_cam(vec3((i - 3.5f) * 220, 150, 1800));
		int32_t pos[3] = {(int32_t)p.x, (int32_t)p.y, (int32_t)p.z};
		gParticleColour = 0xf9ede4u + 0x61216u;
		eng_particle_spawn(i, pos, i == 4 ? 0xc8 : i == 6 ? 0x0810 : 0);
	}
	int32_t pos[3] = {(int32_t)c.x, (int32_t)c.y - 200, (int32_t)c.z}, v[3] = {0, 0x40, 0};
	fx_spark_burst(pos, v, 6, 2);
	if (((now / 200) & 7) == 0) effects_shockwave(pos);
}

// The Venus reflection pass (Mission_Run 0x4146c0, render_reflect.c): the effects camera becomes the
// mirror camera (the mirrored view must already be set) until effects_reflection_end, which draws the
// always-drawn group again (DrawGroup_Queue(0x491be0) in the pass: particles, Segmented), flushes the
// pass's own translucency table (gSortTableMirror, flushed at the head of OT+0x18) and gives the
// effects the main camera back.
static fx_cam_t main_cam;

void effects_reflection_begin(uint32_t now) {
	main_cam = fxc;
	fx_mirror = true;
	fx_cam_capture(now);
}

void effects_reflection_end(uint32_t now) {
	(void)now;
	fx_misc_draw();
	fx_particles_draw();
	fx_flush();
	fx_mirror = false;
	fxc = main_cam;
}

// After the world and the objects (their render callbacks queued their effects): the 3D-layer effects
// in submission order, then the depth-sorted translucent ones back to front (the translucency flush).
void effects_render(uint32_t now) {
	if (!fxc.valid || fxc.now != now) {
		fx_cam_capture(now);
	}
	fx_test_spawn(now);
	fx_sky_draw();
	fx_misc_draw();
	fx_particles_draw();
	fx_flush();
}
