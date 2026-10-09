// MISSIONS\VENUS\1\MISSIONR.DLL - faithful C port.
//
// Mission: find the installation at waypoint 1, pick up the canister (pod mob 2) and climb above
// y = 10000 with it: +16000 cash, result 1 - or result 2 (the brief then unlocks Earth 3 instead
// of Earth 1) when the climb is done within the first 90 s of game time (the 1-s counter runs
// from the start). Destroying 0x80035 reveals a cash pickup and arms the hangar trigger 0x40002; turrets
// 0/1 and the fuel dump switch their lights off when destroyed. Par time 60 s, 1 pickup.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040 (no Animcam export). The score library
// (0x10001050-0x100015f4) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define V1_LANDER       MOB_ID(OT_LANDER, 0xa)        // 0x0101000a
#define V1_CAMERA       MOB_ID(OT_CAMERA, 1)          // 0x01060001
#define V1_MOB_CAMERA   1
#define V1_MOB_POD      2                             // 0x010c0002 the canister
#define V1_MOB_FLYCAM   9
#define V1_MOB_LANDER   0xa
#define V1_AMBIENT_C    MOB_ID(OT_AMBIENTFX, 0xc)     // 0x0115000c
#define V1_TURRET(n)    OBJ_ID(OT_TURRET, (n))        // 0x0000000n
#define V1_TARGET       OBJ_ID(OT_DESTRUCTIBLE, 0x35) // 0x00080035 reveals the cash
#define V1_FUELDUMP     OBJ_ID(OT_FUELDUMP, 6)        // 0x00110006
#define V1_PICKUP_CASH  OBJ_ID(OT_PICKUP, 5)          // 0x00130005 medium cash
#define V1_ANIM(n)      OBJ_ID(OT_ANIMOBJECT, (n))    // 0x000300nn
#define V1_TRIGGER(n)   OBJ_ID(OT_TRIGGERBOX, (n))    // 0x000400nn
#define V1_LIGHT(n)     OBJ_ID(OT_LIGHTOBJECT, (n))   // 0x001600nn

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  v1_nbWaypoints = 1;             // 0x10007160
static const Waypoint v1_waypoints[1] = {             // 0x10007168
    { -32000, -19200 },
};
static const TurretType v1_turretType0 = {            // 0x10007170 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 100, 0x10, 1024, 12000, 0
};
// 0x10007190: [language][3] string pointers (strings at 0x100071b4-0x100072fe)
static const char *v1_text[3][3];

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    int32_t      seconds;       // 0x10009a3c EV_TICK_1S count since the start
    uint8_t      podHooked;     // 0x10009a40
    Window      *win;           // 0x10009a44 objective line
    int32_t      shieldStart;   // 0x10009e80 (written, never read)
    const char *const *text;    // 0x10009e84 = v1_text[gLanguage]
} st;

// 0x10001600  EV_TRIGGER_ENTER / EV_TRIGGER_LEAVE, trigger 0x4002b: installation doors
static void V1_OnTriggerDoors(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != V1_TRIGGER(0x2b))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(V1_ANIM(0x2c), 2, 0);
        Sound_PlayOnce(V1_ANIM(0x2c), 0x140);
        AnimPlay(V1_ANIM(0x2d), 2, 0);
        if (!st.podHooked)
            WindowSetText(st.win, st.text[1]);   // the canister objective
    } else {
        AnimPlay(V1_ANIM(0x2c), 2, 4);
        Sound_PlayOnce(V1_ANIM(0x2c), 0x140);
        AnimPlay(V1_ANIM(0x2d), 2, 4);
    }
}

// 0x100016b0  EV_TRIGGER_ENTER trigger 0x40002 (armed by V1_OnTargetDestroyed): hangar 0x30007
static void V1_OnTriggerHangar(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != V1_TRIGGER(2))
        return;
    AnimPlay(V1_ANIM(7), 4, 0);
    Sound_PlayChain(V1_ANIM(7), 5, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, V1_OnTriggerHangar);
}

// 0x10001710  EV_DESTROYED 0x80035: arm the hangar trigger, reveal the cash (never removed)
static void V1_OnTargetDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V1_TARGET)
        return;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, V1_OnTriggerHangar);
    Object_SetAlive(V1_PICKUP_CASH);
}

// 0x10001740  EV_TICK_1S after the pod is hooked; never removed (LevelEnd stops the scheduler)
static void V1_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(V1_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        MissionScore_End(st.seconds > 90 ? 1 : 2);                     // 0x100013a0
        Lander_SetCash(V1_LANDER, Lander_GetCash(V1_LANDER) + 16000);
    }
}

// 0x100017a0  EV_POD_HOOKED {pod, lander}; never removed: every re-hook adds another EvacTick
static void V1_OnPodHooked(const LanderEvent *ev)
{
    WindowSetText(st.win, st.text[2]);       // the evacuation objective
    st.podHooked = 1;
    WindowSetText(st.win, st.text[2]);       // (set twice in the original)
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0xe6, 0xe6, 0xe6, 0x4e2);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0x73, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    EventType_AddHandler(EV_TICK_1S, NULL, V1_EvacTick);
}

// 0x10001840  EV_TICK_1S: mission clock in seconds
static void V1_CountSeconds(const LanderEvent *ev)
{
    (void)ev;
    st.seconds++;
}

// 0x100018e0  EV_PICKUP: medium cash
static void V1_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V1_PICKUP_CASH)
        return;
    Voice_CashAcquired(V1_LANDER);
    Lander_SetCash(V1_LANDER, Lander_GetCash(V1_LANDER) + gMediumCash);
}

// 0x10001930  EV_DESTROYED fuel dump 0x110006: its red light goes out
static void V1_OnFuelDumpDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V1_FUELDUMP)
        return;
    Object_SetDead(V1_LIGHT(0x12));
    EventType_RemoveHandler(EV_DESTROYED, V1_OnFuelDumpDestroyed);
}

// 0x10001960  EV_DESTROYED turret 0: its light goes out
static void V1_OnTurret0Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V1_TURRET(0))
        return;
    Object_SetDead(V1_LIGHT(0x0d));
    EventType_RemoveHandler(EV_DESTROYED, V1_OnTurret0Destroyed);
}

// 0x10001990  EV_DESTROYED turret 1: its four lights go out
static void V1_OnTurret1Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != V1_TURRET(1))
        return;
    Object_SetDead(V1_LIGHT(0x0e));
    Object_SetDead(V1_LIGHT(0x0f));
    Object_SetDead(V1_LIGHT(0x10));
    Object_SetDead(V1_LIGHT(0x11));
    EventType_RemoveHandler(EV_DESTROYED, V1_OnTurret1Destroyed);
}

// 0x100019e0  common set-up
static void V1_Setup(void)
{
    st.text = v1_text[gLanguage];
    SetBumpMapping(0x38, 0x4c, 0x4d);
    SetBumpMapping(0x09, 0x4e, 0x4f);
    SetBumpMapping(0x31, 0x50, 0x51);
    SetBumpMapping(0x06, 0x52, 0x53);
    SetBumpMapping(0x35, 0x54, 0x55);
    SetBumpMapping(0x33, 0x56, 0x57);
    MobObj_Wake(3);                 // AmbientFX
    MobObj_Wake(4);                 // AmbientFX
    MobObj_Wake(0xd);               // AmbientFX
    MobObj_Wake(0xc);               // AmbientFX
    Sound_PlayLoop(V1_AMBIENT_C, 0xdd, -1);
    for (int32_t id = 0; id <= 1; id++)                         // turrets 0..1
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
    Object_SetProperty(V1_TARGET, PROP_ARMOUR, 0xf);
    Object_SetProperty(V1_TARGET, PROP_EXPLOSION, 0x50);
    Object_SetProperty(V1_FUELDUMP, PROP_EXPLOSION, 0x50);
    Object_SetProperty(V1_FUELDUMP, PROP_ARMOUR, 10);
    gTurretTypes[0] = &v1_turretType0;
    SetGravity(0x12);
    gAirResistance = 0x32;
    gMainThrottlePower = 0x25;
    TexAnimPlay(1, 2, 100);
    gNbWaypoints = v1_nbWaypoints;
    gWaypoints = v1_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 250, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_DESTROYED, NULL, V1_OnTurret0Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, V1_OnTurret1Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, V1_OnFuelDumpDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10001850
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, V1_OnTriggerDoors);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, V1_OnTriggerDoors);
    EventType_AddHandler(EV_DESTROYED, NULL, V1_OnTargetDestroyed);
    EventType_AddHandler(EV_POD_HOOKED, NULL, V1_OnPodHooked);
    EventType_AddHandler(EV_TICK_1S, NULL, V1_CountSeconds);
    EventType_AddHandler(EV_PICKUP, NULL, V1_OnPickup);
    MobObj_Wake(V1_MOB_POD);
    Object_SetDead(V1_PICKUP_CASH);
    AnimPlay(V1_ANIM(0x30), 1, 2);
    AnimPlay(V1_ANIM(0x2e), 5, 0x81);
    Sound_PlayLoop(V1_ANIM(0x2e), 0xb9, -1);
    AnimPlay(V1_ANIM(0x2f), 3, 2);
    Sound_PlayLoop(V1_LIGHT(0x0c), 0xfa, -1);
    Media_AudioPlay(0);
    st.shieldStart = (int32_t)Object_GetProperty(V1_LANDER, PROP_HITPOINTS);
    MissionScore_Init(V1_LANDER, 60000, 1);                    // 0x10001520
}

// 0x10001ca0  LevelStartLevel_Lander (export 0x10001010)
static void V1_StartLander(void)
{
    MobObj_Wake(V1_MOB_LANDER);
    MobObj_Wake(V1_MOB_CAMERA);
    Object_SetProperty(V1_CAMERA, PROP_TARGET, V1_LANDER);
    V1_Setup();
}

// 0x10001ce0  LevelStartLevel_Flycam (export 0x10001020)
static void V1_StartFlycam(void)
{
    V1_Setup();
    MobObj_Wake(V1_MOB_FLYCAM);
}

// 0x10001d00  LevelStopLevel (export 0x10001030): empty
static void V1_Stop(void)
{
}

// 0x10001040
static uint32_t V1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001d00, return 0. Port: fresh DLL data.
static int32_t V1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText v1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10007190, .count = 3 * 3, .out = v1_text },
    { 0 },
};

const mission_script_t mission_venus_1 = {
    .name          = "VENUS\\1",
    .get_version   = V1_GetVersion,
    .load          = V1_Load,
    .start_lander  = V1_StartLander,
    .start_flycam  = V1_StartFlycam,
    .start_animcam = NULL,
    .stop          = V1_Stop,
    .text          = v1_gametext,
};
