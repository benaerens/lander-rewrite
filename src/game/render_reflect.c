// The Venus reflection pass (Mission_Run 0x4146c0). gMirrorPass (0x491be4) is set for world 1 (VENUS)
// when the mission is launched; then every frame, after the main scene was queued and its render list
// run into OT+0x18, if there is a camera object, the outside is visible and the camera node is at or
// above y = 0:
//   gMirrorCamera (0x577c50) = the camera node with row 1 of its matrix and t.y negated, a reflection
//   about the plane y = 0 (the level of the Venus seas); View_SetCamera(gMirrorCamera);
//   gReflectionPass (0x49c5b4) = 1; gSortTable = gSortTableMirror;
//   Tiles_QueueVisible; Objects_QueueVisible(drawDist, 1); Sched_RenderReflective (task types with
//   flag 8: the lander 0x421030, projectiles 0x452990, explosions 0x44e610, particle emitters 0x44dfc0,
//   the particles 0x454bb0); DrawGroup_Queue(0x491be0) (particles, Segmented, light glows, tethers);
//   RenderList_Run(OT+0x10); then the mirror translucency table is flushed by a node at the head of
//   OT+0x18, and everything is restored.
// OT+0x10 executes before the main scene (OT+0x18) and the z-buffer is not cleared in between: the
// reflection is the background that the sea's alpha-blended texture is drawn over. Culling uses the
// mirror camera's eye (Pcp_ComputeBackfaceFlags with the camera of View_SetCamera), as the renderer's
// winding test does with a reflected view. The rewrite draws immediately, so game_mission_frame records
// the main scene and replays it after this pass (render_record_begin / render_replay).
#include <stdlib.h>
#include <string.h>

#include "render_reflect.h"
#include "lander.h"
#include "effects.h"

// ---------------------------------------------------------------- records of the level

// Objects_QueueVisible(.., 1) takes only statics/objects whose record zone is -1 (exterior) after
// MapRec_ComputeZones 0x4453b0 (a located set of exactly the exterior). Statics have no object id:
// their instances are found by the transform MapRec_CreateInstance 0x444ec0 gave them.
typedef struct {
	bool exterior;
	const model_t *model;     // first part
	int32_t t[3];             // first part's node translation
	int16_t inst;             // this frame
} stat_t;

static uint32_t cache_serial;
static stat_t *stats;
static int nstats;
static bool *obj_exterior;
static int *hash;             // static indices + 1 by first-part translation
static int hash_size;

static void add_zones(int16_t *set, int *n, const zone_set_t *zs) { // FUN_004452f0
	for (int i = 0; i < zs->num_zones; i++) {
		int k = 0;
		while (k < *n && set[k] != zs->zones[i]) k++;
		if (k == *n && *n < 16) set[(*n)++] = zs->zones[i];
	}
}

// MapRec_ComputeZones 0x4453b0 (as objects.c): the position and 17 points of the rotated bounding box
static bool rec_exterior(const map_rec_t *r) {
	const zonedata_t *zd = G.level->zonedata;
	if (r->zone == -3) return false;
	if (r->zone == -1 || !zd || zd->num_zones == 0) return true;
	if (r->zone > 0) return false;
	int mode = r->zone == -2 ? -3 : -2;
	int16_t set[16];
	int n = 0;
	zone_set_t zs;
	zoneset_init(zd, &zs, r->pos, mode);
	add_zones(set, &n, &zs);
	const mission_t *m = G.level->mission;
	if (r->model >= 0 && r->model < m->num_models && m->bbox) {
		const int16_t *bb = m->bbox[r->model];
		FwMatrix rot;
		fw_from_packed(r->rot, &rot);
		for (int k = 0; k < 17; k++) {
			int32_t p[3];
			for (int i = 0; i < 3; i++) {
				int32_t mid = (bb[i] + bb[3 + i]) >> 1;
				if (k < 16) {
					p[i] = (k >> i) & 1 ? bb[3 + i] : bb[i];
					if (k > 7) p[i] = (p[i] + mid) >> 1;
				} else {
					p[i] = mid;
				}
			}
			fw_apply(&rot, p, p);
			ivec3_t at = {r->pos.x + p[0], r->pos.y + p[1], r->pos.z + p[2]};
			zoneset_init(zd, &zs, at, mode);
			add_zones(set, &n, &zs);
		}
	}
	return n == 1 && set[0] == -1;
}

static uint32_t hash_of(const int32_t t[3]) {
	return ((uint32_t)t[0] * 73856093u) ^ ((uint32_t)t[1] * 19349663u) ^ ((uint32_t)t[2] * 83492791u);
}

static void build_cache(void) {
	const level_t *lv = G.level;
	const mission_t *m = lv->mission;
	free(stats);
	free(obj_exterior);
	free(hash);
	nstats = m->num_rec[REC_STATIC];
	stats = calloc(nstats + 1, sizeof(stat_t));
	obj_exterior = calloc(m->num_rec[REC_OBJECT] + 1, sizeof(bool));
	for (hash_size = 16; hash_size < nstats * 2; hash_size <<= 1) {}
	hash = calloc(hash_size, sizeof(int));
	for (int i = 0; i < m->num_rec[REC_OBJECT]; i++) obj_exterior[i] = rec_exterior(&m->rec[REC_OBJECT][i]);
	for (int i = 0; i < nstats; i++) {
		const map_rec_t *r = &m->rec[REC_STATIC][i];
		stat_t *s = &stats[i];
		s->inst = -1;
		if (r->model < 0 || !lv->objects || r->model >= lv->objects->count || !lv->objects->entries[r->model].num_parts) {
			continue;
		}
		s->exterior = rec_exterior(r);
		if (!s->exterior) continue;
		const ppm_entry_t *e = &lv->objects->entries[r->model];
		const model_info_t *mi = r->model < m->num_models ? &m->models[r->model] : NULL;
		FwMatrix root, first;
		fw_from_packed(r->rot, &root);
		root.t[0] = r->pos.x;
		root.t[1] = r->pos.y;
		root.t[2] = r->pos.z;
		first = root;
		if (e->num_parts >= 2 && mi && mi->num_parts > 0) { // as rec_create_instance
			FwMatrix pm;
			fw_from_packed(mi->parts[0].rot, &pm);
			pm.t[0] = mi->parts[0].pos.x;
			pm.t[1] = mi->parts[0].pos.y;
			pm.t[2] = mi->parts[0].pos.z;
			fw_compose(&root, &pm, &first);
		}
		s->model = e->parts[0];
		memcpy(s->t, first.t, sizeof(s->t));
		uint32_t h = hash_of(s->t) & (hash_size - 1);
		while (hash[h]) h = (h + 1) & (hash_size - 1);
		hash[h] = i + 1;
	}
	cache_serial = lv->serial;
}

// the awake statics' instances (an instance exists only while the static is awake)
static void find_static_instances(void) {
	for (int i = 0; i < nstats; i++) stats[i].inst = -1;
	for (int k = 0; k < MAX_INSTANCES; k++) {
		const model_inst_t *mi = &gInstances[k];
		if (!mi->used || !mi->model || !mi->coord || mi->coord->parent || mi->shared_coord) continue;
		const int32_t *t = mi->coord->local.t;
		for (uint32_t h = hash_of(t) & (hash_size - 1); hash[h]; h = (h + 1) & (hash_size - 1)) {
			stat_t *s = &stats[hash[h] - 1];
			if (s->inst < 0 && s->model == mi->model && !memcmp(s->t, t, sizeof(s->t))) {
				s->inst = (int16_t)k;
				break;
			}
		}
	}
}

// ---------------------------------------------------------------- Objects_QueueVisible(.., 1) 0x447d20

static bool in_range(const map_rec_t *r, vec3_t from, float distance) { // ObjList_QueueInRange 0x447c70
	return min(fabsf(r->pos.x - from.x), fabsf(r->pos.z - from.z)) < distance;
}

static void queue_objects(vec3_t from, float distance) {
	const mission_t *m = G.level->mission;
	find_static_instances();
	for (int i = 0; i < nstats; i++) {
		if (stats[i].inst >= 0 && in_range(&m->rec[REC_STATIC][i], from, distance)) inst_queue(stats[i].inst);
	}
	for (int i = 0; i < m->num_rec[REC_OBJECT]; i++) {
		const map_rec_t *r = &m->rec[REC_OBJECT][i];
		if (r->model == -1 || !obj_exterior[i]) continue;
		ObjId id = OBJ_ID(r->type, i);
		int16_t inst = Object_IsAwake(id) ? Object_GetInstance(id) : -1;
		if (inst >= 0 && in_range(r, from, distance)) inst_queue(inst);
	}
	// mobs: every awake one with a model whose node is at y >= 0; pods only above a liquid
	for (int i = 0; i < m->num_rec[REC_MOB]; i++) {
		const map_rec_t *r = &m->rec[REC_MOB][i];
		if (r->model == -1) continue;
		ObjId id = MOB_ID(r->type, i);
		int16_t inst = Object_IsAwake(id) ? Object_GetInstance(id) : -1;
		if (inst < 0 || !gInstances[inst].used || !gInstances[inst].coord) continue;
		if (coord_world(gInstances[inst].coord)->t[1] < 0) continue;
		if (r->type == OT_POD && !gLanderAboveLiquid) continue;
		inst_queue(inst);
	}
}

// ---------------------------------------------------------------- the pass

bool render_reflect_wanted(const camera_t *cam) {
	level_t *lv = G.level;
	static int disabled = -1; // LANDER_NO_REFLECT=1: test aid, the frame without the pass
	if (disabled < 0) disabled = getenv("LANDER_NO_REFLECT") != NULL;
	gMirrorProbe = G.world_index == 1; // gMirrorPass = (gCurWorld == 1), Game_ModeLoop 0x4137xx
	if (!gMirrorProbe || disabled || !lv || gCameraObject == OBJ_NONE) return false;
	bool outside = !lv->zonedata || !lv->zonedata->num_zones || vis_find(&lv->vis, -1); // gOutsideVisible
	return outside && (int32_t)cam->pos.y >= 0;  // -1 < camera node t.y
}

void render_reflect_draw(const camera_t *cam, uint32_t now) {
	level_t *lv = G.level;
	if (cache_serial != lv->serial) build_cache();
	// the types that set flag 8 in their create functions (Lander_Create sets its own)
	gObjectTypes[OT_PROJECTILE].task.flags |= 8;
	gObjectTypes[OT_EXPLOSION].task.flags |= 8;
	gObjectTypes[OT_PARTICLEEMITTER].task.flags |= 8;

	mat4_t view = render_view();
	vec3_t eye = G.cam_pos;
	camera_t mc = *cam; // gMirrorCamera: the camera node reflected about y = 0
	mc.pos.y = -cam->pos.y;
	mc.fwd.y = -cam->fwd.y;
	mc.up.y = -cam->up.y;
	render_set_view(mat4_mul(view, mat4_scale(vec3(1, -1, 1))));
	G.cam_pos = mc.pos;
	gReflectionPass = 1;
	effects_reflection_begin(now);

	level_draw_tiles(lv, &mc, &G.base_light);           // Tiles_QueueVisible
	render_list_reset();
	queue_objects(eye, lv->view_distance);              // ranges from the camera object, as the main pass
	Sched_RenderReflective(now);
	render_list_run();
	effects_reflection_end(now);                        // the group items and the mirror sort table

	gReflectionPass = 0;
	G.cam_pos = eye;
	render_set_view(view);
	render_reset_scissor();
	render_set_model(mat4_identity());
	render_set_depth(true, true);
	render_set_cull(CULL_BACK);
}
