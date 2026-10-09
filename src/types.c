#include "types.h"

#define M(mat, row, col) (mat).m[(col) * 4 + (row)]

mat4_t mat4_identity(void) {
	mat4_t r = {{0}};
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
	return r;
}

mat4_t mat4_mul(mat4_t a, mat4_t b) {
	mat4_t r;
	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			float s = 0;
			for (int k = 0; k < 4; k++) {
				s += M(a, row, k) * M(b, k, col);
			}
			M(r, row, col) = s;
		}
	}
	return r;
}

mat4_t mat4_translate(vec3_t t) {
	mat4_t r = mat4_identity();
	M(r, 0, 3) = t.x;
	M(r, 1, 3) = t.y;
	M(r, 2, 3) = t.z;
	return r;
}

mat4_t mat4_scale(vec3_t s) {
	mat4_t r = mat4_identity();
	M(r, 0, 0) = s.x;
	M(r, 1, 1) = s.y;
	M(r, 2, 2) = s.z;
	return r;
}

mat4_t mat4_rotate_x(float a) {
	mat4_t r = mat4_identity();
	float c = cosf(a), s = sinf(a);
	M(r, 1, 1) = c; M(r, 1, 2) = -s;
	M(r, 2, 1) = s; M(r, 2, 2) = c;
	return r;
}

mat4_t mat4_rotate_y(float a) {
	mat4_t r = mat4_identity();
	float c = cosf(a), s = sinf(a);
	M(r, 0, 0) = c; M(r, 0, 2) = s;
	M(r, 2, 0) = -s; M(r, 2, 2) = c;
	return r;
}

mat4_t mat4_rotate_z(float a) {
	mat4_t r = mat4_identity();
	float c = cosf(a), s = sinf(a);
	M(r, 0, 0) = c; M(r, 0, 1) = -s;
	M(r, 1, 0) = s; M(r, 1, 1) = c;
	return r;
}

// Left-handed, camera looks down +z (like Direct3D); clip z in [0, w].
mat4_t mat4_perspective(float fov_y, float aspect, float near, float far) {
	mat4_t r = {{0}};
	float f = 1.0f / tanf(fov_y * 0.5f);
	M(r, 0, 0) = f / aspect;
	M(r, 1, 1) = f;
	M(r, 2, 2) = far / (far - near);
	M(r, 2, 3) = -near * far / (far - near);
	M(r, 3, 2) = 1;
	return r;
}

// Look-at for the game's right-handed, y-up world, producing the renderer's
// camera space (+x right, +y up, +z forward). Looking along +z with y up puts
// world -x on the right of the screen, as in the original.
mat4_t mat4_look_at(vec3_t eye, vec3_t target, vec3_t up) {
	vec3_t z = vec3_normalize(vec3_sub(target, eye));
	vec3_t x = vec3_normalize(vec3_cross(z, up));
	vec3_t y = vec3_cross(x, z);
	mat4_t r = mat4_identity();
	M(r, 0, 0) = x.x; M(r, 0, 1) = x.y; M(r, 0, 2) = x.z; M(r, 0, 3) = -vec3_dot(x, eye);
	M(r, 1, 0) = y.x; M(r, 1, 1) = y.y; M(r, 1, 2) = y.z; M(r, 1, 3) = -vec3_dot(y, eye);
	M(r, 2, 0) = z.x; M(r, 2, 1) = z.y; M(r, 2, 2) = z.z; M(r, 2, 3) = -vec3_dot(z, eye);
	return r;
}

vec4_t mat4_mul_vec4(mat4_t m, vec4_t v) {
	return (vec4_t){
		M(m, 0, 0) * v.x + M(m, 0, 1) * v.y + M(m, 0, 2) * v.z + M(m, 0, 3) * v.w,
		M(m, 1, 0) * v.x + M(m, 1, 1) * v.y + M(m, 1, 2) * v.z + M(m, 1, 3) * v.w,
		M(m, 2, 0) * v.x + M(m, 2, 1) * v.y + M(m, 2, 2) * v.z + M(m, 2, 3) * v.w,
		M(m, 3, 0) * v.x + M(m, 3, 1) * v.y + M(m, 3, 2) * v.z + M(m, 3, 3) * v.w,
	};
}

vec3_t mat4_mul_point(mat4_t m, vec3_t p) {
	vec4_t r = mat4_mul_vec4(m, (vec4_t){p.x, p.y, p.z, 1});
	return vec3(r.x, r.y, r.z);
}
