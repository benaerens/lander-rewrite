// Drawing core of the effects. The original builds its effects as screen-space D3D TLVERTEX
// primitives from PSX-style camera coordinates (x right, y down, z forward, sx = cx + x*H/z,
// sy = cy + y*H/z, rhw = 10/z) and sorts the translucent ones by depth (OT_InsertDepthSorted, flushed
// after the opaque scene). Here every such point is turned back into a world point at the same depth,
// so the effects go through render_tris with the scene's depth buffer, and the translucent primitives
// are kept in a list that effects_render sorts back to front and draws after the objects.
#ifndef EFFECTS_DRAW_H
#define EFFECTS_DRAW_H

#include "engine.h"

// ---------------------------------------------------------------- camera of this frame
typedef struct {
	mat4_t view, inv;      // world -> renderer view space (x right, y up, z forward) and back
	vec3_t eye;            // camera position (DAT_004e9198)
	// Screen values are in 2D units (render_set_2d_scale: the frame is 480 of them high), so the effects
	// keep the original's pixel sizes at any render height. Widescreen (Hor+): w is the frame's width,
	// h the original's 4:3 projection constant, hw its half width (DAT_004985e0 at 640x480), cx the
	// frame's centre (the two are one value in the original).
	float h, cx, cy;       // projection constant and screen centre (DAT_004985fc, 0x4985e0/e4)
	float hw;              // the 4:3 half width that h is made from (FwSetScreenAngle)
	int w, ht;             // the frame (gWindowWidth / gWindowHeight in the original)
	uint32_t now;          // render time (gNow)
	rgba_t fog;            // scene fog colour (the ambient/backdrop colour of this frame)
	float fog_start, fog_end;
	bool valid;
} fx_cam_t;

extern fx_cam_t fxc;
extern rgba_t fx_flash;          // the exterior colour handed to level_exterior_override
extern bool fx_flash_pending;
extern bool fx_mirror;           // the reflection pass is being drawn (fxc is the mirror camera)

void fx_cam_capture(uint32_t now);                  // after the view is set (level_begin_frame)
// The camera view of an earlier time (the original asks the camera task for t = gNow - 40 to draw
// streaks): from a short history of the views of the rendered frames, interpolated.
mat4_t fx_view_at(uint32_t t);

static inline vec3_t fx_to_cam(vec3_t w) {          // world -> PSX camera space
	vec4_t v = mat4_mul_vec4(fxc.view, (vec4_t){w.x, w.y, w.z, 1});
	return vec3(v.x, -v.y, v.z);
}
static inline vec3_t fx_from_cam(vec3_t c) {        // PSX camera space -> world
	return mat4_mul_point(fxc.inv, vec3(c.x, -c.y, c.z));
}
// screen point (original pixels) at camera depth z -> world point
static inline vec3_t fx_unproject(float sx, float sy, float z) {
	return fx_from_cam(vec3((sx - fxc.cx) * z / fxc.h, (sy - fxc.cy) * z / fxc.h, z));
}

// A local frame as the GTE sees it after SetRotMatrix/SetTransMatrix: local -> PSX camera space.
typedef struct { float m[3][3]; float t[3]; } fx_xform_t;

void fx_xform_world(const FwMatrix *world, fx_xform_t *out);   // FUN_00431580(node): world . view
void fx_xform_rot_only(const FwMatrix *rot, const fx_xform_t *trans_from, fx_xform_t *out); // new rotation, old translation
vec3_t fx_rt(const fx_xform_t *x, vec3_t local);               // RotTrans
// RotTransPers 0x435a20: screen x/y and camera z; false when z <= 0 or off the 16-bit screen range
bool fx_rtp(const fx_xform_t *x, vec3_t local, float *sx, float *sy, float *z);

// ---------------------------------------------------------------- primitives
enum { FX_OPAQUE = 0, FX_ALPHA = 1, FX_ADD = 2, FX_MODULATE = 3 }; // gBlendSrc/Dst modes (2 = ONE/ONE)
// the PCP renderer's translucent triangles in the same table (Translucent_AddPoly 0x4416e0, FUN_00441900):
// blend mode 1 / 2 with z writes on, the scene's fog colour, back faces culled
enum { FX_PCP_ALPHA = 4, FX_PCP_ADD = 5 };
// the cloud layer's texture repeats (D3DRENDERSTATE_TEXTUREADDRESS WRAP around its draw, 0x44b5d0); every
// other primitive is drawn with the default CLAMP. Taken by fx_queue when set.
extern bool fx_wrap;

typedef struct {
	vec3_t pos;            // world
	float u, v;            // normalised
	uint32_t c;            // D3D colour 0xAARRGGBB
} fxv_t;

// Triangles (n multiple of 3). fog: apply the scene's fog (additive primitives fade to black like
// the original, which sets the fog colour to 0 for blend mode 2). depth sorts the translucent list;
// FX_LAYER_3D = OT_Insert into the 3D layer (drawn before the depth-sorted ones, in submission order).
#define FX_LAYER_3D 1e30f
void fx_queue(const fxv_t *v, int n, const texture_t *tex, int blend, bool fog, float depth);
void fx_draw_now(const fxv_t *v, int n, const texture_t *tex, int blend, bool fog);
// Quad / fan helpers (vertex order of the strip or fan)
void fx_queue_fan(const fxv_t *v, int n, const texture_t *tex, int blend, bool fog, float depth);
void fx_queue_strip(const fxv_t *v, const uint16_t *idx, int n, const texture_t *tex, int blend, bool fog, float depth);
void fx_queue_line(vec3_t a, vec3_t b, uint32_t ca, uint32_t cb, int blend, bool fog, float depth);
void fx_flush(void);                                // draw the queued translucent primitives
void fx_frame_reset(void);
// Zones_GetClipSet: clip the following primitives to the screen rectangle the zone is seen through
// (zone < -1 = no clip); false when the zone is not visible this frame
bool fx_clip_zone(int zone);
void fx_clip_reset(void);

// FwSpriteDrawSectionFX 0x43d180: a screen-aligned sprite of a texture section at camera depth z.
// flags: 1 colour (0x00BBGGRR), 4 scaled around the point (half size = section * scale >> 13),
// 8 rotated by angle; without 4 the section is drawn at texel size from (x, y).
void fx_sprite_section(float x, float y, float z, const texture_t *tex, int u0, int v0, int u1, int v1,
	unsigned flags, uint32_t bgr, int32_t scale, int32_t angle, int blend, bool fog);

// ---------------------------------------------------------------- textures
const texture_t *fx_global_tex(int index);          // gGlobalTextures entry, frame 0
const texture_t *fx_world_tex(int index);           // gEnvironmentTexFile entry, frame 0
// ONE/ONE ignores the texture alpha: an additive draw uses a copy with alpha 255
const texture_t *fx_tex_opaque_alpha(const texture_t *t);
void fx_textures_reset(void);

// ---------------------------------------------------------------- small helpers of the original
uint32_t fx_colour_scale(uint32_t c, int32_t k);    // FUN_0044a5b0: every byte * k >> 12, clamped
uint32_t fx_colour_mul(uint32_t a, uint32_t b);     // FUN_0044a720: bytewise a * b >> 8
uint32_t fx_colour_pulse(uint32_t c, uint32_t t, uint32_t period); // FUN_0044a570
uint32_t fx_bgr_to_argb(uint32_t bgr);              // 0x00BBGGRR -> 0xFFRRGGBB
// FUN_0044a5f0: hue cycling over period with saturation sat and value val (swap = DAT_005533dc)
uint32_t fx_hue_colour(uint32_t t, uint32_t period, int32_t sat, uint32_t val, bool swap);
int32_t fx_rand(void);                              // MSVC rand(): the visual randomness of the effects
void fx_srand(uint32_t seed);

#endif
