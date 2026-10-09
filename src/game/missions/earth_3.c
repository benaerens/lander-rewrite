// MISSIONS\EARTH\3\MISSIONR.DLL - faithful C port.
//
// Mission: get into the Renaissance facility (waypoint 1), destroy one of the N.B.C. containers
// 0x80028..0x8003b to free the weapon (pod mob 8), hook it, get out and climb above y = 10000:
// +23000 cash, result 2 when done within 300 s of game time or with all six turrets 0x43..0x48
// destroyed, else result 1. The objective line starts at the retrieve objective (text 1, as in the
// original) and follows the entrance boxes 0x40026 / 0x40027. Par time 30 s, 5 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040 (no Animcam export). The score library
// (0x10001050-0x100015f4) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define E3_LANDER       MOB_ID(OT_LANDER, 5)          // 0x01010005
#define E3_CAMERA       MOB_ID(OT_CAMERA, 1)          // 0x01060001
#define E3_POD          MOB_ID(OT_POD, 8)             // 0x010c0008 the N.B.C. weapon
#define E3_MOB_CAMERA   1
#define E3_MOB_FLYCAM   4
#define E3_MOB_LANDER   5
#define E3_MOB_POD      8
#define E3_TURRET(n)    OBJ_ID(OT_TURRET, (n))        // 0x000000nn
#define E3_ANIM(n)      OBJ_ID(OT_ANIMOBJECT, (n))    // 0x000300nn
#define E3_TRIGGER(n)   OBJ_ID(OT_TRIGGERBOX, (n))    // 0x000400nn
#define E3_TARGET(n)    OBJ_ID(OT_DESTRUCTIBLE, (n))  // 0x000800nn
#define E3_FUELDUMP     OBJ_ID(OT_FUELDUMP, 0x41)     // 0x00110041
#define E3_PICKUP(n)    OBJ_ID(OT_PICKUP, (n))        // 0x001300nn

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  e3_nbWaypoints = 1;             // 0x10007160
static const Waypoint e3_waypoints[1] = {             // 0x10007168
    { 6400, 6400 },
};
// 0x10007170: [language][4] string pointers (strings at 0x100071dc-0x10007390)
static const char *e3_text[3][4];
static const TurretType e3_turretType22 = {           // 0x100071a0 -> gTurretTypes[22]
    { 2, 29, 288, 0 }, 0x18, 1, 3, 5, 100, 0x10, 800, 14000, 0
};
static const TurretType e3_turretType3 = {            // 0x100071c0 -> gTurretTypes[3]
    { 0, 0, 396, 0 }, 0x00, 0, 1, 0, 85, 0x10, 800, 12000, 0
};

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    Window      *win;           // 0x10009adc objective line
    int32_t      inside;        // 0x10009ae0 entered the box 0x40027
    int32_t      podHooked;     // 0x10009ae4
    int32_t      seconds;       // 0x10009ae8 EV_TICK_1S count since the start
    int32_t      turretsKilled; // 0x10009aec turrets 0x43..0x48 destroyed
    const char *const *text;    // 0x10009f28 = e3_text[gLanguage]
} st;

// 0x10001600  EV_DESTROYED one of the containers 0x80028..0x8003b: the weapon appears
static void E3_OnContainerDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)E3_TARGET(0x28) || id > (int32_t)E3_TARGET(0x3b))
        return;
    MobObj_Wake(E3_MOB_POD);
    Pod_Script_SetupHookPoint(E3_POD, 0, 0x80, 0);
    Pod_Script_SetupDynamicsCuboid(E3_POD, 0x180, 0x100, 0x180, 0x320);
    Pod_Script_SetupDrag(E3_POD, 0x1333);
    EventType_RemoveHandler(EV_DESTROYED, E3_OnContainerDestroyed);
}

// 0x10001680  EV_TRIGGER_ENTER / EV_TRIGGER_LEAVE, trigger 0x4004b: door 0x30024
static void E3_OnTriggerDoor24(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E3_TRIGGER(0x4b))
        return;
    AnimPlay(E3_ANIM(0x24), 2, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
    Sound_PlayOnce(E3_ANIM(0x24), 0xab);
}

// 0x100016c0  EV_TRIGGER_ENTER / EV_TRIGGER_LEAVE, trigger 0x4004c: door 0x30025
static void E3_OnTriggerDoor25(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E3_TRIGGER(0x4c))
        return;
    AnimPlay(E3_ANIM(0x25), 1, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
    Sound_PlayOnce(E3_ANIM(0x25), 0xab);
}

// 0x10001700  EV_TICK_1S after the weapon is hooked
static void E3_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(E3_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    MissionScore_End(st.seconds > 300 && st.turretsKilled < 6 ? 1 : 2);   // 0x100013a0
    Lander_SetCash(E3_LANDER, Lander_GetCash(E3_LANDER) + 23000);
    EventType_RemoveHandler(EV_TICK_1S, E3_EvacTick);
}

// 0x10001780  EV_DESTROYED: count the turrets 0x43..0x48
static void E3_OnTurretDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)E3_TURRET(0x43) && id <= (int32_t)E3_TURRET(0x48))
        st.turretsKilled++;
}

// 0x100017a0  EV_TICK_1S: mission clock in seconds
static void E3_CountSeconds(const LanderEvent *ev)
{
    (void)ev;
    st.seconds++;
}

// 0x100017b0  EV_POD_HOOKED {pod, lander}; never removed: every re-hook adds another EvacTick
static void E3_OnPodHooked(const LanderEvent *ev)
{
    (void)ev;
    EventType_AddHandler(EV_TICK_1S, NULL, E3_EvacTick);
    st.podHooked = 1;
    WindowSetText(st.win, st.text[2]);       // "Exit facility"
}

// 0x100017f0  EV_TRIGGER_ENTER / EV_TRIGGER_LEAVE, entrance boxes 0x40026 / 0x40027
static void E3_OnTriggerEntrance(const LanderEvent *ev)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        if ((ObjId)ev->arg[1] == E3_TRIGGER(0x26) && st.podHooked == 0)
            WindowSetText(st.win, st.text[1]);
        if ((ObjId)ev->arg[1] == E3_TRIGGER(0x27))
            st.inside = 1;
        return;
    }
    if ((ObjId)ev->arg[1] != E3_TRIGGER(0x26) || st.inside != 0)
        return;
    if (st.podHooked == 1)
        WindowSetText(st.win, st.text[3]);   // the evacuation objective
    else
        WindowSetText(st.win, st.text[0]);
}

// 0x10001920  EV_PICKUP: full shield, +25 % shield x 2, small cash x 2
static void E3_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == E3_PICKUP(0x00)) {
        int32_t shield = (int32_t)Object_GetProperty(E3_LANDER, PROP_HITPOINTS) + 0x100;
        Voice_ShieldAcquired(E3_LANDER, 100);
        Object_SetProperty(E3_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == E3_PICKUP(0x01)) {
        int32_t shield = (int32_t)Object_GetProperty(E3_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(E3_LANDER, 25);
        Object_SetProperty(E3_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == E3_PICKUP(0x02)) {
        int32_t shield = (int32_t)Object_GetProperty(E3_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(E3_LANDER, 25);
        Object_SetProperty(E3_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)E3_PICKUP(0x49) && id <= (int32_t)E3_PICKUP(0x4a)) {
        Lander_SetCash(E3_LANDER, Lander_GetCash(E3_LANDER) + gSmallCash);
        Voice_CashAcquired(E3_LANDER);
    }
}

// 0x10001a60  EV_DESTROYED: hidden pickups (never removed)
static void E3_OnRevealPickups(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == E3_TARGET(0x1c))
        Object_SetAlive(E3_PICKUP(0x00));
    if ((ObjId)ev->arg[0] == E3_TARGET(0x20))
        Object_SetAlive(E3_PICKUP(0x49));
    if ((ObjId)ev->arg[0] == E3_TARGET(0x23))
        Object_SetAlive(E3_PICKUP(0x4a));
}

// 0x10001ab0  common set-up
static void E3_Setup(void)
{
    st.text = e3_text[gLanguage];
    SetGravity(0x13);
    gAirResistance = 0x23;
    gMainThrottlePower = 0x25;
    gNbWaypoints = e3_nbWaypoints;
    gWaypoints = e3_waypoints;
    MissionScore_Init(E3_LANDER, 30000, 5);                    // 0x10001520 (before the handlers)
    Object_SetProperty(E3_TURRET(0x43), PROP_ARMOUR, 0x14);
    Object_SetProperty(E3_TURRET(0x48), PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)E3_TURRET(0x44); id <= (int32_t)E3_TURRET(0x47); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)E3_TARGET(0x06); id <= (int32_t)E3_TARGET(0x0e); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    for (int32_t id = (int32_t)E3_TARGET(0x18); id <= (int32_t)E3_TARGET(0x1a); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x78);
    }
    for (int32_t id = (int32_t)E3_TARGET(0x1b); id <= (int32_t)E3_TARGET(0x23); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = (int32_t)E3_TARGET(0x03); id <= (int32_t)E3_TARGET(0x05); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
    for (int32_t id = (int32_t)E3_TARGET(0x4d); id <= (int32_t)E3_TARGET(0x4f); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(E3_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(E3_FUELDUMP, PROP_EXPLOSION, 0x50);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 250, 12);
    WindowSetText(st.win, st.text[1]);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E3_OnTriggerDoor24);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E3_OnTriggerDoor24);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E3_OnTriggerDoor25);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E3_OnTriggerDoor25);
    EventType_AddHandler(EV_DESTROYED, NULL, E3_OnContainerDestroyed);
    EventType_AddHandler(EV_POD_HOOKED, NULL, E3_OnPodHooked);
    EventType_AddHandler(EV_DESTROYED, NULL, E3_OnTurretDestroyed);
    EventType_AddHandler(EV_TICK_1S, NULL, E3_CountSeconds);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E3_OnTriggerEntrance);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E3_OnTriggerEntrance);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10001890
    EventType_AddHandler(EV_PICKUP, NULL, E3_OnPickup);
    EventType_AddHandler(EV_DESTROYED, NULL, E3_OnRevealPickups);
    AnimPlay(E3_ANIM(0x3c), 3, 0x81);
    AnimPlay(E3_ANIM(0x3d), 3, 0x81);
    AnimPlay(E3_ANIM(0x3e), 3, 0x81);
    AnimPlay(E3_ANIM(0x3f), 3, 0x81);
    AnimPlay(E3_ANIM(0x40), 3, 0x81);
    gTurretTypes[22] = &e3_turretType22;
    gTurretTypes[3] = &e3_turretType3;
    Object_SetDead(E3_PICKUP(0x00));
    Object_SetDead(E3_PICKUP(0x49));
    Object_SetDead(E3_PICKUP(0x4a));
    Media_AudioPlay(0);
}

// 0x10001db0  LevelStartLevel_Lander (export 0x10001010)
static void E3_StartLander(void)
{
    MobObj_Wake(E3_MOB_LANDER);
    MobObj_Wake(E3_MOB_CAMERA);
    MobObj_Wake(6);                 // AmbientFX
    MobObj_Wake(7);                 // AmbientFX
    MobObj_Wake(2);                 // AmbientFX
    MobObj_Wake(3);                 // AmbientFX
    E3_Setup();
    Lander_SetHeadLight(E3_LANDER, 0x1ff, 0xc00);
    Object_SetProperty(E3_CAMERA, PROP_TARGET, E3_LANDER);
}

// 0x10001e20  LevelStartLevel_Flycam (export 0x10001020)
static void E3_StartFlycam(void)
{
    E3_Setup();
    MobObj_Wake(E3_MOB_FLYCAM);
}

// 0x10001e40  LevelStopLevel (export 0x10001030): empty
static void E3_Stop(void)
{
}

// 0x10001040
static uint32_t E3_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001e40, return 0. Port: fresh DLL data.
static int32_t E3_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e3_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10007170, .count = 3 * 4, .out = e3_text },
    { 0 },
};

const mission_script_t mission_earth_3 = {
    .name          = "EARTH\\3",
    .get_version   = E3_GetVersion,
    .load          = E3_Load,
    .start_lander  = E3_StartLander,
    .start_flycam  = E3_StartFlycam,
    .start_animcam = NULL,
    .stop          = E3_Stop,
    .text          = e3_gametext,
};
