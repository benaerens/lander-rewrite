// Software renderer: homogeneous clipping, watertight scanline rasterizer with
// perspective-correct attributes, float depth buffer, nearest/bilinear texturing with the
// original's CLAMP addressing (WRAP on request), per-vertex fog as Fog_VertexSpecular computes it
// and the blend modes used by the original D3D6 renderer.
//
// Drawing can be deferred (render_defer, the game's frames): every render_tris / render_quad_2d call
// is stored with the state it was made with, and the frame is drawn when deferring ends (or its
// pixels are asked for). The stored frame is set up once (transform, clip, project, cull, binning
// into bands of rows) and then rasterised band by band by several threads; each band runs its
// triangles in order, so each pixel sees exactly the sequence of operations of an immediate draw
// (the picture does not depend on the thread count).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "render.h"

// threads where pthreads are known to exist; -DRENDER_NO_THREADS draws in the caller only
#if (defined(__unix__) || defined(__APPLE__) || defined(_WIN32)) && !defined(RENDER_NO_THREADS)
#define RENDER_THREADS 1
#include <pthread.h>
#include <stdatomic.h>
#ifdef _WIN32
int pthread_num_processors_np(void);
#else
#include <unistd.h>
#endif
#endif

typedef struct {
	float x, y, z, w; // clip space
	float u, v;
	float r, g, b, a; // 0..255
	float fog;        // 1 = clear, 0 = fully fogged
} cvert_t;

typedef struct {
	float x, y, z, iw; // screen x/y, depth z/w, 1/w
	float u, v, r, g, b, a, fog; // already multiplied by 1/w
} svert_t;

#define NUM_ATTR 8 // iw, u, v, r, g, b, a, fog (all perspective-correct)

// everything a draw reads
typedef struct {
	mat4_t mvp;
	bool fog_enabled, depth_test, depth_write, bilinear, wrap, mirror;
	rgba_t fog_color;
	float fog_sn, fog_sf, fog_inv; // Fog_SetRange 0x437bd0: 1 - 10/start, 1 - 10/end, 1 / (sf - sn)
	cull_t cull;
	int sx0, sy0, sx1, sy1;        // scissor
} dstate_t;

static struct {
	int width, height;
	uint32_t *color;
	float *depth;
	mat4_t projection, view, model;
	mat4_t mv;
	float fog_start, fog_end;
	float aspect;
	float scale2d, ox2d, oy2d;     // the 2D layers: pixels = (units + offset) * scale
	dstate_t s;                    // the current state
} R;

static void update_matrices(void) {
	R.mv = mat4_mul(R.view, R.model);
	R.s.mvp = mat4_mul(R.projection, R.mv);
}

// ---------------------------------------------------------------- the command list

typedef struct {
	int kind;               // 0 = render_tris, 1 = render_quad_2d
	dstate_t s;
	const texture_t *tex;
	blend_t blend;
	int first, count;       // vertices of the record buffer
	float quad[8];          // render_quad_2d: x, y, w, h, u0, v0, u1, v1
	rgba_t quad_color;
} rec_cmd_t;

static struct {
	bool frame;             // the frame is being stored (deferred drawing)
	bool nested;            // render_record_begin .. end
	int mark0, mark1;       // the nested range
	rec_cmd_t *cmd;
	int ncmd, cap_cmd;
	vertex_t *vert;
	int nvert, cap_vert;
} REC;

static bool storing(void) { return REC.frame || REC.nested; }

static rec_cmd_t *rec_push(int kind, const texture_t *tex, blend_t blend) {
	if (REC.ncmd == REC.cap_cmd) {
		REC.cap_cmd = REC.cap_cmd ? REC.cap_cmd * 2 : 4096;
		REC.cmd = realloc(REC.cmd, sizeof(rec_cmd_t) * REC.cap_cmd);
	}
	rec_cmd_t *c = &REC.cmd[REC.ncmd++];
	c->kind = kind;
	c->s = R.s;
	c->tex = tex;
	c->blend = blend;
	c->first = REC.nvert;
	c->count = 0;
	return c;
}

static void run_commands(int first, int last);
static void flush(void);

void render_record_begin(void) {
	if (!REC.frame) {
		REC.ncmd = REC.nvert = 0;
	}
	REC.nested = true;
	REC.mark0 = REC.ncmd;
}

void render_record_end(void) {
	REC.nested = false;
	REC.mark1 = REC.ncmd;
}

bool render_recording(void) { return REC.nested; }

// the nested range is drawn after what came since render_record_end
void render_replay(void) {
	if (!REC.frame) { // immediate drawing: draw it now
		run_commands(REC.mark0, REC.mark1);
		REC.ncmd = REC.nvert = 0;
		return;
	}
	int a = REC.mark0, b = REC.mark1, n = REC.ncmd - b;
	if (b == a || n == 0) return;
	rec_cmd_t *tmp = malloc(sizeof(rec_cmd_t) * (b - a));
	memcpy(tmp, REC.cmd + a, sizeof(rec_cmd_t) * (b - a));
	memmove(REC.cmd + a, REC.cmd + b, sizeof(rec_cmd_t) * n);
	memcpy(REC.cmd + a + n, tmp, sizeof(rec_cmd_t) * (b - a));
	free(tmp);
}

// ---------------------------------------------------------------- state

void render_init(int width, int height) {
	R.width = width;
	R.height = height;
	R.color = malloc(sizeof(uint32_t) * width * height);
	R.depth = malloc(sizeof(float) * width * height);
	R.aspect = (float)width / (float)height;
	R.projection = mat4_perspective(60 * PI / 180, R.aspect, 16, 65536);
	R.view = mat4_identity();
	R.model = mat4_identity();
	R.scale2d = 1;
	R.ox2d = R.oy2d = 0;
	R.s.depth_test = R.s.depth_write = true;
	R.s.cull = CULL_BACK;
	R.s.bilinear = true;
	render_set_fog(false, rgba(0, 0, 0, 255), 9600, 12800);
	render_reset_scissor();
	update_matrices();
}

void render_resize(int width, int height) {
	if (width == R.width && height == R.height) return;
	flush();
	R.width = width;
	R.height = height;
	free(R.color);
	free(R.depth);
	R.color = calloc((size_t)width * height, sizeof(uint32_t));
	R.depth = malloc(sizeof(float) * width * height);
	R.aspect = (float)width / (float)height;
	R.projection = mat4_perspective(60 * PI / 180, R.aspect, 16, 65536);
	render_reset_scissor();
	update_matrices();
}

void render_set_scissor(int x0, int y0, int x1, int y1) {
	R.s.sx0 = clamp(x0, 0, R.width);
	R.s.sy0 = clamp(y0, 0, R.height);
	R.s.sx1 = clamp(x1, 0, R.width);
	R.s.sy1 = clamp(y1, 0, R.height);
}

void render_set_2d_scale(float scale) { R.scale2d = scale > 0 ? scale : 1; }
void render_set_2d_offset(float ox, float oy) { R.ox2d = ox; R.oy2d = oy; }
float render_2d_scale(void) { return R.scale2d; }
int render_2d_width(void) { return (int)(R.width / R.scale2d + 0.5f); }
int render_2d_height(void) { return (int)(R.height / R.scale2d + 0.5f); }

static int px2d(float v, float o) { return (int)floorf((v + o) * R.scale2d + 0.5f); }

void render_set_scissor_2d(int x0, int y0, int x1, int y1) {
	render_set_scissor(px2d((float)x0, R.ox2d), px2d((float)y0, R.oy2d), px2d((float)x1, R.ox2d), px2d((float)y1, R.oy2d));
}

void render_reset_scissor(void) {
	R.s.sx0 = R.s.sy0 = 0;
	R.s.sx1 = R.width;
	R.s.sy1 = R.height;
}

mat4_t render_projection(void) { return R.projection; }

void render_cleanup(void) {
	flush();
	free(R.color);
	free(R.depth);
	R.color = NULL;
	R.depth = NULL;
	REC.frame = false;
}

int render_width(void) { return R.width; }
int render_height(void) { return R.height; }

uint32_t *render_pixels(void) {
	flush();
	return R.color;
}

// LANDER_RENDER_FPS=1 (test aid): at exit, the frames since the first render_frame_begin per second of
// wall time (the loading before the first frame is not counted)
static struct timespec fps_t0;
static long fps_frames;

static void fps_report(void) {
	struct timespec t;
	timespec_get(&t, TIME_UTC);
	double s = (double)(t.tv_sec - fps_t0.tv_sec) + (t.tv_nsec - fps_t0.tv_nsec) * 1e-9;
	if (fps_frames > 1 && s > 0) {
		fprintf(stderr, "render fps: %ld frames in %.2f s = %.1f fps\n", fps_frames - 1, s, (fps_frames - 1) / s);
	}
}

void render_frame_begin(rgba_t clear) {
	if (fps_frames++ == 0 && getenv("LANDER_RENDER_FPS")) {
		timespec_get(&fps_t0, TIME_UTC);
		atexit(fps_report);
	}
	flush();
	uint32_t c = 0xff000000u | (clear.r << 16) | (clear.g << 8) | clear.b;
	int n = R.width * R.height;
	for (int i = 0; i < n; i++) {
		R.color[i] = c;
		R.depth[i] = 1.0f;
	}
}

void render_defer(bool on) {
	if (!on) flush();
	REC.frame = on;
}

void render_set_projection(float fov_y, float near, float far) {
	R.projection = mat4_perspective(fov_y, R.aspect, near, far);
	update_matrices();
}

void render_set_projection_matrix(mat4_t projection) {
	R.projection = projection;
	update_matrices();
}

void render_set_view(mat4_t view) {
	R.view = view;
	// a view of mat4_look_at turns the right-handed world into the left-handed camera space
	// (determinant -1); a positive determinant is a reflected view
	const float *m = view.m;
	float det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
	R.s.mirror = det > 0;
	update_matrices();
}
mat4_t render_view(void) { return R.view; }
bool render_view_mirrored(void) { return R.s.mirror; }
void render_set_model(mat4_t model) { R.model = model; update_matrices(); }

void render_set_fog(bool enabled, rgba_t color, float start, float end) {
	R.s.fog_enabled = enabled;
	R.s.fog_color = color;
	R.fog_start = start > 10 ? start : 10;
	R.fog_end = end > R.fog_start ? end : R.fog_start + 1;
	R.s.fog_sn = 1.0f - 10.0f / R.fog_start;
	R.s.fog_sf = 1.0f - 10.0f / R.fog_end;
	R.s.fog_inv = 1.0f / (R.s.fog_sf - R.s.fog_sn);
}

void render_set_depth(bool test, bool write) { R.s.depth_test = test; R.s.depth_write = write; }
void render_set_cull(cull_t cull) { R.s.cull = cull; }
void render_set_filter(bool bilinear) { R.s.bilinear = bilinear; }
void render_set_wrap(bool wrap) { R.s.wrap = wrap; }

// Fog_VertexSpecular 0x4740a0: the vertex's specular alpha (255 = clear) from its screen z
// sz = 1 - 10/z: 255 up to the fog start, ftol((1 - (sz - sn) / (sf - sn)) * 255) to the end, 0 beyond
// (linear in 1/z, not in z)
static inline float fog_factor(const dstate_t *st, float z) {
	if (z <= 10) return 1;
	float sz = 1.0f - 10.0f / z;
	if (sz <= st->fog_sn) return 1;
	if (sz >= st->fog_sf) return 0;
	return (float)(int)((1.0f - (sz - st->fog_sn) * st->fog_inv) * 255.0f) * (1.0f / 255.0f);
}

// ---------------------------------------------------------------- texturing

#define INLINE static inline __attribute__((always_inline))

INLINE int ifloor(float f) { // floorf for the texel range, without the libm call
	int i = (int)f;
	return f < (float)i ? i - 1 : i;
}

INLINE int address(int i, int n, bool wrap) {
	if (!wrap) return i < 0 ? 0 : i >= n ? n - 1 : i;
	if ((unsigned)i < (unsigned)n) return i;
	i %= n;
	return i < 0 ? i + n : i;
}

INLINE uint32_t sample_nearest(const texture_t *t, float u, float v, bool wrap) {
	int x = address(ifloor(u * t->width), t->width, wrap);
	int y = address(ifloor(v * t->height), t->height, wrap);
	return t->pixels[y * t->width + x];
}

INLINE uint32_t lerp_px(uint32_t a, uint32_t b, int f) { // f in 0..256
	uint32_t rb = (((a & 0x00ff00ff) * (256 - f) + (b & 0x00ff00ff) * f) >> 8) & 0x00ff00ff;
	uint32_t ag = ((((a >> 8) & 0x00ff00ff) * (256 - f) + ((b >> 8) & 0x00ff00ff) * f)) & 0xff00ff00;
	return rb | ag;
}

INLINE uint32_t bilinear_px(const uint32_t *px, int w, int h, float u, float v, bool wrap) {
	float fx = u * w - 0.5f, fy = v * h - 0.5f;
	int ix = ifloor(fx), iy = ifloor(fy);
	int tx = (int)((fx - (float)ix) * 256), ty = (int)((fy - (float)iy) * 256);
	int x0 = address(ix, w, wrap), y0 = address(iy, h, wrap);
	int x1 = address(ix + 1, w, wrap), y1 = address(iy + 1, h, wrap);
	const uint32_t *r0 = px + y0 * w, *r1 = px + y1 * w;
	uint32_t top = lerp_px(r0[x0], r0[x1], tx);
	uint32_t bot = lerp_px(r1[x0], r1[x1], tx);
	return lerp_px(top, bot, ty);
}

INLINE uint32_t sample_bilinear(const texture_t *t, float u, float v, bool wrap) {
	return bilinear_px(t->pixels, t->width, t->height, u, v, wrap);
}

// log2 from the float's bits (exponent + a quadratic fit of the mantissa, within 0.01)
INLINE float fast_log2(float x) {
	union { float f; uint32_t i; } b = {x};
	float m = (float)(b.i & 0x7fffff) * (1.0f / 8388608.0f);
	return (float)((int)(b.i >> 23) - 127) + m * (1.3465f - 0.3465f * m);
}

// trilinear (D3DTFN_LINEAR / D3DTFP_LINEAR, Begin3DStates with "Filtering" and "Mip Mapping"): the
// level of detail from the texel footprint of the pixel, rho2 = its squared length in level-0 texels
INLINE uint32_t sample_trilinear(const texture_t *t, float u, float v, float rho2, bool wrap) {
	if (rho2 <= 1.0f) return bilinear_px(t->pixels, t->width, t->height, u, v, wrap); // magnified
	float lod = 0.5f * fast_log2(rho2);
	int l = (int)lod;
	int f = (int)((lod - (float)l) * 256);
	if (l >= t->mip_levels) return t->mip_pixels[t->mip_offset[t->mip_levels]]; // 1x1
	const uint32_t *pa = l ? t->mip_pixels + t->mip_offset[l] : t->pixels;
	int wa = t->width >> l, wb = wa >> 1;
	uint32_t ca = bilinear_px(pa, wa, wa, u, v, wrap);
	uint32_t cb = bilinear_px(t->mip_pixels + t->mip_offset[l + 1], wb, wb, u, v, wrap);
	return lerp_px(ca, cb, f);
}

// FUN_0043a910 (RGB) / FUN_0043aed0 (RGBA): each level the 2x2 average of the one above. A colour-keyed
// texture (alpha 0 only on black texels) averages its colours and keys the levels' black texels again.
static void build_mips(texture_t *t) {
	int w = t->width;
	if (t->mip_levels || !t->mip_ok || w != t->height || w < 2 || (w & (w - 1)) || w > 512) {
		t->mip_ok = false;
		return;
	}
	bool keyed = true;
	for (int i = 0; i < w * w && keyed; i++) {
		uint32_t c = t->pixels[i];
		keyed = (c >> 24) == 0xff || c == 0;
	}
	int levels = 0, total = 0;
	for (int s = w >> 1; s >= 1; s >>= 1) {
		t->mip_offset[++levels] = total;
		total += s * s;
	}
	t->mip_pixels = malloc(sizeof(uint32_t) * total);
	const uint32_t *src = t->pixels;
	for (int k = 1, s = w >> 1; k <= levels; k++, s >>= 1) {
		uint32_t *dst = t->mip_pixels + t->mip_offset[k];
		int ss = s * 2;
		for (int y = 0; y < s; y++) {
			for (int x = 0; x < s; x++) {
				uint32_t a = src[(2 * y) * ss + 2 * x], b = src[(2 * y) * ss + 2 * x + 1];
				uint32_t c = src[(2 * y + 1) * ss + 2 * x], d = src[(2 * y + 1) * ss + 2 * x + 1];
				uint32_t o = 0;
				for (int sh = 0; sh < 32; sh += 8) {
					o |= ((((a >> sh) & 0xff) + ((b >> sh) & 0xff) + ((c >> sh) & 0xff) + ((d >> sh) & 0xff)) >> 2) << sh;
				}
				if (keyed) o = (o & 0xffffff) ? (o | 0xff000000u) : 0;
				dst[y * s + x] = o;
			}
		}
		src = dst;
	}
	t->mip_levels = levels;
}

void render_texture_free_mips(texture_t *t) {
	if (!t) return;
	free(t->mip_pixels);
	t->mip_pixels = NULL;
	t->mip_levels = 0;
}

// ---------------------------------------------------------------- spans

typedef struct {
	float iw, u, v, r, g, b, a, fog, z;
} attr_t;

typedef struct {
	attr_t dx, dy;          // the attributes' steps per pixel across and per row down
} grad_t;

// One span of a triangle; the flags are constants in the specialised copies below, so each copy
// keeps only the work its state needs (the same arithmetic in every copy).
INLINE void span(uint32_t *cp, float *zp, int n, attr_t a, const grad_t *gr, const texture_t *tex, blend_t blend,
	bool textured, bool bilinear, bool wrap, bool fog, bool ztest, bool zwrite, uint32_t fogc) {
	bool need_alpha = blend == BLEND_ALPHA_TEST || blend == BLEND_ALPHA || blend == BLEND_ADD;
	const attr_t *d = &gr->dx;
	bool mip = textured && bilinear && tex->mip_levels > 0;
	float tsize2 = mip ? (float)tex->width * (float)tex->width : 0;
	for (int px = 0; px < n; px++) {
		float z = a.z;
		if (!ztest || z <= zp[px]) {
			float w = 1.0f / a.iw;
			uint32_t t = 0xffffffffu;
			int al = 255;
			if (need_alpha) {
				al = (int)(a.a * w);
				al = clamp(al, 0, 255);
			}
			if (textured) {
				float u = a.u * w, v = a.v * w;
				if (mip) {
					// d(U/W)/dx = (dU/dx - u dW/dx) / W with W = 1/w
					float dux = (gr->dx.u - u * gr->dx.iw) * w, dvx = (gr->dx.v - v * gr->dx.iw) * w;
					float duy = (gr->dy.u - u * gr->dy.iw) * w, dvy = (gr->dy.v - v * gr->dy.iw) * w;
					float rx = dux * dux + dvx * dvx, ry = duy * duy + dvy * dvy;
					t = sample_trilinear(tex, u, v, (rx > ry ? rx : ry) * tsize2, wrap);
				} else {
					t = bilinear ? sample_bilinear(tex, u, v, wrap) : sample_nearest(tex, u, v, wrap);
				}
				if (need_alpha) al = ((t >> 24) * (al + 1)) >> 8;
			}
			if (blend != BLEND_ALPHA_TEST || al >= 128) {
				int r = (int)(a.r * w), g = (int)(a.g * w), b = (int)(a.b * w);
				r = clamp(r, 0, 255); g = clamp(g, 0, 255); b = clamp(b, 0, 255);
				if (textured) {
					r = (((t >> 16) & 0xff) * (r + 1)) >> 8;
					g = (((t >> 8) & 0xff) * (g + 1)) >> 8;
					b = ((t & 0xff) * (b + 1)) >> 8;
				}
				uint32_t src = (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b;
				if (fog) {
					float f = a.fog * w;
					int fi = (int)(clamp(f, 0.0f, 1.0f) * 256);
					src = lerp_px(fogc, src, fi);
				}
				if (blend == BLEND_ALPHA) {
					src = lerp_px(cp[px], src, al + (al >> 7));
				} else if (blend == BLEND_ADD || blend == BLEND_ONE_ONE) {
					uint32_t dst = cp[px];
					int f = blend == BLEND_ADD ? al + (al >> 7) : 256;
					int sr = (((src >> 16) & 0xff) * f) >> 8, sg = (((src >> 8) & 0xff) * f) >> 8, sb = ((src & 0xff) * f) >> 8;
					int dr = ((dst >> 16) & 0xff) + sr, dg = ((dst >> 8) & 0xff) + sg, db = (dst & 0xff) + sb;
					src = (uint32_t)(min(dr, 255) << 16) | (uint32_t)(min(dg, 255) << 8) | (uint32_t)min(db, 255);
				}
				cp[px] = 0xff000000u | src;
				if (zwrite) {
					zp[px] = z;
				}
			}
		}
		a.iw += d->iw; a.u += d->u; a.v += d->v; a.r += d->r; a.g += d->g; a.b += d->b; a.a += d->a;
		a.fog += d->fog; a.z += d->z;
	}
}

typedef void (*span_fn)(const dstate_t *, uint32_t *, float *, int, attr_t, const grad_t *, const texture_t *, blend_t,
	uint32_t);

// a copy for every blend mode x textured (clamped / wrapped) x fog x depth test x depth write (bilinear)
#define SPAN(name, BL, TX, WR, FOG, ZT, ZW) \
	static void name(const dstate_t *st, uint32_t *cp, float *zp, int n, attr_t a, const grad_t *d, const texture_t *t, \
		blend_t bl, uint32_t fogc) { \
		(void)st; (void)bl; \
		span(cp, zp, n, a, d, t, BL, TX, true, WR, FOG, ZT, ZW, fogc); \
	}
#define SPAN_Z(name, BL, TX, WR, FOG) \
	SPAN(name##_00, BL, TX, WR, FOG, false, false) SPAN(name##_01, BL, TX, WR, FOG, false, true) \
	SPAN(name##_10, BL, TX, WR, FOG, true, false) SPAN(name##_11, BL, TX, WR, FOG, true, true)
#define SPAN_F(name, BL, TX, WR) SPAN_Z(name##_n, BL, TX, WR, false) SPAN_Z(name##_f, BL, TX, WR, true)
#define SPAN_T(name, BL) SPAN_F(name##_u, BL, false, false) SPAN_F(name##_t, BL, true, false) SPAN_F(name##_w, BL, true, true)
SPAN_T(sp_opaque, BLEND_OPAQUE)
SPAN_T(sp_test, BLEND_ALPHA_TEST)
SPAN_T(sp_alpha, BLEND_ALPHA)
SPAN_T(sp_add, BLEND_ADD)
SPAN_T(sp_one, BLEND_ONE_ONE)

#define ROW_Z(name) name##_00, name##_01, name##_10, name##_11
#define ROW_F(name) ROW_Z(name##_n), ROW_Z(name##_f)
#define ROW_T(name) ROW_F(name##_u), ROW_F(name##_t), ROW_F(name##_w)
// index ((blend * 3 + untextured / clamped / wrapped) * 2 + fog) * 4 + depth test * 2 + depth write
static const span_fn spans[5 * 24] = {ROW_T(sp_opaque), ROW_T(sp_test), ROW_T(sp_alpha), ROW_T(sp_add), ROW_T(sp_one)};

// nearest sampling: one copy with the states as variables
static void span_generic_fog(const dstate_t *st, uint32_t *cp, float *zp, int n, attr_t a, const grad_t *d,
	const texture_t *t, blend_t bl, uint32_t fogc) {
	span(cp, zp, n, a, d, t, bl, t != NULL, st->bilinear, st->wrap, true, st->depth_test, st->depth_write, fogc);
}

static void span_generic(const dstate_t *st, uint32_t *cp, float *zp, int n, attr_t a, const grad_t *d,
	const texture_t *t, blend_t bl, uint32_t fogc) {
	span(cp, zp, n, a, d, t, bl, t != NULL, st->bilinear, st->wrap, false, st->depth_test, st->depth_write, fogc);
}

// fog: the triangle is fogged (the state has fog and not every vertex is clear)
static span_fn pick_span(const dstate_t *st, const texture_t *tex, blend_t blend, bool fog) {
	if ((tex && !st->bilinear) || (unsigned)blend > BLEND_ONE_ONE) {
		return fog ? span_generic_fog : span_generic;
	}
	int tx = !tex ? 0 : st->wrap ? 2 : 1;
	return spans[((blend * 3 + tx) * 2 + fog) * 4 + st->depth_test * 2 + st->depth_write];
}

// ---------------------------------------------------------------- rasterizer

// rows [y0, y1) of the screen (the scissor, cut to a band when the frame is drawn in bands)
static void raster_tri(const dstate_t *st, int y0, int y1, const svert_t *v0, const svert_t *v1, const svert_t *v2,
	const texture_t *tex, blend_t blend, span_fn fn) {
	float ymin = min(v0->y, min(v1->y, v2->y)), ymax = max(v0->y, max(v1->y, v2->y));
	int py0 = (int)ceilf(ymin - 0.5f);
	int py1 = (int)ceilf(ymax - 0.5f) - 1;
	if (py0 < y0) py0 = y0;
	if (py1 > y1 - 1) py1 = y1 - 1;
	if (py0 > py1) {
		return;
	}
	float area = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
	if (area < 0) { // make the winding positive so edge tests are uniform (culling was done at set-up)
		const svert_t *t = v1;
		v1 = v2;
		v2 = t;
		area = -area;
	}
	const svert_t *vs[3] = {v0, v1, v2};

	// attribute gradients
	float a0[NUM_ATTR + 1], dadx[NUM_ATTR + 1], dady[NUM_ATTR + 1];
	const float *f0 = &v0->iw, *f1 = &v1->iw, *f2 = &v2->iw; // iw,u,v,r,g,b,a,fog are contiguous
	float inv_area = 1.0f / area;
	float x10 = v1->x - v0->x, y10 = v1->y - v0->y, x20 = v2->x - v0->x, y20 = v2->y - v0->y;
	for (int i = 0; i < NUM_ATTR; i++) {
		float d1 = f1[i] - f0[i], d2 = f2[i] - f0[i];
		a0[i] = f0[i];
		dadx[i] = (d1 * y20 - d2 * y10) * inv_area;
		dady[i] = (d2 * x10 - d1 * x20) * inv_area;
	}
	// depth (z/w) is linear in screen space
	a0[NUM_ATTR] = v0->z;
	dadx[NUM_ATTR] = ((v1->z - v0->z) * y20 - (v2->z - v0->z) * y10) * inv_area;
	dady[NUM_ATTR] = ((v2->z - v0->z) * x10 - (v1->z - v0->z) * x20) * inv_area;
	grad_t d = {{dadx[0], dadx[1], dadx[2], dadx[3], dadx[4], dadx[5], dadx[6], dadx[7], dadx[8]},
		{dady[0], dady[1], dady[2], dady[3], dady[4], dady[5], dady[6], dady[7], dady[8]}};

	// edges: canonical (lower y first) so shared edges give identical x crossings
	struct { float x0, y0, slope; bool right; bool active; } e[3];
	for (int i = 0; i < 3; i++) {
		const svert_t *a = vs[i], *b = vs[(i + 1) % 3];
		float dy = b->y - a->y;
		e[i].active = dy != 0;
		e[i].right = dy > 0;
		const svert_t *lo = dy > 0 ? a : b, *hi = dy > 0 ? b : a;
		e[i].x0 = lo->x;
		e[i].y0 = lo->y;
		e[i].slope = dy != 0 ? (hi->x - lo->x) / (hi->y - lo->y) : 0;
	}

	uint32_t fogc = ((uint32_t)st->fog_color.r << 16) | ((uint32_t)st->fog_color.g << 8) | st->fog_color.b;

	for (int py = py0; py <= py1; py++) {
		float yc = py + 0.5f;
		float lo = -1e30f, hi = 1e30f;
		for (int i = 0; i < 3; i++) {
			if (!e[i].active) {
				continue;
			}
			float xc = e[i].x0 + (yc - e[i].y0) * e[i].slope;
			if (e[i].right) {
				hi = min(hi, xc);
			} else {
				lo = max(lo, xc);
			}
		}
		int px0 = (int)ceilf(lo - 0.5f);
		int px1 = (int)ceilf(hi - 0.5f) - 1;
		if (px0 < st->sx0) px0 = st->sx0;
		if (px1 > st->sx1 - 1) px1 = st->sx1 - 1;
		if (px0 > px1) {
			continue;
		}
		float dx = px0 + 0.5f - v0->x, dy = yc - v0->y;
		attr_t a;
		float *ap = &a.iw;
		for (int i = 0; i <= NUM_ATTR; i++) {
			ap[i] = a0[i] + dadx[i] * dx + dady[i] * dy;
		}
		fn(st, R.color + py * R.width + px0, R.depth + py * R.width + px0, px1 - px0 + 1, a, &d, tex, blend, fogc);
	}
}

// ---------------------------------------------------------------- set-up: transform, clip, project, cull

// a screen triangle ready for raster_tri
typedef struct {
	const dstate_t *st;
	const texture_t *tex;
	blend_t blend;
	span_fn fn;
	svert_t v[3];
} stri_t;

// where set-up puts its triangles: drawn at once, or stored for the bands
typedef void (*emit_fn)(const dstate_t *st, const svert_t *a, const svert_t *b, const svert_t *c, const texture_t *tex,
	blend_t blend, span_fn fn);

static void emit_now(const dstate_t *st, const svert_t *a, const svert_t *b, const svert_t *c, const texture_t *tex,
	blend_t blend, span_fn fn) {
	raster_tri(st, st->sy0, st->sy1, a, b, c, tex, blend, fn);
}

// the screen-space part of the set-up: degenerate triangles and back faces go
static void setup_tri(const dstate_t *st, const svert_t *v0, const svert_t *v1, const svert_t *v2, const texture_t *tex,
	blend_t blend, span_fn fn, emit_fn emit) {
	float area = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
	if (area == 0 || !isfinite(area)) {
		return;
	}
	cull_t cull = st->cull;
	if (st->mirror && cull != CULL_NONE) { // a reflected view reverses the winding on screen
		cull = cull == CULL_BACK ? CULL_FRONT : CULL_BACK;
	}
	if ((cull == CULL_BACK && area < 0) || (cull == CULL_FRONT && area > 0)) {
		return;
	}
	emit(st, v0, v1, v2, tex, blend, fn);
}

static cvert_t lerp_cvert(const cvert_t *a, const cvert_t *b, float t) {
	cvert_t r;
	const float *fa = &a->x, *fb = &b->x;
	float *fr = &r.x;
	for (int i = 0; i < (int)(sizeof(cvert_t) / sizeof(float)); i++) {
		fr[i] = fa[i] + (fb[i] - fa[i]) * t;
	}
	return r;
}

static float plane_dist(const cvert_t *v, int plane) {
	switch (plane) {
		case 0: return v->z;          // near
		case 1: return v->w - v->z;   // far
		case 2: return v->w + v->x * 0.5f; // left (guard band: |x| <= 2w)
		case 3: return v->w - v->x * 0.5f;
		case 4: return v->w + v->y * 0.5f;
		default: return v->w - v->y * 0.5f;
	}
}

static int clip_poly(cvert_t *in, int n, cvert_t *tmp, int planes) {
	cvert_t *src = in, *dst = tmp;
	for (int p = 0; p < 6 && n >= 3; p++) {
		if (!(planes & (1 << p))) continue;
		int m = 0;
		for (int i = 0; i < n; i++) {
			cvert_t *a = &src[i], *b = &src[(i + 1) % n];
			float da = plane_dist(a, p), db = plane_dist(b, p);
			if (da >= 0) {
				dst[m++] = *a;
			}
			if ((da >= 0) != (db >= 0)) {
				dst[m++] = lerp_cvert(a, b, da / (da - db));
			}
		}
		n = m;
		cvert_t *t = src;
		src = dst;
		dst = t;
	}
	if (src != in) {
		memcpy(in, src, sizeof(cvert_t) * n);
	}
	return n;
}

static void project(const cvert_t *c, svert_t *s) {
	float iw = 1.0f / c->w;
	s->x = (c->x * iw + 1) * 0.5f * R.width;
	s->y = (1 - c->y * iw) * 0.5f * R.height;
	s->z = c->z * iw;
	s->iw = iw;
	s->u = c->u * iw;
	s->v = c->v * iw;
	s->r = c->r * iw;
	s->g = c->g * iw;
	s->b = c->b * iw;
	s->a = c->a * iw;
	s->fog = c->fog * iw;
}

static void setup_clip_tri(const dstate_t *st, cvert_t *c, int planes, const texture_t *tex, blend_t blend, span_fn fn,
	emit_fn emit) {
	cvert_t poly[12], tmp[12];
	memcpy(poly, c, sizeof(cvert_t) * 3);
	int n = 3;
	if (planes) {
		n = clip_poly(poly, n, tmp, planes);
	}
	if (n < 3) {
		return;
	}
	svert_t s[12];
	for (int i = 0; i < n; i++) {
		project(&poly[i], &s[i]);
	}
	for (int i = 1; i + 1 < n; i++) {
		setup_tri(st, &s[0], &s[i], &s[i + 1], tex, blend, fn, emit);
	}
}

// the clip-space outcodes of a vertex (bit p: outside plane p)
static inline int outcode(const cvert_t *v) {
	int c = 0;
	if (v->z < 0) c |= 1;
	if (v->w - v->z < 0) c |= 2;
	if (v->w + v->x * 0.5f < 0) c |= 4;
	if (v->w - v->x * 0.5f < 0) c |= 8;
	if (v->w + v->y * 0.5f < 0) c |= 16;
	if (v->w - v->y * 0.5f < 0) c |= 32;
	return c;
}

static void setup_tris(const dstate_t *st, const vertex_t *v, int num_vertices, const texture_t *tex, blend_t blend,
	emit_fn emit) {
	span_fn fn_fog = pick_span(st, tex, blend, st->fog_enabled), fn_clear = pick_span(st, tex, blend, false);
	const float *m = st->mvp.m;
	for (int i = 0; i + 2 < num_vertices; i += 3) {
		cvert_t c[3];
		int all_out = 0x3f, any_out = 0;
		for (int k = 0; k < 3; k++) {
			const vertex_t *s = &v[i + k];
			float x = s->pos.x, y = s->pos.y, z = s->pos.z;
			c[k].x = m[0] * x + m[4] * y + m[8] * z + m[12];
			c[k].y = m[1] * x + m[5] * y + m[9] * z + m[13];
			c[k].z = m[2] * x + m[6] * y + m[10] * z + m[14];
			c[k].w = m[3] * x + m[7] * y + m[11] * z + m[15];
			int oc = outcode(&c[k]);
			all_out &= oc;
			any_out |= oc;
		}
		if (all_out) {
			continue;
		}
		bool clear = true;
		for (int k = 0; k < 3; k++) {
			const vertex_t *s = &v[i + k];
			c[k].u = s->u; c[k].v = s->v;
			c[k].r = s->color.r; c[k].g = s->color.g; c[k].b = s->color.b; c[k].a = s->color.a;
			c[k].fog = st->fog_enabled ? fog_factor(st, c[k].w) : 1; // w = view-space depth
			clear &= c[k].fog == 1;
		}
		// specular alpha 255 at every vertex: no fog over the triangle
		setup_clip_tri(st, c, any_out, tex, blend, clear ? fn_clear : fn_fog, emit);
	}
}

// st must already have the 2-D states (no depth, no fog, no cull)
static void setup_quad(const dstate_t *st, const float *q, rgba_t color, const texture_t *tex, blend_t blend, emit_fn emit) {
	float x = q[0], y = q[1], w = q[2], h = q[3], u0 = q[4], v0 = q[5], u1 = q[6], v1 = q[7];
	svert_t s[4];
	float xs[4] = {x, x + w, x + w, x}, ys[4] = {y, y, y + h, y + h};
	float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
	for (int i = 0; i < 4; i++) {
		s[i] = (svert_t){xs[i], ys[i], 0, 1, us[i], vs[i], color.r, color.g, color.b, color.a, 1};
	}
	span_fn fn = pick_span(st, tex, blend, false);
	setup_tri(st, &s[0], &s[1], &s[2], tex, blend, fn, emit);
	setup_tri(st, &s[0], &s[2], &s[3], tex, blend, fn, emit);
}

static void quad_state(dstate_t *st) {
	st->depth_test = st->depth_write = st->fog_enabled = st->mirror = false;
	st->cull = CULL_NONE;
}

// ---------------------------------------------------------------- API

void render_tris(const vertex_t *v, int num_vertices, const texture_t *tex, blend_t blend) {
	if (num_vertices < 3) return;
	if (tex && tex->mip_ok && !tex->mip_levels) build_mips((texture_t *)tex); // here, before any thread reads it
	if (storing()) {
		rec_cmd_t *c = rec_push(0, tex, blend);
		if (REC.nvert + num_vertices > REC.cap_vert) {
			REC.cap_vert = max(REC.cap_vert * 2, REC.nvert + num_vertices + 65536);
			REC.vert = realloc(REC.vert, sizeof(vertex_t) * REC.cap_vert);
		}
		memcpy(REC.vert + REC.nvert, v, sizeof(vertex_t) * num_vertices);
		REC.nvert += num_vertices;
		c->count = num_vertices;
		return;
	}
	setup_tris(&R.s, v, num_vertices, tex, blend, emit_now);
}

void render_quad_2d(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
	rgba_t color, const texture_t *tex, blend_t blend) {
	float s = R.scale2d;
	float q[8] = {(x + R.ox2d) * s, (y + R.oy2d) * s, w * s, h * s, u0, v0, u1, v1};
	if (tex && tex->mip_ok && !tex->mip_levels) build_mips((texture_t *)tex);
	if (storing()) {
		rec_cmd_t *c = rec_push(1, tex, blend);
		quad_state(&c->s);
		memcpy(c->quad, q, sizeof(q));
		c->quad_color = color;
		return;
	}
	dstate_t st = R.s;
	quad_state(&st);
	setup_quad(&st, q, color, tex, blend, emit_now);
}

// ---------------------------------------------------------------- drawing the stored commands
//
// Set-up runs once over the commands (in order) and bins the screen triangles into bands of rows;
// then the bands are rasterised, each in triangle order, in parallel.

#define BAND_ROWS 16
#define MAX_BANDS 256

static struct {
	stri_t *tri;
	int ntri, cap_tri;
	struct { int *idx; int n, cap; } band[MAX_BANDS];
	int nbands;
} BIN;

static void emit_store(const dstate_t *st, const svert_t *a, const svert_t *b, const svert_t *c, const texture_t *tex,
	blend_t blend, span_fn fn) {
	float ymin = min(a->y, min(b->y, c->y)), ymax = max(a->y, max(b->y, c->y));
	int py0 = (int)ceilf(ymin - 0.5f), py1 = (int)ceilf(ymax - 0.5f) - 1;
	if (py0 < st->sy0) py0 = st->sy0;
	if (py1 > st->sy1 - 1) py1 = st->sy1 - 1;
	if (py0 > py1) return;
	if (BIN.ntri == BIN.cap_tri) {
		BIN.cap_tri = BIN.cap_tri ? BIN.cap_tri * 2 : 16384;
		BIN.tri = realloc(BIN.tri, sizeof(stri_t) * BIN.cap_tri);
	}
	int t = BIN.ntri++;
	BIN.tri[t] = (stri_t){st, tex, blend, fn, {*a, *b, *c}};
	for (int bnd = py0 / BAND_ROWS; bnd <= py1 / BAND_ROWS && bnd < BIN.nbands; bnd++) {
		if (BIN.band[bnd].n == BIN.band[bnd].cap) {
			BIN.band[bnd].cap = BIN.band[bnd].cap ? BIN.band[bnd].cap * 2 : 1024;
			BIN.band[bnd].idx = realloc(BIN.band[bnd].idx, sizeof(int) * BIN.band[bnd].cap);
		}
		BIN.band[bnd].idx[BIN.band[bnd].n++] = t;
	}
}

static void setup_commands(int first, int last) {
	BIN.ntri = 0;
	BIN.nbands = min((R.height + BAND_ROWS - 1) / BAND_ROWS, MAX_BANDS);
	for (int b = 0; b < BIN.nbands; b++) BIN.band[b].n = 0;
	for (int i = first; i < last; i++) {
		const rec_cmd_t *c = &REC.cmd[i];
		if (c->kind == 0) {
			setup_tris(&c->s, REC.vert + c->first, c->count, c->tex, c->blend, emit_store);
		} else {
			setup_quad(&c->s, c->quad, c->quad_color, c->tex, c->blend, emit_store);
		}
	}
}

static void raster_band(int b) {
	int y0 = b * BAND_ROWS, y1 = min(R.height, y0 + BAND_ROWS);
	for (int k = 0; k < BIN.band[b].n; k++) {
		const stri_t *t = &BIN.tri[BIN.band[b].idx[k]];
		int ry0 = max(y0, t->st->sy0), ry1 = min(y1, t->st->sy1);
		raster_tri(t->st, ry0, ry1, &t->v[0], &t->v[1], &t->v[2], t->tex, t->blend, t->fn);
	}
}

#ifdef RENDER_THREADS
#define MAX_WORKERS 16

static struct {
	int count;                      // worker threads (besides the caller); 0 = draw in the caller
	bool started;
	pthread_t thread[MAX_WORKERS];
	pthread_mutex_t lock;
	pthread_cond_t go, done;
	unsigned job;                   // incremented per run
	int running;
	atomic_int next_band;
} W;

static void work(void) {
	for (;;) {
		int b = atomic_fetch_add(&W.next_band, 1);
		if (b >= BIN.nbands) break;
		raster_band(b);
	}
}

static void *worker(void *arg) {
	(void)arg;
	unsigned seen = 0;
	pthread_mutex_lock(&W.lock);
	for (;;) {
		while (W.job == seen) pthread_cond_wait(&W.go, &W.lock);
		seen = W.job;
		pthread_mutex_unlock(&W.lock);
		work();
		pthread_mutex_lock(&W.lock);
		if (--W.running == 0) pthread_cond_signal(&W.done);
	}
	return NULL;
}

static int cpu_count(void) {
#ifdef _WIN32
	return pthread_num_processors_np();
#else
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? (int)n : 1;
#endif
}

static void start_workers(void) {
	W.started = true;
	const char *env = getenv("LANDER_RENDER_THREADS"); // threads drawing a frame, 1 = no workers
	int n = env ? atoi(env) : cpu_count();
	W.count = clamp(n, 1, MAX_WORKERS) - 1;
	pthread_mutex_init(&W.lock, NULL);
	pthread_cond_init(&W.go, NULL);
	pthread_cond_init(&W.done, NULL);
	for (int i = 0; i < W.count; i++) {
		if (pthread_create(&W.thread[i], NULL, worker, NULL) != 0) {
			W.count = i;
			break;
		}
	}
}

static void raster_bands(void) {
	if (!W.started) start_workers();
	if (W.count == 0) {
		for (int b = 0; b < BIN.nbands; b++) raster_band(b);
		return;
	}
	pthread_mutex_lock(&W.lock);
	atomic_store(&W.next_band, 0);
	W.running = W.count;
	W.job++;
	pthread_cond_broadcast(&W.go);
	pthread_mutex_unlock(&W.lock);
	work();
	pthread_mutex_lock(&W.lock);
	while (W.running > 0) pthread_cond_wait(&W.done, &W.lock);
	pthread_mutex_unlock(&W.lock);
}
#else
static void raster_bands(void) {
	for (int b = 0; b < BIN.nbands; b++) raster_band(b);
}
#endif

static void run_commands(int first, int last) {
	if (first >= last) return;
	setup_commands(first, last);
	raster_bands();
}

static void flush(void) {
	if (REC.nested) return; // inside a nested record: drawn by render_replay
	run_commands(0, REC.ncmd);
	REC.ncmd = REC.nvert = 0;
}
