// Port of the zone tracker (0x484e70-0x485b90) and portal visibility
// (0x485f00-0x486670). Integer maths kept as in the original, including 32-bit
// wrap-around in plane dot products.
#include <stdlib.h>
#include <string.h>

#include "zones.h"
#include "render.h"

static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

static int32_t plane_dot(const zplane_t *pl, ivec3_t p) {
	uint32_t s = (uint32_t)mul32(pl->ny, p.y) + (uint32_t)mul32(pl->nz, p.z) + (uint32_t)mul32(pl->nx, p.x);
	return (int32_t)s >> 12;
}

static int32_t fw_sqrt(int32_t x) { // 0x4354e0: round(sqrt(x))
	return x <= 0 ? 0 : (int32_t)lrint(sqrt((double)x));
}

static int32_t vec_mag(ivec3_t v) { // 0x436930
	int32_t s = (int32_t)((uint32_t)mul32(v.y, v.y) + (uint32_t)mul32(v.z, v.z) + (uint32_t)mul32(v.x, v.x));
	return s ? fw_sqrt(s) : 0;
}

static ivec3_t vsub(ivec3_t a, ivec3_t b) { return (ivec3_t){a.x - b.x, a.y - b.y, a.z - b.z}; }
static ivec3_t vadd(ivec3_t a, ivec3_t b) { return (ivec3_t){a.x + b.x, a.y + b.y, a.z + b.z}; }
static bool small14(ivec3_t v) { // all |components| < 0x4000
	return (abs(v.x) & ~0x3fff) == 0 && (abs(v.y) & ~0x3fff) == 0 && (abs(v.z) & ~0x3fff) == 0;
}

// 0x485240: nonzero when pos is not on the owning zone's side (flip inverts); *dist = n.p - d
static int portal_side_ex(const zonedata_t *zd, const portal_t *p, ivec3_t pos, int flip, int32_t *dist) {
	const zplane_t *pl = &zd->planes[p->plane];
	int r = flip ^ (p->flipped ? 0x8000 : 0);
	int32_t dot = plane_dot(pl, pos);
	*dist = dot - pl->d;
	if (dot < pl->d) {
		r ^= 0x8000;
	}
	return r;
}

// 0x4852b0: point near the portal's plane and within radius + 0x400 of its centre
static int32_t portal_disc_test(const zonedata_t *zd, const portal_t *p, ivec3_t pos, int32_t dist) {
	if (abs(dist) > 0x1000) {
		return -1;
	}
	ivec3_t q = pos;
	if (dist != 0) {
		const zplane_t *pl = &zd->planes[p->plane];
		q = vsub(pos, (ivec3_t){mul32(pl->nx, dist) >> 12, mul32(pl->ny, dist) >> 12, mul32(pl->nz, dist) >> 12});
	}
	ivec3_t d = vsub(q, p->center);
	if (!small14(d)) {
		return -1;
	}
	int32_t m = vec_mag(d);
	if (m < p->radius + 0x400) {
		return p->radius ? (m << 12) / p->radius : -1;
	}
	return -1;
}

// 0x4853d0: does old -> old+delta cross the portal plane? new_dist = distance of the new point.
// Returns false and the crossing point when it does.
static bool portal_segment_no_cross(const zonedata_t *zd, const portal_t *p, ivec3_t old, ivec3_t delta,
	int32_t *dist, ivec3_t *hit) {
	int32_t new_dist = *dist, old_dist;
	portal_side_ex(zd, p, old, 0, &old_dist);
	if (new_dist == old_dist) {
		return true;
	}
	if (new_dist < 0 ? old_dist < 0 : old_dist > 0) {
		return true;
	}
	int32_t s = (old_dist * -0x1000) / (new_dist - old_dist);
	*hit = vadd(old, (ivec3_t){(mul32(delta.x, s) + 0x800) >> 12, (mul32(delta.y, s) + 0x800) >> 12,
		(mul32(delta.z, s) + 0x800) >> 12});
	*dist = 0;
	return false;
}

bool zoneset_has(const zone_set_t *s, int zone) { // 0x484e70
	for (int i = 0; i < s->num_zones; i++) {
		if (s->zones[i] == zone) {
			return true;
		}
	}
	return false;
}

static int first_portal(const zonedata_t *zd, int zone) { return zd->portal_first[zone + 1]; }
static int num_portals(const zonedata_t *zd, int zone) { return zd->portal_count[zone + 1]; }

static const portal_t *find_portal_to(const zonedata_t *zd, int zone, int target) { // 0x484eb0
	for (int i = first_portal(zd, zone); i < first_portal(zd, zone) + num_portals(zd, zone); i++) {
		if (zd->portals[i].target == target) {
			return &zd->portals[i];
		}
	}
	return NULL;
}

static void add_internal(zone_set_t *s, int portal, int zone) {
	if (s->num_internal < ZONESET_MAX_PORTALS) {
		s->internal[s->num_internal].portal = (uint16_t)portal;
		s->internal[s->num_internal++].zone = (int16_t)zone;
	}
}

static void add_boundary(zone_set_t *s, int portal, int zone) {
	if (s->num_boundary < ZONESET_MAX_PORTALS) {
		s->boundary[s->num_boundary].portal = (uint16_t)portal;
		s->boundary[s->num_boundary++].zone = (int16_t)zone;
	}
}

static void set_add_zone(const zonedata_t *zd, zone_set_t *s, int zone) { // 0x484fd0
	int n = 0;
	for (int i = 0; i < s->num_boundary; i++) {
		if (s->boundary[i].zone != zone) {
			s->boundary[n++] = s->boundary[i];
		}
	}
	s->num_boundary = n;
	for (int i = first_portal(zd, zone); i < first_portal(zd, zone) + num_portals(zd, zone); i++) {
		int target = zd->portals[i].target;
		if (zoneset_has(s, target)) {
			for (int j = first_portal(zd, target); j < first_portal(zd, target) + num_portals(zd, target); j++) {
				if (zd->portals[j].target == zone) {
					add_internal(s, j ^ 0x8000, zone);
				}
			}
			add_internal(s, i ^ 0x8000, target);
		} else {
			add_boundary(s, i, target);
		}
	}
	if (s->num_zones < ZONESET_MAX_ZONES) {
		s->zones[s->num_zones++] = (int16_t)zone;
	}
}

static void set_remove_zone(const zonedata_t *zd, zone_set_t *s, int zone) { // 0x4850b0
	int n = 0;
	for (int i = 0; i < s->num_internal; i++) {
		if (s->internal[i].zone != zone) {
			s->internal[n++] = s->internal[i];
		}
	}
	s->num_internal = n;
	n = 0;
	for (int i = 0; i < s->num_zones; i++) {
		if (s->zones[i] != zone) {
			s->zones[n++] = s->zones[i];
		}
	}
	s->num_zones = n;
	for (int i = first_portal(zd, zone); i < first_portal(zd, zone) + num_portals(zd, zone); i++) {
		int target = zd->portals[i].target;
		if (zoneset_has(s, target)) {
			for (int j = first_portal(zd, target); j < first_portal(zd, target) + num_portals(zd, target); j++) {
				if (zd->portals[j].target == zone) {
					add_boundary(s, j, zone);
				}
			}
		}
		n = 0;
		for (int k = 0; k < s->num_internal; k++) {
			if (!(s->internal[k].portal == (uint16_t)(i ^ 0x8000) && s->internal[k].zone == target)) {
				s->internal[n++] = s->internal[k];
			}
		}
		s->num_internal = n;
		n = 0;
		for (int k = 0; k < s->num_boundary; k++) {
			if (s->boundary[k].portal != (uint16_t)i) {
				s->boundary[n++] = s->boundary[k];
			}
		}
		s->num_boundary = n;
	}
}

static ivec3_t portal_center(const portal_t *p) { return p->center; }

// 0x485460: brute-force search
static void set_locate(const zonedata_t *zd, zone_set_t *s, ivec3_t pos, int mode) {
	uint32_t best_between = 0xffffffff, best_inside = 0xffffffff;
	int between_a = -1, between_b = -1, inside = -1;
	for (int z = 0; z < zd->num_zones; z++) {
		int outside_count = 0;
		int first = first_portal(zd, z), count = num_portals(zd, z);
		for (int i = first; i < first + count; i++) {
			const portal_t *p = &zd->portals[i];
			int32_t dist;
			if (!portal_side_ex(zd, p, pos, 0, &dist)) {
				continue;
			}
			outside_count++;
			const portal_t *twin = p->target >= -1 ? find_portal_to(zd, p->target, z) : NULL;
			if (twin && portal_side_ex(zd, twin, pos, 0, &dist)) {
				ivec3_t mid = vadd(portal_center(p), portal_center(twin));
				mid = (ivec3_t){mid.x >> 1, mid.y >> 1, mid.z >> 1};
				ivec3_t d = vsub(mid, pos);
				if (small14(d)) {
					uint32_t m = (uint32_t)vec_mag(d);
					if (m < 0x8000 && p->radius) {
						m = (m << 12) / (uint32_t)p->radius;
						if (m < best_between) {
							between_b = p->target;
							best_between = m;
							between_a = z;
						}
					}
				}
			}
		}
		if (outside_count == 0 && count > 0) {
			uint32_t score;
			ivec3_t c = portal_center(&zd->portals[first]);
			if (count < 2) {
				ivec3_t d = vsub(pos, c);
				score = (abs(d.x) > 25999 || abs(d.y) > 25999 || abs(d.z) > 25999) ? 0x8000 : (uint32_t)vec_mag(d);
			} else {
				for (int i = first + 1; i < first + count; i++) {
					c = vadd(c, portal_center(&zd->portals[i]));
				}
				int32_t scale = 0x1000 / count;
				c = (ivec3_t){(mul32(c.x, scale) + 0x800) >> 12, (mul32(c.y, scale) + 0x800) >> 12,
					(mul32(c.z, scale) + 0x800) >> 12};
				uint32_t r = 0;
				for (int i = first; i < first + count; i++) {
					uint32_t m = (uint32_t)vec_mag(vsub(c, portal_center(&zd->portals[i])));
					if (r < m) r = m;
					if (r < (uint32_t)zd->portals[i].radius) r = (uint32_t)zd->portals[i].radius;
				}
				ivec3_t d = vsub(pos, c);
				if (abs(d.x) < 26000 && abs(d.y) < 26000 && abs(d.z) < 26000 && r) {
					score = ((uint32_t)vec_mag(d) << 12) / r;
				} else {
					score = 0x8000;
				}
			}
			if (score < best_inside) {
				best_inside = score;
				inside = z;
			}
		}
	}
	if (mode == -2 && best_inside > 0x2260 && best_between > 6000) {
		set_add_zone(zd, s, -1);
	} else if (best_between <= best_inside) {
		set_add_zone(zd, s, between_a);
		set_add_zone(zd, s, between_b);
	} else {
		set_add_zone(zd, s, inside);
	}
}

void zoneset_init(const zonedata_t *zd, zone_set_t *s, ivec3_t pos, int zone) { // 0x485840
	memset(s, 0, sizeof(*s));
	if (!zd || zd->num_zones == 0) {
		s->zones[s->num_zones++] = -1;
		return;
	}
	if (zone < -1) {
		set_locate(zd, s, pos, zone);
	} else {
		set_add_zone(zd, s, zone);
	}
}

void zoneset_move(const zonedata_t *zd, zone_set_t *s, const ivec3_t *old, ivec3_t pos) { // 0x485880
	if (!zd || zd->num_zones == 0) {
		return;
	}
	zone_set_t backup = *s;
	ivec3_t delta = old ? vsub(pos, *old) : (ivec3_t){0, 0, 0};
	ivec3_t hit = pos;
	int repeats = 0, last = -2;
	for (;;) {
		int found = -1;
		for (int i = 0; i < s->num_boundary; i++) {
			const portal_t *p = &zd->portals[s->boundary[i].portal & 0x7fff];
			int32_t dist;
			if (portal_side_ex(zd, p, pos, s->boundary[i].portal & 0x8000, &dist)) {
				if (old && portal_segment_no_cross(zd, p, *old, delta, &dist, &hit)) {
					hit = pos;
				}
				int32_t d = portal_disc_test(zd, p, hit, dist);
				if (d != -1 && d < 0x7fffffff) {
					found = i;
					break;
				}
			}
		}
		if (found >= 0) {
			int z = s->boundary[found].zone;
			if (z == last) {
				repeats++;
			}
			set_add_zone(zd, s, z);
			last = z;
		}
		if (repeats > 4) {
			*s = backup;
			return;
		}
		found = -1;
		for (int i = 0; i < s->num_internal; i++) {
			const portal_t *p = &zd->portals[s->internal[i].portal & 0x7fff];
			int32_t dist;
			if (portal_side_ex(zd, p, pos, s->internal[i].portal & 0x8000, &dist)) {
				if (old && portal_segment_no_cross(zd, p, *old, delta, &dist, &hit)) {
					hit = pos;
				}
				int32_t d = portal_disc_test(zd, p, hit, dist);
				if (d != -1 && d < 0x7fffffff) {
					found = i;
					break;
				}
			}
		}
		if (found < 0) {
			return;
		}
		int z = s->internal[found].zone;
		if (z == last) {
			repeats++;
		}
		set_remove_zone(zd, s, z);
		last = z;
		if (repeats > 4) {
			*s = backup;
			return;
		}
	}
}

int zoneset_outside_factor(const zonedata_t *zd, const zone_set_t *s, ivec3_t pos) { // 0x485b90
	if (s->num_zones == 0 || s->zones[0] == -1) {
		return 0x1000;
	}
	for (int i = 0; i < s->num_boundary; i++) {
		if (s->boundary[i].zone != -1) {
			continue;
		}
		const portal_t *p = &zd->portals[s->boundary[i].portal & 0x7fff];
		const zplane_t *pl = &zd->planes[p->plane];
		int32_t d = plane_dot(pl, pos) - pl->d;
		if (p->flipped != ((s->boundary[i].portal & 0x8000) != 0)) {
			d = -d;
		}
		return d < 0x1001 ? 0x1000 - d : 0;
	}
	return 0;
}

// ---------------------------------------------------------------- visibility

typedef struct {
	const zonedata_t *zd;
	const zone_set_t *start;
	vis_t *vis;
	uint8_t *plane_flags;
	int depth;
	mat4_t view, proj;
	int width, height;
} vis_ctx_t;

const vis_record_t *vis_find(const vis_t *vis, int zone) {
	for (int i = 0; i < vis->count; i++) {
		if (vis->rec[i].zone == zone) {
			return &vis->rec[i];
		}
	}
	return NULL;
}

static vis_record_t *vis_add(vis_ctx_t *c, int zone, const vis_record_t *rect) { // 0x486180
	if (c->vis->count >= MAX_VIS_RECORDS) {
		return NULL;
	}
	vis_record_t *r = &c->vis->rec[c->vis->count++];
	*r = *rect;
	r->zone = (int16_t)zone;
	return r;
}

// 0x485f00: project the portal quad and intersect its bounding box with rect
static bool project_portal(vis_ctx_t *c, const portal_t *p, const vis_record_t *rect, vis_record_t *out) {
	const float near_z = 10;
	vec3_t v[16];
	int n = 0, behind = 0;
	for (int k = 0; k < p->num_vertices && k < 8; k++) {
		ivec3_t w = c->zd->vertices[p->first_vertex + k];
		v[n] = mat4_mul_point(c->view, vec3((float)w.x, (float)w.y, (float)w.z));
		if (v[n].z < -16) {
			behind++;
		}
		n++;
	}
	if (behind == n) {
		return false;
	}
	// clip against the near plane
	vec3_t clipped[16];
	int m = 0;
	for (int i = 0; i < n; i++) {
		vec3_t a = v[i], b = v[(i + 1) % n];
		bool ain = a.z >= near_z, bin = b.z >= near_z;
		if (ain) clipped[m++] = a;
		if (ain != bin) {
			float t = (near_z - a.z) / (b.z - a.z);
			clipped[m++] = vec3_add(a, vec3_mulf(vec3_sub(b, a), t));
		}
	}
	if (m == 0) {
		return false;
	}
	float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
	for (int i = 0; i < m; i++) {
		vec4_t q = mat4_mul_vec4(c->proj, (vec4_t){clipped[i].x, clipped[i].y, clipped[i].z, 1});
		float sx = (q.x / q.w + 1) * 0.5f * c->width, sy = (1 - q.y / q.w) * 0.5f * c->height;
		minx = min(minx, sx); maxx = max(maxx, sx);
		miny = min(miny, sy); maxy = max(maxy, sy);
	}
	int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
	if (!(rect->x0 < x1 && x0 < rect->x1 && rect->y0 < y1 && y0 < rect->y1)) {
		return false;
	}
	out->x0 = (int16_t)max(x0 - 1, rect->x0);
	out->y0 = (int16_t)max(y0 - 1, rect->y0);
	out->x1 = (int16_t)min(x1 + 1, rect->x1);
	out->y1 = (int16_t)min(y1 + 1, rect->y1);
	return true;
}

static void vis_recurse(vis_ctx_t *c, int zone, vis_record_t rect) { // 0x4861f0
	if (c->depth + 1 > 8) {
		return;
	}
	c->depth++;
	const zonedata_t *zd = c->zd;
	for (int i = first_portal(zd, zone); i < first_portal(zd, zone) + num_portals(zd, zone); i++) {
		const portal_t *p = &zd->portals[i];
		if (zoneset_has(c->start, p->target) || vis_find(c->vis, p->target)) {
			continue;
		}
		uint8_t f = c->plane_flags[p->plane];
		bool pass;
		vis_record_t r = rect;
		if (f & 2) {
			pass = true; // camera within 100 units of the portal plane
		} else if (((f & 1) != 0) != p->flipped) {
			pass = false; // facing away
		} else {
			pass = project_portal(c, p, &rect, &r);
		}
		if (pass) {
			vis_record_t *added = vis_add(c, p->target, &r);
			if (added) {
				vis_recurse(c, p->target, *added);
			}
		}
	}
	c->depth--;
}

void zones_compute_visible(const zonedata_t *zd, const zone_set_t *cam_set, ivec3_t cam_pos, vis_t *vis) {
	vis->count = 0;
	vis_record_t full = {0, 0, (int16_t)render_width(), 0, (int16_t)render_height()};
	if (!zd || zd->num_zones == 0) {
		vis_add(&(vis_ctx_t){.vis = vis}, -1, &full);
		return;
	}
	vis_ctx_t c = {zd, cam_set, vis, malloc(zd->num_planes + 1), 0, render_view(), render_projection(),
		render_width(), render_height()};
	for (int i = 0; i < zd->num_planes; i++) { // 0x486470: plane flags from the camera position
		const zplane_t *pl = &zd->planes[i];
		int32_t dot = plane_dot(pl, cam_pos);
		int32_t d = dot - pl->d;
		c.plane_flags[i] = (abs(d) < 100 ? 2 : 0) | (dot < pl->d ? 1 : 0);
	}
	for (int i = 0; i < cam_set->num_zones; i++) {
		vis_add(&c, cam_set->zones[i], &full);
	}
	for (int i = 0; i < cam_set->num_zones; i++) {
		c.depth = 0;
		vis_recurse(&c, cam_set->zones[i], full);
	}
	free(c.plane_flags);
}
