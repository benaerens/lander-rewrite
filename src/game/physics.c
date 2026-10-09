// Rigid-body physics and world collision of Lander (1999) - port of lander.EXE (D3D build).
// See physics.h for units and re/specs/physics.md for the algorithm. Every function names the
// address of the original it translates. Arithmetic is 32-bit two's complement like the exe:
// sums of products are formed in int64 and wrapped (W) before the arithmetic shift.
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "physics.h"
#include "fs.h"

typedef int32_t i32;
typedef int64_t i64;

static inline i32 W(i64 v) { return (i32)(uint32_t)(uint64_t)v; }   // wrap to 32 bits
static inline i32 SAR(i32 v, int n) { return v >> n; }               // arithmetic (sar)
static inline i32 SHL(i32 v, int n) { return (i32)((uint32_t)v << n); }
static inline i32 IDIV(i32 a, i32 b) {                                // idiv (truncates)
	if (b == 0) return 0;                                             // the exe would fault
	if (a == INT32_MIN && b == -1) return INT32_MIN;
	return a / b;
}

phys_globals_t phys_g = {
	.gravity = {0, -26, 0}, .air_resistance = 0x15, .friction = 0x1000, .exclude_owner = -1,
};
phys_body_t phys_bodies[PHYS_MAX_BODIES];
int phys_num_bodies;

const phys_surface_t phys_surface_table[9] = {          // 0x498e08
	{0x000, 0x1000, {0x40, 0x00, 0x40, 0x00}, 0x0080, 0},
	{0x000, 0x1000, {0x40, 0x00, 0x40, 0x00}, 0x0080, 0},
	{0x000, 0x2000, {0xbe, 0x00, 0x00, 0x01}, 0x0080, 0},
	{0x000, 0x0bb8, {0x00, 0x00, 0x00, 0x00}, 0x0080, 0},
	{0x578, 0x0200, {0x80, 0x00, 0x80, 0x00}, 0x1000, 1},   // liquid
	{0x000, 0x0400, {0x20, 0x00, 0x50, 0x00}, 0x0080, 0},
	{0x000, 0x4000, {0x40, 0x00, 0xb4, 0x00}, 0x0080, 0},
	{0x000, 0x0000, {0xff, 0x00, 0xff, 0x00}, -1,     0},   // ghost: touch report only
	{0x578, 0x0400, {0x80, 0x00, 0x80, 0x00}, 0x0400, 1},   // liquid
};

static phys_world_t *world;

// ==================================================================== x87 / PSX library

// MSVCRT _ftol (0x4881bc -> IAT): truncate toward zero to 64 bits, return the low 32 bits.
static i32 ftol(double f) {
	if (!(f > -9.2e18 && f < 9.2e18)) {
		return INT32_MIN;
	}
	return W((i64)f);
}

// With the FPU at 24-bit precision (Fw_RestoreFPU 0x434c40, _controlfp(_PC_24)) every x87
// add/mul/div rounds to single precision. fild loads 32-bit integers exactly, so "int * int"
// is one rounding of the exact product: done here through int64 -> float.
static float fmul_ii(i32 a, i32 b) { return (float)((i64)a * b); }
// int * float (float has <= 24 significant bits, the exact product fits a double)
static float fmul_if(i32 a, float b) { return (float)((double)a * (double)b); }
// int / int rounded to single (the double quotient is rounded again: may differ from the x87
// in the last bit about once in 2^29 divisions)
static float fdiv_ii(double a, i32 b) { return (float)(a / (double)b); }

// FwSqrt 0x4354e0: fild, fsqrt (24-bit), fistp (round to nearest even)
static i32 fw_sqrt(i32 x) {
	if (x < 0) {
		return INT32_MIN;
	}
	float s = (float)sqrt((double)x);
	return (i32)nearbyint((double)s);
}

static void v_copy(const i32 *a, i32 *o) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; }
// FwVectorAdd 0x4368d0
static void v_add(const i32 *a, const i32 *b, i32 *o) {
	o[0] = W((i64)a[0] + b[0]); o[1] = W((i64)b[1] + a[1]); o[2] = W((i64)b[2] + a[2]);
}
// FwVectorSubtract 0x436900
static void v_sub(const i32 *a, const i32 *b, i32 *o) {
	o[0] = W((i64)a[0] - b[0]); o[1] = W((i64)a[1] - b[1]); o[2] = W((i64)a[2] - b[2]);
}
// FwVectorScale 0x436960
void phys_scale(const i32 *v, i32 s, i32 *o) {
	i32 x = SAR(W((i64)v[0] * s + 0x800), 12);
	i32 y = SAR(W((i64)v[1] * s + 0x800), 12);
	i32 z = SAR(W((i64)v[2] * s + 0x800), 12);
	o[0] = x; o[1] = y; o[2] = z;
}
// FwVectorDotProduct 0x436800
static i32 v_dot(const i32 *a, const i32 *b) {
	return SAR(W((i64)a[1] * b[1] + (i64)a[2] * b[2] + 0x800 + (i64)b[0] * a[0]), 12);
}
// FwVectorCrossProduct 0x4367a0
static void v_cross(const i32 *a, const i32 *b, i32 *o) {
	i32 x = SAR(W((i64)b[2] * a[1] - (i64)a[2] * b[1] + 0x800), 12);
	i32 y = SAR(W((i64)b[0] * a[2] - (i64)a[0] * b[2] + 0x800), 12);
	i32 z = SAR(W((i64)a[0] * b[1] - (i64)b[0] * a[1] + 0x800), 12);
	o[0] = x; o[1] = y; o[2] = z;
}
// FwVectorNormalise 0x436830: returns the magnitude (the exe returns a pointer when the
// squared length overflowed; the vector becomes 0 then)
i32 phys_normalise(i32 *v) {
	i32 s = W((i64)v[1] * v[1] + (i64)v[2] * v[2] + (i64)v[0] * v[0]);
	if (s == 0) {
		v[0] = v[1] = v[2] = 0;
		return 0;
	}
	i32 mag = 0, inv = 0;
	if (s > 0) {
		mag = fw_sqrt(s);
		inv = mag ? (i32)(0x1000000 / mag) : 0;
	}
	v[0] = SAR(W((i64)v[0] * inv + 0x800), 12);
	v[2] = SAR(W((i64)inv * v[2] + 0x800), 12);
	v[1] = SAR(W((i64)inv * v[1] + 0x800), 12);
	return mag;
}
// FwVectorMagnitude 0x436930
static i32 v_magnitude(const i32 *v) {
	i32 s = W((i64)v[1] * v[1] + (i64)v[2] * v[2] + (i64)v[0] * v[0]);
	return s ? fw_sqrt(s) : 0;
}
// ApplyMatrix 0x435f40 (short vector) and ApplyMatrixLV 0x436110 (long vector): o = M v
static void apply_matrix_sv(const int16_t *m, const int16_t *v, i32 *o) {
	i32 x = SAR(W((i64)m[1] * v[1] + (i64)m[2] * v[2] + 0x800 + (i64)m[0] * v[0]), 12);
	i32 y = SAR(W((i64)m[5] * v[2] + (i64)m[4] * v[1] + 0x800 + (i64)m[3] * v[0]), 12);
	i32 z = SAR(W((i64)m[7] * v[1] + (i64)m[6] * v[0] + 0x800 + (i64)m[8] * v[2]), 12);
	o[0] = x; o[1] = y; o[2] = z;
}
static void apply_matrix_lv(const int16_t *m, const i32 *v, i32 *o) {
	i32 x = SAR(W((i64)m[0] * v[0] + (i64)m[2] * v[2] + 0x800 + (i64)m[1] * v[1]), 12);
	i32 y = SAR(W((i64)m[3] * v[0] + (i64)m[4] * v[1] + 0x800 + (i64)m[5] * v[2]), 12);
	i32 z = SAR(W((i64)m[8] * v[2] + (i64)m[6] * v[0] + 0x800 + (i64)m[7] * v[1]), 12);
	o[0] = x; o[1] = y; o[2] = z;
}
// invmatvec2 0x436aa0 (o = M^T v) and invmatvec 0x436a10 (in place)
static void inv_matvec(const int16_t *m, const i32 *v, i32 *o) {
	i32 r[3];
	for (int k = 0; k < 3; k++) {
		r[k] = SAR(W((i64)m[k] * v[0] + (i64)m[3 + k] * v[1] + (i64)v[2] * m[6 + k] + 0x800), 12);
	}
	v_copy(r, o);
}
// MulRotMatrix0 0x435d20: out = G * m (G = the matrix given to SetRotMatrix 0x435970)
static void mul_rot_matrix0(const int16_t *g, const int16_t *m, int16_t *out) {
	int16_t r[9];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r[i * 3 + j] = (int16_t)SAR(W((i64)m[j] * g[i * 3] + (i64)g[i * 3 + 2] * m[6 + j] + 0x800 +
				(i64)g[i * 3 + 1] * m[3 + j]), 12);
		}
	}
	memcpy(out, r, sizeof(r));
}
// 0x431730: identity rotation (translation untouched)
static void matrix_identity(int16_t *m) {
	static const int16_t id[9] = {0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000};
	memcpy(m, id, sizeof(id));
}

// ==================================================================== rotations

// 0x42fb30: out = in * R, R = Rodrigues rotation by -angle/4096 rad about axis (world axis,
// brought into the body frame with M^T). Float at 24-bit precision, ftol truncation.
void phys_rotate_axis_angle(const int16_t *in, int16_t *out, i32 angle, const i32 *axis) {
	int16_t r[9];
	i32 a[3];
	double theta = (double)(float)((double)(-angle) / 4096.0);  // fild; fdiv 4096.0 (rounded); fstp qword
	inv_matvec(in, axis, a);
	float c = (float)cos(theta);     // _CIcos: extended result stored to a float
	float s = (float)sin(theta);
	float t = (float)(1.0 - (double)c);
	float ax = (float)a[0], ay = (float)a[1], az = (float)a[2];
	const float k = 1.0f / 4096.0f;
	float xx = ax * ax * k, yy = ay * ay * k, zz = az * az * k;
	float xy = ax * ay * k, xz = ax * az * k, yz = ay * az * k;
	float m00 = (4096.0f - xx) * c + xx;
	float m11 = (4096.0f - yy) * c + yy;
	float m22 = (4096.0f - zz) * c + zz;
	r[0] = (int16_t)ftol(m00);
	r[4] = (int16_t)ftol(m11);
	r[8] = (int16_t)ftol(m22);
	xy = xy * t; xz = xz * t; yz = yz * t;
	float sx = ax * s, sy = ay * s, sz = az * s;
	r[1] = (int16_t)ftol(xy - sz);
	r[2] = (int16_t)ftol(xz + sy);
	r[3] = (int16_t)ftol(xy + sz);
	r[5] = (int16_t)ftol(yz - sx);
	r[6] = (int16_t)ftol(xz - sy);
	r[7] = (int16_t)ftol(yz + sx);
	mul_rot_matrix0(in, r, out);
}

// 0x42d360: re-orthonormalise from the Y and Z columns (Z kept, X = Y x Z, Y = Z x X)
static void matrix_orthonormalise(int16_t *m) {
	i32 y[3] = {m[1], m[4], m[7]}, z[3] = {m[2], m[5], m[8]}, x[3];
	phys_normalise(z);
	v_cross(y, z, x);
	phys_normalise(x);
	v_cross(z, x, y);
	m[0] = (int16_t)x[0]; m[3] = (int16_t)x[1]; m[6] = (int16_t)x[2];
	m[1] = (int16_t)y[0]; m[4] = (int16_t)y[1]; m[7] = (int16_t)y[2];
	m[2] = (int16_t)z[0]; m[5] = (int16_t)z[1]; m[8] = (int16_t)z[2];
}

// ==================================================================== surfaces

static int surface_class(int tex_byte) {
	if (world && world->surface_class && tex_byte < world->num_surface_class) {
		return world->surface_class[tex_byte] < 9 ? world->surface_class[tex_byte] : 0;
	}
	return phys_g.surface_default;
}
// Surface_SetContactParams 0x451240 (PTR_FUN_00498430 once a mission is loaded)
static void surface_select(int tex_byte) {
	const phys_surface_t *s = &phys_surface_table[0];
	phys_g.surface_last = 0;
	if (!(world && world->surface_table_old)) {
		int cls = surface_class(tex_byte);
		if (world && world->surface_class && tex_byte < world->num_surface_class) {
			phys_g.surface_last = cls;
		}
		s = &phys_surface_table[cls];
	}
	phys_g.bounce = s->bounce;
	phys_g.friction = s->friction;
	phys_g.liquid = s->liquid;
}
// Surface_Thickness 0x4512b0 (DAT_004e86a4): also sets the liquid flag
static i32 surface_thickness(int tex_byte) {
	const phys_surface_t *s = &phys_surface_table[0];
	if (!(world && world->surface_table_old)) {
		s = &phys_surface_table[surface_class(tex_byte)];
	}
	phys_g.liquid = s->liquid;
	return s->thickness;
}

// ==================================================================== collision polygons

void phys_mesh_init(phys_mesh_t *m, const model_t *model, int index) {
	memset(m, 0, sizeof(*m));
	m->model = model;
	m->index = index;
	if (!model) {
		return;
	}
	m->octree = model_tagged_block(model, TAG_POLY_OCTREE, NULL);
	m->planes = model_tagged_block(model, TAG_POLY_PLANES, NULL);
	m->polys = calloc(model->num_tris + 1, sizeof(phys_poly_t *));
}

void phys_mesh_free(phys_mesh_t *m) {
	if (m->polys && m->model) {
		for (int i = 0; i < m->model->num_tris; i++) {
			free(m->polys[i]);
		}
	}
	free(m->polys);
	memset(m, 0, sizeof(*m));
}

// 0x42b480: polygon record from 3/4 vertices (world units), the s16 4.12 normal and d.
void phys_poly_build(phys_poly_t *p, int n, const int16_t (*v)[3], const int16_t *nrm, int16_t d) {
	p->nverts = (uint8_t)n;
	p->normal[0] = nrm[0]; p->normal[1] = nrm[1]; p->normal[2] = nrm[2];
	p->d8 = SHL(d, 3);
	i32 mn[3], mx[3];
	for (int k = 0; k < 3; k++) {
		mn[k] = mx[k] = v[0][k];
	}
	for (int i = 1; i < n; i++) {
		for (int k = 0; k < 3; k++) {
			if (v[i][k] < mn[k]) mn[k] = v[i][k];
			else if (v[i][k] > mx[k]) mx[k] = v[i][k];
		}
	}
	i32 ax = abs(nrm[0]), ay = abs(nrm[1]), az = abs(nrm[2]);
	int axis, cu, cv;   // dropped axis; the two kept coordinates (first, second)
	if (ax > ay) axis = ax > az ? 0 : 2;
	else axis = ay > az ? 1 : 2;
	if (axis == 0) { cu = 1; cv = 2; }
	else if (axis == 1) { cu = 0; cv = 2; }
	else { cu = 0; cv = 1; }
	// u = the coordinate with the larger extent (the second one on ties)
	bool flip = (mx[cu] - mn[cu]) <= (mx[cv] - mn[cv]);
	p->axis = (uint8_t)axis;
	p->flip = flip;
	int a = flip ? cv : cu, b = flip ? cu : cv;
	for (int i = 0; i < n; i++) {
		p->v2d[i][0] = v[i][a];
		p->v2d[i][1] = v[i][b];
	}
	p->box[0] = (int16_t)mn[a]; p->box[1] = (int16_t)mn[b];
	p->box[2] = (int16_t)mx[a]; p->box[3] = (int16_t)mx[b];
	// push every vertex 32 units away from the centre (triangles add the box centre as a
	// 4th point to the average)
	i32 su = 0, sv = 0;
	for (int i = 0; i < n; i++) {
		su += p->v2d[i][0];
		sv += p->v2d[i][1];
	}
	if (n == 3) {
		su += SAR(p->box[0] + p->box[2] + 1, 1);
		sv += SAR(p->box[1] + p->box[3] + 1, 1);
	}
	i32 cuu = SAR(su + 2, 2), cvv = SAR(sv + 2, 2);
	for (int i = 0; i < n; i++) {
		i32 du = p->v2d[i][0] - cuu, dv = p->v2d[i][1] - cvv;
		i32 len = fw_sqrt(W((i64)du * du + (i64)dv * dv));
		if (len == 0) {
			continue;   // the exe divides by zero here (never happens with real data)
		}
		i32 k = (SAR(len, 1) + 0x20000) / len;
		p->v2d[i][0] = (int16_t)(p->v2d[i][0] + SAR(W((i64)k * du + 0x800), 12));
		p->v2d[i][1] = (int16_t)(p->v2d[i][1] + SAR(W((i64)k * dv + 0x800), 12));
	}
	p->box[2] += 0x20; p->box[3] += 0x20;
	p->box[0] -= 0x20; p->box[1] -= 0x20;
}

// CollPoly_Get 0x428cc0 (named Decal_GetForPoly before): the record of PCP triangle
// index & 0x1fff (+ the next triangle when bit 15 = quad), cached in the 0x00B0FFED slot.
const phys_poly_t *phys_mesh_poly(phys_mesh_t *mesh, uint16_t index) {
	const model_t *m = mesh->model;
	int t = index & 0x1fff;
	if (!m || !mesh->planes || t >= m->num_tris || ((index & 0x8000) && t + 1 >= m->num_tris)) {
		return NULL;
	}
	if (mesh->polys[t]) {
		return mesh->polys[t];
	}
	phys_poly_t *p = calloc(1, sizeof(phys_poly_t));
	int16_t v[4][3];
	int n = (index & 0x8000) ? 4 : 3;
	const pcp_tri_t *tri = &m->tris[t];
	for (int i = 0; i < 3; i++) {
		vec3_t q = m->vertices[m->wedges[tri->w[i]].vertex];
		v[i][0] = (int16_t)ftol(q.x); v[i][1] = (int16_t)ftol(q.y); v[i][2] = (int16_t)ftol(q.z);
	}
	if (n == 4) {   // the partner's third corner
		vec3_t q = m->vertices[m->wedges[m->tris[t + 1].w[2]].vertex];
		v[3][0] = (int16_t)ftol(q.x); v[3][1] = (int16_t)ftol(q.y); v[3][2] = (int16_t)ftol(q.z);
	}
	const uint8_t *pl = mesh->planes + t * 8;
	vec3_t nf = m->normals[get_i16(pl + 4)];
	int16_t nrm[3] = {(int16_t)ftol(nf.x * 4096.0f), (int16_t)ftol(nf.y * 4096.0f), (int16_t)ftol(nf.z * 4096.0f)};
	p->model = (int16_t)mesh->index;
	p->tri = index;
	p->surface = (uint8_t)tri->tex;
	phys_poly_build(p, n, (const int16_t (*)[3])v, nrm, get_i16(pl + 6));
	mesh->polys[t] = p;
	return p;
}

// 0x42b1f0: x8 point -> the polygon's 2-D coordinates (world units, rounded)
static void poly_project(const phys_poly_t *p, const i32 *q, int16_t uv[2]) {
	int16_t *a = p->flip ? &uv[1] : &uv[0], *b = p->flip ? &uv[0] : &uv[1];
	switch (p->axis) {
	case 0: *a = (int16_t)SAR(q[1] + 4, 3); *b = (int16_t)SAR(q[2] + 4, 3); break;
	case 1: *a = (int16_t)SAR(q[0] + 4, 3); *b = (int16_t)SAR(q[2] + 4, 3); break;
	default: *a = (int16_t)SAR(q[0] + 4, 3); *b = (int16_t)SAR(q[1] + 4, 3); break;
	}
}

// 0x42b280: 2-D point in convex polygon (crossings of the line u = const)
static bool poly_contains(const phys_poly_t *p, const int16_t uv[2]) {
	int16_t u = uv[0], v = uv[1];
	if (u < p->box[0] || u > p->box[2] || v < p->box[1] || v > p->box[3]) {
		return false;
	}
	int n = p->nverts;
	i32 x[2];
	int k = 0;
	if (u == p->box[2]) {
		for (int i = 0; i < n; i++) {
			const int16_t *a = p->v2d[i], *b = p->v2d[(i + 1) % n];
			if ((a[0] < u && b[0] >= u) || (b[0] < u && a[0] >= u)) {
				x[k] = a[0] == u ? a[1] : b[1];
				if (x[k] == v) return true;
				if (k == 1) goto compare;
				k++;
			}
		}
		return false;
compare:
		return x[0] >= v ? x[1] < v : x[1] >= v;
	}
	i32 v16 = SHL(v, 4);
	for (int i = 0; i < n; i++) {
		const int16_t *a = p->v2d[i], *b = p->v2d[(i + 1) % n];
		if ((a[0] <= u && b[0] > u) || (b[0] <= u && a[0] > u)) {
			x[k] = IDIV(SHL((u - b[0]) * a[1] + (a[0] - u) * b[1], 4), a[0] - b[0]);
			if (x[k] == v16) return true;
			if (k == 1) return x[0] >= v16 ? x[1] < v16 : x[1] >= v16;
			k++;
		}
	}
	return false;
}

// 0x42b450: signed distance (x8) of a frame-local x8 point from the polygon plane
static i32 poly_dist(const phys_poly_t *p, const i32 *q) {
	return W((i64)SAR(W((i64)p->normal[1] * q[1] + (i64)p->normal[2] * q[2] + (i64)q[0] * p->normal[0]), 12) - p->d8);
}

// ==================================================================== frames (0x42af90-0x42ba30)

enum { XF_ZONE, XF_MATRIX, XF_TILE, XF_MOVING_START, XF_MOVING_END };

static struct {
	int start_fn, end_fn;              // DAT_0058b398 / DAT_0058b3bc
	i32 origin8[3];                    // DAT_004e8650
	int rot;                           // DAT_004e8638
	const phys_matrix_t *mat;          // DAT_004e8694
	const phys_matrix_t *m_start;      // DAT_004e8630 (possibly the interpolated copy)
	const phys_matrix_t *m_start_raw;  // DAT_004e8634
	const phys_matrix_t *m_end;        // DAT_004e8688
	phys_matrix_t m_interp;            // DAT_004e8660
	i32 start8[3], end8[3];            // DAT_004e8610 / DAT_004e8620
	int moving;                        // DAT_004e8680
	uint32_t handle;                   // DAT_004e868c
	int part;                          // DAT_004e8690
	int kind;                          // DAT_0058b384
	phys_cand_t tag[2];                // DAT_0058b378 / DAT_0058b37c
	i32 query_time;                    // DAT_0058b394
} fr;

static uint32_t ghost_touch = 0xffffffff;  // DAT_0058b3b0
static int last_kind;                      // DAT_0058b3b8
static uint32_t last_handle;               // DAT_0058b390

// Tile_RotateToLocal 0x482f50 (and the rotation half of 0x42b030)
static void tile_rot_to_local(i32 *p, int r) {
	int lo = r & 1, hi = r >> 1;
	if (lo) { i32 t = p[0]; p[0] = p[2]; p[2] = t; }
	if (hi != lo) p[0] = -p[0];
	if (hi) p[2] = -p[2];
}

static void to_local(int fn, const i32 *p, i32 *o) {
	switch (fn) {
	case XF_ZONE:   v_sub(p, fr.origin8, o); break;                                   // 0x42af90
	case XF_MATRIX: v_sub(p, fr.origin8, o); inv_matvec(fr.mat->m, o, o); break;      // 0x42afd0
	case XF_TILE:   v_sub(p, fr.origin8, o); tile_rot_to_local(o, fr.rot); break;     // 0x42b030
	case XF_MOVING_START: v_sub(p, fr.start8, o); inv_matvec(fr.m_start->m, o, o); break; // 0x42b140
	default:        v_sub(p, fr.end8, o); inv_matvec(fr.m_end->m, o, o); break;      // 0x42b160
	}
}

static void origin_from(const i32 *t, i32 *o8) {
	o8[0] = SHL(t[0], 3); o8[1] = SHL(t[1], 3); o8[2] = SHL(t[2], 3);
}

// Frame_Select 0x42ba30: consume the tag at *pp (and its operand), set the transforms
static void frame_select(const phys_cand_t **pp) {
	const phys_cand_t *c = *pp;
	uint32_t w = c->word;
	fr.moving = 0;
	fr.handle = 0xffffffff;
	phys_g.surface_default = 0;    // DAT_004e86a8(-1) = 0x447630
	switch (w & 3) {
	case 1:     // zone: polygons relative to the zone origin
		fr.tag[0] = *c;
		fr.start_fn = fr.end_fn = XF_ZONE;
		origin_from((const i32 *)c->ptr, fr.origin8);
		fr.kind = PHYS_FRAME_ZONE;
		return;
	case 2:     // static object: world matrix
		fr.tag[0] = *c;
		fr.start_fn = fr.end_fn = XF_MATRIX;
		fr.mat = (const phys_matrix_t *)c->ptr;
		origin_from(fr.mat->t, fr.origin8);
		fr.kind = PHYS_FRAME_MATRIX;
		return;
	case 3:
		fr.tag[1] = c[1];
		fr.tag[0] = *c;
		if ((w & 0xc) == 4) {   // tile: translation + quarter turns
			fr.start_fn = fr.end_fn = XF_TILE;
			fr.rot = (int)(w >> 4);
			*pp = c + 1;
			origin_from((const i32 *)c[1].ptr, fr.origin8);
			fr.kind = PHYS_FRAME_TILE;
		} else if ((w & 0xc) == 8) {   // object part
			*pp = c + 1;
			fr.handle = c[1].word;
			fr.kind = PHYS_FRAME_OBJECT;
			fr.part = (int)((int32_t)fr.handle >> 26) & 0x3f;
			if (world && world->object_surface) {
				phys_g.surface_default = world->object_surface(fr.handle);
			}
			int cls = world && world->object_class ? world->object_class(fr.handle) : 0;
			if (cls == 1) {
				fr.start_fn = fr.end_fn = XF_MATRIX;
				fr.mat = world->object_matrix(fr.handle, 0);
			} else if (cls == 2) {
				fr.query_time = phys_g.in_physics ? phys_g.step_start : phys_g.game_time;
				fr.m_start = world->object_matrix(fr.handle, fr.query_time);
				if (phys_g.in_physics) {
					fr.query_time = phys_g.step_end;
				}
				fr.m_end = world->object_matrix(fr.handle, fr.query_time);
				if (fr.m_start != fr.m_end) {
					fr.start_fn = XF_MOVING_START;
					fr.end_fn = XF_MOVING_END;
					fr.moving = 1;
					fr.m_start_raw = fr.m_start;
					if (phys_g.interp != 0) {   // start matrix lerped to the fraction reached (0x443e00)
						i32 f = phys_g.interp, g = 0x1000 - f;
						for (int k = 0; k < 9; k++) {
							fr.m_interp.m[k] = (int16_t)(SAR(W((i64)fr.m_start->m[k] * g), 12) +
								SAR(W((i64)fr.m_end->m[k] * f), 12));
						}
						for (int k = 0; k < 3; k++) {
							fr.m_interp.t[k] = SAR(W((i64)fr.m_start->t[k] * g), 12) + SAR(W((i64)fr.m_end->t[k] * f), 12);
						}
						fr.m_start = &fr.m_interp;
					}
					origin_from(fr.m_start->t, fr.start8);
					origin_from(fr.m_end->t, fr.end8);
					return;
				}
				fr.start_fn = fr.end_fn = XF_MATRIX;
				fr.mat = fr.m_start;
			} else {
				return;   // other classes keep the previous transforms (as the exe)
			}
			origin_from(fr.mat->t, fr.origin8);
		}
		return;
	}
}

// Poly_NormalToWorld 0x42b870: polygon normal -> world, using the frame tag saved with it
static void poly_normal_to_world(const phys_poly_t *p, i32 *o, const phys_cand_t *tag) {
	const i32 *n = p->normal;
	uint32_t w = tag[0].word;
	switch (w & 3) {
	case 1:
		v_copy(n, o);
		return;
	case 2:
		apply_matrix_lv(((const phys_matrix_t *)tag[0].ptr)->m, n, o);
		return;
	case 3:
		if ((w & 0xc) == 4) {
			int r = (int)(w >> 4), lo = r & 1, hi = (int)((int32_t)(w >> 4) >> 1);
			i32 x = lo ? n[2] : n[0], z = lo ? n[0] : n[2];
			o[1] = n[1];
			if (hi != lo) z = -z;
			if (hi != 0) x = -x;
			o[0] = x; o[2] = z;
		} else if ((w & 0xc) == 8) {
			uint32_t h = tag[1].word;
			int cls = world && world->object_class ? world->object_class(h) : 0;
			if (cls == 1 || cls == 2) {
				i32 t = phys_g.in_physics ? phys_g.step_start : phys_g.game_time;
				apply_matrix_lv(world->object_matrix(h, cls == 2 ? t : 0)->m, n, o);
			}
		}
		return;
	}
}

// ==================================================================== octree box queries

typedef struct {               // OctIter (0xa0 bytes, 2 of them at 0x61f4e0)
	const uint8_t *base;
	uint16_t node;             // +0x0a current node offset (0 = none)
	i32 c[4];                  // +0x0c cell centre, +0x18 pad
	i32 size;                  // +0x1c cell edge
} oct_iter_t;

static uint16_t node_u16(const oct_iter_t *it, int i) { return get_u16(it->base + it->node + 2 * i); }

// cell -> box (0x468a40), box overlap (0x4689a0: 1 = disjoint), cell inside box (0x468b60)
static void cell_box(const oct_iter_t *it, i32 *b) {
	i32 h = SAR(it->size, 1);
	for (int k = 0; k < 3; k++) {
		b[k] = it->c[k] - h;
		b[4 + k] = h + it->c[k];
	}
	b[3] = b[7] = 0;
}
static bool box_disjoint(const i32 *a, const i32 *b) {
	return !(a[0] <= b[4] && b[0] <= a[4] && a[1] <= b[5] && b[1] <= a[5] && a[2] <= b[6] && b[2] <= a[6]);
}
static bool cell_disjoint(const oct_iter_t *it, const i32 *box) {   // 0x468b30
	i32 cb[8];
	cell_box(it, cb);
	return box_disjoint(cb, box);
}
static bool cell_inside(const oct_iter_t *it, const i32 *b) {        // 0x468b60
	i32 h = SAR(it->size, 1);
	return b[0] < it->c[0] - h && it->c[0] + h < b[4] && b[1] < it->c[1] - h && it->c[1] + h < b[5] &&
		b[2] < it->c[2] - h && h + it->c[2] < b[6];
}

// hit list (DAT_00626aa8 -> 0x620a00): groups of leaves of polygon indices
#define HIT_MAX_GROUPS 64      // the exe has room for 20 (no check)
#define HIT_MAX_LEAVES 4096    // 0x7c0
#define HIT_MAX_POLYS  16384   // 0x1100
typedef struct {
	int num_groups, num_leaves, num_polys;
	struct { int leaves; i32 id; } group[HIT_MAX_GROUPS];
	uint16_t leaf[HIT_MAX_LEAVES];
	uint16_t poly[HIT_MAX_POLYS];
	int group_leaves;          // DAT_00626aac
} hit_list_t;

static hit_list_t hits;

// Leaf_CollectPolys 0x468d80 (+ copy 0x468ca0): kind-1 leaf {1, count, list_off}
static void leaf_collect_polys(oct_iter_t *it) {
	int count = node_u16(it, 1);
	const uint8_t *list = it->base + node_u16(it, 2);
	if (count == 0 || hits.num_leaves >= HIT_MAX_LEAVES || hits.num_polys + count > HIT_MAX_POLYS) {
		return;
	}
	for (int i = 0; i < count; i++) {
		hits.poly[hits.num_polys + i] = get_u16(list + 2 * i);
	}
	hits.leaf[hits.num_leaves++] = (uint16_t)count;
	hits.num_polys += count;
	hits.group_leaves++;
}

// world-octree leaves (kind 2) found by the zone walk (0x469f10 -> table 0x561910)
#define ZONE_LEAVES_MAX 256
static struct { uint16_t node; i32 c[4]; i32 size; } zone_leaf[ZONE_LEAVES_MAX];
static int num_zone_leaves;   // DAT_00626ab4
static void leaf_collect_zone(oct_iter_t *it) {
	if (num_zone_leaves < ZONE_LEAVES_MAX) {
		zone_leaf[num_zone_leaves].node = it->node;
		memcpy(zone_leaf[num_zone_leaves].c, it->c, sizeof(it->c));
		zone_leaf[num_zone_leaves].size = it->size;
		num_zone_leaves++;
	}
}

typedef void (*leaf_fn)(oct_iter_t *it);

// OctIter_VisitAll 0x469840: every leaf below the node
static void oct_visit_all(oct_iter_t *it, int kind, leaf_fn cb) {
	if (!it->node) {
		return;
	}
	if (node_u16(it, 0) == kind) {
		cb(it);
		return;
	}
	oct_iter_t save = *it;
	i32 half = SAR(it->size, 1), q = SAR(it->size, 2);
	for (int i = 0; i < 8; i++) {
		uint16_t child = node_u16(&save, i);
		it->size = half;
		it->c[0] += ((i & 4) ? half : 0) - q;
		it->c[1] += ((i & 2) ? half : 0) - q;
		it->c[2] += ((i & 1) ? half : 0) - q;
		it->node = child;
		oct_visit_all(it, kind, cb);
		*it = save;
	}
}

// OctIter_WalkBox 0x469990: leaves whose cells overlap the box
static void oct_walk_box(oct_iter_t *it, const i32 *box, int kind, leaf_fn cb) {
	if (!it->node || cell_disjoint(it, box)) {
		return;
	}
	if (cell_inside(it, box)) {
		oct_visit_all(it, kind, cb);
		return;
	}
	if (node_u16(it, 0) == kind) {
		cb(it);
		return;
	}
	oct_iter_t save = *it;
	i32 half = SAR(it->size, 1), q = SAR(it->size, 2);
	for (int i = 0; i < 8; i++) {
		uint16_t child = node_u16(&save, i);
		it->size = half;
		it->c[0] += ((i & 4) ? half : 0) - q;
		it->c[1] += ((i & 2) ? half : 0) - q;
		it->c[2] += ((i & 1) ? half : 0) - q;
		it->node = child;
		oct_walk_box(it, box, kind, cb);
		*it = save;
	}
}

// OctIter_InitRoot 0x4694c0 / 0x469550 for type 0/2 headers {type, root, centre[3], pad, size}
static void oct_init_root(oct_iter_t *it, const uint8_t *sec) {
	it->base = sec;
	it->node = get_u16(sec + 2);
	it->c[0] = get_i32(sec + 4); it->c[1] = get_i32(sec + 8); it->c[2] = get_i32(sec + 0xc);
	it->c[3] = get_i32(sec + 0x10);
	it->size = get_i32(sec + 0x14);
}

static void hits_reset(void) {
	hits.num_groups = hits.num_leaves = hits.num_polys = 0;
}

static void hits_end_group(i32 id) {
	if (hits.group_leaves && hits.num_groups < HIT_MAX_GROUPS) {
		hits.group[hits.num_groups].leaves = hits.group_leaves;
		hits.group[hits.num_groups].id = id;
		hits.num_groups++;
	}
}

// Coll_QueryBox for the zonedata world octree (type 0): 0x469f50. Returns -1 on table overflow.
static int query_box_zones(const i32 *box) {
	hits_reset();
	const uint8_t *oct = world ? world->zone_octree : NULL;
	if (!oct) {
		return 0;
	}
	oct_iter_t it;
	oct_init_root(&it, oct);
	if (!it.node || cell_disjoint(&it, box)) {
		return 0;
	}
	num_zone_leaves = 0;
	oct_walk_box(&it, box, 2, leaf_collect_zone);
	if (!num_zone_leaves) {
		return 0;
	}
	// group the (zone, sub) pairs by zone, in order of first appearance (<= 30 zones x 20)
	struct { int16_t zone; int n; struct { int leaf; int sub; } e[20]; } zt[30];
	int nz = 0;
	for (int j = 0; j < num_zone_leaves; j++) {
		const uint8_t *leaf = oct + zone_leaf[j].node;
		int count = get_u16(leaf + 2);
		const uint8_t *list = oct + get_u16(leaf + 4);
		for (int k = 0; k < count; k++) {
			int16_t zone = get_i16(list + 4 * k);
			int z = 0;
			while (z < nz && zt[z].zone != zone) z++;
			if (z == nz) {
				if (nz == 30) return -1;
				zt[nz].zone = zone;
				zt[nz].n = 0;
				nz++;
			}
			if (zt[z].n == 20) return -1;
			zt[z].e[zt[z].n].leaf = j;
			zt[z].e[zt[z].n].sub = get_u16(list + 4 * k + 2);
			zt[z].n++;
		}
	}
	for (int z = 0; z < nz; z++) {
		hits.group_leaves = 0;
		int zone = zt[z].zone;
		const uint8_t *sec = zone >= 0 && zone < world->num_zones ? world->zone_mesh[zone].octree : NULL;
		for (int k = 0; sec && k < zt[z].n; k++) {
			// OctIter_InitSub 0x469500: root = section + 4 + 2*sub, cell of the world leaf
			oct_iter_t sub;
			sub.base = sec;
			sub.node = get_u16(sec + 4 + 2 * zt[z].e[k].sub);
			memcpy(sub.c, zone_leaf[zt[z].e[k].leaf].c, sizeof(sub.c));
			sub.size = zone_leaf[zt[z].e[k].leaf].size;
			if (sub.node) {
				oct_walk_box(&sub, box, 1, leaf_collect_polys);
			}
		}
		hits_end_group(zone);
	}
	return hits.num_polys;
}

// Coll_QueryBox for a type-2 poly octree (tile/object model, local frame): 0x46a6e0
static void query_box_octree(const uint8_t *sec, const i32 *box, i32 id) {
	i32 aabb[8];
	for (int k = 0; k < 8; k++) {
		aabb[k] = get_i32(sec + 0x18 + 4 * k);
	}
	if (box_disjoint(box, aabb)) {
		return;
	}
	oct_iter_t it;
	oct_init_root(&it, sec);
	if (!it.node || cell_disjoint(&it, box)) {
		return;
	}
	hits.group_leaves = 0;
	oct_walk_box(&it, box, 1, leaf_collect_polys);
	hits_end_group(id);
}

// Tile_BoxToLocal 0x482fc0
static void tile_box_to_local(const i32 *box, const i32 *pos, int r, i32 *o) {
	v_sub(box, pos, o);
	tile_rot_to_local(o, r);
	v_sub(box + 4, pos, o + 4);
	tile_rot_to_local(o + 4, r);
	o[3] = o[7] = 0;
	if ((r & 1) != (r >> 1)) { i32 t = o[0]; o[0] = o[4]; o[4] = t; }
	if (r >> 1) { i32 t = o[2]; o[2] = o[6]; o[6] = t; }
}

// Tiles_QueryBox 0x4832c0: every live tile instance, box in the tile's frame
static int query_box_tiles(const i32 *box) {
	hits_reset();
	for (int i = 0; i < world->num_tiles; i++) {
		const phys_tile_t *t = &world->tiles[i];
		const uint8_t *sec = t->model < world->num_tile_models ? world->tile_mesh[t->model].octree : NULL;
		if (!sec) {
			continue;
		}
		i32 lb[8];
		tile_box_to_local(box, t->pos, t->rot, lb);
		query_box_octree(sec, lb, i);
	}
	return hits.num_polys;
}

// World_CollectCandidates 0x449ca0: hit list -> tagged candidate list (kind 0 zones, 1 tiles)
static int collect_candidates(phys_cand_t *out, int max, int kind) {
	int count = 0, leaf = 0, poly = 0;
	for (int g = 0; g < hits.num_groups; g++) {
		phys_mesh_t *mesh;
		i32 id = hits.group[g].id;
		if (kind == 0) {
			if (max - 2 < count) return count;
			out[count].word = 1;
			out[count].ptr = &world->zone_origins[id];
			count++;
			mesh = &world->zone_mesh[id];
		} else {
			if (max - 3 < count) return count;
			const phys_tile_t *t = &world->tiles[id];
			out[count].word = ((uint32_t)t->rot << 4) | 7;
			out[count].ptr = NULL;
			out[count + 1].word = 0;
			out[count + 1].ptr = t->pos;
			count += 2;
			mesh = &world->tile_mesh[t->model];
		}
		int start = count;
		for (int l = 0; l < hits.group[g].leaves; l++, leaf++) {
			for (int k = 0; k < hits.leaf[leaf]; k++, poly++) {
				phys_poly_t *p = (phys_poly_t *)phys_mesh_poly(mesh, hits.poly[poly]);
				if (!p || p->in_list) {
					continue;
				}
				p->in_list = 1;
				out[count].word = 0;
				out[count].ptr = p;
				count++;
				if (count == max) {
					for (int i = start; i < count; i++) ((phys_poly_t *)out[i].ptr)->in_list = 0;
					return count;
				}
			}
		}
		for (int i = start; i < count; i++) {
			((phys_poly_t *)out[i].ptr)->in_list = 0;
		}
	}
	return count;
}

// Object part polygons (World_QueryBox kinds 2-4: the per-record queries 0x445df0 / 0x4463c0 /
// Mob_QueryBox 0x446a30, then World_CollectCandidates 0x449ca0). The box is in the part's frame.
// Static parts get a matrix frame tag (2 + the part's world matrix), objects and mobs an object
// frame tag ((renderItem << 4) | 0xb) followed by the handle (part index in bits 26-31).
int phys_collect_part(phys_mesh_t *mesh, const i32 *local_box, const phys_matrix_t *static_matrix,
	uint32_t render_item, uint32_t handle, phys_cand_t *out, int max) {
	if (!mesh || !mesh->octree || !world) {
		return 0;
	}
	hits_reset();
	query_box_octree(mesh->octree, local_box, 0);
	if (!hits.num_groups) {
		return 0;
	}
	int count;
	if (static_matrix) {
		if (max < 2) return 0;
		out[0].word = 2;
		out[0].ptr = static_matrix;
		count = 1;
	} else {
		if (max < 3) return 0;
		out[0].word = (render_item << 4) | 0xb;
		out[0].ptr = NULL;
		out[1].word = handle;
		out[1].ptr = NULL;
		count = 2;
	}
	int start = count, poly = 0;
	for (int l = 0; l < hits.group[0].leaves && count < max; l++) {
		for (int k = 0; k < hits.leaf[l] && count < max; k++, poly++) {
			phys_poly_t *p = (phys_poly_t *)phys_mesh_poly(mesh, hits.poly[poly]);
			if (!p || p->in_list) {
				continue;
			}
			p->in_list = 1;
			out[count].word = 0;
			out[count].ptr = p;
			count++;
		}
	}
	for (int i = start; i < count; i++) {
		((phys_poly_t *)out[i].ptr)->in_list = 0;
	}
	return count;
}

// World_QueryBox 0x44a0c0 (gWorldQueryBoxFn): union of two boxes -> candidates
int phys_query_box(const i32 *a, const i32 *b, phys_cand_t *out, int max) {
	i32 box[8];
	for (int k = 0; k < 3; k++) {     // 0x468a80
		box[k] = b[k] < a[k] ? b[k] : a[k];
		box[4 + k] = a[4 + k] < b[4 + k] ? b[4 + k] : a[4 + k];
	}
	box[3] = box[7] = 0;
	int n = 0;
	if (!world) {
		return 0;
	}
	if (world->query_mask & 1) {      // Zones_QueryBox 0x485e60
		if (query_box_zones(box) < 0) hits_reset();
		n = collect_candidates(out, max, 0);
		if (n >= max) return n;
	}
	if (world->query_mask & 2) {      // Tiles_QueryBox 0x4832c0
		query_box_tiles(box);
		n += collect_candidates(out + n, max - n, 1);
		if (n >= max) return n;
	}
	if ((world->query_mask & 0x1c) && world->query_objects) {   // objects (0x4478e0, 0x4476b0, 0x447210)
		n += world->query_objects(world, box, out + n, max - n);
	}
	return n;
}

void phys_set_world(phys_world_t *w) { world = w; }
phys_world_t *phys_get_world(void) { return world; }

// Tiles_StreamAround 0x482810 + Tiles_AddInstance 0x482500 (instance list only)
void phys_world_stream_tiles(phys_world_t *w, const i32 *pos, i32 r) {
	w->num_tiles = 0;
	if (!r || !w->tiledata) {
		return;
	}
	int x0 = tile_index(pos[0] - r), z0 = tile_index(pos[2] - r);
	int x1 = tile_index(r + pos[0]), z1 = tile_index(r + pos[2]);
	i32 ylo = pos[1] - r, yhi = r + pos[1];
	for (int tz = z0; tz <= z1; tz++) {
		for (int tx = x0; tx <= x1; tx++) {
			tile_cell_t cells[16];
			int n = tiledata_lookup(w->tiledata, tx, tz, cells, 16);
			for (int i = 0; i < n; i++) {
				const uint8_t *sec = cells[i].model < w->num_tile_models ? w->tile_mesh[cells[i].model].octree : NULL;
				bool keep;
				if (!sec) {   // TileModel_GetYRange 0x483600 failed
					keep = ylo < cells[i].y && cells[i].y < yhi;
				} else {
					i32 ymin = get_i32(sec + 0x1c) + cells[i].y, ymax = get_i32(sec + 0x2c) + cells[i].y;
					keep = ylo <= ymax && ymin <= yhi;
				}
				if (keep && w->num_tiles < PHYS_MAX_TILES) {
					phys_tile_t *t = &w->tiles[w->num_tiles++];
					t->pos[0] = tx * 0x3200 - 0x1900;
					t->pos[1] = cells[i].y;
					t->pos[2] = tz * 0x3200 - 0x1900;
					t->rot = cells[i].rotation;
					t->model = cells[i].model;
				}
			}
		}
	}
}

// ==================================================================== segment narrow phase

static phys_cand_t cand[PHYS_MAX_CANDIDATES];   // gWorldCandidates 0x58a2e0
static int num_cand;                            // DAT_0058b308
static phys_contact_t seg_contact;              // DAT_0058b320 (the segment query's record)
static i32 hit_point[3];                        // DAT_0058b3a0

// 0x42bf90: first polygon crossed front -> back by p0 -> p1 (world units); NULL = clear
static const phys_poly_t *segment_any_hit(const i32 *p0w, const i32 *p1w, const phys_cand_t *c, int n) {
	i32 p0[3] = {SHL(p0w[0], 3), SHL(p0w[1], 3), SHL(p0w[2], 3)};
	i32 p1[3] = {SHL(p1w[0], 3), SHL(p1w[1], 3), SHL(p1w[2], 3)};
	i32 l0[3], l1[3];
	const phys_cand_t *end = c + n;
	bool fresh = n != 0;
	for (; c != end; c++) {
		if ((c->word & 3) != 0) {
			frame_select(&c);
			fresh = true;
			continue;
		}
		if (fresh) {
			to_local(fr.end_fn, p1, l1);
			to_local(fr.start_fn, p0, l0);
			fresh = false;
		}
		const phys_poly_t *p = (const phys_poly_t *)c->ptr;
		i32 d1 = poly_dist(p, l1);
		if (d1 >= 1) continue;
		i32 d0 = poly_dist(p, l0);
		if (d0 < 0) continue;
		if (d1 == 0) {
			v_copy(l1, hit_point);
		} else if (d0 == 0) {
			v_copy(l0, hit_point);
		} else {
			i32 t = IDIV(W((i64)d0 * 0x1000 + SAR(d0 - d1, 1)), d0 - d1);
			i32 d[3];
			v_sub(l1, l0, d);
			for (int k = 0; k < 3; k++) hit_point[k] = SAR(W((i64)t * d[k] + 0x800), 12);
			v_add(l0, hit_point, hit_point);
		}
		int16_t uv[2];
		poly_project(p, hit_point, uv);
		if (poly_contains(p, uv)) {
			return p;
		}
	}
	return NULL;
}

// 0x42c1e0: closest polygon crossed by p0 -> p1 (x8); the time goes to rec->toi
static const phys_poly_t *segment_closest(const i32 *p0, const i32 *p1, const phys_cand_t *c, int n,
	phys_contact_t *rec) {
	const phys_cand_t *end = c + n;
	const phys_poly_t *best = NULL;
	i32 l0[3], l1[3];
	bool fresh = n != 0;
	for (; c != end; c++) {
		if ((c->word & 3) != 0) {
			frame_select(&c);
			fresh = true;
			continue;
		}
		if (fresh) {
			to_local(fr.end_fn, p1, l1);
			to_local(fr.start_fn, p0, l0);
			fresh = false;
		}
		const phys_poly_t *p = (const phys_poly_t *)c->ptr;
		i32 d1 = poly_dist(p, l1);
		if (d1 >= 1) continue;
		i32 d0 = poly_dist(p, l0);
		if (!(d1 < d0 && -0x21 < d0)) continue;
		i32 t;
		if (d1 == 0) {
			t = 0x1000;
			if (rec->toi <= 0x1000) continue;
			v_copy(l1, hit_point);
		} else if (d0 == 0) {
			if (rec->toi <= 0) continue;
			t = 0;
			v_copy(l0, hit_point);
		} else {
			t = IDIV(W((i64)d0 * 0x1000 + SAR(d0 - d1, 1)), d0 - d1);
			if (t >= rec->toi) continue;
			i32 d[3];
			v_sub(l1, l0, d);
			for (int k = 0; k < 3; k++) hit_point[k] = SAR(W((i64)t * d[k] + 0x800), 12);
			v_add(l0, hit_point, hit_point);
		}
		int16_t uv[2];
		poly_project(p, hit_point, uv);
		if (poly_contains(p, uv)) {
			rec->toi = (int16_t)t;
			rec->frame[0] = fr.tag[0];
			rec->frame[1] = fr.tag[1];
			rec->flags = fr.moving ? (rec->flags | PHYS_CF_MOVING) : (rec->flags & ~PHYS_CF_MOVING);
			last_kind = fr.kind;
			last_handle = fr.handle;
			best = p;
		}
	}
	if (best) {
		i32 d[3];
		v_sub(p1, p0, d);
		for (int k = 0; k < 3; k++) hit_point[k] = SAR(W((i64)d[k] * rec->toi + 0x800), 12);
		v_add(p0, hit_point, hit_point);
		rec->poly = best;
	}
	return best;
}

// World_SegmentHit 0x44a240 (+ 0x42c4b0, 0x42c560). The candidate gathering of the exe walks
// the octrees along the segment (World_QuerySegment 0x449f60); this port gathers the
// candidates of the segment's bounding box, a superset, which only matters for exact ties.
int phys_segment_hit(const i32 *p0, const i32 *p1, int flags, phys_hit_t *out) {
	i32 a[8] = {p0[0], p0[1], p0[2], 0, p0[0], p0[1], p0[2], 0};
	i32 b[8] = {p1[0], p1[1], p1[2], 0, p1[0], p1[1], p1[2], 0};
	num_cand = phys_query_box(a, b, cand, PHYS_MAX_CANDIDATES);
	if (flags == 0) {
		return segment_any_hit(p0, p1, cand, num_cand) == NULL;
	}
	i32 q0[3] = {SHL(p0[0], 3), SHL(p0[1], 3), SHL(p0[2], 3)};
	i32 q1[3] = {SHL(p1[0], 3), SHL(p1[1], 3), SHL(p1[2], 3)};
	seg_contact.toi = 0x1001;
	const phys_poly_t *p = segment_closest(q0, q1, cand, num_cand, &seg_contact);
	if (!p) {
		return 0;
	}
	for (int k = 0; k < 3; k++) hit_point[k] = SAR(hit_point[k], 3);
	if (out) {   // 0x42c560
		out->surface = p->surface;
		if (flags & 2) v_copy(hit_point, out->point);
		if (flags & 4) poly_normal_to_world(p, out->normal, seg_contact.frame);
		if (flags & 1) {
			out->frame_kind = last_kind;
			if (last_kind == PHYS_FRAME_OBJECT) out->object = (i32)last_handle;
		}
	}
	return 1;
}

// ==================================================================== bodies

// Force pool 0x42cdd0..0x42ce60 (63 records)
#define FORCE_POOL 63
static phys_force_t force_pool[FORCE_POOL];
static phys_force_t *force_free;
static bool force_pool_ready;

static void force_pool_init(void) {   // 0x42cdd0
	for (int i = 0; i < FORCE_POOL; i++) {
		force_pool[i].next = i + 1 < FORCE_POOL ? &force_pool[i + 1] : NULL;
	}
	force_free = &force_pool[0];
	force_pool_ready = true;
}
static phys_force_t *force_alloc(const i32 *f, int kind, const i32 *p) {   // 0x42cdf0
	if (!force_pool_ready) force_pool_init();
	phys_force_t *r = force_free;
	if (!r) {
		return NULL;   // pool exhausted: the force is lost (as in the exe)
	}
	force_free = r->next;
	v_copy(f, r->f);
	r->kind = kind;
	if (kind == 0) {
		v_copy(p, r->p);
	}
	return r;
}
static void force_push(phys_force_t **list, phys_force_t *r) {   // 0x42ce40
	if (r) {
		r->next = *list;
		*list = r;
	}
}
static void force_pop_free(phys_force_t **list) {   // 0x42ce60
	phys_force_t *r = *list;
	*list = r->next;
	r->next = force_free;
	force_free = r;
}

// Phys_AddForce 0x42d330: kind 0 = at point p8 (x8 world), kind 1 = through the centre
void phys_body_add_force(phys_body_t *b, const i32 *f, int kind, const i32 *p8) {
	force_push(&b->forces, force_alloc(f, kind, p8));
}

// 0x42d250: world positions of the contact points for a state
static void body_update_points(phys_body_t *b, phys_state_t *s) {
	for (int i = 0; i < b->num_points; i++) {
		apply_matrix_sv(s->mat.m, b->points[i], s->wp[i]);
		v_add(s->pos, s->wp[i], s->wp[i]);
	}
}
// 0x42d2a0 / 0x42d2e0
static void state_set_pos(phys_state_t *s, const i32 *p8) {
	s->mat.t[0] = SAR(p8[0], 3); s->mat.t[1] = SAR(p8[1], 3); s->mat.t[2] = SAR(p8[2], 3);
	v_copy(p8, s->pos);
}
static void state_reset(phys_state_t *s, const i32 *p8) {
	matrix_identity(s->mat.m);
	state_set_pos(s, p8);
	s->time = phys_g.game_time;
}

// Phys_AddContactPoint 0x42d550 (world units; stored x8 as s16)
void phys_body_add_point(phys_body_t *b, int x, int y, int z) {
	int n = b->num_points;
	if (n >= PHYS_MAX_POINTS) {
		return;   // the exe has no check
	}
	i32 u = SHL(x, 3), v = SHL(y, 3), w = SHL(z, 3);
	b->points[n][0] = (int16_t)u; b->points[n][1] = (int16_t)v; b->points[n][2] = (int16_t)w; b->points[n][3] = 0;
	b->state[1].wp[n][0] = b->state[0].pos[0] + u; b->state[0].wp[n][0] = b->state[0].pos[0] + u;
	b->state[1].wp[n][1] = b->state[0].pos[1] + v; b->state[0].wp[n][1] = b->state[0].pos[1] + v;
	b->state[1].wp[n][2] = b->state[0].pos[2] + w; b->state[0].wp[n][2] = b->state[0].pos[2] + w;
	if (b->extent[0] < abs(u)) b->extent[0] = abs(u);
	if (b->extent[1] < abs(v)) b->extent[1] = abs(v);
	if (b->extent[2] < abs(w)) b->extent[2] = abs(w);
	b->num_points++;
}

// Phys_AddMassPoint 0x42f960: point in world units, I += (r^2) * m / 4096 (24-bit float)
void phys_body_add_mass(phys_body_t *b, const i32 *p, i32 m) {
	b->mass += m;
	i32 x = SHL(p[0], 3), y = SHL(p[1], 3), z = SHL(p[2], 3);
	i32 xx = W((i64)x * x), yy = W((i64)y * y), zz = W((i64)z * z);
	b->inertia[0] += ftol((double)fmul_ii(W((i64)yy + zz), m) * (1.0 / 4096));
	b->inertia[1] += ftol((double)fmul_ii(W((i64)xx + zz), m) * (1.0 / 4096));
	b->inertia[2] += ftol((double)fmul_ii(W((i64)xx + yy), m) * (1.0 / 4096));
}

// Body_InertiaFromPoints 0x42fa30: inertia of mass/num_points at every contact point
void phys_body_inertia_from_points(phys_body_t *b) {
	i32 m = IDIV(b->mass, b->num_points);
	b->inertia[0] = b->inertia[1] = b->inertia[2] = 0;
	for (int i = 0; i < b->num_points; i++) {
		i32 x = b->points[i][0], y = b->points[i][1], z = b->points[i][2];
		b->inertia[0] += ftol((double)fmul_ii(y * y + z * z, m) * (1.0 / 4096));
		b->inertia[1] += ftol((double)fmul_ii(x * x + z * z, m) * (1.0 / 4096));
		b->inertia[2] += ftol((double)fmul_ii(x * x + y * y, m) * (1.0 / 4096));
	}
}

// Phys_BodyInit 0x42d620
void phys_body_init(phys_body_t *b, i32 mass) {
	static const i32 zero[3] = {0, 0, 0};
	b->cur = &b->state[0];
	b->next = &b->state[1];
	b->renorm_count = 8;
	b->forces = NULL;
	b->has_avg_normal = 0;
	b->unk498 = 0;
	b->no_extrapolate = 0;
	b->friction_sum = 0;
	b->impulse_sum = 0;
	b->grow_box = 0;
	b->coord = NULL;
	b->coord_time = -1;
	state_reset(b->cur, zero);
	state_reset(b->next, zero);
	b->torque[0] = b->torque[1] = b->torque[2] = 0;
	b->vel[0] = b->vel[1] = b->vel[2] = 0;
	b->angvel[0] = b->angvel[1] = b->angvel[2] = 0;
	b->num_points = 0;
	b->mass = mass;
	b->inertia[0] = b->inertia[1] = b->inertia[2] = 0;
	b->extent[0] = b->extent[1] = b->extent[2] = 0;
	for (int i = 0; i < PHYS_MAX_POINTS; i++) {
		b->contact[i].point = (int16_t)i;
		b->contact[i].flags = 0;
		b->contact[i].toi = 0x1001;
	}
}

// Phys_BodySetPos 0x42d760 (both states; orientation kept)
void phys_body_set_position(phys_body_t *b, const i32 *pos8) {
	state_set_pos(&b->state[0], pos8);
	state_set_pos(&b->state[1], pos8);
	body_update_points(b, &b->state[0]);
	body_update_points(b, &b->state[1]);
}

// Body_SetupFromDynamics 0x42ea40 ("Pod_SetupFromDynamicsBlock"): the 0xFACCFACE block
// {u32 nContact, u32 nMass, s32 mass, (s16 x, y, z, 0)[nContact + nMass]} of the model.
// rotate45: x' = (z - x) * 0xb50 >> 12, z' = (x + z) * 0xb50 >> 12 (the landers use it).
int phys_body_setup_dynamics(phys_body_t *b, const model_t *model, int rotate45, i32 mass) {
	uint32_t size = 0;
	const uint8_t *blk = model ? model_tagged_block(model, TAG_DYNAMICS, &size) : NULL;
	if (!blk) {
		return 2;
	}
	int nc = (int)get_u32(blk), nm = (int)get_u32(blk + 4);
	const uint8_t *pt = blk + 12;
	b->num_points = 0;
	for (int i = 0; i < nc; i++, pt += 8) {
		i32 x = get_i16(pt), y = get_i16(pt + 2), z = get_i16(pt + 4);
		if (rotate45) {
			i32 nx = SAR((z - x) * 0xb50, 12), nz = SAR((x + z) * 0xb50, 12);
			x = nx; z = nz;
		}
		phys_body_add_point(b, x, y, z);
	}
	b->mass = 0;
	b->inertia[0] = b->inertia[1] = b->inertia[2] = 0;
	if (mass == -1) {
		mass = get_i32(blk + 8);
	}
	for (int i = 0; i < nm; i++, pt += 8) {
		i32 p[3] = {get_i16(pt), get_i16(pt + 2), get_i16(pt + 4)};
		if (rotate45) {
			i32 nx = SAR((p[2] - p[0]) * 0xb50, 12), nz = SAR((p[0] + p[2]) * 0xb50, 12);
			p[0] = nx; p[2] = nz;
		}
		phys_body_add_mass(b, p, IDIV(mass, nm));
	}
	return 0;
}

// Body_InitBox 0x42fd40: 8 corner contact points, 8 mass points at 0.707 of the corners
void phys_body_init_box(phys_body_t *b, const i32 *size, const i32 *pos8, i32 mass) {
	i32 x = SAR(size[0], 1), y = SAR(size[1], 1), z = SAR(size[2], 1);
	phys_body_init(b, mass);
	phys_body_add_point(b, x, -y, z);
	phys_body_add_point(b, -x, -y, z);
	phys_body_add_point(b, -x, y, z);
	phys_body_add_point(b, x, y, z);
	phys_body_add_point(b, x, y, -z);
	phys_body_add_point(b, -x, y, -z);
	phys_body_add_point(b, -x, -y, -z);
	phys_body_add_point(b, x, -y, -z);
	b->mass = 0;
	i32 p[3] = {SAR(x * 0xb50, 12), SAR(y * 0xb50, 12), SAR(z * 0xb50, 12)};
	i32 m = SAR(mass, 3);
	phys_body_add_mass(b, p, m); p[0] = -p[0];
	phys_body_add_mass(b, p, m); p[1] = -p[1];
	phys_body_add_mass(b, p, m); p[0] = -p[0];
	phys_body_add_mass(b, p, m); p[2] = -p[2];
	phys_body_add_mass(b, p, m); p[0] = -p[0];
	phys_body_add_mass(b, p, m); p[1] = -p[1];
	phys_body_add_mass(b, p, m); p[0] = -p[0];
	phys_body_add_mass(b, p, m);
	phys_body_set_position(b, pos8);
}

// Phys_Reset 0x42f650: all 8 slots become 0x300 x 0x200 x 0x100 boxes and are freed
void phys_reset(void) {
	static const i32 size[3] = {0x300, 0x200, 0x100}, pos[3] = {0, 0, 0};
	force_pool_init();
	phys_num_bodies = 0;
	for (int i = 0; i < PHYS_MAX_BODIES; i++) {
		phys_body_init_box(&phys_bodies[i], size, pos, 0x1000);
		phys_bodies[i].cur = NULL;
	}
}

// Phys_BodyAlloc 0x42f6c0
phys_body_t *phys_body_alloc(void) {
	for (int i = 0; i < PHYS_MAX_BODIES; i++) {
		phys_body_t *b = &phys_bodies[i];
		if (!b->cur) {
			b->coord = NULL;
			b->on_contact = NULL;
			b->cur = &b->state[0];
			b->next = &b->state[1];
			b->owner = -1;
			phys_num_bodies++;
			return b;
		}
	}
	return NULL;
}

// Phys_BodyFree 0x42f720
void phys_body_free(phys_body_t *b) {
	phys_num_bodies--;
	b->cur = NULL;
	while (b->forces) {
		force_pop_free(&b->forces);
	}
}

// 0x42d7b0: torque of the point forces about the state's centre: sum F x r (r = p - c), raw
static void body_force_torque(const phys_body_t *b, const phys_state_t *s, i32 *t) {
	i32 x = 0, y = 0, z = 0;
	for (const phys_force_t *f = b->forces; f; f = f->next) {
		if (f->kind == 0) {
			i32 rx = f->p[0] - s->pos[0], ry = f->p[1] - s->pos[1], rz = f->p[2] - s->pos[2];
			z = W((i64)z + (i64)f->f[0] * ry - (i64)f->f[1] * rx);
			x = W((i64)x + (i64)rz * f->f[1] - (i64)f->f[2] * ry);
			y = W((i64)y + (i64)f->f[2] * rx - (i64)rz * f->f[0]);
		}
	}
	t[0] = x; t[1] = y; t[2] = z;
}

// 0x42dac0: M^T t in single precision, truncated
static void torque_to_body(const int16_t *m, const i32 *t, i32 *o) {
	for (int k = 0; k < 3; k++) {
		float f = fmul_if(t[0], (float)m[k] * (1.0f / 4096.0f));
		f = fmul_if(t[1], (float)m[3 + k] * (1.0f / 4096.0f)) + f;
		f = fmul_if(t[2], (float)m[6 + k] * (1.0f / 4096.0f)) + f;
		o[k] = ftol(f);
	}
}

static i32 clamp800(i32 v) {
	if (abs(v) > 0x800) return v > 0 ? 0x800 : -0x800;
	return v;
}

// Body_ApplyForces 0x42db70: consume the force list (and the body-frame torque accumulator)
// into the velocities. dv = F*512/m (rounded), dw = M (512 (M^T T + torque) / I); every
// component of both is clamped to +-0x800. Returns |dv.x|+|dv.y|+|dv.z| before clamping.
int phys_body_apply_forces(phys_body_t *b, phys_state_t *s) {
	i32 f[3] = {0, 0, 0}, t[3], dv[3], dw[3];
	for (const phys_force_t *r = b->forces; r; r = r->next) {
		v_add(r->f, f, f);
	}
	for (int k = 0; k < 3; k++) {
		dv[k] = SAR(IDIV(SHL(f[k], 10), b->mass) + 1, 1);
	}
	int sum = abs(dv[0]) + abs(dv[1]) + abs(dv[2]);
	for (int k = 0; k < 3; k++) dv[k] = clamp800(dv[k]);
	v_add(b->vel, dv, b->vel);
	body_force_torque(b, s, t);
	torque_to_body(s->mat.m, t, dw);
	for (int k = 0; k < 3; k++) {
		dw[k] = W((i64)dw[k] + b->torque[k]);
		dw[k] = ftol(fdiv_ii((double)((float)dw[k] * 512.0f), b->inertia[k]));   // fmul rounds to 24 bits
	}
	apply_matrix_lv(s->mat.m, dw, dw);
	for (int k = 0; k < 3; k++) dw[k] = clamp800(dw[k]);
	v_add(b->angvel, dw, b->angvel);
	while (b->forces) {
		force_pop_free(&b->forces);
	}
	b->torque[0] = b->torque[1] = b->torque[2] = 0;
	return sum;
}

// Body_Predict 0x42de30: next = cur advanced by frac (4.12 of a tick); next.time = time
void phys_body_predict(phys_body_t *b, i32 frac, i32 time) {
	phys_state_t *n = b->next, *c = b->cur;
	n->time = time;
	if (frac != 0 && (b->angvel[0] || b->angvel[1] || b->angvel[2])) {
		i32 axis[3];
		v_copy(b->angvel, axis);
		i32 mag = phys_normalise(axis);
		phys_rotate_axis_angle(c->mat.m, n->mat.m, SAR(W((i64)mag * frac), 12), axis);
	} else {
		memcpy(n->mat.m, c->mat.m, sizeof(n->mat.m));
	}
	if (frac == 0) {
		v_copy(c->pos, n->pos);
	} else if (frac == 0x1000) {
		v_add(c->pos, b->vel, n->pos);
	} else {
		i32 d[3];
		for (int k = 0; k < 3; k++) d[k] = SAR(W((i64)b->vel[k] * frac + 0x800), 12);
		v_add(c->pos, d, n->pos);
	}
	body_update_points(b, n);
}

// 0x42e120: count down, then re-orthonormalise the current matrix every tick
static void body_renorm_tick(phys_body_t *b) {
	if (--b->renorm_count == 0) {
		b->renorm_count = 1;
		matrix_orthonormalise(b->cur->mat.m);
	}
}
static void body_swap(phys_body_t *b) {   // 0x42e160
	phys_state_t *t = b->cur;
	b->cur = b->next;
	b->next = t;
}

// Phys_SyncCoord 0x42d920: cur (extrapolated by frac) -> transform node (pos >> 3)
void phys_body_write_transform(phys_body_t *b, phys_matrix_t *out, i32 frac) {
	phys_state_t *c = b->cur;
	if (!out) {
		return;
	}
	if (b->no_extrapolate) {
		frac = 0;
	}
	if (frac == 0) {
		for (int k = 0; k < 3; k++) out->t[k] = SAR(c->pos[k] + 4, 3);
	} else {
		i32 p[3];
		phys_scale(b->vel, frac, p);
		v_add(c->pos, p, p);
		for (int k = 0; k < 3; k++) out->t[k] = SAR(p[k] + 4, 3);
	}
	v_copy(out->t, c->mat.t);
	if (frac != 0) {
		i32 axis[3];
		v_copy(b->angvel, axis);
		if (axis[0] || axis[1] || axis[2]) {
			i32 a = SAR(W((i64)phys_normalise(axis) * frac), 12);
			if (a != 0) {
				phys_rotate_axis_angle(c->mat.m, out->m, a, axis);
				return;
			}
		}
	}
	memcpy(out->m, c->mat.m, sizeof(out->m));
}

// Body_Interpolate 0x42dfb0: render state at game time t (t in x8 units: the exe stores
// pos8 + v*f into the matrix translation)
void phys_body_interpolate(phys_body_t *b, phys_matrix_t *out, i32 time) {
	phys_state_t *c = b->cur;
	i32 f = IDIV(W((i64)(time - c->time) * 0x1000), 0x28);
	if (!b->angvel[0] && !b->angvel[1] && !b->angvel[2]) {
		memcpy(out->m, c->mat.m, sizeof(out->m));
	} else {
		i32 axis[3];
		v_copy(b->angvel, axis);
		i32 mag = phys_normalise(axis);
		phys_rotate_axis_angle(c->mat.m, out->m, SAR(W((i64)f * mag), 12), axis);
	}
	if (f == 0x1000) {
		v_add(c->pos, b->vel, out->t);
	} else {
		i32 d[3];
		for (int k = 0; k < 3; k++) d[k] = SAR(W((i64)b->vel[k] * f + 0x800), 12);
		v_add(c->pos, d, out->t);
	}
}

// 0x42d880 / 0x42d8b0
i32 phys_body_linear_energy(const phys_body_t *b) {
	i32 v = v_magnitude(b->vel);
	return SAR(W((i64)SAR(W((i64)b->mass * v), 12) * v), 1);
}
i32 phys_body_angular_energy(const phys_body_t *b) {
	i32 w[3];
	inv_matvec(b->cur->mat.m, b->angvel, w);
	return SAR(W((i64)SAR(W((i64)w[2] * w[2]), 12) * b->inertia[2]), 13) +
		SAR(W((i64)SAR(W((i64)w[1] * w[1]), 12) * b->inertia[1]), 13) +
		SAR(W((i64)SAR(W((i64)w[0] * w[0]), 12) * b->inertia[0]), 13);
}

// ==================================================================== contacts

// Body_FindContacts 0x42c5e0: sweep every free contact point from cur to next through the
// candidate polygons; the earliest crossing (within the surface skin) per point is recorded.
static int body_find_contacts(phys_body_t *b, const phys_cand_t *c, int n) {
	const phys_cand_t *end = c + n;
	int any = 0;
	ghost_touch = 0xffffffff;
	seg_contact.toi = 0x1001;
	while (c != end) {
		frame_select(&c);
		const phys_cand_t *first = c + 1;
		c = first;
		for (int i = 0; i < b->num_points; i++) {
			phys_contact_t *ct = &b->contact[i];
			if (ct->flags & (PHYS_CF_HIT | PHYS_CF_DONE)) {
				continue;
			}
			const i32 *w0 = b->cur->wp[i], *w1 = b->next->wp[i];
			i32 l0[3], l1[3];
			to_local(fr.start_fn, w0, l0);
			to_local(fr.end_fn, w1, l1);
			int found = 0;
			for (c = first; c != end && (c->word & 3) == 0; c++) {
				const phys_poly_t *p = (const phys_poly_t *)c->ptr;
				i32 d1 = poly_dist(p, l1);
				if (d1 > 0) continue;
				i32 d0 = poly_dist(p, l0);
				i32 thick = surface_thickness(p->surface);
				bool ghost = thick == -1;
				if (ghost) thick = 0;
				if (phys_g.liquid) d1 = d0 - thick;
				if (d0 <= d1 || d0 < -thick) continue;
				i32 t;
				if (d1 == 0) {
					t = 0x1000;
					if (ct->toi <= 0x1000) continue;
					v_copy(l1, hit_point);
				} else if (!phys_g.liquid && d0 != 0) {
					t = IDIV(W((i64)SHL(d0, 12) + SAR(d0 - d1, 1)), d0 - d1);
					if (t >= ct->toi || t < phys_g.min_toi) continue;
					i32 d[3];
					v_sub(l1, l0, d);
					for (int k = 0; k < 3; k++) hit_point[k] = SAR(W((i64)t * d[k] + 0x800), 12);
					v_add(l0, hit_point, hit_point);
				} else {
					if (ct->toi <= 0 || phys_g.min_toi > 0) continue;
					t = 0;
					v_copy(l0, hit_point);
				}
				int16_t uv[2];
				poly_project(p, hit_point, uv);
				if (!poly_contains(p, uv)) continue;
				if (ghost) {
					ghost_touch = fr.handle;
					continue;
				}
				ct->poly = p;
				ct->toi = (int16_t)t;
				ct->frame[0] = fr.tag[0];
				ct->frame[1] = fr.tag[1];
				ct->flags = fr.moving ? (ct->flags | PHYS_CF_MOVING) : (ct->flags & ~PHYS_CF_MOVING);
				ct->depth = d0 > 0 ? 0 : (int16_t)IDIV(SHL(-d0, 12), thick);
				found = 1;
				ct->frame_kind = (uint8_t)fr.kind;
				ct->object = fr.handle;
				last_kind = fr.kind;
				last_handle = fr.handle;
			}
			if (found) {   // contact point = lerp of the world points at the recorded time
				i32 d[3];
				v_sub(w1, w0, d);
				for (int k = 0; k < 3; k++) ct->pos[k] = SAR(W((i64)d[k] * ct->toi + 0x800), 12);
				v_add(w0, ct->pos, ct->pos);
				poly_normal_to_world(ct->poly, ct->normal, ct->frame);
			}
		}
		while (c != end && (c->word & 3) == 0) {   // skip to the next frame tag
			c++;
		}
	}
	// flag the new contacts; average normal = centre of the normals' bounding box
	i32 mx[3] = {INT32_MIN, INT32_MIN, INT32_MIN}, mn[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
	for (int i = 0; i < b->num_points; i++) {
		phys_contact_t *ct = &b->contact[i];
		if (!(ct->flags & (PHYS_CF_HIT | PHYS_CF_DONE)) && ct->toi != 0x1001) {
			ct->flags = (ct->flags & ~PHYS_CF_DONE) | PHYS_CF_HIT;
			const phys_cand_t *tp = ct->frame;
			if (ct->flags & PHYS_CF_MOVING) {   // velocity of the moving frame at the point
				frame_select(&tp);
			}
			if ((ct->flags & PHYS_CF_MOVING) && fr.moving && fr.m_start_raw) {
				i32 q[3], a[3], e[3];
				v_sub(fr.m_end->t, fr.m_start_raw->t, ct->frame_vel);
				q[0] = SAR(ct->pos[0], 3); q[1] = SAR(ct->pos[1], 3); q[2] = SAR(ct->pos[2], 3);
				v_sub(q, fr.m_start->t, q);
				inv_matvec(fr.m_start->m, q, q);
				apply_matrix_lv(fr.m_start_raw->m, q, a);
				apply_matrix_lv(fr.m_end->m, q, e);
				v_sub(e, a, q);
				v_add(q, ct->frame_vel, ct->frame_vel);
			}
			if (ct->toi < 0) {
				ct->toi = 0;
			}
		}
		if (ct->flags & PHYS_CF_HIT) {
			for (int k = 0; k < 3; k++) {
				if (ct->normal[k] < mn[k]) mn[k] = ct->normal[k];
				if (ct->normal[k] > mx[k]) mx[k] = ct->normal[k];
			}
			any = 1;
		}
	}
	for (int k = 0; k < 3; k++) {
		b->avg_normal[k] = SAR(W((i64)mx[k] + mn[k]), 1);
	}
	if (!b->avg_normal[0] && !b->avg_normal[1] && !b->avg_normal[2]) {
		b->avg_normal[1] = 0x1000;
		b->has_avg_normal = 0;
		return any;
	}
	phys_normalise(b->avg_normal);
	b->has_avg_normal = 1;
	return any;
}

// Body_PenetrationCorrection 0x42cca0: push along the average normal, <= 8 (1 world unit)
static int body_penetration(phys_body_t *b, i32 *out, phys_contact_t *ct) {
	if (!b->has_avg_normal) {
		return 0;
	}
	const phys_cand_t *tp = ct->frame;
	frame_select(&tp);
	i32 l[3];
	to_local(fr.start_fn, b->next->wp[ct->point], l);
	i32 d = poly_dist(ct->poly, l);
	if (d >= 0) {
		return 0;
	}
	v_copy(ct->normal, out);
	i32 k = v_dot(b->avg_normal, out);
	if (k < 1) {
		return 0;
	}
	i32 s = IDIV(W((i64)d * -0x1000), k);
	if (s > 8) {
		s = 8;
	}
	phys_scale(b->avg_normal, s, out);
	return 1;
}

// 0x42e1a0: n . (r x ((n x r) * 4096 / I)) for body-frame n and r (the angular part of the
// effective mass along n). Single precision divisions, truncated.
static i32 body_angular_term(const phys_body_t *b, const i32 *n, const i32 *r) {
	i32 c[3], a[3], t[3];
	c[0] = W((i64)r[2] * n[1] - (i64)n[2] * r[1]);
	c[1] = W((i64)n[2] * r[0] - (i64)r[2] * n[0]);
	c[2] = W((i64)n[0] * r[1] - (i64)r[0] * n[1]);
	for (int k = 0; k < 3; k++) {
		a[k] = ftol(fdiv_ii((double)((float)c[k] * 4096.0f), b->inertia[k]));
	}
	v_cross(r, a, t);
	return v_dot(n, t);
}

// Body_ContactResponse 0x42e280: impulse at one contact (other = the touched object's body).
// The impulse becomes a point force on the force list (and the opposite one on other).
// Returns the applied impulse (0 = separating).
static i32 body_contact_response(phys_body_t *b, phys_body_t *o, phys_contact_t *ct) {
	int i = ct->point;
	phys_state_t *s = b->next;
	i32 rl[3] = {b->points[i][0], b->points[i][1], b->points[i][2]};   // body-local r
	const i32 *wp = s->wp[i];
	i32 ro[3] = {0, 0, 0}, r[3], v[3], tmp[3], imp[3];
	i32 result = 0, jn, jf;
	if (o) {
		v_sub(wp, o->next->pos, ro);
		inv_matvec(o->next->mat.m, ro, ro);
	}
	v_sub(wp, s->pos, r);
	v_cross(r, b->angvel, v);
	v_add(v, b->vel, v);
	if (ct->flags & PHYS_CF_MOVING) {
		if (o) {
			i32 w[3];
			v_sub(wp, o->next->pos, tmp);
			v_cross(tmp, o->angvel, w);
			v_add(w, o->vel, tmp);
		} else {
			tmp[0] = SHL(ct->frame_vel[0], 3); tmp[1] = SHL(ct->frame_vel[1], 3); tmp[2] = SHL(ct->frame_vel[2], 3);
		}
		v_sub(v, tmp, v);
	}
	v_copy(v, ct->rel_vel);
	i32 inv_m = IDIV(0x1000000, b->mass);
	if (phys_g.liquid) {
		// buoyancy against gravity, proportional to the depth, plus drag against the motion
		i32 s1 = -SAR(W((i64)SAR(W((i64)SHL(b->mass, 3) * ct->depth), 12) * phys_g.bounce), 12);
		phys_scale(phys_g.gravity, s1, imp);
		i32 speed = phys_normalise(v);
		v[0] = -v[0]; v[1] = -v[1]; v[2] = -v[2];
		i32 nl[3];
		inv_matvec(s->mat.m, v, nl);
		i32 k = body_angular_term(b, nl, rl);
		i32 q = IDIV(SHL(speed, 15), inv_m + k);
		q = SAR(W((i64)SAR(W((i64)q * ct->depth), 12) * phys_g.friction), 12);
		phys_scale(v, q, tmp);
		v_add(tmp, imp, imp);
		b->impulse_sum = 1;
		result = 1;
		jn = 0;
		jf = 0;
	} else {
		const i32 *nrm = ct->normal;
		i32 vn = v_dot(v, nrm);
		if (vn >= 0) {
			return 0;
		}
		i32 vt[3], nl[3], tl[3], mag;
		phys_scale(nrm, vn, tmp);
		v_sub(v, tmp, vt);
		mag = (vt[0] || vt[1] || vt[2]) ? phys_normalise(vt) : 0;
		inv_matvec(s->mat.m, nrm, nl);
		i32 k1 = body_angular_term(b, nl, rl);
		i32 k2 = 0, inv_o = 0;
		if (o) {
			inv_matvec(o->next->mat.m, nrm, nl);
			k2 = body_angular_term(o, nl, ro);
			inv_o = IDIV(0x1000000, o->mass);
		}
		i32 sum = k2 + inv_o + inv_m;
		i32 j = -IDIV(SHL(vn, 15), k1 + sum);
		if (j < 0) {
			return 0;
		}
		b->impulse_sum += j;
		if (o) {
			o->impulse_sum += j;
		}
		if (phys_g.bounce != 0) {
			j += SAR(W((i64)j * phys_g.bounce), 12);
		}
		if (ct->depth > 0) {   // extra push for points that started inside the skin
			j += SAR(W((i64)SAR(ct->depth * ct->depth, 12) * j), 12);
		}
		phys_scale(nrm, j, imp);
		jn = j;
		result = j;
		jf = 0;
		if (mag != 0) {
			// friction: stop the sliding (stop), limited by 2 * mu * |vn| (limit)
			inv_matvec(s->mat.m, vt, tl);
			i32 kt = body_angular_term(b, tl, rl);
			if (o) {
				inv_matvec(o->next->mat.m, vt, tl);
				kt = body_angular_term(o, tl, rl);   // the exe passes the body's r here
			}
			i32 stop = IDIV(SHL(mag, 15), kt + sum);
			i32 limit = SAR(W(-(i64)vn * phys_g.friction), 11);
			jf = SAR(W(-((i64)W((i64)mag * vn) * phys_g.friction)), 12);
			if (mag < 0x20) {   // static friction blend at low sliding speed
				i32 a = W((i64)limit * mag);
				limit = (a + (SAR(a, 31) & 0x1f)) >> 5;
				if (vn < -0x10) vn = -0x10;
				i32 e = W((i64)-(i64)(0x20 - mag) * vn * stop * 2);
				limit += (e + (SAR(e, 31) & 0x1ff)) >> 9;
			}
			if (stop < limit) limit = stop;
			if (limit > 0x2000) limit = 0x2000;
			if (limit != 0) {
				b->friction_sum += limit;
				result += limit;
				phys_scale(vt, -limit, tmp);
				v_add(imp, tmp, imp);
			}
		}
	}
	phys_body_add_force(b, imp, 0, wp);
	if (b->on_contact) {
		b->on_contact(b, ct, jn, jf);
	}
	if (o) {
		imp[0] = -imp[0]; imp[1] = -imp[1]; imp[2] = -imp[2];
		phys_body_add_force(o, imp, 0, wp);
		if (o->on_contact) {
			o->on_contact(o, ct, jn, jf);
		}
	}
	return result;
}

// Physics_BroadPhase 0x42f760: box of the swept contact points (+64 units, +0x400 more up
// after a liquid contact) -> world query appended to the shared candidate list
static int body_broadphase(phys_body_t *b, int start) {
	i32 a[8], c[8];
	for (int i = 0; i < b->num_points; i++) {
		for (int k = 0; k < 3; k++) {
			i32 p = SAR(b->cur->wp[i][k], 3), q = SAR(b->next->wp[i][k], 3);
			if (i == 0 || p < a[k]) a[k] = p;
			if (i == 0 || p > a[4 + k]) a[4 + k] = p;
			if (i == 0 || q < c[k]) c[k] = q;
			if (i == 0 || q > c[4 + k]) c[4 + k] = q;
		}
	}
	if (b->num_points == 0) {
		memset(a, 0, sizeof(a));   // uninitialised in the exe
		memset(c, 0, sizeof(c));
	}
	for (int k = 0; k < 3; k++) {
		a[k] -= 0x40; c[k] -= 0x40;
		a[4 + k] += 0x40; c[4 + k] += 0x40;
	}
	if (b->grow_box) {
		a[5] += 0x400;
		c[5] += 0x400;
	}
	a[3] = a[7] = c[3] = c[7] = 0;
	return phys_query_box(a, c, cand + start, PHYS_MAX_CANDIDATES - start);
}

// ==================================================================== Phys_Step 0x42ec00

void phys_step(void) {
	i32 reached[PHYS_MAX_BODIES], done[PHYS_MAX_BODIES], stepped[PHYS_MAX_BODIES];
	i32 touched[PHYS_MAX_BODIES], ncand[PHYS_MAX_BODIES], cstart[PHYS_MAX_BODIES];
	struct { int16_t toi; uint8_t body, contact; } list[PHYS_MAX_BODIES * PHYS_MAX_POINTS];
	int iter = 0;
	num_cand = 0;
	phys_g.interp = 0;
	phys_g.min_toi = (i32)0xffff0001;
	// 1. forces -> velocities, predict the full tick, gather candidates
	for (int bi = 0; bi < PHYS_MAX_BODIES; bi++) {
		phys_body_t *b = &phys_bodies[bi];
		done[bi] = 0;
		reached[bi] = 0;
		if (!b->cur) {
			continue;
		}
		body_renorm_tick(b);
		b->impulse_sum = 0;
		b->friction_sum = 0;
		touched[bi] = -1;
		b->unk4b0[0] = b->unk4b0[1] = 0;
		for (int i = 0; i < b->num_points; i++) {
			b->contact[i].toi = 0x1001;
			b->contact[i].flags = 0;
			b->contact[i].point = (int16_t)i;
		}
		phys_body_apply_forces(b, b->cur);
		phys_body_predict(b, 0x1000, phys_g.step_end);
		cstart[bi] = num_cand;
		phys_g.exclude_owner = b->owner;    // DAT_00498434: the object query skips the owner
		int n = body_broadphase(b, num_cand);
		phys_g.exclude_owner = -1;
		num_cand += n;
		ncand[bi] = n;
		b->grow_box = 0;
	}
	// 2. time-of-impact rounds (at most 8)
	i32 t_prev = 0;
	for (;;) {
		bool big = false;
		for (int bi = 0; bi < PHYS_MAX_BODIES; bi++) {
			phys_body_t *b = &phys_bodies[bi];
			if (!b->cur) {
				continue;
			}
			phys_g.interp = reached[bi];
			done[bi] = 0;
			stepped[bi] = 0;
			if (ncand[bi] == 0) {
				done[bi] = 1;
				continue;
			}
			if (!body_find_contacts(b, cand + cstart[bi], ncand[bi])) {
				done[bi] = 1;
			}
			if (ghost_touch != 0xffffffff) {
				touched[bi] = (i32)ghost_touch;
			}
		}
		// earliest contact, the times rescaled from [reached, 1] to [0, 1]
		i32 t = 0x1000;
		for (int bi = 0; bi < PHYS_MAX_BODIES; bi++) {
			phys_body_t *b = &phys_bodies[bi];
			if (!b->cur || done[bi]) {
				continue;
			}
			for (int i = 0; i < b->num_points; i++) {
				phys_contact_t *ct = &b->contact[i];
				if (ct->toi < 0x1000) {
					ct->toi = (int16_t)(SAR((0x1000 - reached[bi]) * ct->toi, 12) + reached[bi]);
					if (ct->toi < t) t = ct->toi;
				}
			}
		}
		if (t == 0x1000) {
			break;
		}
		if (t < t_prev) {
			t = t_prev;
		}
		i32 t_next = t + 0x1000, limit = t_next > 0x1000 ? 0x1000 : t_next;
		i32 time = SAR(t * 0x28, 12) + phys_g.step_start;
		// every unresolved contact up to the limit (all of them, as t >= 0)
		int nl = 0;
		for (int bi = 0; bi < PHYS_MAX_BODIES; bi++) {
			phys_body_t *b = &phys_bodies[bi];
			if (!b->cur || done[bi]) {
				continue;
			}
			for (int i = 0; i < b->num_points; i++) {
				phys_contact_t *ct = &b->contact[i];
				if (!(ct->flags & PHYS_CF_DONE) && ct->toi <= limit) {
					list[nl].toi = ct->toi;
					list[nl].body = (uint8_t)bi;
					list[nl].contact = (uint8_t)i;
					nl++;
				}
			}
		}
		// resolve them, last collected first
		while (nl > 0) {
			nl--;
			int bi = list[nl].body;
			phys_body_t *b = &phys_bodies[bi];
			phys_contact_t *ct = &b->contact[list[nl].contact];
			if (!stepped[bi]) {
				phys_body_predict(b, t - reached[bi], time);
				reached[bi] = t;
				stepped[bi] = 1;
			}
			phys_body_t *o = NULL;
			if (ct->frame_kind == PHYS_FRAME_OBJECT) {
				o = world && world->object_body ? world->object_body(ct->object) : NULL;
				touched[bi] = (i32)ct->object;
				if (o) {
					int oi = (int)(o - phys_bodies);
					done[oi] = 0;
					if (!stepped[oi]) {
						phys_body_predict(o, t - reached[oi], time);
						reached[oi] = t;
						stepped[oi] = 1;
					}
				}
			}
			surface_select(ct->poly->surface);
			if (body_contact_response(b, o, ct)) {
				if (phys_body_apply_forces(b, b->next) > 3) big = true;
				if (o && phys_body_apply_forces(o, o->next) > 3) big = true;
			}
			if (phys_g.liquid) {
				b->grow_box = 1;
				ct->flags |= PHYS_CF_DONE;
			} else {
				i32 corr[3];
				if (body_penetration(b, corr, ct)) {
					v_add(b->next->pos, corr, b->next->pos);   // 0x42e180
					memcpy(b->cur->pos, b->next->pos, 4 * sizeof(i32));
				}
			}
		}
		// stepped bodies continue from the contact time to the end of the tick
		for (int bi = 0; bi < PHYS_MAX_BODIES; bi++) {
			phys_body_t *b = &phys_bodies[bi];
			if (b->cur && !done[bi] && stepped[bi]) {
				body_swap(b);
				phys_body_predict(b, 0x1000 - reached[bi], phys_g.step_end);
			}
		}
		t_prev = big ? t : t_next;
		if (t_prev >= 0x1000 || ++iter >= 8) {
			break;
		}
	}
	// 3. accept the predicted states, report touched objects (event 0x10)
	for (int bi = 0; bi < PHYS_MAX_BODIES; bi++) {
		phys_body_t *b = &phys_bodies[bi];
		if (!b->cur) {
			continue;
		}
		body_swap(b);
		b->touched = touched[bi];
		if (touched[bi] != -1 && world && world->touch_report) {
			world->touch_report(b->owner, (uint32_t)touched[bi], b->impulse_sum);
		}
	}
	phys_g.interp = 0;
	phys_g.min_toi = 0;
}

// SetGravity 0x4442f0: gravity = (0, -g, 0), rotated into the inertial frame when one is used
void phys_set_gravity(i32 g, const phys_matrix_t *inertial) {
	i32 v[3] = {0, -g, 0};
	if (inertial) {
		apply_matrix_lv(inertial->m, v, v);
	}
	v_copy(v, phys_g.gravity);
}

// PhysTask_Update 0x413890 (period 40 ms, the first task of every tick). In training
// missions (gTrainingMission 0x61f304) the lander update calls Phys_Step itself instead.
void phys_task(i32 t) {
	phys_g.game_time = t;
	phys_g.step_start = t;
	phys_g.step_end = t + PHYS_TICK_MS;
	phys_g.in_physics = true;
	phys_g.interp = 0;
	phys_step();
	phys_g.in_physics = false;
	phys_g.interp = 0;
}
