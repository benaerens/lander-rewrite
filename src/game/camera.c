// Lander (1999) game camera - faithful C translation. See camera.h and re/specs/camera-input-hud.md §1.
// Addresses are lander.EXE (D3D build) virtual addresses.
#include <math.h>
#include <string.h>

#include "camera.h"

cam_host_t cam_host;

// ---------------------------------------------------------------------------------------------
// fixed-point math (0x435450 .. 0x436960)

// x87 with the precision control at 24 bits (single player; network games use 53 bits): every
// arithmetic result is rounded to a float mantissa, _ftol truncates toward zero.
int32_t fx_rsin(int32_t a) {
	float x = (float)((double)a * 6.283185307179586); // fild; fmul qword 2pi
	x = x * 0.000244140625f;                          // fmul 1/4096 (exact)
	float s = (float)sin((double)x);                  // fsin
	return (int32_t)(s * 4096.0f);                    // fmul 4096 (exact); _ftol
}

int32_t fx_rcos(int32_t a) {
	float x = (float)((double)a * 6.283185307179586);
	x = x * 0.000244140625f;
	float c = (float)cos((double)x);
	return (int32_t)(c * 4096.0f);
}

int32_t fx_ratan2(int32_t y, int32_t x) {
	float r = (float)atan2((double)y, (double)x);     // fpatan
	r = r * 4096.0f;
	r = (float)((double)r * 0.15915494309189535);     // * 1/2pi
	return (int32_t)r;
}

int32_t fx_sqrt(int32_t v) {
	float r = (float)sqrt((double)v); // fsqrt (24-bit result)
	return (int32_t)lrintf(r);        // fistp, round to nearest
}

int32_t cvec_normalise(cvec3_t *v) {
	int32_t sq = v->y * v->y + v->z * v->z + v->x * v->x;
	if (sq == 0) {
		v->x = v->y = v->z = 0;
		return 0;
	}
	int32_t len, inv;
	if (sq < 0) { // int overflow: the original returns its argument pointer as the "length" here
		len = 0;
		inv = 0;
	} else {
		len = fx_sqrt(sq);
		inv = len ? (int32_t)(0x1000000LL / len) : 0;
	}
	v->x = (v->x * inv + 0x800) >> 12;
	v->z = (inv * v->z + 0x800) >> 12;
	v->y = (inv * v->y + 0x800) >> 12;
	return len;
}

cvec3_t cvec_scale(cvec3_t v, int32_t s) {
	return (cvec3_t){(v.x * s + 0x800) >> 12, (v.y * s + 0x800) >> 12, (v.z * s + 0x800) >> 12};
}

int32_t cvec_dot(cvec3_t a, cvec3_t b) {
	return (a.y * b.y + a.z * b.z + 0x800 + b.x * a.x) >> 12;
}

cvec3_t cvec_cross(cvec3_t a, cvec3_t b) {
	return (cvec3_t){
		((b.z * a.y - a.z * b.y) + 0x800) >> 12,
		((b.x * a.z - a.x * b.z) + 0x800) >> 12,
		((a.x * b.y - b.x * a.y) + 0x800) >> 12,
	};
}

int32_t cvec_length(cvec3_t v) {
	int32_t sq = v.y * v.y + v.z * v.z + v.x * v.x;
	return sq ? fx_sqrt(sq) : 0;
}

static cvec3_t cvec_add(cvec3_t a, cvec3_t b) { return (cvec3_t){a.x + b.x, a.y + b.y, a.z + b.z}; }
static cvec3_t cvec_sub(cvec3_t a, cvec3_t b) { return (cvec3_t){a.x - b.x, a.y - b.y, a.z - b.z}; }

cvec3_t cmat_apply(const cmatrix_t *m, cvec3_t v) { // ApplyMatrixLV 0x436110
	return (cvec3_t){
		(m->m[0][0] * v.x + m->m[0][2] * v.z + 0x800 + m->m[0][1] * v.y) >> 12,
		(m->m[1][0] * v.x + m->m[1][1] * v.y + 0x800 + m->m[1][2] * v.z) >> 12,
		(m->m[2][2] * v.z + m->m[2][0] * v.x + 0x800 + m->m[2][1] * v.y) >> 12,
	};
}

// MulMatrix 0x4355a0: m0 = m0 * m1, rounded, computed into a temporary
static void cmat_mul(int16_t m0[3][3], const int16_t m1[3][3]) {
	int16_t r[3][3];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r[i][j] = (int16_t)((m0[i][0] * m1[0][j] + m0[i][1] * m1[1][j] + m0[i][2] * m1[2][j] + 0x800) >> 12);
		}
	}
	memcpy(m0, r, sizeof(r));
}

// ---------------------------------------------------------------------------------------------
// view node (0x431730 .. 0x431b50)

void cam_view_identity(cam_view_t *v) { // 0x431730
	memset(v->mat.m, 0, sizeof(v->mat.m));
	v->mat.m[0][0] = v->mat.m[1][1] = v->mat.m[2][2] = 0x1000;
}

static void cam_view_orthonormalise(cam_view_t *v) { // 0x431760: rows
	cvec3_t r0 = {v->mat.m[0][0], v->mat.m[0][1], v->mat.m[0][2]};
	cvec3_t r1 = {v->mat.m[1][0], v->mat.m[1][1], v->mat.m[1][2]};
	cvec3_t r2;
	cvec_normalise(&r0);
	r2 = cvec_cross(r0, r1);
	cvec_normalise(&r2);
	r1 = cvec_cross(r2, r0);
	int16_t *m = &v->mat.m[0][0];
	m[0] = (int16_t)r0.x; m[1] = (int16_t)r0.y; m[2] = (int16_t)r0.z;
	m[3] = (int16_t)r1.x; m[4] = (int16_t)r1.y; m[5] = (int16_t)r1.z;
	m[6] = (int16_t)r2.x; m[7] = (int16_t)r2.y; m[8] = (int16_t)r2.z;
}

static void cam_view_rotate(cam_view_t *v, const int16_t r[3][3]) {
	cmat_mul(v->mat.m, r);
	v->counter++;
	if (v->counter > 0x10) {
		cam_view_orthonormalise(v);
		v->counter = 0;
	}
}

void cam_view_rotate_x(cam_view_t *v, int32_t a) { // 0x431a30
	int16_t c = (int16_t)fx_rcos(a), s = (int16_t)fx_rsin(a);
	const int16_t r[3][3] = {{0x1000, 0, 0}, {0, c, (int16_t)-s}, {0, s, c}};
	cam_view_rotate(v, r);
}

void cam_view_rotate_y(cam_view_t *v, int32_t a) { // 0x431b50
	int16_t c = (int16_t)fx_rcos(a), s = (int16_t)fx_rsin(a);
	const int16_t r[3][3] = {{c, 0, s}, {0, 0x1000, 0}, {(int16_t)-s, 0, c}};
	cam_view_rotate(v, r);
}

void cam_view_rotate_z(cam_view_t *v, int32_t a) { // 0x431ac0
	int16_t c = (int16_t)fx_rcos(a), s = (int16_t)fx_rsin(a);
	const int16_t r[3][3] = {{c, (int16_t)-s, 0}, {s, c, 0}, {0, 0, 0x1000}};
	cam_view_rotate(v, r);
}

void cam_view_set_axes(cam_view_t *v, cvec3_t z, cvec3_t y) { // 0x431830
	if (z.x == 0 && z.y == 0 && z.z == 0) {
		cam_view_identity(v);
		return;
	}
	int16_t (*m)[3] = v->mat.m;
	m[0][2] = (int16_t)z.x; m[1][2] = (int16_t)z.y; m[2][2] = (int16_t)z.z;    // col 2 = z
	m[0][1] = (int16_t)-y.x; m[1][1] = (int16_t)-y.y; m[2][1] = (int16_t)-y.z; // col 1 = -y
	m[0][0] = (int16_t)((z.y * y.z - y.y * z.z) >> 12);                         // col 0 = z x y
	m[1][0] = (int16_t)((y.x * z.z - z.x * y.z) >> 12);
	m[2][0] = (int16_t)((z.x * y.y - y.x * z.y) >> 12);
}

void cam_view_look_dir(cam_view_t *v, cvec3_t dir) { // 0x431920
	cvec3_t up = {0, 0x1000, 0};
	cvec_normalise(&dir);
	cvec3_t x = cvec_cross(dir, up);
	cvec_normalise(&x);
	cvec3_t y = cvec_cross(dir, x);
	cam_view_set_axes(v, dir, y);
}

void cam_view_look_at(cam_view_t *v, cvec3_t p) { // 0x431990
	cam_view_look_dir(v, (cvec3_t){p.x - v->mat.t[0], p.y - v->mat.t[1], p.z - v->mat.t[2]});
}

int32_t cam_projection_h(int32_t fov_angle, int32_t half_width) { // FwSetScreenAngle 0x434b60
	int32_t c = fx_rcos(fov_angle >> 1), s = fx_rsin(fov_angle >> 1);
	int32_t tanq = (s << 12) / c;
	return (half_width << 12) / tanq;
}

cam_basis_t cam_view_basis(const cam_view_t *v) {
	cam_basis_t b;
	for (int i = 0; i < 3; i++) {
		b.eye[i] = (float)v->mat.t[i];
		b.right[i] = v->mat.m[i][0] / 4096.0f;
		b.down[i] = v->mat.m[i][1] / 4096.0f;
		b.fwd[i] = v->mat.m[i][2] / 4096.0f;
	}
	return b;
}

// ---------------------------------------------------------------------------------------------
// AutoCam working state (globals 0x553900 .. 0x553a18)

const int32_t autocam_angles[5] = {-600, -350, 0, 350, 600};                        // 0x498d98
const int32_t autocam_fallback[5][4] = {{1, 2, 3, 4}, {2, 0, 3, 4}, {1, 3, 0, 4}, {2, 4, 1, 0}, {3, 2, 1, 0}}; // 0x498db0
#define AC_EYE_K 8    // 0x498d88
#define AC_LOOK_K 4   // 0x498d8c
#define AC_ROLL_K 16  // 0x498d90
#define AC_NO_TARGET 0xffffffffu

static struct {
	cmatrix_t target_mat;  // 0x553960 -> target matrix
	cvec3_t target;        // *0x553908
	cvec3_t look, eye;     // 0x553980, 0x553910 desired points
	cvec3_t look_acc, eye_acc;      // 0x5539e8, 0x553970
	int32_t look_w, eye_w;          // 0x553990, 0x553900
	cvec3_t look_acc_s, eye_acc_s;  // saved copies 0x553998, 0x5539d8
	int32_t look_w_s, eye_w_s;      // 0x553994, 0x553938
	cvec3_t heading;       // 0x553940
	cvec3_t target_vel;    // 0x5539f8
	int32_t saved_roll;    // 0x553964
	int32_t spring_k, spring_k2; // 0x553920, 0x553968
} ac;

static bool ac_fetch_target(const autocam_t *c, bool relocate) { // 0x44feb0
	if (!cam_host.target_matrix || !cam_host.target_matrix(c->target, &ac.target_mat)) {
		return false;
	}
	ac.target = (cvec3_t){ac.target_mat.t[0], ac.target_mat.t[1], ac.target_mat.t[2]};
	(void)relocate; // 1: ZoneLoc_Update(gTargetZoneTracker) full relocate, 0: ZoneTracker_Move
	if (cam_host.zones_track_target) {
		cam_host.zones_track_target(ac.target);
	}
	return true;
}

static cvec3_t ac_heading_from_angle(int32_t a) { // 0x44ff20 (y is left untouched by the original)
	int32_t s = fx_rsin(a), c = fx_rcos(a);
	return (cvec3_t){(int16_t)((s * 0xb50 + c * 0xb50) >> 12), ac.heading.y, (int16_t)((c * 0xb50 + s * -0xb50) >> 12)};
}

static void accum(cvec3_t base, cvec3_t *acc, cvec3_t p, int32_t w) { // 0x44f7b0
	acc->x += (p.x - base.x) * w;
	acc->y += (p.y - base.y) * w;
	acc->z += (p.z - base.z) * w;
}

static cvec3_t add_avg(cvec3_t base, cvec3_t acc, int32_t w) { // 0x44f800
	return (cvec3_t){base.x + acc.x / w, base.y + acc.y / w, base.z + acc.z / w};
}

static void ac_add_look(cvec3_t p, int32_t w) { accum(ac.look, &ac.look_acc, p, w); ac.look_w += w; } // 0x44f840
static void ac_add_eye(cvec3_t p, int32_t w) { accum(ac.eye, &ac.eye_acc, p, w); ac.eye_w += w; }     // 0x44f870

static void ac_save(void) { // 0x44f8a0
	ac.look_acc_s = ac.look_acc; ac.eye_acc_s = ac.eye_acc;
	ac.look_w_s = ac.look_w; ac.eye_w_s = ac.eye_w;
}

static void ac_restore(void) { // 0x44f900
	ac.look_acc = ac.look_acc_s; ac.eye_acc = ac.eye_acc_s;
	ac.look_w = ac.look_w_s; ac.eye_w = ac.eye_w_s;
}

static cvec3_t ac_inertial(cvec3_t v) {
	const cmatrix_t *f = cam_host.inertial_frame ? cam_host.inertial_frame() : NULL;
	return f ? cmat_apply(f, v) : v;
}

static void ac_term_base(void) { // 0x44f960
	ac_add_look(ac.target, 1);
	cvec3_t v = ac_inertial(cvec_scale(ac.heading, 0x500));
	ac_add_eye(cvec_sub(ac.target, v), 1);
}

static void ac_term_velocity(void) { // 0x44fb80
	int32_t w = 0;
	cvec3_t lp = {0, 0, 0}, ep = {0, 0, 0}; // uninitialised in the original; weight 0 makes them irrelevant
	if (ac.target_vel.x != 0 || ac.target_vel.y != 0 || ac.target_vel.z != 0) {
		cvec3_t d = ac.target_vel;
		w = cvec_normalise(&d);
		d = cvec_scale(d, 0x500);
		lp = cvec_add(ac.target, d);
		ep = cvec_sub(ac.target, d);
		w >>= 4;
	}
	ac_add_look(lp, w);
	ac_add_eye(ep, w);
}

static void ac_term_elevation(int32_t angle) { // 0x44f9e0
	cvec3_t v = cvec_scale(ac.heading, 0x500);
	int32_t hx = ac.heading.x, hz = ac.heading.z;
	int32_t hz2 = hz * hz >> 12, hx2 = hx * hx >> 12;
	int32_t c = fx_rcos(angle);
	int32_t hxhz = hx * hz;
	int32_t s = fx_rsin(angle);
	cmatrix_t r;
	r.m[0][0] = (int16_t)(((0x1000 - hz2) * c >> 12) + hz2);
	r.m[2][2] = (int16_t)(((0x1000 - hx2) * c >> 12) + hx2);
	r.m[1][1] = (int16_t)c;
	r.m[1][0] = (int16_t)(s * hx >> 12);
	r.m[0][1] = (int16_t)-r.m[1][0];
	r.m[2][1] = (int16_t)(-(s * hz) >> 12);
	r.m[1][2] = (int16_t)-r.m[2][1];
	r.m[0][2] = r.m[2][0] = (int16_t)(((-hxhz) >> 12) * (0x1000 - c) >> 12);
	v = ac_inertial(cmat_apply(&r, v));
	cvec3_t vel = ac.target_vel;
	int32_t len = cvec_normalise(&vel);
	int32_t w;
	if ((len >> 6) == 0 || (w = (int32_t)(0x1000 / (len >> 6)) >> 2) > 0x100) {
		w = 0x100;
	}
	ac_add_eye(cvec_sub(ac.target, v), w);
	ac_add_look(ac.target, w);
}

static bool ac_is_blocked(const autocam_t *c) { // 0x44fc40
	cvec3_t look = add_avg(ac.look, ac.look_acc, ac.look_w);
	cvec3_t eye = add_avg(ac.eye, ac.eye_acc, ac.eye_w);
	cvec3_t hit;
	if (!cam_host.segment_hit) {
		return false;
	}
	if (cam_host.segment_hit(look, eye, 0xfffffffe, c->target, &hit)) { // tiles + objects
		return true;
	}
	return cam_host.segment_hit(eye, look, 1, c->target, &hit); // zones, reversed (one-sided polys)
}

static void ac_pull_in_front(const autocam_t *c) { // 0x44fd30
	cvec3_t look = add_avg(ac.look, ac.look_acc, ac.look_w);
	cvec3_t eye = add_avg(ac.eye, ac.eye_acc, ac.eye_w);
	cvec3_t dir = cvec_sub(eye, look);
	int32_t d = cvec_normalise(&dir);
	if (d > 400) {
		d = 0;
	}
	cvec3_t hit;
	if (cam_host.segment_hit && cam_host.segment_hit(look, eye, 0xfffffffe, c->target, &hit)) {
		d = cvec_length(cvec_sub(hit, look)) - 0x20;
		if (d < 2) {
			d = 1;
		}
	}
	if (d != 0) {
		if (d < 400) {
			d = 400;
		}
		cvec3_t e = cvec_add(look, cvec_scale(dir, d));
		ac.eye_acc = (cvec3_t){0, 0, 0};
		ac.eye_w = 0;
		ac_add_eye(e, 0x100);
	}
}

static void ac_approach_angle(autocam_t *c, int32_t a) { // 0x44ff70
	if (c->cur_angle < a) {
		c->cur_angle += 0x10;
		if (a < c->cur_angle) c->cur_angle = a;
	} else {
		c->cur_angle -= 0x10;
		if (c->cur_angle < a) c->cur_angle = a;
	}
}

static void ac_compute_desired(autocam_t *c) { // 0x44ffa0
	if (c->target == AC_NO_TARGET || !ac_fetch_target(c, false)) {
		return;
	}
	ac.target_vel = cvec_sub(ac.target, c->last_target);
	c->last_target = ac.target;
	cvec3_t fwd = {ac.target_mat.m[0][2], ac.target_mat.m[1][2], ac.target_mat.m[2][2]}; // 0x553928
	cvec3_t h;
	int32_t len = -1, yaw;
	switch (c->flags & 3) {
	case 2:
		if (c->orbit != -1) {
			ac.heading = ac_heading_from_angle(c->orbit);
			break;
		}
		/* fall through */ // no orbit angle: as mode 0
	case 0:
		if ((c->target & 0xff0000) == 0x10000) { // object type 1 = Lander: its yaw (0 if not found)
			yaw = 0;
			if (cam_host.lander_yaw) cam_host.lander_yaw(c->target, &yaw);
			ac.heading = ac_heading_from_angle(yaw);
			break;
		}
		h = (cvec3_t){fwd.x, 0, fwd.z};
		len = cvec_normalise(&h);
		if (len != 0) ac.heading = h;
		break;
	case 1:
		h = (cvec3_t){ac.target_vel.x, 0, ac.target_vel.z};
		len = cvec_normalise(&h);
		if (len != 0) ac.heading = h;
		break;
	default:
		break;
	}
	ac.look_acc = ac.eye_acc = (cvec3_t){0, 0, 0};
	ac.look_w = ac.eye_w = 0;
	ac_term_base();
	if (!(c->flags & AC_FLAG_BASE_ONLY)) {
		ac_term_velocity();
		ac_save();
		int32_t a = autocam_angles[c->user_angle];
		ac_term_elevation(a);
		if (!ac_is_blocked(c)) {
			ac_approach_angle(c, a);
			if (c->cur_angle != a) {
				ac_restore();
				ac_term_elevation(c->cur_angle);
			}
		} else {
			int k;
			for (k = 0; k < 2; k++) {
				int32_t a2 = autocam_angles[autocam_fallback[c->user_angle][k]];
				ac_restore();
				ac_term_elevation(a2);
				if (!ac_is_blocked(c)) {
					ac_approach_angle(c, a2);
					if (c->cur_angle != a2) {
						ac_restore();
						ac_term_elevation(c->cur_angle);
					}
					break;
				}
			}
			if (k == 2) {
				ac_restore();
				ac_approach_angle(c, a);
				ac_term_elevation(c->cur_angle);
				ac_pull_in_front(c);
			}
		}
	}
	ac.look = add_avg(ac.look, ac.look_acc, ac.look_w);
	ac.eye = add_avg(ac.eye, ac.eye_acc, ac.eye_w);
}

static void spring_set_k(int32_t k) { ac.spring_k = k; ac.spring_k2 = k * k; } // 0x450250

static void spring_step(int32_t *pos, int32_t *vel, int32_t target) { // 0x450270
	int32_t v = *vel + ((ac.spring_k2 >> 1) + ((target - ac.spring_k * *vel) - *pos) * 2) / ac.spring_k2;
	*vel = v;
	if (v != 0) {
		*pos += v;
	} else if (target < *pos) {
		*pos -= 1;
	} else if (*pos < target) {
		*pos += 1;
	}
}

static void ac_springs(autocam_t *c) { // 0x4502d0
	if (c->target == AC_NO_TARGET) {
		return;
	}
	cvec3_t look_vel_old = c->look_vel;
	cvec3_t dir_old = cvec_sub(c->look, c->eye);
	cvec_normalise(&dir_old);
	if (!(c->flags & AC_FLAG_FREEZE_EYE)) {
		spring_set_k(AC_EYE_K);
		spring_step(&c->eye.x, &c->eye_vel.x, ac.eye.x);
		spring_step(&c->eye.z, &c->eye_vel.z, ac.eye.z);
		spring_set_k(c->lag_y);
		spring_step(&c->eye.y, &c->eye_vel.y, ac.eye.y);
	}
	if (!(c->flags & AC_FLAG_FREEZE_LOOK)) {
		spring_set_k(AC_LOOK_K);
		spring_step(&c->look.x, &c->look_vel.x, ac.look.x);
		spring_step(&c->look.y, &c->look_vel.y, ac.look.y);
		spring_step(&c->look.z, &c->look_vel.z, ac.look.z);
	}
	if (!(c->flags & AC_FLAG_NO_ROLL)) {
		cvec3_t dir_new = cvec_sub(c->look, c->eye);
		cvec_normalise(&dir_new);
		cvec3_t turn = cvec_cross(dir_old, dir_new);
		int32_t s = cvec_dot(dir_new, look_vel_old) - 0x10;
		if (s < 4) s = 4;
		spring_set_k(AC_ROLL_K);
		spring_step(&c->roll, &c->roll_vel, turn.y * -0x20 * s >> 6);
	}
}

static void ac_track_zones(const autocam_t *c) {
	if (cam_host.zones_camera_from_target) {
		cam_host.zones_camera_from_target(ac.target, c->eye);
	}
}

void autocam_create(autocam_t *c, cam_view_t *view, uint32_t own_id, bool demo_playback, bool from_record) { // 0x450e10
	memset(c, 0, sizeof(*c));
	c->flags = demo_playback ? 0x802 : 0x800;
	c->user_angle = 1;
	c->cur_angle = autocam_angles[1]; // DAT_00498d9c
	c->lag_y = 8;
	c->orbit = -1;
	if (!from_record) {
		view->mat.t[0] = 0;
		view->mat.t[1] = 0x21d;
		view->mat.t[2] = 0x400;
	}
	c->id = own_id;
	c->target = AC_NO_TARGET;
	c->eye = (cvec3_t){view->mat.t[0], view->mat.t[1], view->mat.t[2]};
	c->look = (cvec3_t){view->mat.t[0] + view->mat.m[0][2], view->mat.t[1] + view->mat.m[1][2],
		view->mat.t[2] + view->mat.m[2][2]};
	// Camera_SetTarget(own_id): gCameraObject = own_id, gCameraTask = this task (done by the caller)
}

void autocam_snap(autocam_t *c, cam_view_t *view) { // 0x4509d0
	if (!ac_fetch_target(c, true)) {
		return;
	}
	if (!(c->flags & AC_FLAG_FREEZE_LOOK)) {
		ac.look = ac.target;
	}
	if (!(c->flags & AC_FLAG_FREEZE_EYE)) {
		ac.eye = ac.look;
	} else {
		ac.eye = (cvec3_t){view->mat.t[0], view->mat.t[1], view->mat.t[2]};
	}
	ac.saved_roll = (c->flags & AC_FLAG_NO_ROLL) ? c->roll : 0;
	c->last_target = ac.target;
	if (!(c->flags & AC_FLAG_SNAP)) {
		return;
	}
	if (c->flags & AC_FLAG_FREEZE_LOOK) {
		c->look = ac.target;
		ac.heading = cvec_sub(ac.target, ac.eye);
		ac.heading.y = 0;
		cvec_normalise(&ac.heading);
	}
	cvec3_t v = cam_host.target_velocity ? cam_host.target_velocity(c->target) : (cvec3_t){0, 0, 0};
	cvec3_t d = {v.x * 0x28 / 1000, v.y * 0x28 / 1000, v.z * 0x28 / 1000}; // per 40 ms tick
	c->last_target = cvec_sub(ac.target, d);
	c->roll = 0;
	c->roll_vel = 0;
	cvec3_t look_s = ac.look, eye_s = ac.eye;
	int32_t roll_s = ac.saved_roll;
	ac_compute_desired(c);
	c->look = (c->flags & AC_FLAG_FREEZE_LOOK) ? look_s : ac.look;
	c->eye = (c->flags & AC_FLAG_FREEZE_EYE) ? eye_s : ac.eye;
	c->roll = (c->flags & AC_FLAG_NO_ROLL) ? roll_s : ac.saved_roll;
	c->eye_vel = c->look_vel = (cvec3_t){0, 0, 0};
	cam_view_look_at(view, c->look);
	ac_track_zones(c); // ZoneLoc_Update(gCameraZoneTracker, eye) in the original
	ac_compute_desired(c);
}

void autocam_set_target(autocam_t *c, cam_view_t *view, uint32_t target) { // 0x450fe0
	c->target = target;
	autocam_snap(c, view);
}

void autocam_update(autocam_t *c) { // 0x450480
	if ((c->flags & AC_FLAG_ORBIT_KEYS) && cam_host.key_down) {
		bool left = cam_host.key_down(0);
		if (left || cam_host.key_down(1)) {
			c->orbit = (c->orbit + (left ? -0x20 : 0x20)) & 0xfff;
		}
		if (c->orbit != -1 && cam_host.key_pressed) {
			if (cam_host.key_pressed(2) && --c->user_angle < 0) c->user_angle = 0;
			if (cam_host.key_pressed(3) && ++c->user_angle >= 5) c->user_angle = 4;
		}
	}
	// DAT_00498d94 = autocam_angles[c->user_angle] (never read)
	ac_compute_desired(c);
	ac_springs(c);
	ac_track_zones(c);
}

// AutoCam_Render 0x450650 + AutoCam_Frame 0x4505a0
void autocam_frame(autocam_t *c, cam_view_t *view, uint32_t t, uint32_t wake) {
	int32_t period = AUTOCAM_PERIOD_MS;
	int32_t frac = (((int32_t)(t - wake) + period) * 0x1000) / period;
	cvec3_t eye = cvec_add(c->eye, cvec_scale(c->eye_vel, frac));
	view->mat.t[0] = eye.x;
	view->mat.t[1] = eye.y;
	view->mat.t[2] = eye.z;
	cvec3_t look = cvec_add(c->look, cvec_scale(c->look_vel, frac));
	const cmatrix_t *inertial = cam_host.inertial_frame ? cam_host.inertial_frame() : NULL;
	if (!inertial) {
		cam_view_look_at(view, look);
	} else {
		cvec3_t dir = cvec_sub(look, eye);
		cvec3_t up = cam_host.inertial_up ? cam_host.inertial_up() : (cvec3_t){0, 0x1000, 0};
		cvec_normalise(&dir);
		cvec3_t x = cvec_cross(dir, up);
		cvec_normalise(&x);
		up = cvec_cross(dir, x);
		cam_view_set_axes(view, dir, up);
	}
	cam_view_rotate_z(view, ((((c->roll_vel * frac) >> 12) + c->roll) >> 5) + 0x800);
	view->counter = 0;
	// occlusion test: lookR -> eye against zones only
	cvec3_t hit;
	bool occluded = cam_host.segment_hit && cam_host.segment_hit(look, eye, 1, AC_NO_TARGET, &hit);
	if (occluded && cam_host.zones_camera_from_target) {
		cam_host.zones_camera_from_target(ac.target, eye); // walk from the target to the rendered eye
	}
	if (cam_host.set_occluded) {
		cam_host.set_occluded(occluded);
	}
}

int32_t autocam_get_user_angle(const autocam_t *c) { return c->user_angle; } // 0x450f60

void autocam_set_user_angle(autocam_t *c, int32_t idx) { // 0x450f80
	if (c->orbit == -1) {
		int32_t d = c->cur_angle - autocam_angles[c->user_angle];
		if (d < 0) d = -d;
		if (d < 100) {
			c->cur_angle = autocam_angles[idx];
		}
		c->user_angle = idx;
	}
}

void autocam_set_lag_y(autocam_t *c, int32_t k) { c->lag_y = k; } // 0x450fc0

cvec3_t autocam_velocity_per_second(const autocam_t *c) { // 0x451060
	int32_t p = AUTOCAM_PERIOD_MS;
	return (cvec3_t){c->eye_vel.x * 1000 / p, c->eye_vel.y * 1000 / p, c->eye_vel.z * 1000 / p};
}

void autocam_set_pos(autocam_t *c, cvec3_t new_pos, bool target_is_local_player) { // 0x4510e0
	if (!target_is_local_player) {
		return;
	}
	cvec3_t d = cvec_sub(new_pos, c->eye);
	c->eye = cvec_add(c->eye, d);
	c->look = cvec_add(c->look, d);
	c->last_target = cvec_add(c->last_target, d);
	ac.eye = cvec_add(ac.eye, d);
	ac.look = cvec_add(ac.look, d);
	ac_fetch_target(c, true);
	ac_track_zones(c);
	// ViewRangeTask_Update(0) follows in the original
}

static cvec3_t teleport_point(cvec3_t p, const cmatrix_t *r, cvec3_t pivot, cvec3_t t) { // 0x450cb0
	cvec3_t q = cvec_sub(cvec_add(t, p), pivot);
	return cvec_add(pivot, cmat_apply(r, q));
}

void autocam_teleport(autocam_t *c, const cmatrix_t *rot, cvec3_t pivot, cvec3_t t) { // 0x450d10
	c->last_target = teleport_point(c->last_target, rot, pivot, t);
	c->eye = teleport_point(c->eye, rot, pivot, t);
	c->look = teleport_point(c->look, rot, pivot, t);
	ac.eye = teleport_point(ac.eye, rot, pivot, t);
	ac.look = teleport_point(ac.look, rot, pivot, t);
	c->look_vel = cmat_apply(rot, c->look_vel);
	c->eye_vel = cmat_apply(rot, c->eye_vel);
	ac_fetch_target(c, true);
	ac_track_zones(c);
}

// ---------------------------------------------------------------------------------------------
// FlyCam (debug) 0x459330 .. 0x459700

static void flycam_set_view(const flycam_t *f, cam_view_t *view, int32_t yaw, int32_t pitch) { // 0x459330
	cam_view_identity(view);
	view->counter = 0x10; // so the first rotation renormalises immediately
	cam_view_rotate_y(view, yaw);
	cam_view_rotate_x(view, pitch);
	cam_view_rotate_z(view, 0x800 - f->yaw_vel);
}

void flycam_create(flycam_t *f, cam_view_t *view, const cvec3_t *pos, int32_t yaw) { // 0x459700
	memset(f, 0, sizeof(*f));
	if (pos) {
		f->pos = *pos;
		f->yaw = yaw; // record: packed rotation bits 10..20, sign-extended
	} else {
		f->pos = (cvec3_t){0, 0x21d, 0x400};
		f->yaw = 0x800;
	}
	view->mat.t[0] = f->pos.x;
	view->mat.t[1] = f->pos.y;
	view->mat.t[2] = f->pos.z;
	// Player_Add + Camera_SetTarget(id) + gCameraTask = task in the original
}

static int32_t decay4(int32_t v) { // 0x40c740
	if (v < 0) { v += 4; return v > 0 ? 0 : v; }
	v -= 4;
	return v < 0 ? 0 : v;
}

void flycam_update(flycam_t *f, cam_view_t *view, const flycam_input_t *in) { // 0x4593e0
	f->yaw += f->yaw_vel >> 2;
	f->pitch += f->pitch_vel >> 2;
	f->pos = cvec_add(f->pos, f->vel);
	flycam_set_view(f, view, f->yaw, f->pitch);
	f->vel = (cvec3_t){view->mat.m[0][2], view->mat.m[1][2], view->mat.m[2][2]}; // forward
	int buttons = (in->lmb ? 1 : 0) | (in->rmb ? 2 : 0);
	if (buttons) {
		f->yaw_vel -= in->mouse_dx;
		f->pitch_vel = -in->mouse_dy << 4;
	} else {
		f->pitch_vel = 0;
		if (f->yaw_vel < 0) {
			f->yaw_vel += 8;
			if (f->yaw_vel > 0) f->yaw_vel = 0;
		} else {
			f->yaw_vel -= 8;
			if (f->yaw_vel < 0) f->yaw_vel = 0;
		}
	}
	switch (buttons) {
	case 0: case 1: f->speed = decay4(f->speed); break;
	case 2: f->speed += 4; break;
	case 3: f->speed = 1; break; // the IsDown(RMB) result left in eax
	}
	f->vel = cvec_scale(f->vel, f->speed);
	cvec3_t next = cvec_add(f->pos, f->vel);
	cvec3_t hit;
	if (!in->rshift && cam_host.segment_hit && cam_host.segment_hit(f->pos, next, 0xffffffff, AC_NO_TARGET, &hit)) {
		f->speed = -(f->speed >> 1);
		f->vel = cvec_scale(f->vel, f->speed); // scales the velocity again, as the original does
	}
	// camera zone tracker follows f->pos; X pressed spawns a test projectile (type 2) - omitted
}

void flycam_frame(flycam_t *f, cam_view_t *view, uint32_t t, uint32_t wake, uint32_t period) { // 0x4596a0
	uint32_t frac = ((t - wake + period) << 12) / period;
	cvec3_t p = cvec_add(f->pos, cvec_scale(f->vel, (int32_t)frac)); // 0x459390
	view->mat.t[0] = p.x;
	view->mat.t[1] = p.y;
	view->mat.t[2] = p.z;
	int32_t dyaw = ((int32_t)frac * f->yaw_vel) >> 12;
	int32_t pitch = (((int32_t)frac * f->pitch_vel) >> 14) + f->pitch;
	flycam_set_view(f, view, (dyaw >> 2) + f->yaw, pitch);
}

// ---------------------------------------------------------------------------------------------
// AnimCam (0x455050): the animation (mission.md §4) has written view->mat; its camera looks along +X.
void animcam_post(cam_view_t *view) {
	cam_view_rotate_y(view, 0x400);
	cam_view_rotate_z(view, 0x800);
}
