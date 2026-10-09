// The HUD's 3D attitude models (render items of Lander_CreateHud 0x420770, placed each frame by the
// lander's render callback 0x4237d9): misc\ATTITUDE.PCP drawn solid and as an additive wireframe
// (draw function 0x4201c0, colour 0x40), misc\FRAME.PCP as a wireframe (0x420190, colour 0x4000),
// all children of the camera node (camera-space placement). Wireframes are the models' unique triangle
// edges as lines (Pcp_DrawInstanceWire 0x46c5d0 -> PcpRender_DrawEdges 0x4704b0), ONE/ONE blended.
#include <stdlib.h>
#include <string.h>

#include "effects_int.h"
#include "hud.h"

typedef struct {
	model_t *model;
	int nedges;
	uint16_t (*edges)[2];   // vertex index pairs (the runtime edge list of FUN_0046f540)
	bool tried;
} hud_model_t;

static hud_model_t models[3];
static const char *const model_files[3] = {"MISC\\ATTITUDE.PCP", "MISC\\FRAME.PCP", "MISC\\COMPASS.PCP"};

static void build_edges(hud_model_t *h) {
	const model_t *m = h->model;
	h->edges = malloc(sizeof(*h->edges) * (m->num_tris * 3 + 1));
	h->nedges = 0;
	for (int i = 0; i < m->num_tris; i++) {
		const pcp_tri_t *t = &m->tris[i];
		if (t->flags & TRI_INVISIBLE) continue;
		for (int k = 0; k < 3; k++) {
			int a = m->wedges[t->w[k] < m->num_wedges ? t->w[k] : 0].vertex;
			int b = m->wedges[t->w[(k + 1) % 3] < m->num_wedges ? t->w[(k + 1) % 3] : 0].vertex;
			if (a == b) continue;
			uint16_t lo = (uint16_t)min(a, b), hi = (uint16_t)max(a, b);
			bool dup = false;
			for (int e = 0; e < h->nedges && !dup; e++) dup = h->edges[e][0] == lo && h->edges[e][1] == hi;
			if (!dup) {
				h->edges[h->nedges][0] = lo;
				h->edges[h->nedges++][1] = hi;
			}
		}
	}
}

static hud_model_t *get_model(int i) {
	if (i < 0 || i >= 3) return NULL;
	hud_model_t *h = &models[i];
	if (!h->tried) {
		h->tried = true;
		h->model = model_load_pcp(model_files[i]);
		if (h->model) build_edges(h);
	}
	return h->model ? h : NULL;
}

void fx_hud_reset(void) {
	for (int i = 0; i < 3; i++) {
		model_free(models[i].model);
		free(models[i].edges);
	}
	memset(models, 0, sizeof(models));
}

// [A] widescreen: the original places these in front of its 640x480 camera (FwSetScreenAngle(900)), next to
// the Roll / Pitch scales that hang from the screen's bottom-right corner. On a wider frame the models are
// drawn with the projection centre (and the effects camera's cx) moved right by screen_dx 2D units, so
// they keep their place and look in that group.
static mat4_t saved_proj;
static float saved_cx;

static void shift_begin(int dx) {
	saved_proj = render_projection();
	saved_cx = fxc.cx;
	if (!dx) return;
	// clip x += tx * w moves the picture by tx * width / 2 pixels
	float tx = 2.0f * (float)dx * render_2d_scale() / (float)render_width();
	render_set_projection_matrix(mat4_mul(mat4_translate(vec3(tx, 0, 0)), saved_proj));
	fxc.cx += (float)dx;
}

static void shift_end(int dx) {
	if (!dx) return;
	render_set_projection_matrix(saved_proj);
	fxc.cx = saved_cx;
}

void effects_hud_model(int model, const int16_t rot[3][3], const int32_t pos[3], uint32_t wire, int screen_dx) {
	hud_model_t *h = get_model(model);
	if (!h || !fxc.valid) return;
	// model -> PSX camera space (rot rows, pos) -> renderer view space (y flipped) -> world
	mat4_t c = mat4_identity();
	for (int r = 0; r < 3; r++) {
		for (int k = 0; k < 3; k++) c.m[k * 4 + r] = rot[r][k] / 4096.0f;
		c.m[12 + r] = (float)pos[r];
	}
	mat4_t t = mat4_mul(fxc.inv, mat4_mul(mat4_scale(vec3(1, -1, 1)), c));
	shift_begin(screen_dx);
	if (!wire) { // the solid model, lit by the scene's ambient and directional lights
		model_draw(h->model, t, G.global_tex, &G.base_light);
		render_set_model(mat4_identity());
		shift_end(screen_dx);
		return;
	}
	uint32_t argb = fx_bgr_to_argb(wire);
	const model_t *m = h->model;
	fxv_t *v = malloc(sizeof(fxv_t) * 6 * (h->nedges + 1));
	int n = 0;
	for (int e = 0; e < h->nedges; e++) {
		int a = h->edges[e][0], b = h->edges[e][1];
		if (a >= m->num_vertices || b >= m->num_vertices) continue;
		vec3_t pa = mat4_mul_point(t, m->vertices[a]), pb = mat4_mul_point(t, m->vertices[b]);
		vec3_t ca = fx_to_cam(pa), cb = fx_to_cam(pb);
		if (ca.z <= 10 || cb.z <= 10) continue;
		float ax = fxc.cx + ca.x * fxc.h / ca.z, ay = fxc.cy + ca.y * fxc.h / ca.z;
		float bx = fxc.cx + cb.x * fxc.h / cb.z, by = fxc.cy + cb.y * fxc.h / cb.z;
		float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy), nx = 0.5f, ny = 0;
		if (l > 0.001f) { nx = -dy / l * 0.5f; ny = dx / l * 0.5f; }
		fxv_t q[4] = {
			{fx_unproject(ax - nx, ay - ny, ca.z), 0, 0, argb}, {fx_unproject(ax + nx, ay + ny, ca.z), 0, 0, argb},
			{fx_unproject(bx + nx, by + ny, cb.z), 0, 0, argb}, {fx_unproject(bx - nx, by - ny, cb.z), 0, 0, argb},
		};
		v[n++] = q[0]; v[n++] = q[1]; v[n++] = q[2];
		v[n++] = q[0]; v[n++] = q[2]; v[n++] = q[3];
	}
	if (n) fx_draw_now(v, n, NULL, FX_ADD, true);
	free(v);
	shift_end(screen_dx);
}
