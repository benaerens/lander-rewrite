// MISSIONS\MERCURY\1\MISSIONR.DLL - faithful C port.
//
// Mission: fly to waypoint 1, hook the downed satellite (pod, mob 2) and climb above y = 10000
// with it. Success 2 if no shield was lost since the start, else 1; +14000 cash either way.
// Shooting a gun tower (0xf001d..1f) reveals three cash pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Animcam 0x10001020, LevelStartLevel_Flycam
// 0x10001030, LevelStopLevel 0x10001040, LevelGetVersion 0x10001050. The score library
// (0x10001070-0x10001614) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define M1_LANDER        MOB_ID(OT_LANDER, 6)            // 0x01010006
#define M1_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define M1_MOB_CAMERA    1
#define M1_MOB_POD       2                               // the satellite
#define M1_MOB_PATROL_A  3                               // AnimDestructible 0x010f0003
#define M1_MOB_PATROL_B  4                               // AnimDestructible 0x010f0004
#define M1_MOB_FLYCAM    5
#define M1_MOB_LANDER    6
#define M1_MOB_AMBIENT_A 7                               // AmbientFX
#define M1_MOB_AMBIENT_B 8
#define M1_PATROL_A      MOB_ID(OT_ANIMDESTRUCTIBLE, 3)  // 0x010f0003
#define M1_PATROL_B      MOB_ID(OT_ANIMDESTRUCTIBLE, 4)  // 0x010f0004
#define M1_TOWER_FIRST   OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1d) // 0x000f001d..0x000f001f gun towers
#define M1_TOWER_LAST    OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1f)
#define M1_FUELDUMP      OBJ_ID(OT_FUELDUMP, 0x0f)       // 0x0011000f
#define M1_FUELDUMP_LIGHT OBJ_ID(OT_LIGHTOBJECT, 0x10)   // 0x00160010
#define M1_PICKUP_SHIELD OBJ_ID(OT_PICKUP, 0)            // 0x00130000
#define M1_PICKUP_CASH1  OBJ_ID(OT_PICKUP, 1)            // 0x00130001..0x00130004 small cash
#define M1_PICKUP_CASH4  OBJ_ID(OT_PICKUP, 4)

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  m1_nbWaypoints = 1;                // 0x10007160
static const Waypoint m1_waypoints[1] = {                // 0x10007168
    { 16640, 13440 },
};
static const TurretType m1_turretType2 = {               // 0x10007170 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 1, 0, 100, 0x10, 800, 12000, 0
};
static const char *m1_objectives[3][3];              // 0x10007190 [language][line], 0x180 per language
static const uint8_t m1_objectivePeriod = 3;             // 0x10007610 (compared as a byte)

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    Window  *objectiveWin;   // 0x10009d5c
    uint8_t  objectiveStep;  // 0x10009d60
    uint8_t  objectiveDown;  // 0x10009d64
    int32_t  startShield;    // 0x1000a1a0
} st;

static void M1_EvacTick(const LanderEvent *ev);

// 0x10001620  EV_TICK_1S: alternate the objective line every 3 s
static void M1_ObjectiveTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.objectiveStep == 0) {
        WindowSetText(st.objectiveWin, m1_objectives[gLanguage][0]);
        st.objectiveStep++;
        st.objectiveDown = 0;
        return;
    }
    if (st.objectiveStep == m1_objectivePeriod) {
        WindowSetText(st.objectiveWin, m1_objectives[gLanguage][1]);
        st.objectiveStep--;
        st.objectiveDown = 1;
        return;
    }
    if (st.objectiveDown == 0)
        st.objectiveStep++;
    else
        st.objectiveStep--;
}

// 0x10001750  EV_PICKUP {pickup, player}
static void M1_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == M1_PICKUP_SHIELD) {
        int32_t shield = (int32_t)Object_GetProperty(M1_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(M1_LANDER, 25);
        Object_SetProperty(M1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)M1_PICKUP_CASH1 && id <= (int32_t)M1_PICKUP_CASH4) {
        Lander_SetCash(M1_LANDER, Lander_GetCash(M1_LANDER) + gSmallCash);
        Voice_CashAcquired(M1_LANDER);
    }
}

// 0x100017f0  EV_DESTROYED: first gun tower destroyed -> pickups 2..4 appear
static void M1_OnTowerDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)M1_TOWER_FIRST && id <= (int32_t)M1_TOWER_LAST) {
        Object_SetAlive(OBJ_ID(OT_PICKUP, 2));
        Object_SetAlive(OBJ_ID(OT_PICKUP, 3));
        Object_SetAlive(OBJ_ID(OT_PICKUP, 4));
        EventType_RemoveHandler(EV_DESTROYED, M1_OnTowerDestroyed);
    }
}

// 0x10001840  EV_TICK_1S (after the satellite is hooked): above 10000 -> mission end
static void M1_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(M1_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        if ((int32_t)Object_GetProperty(M1_LANDER, PROP_HITPOINTS) == st.startShield) {
            MissionScore_End(2);                                       // 0x100013c0
            Lander_SetCash(M1_LANDER, Lander_GetCash(M1_LANDER) + 14000);
            EventType_RemoveHandler(EV_TICK_1S, M1_EvacTick);
            return;
        }
        Lander_SetCash(M1_LANDER, Lander_GetCash(M1_LANDER) + 14000);
        MissionScore_End(1);
        EventType_RemoveHandler(EV_TICK_1S, M1_EvacTick);
    }
}

// 0x10001900  EV_POD_HOOKED {pod, lander}; never removed (re-hooking adds another M1_EvacTick)
static void M1_OnPodHooked(const LanderEvent *ev)
{
    EventType_RemoveHandler(EV_TICK_1S, M1_ObjectiveTick);
    WindowSetText(st.objectiveWin, m1_objectives[gLanguage][2]);
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0x33, 0x33, 0xcc, 0x6d6);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0x19, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    EventType_AddHandler(EV_TICK_1S, NULL, M1_EvacTick);
}

// 0x10001990  EV_DESTROYED: fuel dump destroyed -> its light goes
static void M1_OnFuelDumpDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == M1_FUELDUMP) {
        Object_SetDead(M1_FUELDUMP_LIGHT);
        EventType_RemoveHandler(EV_DESTROYED, M1_OnFuelDumpDestroyed);
    }
}

// 0x100019c0  common set-up (all three start modes)
static void M1_Setup(void)
{
    for (int32_t id = OBJ_ID(OT_TURRET, 0x17); id <= (int32_t)OBJ_ID(OT_TURRET, 0x18); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
    for (int32_t id = (int32_t)M1_TOWER_FIRST; id <= (int32_t)M1_TOWER_LAST; id++) {
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x28);
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
    }
    Object_SetProperty(M1_PATROL_A, PROP_EXPLOSION, 0x46);
    Object_SetProperty(M1_PATROL_B, PROP_EXPLOSION, 0x46);
    Object_SetProperty(M1_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(M1_FUELDUMP, PROP_EXPLOSION, 0x50);
    SetGravity(10);
    gAirResistance = 0x1c;
    gMainThrottlePower = 0x19;
    gNbWaypoints = m1_nbWaypoints;
    gWaypoints = m1_waypoints;
    st.objectiveWin = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);  // 0x48
    WindowSetSize(st.objectiveWin, 20, 20, 300, 12);
    EventType_AddHandler(EV_TICK_1S, NULL, M1_ObjectiveTick);
    EventType_AddHandler(EV_POD_HOOKED, NULL, M1_OnPodHooked);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x100016c0
    EventType_AddHandler(EV_PICKUP, NULL, M1_OnPickup);
    EventType_AddHandler(EV_DESTROYED, NULL, M1_OnTowerDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, M1_OnFuelDumpDestroyed);
    MobObj_Wake(M1_MOB_POD);
    MobObj_Wake(M1_MOB_PATROL_A);
    MobObj_Wake(M1_MOB_PATROL_B);
    Object_SetDead(OBJ_ID(OT_PICKUP, 2));
    Object_SetDead(OBJ_ID(OT_PICKUP, 3));
    Object_SetDead(OBJ_ID(OT_PICKUP, 4));
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1d), 1, 0x81);
    Sound_PlayLoop(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1d), 0xd2, -1);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1e), 1, 0x81);
    Sound_PlayLoop(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1e), 0xd2, -1);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1f), 1, 0x81);
    Sound_PlayLoop(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x1f), 0xd2, -1);
    AnimPlay(M1_PATROL_A, 2, 0xe1);
    AnimPlay(M1_PATROL_B, 3, 0xe1);
    AnimSetUpdateInterval(M1_PATROL_A, 0x50);
    AnimSetUpdateInterval(M1_PATROL_B, 0x50);
    Sound_PlayLoop(M1_PATROL_A, 0x84, -1);
    Sound_PlayLoop(M1_PATROL_B, 0x84, -1);
    for (int32_t id = OBJ_ID(OT_PARTICLEEMITTER, 0x20); id <= (int32_t)OBJ_ID(OT_PARTICLEEMITTER, 0x25); id++)
        Sound_PlayLoop((ObjId)id, 0xff, -1);
    for (int32_t id = OBJ_ID(OT_MARKER, 0x11); id <= (int32_t)OBJ_ID(OT_MARKER, 0x12); id++)
        Sound_PlayLoop((ObjId)id, 0xd1, -1);
    for (int32_t id = OBJ_ID(OT_MARKER, 0x13); id <= (int32_t)OBJ_ID(OT_MARKER, 0x16); id++)
        Sound_PlayLoop((ObjId)id, 0xd1, -1);
    gTurretTypes[2] = &m1_turretType2;
    Media_AudioPlay(0);
    st.startShield = (int32_t)Object_GetProperty(M1_LANDER, PROP_HITPOINTS);
    MissionScore_Init(M1_LANDER, 30000, 5);                            // 0x10001540
}

// 0x10001ca0  LevelStartLevel_Lander (export 0x10001010)
static void M1_StartLander(void)
{
    MobObj_Wake(M1_MOB_LANDER);
    MobObj_Wake(M1_MOB_CAMERA);
    MobObj_Wake(M1_MOB_AMBIENT_A);
    MobObj_Wake(M1_MOB_AMBIENT_B);
    Object_SetProperty(M1_CAMERA, PROP_TARGET, M1_LANDER);
    M1_Setup();
}

// 0x10001ce0  LevelStartLevel_Animcam (export 0x10001020)
static void M1_StartAnimcam(void)
{
    M1_Setup();
}

// 0x10001cf0  LevelStartLevel_Flycam (export 0x10001030)
static void M1_StartFlycam(void)
{
    M1_Setup();
    MobObj_Wake(M1_MOB_FLYCAM);
}

// 0x10001060  LevelStopLevel (export 0x10001040): empty
static void M1_Stop(void)
{
}

// 0x10001050
static uint32_t M1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel: two calls of the empty 0x10001060, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t M1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m1_gametext[] = {
    { .kind = GT_STRINGS, .va = 0x10007190, .count = 3 * 3, .out = m1_objectives, .stride = 128 },
    { 0 },
};

const mission_script_t mission_mercury_1 = {
    .name          = "MERCURY\\1",
    .get_version   = M1_GetVersion,
    .load          = M1_Load,
    .start_lander  = M1_StartLander,
    .start_flycam  = M1_StartFlycam,
    .start_animcam = M1_StartAnimcam,
    .stop          = M1_Stop,
    .text          = m1_gametext,
};
