// The original's UI system: window manager core (UI_* 0x4839b0-0x484220), windows (WindowCreate
// 0x4284b0 and the Window* exports), window frames / titles / scroll bars / panes (0x45d4b0-0x462690)
// and the compiled-HTML text pages (TextDoc_* 0x45c460-0x45d4b0, small-formats.md §3).
// The controls (Control_Create* and their drawing) are in src/fe/fe_controls.c.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "font.h"
#include "camera.h"
#include "effects_draw.h"
#include "../fs.h"
#include "../platform.h"

// ================================================================ UI input (Input_* 0x429660..)

static struct { uint32_t code; bool down, prev, latched; } keys[] = {
	{UIK_UP, 0, 0, 0}, {UIK_DOWN, 0, 0, 0}, {UIK_LEFT, 0, 0, 0}, {UIK_RIGHT, 0, 0, 0}, {UIK_SELECT, 0, 0, 0},
	{UIK_BACK, 0, 0, 0}, {UIK_ESCAPE, 0, 0, 0}, {UIK_P, 0, 0, 0}, {UIK_MOUSE0, 0, 0, 0}, {UIK_MOUSE1, 0, 0, 0},
};

static button_t ui_button(uint32_t code) {
	switch (code) {
		case UIK_UP: return BTN_KEY(KEY_UP);
		case UIK_DOWN: return BTN_KEY(KEY_DOWN);
		case UIK_LEFT: return BTN_KEY(KEY_LEFT);
		case UIK_RIGHT: return BTN_KEY(KEY_RIGHT);
		case UIK_SELECT: return BTN_KEY(KEY_RETURN);
		case UIK_BACK: return BTN_KEY(KEY_BACKSPACE);
		case UIK_ESCAPE: return BTN_KEY(KEY_ESCAPE);
		case UIK_P: return BTN_KEY('p');
		case UIK_MOUSE0: return BTN_MOUSE_LEFT;
		case UIK_MOUSE1: return BTN_MOUSE_RIGHT;
		default: return BTN_NONE;
	}
}

void ui_input_frame(void) { // Input_Poll: latch the new key edges (kept until consumed or the next poll)
	for (size_t i = 0; i < len(keys); i++) {
		bool d = input_down(ui_button(keys[i].code));
		keys[i].latched = d && !keys[i].prev;
		keys[i].prev = d;
		keys[i].down = d;
	}
}

bool ui_key_down(uint32_t code) {
	return input_down(ui_button(code));
}

bool ui_key_pressed(uint32_t code) {
	for (size_t i = 0; i < len(keys); i++) {
		if (keys[i].code == code) return keys[i].latched;
	}
	return false;
}

void ui_key_consume(uint32_t code) {
	for (size_t i = 0; i < len(keys); i++) {
		if (keys[i].code == code) keys[i].latched = false;
	}
}

// the pointer in 2D units (the platform gives frame pixels)
int ui_mouse_x(void) { return (int)floorf((float)input_mouse_x() / render_2d_scale()); }
int ui_mouse_y(void) { return (int)floorf((float)input_mouse_y() / render_2d_scale()); }

// ================================================================ the UI's screen (widescreen)
// The windows are laid out on the original's gWindowWidth x gWindowHeight (640x480) screen. A wider
// frame (a mission on a widescreen display) shows that screen in its middle: the mission scripts'
// windows (text pages, objective lines), the pause / quit dialogs and the game-over message keep their
// 640x480 places and sizes there, while the HUD (hud.c) spreads to the frame's edges. The front end's
// frame is that screen itself (no offset).
void ui_area_offset(int *ox, int *oy) {
	*ox = (render_2d_width() - gWindowWidth) / 2;
	*oy = (render_2d_height() - gWindowHeight) / 2;
}

// ================================================================ drawing: layered primitive list

enum { PRIM_QUAD, PRIM_LINE, PRIM_TEXT };

typedef struct {
	uint8_t kind;
	int16_t layer;
	int seq;
	float x, y, w, h, u0, v0, u1, v1;   // quad; line: x,y -> w,h = end point
	const texture_t *tex;
	rgba_t colour;
	blend_t blend;
	int text, n, font;                  // text: offset into the arena
	int clip[4];                        // x0, y0, x1, y1 (scissor), clip[2] <= clip[0] = none
	bool xf;                            // drawn through a window transform: the projected corners below
	float qx[4], qy[4], qiz[4];         // quad TL, TR, BR, BL (line: 0, 1) on the screen, 1/z
} prim_t;

#define MAX_PRIMS 4096
static prim_t prims[MAX_PRIMS];
static int num_prims;
static char arena[65536];
static int arena_len;
static int cur_clip[4];

texfile_t *gGlobalTextures, *gStandardTextures, *gEnvironmentTexFile;
ppm_t *gUiModels;

void ui_draw_begin(void) {
	num_prims = 0;
	arena_len = 0;
	cur_clip[0] = cur_clip[2] = 0;
}

static prim_t *new_prim(int kind, int layer) {
	if (num_prims >= MAX_PRIMS) return NULL;
	prim_t *p = &prims[num_prims];
	memset(p, 0, sizeof(*p));
	p->kind = (uint8_t)kind;
	p->layer = (int16_t)layer;
	p->seq = num_prims++;
	memcpy(p->clip, cur_clip, sizeof(p->clip));
	return p;
}

static void ui_clip(int x0, int y0, int x1, int y1) {
	cur_clip[0] = x0; cur_clip[1] = y0; cur_clip[2] = x1; cur_clip[3] = y1;
}

// ================================================================ window transforms (0x45d4b0..0x45d760)
// A top-level window gets a record at open (FUN_0045d4b0) with three random spin angles; after
// Window_CreateTransform its node (a child of the camera) is placed every frame (FUN_0045d580) at
// z = H + 2 * rem (rem = ms left of the 800 ms fly-in) along the window centre's view ray, turned by
// Rz(rx*rem) Rx(ry*rem) Ry(rz*rem), so the window spins in from the distance while its centre stays put.
// Every 2D primitive (FwTileDraw, FwLineDraw, FwSpriteDraw*FX, fonts) queued while a transform is
// current goes through FUN_0043c410: (x - cx, y - cy, 0) * matrix, projected with H / z.

typedef struct {
	uint32_t start;            // [0] creation time (1 when gNow was 0), 0 = free
	uint32_t last;             // [1] gNow of the last placement
	bool coords;               // [3] / [4]: the nodes of Window_CreateTransform exist (freed at the end)
	int32_t rx, ry, rz;        // [5..7] -0x2000..0x1fff
	int32_t cx, cy;            // [8], [9] window centre relative to the screen centre
	cam_view_t outer;          // [4] the spinning node; [3] = its child at (-cx, -cy, 0)
} WinXform;

#define MAX_XFORMS 16                  // 0x55ff70..0x5601f0
static WinXform xforms[MAX_XFORMS];
static const WinXform *cur_xf;         // DAT_005415f0 the current sprite transform (FwSpriteSetTransform)
static float xf_m[9], xf_t[3];         // DAT_0058b8e0 its camera-space matrix, taken when it is set
static float xf_cx, xf_cy, xf_h;       // DAT_004985e0 / e4 screen centre, DAT_004985fc projection distance

// FwSetScreenAngle(900) of the 4:3 screen (2D units; a wider frame keeps the vertical view)
static int32_t proj_h(void) { return cam_projection_h(900, (render_2d_height() * 4 / 3) >> 1); }

static WinXform *xform_alloc(void) { // FUN_0045d4b0
	WinXform *r = NULL;
	for (int i = 0; i < MAX_XFORMS && !r; i++) {
		if (!xforms[i].start) r = &xforms[i];
	}
	if (!r) return NULL;    // (the original runs off the end of its pool)
	uint32_t now = Clock_Now();
	r->start = now ? now : 1;
	r->coords = false;
	r->rx = (fx_rand() & 0x3fff) - 0x2000;
	r->ry = (fx_rand() & 0x3fff) - 0x2000;
	r->rz = (fx_rand() & 0x3fff) - 0x2000;
	return r;
}

static void xform_free(WinXform *r) { // FUN_0045d520
	if (!r) return;
	r->start = 0;
	r->coords = false;
	if (cur_xf == r) cur_xf = NULL;
}

static void xform_place(WinXform *r) { // FUN_0045d580
	if (!r->coords) return;
	uint32_t now = Clock_Now();
	r->last = now;
	int32_t rem = (int32_t)(r->start - now) + 800;
	if (rem < 1) {
		r->coords = false;
		return;
	}
	int32_t h = proj_h();
	cam_view_identity(&r->outer);
	cam_view_rotate_z(&r->outer, r->rx * rem >> 12);
	cam_view_rotate_x(&r->outer, r->ry * rem >> 12);
	cam_view_rotate_y(&r->outer, r->rz * rem >> 12);
	int32_t z = h + rem * 2;
	r->outer.mat.t[2] = z;
	r->outer.mat.t[0] = r->cx * z / h;
	r->outer.mat.t[1] = r->cy * z / h;
}

static void sprite_set_transform(const WinXform *x) { // FwSpriteSetTransform 0x43c3e0
	// the matrix is only taken when the transform changes (as the original's DAT_005415f0 check)
	if (cur_xf == x) return;
	cur_xf = x;
	if (!x) return;
	// FUN_00431580 of the inner node: the camera's own camera-space matrix is the identity
	const cmatrix_t *m = &x->outer.mat;
	cvec3_t o = cmat_apply(m, (cvec3_t){-x->cx, -x->cy, 0});
	for (int i = 0; i < 9; i++) xf_m[i] = m->m[i / 3][i % 3] * (1.0f / 4096.0f);   // FUN_0043f350
	xf_t[0] = (float)(o.x + m->t[0]);
	xf_t[1] = (float)(o.y + m->t[1]);
	xf_t[2] = (float)(o.z + m->t[2]);
	xf_cx = (float)(render_2d_width() >> 1);
	xf_cy = (float)(render_2d_height() >> 1);
	xf_h = (float)proj_h();
}

static void xf_project(float x, float y, float *sx, float *sy, float *iz) { // FUN_0043c410 (one vertex)
	float lx = x - xf_cx, ly = y - xf_cy;
	float X = xf_m[0] * lx + xf_m[1] * ly + xf_t[0];
	float Y = xf_m[3] * lx + xf_m[4] * ly + xf_t[1];
	float Z = xf_m[6] * lx + xf_m[7] * ly + xf_t[2];
	if (Z < 1) Z = 1;
	float f = xf_h / Z;
	*sx = xf_cx + X * f;
	*sy = xf_cy + Y * f;
	*iz = 1.0f / Z;
}

static void prim_transform_rect(prim_t *p) { // the quad's corners TL, TR, BR, BL through the transform
	if (!cur_xf) return;
	float xs[4] = {p->x, p->x + p->w, p->x + p->w, p->x}, ys[4] = {p->y, p->y, p->y + p->h, p->y + p->h};
	for (int i = 0; i < 4; i++) xf_project(xs[i], ys[i], &p->qx[i], &p->qy[i], &p->qiz[i]);
	p->xf = true;
}

// ---------------------------------------------------------------- drawing the transformed primitives
// A transformed rectangle is a planar quad in camera space: 1/z, u/z and v/z are affine on the
// screen, so the whole quad is filled from one set of gradients (perspective-correct like D3D).

static inline uint32_t px_lerp(uint32_t a, uint32_t b, int f) { // f 0..256
	uint32_t rb = (((a & 0x00ff00ff) * (256 - f) + (b & 0x00ff00ff) * f) >> 8) & 0x00ff00ff;
	uint32_t ag = ((((a >> 8) & 0x00ff00ff) * (256 - f) + ((b >> 8) & 0x00ff00ff) * f)) & 0xff00ff00;
	return rb | ag;
}

static inline int wrapi(int i, int n) { i %= n; return i < 0 ? i + n : i; }

static uint32_t tex_bilinear(const texture_t *t, float u, float v) {
	float fx = u * t->width - 0.5f, fy = v * t->height - 0.5f;
	float x0f = floorf(fx), y0f = floorf(fy);
	int tx = (int)((fx - x0f) * 256), ty = (int)((fy - y0f) * 256);
	int x0 = wrapi((int)x0f, t->width), y0 = wrapi((int)y0f, t->height);
	int x1 = x0 + 1 == t->width ? 0 : x0 + 1, y1 = y0 + 1 == t->height ? 0 : y0 + 1;
	const uint32_t *p = t->pixels;
	return px_lerp(px_lerp(p[y0 * t->width + x0], p[y0 * t->width + x1], tx),
		px_lerp(p[y1 * t->width + x0], p[y1 * t->width + x1], tx), ty);
}

// the transformed primitives are drawn straight into the frame: their corners and clip in pixels
static void prim_bounds(const prim_t *p, int *x0, int *y0, int *x1, int *y1) {
	float s = render_2d_scale();
	*x0 = 0; *y0 = 0; *x1 = render_width(); *y1 = render_height();
	if (p->clip[2] > p->clip[0]) {
		*x0 = max(*x0, (int)floorf(p->clip[0] * s + 0.5f)); *y0 = max(*y0, (int)floorf(p->clip[1] * s + 0.5f));
		*x1 = min(*x1, (int)floorf(p->clip[2] * s + 0.5f)); *y1 = min(*y1, (int)floorf(p->clip[3] * s + 0.5f));
	}
}

static void xf_draw_quad(const prim_t *p) {
	float sc = render_2d_scale(), X[4], Y[4];
	for (int i = 0; i < 4; i++) {
		X[i] = p->qx[i] * sc;
		Y[i] = p->qy[i] * sc;
	}
	float area = 0;
	for (int i = 0; i < 4; i++) area += X[i] * Y[(i + 1) & 3] - X[(i + 1) & 3] * Y[i];
	if (fabsf(area) < 0.01f) return;
	float s = area > 0 ? 1.0f : -1.0f;
	// gradients of iz, u*iz, v*iz from the corners 0, 1, 3 (u runs along 0->1, v along 0->3)
	float us[4] = {p->u0, p->u1, p->u1, p->u0}, vs[4] = {p->v0, p->v0, p->v1, p->v1};
	float a[3][4];
	for (int i = 0; i < 4; i++) {
		a[0][i] = p->qiz[i];
		a[1][i] = us[i] * p->qiz[i];
		a[2][i] = vs[i] * p->qiz[i];
	}
	float x10 = X[1] - X[0], y10 = Y[1] - Y[0], x30 = X[3] - X[0], y30 = Y[3] - Y[0];
	float det = x10 * y30 - x30 * y10;
	if (fabsf(det) < 1e-6f) return;
	float dadx[3], dady[3];
	for (int k = 0; k < 3; k++) {
		float d1 = a[k][1] - a[k][0], d3 = a[k][3] - a[k][0];
		dadx[k] = (d1 * y30 - d3 * y10) / det;
		dady[k] = (d3 * x10 - d1 * x30) / det;
	}
	int bx0, by0, bx1, by1;
	prim_bounds(p, &bx0, &by0, &bx1, &by1);
	float ymin = min(min(Y[0], Y[1]), min(Y[2], Y[3])), ymax = max(max(Y[0], Y[1]), max(Y[2], Y[3]));
	int py0 = max(by0, (int)ceilf(ymin - 0.5f)), py1 = min(by1 - 1, (int)ceilf(ymax - 0.5f) - 1);
	uint32_t *fb = render_pixels();
	int W = render_width();
	const texture_t *t = p->tex;
	for (int py = py0; py <= py1; py++) {
		float yc = py + 0.5f, lo = -1e30f, hi = 1e30f;
		bool empty = false;
		for (int i = 0; i < 4 && !empty; i++) { // inside: s * cross(edge, point - start) >= 0
			int j = (i + 1) & 3;
			float ex = X[j] - X[i], ey = Y[j] - Y[i];
			float c1 = -s * ey, c0 = s * (ex * (yc - Y[i]) + ey * X[i]);
			if (c1 > 0) lo = max(lo, -c0 / c1);
			else if (c1 < 0) hi = min(hi, -c0 / c1);
			else if (c0 < 0) empty = true;
		}
		if (empty) continue;
		int px0 = max(bx0, (int)ceilf(lo - 0.5f)), px1 = min(bx1 - 1, (int)floorf(hi - 0.5f));
		for (int px = px0; px <= px1; px++) {
			float dx = px + 0.5f - X[0], dy = yc - Y[0];
			float iz = a[0][0] + dadx[0] * dx + dady[0] * dy;
			if (iz <= 0) continue;
			int r = p->colour.r, g = p->colour.g, b = p->colour.b, al = p->colour.a;
			if (t) {
				float u = (a[1][0] + dadx[1] * dx + dady[1] * dy) / iz, v = (a[2][0] + dadx[2] * dx + dady[2] * dy) / iz;
				uint32_t tx = tex_bilinear(t, u, v);
				r = (((tx >> 16) & 0xff) * (r + 1)) >> 8;
				g = (((tx >> 8) & 0xff) * (g + 1)) >> 8;
				b = ((tx & 0xff) * (b + 1)) >> 8;
				al = ((tx >> 24) * (al + 1)) >> 8;
			}
			if (p->blend == BLEND_ALPHA_TEST && al < 128) continue;
			uint32_t src = (uint32_t)(r << 16 | g << 8 | b), *dst = &fb[py * W + px];
			if (p->blend == BLEND_ALPHA) src = px_lerp(*dst, src, al + (al >> 7));
			*dst = 0xff000000u | src;
		}
	}
}

static void xf_draw_line(const prim_t *p) { // a transformed FwLineDraw: one pixel wide, end points included
	int bx0, by0, bx1, by1;
	prim_bounds(p, &bx0, &by0, &bx1, &by1);
	float s = render_2d_scale(), x0 = p->qx[0] * s, y0 = p->qy[0] * s;
	float dx = p->qx[1] * s - x0, dy = p->qy[1] * s - y0;
	int n = (int)ceilf(max(fabsf(dx), fabsf(dy)));
	uint32_t c = 0xff000000u | (uint32_t)(p->colour.r << 16 | p->colour.g << 8 | p->colour.b), *fb = render_pixels();
	for (int i = 0; i <= n; i++) {
		float f = n ? (float)i / n : 0;
		int x = (int)floorf(x0 + dx * f), y = (int)floorf(y0 + dy * f);
		if (x >= bx0 && x < bx1 && y >= by0 && y < by1) fb[y * render_width() + x] = c;
	}
}

// the font glyph table (misc\fonts.fti, as font.c) for text drawn through a transform: one quad per glyph
static struct { uint8_t glyph[3][224][4]; texfile_t *tex; int state; } xf_font;   // state 0 not loaded, 1 ok, -1 failed

static bool xf_font_load(void) {
	if (xf_font.state) return xf_font.state > 0;
	xf_font.state = -1;
	uint32_t size;
	uint8_t *d = fs_load("MISC\\FONTS.FTI", &size);
	if (!d || size < 0x14) {
		free(d);
		return false;
	}
	int n = get_u16(d);
	for (int f = 0; f < n && f < 3; f++) {
		uint32_t off = get_u16(d + 2 + 2 * f);
		if (off + 224 * 4 <= size) memcpy(xf_font.glyph[f], d + off, 224 * 4);
	}
	free(d);
	xf_font.tex = texfile_load("MISC\\FONTS\\PCPURE\\FONTS");
	if (xf_font.tex) xf_font.state = 1;
	return xf_font.state > 0;
}


static int prim_cmp(const void *a, const void *b) {
	const prim_t *p = a, *q = b;
	if (p->layer != q->layer) return p->layer - q->layer;
	return q->seq - p->seq; // OT_Insert prepends: the last one queued in a layer is drawn first
}

void ui_draw_end(void) {
	qsort(prims, (size_t)num_prims, sizeof(prim_t), prim_cmp);
	for (int i = 0; i < num_prims; i++) {
		const prim_t *p = &prims[i];
		if (p->clip[2] > p->clip[0]) {
			render_set_scissor_2d(p->clip[0], p->clip[1], p->clip[2], p->clip[3]);
		} else {
			render_reset_scissor();
		}
		if (p->xf) {
			if (p->kind == PRIM_QUAD) xf_draw_quad(p);
			else if (p->kind == PRIM_LINE) xf_draw_line(p);
			continue;
		}
		switch (p->kind) {
			case PRIM_QUAD:
				render_quad_2d(p->x, p->y, p->w, p->h, p->u0, p->v0, p->u1, p->v1, p->colour, p->tex, p->blend);
				break;
			case PRIM_LINE: {
				// FwLineDraw: a 1-pixel line (all the UI's lines are horizontal or vertical)
				float x0 = min(p->x, p->w), x1 = max(p->x, p->w), y0 = min(p->y, p->h), y1 = max(p->y, p->h);
				render_quad_2d(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0, 0, 0, 0, p->colour, NULL, BLEND_OPAQUE);
				break;
			}
			case PRIM_TEXT:
				font_print_n(arena + p->text, p->n, (int)p->x, (int)p->y, p->colour.r | p->colour.g << 8 | p->colour.b << 16,
					p->font);
				break;
		}
	}
	render_reset_scissor();
	num_prims = 0;
	arena_len = 0;
}

texture_t *ui_texture(const UiSprite *s) {
	if (!s || !s->tf || s->tex < 0 || s->tex >= s->tf->count) return NULL;
	tex_entry_t *e = &s->tf->entries[s->tex];
	if (!e->frame) return NULL;
	int f = e->frames > 1 ? (int)(Clock_Now() / 80u) % e->frames : 0; // TexAnimPlay(loop, 80 ms) of the FE logos
	return &e->frame[f];
}

void ui_sprite(int layer, int x, int y, const UiSprite *s, const UiRect *r, bool blend) { // FwSpriteDrawSectionFX
	texture_t *t = ui_texture(s);
	prim_t *p;
	if (!t || !r || !(p = new_prim(PRIM_QUAD, layer))) return;
	int w = r->right - r->left, h = r->bottom - r->top;
	p->x = (float)x; p->y = (float)y; p->w = (float)w; p->h = (float)h;
	p->u0 = (float)r->left / t->width; p->v0 = (float)r->top / t->height;
	p->u1 = (float)r->right / t->width; p->v1 = (float)r->bottom / t->height;
	p->tex = t;
	p->colour = rgba(255, 255, 255, 255);
	p->blend = blend ? BLEND_ALPHA : BLEND_ALPHA_TEST;
	prim_transform_rect(p);
}

void ui_sprite_full(int layer, int x, int y, const UiSprite *s) { // FwSpriteDrawFX
	texture_t *t = ui_texture(s);
	if (!t) return;
	UiRect r = {0, 0, (int16_t)t->height, (int16_t)t->width};
	ui_sprite(layer, x, y, s, &r, true);
}

void ui_sprite_tiled_h(int layer, int x, int y, int width, const UiSprite *s, const UiRect *r) { // 0x45e000
	int tw = r->right - r->left;
	if (tw <= 0) return;
	int part = width % tw;
	if (part > 0) {
		UiRect pr = {r->top, r->left, r->bottom, (int16_t)(r->left + part)};
		ui_sprite(layer, x, y, s, &pr, true);
		x += part;
	}
	for (int i = width / tw; i > 0; i--, x += tw) {
		ui_sprite(layer, x, y, s, r, true);
	}
}

void ui_sprite_tiled_v(int layer, int x, int y, int height, const UiSprite *s, const UiRect *r) { // 0x45e0f0
	int th = r->bottom - r->top;
	if (th <= 0) return;
	int part = height % th;
	if (part > 0) {
		UiRect pr = {r->top, r->left, (int16_t)(r->top + part), r->right};
		ui_sprite(layer, x, y, s, &pr, true);
		y += part;
	}
	for (int i = height / th; i > 0; i--, y += th) {
		ui_sprite(layer, x, y, s, r, true);
	}
}

void ui_tile(int layer, int x, int y, int w, int h, uint32_t c, int mode) { // FwTileDraw 0x43c190
	prim_t *p;
	if (w <= 0 || h <= 0 || !(p = new_prim(PRIM_QUAD, layer))) return;
	p->x = (float)x; p->y = (float)y; p->w = (float)w; p->h = (float)h;
	// colour 0xAABBGGRR; mode 0 = opaque, otherwise translucent with the colour's alpha
	p->colour = rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, mode ? (uint8_t)(c >> 24) : 255);
	p->blend = mode ? BLEND_ALPHA : BLEND_OPAQUE;
	prim_transform_rect(p);
}

void ui_line(int layer, int x0, int y0, int x1, int y1, uint32_t c) { // FwLineDraw 0x43dd30
	prim_t *p = new_prim(PRIM_LINE, layer);
	if (!p) return;
	p->x = (float)x0; p->y = (float)y0; p->w = (float)x1; p->h = (float)y1;
	p->colour = rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255);
	if (cur_xf) {
		xf_project(p->x, p->y, &p->qx[0], &p->qy[0], &p->qiz[0]);
		xf_project(p->w, p->h, &p->qx[1], &p->qy[1], &p->qiz[1]);
		p->xf = true;
	}
}

static void ui_text_xf(int layer, const char *s, int n, int x, int y, uint32_t c, int font) { // fonts through a transform
	if (!xf_font_load() || font < 0 || font > 2 || font >= xf_font.tex->count) return;
	const texture_t *t = xf_font.tex->entries[font].frame;
	if (!t) return;
	for (int i = 0; i < n && s[i]; i++) {   // Font_PrintClipped 0x4302d0: one quad per glyph
		unsigned char ch = (unsigned char)s[i] == 10 ? ' ' : (unsigned char)s[i];
		if (ch < 0x20) continue;
		const uint8_t *g = xf_font.glyph[font][ch - 0x20];
		prim_t *p;
		if (g[2] && g[3] && (p = new_prim(PRIM_QUAD, layer))) {
			p->x = (float)x; p->y = (float)y; p->w = g[2]; p->h = g[3];
			p->u0 = (float)g[0] / t->width; p->v0 = (float)g[1] / t->height;
			p->u1 = (float)(g[0] + g[2]) / t->width; p->v1 = (float)(g[1] + g[3]) / t->height;
			p->tex = t;
			p->colour = rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255);
			p->blend = BLEND_ALPHA;
			prim_transform_rect(p);
		}
		x += g[2];
	}
}

void ui_text(int layer, const char *s, int n, int x, int y, uint32_t c, int font) { // Font_Print / Font_PrintN
	if (!s) return;
	int l = (int)strnlen(s, (size_t)max(n, 0));
	if (cur_xf) {
		ui_text_xf(layer, s, l, x, y, c, font);
		return;
	}
	if (l <= 0 || arena_len + l + 1 > (int)sizeof(arena)) return;
	prim_t *p = new_prim(PRIM_TEXT, layer);
	if (!p) return;
	memcpy(arena + arena_len, s, (size_t)l);
	arena[arena_len + l] = 0;
	p->text = arena_len;
	p->n = l;
	arena_len += l + 1;
	p->x = (float)x; p->y = (float)y;
	p->colour = rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255);
	p->font = font;
}

void ui_text_clipped(int layer, const char *s, int x, int y, uint32_t c, int font, int width) { // 0x45c630
	if (!s) return;
	int n = (int)strlen(s);
	if (font_width_n(s, n, font) <= width) {
		ui_text(layer, s, n, x, y, c, font);
		return;
	}
	// the characters that fit, then the next one cut at the width (Font_PrintClipped)
	int fit = 0, w = 0;
	while (fit < n && w + font_width_n(s + fit, 1, font) <= width) w += font_width_n(s + fit++, 1, font);
	ui_text(layer, s, fit, x, y, c, font);
	if (fit < n && width > w) {
		int saved[4];
		memcpy(saved, cur_clip, sizeof(saved));
		ui_clip(x + w, -10000, x + width, 10000);
		ui_text(layer, s + fit, 1, x + w, y, c, font);
		memcpy(cur_clip, saved, sizeof(saved));
	}
}

void ui_picture(int layer, int x, int y, int w, int h, const texture_t *t) {
	prim_t *p;
	if (!t || !(p = new_prim(PRIM_QUAD, layer))) return;
	p->x = (float)x; p->y = (float)y; p->w = (float)w; p->h = (float)h;
	p->u0 = 0; p->v0 = 0; p->u1 = (float)w / t->width; p->v1 = (float)h / t->height;
	p->tex = t;
	p->colour = rgba(255, 255, 255, 255);
	p->blend = BLEND_ALPHA;
	prim_transform_rect(p);
}

// ================================================================ UI core

#define UI_POOL 0x43
static Window pool[UI_POOL];
static Window *free_list;
static int pool_free;
static Window *gUIRoot;              // 0x573150 (top-level windows)
Window *gUiFocus;                    // 0x573174
static Window *ui_hover;             // 0x573178 window under the mouse
static Window *ui_cancel;            // 0x573170 activated by Backspace
static Window **ui_nav_link;         // 0x57316c
static int mouse_dx, mouse_dy;       // 0x4e85ec / 0x4e85f0 mouse movement this frame
static int mouse_last_x, mouse_last_y;
static uint32_t mouse_moved_time;    // 0x57317c
int32_t gUiIdleMs;                   // 0x573180
int32_t gUiShowPointer;              // 0x49a2a8
static bool mouse_down_now, mouse_down_prev; // 0x573158 / 0x573160

void UI_InitPool(void) { // 0x4839b0
	memset(pool, 0, sizeof(pool));
	for (int i = 0; i < UI_POOL - 1; i++) pool[i].next = &pool[i + 1];
	free_list = &pool[0];
	pool_free = UI_POOL;
	gUIRoot = NULL;
	gUiFocus = ui_hover = ui_cancel = NULL;
	memset(xforms, 0, sizeof(xforms));   // the windows of the last pool are gone with their transforms
	cur_xf = NULL;
}

Window *UI_AllocWindow(void) { // 0x4839e0
	Window *w = free_list;
	if (!w) return NULL;
	free_list = w->next;
	memset(w, 0, sizeof(*w));
	w->flags = UIF_POOLED;
	w->used = true;
	pool_free--;
	return w;
}

static void ui_free(Window *w) { // 0x483700
	w->used = false;
	w->next = free_list;
	free_list = w;
	pool_free++;
}

static void ui_link_nav(Window *n, Window *w) { // 0x483960: neighbours that point at w get w's back links
	if (n->next) ui_link_nav(n->next, w);
	if (n->child) ui_link_nav(n->child, w);
	if (n->nav_up == w) w->nav_down = n;
	if (n->nav_down == w) w->nav_up = n;
	if (n->nav_left == w) w->nav_right = n;
	if (n->nav_right == w) w->nav_left = n;
}

void UI_OpenWindow(Window *w, Window *parent) { // 0x483a20
	if (!w) return;
	if (!parent) {
		w->next = gUIRoot;
		w->parent = NULL;
		gUIRoot = w;
		w->depth = 0;
	} else {
		w->next = parent->child;
		parent->child = w;
		w->parent = parent;
		w->depth = parent->depth + 1;
	}
	w->prev = NULL;
	if (w->next) w->next->prev = w;
	w->flags &= ~UIF_DRAWN;
	if (w->parent && (w->parent->flags & UIF_NO_MOUSE)) w->flags |= UIF_NO_MOUSE;
	if (!w->open) {
		w->state = UI_OPEN;
	} else {
		w->state = UI_OPENING;
		if (w->flags & UIF_OPEN_NOW) {
			w->state = w->open(w, 10 + w->depth);
			w->flags |= UIF_DRAWN;
		}
	}
	if ((w->flags & UIF_NAV_LINK) && gUIRoot) ui_link_nav(gUIRoot, w);
}

void UI_CloseWindow(Window *w) { // 0x483ae0
	if (!w || !w->used) return;
	w->state = (w->flags & UIF_DRAWN) ? UI_CLOSING : UI_CLOSED;
	if (w == ui_hover) ui_hover = NULL;
	if (w == ui_cancel) ui_cancel = NULL;
	for (Window *c = w->child; c; c = c->next) UI_CloseWindow(c);
}

static bool ui_is_rooted(Window *w) { // 0x483650
	while (w->parent) w = w->parent;
	while (w->prev) w = w->prev;
	return w == gUIRoot;
}

void UI_SetFocus(Window *w) { // 0x483b40
	if (!w || !ui_is_rooted(w)) return;
	if ((w->flags & UIF_FOCUSABLE) && (w->state == UI_OPEN || w->state == UI_OPENING)) {
		if ((w->flags & UIF_NAV_LINK) && ui_nav_link) *ui_nav_link = gUiFocus;
		if (gUiFocus) gUiFocus->focused = 0;
		w->focused = 1;
		gUiFocus = w;
	} else if (w->flags & UIF_FOCUS_UP) {
		UI_SetFocus(w->parent);
	}
	if ((w->flags & UIF_FOCUS_CHILD) && w->child) UI_SetFocus(w->child);
	ui_nav_link = NULL;
}

void UI_SetCancel(Window *w) { ui_cancel = w; } // 0x483bd0

// On the UI's screen (see ui_area_offset), returned in frame units. An absolute child window holds frame
// units already: the controls place their row / arrow hit windows from their own UI_GetScreenPos.
void UI_GetScreenPos(const Window *w, int *x, int *y) { // FUN_00484030
	int sw = gWindowWidth, sh = gWindowHeight, ox, oy;
	ui_area_offset(&ox, &oy);
	switch (w->flags & 7) {
		case UI_POS_CENTRE: *x = w->x + sw / 2; *y = w->y + sh / 2; break;
		case UI_POS_ABS:
			*x = w->x;
			*y = w->y;
			if (w->parent) return;
			break;
		case UI_POS_BOTTOM: *x = w->x; *y = sh - w->y; break;
		case UI_POS_RIGHT: *x = sw - w->x; *y = w->y; break;
		case UI_POS_BOTTOM_RIGHT: *x = sw - w->x; *y = sh - w->y; break;
		case UI_POS_PARENT:
			if (w->parent) {
				UI_GetScreenPos(w->parent, x, y);
			} else {
				*x = ox;
				*y = oy;
			}
			*x += w->x;
			*y += w->y;
			return;
		default: *x = w->x; *y = w->y; break;
	}
	*x += ox;
	*y += oy;
}

bool UI_MouseInside(Window *w) { // 0x483690
	int x, y;
	UI_GetScreenPos(w, &x, &y);
	int mx = ui_mouse_x(), my = ui_mouse_y();
	return x <= mx && mx <= x + w->w && y <= my && my <= y + w->h;
}

bool UI_AnyFocused(const Window *w) { // 0x4841a0
	if (w->focused) return true;
	for (const Window *c = w->child; c; c = c->next) {
		if (UI_AnyFocused(c)) return true;
	}
	return false;
}

static void ui_find_hover(Window *w) { // 0x483720: deepest open window under the mouse
	if (w->next) ui_find_hover(w->next);
	if (w->child) ui_find_hover(w->child);
	if (w->state == UI_OPEN && !(w->flags & UIF_NO_MOUSE) && UI_MouseInside(w) &&
		(!ui_hover || ui_hover->depth < w->depth)) {
		ui_hover = w;
	}
}

static void ui_mouse_pass(Window *w) { // 0x483780: mouse callbacks
	if (w->next) ui_mouse_pass(w->next);
	if (w->child) ui_mouse_pass(w->child);
	if (!w->used) return;
	if (w->state != UI_OPEN || (w->flags & UIF_NO_MOUSE) || !w->mouse) return;
	int inside;
	if (!mouse_down_prev) {
		inside = UI_MouseInside(w);
	} else {
		if (w != ui_hover) {
			w->mouse(w, 0, 0);
			return;
		}
		if (w->flags & UIF_STICKY) {
			w->mouse(w, 1, 1);
			return;
		}
		inside = UI_MouseInside(w);
	}
	w->mouse(w, inside, mouse_down_now);
}

static void ui_update_pass(Window *w, int layer) { // 0x483820: state machine + drawing
	if (w->next) ui_update_pass(w->next, layer);
	if (w->child) ui_update_pass(w->child, layer);
	if (!w->used) return;
	int l = layer + w->depth;
	switch (w->state) {
		case UI_OPENING:
			w->state = w->open ? w->open(w, l) : UI_OPEN;
			w->flags |= UIF_DRAWN;
			break;
		case UI_OPEN:
			if (w->update) {
				int s = w->update(w, l);
				if (w->state == UI_OPEN) w->state = s;
			}
			w->flags |= UIF_DRAWN;
			break;
		case UI_CLOSING:
			w->state = w->close ? w->close(w, l) : UI_CLOSED;
			break;
		case UI_CLOSED:
			if (!w->child) {
				if (w->prev) w->prev->next = w->next;
				if (w->next) w->next->prev = w->prev;
				if (!w->parent) {
					if (gUIRoot == w) gUIRoot = w->next;
				} else if (w->parent->child == w) {
					w->parent->child = w->next;
				}
				w->parent = w->next = w->prev = NULL;
				if (gUiFocus == w) {
					w->focused = 0;
					gUiFocus = NULL;
				}
				w->state = UI_FREE;
				if (w->flags & UIF_POOLED) ui_free(w);
			}
			break;
		default:
			break;
	}
}

void ui_draw_pointer(void) { // UI_DrawPointer 0x484220: misc\standard texture 3 (drawn last, over everything)
	if (!gUiShowPointer) return;
	UiSprite s = {0, gStandardTextures, 3, 0};
	texture_t *t = ui_texture(&s);
	if (t) {
		render_reset_scissor();
		render_quad_2d((float)ui_mouse_x(), (float)ui_mouse_y(), (float)t->width, (float)t->height, 0, 0, 1, 1,
			rgba(255, 255, 255, 255), t, BLEND_ALPHA);
	}
}

static bool any_closing(Window *w) {
	for (; w; w = w->next) {
		if (w->state == UI_CLOSING || w->state == UI_CLOSED) return true;
		if (w->child && any_closing(w->child)) return true;
	}
	return false;
}

void UI_Flush(void) { // FUN_00409f10: run the window states (no input, nothing drawn) until no window is closing
	for (int i = 0; i < 8 && gUIRoot && any_closing(gUIRoot); i++) {
		int saved_prims = num_prims, saved_arena = arena_len;
		ui_update_pass(gUIRoot, 1);
		num_prims = saved_prims;
		arena_len = saved_arena;
	}
}

void UI_UpdateDraw(int layer) { // 0x483be0
	int mx = ui_mouse_x(), my = ui_mouse_y();
	mouse_dx = mx - mouse_last_x;
	mouse_dy = my - mouse_last_y;
	mouse_last_x = mx;
	mouse_last_y = my;
	if (mouse_dx || mouse_dy) mouse_moved_time = Clock_Now();
	gUiIdleMs = (int32_t)(Clock_Now() - mouse_moved_time);
	mouse_down_now = ui_key_down(UIK_MOUSE0);
	bool empty;
	if (!mouse_down_prev && !mouse_down_now) {
		ui_hover = NULL;
		if (gUIRoot) ui_find_hover(gUIRoot);
	}
	empty = gUIRoot == NULL;
	if (!empty) {
		ui_mouse_pass(gUIRoot);
		empty = gUIRoot == NULL;
	}
	if (!empty) ui_update_pass(gUIRoot, layer);
	if (gUiFocus) {
		static const uint32_t nav_key[4] = {UIK_UP, UIK_DOWN, UIK_LEFT, UIK_RIGHT};
		for (int i = 0; i < 4 && gUiFocus; i++) {
			Window *to = i == 0 ? gUiFocus->nav_up : i == 1 ? gUiFocus->nav_down : i == 2 ? gUiFocus->nav_left : gUiFocus->nav_right;
			if (ui_key_pressed(nav_key[i]) && to) {
				ui_nav_link = i == 0 ? &to->nav_down : i == 1 ? &to->nav_up : i == 2 ? &to->nav_right : &to->nav_left;
				UI_SetFocus(to);
				ui_key_consume(nav_key[i]);
			}
		}
		if (gUiFocus && gUiFocus->state == UI_OPEN && ui_key_pressed(UIK_SELECT) && gUiFocus->select) {
			gUiFocus->select(gUiFocus);
			ui_key_consume(UIK_SELECT);
		}
	}
	if (ui_hover && ui_key_pressed(UIK_MOUSE0)) {
		ui_key_consume(UIK_MOUSE0);
		UI_SetFocus(ui_hover);
		if (ui_hover && ui_hover->select) ui_hover->select(ui_hover);
	}
	if (ui_cancel && ui_key_pressed(UIK_BACK)) {
		ui_key_consume(UIK_BACK);
		UI_SetFocus(ui_cancel);
		if (ui_cancel && ui_cancel->select) ui_cancel->select(ui_cancel);
	}
	mouse_down_prev = mouse_down_now;
}

Window *UI_GetRoot(void) { return gUIRoot; }

void UI_ResetIdle(void) { // FE_Run: DAT_00573180 = 0, _DAT_0057317c = 0 (the mouse "moved" at clock 0)
	mouse_moved_time = 0;
	gUiIdleMs = 0;
}

void UI_CloseAll(void) { // 0x483ee0
	for (Window *w = gUIRoot; w;) {
		Window *n = w->next;
		UI_CloseWindow(w);
		w = n;
	}
}

// ================================================================ windows

typedef struct {          // HTML layout state, pool FUN_004282b0 (0x4c bytes)
	int32_t pad;
	int32_t rec;          // +0x04 record index (-1 none)
	int32_t pad08;
	int32_t pstate;       // +0x0c 0 head, 1 title, 2 body
	const char *cur;      // +0x10
	int32_t pad14;
	int32_t lines;        // +0x18
	int32_t y;            // +0x1c
	int32_t x;            // +0x20
	int32_t lineh;        // +0x24
	int16_t lm, rm, tm, bm;  // +0x28..+0x2e margins
	int32_t span_align;   // +0x30
	int32_t align;        // +0x34
	int32_t font;         // +0x38
	uint32_t bg;          // +0x3c
	uint32_t colour;      // +0x40 (0x00BBGGRR here)
	const char *title;    // +0x44
	int16_t img_first, img_last;   // +0x48, +0x4a
} HtmlInfo;

typedef struct {          // image record (0x28 bytes, DAT_0061f460[8])
	int32_t used;
	int32_t tex;          // +4
	const char *alt;      // +8
	int32_t side;         // +0xc 0 left, 1 right, 2 centred
	int16_t w, h;         // +0x10, +0x12
	int32_t hspace, vspace; // +0x14, +0x18
	int16_t top, x0, bottom, x1;   // +0x1c..+0x22 placement (content space)
	int32_t state;        // +0x24 1 = size not read yet
} HtmlImage;

#define MAX_PANES 16
static PaneInfo pane_pool[MAX_PANES];
static HtmlInfo html_pool[4];
static HtmlImage images[8];

static PaneInfo *pane_alloc(void) { // FUN_00428270
	for (int i = 0; i < MAX_PANES; i++) {
		if (pane_pool[i].pad == 0) {
			memset(&pane_pool[i], 0, sizeof(pane_pool[i]));
			pane_pool[i].pad = 1;
			return &pane_pool[i];
		}
	}
	return NULL;
}

static HtmlInfo *html_alloc(void) { // FUN_004282b0
	for (int i = 0; i < 4; i++) {
		if (html_pool[i].pad == 0) {
			memset(&html_pool[i], 0, sizeof(html_pool[i]));
			html_pool[i].pad = 1;
			html_pool[i].rec = -1;
			html_pool[i].lm = html_pool[i].rm = html_pool[i].tm = html_pool[i].bm = 2;
			html_pool[i].colour = 0xaaaaaa;
			return &html_pool[i];
		}
	}
	return NULL;
}

UiBorder gUiBorder;
static const UiRect border_rects[8] = { // Border 0x48f5f8 + 4: corners, edges in misc\global texture 5
	{8, 8, 12, 12}, {8, 16, 12, 20}, {16, 8, 20, 12}, {16, 16, 20, 20},
	{0, 8, 4, 28}, {24, 0, 28, 20}, {0, 0, 20, 4}, {8, 24, 28, 28},
};
static UiSprite border_sprite = {0, NULL, 5, 0};    // SpriteInfo_Border 0x583730
static const UiRect vscroll_rects[6] = {           // gVScroll_Info rects 0x48fb58 (texture 6)
	{64, 0, 68, 8}, {68, 0, 83, 8}, {84, 0, 88, 8}, {89, 0, 97, 8}, {98, 0, 106, 8}, {107, 0, 115, 8},
};
static UiSprite vscroll_sprite = {0, NULL, 6, 0};   // 0x49b268

static void border_size(const Window *w, int *bw, int *bh) { // FUN_0045da80
	if (!(w->u58 & WINF_BORDER)) {
		*bw = *bh = 0;
	} else if (w->u60) {
		const UiBorder *b = (const UiBorder *)w->u60;
		*bw = b->rects[0].right - b->rects[0].left;
		*bh = b->rects[0].bottom - b->rects[0].top;
	} else {
		*bw = *bh = 1;
	}
}

static void client_rect(const Window *w, int r[4], int b[2]) { // FUN_0045daf0: x, y, w, h inside the frame
	uint32_t f = (uint32_t)w->u58;
	border_size(w, &b[0], &b[1]);
	r[0] = b[0];
	r[1] = b[1];
	r[2] = w->w - 2 * b[0];
	r[3] = w->h - 2 * b[1];
	if (f & WINF_TITLE) { r[1] += 12; r[3] -= 12; }
	if (f & WINF_VSCROLL) r[2] -= 10;
	if (f & WINF_HSCROLL) r[3] -= 10;
	if (r[2] < 0) r[2] = 0;
	if (r[3] < 0) r[3] = 0;
}

static void draw_frame(Window *w, int layer) { // FUN_004624a0: 9-slice border
	const UiBorder *b = (const UiBorder *)w->u60;
	const UiRect *r = b->rects;
	const UiSprite *s = b->sprite;
	int x, y;
	UI_GetScreenPos(w, &x, &y);
	int cw = r[0].right - r[0].left, ch = r[0].bottom - r[0].top;
	ui_sprite(layer, x, y, s, &r[0], true);
	ui_sprite(layer, x + w->w - cw, y, s, &r[1], true);
	ui_sprite(layer, x, y + w->h - ch, s, &r[2], true);
	ui_sprite(layer, x + w->w - cw, y + w->h - ch, s, &r[3], true);
	ui_sprite_tiled_h(layer, x + cw, y, w->w - 2 * cw, s, &r[4]);
	ui_sprite_tiled_v(layer, x, y + ch, w->h - 2 * ch, s, &r[6]);
	ui_sprite_tiled_h(layer, x + cw, y + w->h - ch, w->w - 2 * cw, s, &r[5]);
	ui_sprite_tiled_v(layer, x + w->w - cw, y + ch, w->h - 2 * ch, s, &r[7]);
}

// ---------------------------------------------------------------- the window transform (Win_SetTransform)
static int window_open(Window *w, int layer);

static Window *outer_window(const Window *w) { // FUN_0045d560: the outermost window node of w's parents
	Window *r = NULL;
	for (; w; w = w->parent) {
		if (w->open == window_open) r = (Window *)w;
	}
	return r;
}

void Win_SetTransform(Window *w) { // 0x45d660 (NULL = screen space)
	const WinXform *x = NULL;
	Window *top = w ? outer_window(w) : NULL;
	if (top) {
		WinXform *r = (WinXform *)top->u64;
		if (r && r->coords && Clock_Now() != r->last) xform_place(r);
		if (r && r->coords) x = r;
	}
	sprite_set_transform(x);
}

void ui_set_transform(const Window *w) { Win_SetTransform((Window *)w); }

void Window_CreateTransform(Window *w) { // 0x45d6b0
	WinXform *r = w ? (WinXform *)w->u64 : NULL;
	if (!r) return;
	int x, y;
	UI_GetScreenPos(w, &x, &y);
	r->cx = x + ((w->w >> 1) - (render_2d_width() >> 1));
	r->cy = y + ((w->h >> 1) - (render_2d_height() >> 1));
	cam_view_identity(&r->outer);       // Coord_Alloc(gView) twice: the outer node and its child
	r->outer.counter = 0;
	r->outer.mat.t[0] = r->outer.mat.t[1] = r->outer.mat.t[2] = 0;
	r->coords = true;
	if (cur_xf == r) cur_xf = NULL;     // new nodes: FwSpriteSetTransform takes the matrix again
	xform_place(r);
}

// ---------------------------------------------------------------- title bar (FUN_0045d8b0)
static int title_update(Window *t, int layer) {
	Window *p = t->parent;
	Win_SetTransform(t);
	int b[2];
	border_size(p, &b[0], &b[1]);
	t->x = b[0];
	t->y = b[1];
	t->w = p->w - 2 * b[0];
	t->u60 = p->u54;                                    // the parent's title
	const char *s = (const char *)t->u60;
	if (s) {
		int x, y, n = (int)strlen(s);
		UI_GetScreenPos(t, &x, &y);
		int tw = font_width_n(s, n, 1), th = 0;
		for (int i = 0; i < n; i++) th = max(th, font_glyph_h(s[i], 1));
		if (t->w < tw && t->w > 0) {               // FUN_0045c4c0: the characters that fit in w - 2
			while (n > 0 && font_width_n(s, n, 1) > t->w - 2) n--;
			tw = font_width_n(s, n, 1);
		}
		ui_text(layer + 2, s, n, x + 2, y + 2, (uint32_t)t->u5c, 1);
		ui_line(layer, x + 2 + tw, y + 2 - 1 + th, x + 2 + t->w - 4, y + 2 - 1 + th, 0x00b3ad00); // DAT_004992a0
	}
	int x, y;
	UI_GetScreenPos(t, &x, &y);
	ui_tile(layer, x, y, t->w, t->h, (uint32_t)t->u58, 1);
	return UI_OPEN;
}

// ---------------------------------------------------------------- vertical scroll bar (0x462390..)
static void vthumb_set(Window *k) { // FUN_00462180 + FUN_00462150: knob position -> scroll value
	Window *bar = k->parent;
	int range_px = bar->h - k->h;
	int lim = range_px + 4;
	if (k->y < 4) k->y = 4;
	else if (k->y > lim) k->y = lim;
	int32_t *max = (int32_t *)k->u5c, *val = (int32_t *)k->u60;
	int v = range_px > 0 ? ((*max - 0) * (k->y - 4)) / range_px : 0;
	if (v > *max) v = *max;
	*val = v;
}

static int vthumb_update(Window *k, int layer) { // FUN_004621d0
	const UiRect *r = &vscroll_rects[3];
	if (k->parent->parent && k->parent->parent->focused) {   // the scrolled window has the focus
		if (ui_key_down(UIK_UP)) { k->y -= 4; vthumb_set(k); }
		if (ui_key_down(UIK_DOWN)) { k->y += 4; vthumb_set(k); }
		r = k->u64 == 1 ? &vscroll_rects[5] : &vscroll_rects[4];
	}
	k->u64 = 0;
	int x, y;
	UI_GetScreenPos(k, &x, &y);
	ui_sprite(layer + 1, x, y, &vscroll_sprite, r, true);
	return UI_OPEN;
}

static void vthumb_mouse(Window *k, int inside, int down) { // FUN_004622a0: drag
	if (!inside) { k->u64 = 0; return; }
	if (!down) { k->u64 = 1; return; }
	int x, y;
	UI_GetScreenPos(k, &x, &y);
	if ((mouse_dy > 0 && ui_mouse_y() >= y) || (mouse_dy < 0 && ui_mouse_y() <= y + k->h)) {
		k->y += mouse_dy;
		vthumb_set(k);
	}
	k->u64 = 2;
}

static int vbar_open(Window *bar, int layer) { // FUN_00462390 + FUN_00462330
	(void)layer;
	Window *k = UI_AllocWindow();
	if (!k) return UI_OPEN;
	k->flags |= 0x95;
	k->x = 0;
	k->y = 0;
	k->w = bar->w;
	k->h = 0x10;
	k->update = vthumb_update;
	k->mouse = vthumb_mouse;
	k->u5c = bar->u5c;
	k->u60 = bar->u60;
	UI_OpenWindow(k, bar);
	int32_t *max = (int32_t *)bar->u5c, *val = (int32_t *)bar->u60;
	k->y = (*max > 0 ? (*val * (bar->h - k->h)) / *max : 0) + 4;    // FUN_00462120
	return UI_OPEN;
}

static int vbar_update(Window *bar, int layer) { // FUN_004623d0: track = top cap, tiled middle, bottom cap
	int x, y;
	UI_GetScreenPos(bar, &x, &y);
	const UiRect *r = vscroll_rects;
	int top = r[0].bottom - r[0].top, bot = r[2].bottom - r[2].top;
	ui_sprite(layer, x, y, &vscroll_sprite, &r[0], true);
	ui_sprite_tiled_v(layer, x, y + top, bar->h - top - bot, &vscroll_sprite, &r[1]);
	ui_sprite(layer, x, y + bar->h - bot, &vscroll_sprite, &r[2], true);
	return UI_OPEN;
}

static int ui_closed(Window *w, int layer) { (void)w; (void)layer; return UI_CLOSED; } // FUN_0045ec60

// ---------------------------------------------------------------- HTML (TextDoc_*)
typedef struct { Window *pane; int layer; PaneInfo *pi; HtmlInfo *h; int sx, sy; } doc_t;

static const uint8_t *text_data;
static uint32_t text_size, text_count, text_table;

static const uint8_t *text_record(int id, uint32_t *size) { // Text_GetRecord 0x45c460
	if (!text_data || id < 0 || (uint32_t)id >= text_count) return NULL;   // (the original accepts id == count)
	const uint8_t *e = text_data + 12 + 8 * id;
	uint32_t off = get_u32(e), sz = get_u32(e + 4), start = 12 + text_table + off;
	if (start + sz > text_size) return NULL;
	*size = sz;
	return text_data + start;
}

static void doc_newline(HtmlInfo *h) { // TextDoc_NewLine 0x45ccc0
	int lh = h->lineh;
	h->x = h->lm;
	h->lineh = 8;
	h->y += lh + 1;
	h->lines++;
}

static void doc_parabreak(HtmlInfo *h) { // TextDoc_ParaBreak 0x45ccf0
	if (h->lm < h->x) {
		if (h->lineh > 1) {
			h->y += h->lineh + 1;
			h->lines++;
		}
		h->lineh = 0;
	}
	h->x = h->lm;
	if (h->tm < h->y) h->y += 6;
}

static void doc_indents(HtmlInfo *h, int *left, int *right) { // FUN_0045c790: floating images
	*left = *right = 0;
	for (int i = h->img_first; i <= h->img_last && i < 8; i++) {
		HtmlImage *im = &images[i];
		if (!im->used) continue;
		if (h->y < im->top) return;
		if (h->lineh + im->bottom < h->y) {
			h->img_first++;
		} else if (im->side == 0) {
			*left = im->w + im->hspace * 2;
		} else if (im->side == 1) {
			*right = im->w + im->hspace * 2;
		}
	}
}

static void doc_span(doc_t *d, const char *s, int n) { // TextDoc_DrawSpan 0x45c820 (clipped by the scissor)
	HtmlInfo *h = d->h;
	int align = h->align == 4 ? h->span_align : h->align;
	if (n < 1) return;
	if (d->pane->h < h->y - d->pi->sy) {
		h->x += font_width_n(s, n, h->font);
		return;
	}
	int w = font_width_n(s, n, h->font), gh = 0;
	for (int i = 0; i < n; i++) gh = max(gh, font_glyph_h((unsigned char)s[i], h->font));
	if (w < 1) return;
	int room = d->pi->w - w;
	if (align == 0) room -= h->x;
	if (room >= 1) {
		if (align == 2) h->x = room >> 1;
		else if (align == 1) h->x = h->lm - h->rm + room;
	} else if (align != 0) {
		h->x = h->lm;
	}
	int x = h->x;
	h->x += w;
	if (h->lineh < gh) h->lineh = gh;
	int ty = h->y - d->pi->sy;
	if (ty + h->lineh < 0) return;
	ui_text(d->layer, s, n, d->sx + x - d->pi->sx, d->sy + ty, h->colour, h->font);
}

static void doc_words(doc_t *d, const char *s, int n) { // TextDoc_LayoutWords 0x45cd40
	HtmlInfo *h = d->h;
	PaneInfo *pi = d->pi;
	const char *span = s;       // start of the words not drawn yet
	int span_w = 0;
	int left = 0, right = 0;
	int i = 0;
	while (i < n) {
		int wl;
		if (s[i] == ' ' || s[i] == '\n') {
			wl = 1;
		} else {
			wl = 0;
			while (i + wl < n && s[i + wl] != ' ' && s[i + wl] != '\n') wl++;
		}
		int ww = font_width_n(s + i, wl, h->font), wh = 0;
		for (int k = 0; k < wl; k++) wh = max(wh, font_glyph_h((unsigned char)s[i + k], h->font));
		if (ww > 0) {
			if (h->lineh < wh) h->lineh = wh;
			doc_indents(h, &left, &right);
			if (h->x < left) h->x += left;
			int room = pi->w - (h->x + span_w) - right - ww;
			if (room > h->rm) {
				span_w += ww;
			} else {
				// the word does not fit: draw the line so far, new line
				doc_span(d, span, (int)(s + i - span));
				doc_newline(h);
				span = s + i;
				span_w = 0;
				if (s[i] == ' ' || s[i] == '\n') {
					span++;
				} else {
					span_w = ww;
				}
			}
		}
		i += wl;
	}
	doc_span(d, span, (int)(s + n - span));
	int bottom = h->y + h->lineh;
	if (pi->h < bottom) {
		pi->smy = bottom - d->pane->h;
		if (pi->smy < 0) pi->smy = 0;
	}
}

static void doc_image(doc_t *d, HtmlImage *im) { // FUN_0045c260
	HtmlInfo *h = d->h;
	if (im->state == 1) {          // FUN_0045df90: texture size
		tex_entry_t *e = gEnvironmentTexFile && im->tex >= 0 && im->tex < gEnvironmentTexFile->count ?
			&gEnvironmentTexFile->entries[im->tex] : NULL;
		if (e && e->frame) {
			if (!im->w) im->w = (int16_t)e->frame->width;
			if (!im->h) im->h = (int16_t)e->frame->height;
		}
		im->state = 2;
	}
	int side = im->side == 4 ? h->span_align : im->side, x;
	if (side == 0) x = h->lm + im->hspace;
	else if (side == 1) x = d->pane->w - im->hspace - h->rm - im->w;
	else x = (d->pane->w - im->w) >> 1;
	im->top = (int16_t)h->y;
	im->bottom = (int16_t)(h->y + im->h);
	im->x0 = (int16_t)x;
	im->x1 = (int16_t)(x + im->w);
	if (side != 0 && side != 1) {
		h->x = x;
	}
	tex_entry_t *e = gEnvironmentTexFile && im->tex >= 0 && im->tex < gEnvironmentTexFile->count ?
		&gEnvironmentTexFile->entries[im->tex] : NULL;
	if (e && e->frame) {
		ui_picture(d->layer, d->sx + x - d->pi->sx, d->sy + h->y - d->pi->sy + im->vspace, im->w, im->h, e->frame);
	}
	if (side == 2) h->y += im->h + im->vspace * 2;
}

static uint32_t prop_value(const uint8_t *p) { return get_u32(p + 8); }

static void doc_chunk(doc_t *d, const uint8_t *c, uint32_t len) { // TextDoc_HandleChunk 0x45cfd0
	HtmlInfo *h = d->h;
	uint32_t tag = get_u32(c);
	uint32_t closing = tag & 0x40000000, t = tag & 0x3fffffff;
	int kind = (int)((t >> 24) & 0xff) - 1, level = (int)((t >> 16) & 0xff);
	if (h->pstate == 2) {
		if (t == 0) {
			int n = (int)len - 8;
			if (n > 0x400) n++;
			if (n > (int)len - 8) n = (int)len - 8;
			doc_words(d, (const char *)c + 8, n);
			return;
		}
		if (!closing) {
			if (!(tag & 0x80000000)) {
				if (kind == 0 && level == 0) doc_newline(h);
				else if (kind == 1 && level == 2) { h->span_align = 2; doc_newline(h); }
				return;
			}
			if (kind != 3 && kind != 8 && kind != 10 && kind != 12) return;
			// a list of property sub-chunks
			uint32_t o = 8;
			bool has_value = len > 8 && get_u32(c + 12) > 8;   // param_2[3] > 8
			if (kind == 3) {                                    // PARA_OPEN: alignment
				int align = 0;
				bool got = false;
				while (o + 12 <= len) {
					uint32_t pl = get_u32(c + o + 4);
					if (pl < 8) break;
					if (c[o] == 0) { align = (int)prop_value(c + o); got = true; }
					o += pl;
				}
				doc_parabreak(h);
				if (got && has_value) h->align = align <= 2 ? align : 0;   // junk alignments render as left
				return;
			}
			while (o + 8 <= len) {
				const uint8_t *p = c + o;
				uint32_t pl = get_u32(p + 4);
				if (pl < 8 || o + pl > len) break;
				if (has_value && p[1] == 1) {
					uint32_t v = pl >= 12 ? prop_value(p) : 0;
					if (kind == 8) {                    // DOC_COLOURS
						if (p[0] == 0) h->bg = v;
						else if (p[0] == 1) h->colour = v & 0xffffff;
					} else if (kind == 10) {            // CHAR_FMT
						if (p[0] == 0) h->font = (int32_t)v;
						else if (p[0] == 1) {
							if ((int32_t)v < 3) h->font = 2;
							else if (h->font != 1) h->font = 0;
						} else if (p[0] == 2) h->colour = v & 0xffffff;
					} else if (kind == 12) {            // IMAGE
						if (p[0] == 0) {
							if (h->img_last + 1 < 8) {
								h->img_last++;
								HtmlImage *im = &images[h->img_last];
								if (!im->used) {
									memset(im, 0, sizeof(*im));
									im->used = 1;
									im->state = 1;
									im->tex = (int32_t)v;
								}
							}
						} else if (h->img_last >= 0 && h->img_last < 8) {
							HtmlImage *im = &images[h->img_last];
							switch (p[0]) {
								case 1: im->alt = (const char *)p + 8; break;
								case 2: im->side = (int32_t)v; break;
								case 3: im->w = (int16_t)v; break;
								case 4: im->h = (int16_t)v; break;
								case 5: im->hspace = (int32_t)v; break;
								case 6: im->vspace = (int32_t)v; doc_image(d, im); break;
								default: break;
							}
						}
					}
				}
				o += pl;
			}
			return;
		}
		if (!(tag & 0x80000000) && (kind != 7 || level != 0)) {
			if (kind == 2 && level == 2) { h->span_align = 0; doc_newline(h); }
			else if (kind == 4) { doc_parabreak(h); h->align = 4; }   // PARA_CLOSE
		}
	} else if (h->pstate == 0) {
		if (!closing && kind == 6 && level == 2) h->pstate = 1;
		else if (closing && kind == 7 && level == 1) h->pstate = 2;
	} else if (h->pstate == 1) {
		if (t == 0) h->title = (const char *)c + 8;
		else if (closing && kind == 7 && level == 2) h->pstate = 0;
	}
}

static void doc_layout(Window *pane, int layer, PaneInfo *pi) { // TextDoc_Layout 0x461e70
	HtmlInfo *h = pi->data;
	uint32_t size;
	const uint8_t *rec;
	if (!h || h->rec < 0 || !(rec = text_record(h->rec, &size)) || !size) return;
	h->pstate = 0;
	h->span_align = 0;
	h->align = 0;
	h->lines = 0;
	h->lineh = 0;
	h->img_first = 0;
	h->y = 2;
	h->x = 2;
	h->img_last = -1;
	h->font = 0;
	h->colour = 0xaaaaaa;
	doc_t d = {pane, layer, pi, h, 0, 0};
	UI_GetScreenPos(pane, &d.sx, &d.sy);
	int saved[4];
	memcpy(saved, cur_clip, sizeof(saved));
	ui_clip(d.sx, d.sy, d.sx + pane->w, d.sy + pane->h);
	for (uint32_t o = 0; o + 8 <= size;) {
		uint32_t l = get_u32(rec + o + 4);
		if (l < 8 || o + l > size) break;
		doc_chunk(&d, rec + o, l);
		o += l;
	}
	memcpy(cur_clip, saved, sizeof(saved));
	int bottom = h->y + h->lineh;
	h->y = bottom;
	if (bottom < pi->h) {
		pi->h = bottom;
		pi->smy = bottom - pane->h;
		if (pi->smy < 0) pi->smy = 0;
		WinAPI_DestroyVScroll(pane->parent);
	} else if (pi->h == bottom) {
		WinAPI_DestroyVScroll(pane->parent);
	}
}

// ---------------------------------------------------------------- the client pane (FUN_00461f90)
static int pane_update(Window *p, int layer) { // FUN_00462050: Win_SetTransform + FUN_00461f90
	Window *w = p->parent;
	Win_SetTransform(p);
	int r[4], b[2];
	client_rect(w, r, b);
	PaneInfo *pi = (PaneInfo *)p->u60;
	p->x = r[0]; p->y = r[1]; p->w = r[2]; p->h = r[3];
	pi->w = r[2];
	pi->h = r[3];
	if (pi->flags & 2) {
		int x, y;
		UI_GetScreenPos(p, &x, &y);
		ui_tile(layer - 1, x, y, p->w, p->h, 0xb00d0500, 1);
	}
	if ((pi->flags & 4) && pi->data) {            // text line (FUN_00461d30)
		int x, y;
		UI_GetScreenPos(p, &x, &y);
		ui_text_clipped(layer, (const char *)pi->data, x + 2, y + 2, (uint32_t)p->u5c & 0xffffff, 0, p->w);
	}
	if (pi->flags & 0x40) doc_layout(p, layer, pi);
	if ((pi->flags & 0x10) && pi->data) {
		int x, y;
		UI_GetScreenPos(p, &x, &y);
		ui_sprite_full(layer, x, y, (const UiSprite *)pi->data);
	}
	return UI_OPEN;
}

static void pane_mouse(Window *p, int inside, int down) { (void)p; (void)inside; (void)down; } // FUN_004620c0 (event 0xb: multiplayer only)

// The model window's node (FUN_0045f860; open FUN_0045f060). +0x64 packs yaw (bits 0-11), pitch (12-22,
// signed) and the spin per frame (23-31, signed): 0x10100000 = pitch 256, spin 32. The instance sits in
// a parent node placed on the window centre's view ray that looks at the camera (FUN_0045f0a0).
typedef struct {
	int32_t model, scale, flags;  // +0x58 model, the parent node's z, +0x5e flags (WindowSetModelFlags)
	uint32_t opened;              // +0x60 open time (the scan-in)
	int32_t yaw, pitch, spin;     // +0x64
	bool turned;                  // the instance's rotation has been set (identity until the first turn)
	int32_t tx, ty;               // the instance's offset in its parent (dragged with the right button)
	int32_t zoom;                 // added to the parent's z (right button)
	bool dragging;                // +0x5e bit 0x10
} WinModel;

static WinModel *model_node(void) { // FUN_0045f060
	WinModel *m = calloc(1, sizeof(WinModel));
	m->model = -1;
	m->opened = Clock_Now();
	m->pitch = 256;
	m->spin = 32;
	return m;
}

static void model_mouse(Window *w, int inside, int down) { // FUN_0045f570 (the node's mouse callback, every frame)
	WinModel *m = (WinModel *)w->u60;
	if (!m) return;
	bool right = ui_key_down(UIK_MOUSE1);           // DAT_0049836c
	int32_t f = m->flags;
	if ((f & 2) && !down && right) right = false;
	if ((f & 4) && down && right) right = false;
	bool skip_move = false;
	if (f & 1) {
		if (!down) skip_move = true;
		else if (!right) down = 0;
	}
	if (!skip_move && down && right) {              // both buttons: move the model in its frame
		m->tx += mouse_dx * 4;
		m->ty -= mouse_dy * 4;
		return;
	}
	int32_t dyaw, dpitch = 0;
	if (!inside || !down) {
		if (m->dragging) {                          // let go: it keeps spinning at the last drag speed
			m->spin = (int32_t)((uint32_t)mouse_dx << 25) >> 23;
			m->dragging = false;
		}
		dyaw = m->spin;
	} else {                                        // dragged: yaw / pitch follow the mouse
		dyaw = mouse_dx * 4;
		dpitch = mouse_dy * 4;
		m->dragging = true;
	}
	if (dyaw || dpitch) {
		m->yaw = (m->yaw + dyaw) & 0xfff;
		int32_t p = m->pitch + dpitch;
		m->pitch = p > 1000 ? 1000 : p < -1000 ? -1000 : p;
		m->turned = true;
	}
	if (right) m->zoom += mouse_dy * 4 - mouse_dx * 4;   // the right button pulls it nearer / further
}

static int window_open(Window *w, int layer) { // FUN_00428450 -> FUN_0045db80
	(void)layer;
	uint32_t f = (uint32_t)w->u58;
	if ((f & WINF_MODEL) && !w->u60) {
		// the model node (FUN_0045f860: flags 0x485, mouse FUN_0045f570): kept in the window here
		w->u60 = (intptr_t)model_node();
		w->mouse = model_mouse;
		w->flags |= UIF_STICKY;
	}
	// a top-level window gets a transform record (FUN_0045d4b0), the others draw through their top's
	w->u64 = outer_window(w) == w ? (intptr_t)xform_alloc() : 0;
	if (f & WINF_MODEL) {
		return UI_OPEN;
	}
	PaneInfo *pi = (PaneInfo *)w->u5c;
	int r[4], b[2];
	client_rect(w, r, b);
	int cx = (f & WINF_BORDER) ? b[0] : 0, cy = (f & WINF_BORDER) ? b[1] : 0;
	if (f & WINF_SECTION) return UI_OPEN;
	if (f & WINF_TITLE) {
		Window *t = UI_AllocWindow();
		if (t) {
			t->y = cy;
			t->flags |= 0x85;
			t->x = cx;
			t->w = r[2];
			t->h = 12;
			t->update = title_update;
			t->u54 = (intptr_t)"Title";
			t->u58 = (intptr_t)0xb00d0500u;
			t->u5c = 0x00fff700;                // DAT_00499298
			t->u60 = w->u54;
			UI_OpenWindow(t, w);
		}
		cy += 12;
	}
	int cw = r[2], ch = r[3];
	if (f & WINF_TITLE) ch -= 12;            // (the original takes the title off twice here; WindowSetSize fixes it)
	if (f & WINF_VSCROLL) {
		Window *s = UI_AllocWindow();
		if (s && pi) {
			s->w = 10;
			s->flags |= 0xd;
			s->x = r[2] - 9 + cx;
			s->y = cy;
			s->h = ch;
			cw -= 10;
			s->open = vbar_open;
			s->close = ui_closed;
			s->update = vbar_update;
			s->u54 = (intptr_t)"VScroll";
			pi->smy = pi->h - ch;
			s->u5c = (intptr_t)&pi->smy;
			s->u60 = (intptr_t)&pi->sy;
			UI_OpenWindow(s, w);
		}
	}
	Window *p = UI_AllocWindow();
	if (p) {
		p->x = cx;
		p->w = cw;
		p->flags |= 5;
		p->y = cy;
		p->h = ch;
		p->update = pane_update;
		p->mouse = pane_mouse;
		p->u54 = (intptr_t)"Pane";
		p->u58 = (intptr_t)0xb00d0500u;
		p->u5c = (intptr_t)0xb000ffebu;
		p->u60 = (intptr_t)pi;
		UI_OpenWindow(p, w);
		p->depth = w->depth;
	}
	return UI_OPEN;
}

static int window_update(Window *w, int layer) { // FUN_004285f0 -> FUN_0045de00
	uint32_t f = (uint32_t)w->u58;
	if (f & WINF_MODEL) return UI_OPEN;
	Win_SetTransform(w);
	if ((f & WINF_BORDER) && w->u60) draw_frame(w, layer);
	return UI_OPEN;
}

static int window_close(Window *w, int layer) { // FUN_00428370 (+ FUN_0045de60)
	(void)layer;
	xform_free((WinXform *)w->u64);
	w->u64 = 0;
	PaneInfo *pi = (PaneInfo *)w->u5c;
	if (pi) {
		if (pi->flags & 0x40) {
			HtmlInfo *h = pi->data;
			if (h) h->pad = 0;
		}
		pi->pad = 0;
		w->u5c = 0;
	}
	if (((uint32_t)w->u58 & WINF_MODEL) && w->u60) {
		free((void *)w->u60);
		w->u60 = 0;
	}
	return UI_CLOSED;
}

Window *WindowCreate(uint32_t flags, Window *parent) { // 0x4284b0
	Window *w = UI_AllocWindow();
	if (!w) return NULL;
	uint32_t f = flags;
	PaneInfo *pi = NULL;
	if (!(flags & WINF_MODEL)) {
		pi = pane_alloc();
		if (pi) {
			f |= 0x20;
			if (flags & WINF_TEXT) pi->flags |= 4;
			else if (flags & WINF_SPRITE) pi->flags |= 0x10;
			else if (flags & WINF_PICTURE) pi->flags |= 0x20;
			else if (flags & WINF_SECTION) pi->flags |= 0x80;
			else if (flags & WINF_HTML) {
				pi->data = html_alloc();
				f |= 0x22;
				pi->flags |= 0x40;
				memset(images, 0, sizeof(images));
			}
			if (f & WINF_BG) pi->flags |= 2;
			if ((flags & (WINF_SPRITE | WINF_PICTURE | WINF_SECTION)) && !pi->data) {
				pi->data = calloc(1, sizeof(UiSprite));
			}
		}
	}
	w->focused = 1;
	w->flags |= 0x401;
	w->open = window_open;
	w->update = window_update;
	w->close = window_close;
	w->u54 = (intptr_t)"Window title";
	w->u58 = (intptr_t)f;
	w->u5c = (intptr_t)pi;
	UI_OpenWindow(w, parent);
	return w;
}

void WindowDestroy(Window *w) { UI_CloseWindow(w); } // 0x428620

void WindowSetSize(Window *w, int32_t x, int32_t y, int32_t width, int32_t height) { // 0x428630
	if (!w) return;
	w->x = x; w->y = y; w->w = width; w->h = height;
	PaneInfo *pi = (PaneInfo *)w->u5c;
	if (!pi) return;
	int r[4], b[2];
	client_rect(w, r, b);
	if (!((uint32_t)w->u58 & WINF_MODEL)) {
		pi->w = r[2];
		pi->h = r[3];
		pi->smx = 0;
		pi->smy = 0;
	}
	// re-place the children made by the open function
	for (Window *c = w->child; c; c = c->next) {
		const char *n = (const char *)c->u54;
		if (!n) continue;
		if (!strncmp(n, "Title", 5)) { c->w = w->w - (b[0] < 1); c->x = b[0]; c->y = b[1]; }
		else if (!strncmp(n, "VScroll", 7)) { c->x = b[0] + r[2]; c->h = r[3]; c->y = b[1]; }
		else if (!strncmp(n, "Pane", 4)) { c->x = r[0]; c->y = r[1]; c->w = r[2]; c->h = r[3]; }
	}
}

void WindowSetRect(Window *w, const WinRect *r) { if (w && r) WindowSetSize(w, r->x, r->y, r->w, r->h); } // 0x428840

void WindowGetRect(Window *w, WinRect *r) { r->x = w->x; r->y = w->y; r->w = w->w; r->h = w->h; } // 0x428870

void WindowSetTitle(Window *w, const char *title) { if (w) w->u54 = (intptr_t)title; } // 0x428890

void WindowSetText(Window *w, const char *text) { // 0x4288f0: stores the pointer
	PaneInfo *pi = w ? (PaneInfo *)w->u5c : NULL;
	if (pi) pi->data = (void *)text;
}

void WindowSetTextFile(Window *w, int32_t record) { // 0x428980
	PaneInfo *pi = w ? (PaneInfo *)w->u5c : NULL;
	if (pi && (pi->flags & 0x40) && pi->data) {
		((HtmlInfo *)pi->data)->rec = record;
		memset(images, 0, sizeof(images));
	}
}

void WindowSetPicture(Window *w, int32_t tex, texfile_t *tf) { // 0x428920
	PaneInfo *pi = w ? (PaneInfo *)w->u5c : NULL;
	if (pi && pi->data) {
		UiSprite *s = pi->data;
		s->tf = tf;
		s->tex = tex;
		s->state = 0;
	}
}

void WindowSetBorderInfo(Window *w, struct BorderInfoRec *info) { // 0x4289c0
	(void)info;     // BorderInfo (the only border there is): the frame sprite of misc\global texture 5
	if (w && !((uint32_t)w->u58 & WINF_SECTION)) w->u60 = (intptr_t)&gUiBorder;
}

void WinAPI_DestroyVScroll(Window *w) { // 0x4287e0
	if (!w || !((uint32_t)w->u58 & WINF_VSCROLL)) return;
	for (Window *c = w->child; c; c = c->next) {
		const char *n = (const char *)c->u54;
		if (n && !strncmp(n, "VScroll", 7) && c->state < UI_CLOSING) {
			UI_CloseWindow(c);
			w->u58 &= ~(intptr_t)WINF_VSCROLL;
			return;
		}
	}
}

// model windows: the model node made at open holds {model, scale, flags, open time}; WindowSetModel
// fills in the model (FUN_0045f740 does not touch the open time); drawn by ui_draw_models
void WindowSetModel(Window *w, int32_t model, int32_t scale) { // 0x4288a0 -> FUN_0045f740
	if (!w || !((uint32_t)w->u58 & WINF_MODEL)) return;
	WinModel *m = (WinModel *)w->u60;
	if (!m) {
		w->u60 = (intptr_t)(m = model_node());
		w->mouse = model_mouse;
		w->flags |= UIF_STICKY;
	}
	m->model = model;
	m->scale = scale;
}

void WindowSetModelFlags(Window *w, int32_t flags) { // 0x4288d0 -> FUN_0045f730
	if (w && w->u60) ((WinModel *)w->u60)->flags = flags;
}

// ---------------------------------------------------------------- the scan-in (FUN_0045f200)
// For 4096 ms after the model window opened, a plane across the model's local y axis sweeps from
// y = 256 down to y = -256 (k = 256 - t*0x200/0x1000): the model is drawn solid above it (an extra clip
// plane n.p >= k for Model_Draw) and as a wire-frame below it (the negated plane; Pcp_DrawInstanceWire:
// every unique edge as an additive line in DAT_004992f4 = 0x004000, a dark green).

typedef struct { const model_t *m; int n; uint32_t *edges; } EdgeList;
static EdgeList edge_lists[8];

static const EdgeList *model_edges(const model_t *m) { // the unique edge list (Pcp_AddUniqueEdge 0x46f540)
	for (int i = 0; i < 8; i++) {
		if (edge_lists[i].m == m) return &edge_lists[i];
	}
	EdgeList *e = NULL;
	for (int i = 0; i < 8 && !e; i++) {
		if (!edge_lists[i].m) e = &edge_lists[i];
	}
	if (!e) {           // (only a handful of models are ever shown in windows)
		e = &edge_lists[0];
		free(e->edges);
	}
	e->m = m;
	e->n = 0;
	e->edges = malloc(sizeof(uint32_t) * (size_t)(m->num_tris * 3 + 1));
	for (int t = 0; t < m->num_tris; t++) {
		for (int k = 0; k < 3; k++) {
			int wa = m->tris[t].w[k], wb = m->tris[t].w[(k + 1) % 3];
			if (wa >= m->num_wedges || wb >= m->num_wedges) continue;
			uint32_t a = (uint16_t)m->wedges[wa].vertex, b = (uint16_t)m->wedges[wb].vertex;
			uint32_t key = b << 16 | a, rev = a << 16 | b;
			int j = 0;
			while (j < e->n && e->edges[j] != key && e->edges[j] != rev) j++;
			if (j == e->n) e->edges[e->n++] = key;
		}
	}
	return e;
}

// A copy of the model with every triangle cut to local y >= k (Model_Draw with the extra clip plane).
// New corners on the cut take the kept corner's normal and colour; the arrays it does not change are
// shared with the original.
static model_t *model_cut_above(const model_t *m, float k) {
	model_t *c = malloc(sizeof(model_t));
	*c = *m;
	int maxv = m->num_vertices + 2 * m->num_tris, maxw = m->num_wedges + 2 * m->num_tris;
	int maxl = m->num_litverts + 2 * m->num_tris;
	c->vertices = malloc(sizeof(vec3_t) * (size_t)(maxv + 1));
	c->wedges = malloc(sizeof(pcp_wedge_t) * (size_t)(maxw + 1));
	c->tris = malloc(sizeof(pcp_tri_t) * (size_t)(2 * m->num_tris + 1));
	c->litverts = m->num_litverts ? malloc(sizeof(pcp_litvert_t) * (size_t)(maxl + 1)) : m->litverts;
	memcpy(c->vertices, m->vertices, sizeof(vec3_t) * (size_t)m->num_vertices);
	memcpy(c->wedges, m->wedges, sizeof(pcp_wedge_t) * (size_t)m->num_wedges);
	if (m->num_litverts) memcpy(c->litverts, m->litverts, sizeof(pcp_litvert_t) * (size_t)m->num_litverts);
	c->num_tris = 0;
	for (int t = 0; t < m->num_tris; t++) {
		const pcp_tri_t *tr = &m->tris[t];
		int in[3], nin = 0;
		float d[3];
		bool ok = true;
		for (int i = 0; i < 3; i++) {
			if (tr->w[i] >= m->num_wedges) ok = false;
			const pcp_wedge_t *w = &m->wedges[tr->w[i] < m->num_wedges ? tr->w[i] : 0];
			int vi = w->vertex >= 0 && w->vertex < m->num_vertices ? w->vertex : 0;
			d[i] = m->vertices[vi].y - k;
			in[i] = d[i] >= 0;
			nin += in[i];
		}
		if (!ok || nin == 0) continue;
		if (nin == 3) {
			c->tris[c->num_tris++] = *tr;
			continue;
		}
		// Sutherland-Hodgman on the triangle's corners (order kept: clockwise front faces)
		uint16_t poly[4];
		int np = 0;
		for (int i = 0; i < 3; i++) {
			int j = (i + 1) % 3;
			if (in[i]) poly[np++] = tr->w[i];
			if (in[i] != in[j]) {
				const pcp_wedge_t *a = &m->wedges[tr->w[i]], *b = &m->wedges[tr->w[j]];
				const pcp_wedge_t *keep = in[i] ? a : b;
				float f = d[i] / (d[i] - d[j]);
				int va = a->vertex >= 0 && a->vertex < m->num_vertices ? a->vertex : 0;
				int vb = b->vertex >= 0 && b->vertex < m->num_vertices ? b->vertex : 0;
				int nv = c->num_vertices++, nw = c->num_wedges++;
				c->vertices[nv] = vec3_add(m->vertices[va], vec3_mulf(vec3_sub(m->vertices[vb], m->vertices[va]), f));
				pcp_wedge_t w = *keep;
				w.vertex = (int16_t)nv;
				w.u = a->u + (b->u - a->u) * f;
				w.v = a->v + (b->v - a->v) * f;
				if (m->num_litverts && keep->lit >= 0 && keep->lit < m->num_litverts) {
					int nl = c->num_litverts++;
					c->litverts[nl] = m->litverts[keep->lit];
					c->litverts[nl].vertex = nv;
					w.lit = nl;
				}
				c->wedges[nw] = w;
				poly[np++] = (uint16_t)nw;
			}
		}
		for (int i = 1; i + 1 < np; i++) {
			pcp_tri_t n = *tr;
			n.w[0] = poly[0];
			n.w[1] = poly[i];
			n.w[2] = poly[i + 1];
			c->tris[c->num_tris++] = n;
		}
	}
	return c;
}

static void model_cut_free(model_t *c, const model_t *m) {
	free(c->vertices);
	free(c->wedges);
	free(c->tris);
	if (c->litverts != m->litverts) free(c->litverts);
	free(c);
}

// The wire-frame below the plane: each unique edge cut to y <= k, drawn as a one-pixel band facing the
// camera (additive, depth-tested against the solid part like the D3D line list).
static void model_wire_below(const model_t *m, mat4_t t, float k, float h) {
	const EdgeList *e = model_edges(m);
	static vertex_t v[6 * 64];
	int nv = 0;
	render_set_model(mat4_identity());
	render_set_depth(true, false);
	render_set_cull(CULL_NONE);
	rgba_t col = rgba(0x00, 0x40, 0x00, 255);
	for (int i = 0; i <= e->n; i++) {
		if (nv + 6 > (int)len(v) || (i == e->n && nv)) {
			render_tris(v, nv, NULL, BLEND_ADD);
			nv = 0;
		}
		if (i == e->n) break;
		int ia = (int)(e->edges[i] & 0xffff), ib = (int)(e->edges[i] >> 16);
		if (ia >= m->num_vertices || ib >= m->num_vertices) continue;
		vec3_t a = m->vertices[ia], b = m->vertices[ib];
		if (a.y > k && b.y > k) continue;
		if (a.y > k) a = vec3_add(b, vec3_mulf(vec3_sub(a, b), (k - b.y) / (a.y - b.y)));
		else if (b.y > k) b = vec3_add(a, vec3_mulf(vec3_sub(b, a), (k - a.y) / (b.y - a.y)));
		vec3_t wa = mat4_mul_point(t, a), wb = mat4_mul_point(t, b);
		vec3_t side = vec3_cross(vec3_sub(wb, wa), vec3_mulf(vec3_add(wa, wb), 0.5f));
		float sl = vec3_len(side);
		if (sl <= 0 || wa.z <= 1 || wb.z <= 1) continue;
		side = vec3_mulf(side, 1.0f / sl);
		vec3_t sa = vec3_mulf(side, 0.5f * wa.z / h), sb = vec3_mulf(side, 0.5f * wb.z / h);
		vec3_t q[4] = {vec3_sub(wa, sa), vec3_add(wa, sa), vec3_add(wb, sb), vec3_sub(wb, sb)};
		static const int idx[6] = {0, 1, 2, 0, 2, 3};
		for (int j = 0; j < 6; j++) v[nv++] = (vertex_t){q[idx[j]], 0, 0, col};
	}
	render_set_depth(true, true);
	render_set_cull(CULL_BACK);
}

static void draw_model_windows(Window *w) {
	for (; w; w = w->next) {
		if (w->used && w->state == UI_OPEN && ((uint32_t)w->u58 & WINF_MODEL) && w->u60) {
			WinModel *m = (WinModel *)w->u60;
			const model_t *mdl = gUiModels && m->model >= 0 && m->model < gUiModels->count &&
				gUiModels->entries[m->model].num_parts ? gUiModels->entries[m->model].parts[0] : NULL;
			if (mdl) {
				int x, y;
				UI_GetScreenPos(w, &x, &y);
				// FUN_0045f0a0: the parent node on the window centre's view ray at z (the scale),
				// x = -((cx - 320) * z / H), y = -((cy - 240) * z / H) (the front-end camera is Rz(0x800)
				// at the origin, so the world here is the rewrite's), turned to look at the camera
				// (FUN_004319e0: its z axis towards the eye - Ry(180) for a centred window); the instance
				// in it turns Rx(pitch) * Ry(yaw) (FUN_0045f570, the node's mouse callback, every frame)
				int32_t H = proj_h(), z = m->scale + m->zoom;
				int32_t cx = x + (w->w >> 1), cy = y + (w->h >> 1);
				int32_t px = -(((cx - (render_2d_width() >> 1)) * z) / H), py = -(((cy - (render_2d_height() >> 1)) * z) / H);
				cam_view_t look;
				memset(&look, 0, sizeof(look));
				cam_view_look_dir(&look, (cvec3_t){-px, -py, -z});
				mat4_t lm = mat4_identity();
				for (int r = 0; r < 3; r++) {
					for (int c = 0; c < 3; c++) lm.m[c * 4 + r] = look.mat.m[r][c] * (1.0f / 4096.0f);
				}
				mat4_t inst = m->turned ? mat4_mul(mat4_rotate_x((float)m->pitch * 2 * PI / 4096.0f),
					mat4_rotate_y((float)m->yaw * 2 * PI / 4096.0f)) : mat4_identity();
				mat4_t t = mat4_mul(mat4_translate(vec3((float)px, (float)py, (float)z)),
					mat4_mul(lm, mat4_mul(mat4_translate(vec3((float)m->tx, (float)m->ty, 0)), inst)));
				// Lights_SetupDefault3 0x415b50: three directional lights on the front-end camera (Rz(0x800):
				// camera-local (x, y, z) is world (-x, -y, z)): (0,0,5000) 350 white, (-5000,5000,1000)
				// (200,170,110), (5000,5000,1000) (80,80,200); no ambient (DAT_0049cce0 is never set)
				lighting_t light = {0};
				light.num_dir = 3;
				light.dir[0] = vec3(0, 0, 1);
				light.dir_color[0] = vec3(350 / 255.0f, 350 / 255.0f, 350 / 255.0f);
				light.dir[1] = vec3_normalize(vec3(5000, -5000, 1000));
				light.dir_color[1] = vec3(200 / 255.0f, 170 / 255.0f, 110 / 255.0f);
				light.dir[2] = vec3_normalize(vec3(-5000, -5000, 1000));
				light.dir_color[2] = vec3(80 / 255.0f, 80 / 255.0f, 200 / 255.0f);
				render_set_scissor_2d(x, y, x + w->w, y + w->h);
				uint32_t age = Clock_Now() - m->opened;
				if (age < 0x1000) {    // the scan-in
					float k = (float)(0x100 - (int32_t)(age * 0x200) / 0x1000);
					model_t *solid = model_cut_above(mdl, k);
					model_draw(solid, t, gGlobalTextures, &light);
					model_cut_free(solid, mdl);
					model_wire_below(mdl, t, k, (float)H);
				} else {
					model_draw(mdl, t, gGlobalTextures, &light);
				}
				render_reset_scissor();
			}
		}
		if (w->child) draw_model_windows(w->child);
	}
}

void ui_draw_models(void) {
	if (!gUIRoot) return;
	render_set_projection(2 * atanf((render_2d_height() * 0.5f) / (float)proj_h()), 10, 100000);   // sy = 240 + y * H / z
	render_set_view(mat4_look_at(vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)));
	render_set_depth(true, true);
	render_set_fog(false, rgba(0, 0, 0, 0), 0, 1);
	draw_model_windows(gUIRoot);
}

// ================================================================ text files and the in-game UI

WinRect rectHTML = {216, 144, 345, 223};
Render2DFn g2dRenderCallback;
int32_t gWindowWidth = 640, gWindowHeight = 480;
static uint8_t *text_file;

void ui_free_text(void) {
	free(text_file);
	text_file = NULL;
	text_data = NULL;
	text_size = text_count = text_table = 0;
}

bool ui_load_text(const char *path) {
	ui_free_text();
	uint32_t size;
	uint8_t *d = fs_load(path, &size);
	if (!d || size < 12) {
		free(d);
		return false;
	}
	text_file = d;
	text_data = d;
	text_size = size;
	text_count = get_u32(d + 4);
	text_table = get_u32(d + 8);
	return true;
}

void ui_load_mission_text(const char *mission_dir, bool ingame) { // MissionText_Load / Text_Select
	static const char *suffix[3] = {"", "F", "G"};
	char path[256];
	int lang = gLanguage >= 0 && gLanguage < 3 ? gLanguage : 0;
	snprintf(path, sizeof(path), "%s\\%sTEXT%s.DAT", mission_dir, ingame ? "G" : "", suffix[lang]);
	if (ui_load_text(path)) return;
	if (lang) {
		snprintf(path, sizeof(path), "%s\\%sTEXT.DAT", mission_dir, ingame ? "G" : "");
		if (ui_load_text(path)) return;
	}
	if (ingame) {
		snprintf(path, sizeof(path), "%s\\GAMETEXT.DAT", mission_dir);
		ui_load_text(path);
	}
}

static void ui_init_resources(void) {
	if (!gGlobalTextures) gGlobalTextures = texfile_load("MISC\\GLOBAL\\PCPURE\\GLOBAL");
	if (!gStandardTextures) gStandardTextures = texfile_load("MISC\\STANDARD\\PCPURE\\STANDARD");
	border_sprite.tf = gGlobalTextures;
	vscroll_sprite.tf = gGlobalTextures;
	gUiBorder.rects = border_rects;     // Hud_LoadBorderSprite_ 0x409070
	gUiBorder.sprite = &border_sprite;
}

void ui_reset(void) { // per mission (Mission_Run: UI_OpenWindow(HUD root) after the pools reset)
	ui_init_resources();
	UI_InitPool();
	memset(pane_pool, 0, sizeof(pane_pool));
	memset(html_pool, 0, sizeof(html_pool));
	g2dRenderCallback = NULL;
}

void ui_draw(void) { // the in-game UI: UI_UpdateDraw(gOT + 0x50) between the 3D scene and the HUD
	ui_input_frame();
	ui_draw_begin();
	UI_UpdateDraw(10);
	sprite_set_transform(NULL);   // FwSpriteSetTransform(0) after UI_UpdateDraw (Mission_Run 0x414e6b)
	ui_draw_end();
}
