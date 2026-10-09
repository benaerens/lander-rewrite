// Level world layout: interior zones joined by portals (zonedata.dat) and the
// exterior tile landscape (tiledata.dat). See re/specs/terrain.md.
#ifndef TERRAIN_H
#define TERRAIN_H

#include "types.h"

#define TILE_SIZE 0x3200 // 12800 world units per tile

typedef struct { int32_t x, y, z; } ivec3_t;

typedef struct {
	int16_t nx, ny, nz; // 4.12 unit normal
	int32_t d;          // plane: (n . p) >> 12 == d
} zplane_t;

typedef struct {
	uint16_t first_vertex, num_vertices;
	int16_t target;     // zone it leads to, -1 = exterior
	uint16_t plane;
	bool flipped;
	ivec3_t center;     // world
	int16_t radius;
} portal_t;

typedef struct {
	int num_zones;
	ivec3_t *origins;          // num_zones; zone model vertices are relative to these
	// PVS: lists of zones (the zone itself first or after -1); index zone + 1 (0 = exterior)
	int16_t **pvs;
	int *pvs_count;
	// portals
	int num_vertices, num_planes, num_portals;
	ivec3_t *vertices;         // world
	zplane_t *planes;
	portal_t *portals;
	int16_t *portal_first;     // per zone + 1 (index 0 = exterior)
	int16_t *portal_count;
	// collision octree (raw, see terrain.md §2.5), offsets relative to octree_base
	uint8_t *raw;
	uint32_t raw_size;
	uint32_t octree_offset;
} zonedata_t;

zonedata_t *zonedata_load(const char *path);
void zonedata_free(zonedata_t *zd);
// true if p is on the owning zone's side of the portal
bool portal_side(const zonedata_t *zd, const portal_t *p, ivec3_t pos);

typedef struct {
	int16_t x0, z0;
	uint16_t w, h;
	uint32_t *cells; // w * h
} tile_region_t;

typedef struct {
	tile_region_t *region;
	int16_t x0, z0;
	uint16_t w, h;
} tile_repeat_t;

typedef struct {
	int num_regions;          // explicit regions, searched first
	int num_all_regions;      // explicit + pattern regions (the default is the last)
	tile_region_t *regions;
	tile_region_t *default_region;
	int num_repeats;
	tile_repeat_t *repeats;
	int num_overflow;
	uint32_t *overflow;
} tiledata_t;

typedef struct {
	int model;   // index into tiles.ppm
	int rotation; // 0..3, quarter turns about Y (PSX RotMatrixY convention)
	int32_t y;   // height of the instance
} tile_cell_t;

tiledata_t *tiledata_load(const char *path);
void tiledata_free(tiledata_t *td);
// Tiles stacked in one grid cell; returns how many were written to out.
int tiledata_lookup(const tiledata_t *td, int tx, int tz, tile_cell_t *out, int max);
int tile_index(int32_t v);           // world coordinate -> tile index
int32_t tile_center(int t);          // tile index -> world centre coordinate
// rotation of local (x, z) into world for a tile rotation 0..3
void tile_rotate(int rotation, float x, float z, float *wx, float *wz);

#endif
