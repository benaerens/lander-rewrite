#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "render.h"

level_t *level_load(const char *world, const char *mission) {
	static uint32_t serial;
	level_t *lv = calloc(1, sizeof(level_t));
	lv->serial = ++serial;
	snprintf(lv->world, sizeof(lv->world), "%s", world);
	snprintf(lv->dir, sizeof(lv->dir), "MISSIONS\\%s\\%s", world, mission);
	lv->view_distance = 12800;

	char path[256];
	snprintf(path, sizeof(path), "MISSIONS\\%s\\TEXFILE\\PCPURE\\TEXFILE", world);
	lv->textures = texfile_load(path);
	lv->global_textures = texfile_load("MISC\\GLOBAL\\PCPURE\\GLOBAL");

	snprintf(path, sizeof(path), "%s\\ZONEDATA.DAT", lv->dir);
	lv->zonedata = zonedata_load(path);
	snprintf(path, sizeof(path), "%s\\ZONES.PPM", lv->dir);
	lv->zones = ppm_load(path);
	snprintf(path, sizeof(path), "%s\\TILEDATA.DAT", lv->dir);
	lv->tiledata = tiledata_load(path);
	snprintf(path, sizeof(path), "%s\\TILES.PPM", lv->dir);
	lv->tiles = ppm_load(path);
	snprintf(path, sizeof(path), "%s\\OBJECTS.PPM", lv->dir);
	lv->objects = ppm_load(path);
	lv->global_objects = ppm_load("MISC\\OBJECTS.PPM");
	lv->landers = ppm_load("MISC\\LANDERS.PPM");

	int n = lv->objects ? lv->objects->count : 0;
	int *parts = calloc(n + 1, sizeof(int));
	for (int i = 0; i < n; i++) {
		parts[i] = max(1, lv->objects->entries[i].num_parts);
	}
	snprintf(path, sizeof(path), "%s\\MISSION.DAT", lv->dir);
	lv->mission = mission_load(path, n, parts);
	free(parts);

	if (!lv->textures || !lv->mission) {
		fprintf(stderr, "level %s: missing %s\n", lv->dir, !lv->textures ? "textures" : "MISSION.DAT");
		level_free(lv);
		return NULL;
	}
	// the D3D surfaces' colour key / alpha classes, before anything (texture animation) copies frames
	model_prepare_texfile(lv->textures);
	model_prepare_texfile(lv->global_textures);
	return lv;
}

void level_free(level_t *lv) {
	if (!lv) {
		return;
	}
	model_forget_texfile(lv->textures);
	model_forget_texfile(lv->global_textures);
	texfile_free(lv->textures);
	texfile_free(lv->global_textures);
	zonedata_free(lv->zonedata);
	ppm_free(lv->zones);
	tiledata_free(lv->tiledata);
	ppm_free(lv->tiles);
	ppm_free(lv->objects);
	ppm_free(lv->global_objects);
	ppm_free(lv->landers);
	mission_free(lv->mission);
	free(lv);
}

static model_t *zone_model(const level_t *lv, int z) {
	if (!lv->zones || z < 0 || z >= lv->zones->count || !lv->zones->entries[z].num_parts) {
		return NULL;
	}
	return lv->zones->entries[z].parts[0];
}

int level_zone_at(const level_t *lv, vec3_t p) {
	if (!lv->zonedata) {
		return -1;
	}
	for (int z = 0; z < lv->zonedata->num_zones; z++) {
		model_t *m = zone_model(lv, z);
		if (!m) {
			continue;
		}
		ivec3_t o = lv->zonedata->origins[z];
		vec3_t lo = vec3_add(m->bbox_min, vec3(o.x, o.y, o.z)), hi = vec3_add(m->bbox_max, vec3(o.x, o.y, o.z));
		if (p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y && p.z >= lo.z && p.z <= hi.z) {
			return z;
		}
	}
	return -1;
}

static vec3_t rgb_f(rgba_t c) {
	return vec3(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
}

static vec3_t mat_col(mat4_t m, int c) {
	return vec3(m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2]);
}

static bool reaches(vec3_t lp, float range, vec3_t center, float radius) {
	vec3_t d = vec3_sub(lp, center);
	return vec3_dot(d, d) <= (range + radius) * (range + radius);
}

// Lights that can reach a bounding sphere.
static void gather_lights(const level_t *lv, const lighting_t *base, vec3_t center, float radius, lighting_t *out) {
	*out = *base;
	const mission_t *m = lv->mission;
	for (int i = 0; i < m->num_lights[2] && out->num_spot < MAX_SPOT_LIGHTS; i++) {
		const light_spot_t *sl = &m->spot_lights[i];
		vec3_t lp = vec3(sl->pos.x, sl->pos.y, sl->pos.z);
		if (!reaches(lp, (float)sl->inner + sl->falloff, center, radius)) {
			continue;
		}
		int k = out->num_spot++;
		out->spot[k].pos = lp;
		out->spot[k].dir = mat_col(rot_matrix(sl->rot), 2);
		out->spot[k].color = rgb_f(sl->color);
		out->spot[k].inner = (float)sl->inner;
		out->spot[k].falloff = sl->falloff > 0 ? (float)sl->falloff : 1;
		out->spot[k].cos_inner = cosf(sl->cone_inner * PI / 4096);
		out->spot[k].cos_outer = cosf(sl->cone_outer * PI / 4096);
	}
	for (int i = 0; i < m->num_lights[3] && out->num_box < MAX_BOX_LIGHTS; i++) {
		const light_box_t *bl = &m->box_lights[i];
		vec3_t lp = vec3(bl->pos.x, bl->pos.y, bl->pos.z);
		const int32_t *e = bl->box;
		float ry = (float)max(e[2] + e[3], e[6] + e[7]);
		float reach = sqrtf((float)(e[0] + e[1]) * (e[0] + e[1]) + ry * ry + (float)(e[4] + e[5]) * (e[4] + e[5]));
		if (!reaches(lp, reach, center, radius)) {
			continue;
		}
		int k = out->num_box++;
		mat4_t r = rot_matrix(bl->rot);
		out->box[k].pos = lp;
		for (int a = 0; a < 3; a++) {
			out->box[k].axis[a] = mat_col(r, a);
		}
		out->box[k].color = rgb_f(bl->color);
		for (int a = 0; a < 8; a++) {
			// raw values: a zero/negative falloff is a hard edge (the fade branch is then unreachable)
			out->box[k].ext[a] = (float)e[a];
		}
	}
	for (int i = 0; i < m->num_lights[1] && out->num_point < MAX_POINT_LIGHTS; i++) {
		const light_point_t *pl = &m->point_lights[i];
		vec3_t lp = vec3(pl->pos.x, pl->pos.y, pl->pos.z);
		float range = (float)pl->inner + pl->falloff;
		vec3_t d = vec3_sub(lp, center);
		if (vec3_dot(d, d) > (range + radius) * (range + radius)) {
			continue;
		}
		int k = out->num_point++;
		out->point_pos[k] = lp;
		out->point_color[k] = rgb_f(pl->color);
		out->point_inner[k] = (float)pl->inner;
		out->point_falloff[k] = pl->falloff > 0 ? (float)pl->falloff : 1;
	}
}

static bool sphere_visible(const camera_t *cam, vec3_t c, float r, float dist) {
	vec3_t d = vec3_sub(c, cam->pos);
	return vec3_dot(d, d) < (dist + r) * (dist + r);
}

static void draw_model(const level_t *lv, const model_t *m, mat4_t t, texfile_t *tf, const lighting_t *base,
	const camera_t *cam, float dist) {
	if (!m) {
		return;
	}
	vec3_t c = mat4_mul_point(t, vec3_mulf(vec3_add(m->bbox_min, m->bbox_max), 0.5f));
	if (!sphere_visible(cam, c, m->radius, dist)) {
		return;
	}
	lighting_t L;
	gather_lights(lv, base, c, m->radius, &L);
	model_draw(m, t, tf, &L);
}

static mat4_t record_matrix(ivec3_t pos, uint32_t rot) {
	return mat4_mul(mat4_translate(vec3(pos.x, pos.y, pos.z)), rot_matrix(rot));
}

static void draw_record(const level_t *lv, const map_rec_t *r, int table, const lighting_t *base,
	const camera_t *cam, float dist) {
	const mission_t *ms = lv->mission;
	mat4_t t = record_matrix(r->pos, r->rot);
	if (table == REC_MOB && r->type == OBJ_LANDER) {
		if (lv->landers && lv->landers->count) {
			draw_model(lv, lv->landers->entries[0].parts[0], t, lv->global_textures, base, cam, dist);
		}
		return;
	}
	if (r->model < 0 || !lv->objects || r->model >= lv->objects->count) {
		return;
	}
	const ppm_entry_t *e = &lv->objects->entries[r->model];
	const model_info_t *mi = r->model < ms->num_models ? &ms->models[r->model] : NULL;
	for (int k = 0; k < e->num_parts; k++) {
		mat4_t pt = t;
		if (e->num_parts > 1 && mi && k < mi->num_parts) {
			const model_part_t *p = &mi->parts[k];
			pt = mat4_mul(t, record_matrix(p->pos, p->rot));
		}
		draw_model(lv, e->parts[k], pt, lv->textures, base, cam, dist);
	}
}

// Env_LerpInOut (0x40f940): interior -> exterior by the outside factor (0..0x1000)
static rgba_t lerp_inout(rgba_t in, rgba_t ex, int f) {
	if (f == 0) return in;
	if (f == 0x1000) return ex;
	return rgba((uint8_t)(in.r + (((ex.r - in.r) * f) >> 12)), (uint8_t)(in.g + (((ex.g - in.g) * f) >> 12)),
		(uint8_t)(in.b + (((ex.b - in.b) * f) >> 12)), 255);
}

void level_lighting(const level_t *lv, const lighting_t *base, vec3_t center, float radius, lighting_t *out) {
	gather_lights(lv, base, center, radius, out);
}

const rgba_t *level_exterior_override;

void level_begin_frame(level_t *lv, const camera_t *cam, lighting_t *base_out) {
	const mission_t *ms = lv->mission;
	float vd = lv->view_distance;

	if (cam->has_basis) {
		render_set_view(mat4_look_at(cam->pos, vec3_add(cam->pos, cam->fwd), cam->up));
	} else {
		vec3_t fwd = vec3(sinf(cam->yaw) * cosf(cam->pitch), sinf(cam->pitch), cosf(cam->yaw) * cosf(cam->pitch));
		render_set_view(mat4_look_at(cam->pos, vec3_add(cam->pos, fwd), vec3(0, 1, 0)));
	}
	// FwSetScreenAngle(900): 900/4096 of a turn horizontally across the original's 4:3 screen. A wider
	// frame keeps that vertical view and sees more at the sides (Hor+); the original, which derives the
	// projection from the screen's half width, would show the same width zoomed in (Vert-).
	float hfov = 900.0f / 4096.0f * 2 * PI;
	float vfov = 2 * atanf(tanf(hfov / 2) / (4.0f / 3.0f));
	render_set_projection(vfov, 10, vd * 1.25f);

	// which zones the camera is in (tracked through portals like the game's camera)
	ivec3_t cpos = {(int32_t)cam->pos.x, (int32_t)cam->pos.y, (int32_t)cam->pos.z};
	// a jump (teleport, a script moving the camera) cannot be followed through portals: locate again
	if (lv->cam_set_valid && (abs(cpos.x - lv->cam_last.x) > 0x2000 || abs(cpos.y - lv->cam_last.y) > 0x2000 ||
		abs(cpos.z - lv->cam_last.z) > 0x2000)) {
		lv->cam_set_valid = false;
	}
	if (!lv->cam_set_valid) {
		zoneset_init(lv->zonedata, &lv->cam_set, cpos, -2);
		lv->cam_set_valid = true;
	} else {
		zoneset_move(lv->zonedata, &lv->cam_set, &lv->cam_last, cpos);
	}
	lv->cam_last = cpos;
	lv->outside_factor = lv->zonedata ? zoneset_outside_factor(lv->zonedata, &lv->cam_set, cpos) : 0x1000;

	rgba_t exterior = ms->exterior;
	if (level_exterior_override) { // DAT_0049bf20: one frame's exterior colour (lightning flash)
		exterior = *level_exterior_override;
		level_exterior_override = NULL;
	}
	rgba_t env = lerp_inout(ms->interior, exterior, lv->outside_factor);
	render_frame_begin(env);
	float fog_near = ms->fog_far ? vd * ms->fog_near / ms->fog_far : vd * 0.75f;
	render_set_fog(true, env, fog_near, vd);

	lighting_t base = {0};
	base.ambient = rgb_f(env);
	float ext = lv->outside_factor / 4096.0f; // ExteriorLights_SetFactor (0x451c00)
	for (int i = 0; i < ms->num_lights[0] && i < 4 && ext > 0; i++) {
		base.dir[base.num_dir] = ms->dir_lights[i].dir;
		base.dir_color[base.num_dir++] = vec3_mulf(rgb_f(ms->dir_lights[i].color), ext);
	}
	*base_out = base;

	// portal visibility from the camera's zones
	zones_compute_visible(lv->zonedata, &lv->cam_set, cpos, &lv->vis);
}

void level_draw_world(level_t *lv, const camera_t *cam, const lighting_t *base_in) {
	float vd = lv->view_distance;
	lighting_t base = *base_in;
	if (lv->zonedata) {
		for (int z = 0; z < lv->zonedata->num_zones; z++) {
			const vis_record_t *r = vis_find(&lv->vis, z);
			if (!r) {
				continue;
			}
			render_set_scissor(r->x0, r->y0, r->x1, r->y1);
			ivec3_t o = lv->zonedata->origins[z];
			draw_model(lv, zone_model(lv, z), mat4_translate(vec3(o.x, o.y, o.z)), lv->textures, &base, cam, vd);
		}
	}

	level_draw_tiles(lv, cam, &base);
	render_reset_scissor();
}

// Tiles_QueueVisible 0x482ed0: the exterior tiles, only when the outside is visible, clipped to its
// rectangle (the outside's clip set of this frame's portal visibility)
void level_draw_tiles(level_t *lv, const camera_t *cam, const lighting_t *base_in) {
	float vd = lv->view_distance;
	lighting_t base = *base_in;
	const vis_record_t *outside_rec = vis_find(&lv->vis, -1);
	if (lv->tiledata && lv->tiles && outside_rec) {
		render_set_scissor(outside_rec->x0, outside_rec->y0, outside_rec->x1, outside_rec->y1);
		int tx0 = tile_index((int32_t)(cam->pos.x - vd)), tx1 = tile_index((int32_t)(cam->pos.x + vd));
		int tz0 = tile_index((int32_t)(cam->pos.z - vd)), tz1 = tile_index((int32_t)(cam->pos.z + vd));
		for (int tz = tz0; tz <= tz1; tz++) {
			for (int tx = tx0; tx <= tx1; tx++) {
				tile_cell_t cells[8];
				int n = tiledata_lookup(lv->tiledata, tx, tz, cells, 8);
				for (int i = 0; i < n; i++) {
					if (cells[i].model >= lv->tiles->count || !lv->tiles->entries[cells[i].model].num_parts) {
						continue;
					}
					float ax, az, bx, bz;
					tile_rotate(cells[i].rotation, 1, 0, &ax, &az);
					tile_rotate(cells[i].rotation, 0, 1, &bx, &bz);
					mat4_t r = mat4_identity();
					r.m[0] = ax; r.m[2] = az;   // image of local +x
					r.m[8] = bx; r.m[10] = bz;  // image of local +z
					mat4_t t = mat4_mul(mat4_translate(vec3(tile_center(tx), cells[i].y, tile_center(tz))),
						mat4_mul(r, mat4_scale(vec3(1.0009766f, 1.0009766f, 1.0009766f))));
					draw_model(lv, lv->tiles->entries[cells[i].model].parts[0], t, lv->textures, &base, cam, vd);
				}
			}
		}
	}

	render_reset_scissor();
}

void level_draw(level_t *lv, const camera_t *cam) {
	const mission_t *ms = lv->mission;
	float vd = lv->view_distance;
	lighting_t base;
	level_begin_frame(lv, cam, &base);
	level_draw_world(lv, cam, &base);

	// map objects: statics, objects, and mobs that are visible models
	for (int table = 0; table < 3; table++) {
		for (int i = 0; i < ms->num_rec[table]; i++) {
			const map_rec_t *r = &ms->rec[table][i];
			if (table == REC_MOB && (r->type == OBJ_EXPLOSION || r->type == OBJ_AMBIENT_FX)) {
				continue;
			}
			vec3_t p = vec3(r->pos.x, r->pos.y, r->pos.z);
			if (!sphere_visible(cam, p, 0, vd * 1.25f)) {
				continue;
			}
			draw_record(lv, r, table, &base, cam, vd);
		}
	}
}
