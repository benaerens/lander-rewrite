// Software renderer: the whole game draws through this, so every platform only
// needs to show a 32-bit pixel buffer (0xAARRGGBB).
#ifndef RENDER_H
#define RENDER_H

#include "types.h"

typedef enum {
	BLEND_OPAQUE,     // write texel * colour
	BLEND_ALPHA_TEST, // like opaque, but texels with alpha < 128 are skipped
	BLEND_ALPHA,      // src * a + dst * (1 - a)
	BLEND_ADD,        // dst + src * a (saturating)
	BLEND_ONE_ONE,    // dst + src (saturating), alpha ignored (D3DBLEND_ONE / D3DBLEND_ONE)
} blend_t;

typedef enum { CULL_NONE, CULL_BACK, CULL_FRONT } cull_t;

typedef struct texture_t {
	int width, height;
	uint32_t *pixels; // 0xAARRGGBB
	// mipmaps (the original's square textures with "Mip Mapping" on, the default): set mip_ok and the
	// renderer builds the chain (2x2 box filter as 0x43a910 / 0x43aed0) the first time it is drawn in 3-D;
	// render_texture_free_mips frees it. Zero-initialised textures have none.
	bool mip_ok;
	int mip_levels;            // levels after level 0
	uint32_t *mip_pixels;      // the levels back to back, each half the size of the previous one
	int mip_offset[10];        // level k (1..mip_levels) starts at mip_pixels + mip_offset[k]
} texture_t;

void render_texture_free_mips(texture_t *t);

typedef struct {
	vec3_t pos;
	float u, v; // normalized texture coordinates, repeat wrapping
	rgba_t color; // modulates the texel (or is the colour when untextured)
} vertex_t;

void render_init(int width, int height);
void render_cleanup(void);
// A new frame size (the game: 640x480 for the front end, the display's aspect in missions). What was
// drawn is finished first; the new buffers' contents are undefined until render_frame_begin.
void render_resize(int width, int height);
int render_width(void);
int render_height(void);
uint32_t *render_pixels(void);

// The 2D layers (HUD, UI windows, fonts, the pointer) are laid out in units of a 480-high screen: a
// render_quad_2d rectangle and a render_set_scissor_2d rectangle are drawn at ((x + ox) * scale,
// (y + oy) * scale) pixels. Default scale 1, no offset. render_2d_width/height = the frame in those units.
void render_set_2d_scale(float scale);
void render_set_2d_offset(float ox, float oy);
float render_2d_scale(void);
int render_2d_width(void);
int render_2d_height(void);
void render_set_scissor_2d(int x0, int y0, int x1, int y1);

void render_frame_begin(rgba_t clear);

void render_set_projection(float fov_y, float near, float far);
void render_set_projection_matrix(mat4_t projection);   // any projection (e.g. an off-centre one)
void render_set_view(mat4_t view);
mat4_t render_view(void);
bool render_view_mirrored(void); // the view set is a reflection (the Venus pass)
mat4_t render_projection(void);
// Clip drawing to a screen rectangle [x0, x1) x [y0, y1) (portal visibility).
void render_set_scissor(int x0, int y0, int x1, int y1);
void render_reset_scissor(void);
void render_set_model(mat4_t model);
void render_set_fog(bool enabled, rgba_t color, float start, float end);
void render_set_depth(bool test, bool write);
void render_set_cull(cull_t cull);
void render_set_filter(bool bilinear);
// D3DRENDERSTATE_TEXTUREADDRESS: the game draws with CLAMP (D3D_InitMatricesAndStates 0x43bc40);
// only the cloud layer switches to WRAP around its draw. Default false = clamp.
void render_set_wrap(bool wrap);

// Triangles in model space; num_vertices is a multiple of 3. Front faces are
// clockwise on screen (Direct3D convention).
void render_tris(const vertex_t *v, int num_vertices, const texture_t *tex, blend_t blend);

// Screen-space rectangle (2D units, see render_set_2d_scale), e.g. for HUD, sprites and fonts.
void render_quad_2d(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
	rgba_t color, const texture_t *tex, blend_t blend);

// Deferred drawing: between begin and end, render_tris / render_quad_2d are recorded with the state
// they were called with (matrices, fog, depth, cull, scissor) instead of drawn; render_replay draws
// them later in the same order. The original builds its frame in ordering tables that run in a
// different order than they are filled (the Venus reflection pass is built after the main scene but
// drawn before it); this gives the rewrite the same freedom.
void render_record_begin(void);
void render_record_end(void);
bool render_recording(void);
void render_replay(void);

// Deferred frame: while on, everything drawn is stored and drawn (by several threads, in bands of rows,
// with exactly the result of drawing it at once) when it is turned off or render_pixels is called.
// Textures used must stay valid until then. LANDER_RENDER_THREADS=n sets the thread count (1 = none).
void render_defer(bool on);

#endif
