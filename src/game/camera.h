// Lander (1999) game camera: AutoCam (object type 6), FlyCam (type 7), AnimCam (type 5) post-step,
// and the fixed-point helpers they use. Faithful translation of lander.EXE (D3D build) 0x44f7b0-0x451210,
// 0x4593e0-0x4597e0, 0x455050, 0x431830-0x431b50. Spec: re/specs/camera-input-hud.md section 1.
//
// Everything is integer math exactly like the original (the camera position feeds object waking, so
// demo playback needs it bit-exact). The engine services the camera needs (target matrix, world segment
// query, zone tracking, keys, inertial frame) are reached through cam_host_t callbacks.
#ifndef LANDER_PORT_CAMERA_H
#define LANDER_PORT_CAMERA_H

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------------------------
// fixed-point basics (4096 = 1.0 / one full turn)
typedef struct { int32_t x, y, z; } cvec3_t;

// PSX-style MATRIX: m[row][col] in 4.12, columns = the node's axes in the parent frame, t = translation.
typedef struct {
	int16_t m[3][3];
	int32_t t[3];
} cmatrix_t;

// The camera view node (gView 0x4e9178): only the fields the camera code touches.
typedef struct {
	int16_t counter; // +0x00 incremental-rotation counter (renormalise after 16)
	cmatrix_t mat;   // +0x0c rotation, +0x20 eye position
} cam_view_t;

int32_t fx_rsin(int32_t a);              // 0x435450 trunc(sin(a*2pi/4096) * 4096), x87 single precision
int32_t fx_rcos(int32_t a);              // 0x435470
int32_t fx_ratan2(int32_t y, int32_t x); // 0x435490 trunc(atan2(y, x) * 4096 / 2pi)
int32_t fx_sqrt(int32_t v);              // 0x4354e0 FwSqrt: round to nearest
int32_t cvec_normalise(cvec3_t *v);      // 0x436830 FwVectorNormalise: returns the length
cvec3_t cvec_scale(cvec3_t v, int32_t s);              // 0x436960 (v*s + 0x800) >> 12
int32_t cvec_dot(cvec3_t a, cvec3_t b);                // 0x436800
cvec3_t cvec_cross(cvec3_t a, cvec3_t b);              // 0x4367a0
int32_t cvec_length(cvec3_t v);                        // 0x436930 FwVectorMagnitude
cvec3_t cmat_apply(const cmatrix_t *m, cvec3_t v);     // ApplyMatrixLV: (m * v) >> 12 (rotation only)

// view-node helpers (0x431730-0x431b50)
void cam_view_identity(cam_view_t *v);
void cam_view_set_axes(cam_view_t *v, cvec3_t z, cvec3_t y); // 0x431830 Coord_SetAxes
void cam_view_look_dir(cam_view_t *v, cvec3_t dir);          // 0x431920 Coord_LookDir (up = +Y)
void cam_view_look_at(cam_view_t *v, cvec3_t point);         // 0x431990
void cam_view_rotate_x(cam_view_t *v, int32_t a);            // 0x431a30 m = m * Rx(a)
void cam_view_rotate_y(cam_view_t *v, int32_t a);            // 0x431b50 m = m * Ry(a)
void cam_view_rotate_z(cam_view_t *v, int32_t a);            // 0x431ac0 m = m * Rz(a)

// Projection constant of FwSetScreenAngle (0x434b60): H = halfW * rcos(fov/2) / rsin(fov/2).
// fov 900 (startup value) at 640 wide gives 387. sx = W/2 + X*H/Z, sy = H/2 + Y*H/Z, near plane 10.
int32_t cam_projection_h(int32_t fov_angle, int32_t half_width);

// Float camera basis for the rewrite's renderer: camera x (screen right), y (screen down), z (forward)
// in world space, plus the eye. Use e.g. mat4_look_at(eye, eye + fwd, -down).
typedef struct { float eye[3], right[3], down[3], fwd[3]; } cam_basis_t;
cam_basis_t cam_view_basis(const cam_view_t *v);

// ---------------------------------------------------------------------------------------------
// host services
typedef struct {
	// Object_GetProperty(id, 2): world matrix (rotation + translation) of the target; false if gone.
	bool (*target_matrix)(uint32_t id, cmatrix_t *out);
	// Object_GetProperty(id, 7): velocity in world units per second (AutoCam_SnapToTarget).
	cvec3_t (*target_velocity)(uint32_t id);
	// Lander yaw (lander +0, 4096/turn) for ids of object type 1; return false for other types.
	bool (*lander_yaw)(uint32_t id, int32_t *yaw);
	// World_SegmentHit(p0, p1, mode 2, out) with gWorldQueryMask = mask (bit0 zones, bit1 tiles,
	// bits2-4 objects) and gQueryIgnoreObject = ignore. Returns true and the hit point on a hit.
	bool (*segment_hit)(cvec3_t p0, cvec3_t p1, uint32_t mask, uint32_t ignore, cvec3_t *hit);
	// gUseInertialFrame / gInertialFrame (may be NULL): rotation applied to camera offsets.
	const cmatrix_t *(*inertial_frame)(void);
	// gInertialFrameInterp column 1 (up vector, 4.12) used by the render look-at when a frame is active.
	cvec3_t (*inertial_up)(void);
	// zone tracking (0x485b00 copy target tracker -> camera tracker, 0x485880 walk from -> to)
	void (*zones_track_target)(cvec3_t target_pos);              // ZoneTracker_Move(gTargetZoneTracker)
	void (*zones_camera_from_target)(cvec3_t from, cvec3_t to);  // copy + walk the camera tracker
	// raw keys for the demo-playback orbit (gUIKeys Left/Right/Up/Down)
	bool (*key_down)(int key);     // key: 0 Left, 1 Right, 2 Up, 3 Down
	bool (*key_pressed)(int key);  // edge, consumed
	// gCamOccluded (0x4a7008) output of the frame callback
	void (*set_occluded)(bool occluded);
} cam_host_t;

extern cam_host_t cam_host;

// ---------------------------------------------------------------------------------------------
// AutoCam (0x74-byte record, gAutoCamPool 0x61e9e0 - the original has exactly one)
enum {
	AC_HEADING_FACING = 0, AC_HEADING_VELOCITY = 1, AC_HEADING_ORBIT = 2, AC_HEADING_KEEP = 3,
	AC_FLAG_ORBIT_KEYS = 0x02,  // also heading mode 2 when combined with 0x800 (demo playback: 0x802)
	AC_FLAG_BASE_ONLY = 0x04,   // skip velocity/elevation/collision terms
	AC_FLAG_FREEZE_EYE = 0x20,
	AC_FLAG_FREEZE_LOOK = 0x40,
	AC_FLAG_NO_ROLL = 0x80,
	AC_FLAG_SNAP = 0x800,       // full snap when the target is (re)set
};

typedef struct {
	uint32_t id;        // +0x00
	uint32_t target;    // +0x04 (0xffffffff = none)
	uint32_t flags;     // +0x08
	int32_t user_angle; // +0x0c 0..4
	int32_t cur_angle;  // +0x10
	int32_t lag_y;      // +0x14
	cvec3_t last_target;// +0x18
	cvec3_t look;       // +0x28
	cvec3_t look_vel;   // +0x38
	cvec3_t eye;        // +0x48
	cvec3_t eye_vel;    // +0x58
	int32_t roll;       // +0x68 (x32)
	int32_t roll_vel;   // +0x6c
	int32_t orbit;      // +0x70 (-1 = none)
} autocam_t;

#define AUTOCAM_PERIOD_MS 40
extern const int32_t autocam_angles[5];          // 0x498d98: -600 -350 0 350 600
extern const int32_t autocam_fallback[5][4];     // 0x498db0

// Camera_Create 0x450e10. record_rot: packed rotation of the mob record or NULL for a dynamic camera
// (then the view is put at (0, 541, 1024)). view = gView (its rotation must already be set for records).
void autocam_create(autocam_t *c, cam_view_t *view, uint32_t own_id, bool demo_playback, bool from_record);
void autocam_set_target(autocam_t *c, cam_view_t *view, uint32_t target); // property 1 set 0x450fe0
void autocam_snap(autocam_t *c, cam_view_t *view);                        // 0x4509d0
void autocam_update(autocam_t *c);                                        // 0x450480, every 40 ms
// Frame 0x4505a0: t = render time, wake = the task's next wake time. Writes view (rotation + eye).
void autocam_frame(autocam_t *c, cam_view_t *view, uint32_t t, uint32_t wake);
int32_t autocam_get_user_angle(const autocam_t *c);                       // 0x450f60
void autocam_set_user_angle(autocam_t *c, int32_t idx);                   // 0x450f80
void autocam_set_lag_y(autocam_t *c, int32_t k);                          // 0x450fc0
cvec3_t autocam_velocity_per_second(const autocam_t *c);                  // property 7 get 0x451060
// Property 0 set 0x4510e0 (target == local player): shift every point by new_pos - eye.
void autocam_set_pos(autocam_t *c, cvec3_t new_pos, bool target_is_local_player);
// SnapToTarget support 0x450d10: p = pivot + R*(p + t - pivot) for every point, velocities rotated.
void autocam_teleport(autocam_t *c, const cmatrix_t *rot, cvec3_t pivot, cvec3_t t);

// ---------------------------------------------------------------------------------------------
// FlyCam (debug fly-through camera, 0x4593e0-0x4597e0)
typedef struct {
	cvec3_t pos;     // gFlyCamPos 0x61e8c0
	cvec3_t vel;     // 0x55fd08 (also holds the forward vector between steps)
	int32_t yaw, pitch, yaw_vel, pitch_vel; // 0x55fd9c..0x55fda8
	int32_t speed;   // 0x49b39c
} flycam_t;

typedef struct {
	bool lmb, rmb, rshift; // mouse buttons 0/1, RShift = no collision
	int32_t mouse_dx, mouse_dy;
} flycam_input_t;

void flycam_create(flycam_t *f, cam_view_t *view, const cvec3_t *pos, int32_t yaw); // pos NULL: default
void flycam_update(flycam_t *f, cam_view_t *view, const flycam_input_t *in);
void flycam_frame(flycam_t *f, cam_view_t *view, uint32_t t, uint32_t wake, uint32_t period);

// AnimCam frame tail (0x455050): after the animation wrote view->mat (rotation + position).
void animcam_post(cam_view_t *view);

#endif
