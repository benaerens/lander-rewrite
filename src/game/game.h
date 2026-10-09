// The in-game part of the program: Mission_Load / Mission_Run / Mission_Unload
// (0x413d20, 0x4146c0, 0x4143d0) with their system tasks, the camera object
// types and the HUD.
#ifndef GAME_H
#define GAME_H

#include "engine.h"
#include "lander.h"
#include "camera.h"

typedef struct {
	const char *world, *mission;   // folder names, e.g. "MARS", "1A"
	int world_index, mission_index;
	const mission_script_t *script;  // MISSIONR.DLL port (functions NULL = not ported: free flight)
	const brief_script_t *brief;     // brief.dll port (NULL for the deathmatch maps)
} mission_def_t;

const mission_def_t *mission_find(const char *world, const char *mission);
const mission_def_t *mission_list(int *count);
bool mission_is_ported(const mission_def_t *m);

bool game_load_mission(const mission_def_t *m);   // Mission_LoadFiles + Mission_Load + Mission_Run set-up
// Mission_Run with a start mode (0/3 lander, 1 FlyCam, 2 AnimCam, 4 AnimCam + lander);
// game_load_mission(def) is mode 0 (or $LANDER_START_MODE)
bool game_load_mission_mode(const mission_def_t *def, int mode);
// one frame; returns false when the mission has ended (result in G.mission_result)
bool game_mission_frame(void);
void game_unload_mission(void);

void game_over(int dead);                          // Mission_GameOver 0x414680
void game_hud_create(int lander_type);
void game_hud_destroy(void);
int game_hud_marker_add(int32_t what, uint32_t rgb, int32_t kind);
void game_hud_marker_remove(int marker);
void game_hud_lander_tick(Lander *L);
extern int32_t gMissionResult;

void lander_register_type(void);                   // eng_lander.c
void eng_api_reset_mission(void);                  // eng_api.c
cam_view_t *game_view(void);                       // the view node gView (0x4e9178) the cameras drive
extern task_t *gPhysicsTask;                       // DAT_00577c38 (the pods phase their updates with it)
const char *game_level_end_movie(void);            // SetLevelEndMovie's film ("" = none)
extern uint32_t gCtrlWords[16];
extern int32_t gGravity;

#endif
