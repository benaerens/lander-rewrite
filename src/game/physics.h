// Rigid-body physics and world collision of Lander (Psygnosis 1999).
//
// Faithful C port of lander.EXE (Direct3D build, image base 0x400000):
//   0x42af90-0x430120  rigid bodies, forces, contacts, Phys_Step
//   0x428bc0-0x428f80  collision polygon records (built on demand from PCP triangles)
//   0x449ca0, 0x44a0c0 world candidate lists (World_CollectCandidates, World_QueryBox)
//   0x467fb0-0x46a820  octree box queries (zones type 0/1, poly octrees type 2)
//   0x482810, 0x4832c0 tile instances and their box query
//   0x451240-0x451330  surface classes
// Every function carries its original address. The spec is re/specs/physics.md.
//
// Units (all integers unless stated):
//   * positions inside the physics are world units x 8 ("x8"); the body's matrix translation and
//     the polygon data are plain world units;
//   * velocities are x8 units per 40 ms physics tick; angular velocity is 4.12 radians per tick
//     (a body turns by -|w|/4096 rad per tick about w, see phys_rotate_axis_angle);
//   * matrices and unit vectors are 4.12 (0x1000 = 1.0), PSX MATRIX layout (row major, v' = M v,
//     the columns are the body axes in world space);
//   * time-of-impact fractions are 4.12 of one tick (0x1001 = no contact);
//   * forces are "impulse units": a force record F changes the velocity by F*512/mass per tick.
// Arithmetic wraps like the 32-bit original (computed in int64 and truncated). A few spots use
// single-precision floats exactly like the x87 code at 24-bit precision (documented in the .c).
//
// The world geometry comes from the rewrite's loaders (terrain.h: zonedata_t / tiledata_t,
// model.h: model_t with the raw PCP blob for the tagged blocks).
#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdint.h>
#include <stdbool.h>

#include "terrain.h"
#include "model.h"

#define PHYS_MAX_BODIES      8       // gBodies 0x583a40, 8 x 0xd10
#define PHYS_MAX_POINTS      24      // contact records per body
#define PHYS_MAX_CANDIDATES  0x400   // gWorldCandidates 0x58a2e0
#define PHYS_TOI_NONE        0x1001
#define PHYS_ONE             0x1000
#define PHYS_TICK_MS         40
#define PHYS_MAX_TILES       625     // tile instances per frame (Tiles_AddInstance)

// contact flags (+0x0a)
#define PHYS_CF_HIT     1   // contact found this step (point no longer swept)
#define PHYS_CF_MOVING  2   // polygon belongs to a moving object frame
#define PHYS_CF_DONE    4   // resolved by the "liquid" surface response

// frame kinds (+0x0b, DAT_0058b384)
enum { PHYS_FRAME_ZONE = 0, PHYS_FRAME_TILE = 1, PHYS_FRAME_MATRIX = 2, PHYS_FRAME_OBJECT = 3 };

// PSX MATRIX: s16 m[3][3] (4.12) + s32 t[3] (0x20 bytes)
typedef struct {
	int16_t m[9];
	int16_t pad;
	int32_t t[3];
} phys_matrix_t;

// One slot of the candidate list. The original is a u32 list: aligned pointers to polygon
// records (low 2 bits 0) and tagged frame words (bits 0-1 != 0) that select the transform for
// the polygons that follow. Here every slot keeps the u32 word plus a pointer:
//   polygon:       word 0,                      ptr = phys_poly_t
//   zone frame:    word 1,                      ptr = int32 origin[3] (zonedata origin)
//   matrix frame:  word 2,                      ptr = phys_matrix_t (static object world matrix)
//   tile frame:    word (rot << 4) | 7,         next slot ptr = int32 position[3] of the instance
//   object frame:  word (renderItem << 4) | 0xb, next slot word = object handle
typedef struct {
	uint32_t word;
	const void *ptr;
} phys_cand_t;

// Collision polygon record (0x40 bytes in the original, cache of 0x1000 at 0x4a7570).
typedef struct phys_poly {
	int16_t model;        // +0x08 model id (here: mesh index), informational
	uint16_t tri;         // +0x0a triangle index | 0x8000 = quad (this triangle + the next)
	uint8_t nverts;       // +0x0c 3 or 4
	uint8_t surface;      // +0x0d low byte of the triangle's texture index (surface material)
	uint8_t axis;         // +0x0e dropped axis for the 2-D test: 0 = x, 1 = y, 2 = z
	uint8_t flip;         // +0x0f u is the second coordinate of the pair (larger extent)
	uint8_t in_list;      // +0x10 de-duplication flag while a candidate group is built
	int16_t v2d[4][2];    // +0x14 2-D outline (world units), pushed out by 32 units
	int16_t box[4];       // +0x24 umin, vmin, umax, vmax (grown by 32)
	int32_t normal[3];    // +0x2c plane normal 4.12 (frame local)
	int32_t d8;           // +0x3c plane distance x8: plane is (n.p >> 12) == d8 for x8 points
} phys_poly_t;

// Force record (10 dwords, pool at 0x4e86b8)
typedef struct phys_force {
	struct phys_force *next;  // +0x00
	int32_t f[3];             // +0x04 force (impulse units)
	int32_t pad;
	int32_t p[3];             // +0x14 point of application (x8 world), only for kind 0
	int32_t pad2;
	int32_t kind;             // +0x24 0 = at point (adds torque), 1 = at the centre of mass
} phys_force_t;

// Body state (0x1b4 bytes): the body has two, "cur" and "next"
typedef struct {
	int32_t pos[3];                  // +0x000 centre of mass, x8
	int32_t pad;
	phys_matrix_t mat;               // +0x010 orientation, t = pos >> 3
	int32_t wp[PHYS_MAX_POINTS][4];  // +0x030 world positions of the contact points, x8
	int32_t time;                    // +0x1b0 game time (ms) of this state
} phys_state_t;

typedef struct {
	const phys_poly_t *poly;  // +0x00 polygon hit (NULL until hit)
	int16_t point;            // +0x04 contact point index
	int16_t toi;              // +0x06 time of impact 4.12 (0x1001 none)
	int16_t depth;            // +0x08 start penetration / skin thickness 4.12 (liquid: depth)
	uint8_t flags;            // +0x0a PHYS_CF_*
	uint8_t frame_kind;       // +0x0b PHYS_FRAME_*
	uint32_t object;          // +0x0c object handle (object frames), else -1
	int32_t pos[4];           // +0x10 contact point, x8 world
	int32_t normal[4];        // +0x20 world normal 4.12
	int32_t rel_vel[4];       // +0x30 relative velocity at the point (x8/tick)
	int32_t frame_vel[4];     // +0x40 velocity of a moving frame at the point (world/tick)
	phys_cand_t frame[2];     // +0x50 the frame tag (and operand) of the polygon
} phys_contact_t;

struct phys_body;
// +0x4ac: called for every contact response: (body, contact, normal impulse, friction value)
typedef void (*phys_contact_fn)(struct phys_body *b, phys_contact_t *c, int32_t jn, int32_t jf);

typedef struct phys_body {
	int32_t num_points;                    // +0x000
	int16_t points[PHYS_MAX_POINTS][4];    // +0x004 body-local contact points, x8
	phys_state_t state[2];                 // +0x0c4 / +0x278
	phys_state_t *next;                    // +0x42c predicted / trial state
	phys_state_t *cur;                     // +0x430 current state; NULL = free slot
	int32_t vel[4];                        // +0x434 linear velocity x8/tick
	int32_t angvel[4];                     // +0x444 angular velocity 4.12 rad/tick (sign: see top)
	int32_t inertia[4];                    // +0x454 principal moments (sum m r^2 / 4096, r x8)
	int32_t extent[4];                     // +0x464 max |local coordinate| of the points, x8
	int32_t mass;                          // +0x474
	phys_force_t *forces;                  // +0x478 queued forces (consumed by apply_forces)
	int32_t torque[3];                     // +0x47c body-frame torque accumulator
	int32_t unk488;
	int32_t impulse_sum;                   // +0x48c normal impulses this step (liquid: 1)
	int32_t friction_sum;                  // +0x490 friction impulses this step
	int32_t touched;                       // +0x494 last object touched (-1 none)
	int16_t unk498;                        // +0x498
	int16_t no_extrapolate;                // +0x49a write_transform ignores the fraction
	int16_t renorm_count;                  // +0x49c orthonormalise countdown (8, then every tick)
	int16_t grow_box;                      // +0x49e liquid contact: broad phase box +0x400 up
	phys_matrix_t *coord;                  // +0x4a0 transform node the body drives (may be NULL)
	int32_t coord_time;                    // +0x4a4 (-1; never set by the exe)
	int32_t owner;                         // +0x4a8 object handle (excluded from its own query)
	phys_contact_fn on_contact;            // +0x4ac
	int16_t unk4b0[2];                     // +0x4b0 cleared every step
	phys_contact_t contact[PHYS_MAX_POINTS]; // +0x4bc
	int32_t has_avg_normal;                // +0xcfc
	int32_t avg_normal[3];                 // +0xd00 mid-range of the contact normals, normalised
} phys_body_t;

// One collidable PCP model (zone, tile or object part).
typedef struct {
	const model_t *model;
	const uint8_t *octree;    // tagged block 0x0CBEEEEE (type 1 for zones, 2 for tiles/objects)
	const uint8_t *planes;    // tagged block 0x00B0FFED (u32 runtime, s16 normal, s16 d) per tri
	phys_poly_t **polys;      // lazily built records, one per triangle
	int index;
} phys_mesh_t;

// Tile instance (0x18 bytes in the original: {x, y, z, ?, renderItem, cell*})
typedef struct {
	int32_t pos[3];  // tile centre x, cell height, tile centre z
	int rot;         // quarter turns about Y
	int model;       // tiles.ppm entry
} phys_tile_t;

// Everything the queries need about the level. Objects are reached through callbacks
// (the object system is not part of this port).
typedef struct phys_world {
	const uint8_t *zone_octree;     // zonedata.dat + header[0x0C] (type 0), NULL if none
	int num_zones;
	const ivec3_t *zone_origins;
	phys_mesh_t *zone_mesh;         // num_zones (zone i = zones.ppm entry i)
	const tiledata_t *tiledata;
	int num_tile_models;
	phys_mesh_t *tile_mesh;         // num_tile_models (tiles.ppm entries)
	phys_tile_t tiles[PHYS_MAX_TILES]; // live tile instances (Tiles_StreamAround)
	int num_tiles;
	// mission.dat section D: texture byte -> surface class 0..8
	const uint8_t *surface_class;
	int num_surface_class;
	bool surface_table_old;         // gSurfaceTableOld 0x553b30: always class 0
	int query_mask;                 // gWorldQueryMask 0x4989f4 (bit0 zones, bit1 tiles, 2-4 objects)

	// --- object adapters (optional) ---
	// broad phase for objects: append candidates (tags 2 / 0xb with their polygons) for a box
	int (*query_objects)(struct phys_world *w, const int32_t *box, phys_cand_t *out, int max);
	int (*object_class)(uint32_t handle);                       // ObjectType_GetClass: 1 static, 2 mobile
	const phys_matrix_t *(*object_matrix)(uint32_t handle, int32_t time); // property 2 / part matrix
	phys_body_t *(*object_body)(uint32_t handle);               // property 6 (0x413880)
	int (*object_surface)(uint32_t handle);                     // MapRec_GetPartAux (0x447630)
	void (*touch_report)(int32_t owner, uint32_t handle, int32_t impulse); // 0x413af0: event 0x10
} phys_world_t;

// ------------------------------------------------------------------ globals of the original
typedef struct {
	int32_t gravity[3];        // gGravityVector 0x577a70 (SetGravity 0x4442f0)
	int32_t air_resistance;    // gAirResistance 0x4935f0 (used by the lander, not the solver)
	int32_t step_start;        // DAT_0058b3b4 = gGameUpdateTime at the physics tick
	int32_t step_end;          // DAT_0058b380 = start + 40
	int32_t min_toi;           // DAT_004e86a0 (-0xffff during Phys_Step, else 0)
	int32_t interp;            // DAT_0058b388 fraction reached, for moving object frames
	bool in_physics;           // DAT_004e869c
	// surface parameters of the polygon being resolved (Surface_SetContactParams 0x451240)
	int32_t bounce;            // DAT_004e90b8  restitution boost 4.12
	int32_t friction;          // DAT_0049842c  Coulomb factor 4.12
	int32_t liquid;            // DAT_004e90bc  1 = soft/liquid response
	int32_t surface_default;   // DAT_00553b38  class index for texture bytes past the table
	int32_t surface_last;      // DAT_00553b34  class of the last Surface_Select (read by objects)
	int32_t game_time;         // gGameUpdateTime 0x60f2c0 (for frames queried outside the step)
	int32_t exclude_owner;     // DAT_00498434: owner of the body being queried (object query skips it)
} phys_globals_t;

// Built-in surface classes, 16 bytes each at 0x498e08
typedef struct {
	int32_t bounce;     // +0  restitution boost 4.12 (solid) / buoyancy factor (liquid)
	int32_t friction;   // +4  friction factor 4.12 (liquid: drag factor)
	uint8_t aux[4];     // +8  not read by the solver (effects?)
	int16_t thickness;  // +12 contact skin, x8 units (-1 = ghost: touch report only)
	uint16_t liquid;    // +14 1 = soft/liquid response
} phys_surface_t;

extern phys_globals_t phys_g;
extern phys_body_t phys_bodies[PHYS_MAX_BODIES];
extern int phys_num_bodies;          // DAT_0058a2c0
extern const phys_surface_t phys_surface_table[9];

// ------------------------------------------------------------------ world
void phys_mesh_init(phys_mesh_t *m, const model_t *model, int index);
void phys_mesh_free(phys_mesh_t *m);
const phys_poly_t *phys_mesh_poly(phys_mesh_t *m, uint16_t index);  // 0x428cc0
// Poly_Build 0x42b480: record from 3/4 vertices (world units, frame local), s16 4.12 normal, d
void phys_poly_build(phys_poly_t *p, int n, const int16_t (*v)[3], const int16_t *normal, int16_t d);
void phys_set_world(phys_world_t *w);
phys_world_t *phys_get_world(void);
// Tiles_StreamAround 0x482810: rebuild the live tile instances around pos (the exe streams around
// the camera with radius ~ view distance; radius 0 = no tiles)
void phys_world_stream_tiles(phys_world_t *w, const int32_t *pos, int32_t radius);

// ------------------------------------------------------------------ queries
// World_QueryBox 0x44a0c0: two boxes {min xyz, pad, max xyz, pad} in world units; returns the
// number of candidate slots written (polygons grouped behind frame tags)
int phys_query_box(const int32_t *a, const int32_t *b, phys_cand_t *out, int max);
// For query_objects: one object part's polygons inside local_box (box in the part's frame).
// static_matrix != NULL: static part (matrix frame tag); else object frame tag + handle.
int phys_collect_part(phys_mesh_t *mesh, const int32_t *local_box, const phys_matrix_t *static_matrix,
	uint32_t render_item, uint32_t handle, phys_cand_t *out, int max);
// World_SegmentHit 0x44a240 (flags 0 = line of sight: returns 1 if clear)
typedef struct {
	int32_t frame_kind, object;  // flags & 1
	int32_t point[3];            // flags & 2 (world units)
	int32_t pad;
	int32_t normal[3];           // flags & 4 (4.12)
	int32_t pad2;
	int32_t surface;             // always
} phys_hit_t;
int phys_segment_hit(const int32_t *p0, const int32_t *p1, int flags, phys_hit_t *out);

// ------------------------------------------------------------------ bodies
void phys_reset(void);                                         // 0x42f650
phys_body_t *phys_body_alloc(void);                            // 0x42f6c0
void phys_body_free(phys_body_t *b);                           // 0x42f720
void phys_body_init(phys_body_t *b, int32_t mass);             // 0x42d620
void phys_body_add_point(phys_body_t *b, int x, int y, int z); // 0x42d550 (world units)
void phys_body_add_mass(phys_body_t *b, const int32_t *p, int32_t m); // 0x42f960
void phys_body_inertia_from_points(phys_body_t *b);            // 0x42fa30
// 0x42ea40: 0 ok, 2 no 0xFACCFACE block; mass -1 = the block's default
int phys_body_setup_dynamics(phys_body_t *b, const model_t *model, int rotate45, int32_t mass);
void phys_body_init_box(phys_body_t *b, const int32_t *size, const int32_t *pos8, int32_t mass); // 0x42fd40
void phys_body_set_position(phys_body_t *b, const int32_t *pos8);  // 0x42d760
void phys_body_add_force(phys_body_t *b, const int32_t *f, int kind, const int32_t *p8); // 0x42d330
int phys_body_apply_forces(phys_body_t *b, phys_state_t *s);   // 0x42db70
void phys_body_predict(phys_body_t *b, int32_t frac, int32_t time); // 0x42de30
void phys_body_write_transform(phys_body_t *b, phys_matrix_t *out, int32_t frac); // 0x42d920
void phys_body_interpolate(phys_body_t *b, phys_matrix_t *out, int32_t time);     // 0x42dfb0
int32_t phys_body_linear_energy(const phys_body_t *b);         // 0x42d880
int32_t phys_body_angular_energy(const phys_body_t *b);        // 0x42d8b0

// ------------------------------------------------------------------ the step
void phys_set_gravity(int32_t g, const phys_matrix_t *inertial_frame); // SetGravity 0x4442f0
// Physics task 0x413890: one 40 ms tick starting at game time t
void phys_task(int32_t t);
void phys_step(void);                                          // Phys_Step 0x42ec00

// PSX-style helpers that the object code also needs
void phys_rotate_axis_angle(const int16_t *in, int16_t *out, int32_t angle, const int32_t *axis); // 0x42fb30
int32_t phys_normalise(int32_t *v);                          // FwVectorNormalise 0x436830
void phys_scale(const int32_t *v, int32_t s, int32_t *out);// FwVectorScale 0x436960

#endif
