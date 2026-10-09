// Ported mission scripts: one mission_script_t per MISSIONR.DLL, one brief_script_t per brief.dll
// (missions/<world>_<n>.c and <world>_<n>_brief.c). A script whose functions are NULL is not ported
// yet: the game flies that mission as free flight. Status: re/specs/mission-api.md "Porting status".
#ifndef LANDER_MISSIONS_H
#define LANDER_MISSIONS_H

#include "../mission_api.h"

#define LANDER_MISSIONS(X) \
	X(training_1) X(training_2) X(mercury_1) X(venus_1) X(venus_2) X(earth_1) X(earth_2) X(earth_3) \
	X(earth_4) X(mars_1a) X(mars_1b) X(mars_2) X(mars_3) X(mars_4) X(mars_5) X(europa_1) X(europa_2) \
	X(callisto_1) X(callisto_2) X(ganymede_1) X(io_1) X(io_2) X(titan_1) X(titan_2) X(titan_3) \
	X(mimas_1) X(rhea_1) X(rhea_2) X(neptune_1) X(neptune_2) X(pluto_1) X(deathmat_1) X(deathmat_3)

#define LANDER_BRIEFS(X) \
	X(training_1) X(training_2) X(mercury_1) X(venus_1) X(venus_2) X(earth_1) X(earth_2) X(earth_3) \
	X(earth_4) X(mars_1a) X(mars_1b) X(mars_2) X(mars_3) X(mars_4) X(mars_5) X(europa_1) X(europa_2) \
	X(callisto_1) X(callisto_2) X(ganymede_1) X(io_1) X(io_2) X(titan_1) X(titan_2) X(titan_3) \
	X(mimas_1) X(rhea_1) X(rhea_2) X(neptune_1) X(neptune_2) X(pluto_1)

#define LANDER_DECLARE_MISSION(n) extern const mission_script_t mission_##n;
#define LANDER_DECLARE_BRIEF(n) extern const brief_script_t brief_##n;
LANDER_MISSIONS(LANDER_DECLARE_MISSION)
LANDER_BRIEFS(LANDER_DECLARE_BRIEF)

#endif // LANDER_MISSIONS_H
