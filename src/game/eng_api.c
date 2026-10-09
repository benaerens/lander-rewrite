// The rest of the mission-script API (mission_api.h; re/specs/mission-api.md §5)
// and its globals. Object, event, sound, UI, font and camera exports live in
// their own modules.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "../mission.h"

// ---------------------------------------------------------------- globals
int32_t gAirResistance = 21, gMainThrottlePower = 48, gInOutOverride, gTrainingMission;
const Waypoint *gWaypoints;
int32_t gNbWaypoints;
const TurretType *gTurretTypes[TURRET_TYPE_SLOTS];
int32_t gSmallCash = 2500, gMediumCash = 5000, gLargeCash = 7500;
GameScoreData gGame_ScoreData;
int32_t gMultiPlayerGame, gNbPlayers, gNetworkGameType, gJoinedGame, gThisPlayer = -1;
ObjId gPlayerObjects[16];
int32_t gPlayerIds[16];
uint32_t gTeamAShipColour = 0, gTeamBShipColour = 1;   // indices into gShipColours (0x4a6de4 bss, 0x49359c)
struct BorderInfoRec { int unused; } BorderInfo;
int32_t gLanguage;
int32_t gGravity = 26;
int32_t gMissionResult;
static uint32_t rand_seed = 0x8fd53ae3;
static char level_end_movie[16];

// ---------------------------------------------------------------- level control

void LevelEnd(int32_t result) { // 0x444330
	gMissionResult = result;
	gLevelEndFlag = 1;
}

void SetLevelEndMovie(const char *name) { // 0x444760
	snprintf(level_end_movie, sizeof(level_end_movie), "%s", name ? name : "");
}

const char *game_level_end_movie(void) { return level_end_movie; }

void AbortLevel(void) { // 0x47b950: fatal
	fprintf(stderr, "A serious problem occurred in Lander (AbortLevel).\n");
	exit(3);
}

void SetGravity(int32_t g) { // 0x4442f0
	gGravity = g;
	phys_set_gravity(g, gUseInertialFrame ? (const phys_matrix_t *)&gInertialFrame : NULL);
}

// ---------------------------------------------------------------- environment (0x40fb20..)

static rgba_t *env_interior(void) { return &G.level->mission->interior; }
static rgba_t *env_exterior(void) { return &G.level->mission->exterior; }

void Environment_SetInteriorAmbient(int32_t r, int32_t g, int32_t b) { *env_interior() = rgba(r, g, b, 255); }
void Environment_SetExteriorAmbient(int32_t r, int32_t g, int32_t b) { *env_exterior() = rgba(r, g, b, 255); }
void Environment_OverrideInteriorFog(int32_t r, int32_t g, int32_t b) { (void)r; (void)g; (void)b; }
void Environment_OverrideExteriorFog(int32_t r, int32_t g, int32_t b) { (void)r; (void)g; (void)b; }

static float saved_view_distance;
void Environment_OverrideViewDistance(int32_t fog_near, int32_t fog_far) { // 0x40fbe0
	saved_view_distance = G.level->view_distance;
	G.level->view_distance = (float)(fog_far > 0 ? fog_far : 1000);
	(void)fog_near;
}
void Environment_RestoreViewDistance(void) {
	if (saved_view_distance > 0) G.level->view_distance = saved_view_distance;
}

// ---------------------------------------------------------------- objects and misc

void SnapToTarget(ObjId obj, ObjId target) { // 0x444470
	const FwMatrix *m = (const FwMatrix *)Object_GetProperty(target, PROP_MATRIX);
	const int32_t *p = (const int32_t *)Object_GetProperty(target, PROP_POSITION);
	if (m) Object_SetProperty(obj, PROP_MATRIX, (intptr_t)m);
	if (p) Object_SetProperty(obj, PROP_POSITION, (intptr_t)p);
}

static FwMatrix teleport_rot;
static int32_t teleport_offset[3], teleport_pivot[3];

void SetupOrientationTeleport(ObjId from, ObjId to) { // 0x4444c0
	const FwMatrix *a = (const FwMatrix *)Object_GetProperty(from, PROP_MATRIX);
	const FwMatrix *b = (const FwMatrix *)Object_GetProperty(to, PROP_MATRIX);
	if (!a || !b) return;
	// rotation from a to b: b * a^T
	FwMatrix at = *a;
	for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) at.m[r][c] = a->m[c][r];
	teleport_rot = *b;
	fw_mul(b, &at, &teleport_rot);
	for (int i = 0; i < 3; i++) {
		teleport_pivot[i] = a->t[i];
		teleport_offset[i] = b->t[i] - a->t[i];
	}
}

void PositionTeleport(ObjId obj) { // 0x444560
	const int32_t *p = (const int32_t *)Object_GetProperty(obj, PROP_POSITION);
	if (!p) return;
	int32_t d[3] = {p[0] - teleport_pivot[0], p[1] - teleport_pivot[1], p[2] - teleport_pivot[2]}, r[3];
	fw_apply(&teleport_rot, d, r);
	int32_t np[3] = {teleport_pivot[0] + r[0] + teleport_offset[0], teleport_pivot[1] + r[1] + teleport_offset[1],
		teleport_pivot[2] + r[2] + teleport_offset[2]};
	Object_SetProperty(obj, PROP_POSITION, (intptr_t)np);
}

void OrientationTeleport(ObjId obj) { // 0x444620
	PositionTeleport(obj);
	const FwMatrix *m = (const FwMatrix *)Object_GetProperty(obj, PROP_MATRIX);
	if (m) {
		FwMatrix nm = *m;
		fw_mul(&teleport_rot, m, &nm);
		Object_SetProperty(obj, PROP_MATRIX, (intptr_t)&nm);
	}
}

int32_t LanderAPI_LineOfSight(const int32_t from[3], const int32_t to[3]) { // 0x4447a0
	for (int i = 0; i < 3; i++) {
		if (abs(to[i] - from[i]) > 0x2000) return 0;
	}
	G.world.query_mask = 3;
	phys_hit_t h;
	int clear = phys_segment_hit(from, to, 0, &h);   // 1 = nothing in the way
	G.world.query_mask = -1;                         // the original sets 0xffffffff, not the old mask
	return clear;
}

int32_t Player_GetId(ObjId lander) { // 0x444200
	for (int i = 0; i < 16; i++) {
		if (gPlayerObjects[i] == lander) return gPlayerIds[i];
	}
	return -1;
}

uint32_t LndrRand(void) { // 0x444420
	uint32_t s = rand_seed;
	s = ((s << 5) | (s >> 27)) ^ s;
	s += 0x518c28d3;
	s ^= (s << 11) | (s >> 21);
	rand_seed = s;
	return s;
}

void eng_api_reset_mission(void) {
	rand_seed = 0x8fd53ae3;
	level_end_movie[0] = 0;
}

const char *Message_GetNameFromId(int32_t player) { (void)player; return "Player"; }

void FwVectorAdd(const int32_t a[3], const int32_t b[3], int32_t out[3]) {
	out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
}

int32_t FwVectorNormalise(int32_t v[3]) { return phys_normalise(v); }

void GameState_RegisterDataBlock(void *block, int32_t size) { (void)block; (void)size; }
void GameState_RegisterFunction(void (*fn)(void)) { (void)fn; }
void GameState_UnRegisterFunction(void (*fn)(void)) { (void)fn; }
