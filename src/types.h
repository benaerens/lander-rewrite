// Basic types and small math helpers shared by the whole rewrite.
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

typedef struct { float x, y; } vec2_t;
typedef struct { float x, y, z; } vec3_t;
typedef struct { float x, y, z, w; } vec4_t;

// 4x4 matrix, column vectors: v' = M * v. Stored column-major: m[col * 4 + row].
typedef struct { float m[16]; } mat4_t;

typedef struct { uint8_t r, g, b, a; } rgba_t;

#define PI 3.14159265358979323846f
#define FX12 4096.0f // the original engine's 4.12 fixed point: 0x1000 = 1.0

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define len(a) (sizeof(a) / sizeof((a)[0]))

static inline rgba_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return (rgba_t){r, g, b, a}; }

static inline vec3_t vec3(float x, float y, float z) { return (vec3_t){x, y, z}; }
static inline vec3_t vec3_add(vec3_t a, vec3_t b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3_t vec3_sub(vec3_t a, vec3_t b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3_t vec3_mulf(vec3_t a, float f) { return vec3(a.x * f, a.y * f, a.z * f); }
static inline float vec3_dot(vec3_t a, vec3_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3_t vec3_cross(vec3_t a, vec3_t b) {
	return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float vec3_len(vec3_t a) { return sqrtf(vec3_dot(a, a)); }
static inline vec3_t vec3_normalize(vec3_t a) {
	float l = vec3_len(a);
	return l > 0 ? vec3_mulf(a, 1.0f / l) : a;
}

mat4_t mat4_identity(void);
mat4_t mat4_mul(mat4_t a, mat4_t b);
mat4_t mat4_translate(vec3_t t);
mat4_t mat4_scale(vec3_t s);
mat4_t mat4_rotate_x(float a);
mat4_t mat4_rotate_y(float a);
mat4_t mat4_rotate_z(float a);
mat4_t mat4_perspective(float fov_y, float aspect, float near, float far);
mat4_t mat4_look_at(vec3_t eye, vec3_t target, vec3_t up);
vec4_t mat4_mul_vec4(mat4_t m, vec4_t v);
vec3_t mat4_mul_point(mat4_t m, vec3_t p);

#endif
