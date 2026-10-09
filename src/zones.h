// Zone tracking (which interior zones a point is in), the inside/outside
// factor, and portal visibility. Port of 0x484e70-0x486900 (terrain.md §4).
#ifndef ZONES_H
#define ZONES_H

#include "terrain.h"

#define ZONESET_MAX_ZONES 4
#define ZONESET_MAX_PORTALS 32

typedef struct {
	int num_boundary, num_internal, num_zones;
	int16_t zones[ZONESET_MAX_ZONES];    // -1 = outside
	struct { uint16_t portal; int16_t zone; } boundary[ZONESET_MAX_PORTALS]; // portals leading out of the set
	struct { uint16_t portal; int16_t zone; } internal[ZONESET_MAX_PORTALS]; // portal | 0x8000, zone beyond it
} zone_set_t;

// zone < -1: locate by brute force; otherwise start in that zone (-1 = outside)
void zoneset_init(const zonedata_t *zd, zone_set_t *s, ivec3_t pos, int zone);
// Move the point from old (NULL = teleport) to pos, entering/leaving zones through portals.
void zoneset_move(const zonedata_t *zd, zone_set_t *s, const ivec3_t *old, ivec3_t pos);
bool zoneset_has(const zone_set_t *s, int zone);
// 0x1000 = fully outside .. 0 = inside (fades over 4096 units from the exterior portal)
int zoneset_outside_factor(const zonedata_t *zd, const zone_set_t *s, ivec3_t pos);

// Portal visibility: one record per visible zone, with the screen rectangle it is seen through.
typedef struct {
	int16_t zone;          // -1 = the exterior
	int16_t x0, x1, y0, y1;
} vis_record_t;

#define MAX_VIS_RECORDS 128

typedef struct {
	int count;
	vis_record_t rec[MAX_VIS_RECORDS];
} vis_t;

// Needs the render view/projection already set for this frame.
void zones_compute_visible(const zonedata_t *zd, const zone_set_t *cam_set, ivec3_t cam_pos, vis_t *vis);
const vis_record_t *vis_find(const vis_t *vis, int zone);

#endif
