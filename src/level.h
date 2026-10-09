// A loaded mission: world geometry, models, textures and mission data, and
// drawing it from a camera.
#ifndef LEVEL_H
#define LEVEL_H

#include "types.h"
#include "terrain.h"
#include "model.h"
#include "mission.h"
#include "texfile.h"
#include "zones.h"

typedef struct {
	uint32_t serial;        // distinct for every level_load (caches keyed by the level)
	char dir[128];          // e.g. "MISSIONS\\MARS\\1A"
	char world[32];         // e.g. "MARS"
	texfile_t *textures;    // world texfile
	texfile_t *global_textures;
	zonedata_t *zonedata;
	ppm_t *zones;
	tiledata_t *tiledata;
	ppm_t *tiles;
	ppm_t *objects;         // level objects (objects.ppm)
	ppm_t *global_objects;  // misc/objects.ppm
	ppm_t *landers;         // misc/landers.ppm
	mission_t *mission;
	float view_distance;    // "Fog Distance" setting (default 12800)

	// camera zone tracking and visibility (per frame)
	zone_set_t cam_set;
	bool cam_set_valid;
	ivec3_t cam_last;
	int outside_factor;     // 0..0x1000
	vis_t vis;
} level_t;

typedef struct {
	vec3_t pos;
	float yaw, pitch, roll; // radians; yaw 0 looks along +z
	bool has_basis;         // use fwd/up instead of yaw/pitch (the game camera)
	vec3_t fwd, up;
} camera_t;

level_t *level_load(const char *world, const char *mission);
void level_free(level_t *lv);

// Which zone contains the point (-1 = outside), by the zone models' boxes.
int level_zone_at(const level_t *lv, vec3_t p);

void level_draw(level_t *lv, const camera_t *cam);  // viewer: world + every map record

// The steps of level_draw, for the game (objects are drawn by the game's instances):
// camera, zone tracking, environment colours, fog, frame clear and portal visibility.
void level_begin_frame(level_t *lv, const camera_t *cam, lighting_t *base);
// Environment_ApplyAmbientFog's DAT_0049bf20: when set, the next level_begin_frame uses this exterior
// colour instead of the mission's (once; the AmbientFX lightning flash)
extern const rgba_t *level_exterior_override;
void level_draw_world(level_t *lv, const camera_t *cam, const lighting_t *base); // zones + tiles
// the exterior tiles around cam (when the outside is visible this frame), as level_draw_world draws them
void level_draw_tiles(level_t *lv, const camera_t *cam, const lighting_t *base);
// lighting for a model at center: base + mission lights reaching it
void level_lighting(const level_t *lv, const lighting_t *base, vec3_t center, float radius, lighting_t *out);

#endif
