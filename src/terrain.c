#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terrain.h"
#include "fs.h"

// ---------------------------------------------------------------- zonedata.dat

zonedata_t *zonedata_load(const char *path) {
	uint32_t size;
	uint8_t *d = fs_load(path, &size);
	if (!d || size < 0x18) {
		free(d);
		return NULL;
	}
	zonedata_t *zd = calloc(1, sizeof(zonedata_t));
	zd->raw = d;
	zd->raw_size = size;
	int n = zd->num_zones = get_u32(d + 4);
	uint32_t origins = get_u32(d + 8);
	zd->octree_offset = get_u32(d + 0x0C);
	uint32_t pvs = get_u32(d + 0x10);
	uint32_t graph = get_u32(d + 0x14);

	zd->origins = calloc(n ? n : 1, sizeof(ivec3_t));
	for (int i = 0; i < n; i++) {
		const uint8_t *o = d + origins + i * 12;
		zd->origins[i] = (ivec3_t){get_i32(o), get_i32(o + 4), get_i32(o + 8)};
	}

	// PVS: entry for zone -1 at +4, zones 0..n-1 at +8; list address = pvs + 2 * off
	zd->pvs = calloc(n + 1, sizeof(int16_t *));
	zd->pvs_count = calloc(n + 1, sizeof(int));
	for (int z = -1; z < n; z++) {
		const uint8_t *e = d + pvs + 8 + 4 * z;
		int count = get_u16(e), off = get_u16(e + 2);
		zd->pvs_count[z + 1] = count;
		zd->pvs[z + 1] = calloc(count ? count : 1, sizeof(int16_t));
		for (int k = 0; k < count; k++) {
			zd->pvs[z + 1][k] = get_i16(d + pvs + 2 * off + 2 * k);
		}
	}

	// portal graph
	const uint8_t *g = d + graph;
	zd->num_vertices = get_u32(g);
	zd->num_planes = get_u32(g + 4);
	zd->num_portals = get_u32(g + 8);
	uint32_t vert_off = get_u32(g + 0x10), plane_off = get_u32(g + 0x14);
	uint32_t portal_off = get_u32(g + 0x18), range_off = get_u32(g + 0x1C);

	zd->planes = calloc(zd->num_planes + 1, sizeof(zplane_t));
	for (int i = 0; i < zd->num_planes; i++) {
		const uint8_t *p = d + plane_off + i * 12;
		zd->planes[i] = (zplane_t){get_i16(p), get_i16(p + 2), get_i16(p + 4), get_i32(p + 8)};
	}

	zd->portals = calloc(zd->num_portals + 1, sizeof(portal_t));
	zd->vertices = calloc(zd->num_vertices + 1, sizeof(ivec3_t));
	for (int i = 0; i < zd->num_portals; i++) {
		const uint8_t *p = d + portal_off + i * 24;
		portal_t *pt = &zd->portals[i];
		pt->first_vertex = get_u16(p);
		pt->num_vertices = get_u16(p + 2);
		pt->target = get_i16(p + 4);
		pt->plane = get_u16(p + 6) & 0x7fff;
		pt->flipped = (get_u16(p + 6) & 0x8000) != 0;
		ivec3_t lo = {get_i16(p + 8), get_i16(p + 10), get_i16(p + 12)};
		ivec3_t hi = {get_i16(p + 16), get_i16(p + 18), get_i16(p + 20)};
		pt->radius = get_i16(p + 14);
		pt->center = (ivec3_t){hi.x * 0x1000 + lo.x, hi.y * 0x1000 + lo.y, hi.z * 0x1000 + lo.z};
		// vertices are relative to the portal's hi * 0x1000 cell
		for (int k = 0; k < pt->num_vertices; k++) {
			int vi = pt->first_vertex + k;
			if (vi < zd->num_vertices) {
				const uint8_t *v = d + vert_off + vi * 8;
				zd->vertices[vi] = (ivec3_t){
					hi.x * 0x1000 + get_i16(v), hi.y * 0x1000 + get_i16(v + 2), hi.z * 0x1000 + get_i16(v + 4)};
			}
		}
	}

	// zone -> portal ranges; the entry for zone -1 is just before the pointer
	zd->portal_first = calloc(n + 1, sizeof(int16_t));
	zd->portal_count = calloc(n + 1, sizeof(int16_t));
	for (int z = -1; z < n; z++) {
		const uint8_t *r = d + range_off + 4 * z;
		zd->portal_first[z + 1] = get_i16(r);
		zd->portal_count[z + 1] = get_i16(r + 2);
	}
	return zd;
}

void zonedata_free(zonedata_t *zd) {
	if (!zd) {
		return;
	}
	for (int i = 0; i <= zd->num_zones; i++) {
		free(zd->pvs[i]);
	}
	free(zd->pvs);
	free(zd->pvs_count);
	free(zd->origins);
	free(zd->planes);
	free(zd->portals);
	free(zd->vertices);
	free(zd->portal_first);
	free(zd->portal_count);
	free(zd->raw);
	free(zd);
}

bool portal_side(const zonedata_t *zd, const portal_t *p, ivec3_t pos) {
	const zplane_t *pl = &zd->planes[p->plane];
	int64_t dot = ((int64_t)pl->nx * pos.x + (int64_t)pl->ny * pos.y + (int64_t)pl->nz * pos.z) >> 12;
	return (dot >= pl->d) != p->flipped;
}

// ---------------------------------------------------------------- tiledata.dat

static void read_region(const uint8_t *d, uint32_t size, uint32_t off, tile_region_t *r) {
	const uint8_t *p = d + off;
	r->x0 = get_i16(p);
	r->z0 = get_i16(p + 2);
	r->w = get_u16(p + 4);
	r->h = get_u16(p + 6);
	uint32_t cells = get_u32(p + 8);
	int n = r->w * r->h;
	r->cells = calloc(n ? n : 1, sizeof(uint32_t));
	for (int i = 0; i < n && cells + 4 * i + 4 <= size; i++) {
		r->cells[i] = get_u32(d + cells + 4 * i);
	}
}

tiledata_t *tiledata_load(const char *path) {
	uint32_t size;
	uint8_t *d = fs_load(path, &size);
	if (!d || size < 0x1C) {
		free(d);
		return NULL;
	}
	tiledata_t *td = calloc(1, sizeof(tiledata_t));
	td->num_regions = get_u32(d + 4);
	td->num_repeats = get_u32(d + 8);
	uint32_t region_off = get_u32(d + 0x0C), default_off = get_u32(d + 0x10);
	uint32_t repeat_off = get_u32(d + 0x14), overflow_off = get_u32(d + 0x18);

	td->num_all_regions = (default_off - region_off) / 20 + 1;
	td->regions = calloc(td->num_all_regions, sizeof(tile_region_t));
	for (int i = 0; i < td->num_all_regions; i++) {
		read_region(d, size, region_off + i * 20, &td->regions[i]);
	}
	td->default_region = &td->regions[td->num_all_regions - 1];

	td->repeats = calloc(td->num_repeats + 1, sizeof(tile_repeat_t));
	for (int i = 0; i < td->num_repeats; i++) {
		const uint8_t *p = d + repeat_off + i * 12;
		tile_repeat_t *rp = &td->repeats[i];
		rp->x0 = get_i16(p);
		rp->z0 = get_i16(p + 2);
		rp->w = get_u16(p + 4);
		rp->h = get_u16(p + 6);
		int idx = (int)(get_u32(p + 8) - region_off) / 20;
		rp->region = idx >= 0 && idx < td->num_all_regions ? &td->regions[idx] : td->default_region;
	}

	td->num_overflow = overflow_off < size ? (size - overflow_off) / 4 : 0;
	td->overflow = calloc(td->num_overflow + 1, sizeof(uint32_t));
	for (int i = 0; i < td->num_overflow; i++) {
		td->overflow[i] = get_u32(d + overflow_off + 4 * i);
	}
	free(d);
	return td;
}

void tiledata_free(tiledata_t *td) {
	if (!td) {
		return;
	}
	for (int i = 0; i < td->num_all_regions; i++) {
		free(td->regions[i].cells);
	}
	free(td->regions);
	free(td->repeats);
	free(td->overflow);
	free(td);
}

static int mod_pos(int a, int n) {
	int m = a % n;
	return m < 0 ? m + n : m;
}

static void decode_cell(uint32_t c, tile_cell_t *out) {
	out->model = c >> 24;
	out->rotation = (c >> 22) & 3;
	out->y = ((int32_t)(c << 10)) >> 10;
}

int tiledata_lookup(const tiledata_t *td, int tx, int tz, tile_cell_t *out, int max) {
	uint32_t c = 0;
	bool found = false;
	for (int i = 0; i < td->num_regions; i++) {
		const tile_region_t *r = &td->regions[i];
		if (tx >= r->x0 && tx < r->x0 + r->w && tz >= r->z0 && tz < r->z0 + r->h) {
			c = r->cells[(tz - r->z0) * r->w + (tx - r->x0)];
			found = true;
			break;
		}
	}
	if (!found) {
		const tile_region_t *r = td->default_region;
		for (int i = 0; i < td->num_repeats; i++) {
			const tile_repeat_t *rp = &td->repeats[i];
			if (tx >= rp->x0 && tx < rp->x0 + rp->w && tz >= rp->z0 && tz < rp->z0 + rp->h) {
				r = rp->region;
				break;
			}
		}
		if (!r->w || !r->h) {
			return 0;
		}
		c = r->cells[mod_pos(tz - r->z0, r->h) * r->w + mod_pos(tx - r->x0, r->w)];
	}
	if ((c >> 24) == 0xff) {
		int count = (c >> 16) & 0xff, first = c & 0xffff, n = 0;
		for (int i = 0; i < count && n < max && first + i < td->num_overflow; i++) {
			decode_cell(td->overflow[first + i], &out[n++]);
		}
		return n;
	}
	if (max < 1) {
		return 0;
	}
	decode_cell(c, &out[0]);
	return 1;
}

int tile_index(int32_t v) {
	return v + 0x1900 > 0 ? (v + 0x3200) / 0x3200 : v / 0x3200;
}

int32_t tile_center(int t) {
	return t * 0x3200 - 0x1900;
}

void tile_rotate(int rotation, float x, float z, float *wx, float *wz) {
	switch (rotation & 3) {
		case 0: *wx = x; *wz = z; break;
		case 1: *wx = z; *wz = -x; break;
		case 2: *wx = -x; *wz = -z; break;
		default: *wx = -z; *wz = x; break;
	}
}
