// MISSIONS\EARTH\1\MISSIONR.DLL - faithful C port.
//
// Mission: find the hydro-electric facility in the canyon (waypoint 1), enter it through the
// trigger 0x40000 (which wakes the cache, pod mob 2), hook the cache and climb above y = 10000:
// result 1 and +20000 cash. The trigger plane 0x1a0010 and the Celestia airspace boxes
// 0x40007/0x40008 switch the objective line; the boxes 0x4000b/0x4000c tighten the camera's
// vertical lag. Five AnimObject mobs (0x1030007..b) are started one after another and every
// ping-pong turn of any animation re-plays the turning object with the next anim of a 20-entry
// cycle. Par time 180 s, 4 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040 (no Animcam export). The score library
// (0x10001050-0x100015f4) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define E1_LANDER       MOB_ID(OT_LANDER, 4)          // 0x01010004
#define E1_CAMERA       MOB_ID(OT_CAMERA, 1)          // 0x01060001
#define E1_MOB_CAMERA   1
#define E1_MOB_POD      2                             // 0x010c0002 the cache
#define E1_MOB_FLYCAM   3
#define E1_MOB_LANDER   4
#define E1_ANIMMOB(n)   MOB_ID(OT_ANIMOBJECT, (n))    // 0x010300nn
#define E1_TRIGGER(n)   OBJ_ID(OT_TRIGGERBOX, (n))    // 0x000400nn
#define E1_TARGET(n)    OBJ_ID(OT_DESTRUCTIBLE, (n))  // 0x000800nn
#define E1_LIGHT(n)     OBJ_ID(OT_LIGHTOBJECT, (n))   // 0x001600nn
#define E1_MARKER(n)    OBJ_ID(OT_MARKER, (n))        // 0x001900nn
#define E1_FUELDUMP     OBJ_ID(OT_FUELDUMP, 0x11)     // 0x00110011
#define E1_PLANE        OBJ_ID(OT_TRIGGERPLANE, 0x10) // 0x001a0010
#define E1_PICKUP_SHIELD MOB_ID(OT_PICKUP, 0x0c)      // 0x0113000c +50 % shield
#define E1_PICKUP_CASH_FIRST OBJ_ID(OT_PICKUP, 0x0d)  // 0x0013000d..f small cash
#define E1_PICKUP_CASH_LAST  OBJ_ID(OT_PICKUP, 0x0f)

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  e1_nbWaypoints = 1;             // 0x10007160
static const Waypoint e1_waypoints[1] = {             // 0x10007168
    { -6400, 6400 },
};
// 0x10007170: [language][5] string pointers (strings at 0x100071e4-0x1000745f)
static const char *e1_text[3][5];
static const TurretType e1_turretType0 = {            // 0x100071b0 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 80, 0x18, 1024, 12000, 0
};
static const int8_t e1_animCycle[20] = {              // 0x100071d0: anim index - 1
    0, 3, 2, 4, 1, 0, 3, 1, 4, 2, 3, 1, 4, 1, 0, 4, 2, 3, 0, 1
};

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    Window      *win;           // 0x10009b9c objective line
    int32_t      podHooked;     // 0x10009ba0
    int8_t       cycle;         // 0x10009ba4 index into e1_animCycle
    const char *const *text;    // 0x10009fe0 = e1_text[gLanguage]
} st;

// 0x10001600  EV_TICK_1S after the cache is hooked; never removed (LevelEnd stops the scheduler)
static void E1_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(E1_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        MissionScore_End(1);                                          // 0x100013a0
        Lander_SetCash(E1_LANDER, Lander_GetCash(E1_LANDER) + 20000);
    }
}

// 0x10001650  EV_ANIM_TURNED {object, anim}: any object (no filter) gets the next anim of the cycle
static void E1_OnAnimTurned(const LanderEvent *ev)
{
    AnimStop((ObjId)ev->arg[0], 1);
    AnimPlay((ObjId)ev->arg[0], e1_animCycle[st.cycle] + 1, 0);
    if (st.cycle == 0x13)
        st.cycle = 0;
    else
        st.cycle++;
}

// 0x100016b0  EV_POD_HOOKED {pod, lander} (added by E1_OnTriggerFacility, never removed)
static void E1_OnPodHooked(const LanderEvent *ev)
{
    WindowSetText(st.win, st.text[2]);       // the evacuation objective
    st.podHooked = 1;
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0xe6, 0xe6, 0xe6, 0x578);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0x73, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    EventType_AddHandler(EV_TICK_1S, NULL, E1_EvacTick);
}

// 0x10001740  EV_TRIGGER_ENTER trigger 0x40000: wake the cache, listen for the hook
static void E1_OnTriggerFacility(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0))
        return;
    MobObj_Wake(E1_MOB_POD);
    EventType_AddHandler(EV_POD_HOOKED, NULL, E1_OnPodHooked);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E1_OnTriggerFacility);
}

// 0x10001780  EV_TRIGGER_ENTER trigger 0x4000b / 0x4000c: camera lag 16
static void E1_OnTriggerLagEnter(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[1];
    if (id == E1_TRIGGER(0x0b) || id == E1_TRIGGER(0x0c))
        AutoCam_SetLagY(E1_CAMERA, 0x10);
}

// 0x100017b0  EV_TRIGGER_LEAVE trigger 0x4000b..0x4000c: camera lag 8
static void E1_OnTriggerLagLeave(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[1];
    if (id >= (int32_t)E1_TRIGGER(0x0b) && id <= (int32_t)E1_TRIGGER(0x0c))
        AutoCam_SetLagY(E1_CAMERA, 8);
}

// 0x100017e0  EV_DESTROYED 0x8001c / 0x8001d: extra sound 0xf8
static void E1_OnDestroyedDam(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id == E1_TARGET(0x1c))
        Sound_PlayOnce(id, 0xf8);
    else if (id == E1_TARGET(0x1d))
        Sound_PlayOnce(id, 0xf8);
}

// 0x10001820  EV_DESTROYED 0x80020..0x80029: extra sound 0x100 (tested 20, 22..29, 21)
static void E1_OnDestroyedPylon(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    switch (id) {
    case E1_TARGET(0x20): case E1_TARGET(0x22): case E1_TARGET(0x23): case E1_TARGET(0x24):
    case E1_TARGET(0x25): case E1_TARGET(0x26): case E1_TARGET(0x27): case E1_TARGET(0x28):
    case E1_TARGET(0x29): case E1_TARGET(0x21):
        Sound_PlayOnce(id, 0x100);
        break;
    default:
        break;
    }
}

// 0x10001910  EV_TRIGGER_ENTER / EV_TRIGGER_LEAVE, Celestia airspace 0x40007 / 0x40008
static void E1_OnTriggerAirspace(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[1];
    if (id != E1_TRIGGER(7) && id != E1_TRIGGER(8))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        WindowSetText(st.win, st.text[4]);   // the airspace warning
    else if (st.podHooked == 0)
        WindowSetText(st.win, st.text[0]);
    else
        WindowSetText(st.win, st.text[2]);
}

// 0x10001a10  EV_PICKUP: +50 % shield, small cash x 3
static void E1_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == E1_PICKUP_SHIELD) {
        int32_t shield = (int32_t)Object_GetProperty(E1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(E1_LANDER, 50);
        Object_SetProperty(E1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)E1_PICKUP_CASH_FIRST && id <= (int32_t)E1_PICKUP_CASH_LAST) {
        Voice_CashAcquired(E1_LANDER);
        Lander_SetCash(E1_LANDER, Lander_GetCash(E1_LANDER) + gSmallCash);
    }
}

// 0x10001ab0  EV_TRIGGER_PLANE {player, plane, side}, plane 0x1a0010, until the cache is hooked
static void E1_OnTriggerPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_PLANE || st.podHooked != 0)
        return;
    if (ev->arg[2] == 0)
        WindowSetText(st.win, st.text[1]);   // the cache objective
    else
        WindowSetText(st.win, st.text[0]);
}

// 0x10001b00  common set-up
static void E1_Setup(void)
{
    st.text = e1_text[gLanguage];
    SetBumpMapping(0x1a, 0xa3, 0xa4);
    TexAnimPlay(0xb2, 2, 100);
    for (int32_t id = (int32_t)E1_TARGET(0x20); id <= (int32_t)E1_TARGET(0x29); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x19);
    for (int32_t id = 1; id <= 6; id++)                         // turrets 1..6
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    Object_SetProperty(E1_TARGET(0x1e), PROP_ARMOUR, 0x1e);
    Object_SetProperty(E1_TARGET(0x1e), PROP_EXPLOSION, 0x50);
    Object_SetProperty(E1_TARGET(0x1f), PROP_ARMOUR, 0x1e);
    Object_SetProperty(E1_TARGET(0x1f), PROP_EXPLOSION, 0x50);
    Object_SetProperty(E1_TARGET(0x1c), PROP_ARMOUR, 0x1e);
    Object_SetProperty(E1_TARGET(0x1c), PROP_EXPLOSION, 0x50);
    Object_SetProperty(E1_TARGET(0x1d), PROP_ARMOUR, 0x1e);
    Object_SetProperty(E1_TARGET(0x1d), PROP_EXPLOSION, 0x50);
    Object_SetProperty(E1_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(E1_FUELDUMP, PROP_EXPLOSION, 0x50);
    SetGravity(0x13);
    gAirResistance = 0x23;
    gMainThrottlePower = 0x25;
    gNbWaypoints = e1_nbWaypoints;
    gWaypoints = e1_waypoints;
    MissionScore_Init(E1_LANDER, 180000, 4);                   // 0x10001520 (before the handlers)
    gTurretTypes[0] = &e1_turretType0;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 400, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, E1_OnAnimTurned);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerFacility);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerLagEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerLagLeave);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerAirspace);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerAirspace);
    EventType_AddHandler(EV_DESTROYED, NULL, E1_OnDestroyedDam);
    EventType_AddHandler(EV_DESTROYED, NULL, E1_OnDestroyedPylon);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10001980
    EventType_AddHandler(EV_PICKUP, NULL, E1_OnPickup);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, E1_OnTriggerPlane);
    MobObj_Wake(7);                 // AnimObject 0x01030007
    MobObj_Wake(8);                 // AnimObject 0x01030008
    MobObj_Wake(9);                 // AnimObject 0x01030009
    MobObj_Wake(0xa);               // AnimObject 0x0103000a
    MobObj_Wake(0xb);               // AnimObject 0x0103000b
    MobObj_Wake(0xc);               // Pickup 0x0113000c
    MobObj_Wake(6);                 // AmbientFX
    MobObj_Wake(5);                 // AmbientFX
    MobObj_Wake(0xd);               // AmbientFX
    Sound_PlayLoop(OBJ_NONE, 0x136, -1);                        // 2D ambience
    InTime3(0, AnimPlay, E1_ANIMMOB(7), 5, 0);
    InTime3(700, AnimPlay, E1_ANIMMOB(8), 4, 0);
    InTime3(2000, AnimPlay, E1_ANIMMOB(9), 3, 0);
    InTime3(2500, AnimPlay, E1_ANIMMOB(0xa), 2, 0);
    InTime3(3100, AnimPlay, E1_ANIMMOB(0xb), 1, 0);
    Sound_PlayLoop(E1_LIGHT(0x18), 0xfa, -1);
    Sound_PlayLoop(E1_LIGHT(0x1a), 0xfa, -1);
    Sound_PlayLoop(E1_TRIGGER(0x2d), 0x103, -1);
    Sound_PlayLoop(E1_TARGET(0x1e), 0xe3, -1);
    for (int32_t id = (int32_t)E1_MARKER(0x2a); id <= (int32_t)E1_MARKER(0x2c); id++)
        Sound_PlayLoop((ObjId)id, 0xe3, -1);
    Media_AudioPlay(0);
}

// 0x10001e80  LevelStartLevel_Lander (export 0x10001010)
static void E1_StartLander(void)
{
    MobObj_Wake(E1_MOB_LANDER);
    MobObj_Wake(E1_MOB_CAMERA);
    Lander_SetHeadLight(E1_LANDER, 0x31c, 0x999);
    E1_Setup();
    Object_SetProperty(E1_CAMERA, PROP_TARGET, E1_LANDER);
}

// 0x10001ed0  LevelStartLevel_Flycam (export 0x10001020)
static void E1_StartFlycam(void)
{
    E1_Setup();
    MobObj_Wake(E1_MOB_FLYCAM);
}

// 0x10001ef0  LevelStopLevel (export 0x10001030): empty
static void E1_Stop(void)
{
}

// 0x10001040
static uint32_t E1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001ef0, return 0. Port: fresh DLL data.
static int32_t E1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10007170, .count = 3 * 5, .out = e1_text },
    { 0 },
};

const mission_script_t mission_earth_1 = {
    .name          = "EARTH\\1",
    .get_version   = E1_GetVersion,
    .load          = E1_Load,
    .start_lander  = E1_StartLander,
    .start_flycam  = E1_StartFlycam,
    .start_animcam = NULL,
    .stop          = E1_Stop,
    .text          = e1_gametext,
};
