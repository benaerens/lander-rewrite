// MISSIONS\VENUS\2\MISSIONR.DLL - faithful C port.
//
// Mission: find the installation at waypoint 1; the trigger 0x40001 wakes the engine (pod mob 3).
// Hook it and climb above y = 10000: +18000 cash and result 2 if the engine has not touched
// anything since 3 s after it was first hooked, else result 1 ("Collided" replaces the objective
// line at the first impact). Destroying 0x80010 / 0x80013 / 0x80027 reveals medium cash / small
// cash / a 25 % shield pickup. Par time 75 s, 3 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040 (no Animcam export). The score library
// (0x10001050-0x100015f4) is in mission_lib.c. The disc also has MISSION.DLL / BRIEFING.DLL
// (another build); the game loads MISSIONR.DLL, which this file ports.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define V2_LANDER       MOB_ID(OT_LANDER, 5)          // 0x01010005
#define V2_CAMERA       MOB_ID(OT_CAMERA, 1)          // 0x01060001
#define V2_POD          MOB_ID(OT_POD, 3)             // 0x010c0003 the engine
#define V2_MOB_CAMERA   1
#define V2_MOB_POD      3
#define V2_MOB_FLYCAM   4
#define V2_MOB_LANDER   5
#define V2_ANIM(n)      OBJ_ID(OT_ANIMOBJECT, (n))    // 0x000300nn
#define V2_TRIGGER(n)   OBJ_ID(OT_TRIGGERBOX, (n))    // 0x000400nn
#define V2_TARGET(n)    OBJ_ID(OT_DESTRUCTIBLE, (n))  // 0x000800nn
#define V2_EMITTER(n)   OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn
#define V2_FUELDUMP     OBJ_ID(OT_FUELDUMP, 0x22)     // 0x00110022
#define V2_PICKUP_SHIELD OBJ_ID(OT_PICKUP, 0x00)      // 0x00130000 +25 % shield
#define V2_PICKUP_MEDIUM OBJ_ID(OT_PICKUP, 0x26)      // 0x00130026 medium cash
#define V2_PICKUP_SMALL  OBJ_ID(OT_PICKUP, 0x2c)      // 0x0013002c small cash

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  v2_nbWaypoints = 1;             // 0x10007160
static const Waypoint v2_waypoints[1] = {             // 0x10007168
    { -5120, -7680 },
};
static const TurretType v2_turretType0 = {            // 0x10007170 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 100, 0x10, 1024, 12000, 0
};
static const TurretType v2_turretType1 = {            // 0x10007190 -> gTurretTypes[1]
    { 0, 0, 145, 0 }, 0x01, 0, 1, 0, 80, 0x10, 1024, 12000, 0
};
// 0x100071b0: [language][3] string pointers (strings at 0x100071d4-0x10007317)
static const char *v2_text[3][3];
static const char *v2_collided;                       // 0x10007318 (not translated)

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    int32_t      collided;      // 0x10009a6c the engine hit something
    Window      *win;           // 0x10009a70 objective line
    const char *const *text;    // 0x10009eb0 = v2_text[gLanguage]
} st;

// 0x10001600  EV_TICK_1S after the engine is hooked
static void V2_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(V2_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    MissionScore_End(st.collided == 0 ? 2 : 1);                  // 0x100013a0
    Lander_SetCash(V2_LANDER, Lander_GetCash(V2_LANDER) + 18000);
    EventType_RemoveHandler(EV_TICK_1S, V2_EvacTick);
}

// 0x10001670  EV_TRIGGER_ENTER trigger 0x40033: the engine objective
static void V2_OnTriggerInside(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != V2_TRIGGER(0x33))
        return;
    WindowSetText(st.win, st.text[1]);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, V2_OnTriggerInside);
}

// 0x100016b0  EV_TICK_100MS: restart the three fans' sound loops (every 100 ms, as the original)
static void V2_FanSounds(const LanderEvent *ev)
{
    (void)ev;
    Sound_StopObject(V2_ANIM(0x18));
    Sound_PlayLoop(V2_ANIM(0x18), 0xb9, -1);
    Sound_StopObject(V2_ANIM(0x19));
    Sound_PlayLoop(V2_ANIM(0x19), 0xb9, -1);
    Sound_StopObject(V2_ANIM(0x1a));
    Sound_PlayLoop(V2_ANIM(0x1a), 0xb9, -1);
}

// 0x10001a50  EV_COLLISION (added 3 s after the hook): the engine hit something
static void V2_OnPodCollision(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V2_POD)
        return;
    st.collided = 1;
    WindowSetText(st.win, v2_collided);
    EventType_RemoveHandler(EV_COLLISION, V2_OnPodCollision);
}

// 0x10001720  EV_POD_HOOKED {pod, lander} (added by V2_OnTriggerWakePod, never removed: every
// re-hook adds another EvacTick and, 3 s later, another collision handler)
static void V2_OnPodHooked(const LanderEvent *ev)
{
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0xc0, 0xc0, 0x1c0, 0x4e2);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0x60, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    WindowSetText(st.win, st.text[2]);       // the evacuation objective
    EventType_AddHandler(EV_TICK_1S, NULL, V2_EvacTick);
    InTime3(3000, EventType_AddHandler, EV_COLLISION, NULL, V2_OnPodCollision);
}

// 0x100017c0  EV_TRIGGER_ENTER trigger 0x40001: wake the engine, listen for the hook
static void V2_OnTriggerWakePod(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != V2_TRIGGER(1))
        return;
    MobObj_Wake(V2_MOB_POD);
    EventType_AddHandler(EV_POD_HOOKED, NULL, V2_OnPodHooked);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, V2_OnTriggerWakePod);
}

// 0x10001800  EV_TRIGGER_ENTER / EV_TRIGGER_LEAVE, trigger 0x40016: door 0x30017
static void V2_OnTriggerDoor(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != V2_TRIGGER(0x16))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(V2_ANIM(0x17), 0x154);
        AnimPlay(V2_ANIM(0x17), 3, 0);
    } else {
        Sound_PlayOnce(V2_ANIM(0x17), 0x154);
        AnimPlay(V2_ANIM(0x17), 3, 4);
    }
}

// 0x100018f0  EV_PICKUP: +25 % shield, small cash, medium cash
static void V2_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == V2_PICKUP_SHIELD) {
        int32_t shield = (int32_t)Object_GetProperty(V2_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(V2_LANDER, 25);
        Object_SetProperty(V2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == V2_PICKUP_SMALL) {
        Lander_SetCash(V2_LANDER, Lander_GetCash(V2_LANDER) + gSmallCash);
        Voice_CashAcquired(V2_LANDER);
    }
    if ((ObjId)ev->arg[0] == V2_PICKUP_MEDIUM) {
        Lander_SetCash(V2_LANDER, Lander_GetCash(V2_LANDER) + gMediumCash);
        Voice_CashAcquired(V2_LANDER);
    }
}

// 0x100019c0  EV_DESTROYED 0x80027: reveal the shield pickup
static void V2_OnTarget27Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V2_TARGET(0x27))
        return;
    Object_SetAlive(V2_PICKUP_SHIELD);
    EventType_RemoveHandler(EV_DESTROYED, V2_OnTarget27Destroyed);
}

// 0x100019f0  EV_DESTROYED 0x80010: reveal the medium cash
static void V2_OnTarget10Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V2_TARGET(0x10))
        return;
    Object_SetAlive(V2_PICKUP_MEDIUM);
    EventType_RemoveHandler(EV_DESTROYED, V2_OnTarget10Destroyed);
}

// 0x10001a20  EV_DESTROYED 0x80013: reveal the small cash
static void V2_OnTarget13Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V2_TARGET(0x13))
        return;
    Object_SetAlive(V2_PICKUP_SMALL);
    EventType_RemoveHandler(EV_DESTROYED, V2_OnTarget13Destroyed);
}

// 0x10001a90  common set-up
static void V2_Setup(void)
{
    st.text = v2_text[gLanguage];
    for (int32_t id = 0x2a; id <= 0x2b; id++) {                 // turrets 0x2a..0x2b
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = 2; id <= 3; id++)                         // turrets 2..3
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
    Object_SetProperty(V2_TARGET(0x10), PROP_ARMOUR, 0x19);
    Object_SetProperty(V2_TARGET(0x10), PROP_EXPLOSION, 0x50);
    Object_SetProperty(V2_TARGET(0x13), PROP_ARMOUR, 0x19);
    Object_SetProperty(V2_TARGET(0x13), PROP_EXPLOSION, 0x50);
    Object_SetProperty(V2_TARGET(0x27), PROP_ARMOUR, 0x19);
    Object_SetProperty(V2_TARGET(0x27), PROP_EXPLOSION, 0x50);
    Object_SetProperty(V2_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(V2_FUELDUMP, PROP_EXPLOSION, 0x50);
    TexAnimPlay(1, 2, 200);
    SetBumpMapping(9, 0x4e, 0x4f);
    SetBumpMapping(6, 0x52, 0x53);
    MobObj_Wake(6);                 // AmbientFX
    MobObj_Wake(7);                 // AmbientFX
    MobObj_Wake(8);                 // AmbientFX
    MobObj_Wake(9);                 // AmbientFX
    Environment_OverrideInteriorFog(0, 0, 0);
    gTurretTypes[0] = &v2_turretType0;
    gTurretTypes[1] = &v2_turretType1;
    SetGravity(0x12);
    gAirResistance = 0x32;
    gMainThrottlePower = 0x25;
    Object_SetDead(V2_PICKUP_MEDIUM);
    Object_SetDead(V2_PICKUP_SMALL);
    Object_SetDead(V2_PICKUP_SHIELD);
    gNbWaypoints = v2_nbWaypoints;
    gWaypoints = v2_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 250, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_DESTROYED, NULL, V2_OnTarget10Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, V2_OnTarget13Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, V2_OnTarget27Destroyed);
    EventType_AddHandler(EV_PICKUP, NULL, V2_OnPickup);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10001860
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, V2_OnTriggerDoor);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, V2_OnTriggerDoor);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, V2_OnTriggerWakePod);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, V2_OnTriggerInside);
    EventType_AddHandler(EV_TICK_100MS, NULL, V2_FanSounds);
    AnimPlay(V2_ANIM(0x04), 1, 2);
    AnimPlay(V2_ANIM(0x06), 2, 2);
    AnimPlay(V2_ANIM(0x07), 1, 2);
    AnimPlay(V2_ANIM(0x08), 2, 2);
    AnimPlay(V2_ANIM(0x09), 1, 2);
    AnimPlay(V2_ANIM(0x0a), 2, 2);
    AnimPlay(V2_ANIM(0x0b), 1, 2);
    AnimPlay(V2_ANIM(0x0c), 2, 2);
    AnimPlay(V2_ANIM(0x0d), 1, 2);
    AnimPlay(V2_ANIM(0x05), 2, 2);
    AnimPlay(V2_ANIM(0x18), 4, 0x81);
    AnimPlay(V2_ANIM(0x19), 4, 0x81);
    AnimPlay(V2_ANIM(0x1a), 4, 0x81);
    Sound_PlayLoop(V2_EMITTER(0x1c), 0xf1, -1);
    Sound_PlayLoop(V2_EMITTER(0x1d), 0xf0, -1);
    Sound_PlayLoop(V2_EMITTER(0x2d), 0xff, -1);
    Sound_PlayLoop(V2_EMITTER(0x2e), 0xff, -1);
    Sound_PlayLoop(V2_EMITTER(0x2f), 0xff, -1);
    MissionScore_Init(V2_LANDER, 75000, 3);                    // 0x10001520
    Media_AudioPlay(0);
}

// 0x10001e40  LevelStartLevel_Lander (export 0x10001010)
static void V2_StartLander(void)
{
    MobObj_Wake(V2_MOB_LANDER);
    MobObj_Wake(V2_MOB_CAMERA);
    Object_SetProperty(V2_CAMERA, PROP_TARGET, V2_LANDER);
    V2_Setup();
}

// 0x10001e80  LevelStartLevel_Flycam (export 0x10001020)
static void V2_StartFlycam(void)
{
    V2_Setup();
    MobObj_Wake(V2_MOB_FLYCAM);
}

// 0x10001ea0  LevelStopLevel (export 0x10001030): empty
static void V2_Stop(void)
{
}

// 0x10001040
static uint32_t V2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001ea0, return 0. Port: fresh DLL data.
static int32_t V2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText v2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x100071b0, .count = 3 * 3, .out = v2_text },
    { .kind = GT_STRINGS, .va = 0x10007318, .count = 1, .out = &v2_collided },
    { 0 },
};

const mission_script_t mission_venus_2 = {
    .name          = "VENUS\\2",
    .get_version   = V2_GetVersion,
    .load          = V2_Load,
    .start_lander  = V2_StartLander,
    .start_flycam  = V2_StartFlycam,
    .start_animcam = NULL,
    .stop          = V2_Stop,
    .text          = v2_gametext,
};
