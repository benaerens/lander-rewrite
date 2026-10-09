// MISSIONS\TITAN\2\MISSIONR.DLL - faithful C port.
//
// Mission: find the reactor entrance (waypoint {19200, 6400}), work down through the reactor (lift
// 0x30007 with switches 0xa006d / 0xa006e) and hook the fuel rods (pod 0x10c0009): the plant wakes
// up - the platform on the inertial frame mob 0xe moves on trigger boxes 0x4002e..36 and shuttles
// between stations, guards, cranes, lights and gates come alive - the return objective, and above
// y = 10000 the mission ends with +320000 cash, result 1 (end movie CUT3 if Mimas 1 ended with
// result 1). Par time 300 s, 6 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10003ad0), LevelStartLevel_Flycam 0x10001020
// (-> 0x10003b20), LevelStopLevel 0x10001030 (-> the empty 0x10003b40), LevelGetVersion
// 0x10001040. The score library (0x10001050-0x100015f0) is in mission_lib.c; the destroyed-sound
// handler 0x100032b0 is MissionLib_OnDestroyedSound.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define T2_LANDER        MOB_ID(OT_LANDER, 0xa)          // 0x0101000a
#define T2_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define T2_SHUTTLE       MOB_ID(OT_INERTIALFRAME, 0xe)   // 0x0114000e
#define T2_LIFT          OBJ_ID(OT_ANIMOBJECT, 0x07)     // 0x00030007 fuel-rod lift
#define T2_LIFT_LIGHT    MOB_ID(OT_LIGHTOBJECT, 0x07)    // 0x01160007
#define T2_GATE_LIGHT    MOB_ID(OT_LIGHTOBJECT, 0x0b)    // 0x0116000b
#define T2_CRANE_D       MOB_ID(OT_ANIMOBJECT, 0x0d)     // 0x0103000d
#define T2_CRANE_F       MOB_ID(OT_ANIMOBJECT, 0x0f)     // 0x0103000f
#define T2_CRANE_2       MOB_ID(OT_ANIMOBJECT, 0x02)     // 0x01030002
#define T2_PRESS         OBJ_ID(OT_ANIMOBJECT, 0x09)     // 0x00030009
#define T2_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define T2_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define T2_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define T2_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))    // 0x000800nn
#define T2_GUARD(n)      OBJ_ID(OT_ANIMTURRET, (n))      // 0x000e00nn
#define T2_ANIMDESTR(n)  OBJ_ID(OT_ANIMDESTRUCTIBLE, (n))// 0x000f00nn
#define T2_FORCEBOX(n)   OBJ_ID(OT_FORCEBOX, (n))        // 0x001000nn
#define T2_FUELDUMP(n)   OBJ_ID(OT_FUELDUMP, (n))        // 0x001100nn
#define T2_LIGHT(n)      OBJ_ID(OT_LIGHTOBJECT, (n))     // 0x001600nn
#define T2_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define T2_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn
#define T2_MOB_LANDER    0xa
#define T2_MOB_CAMERA    1
#define T2_MOB_FLYCAM    8
#define T2_WORLD_MIMAS   12
#define T2_WORLD_TITAN   11

// ---- static data (.data) --------------------------------------------------------------------------
// 0x10008170: [language][4] string pointers
static const char *t2_text[3][4];
static const int32_t  t2_nbWaypoints = 1;                // 0x10008160
static const Waypoint t2_waypoints[1] = {                // 0x10008168
    { 19200, 6400 },
};
static const TurretType t2_turretType2 = {               // 0x100081a0 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 3, 5, 80, 0x20, 1024, 12000, 0
};
static const char t2_endMovie[] = "CUT3";                // 0x10008364

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    Window      *win;             // 0x1000aaac objective line
    int32_t      liftRunning;     // 0x1000aab0
    int32_t      liftSteps;       // 0x1000aab4 lift position (0x1b steps, at most 12)
    int32_t      liftReverse;     // 0x1000aab8 down switch pressed while running
    int32_t      liftDescending;  // 0x1000aabc
    int32_t      guardsReady;     // 0x1000aac0 set by any anim-finished after the guards rise
    int32_t      inGate;          // 0x1000aac4 written, never read
    int32_t      rodsHooked;      // 0x1000aac8
    int32_t      gateBlocks;      // 0x1000aacc destroyed of 0x8001e / 0x8001f
    int32_t      reactorBlocks;   // 0x1000aad0 destroyed of 0x80020 / 0x80021
    int32_t      savedCamAngle;   // 0x1000af14
    const char *const *text;      // 0x1000af10 = t2_text[gLanguage]
} st;

// 0x10001600  EV_TICK_1S (after the rods are hooked): evac above y = 10000
static void T2_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(T2_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    if (Briefing_GetExitCode(T2_WORLD_MIMAS, 0) == 1)
        SetLevelEndMovie(t2_endMovie);
    Lander_SetCash(T2_LANDER, Lander_GetCash(T2_LANDER) + 320000);
    MissionScore_End(1);                                 // 0x100013a0
    EventType_RemoveHandler(EV_TICK_1S, T2_EvacTick);
}

// 0x10001680  EV_TRIGGER_ENTER 0x40008: lift shaft camera (lag 0xe, user angle preset 2)
static void T2_OnTriggerShaftCamIn(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x08))
        return;
    AutoCam_SetLagY(T2_CAMERA, 0xe);
    st.savedCamAngle = AutoCam_GetUserAngle(T2_CAMERA);
    AutoCam_SetUserAngle(T2_CAMERA, 2);
}

// 0x100016d0  EV_TRIGGER_LEAVE 0x40008: camera back (the angle only if still preset 2)
static void T2_OnTriggerShaftCamOut(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x08))
        return;
    AutoCam_SetLagY(T2_CAMERA, 8);
    if (AutoCam_GetUserAngle(T2_CAMERA) == 2)
        AutoCam_SetUserAngle(T2_CAMERA, st.savedCamAngle);
}

// Door pair: a (with sound 0xa9) and b play `anim` (flags 0 open, 4 close). Inlined in the original.
static void T2_DoorPair(ObjId a, ObjId b, int32_t anim, int32_t flags)
{
    AnimPlay(a, anim, flags);
    Sound_PlayOnce(a, 0xa9);
    AnimPlay(b, anim, flags);
}

// 0x10001720  EV_SWITCH 0xa006b: doors 0x30071/0x30072 (state 1 open, else close)
static void T2_OnSwitchDoor71(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T2_SWITCH(0x6b))
        T2_DoorPair(T2_ANIM(0x71), T2_ANIM(0x72), 3, ev->arg[1] == 1 ? 0 : 4);
}

// 0x100017b0  EV_SWITCH 0xa006c: doors 0x30043/0x30044
static void T2_OnSwitchDoor43(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T2_SWITCH(0x6c))
        T2_DoorPair(T2_ANIM(0x43), T2_ANIM(0x44), 2, ev->arg[1] == 1 ? 0 : 4);
}

// 0x10001840  play forwards on EV_TRIGGER_ENTER, backwards on anything else
static void T2_AnimByEvent(int32_t type, ObjId obj, int32_t anim)
{
    if (type == EV_TRIGGER_ENTER)
        AnimPlay(obj, anim, 0);
    else
        AnimPlay(obj, anim, 4);
}

// 0x10001880  EV_TRIGGER_ENTER / LEAVE: doors 0x30037/38 (trigger 0x40039) and 0x3004d/4e
// (0x40052; that case also runs the shared tail that sets the 0x30037/38 frame time)
static void T2_OnTriggerDoorsByEvent(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T2_TRIGGER(0x39)) {
        T2_AnimByEvent(ev->type, T2_ANIM(0x37), 1);
        Sound_PlayOnce(T2_ANIM(0x37), 0x3f7);
        T2_AnimByEvent(ev->type, T2_ANIM(0x38), 1);
    } else if (ev->arg[1] == (intptr_t)T2_TRIGGER(0x52)) {
        T2_AnimByEvent(ev->type, T2_ANIM(0x4d), 2);
        Sound_PlayOnce(T2_ANIM(0x4d), 0x3f7);
        T2_AnimByEvent(ev->type, T2_ANIM(0x4e), 2);
    } else {
        return;
    }
    AnimSetUpdateInterval(T2_ANIM(0x37), 3);             // (shared tail 0x100018fd)
    AnimSetUpdateInterval(T2_ANIM(0x38), 3);
}

static void T2_OnGuardsAnyAnimDone(const LanderEvent *ev);

// 0x10001920  EV_DESTROYED: both gate blocks 0x8001e / 0x8001f gone -> the three guards rise
static void T2_OnDestroyedGateBlocks(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T2_DESTR(0x1e) || ev->arg[0] == (intptr_t)T2_DESTR(0x1f))
        st.gateBlocks++;
    if (st.gateBlocks != 2)
        return;
    AnimPlay(T2_GUARD(0x2b), 0xa, 0x60);
    AnimSetUpdateInterval(T2_GUARD(0x2b), 0x1e);
    AnimPlay(T2_GUARD(0x2c), 0xb, 0x60);
    Sound_PlayChain(T2_GUARD(0x2c), 20, 0x30e, 0x30f, 0x310, 0x31f, 0x30d, 0x30f, 0x30c, 0x30c, 0x310,
                    0x311, 0x31e, 0x30e, 0x30d, 0x30c, 0x311, 0x310, 0x30f, 0x30d, 0x30d, 0x310);
    AnimPlay(T2_GUARD(0x2d), 0xb, 0x60);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnGuardsAnyAnimDone);
    EventType_RemoveHandler(EV_DESTROYED, T2_OnDestroyedGateBlocks);
}

// Shuttle doors 0x3003d/0x30040 (anim a) and 0x3003e/0x3003f (anim b) after `delay` ms, frame time
// 15, sound 0x315 each (through InTime with delay 0). Inlined in the original.
static void T2_ShuttleDoors(int32_t delay, int32_t a, int32_t b)
{
    InTime3(delay, AnimPlay, T2_ANIM(0x3d), a, 0x40);
    AnimSetUpdateInterval(T2_ANIM(0x3d), 0xf);
    InTime2(0, Sound_PlayOnce, T2_ANIM(0x3d), 0x315);
    InTime3(delay, AnimPlay, T2_ANIM(0x40), a, 0x40);
    AnimSetUpdateInterval(T2_ANIM(0x40), 0xf);
    InTime2(0, Sound_PlayOnce, T2_ANIM(0x40), 0x315);
    InTime3(delay, AnimPlay, T2_ANIM(0x3e), b, 0x40);
    AnimSetUpdateInterval(T2_ANIM(0x3e), 0xf);
    InTime2(0, Sound_PlayOnce, T2_ANIM(0x3e), 0x315);
    InTime3(delay, AnimPlay, T2_ANIM(0x3f), b, 0x40);
    AnimSetUpdateInterval(T2_ANIM(0x3f), 0xf);
    InTime2(0, Sound_PlayOnce, T2_ANIM(0x3f), 0x315);
}

// 0x10001a30  EV_ANIM_FINISHED (anims 0x26 / 0x2c, any object): the shuttle shuttles - after anim
// 0x26 it waits, closes/opens its doors and leaves with anim 0x2c; after 0x2c, back with 0x26
static void T2_OnShuttleArrived(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x26) {
        AnimStop(T2_SHUTTLE, 0);
        InTime2(3500, Sound_PlayOnce, T2_SHUTTLE, 0xe7);
        InTime3(4000, AnimPlay, T2_SHUTTLE, 0x2c, 0x40);
        T2_ShuttleDoors(4500, 0x18, 0x19);
    }
    if (ev->arg[1] == 0x2c) {
        AnimStop(T2_SHUTTLE, 0);
        InTime2(5500, Sound_PlayOnce, T2_SHUTTLE, 0xe7);
        InTime3(6000, AnimPlay, T2_SHUTTLE, 0x26, 0x40);
        T2_ShuttleDoors(6500, 0x19, 0x18);
    }
}

// 0x10001ca0  EV_ANIM_FINISHED anim 0x2c (after trigger 0x40030): last leg, anim 0x2d
static void T2_OnShuttleLastLeg(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x2c)
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x2d, 0x40);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnShuttleLastLeg);
}

// 0x10001ce0  EV_TRIGGER_ENTER 0x40030: the shuttle stops shuttling
static void T2_OnTriggerShuttleStop(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x30))
        return;
    EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnShuttleArrived);
    AnimStop(T2_SHUTTLE, 1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnShuttleLastLeg);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTriggerShuttleStop);
}

// 0x10001d30  EV_ANIM_FINISHED anim 0x28 (never removed): shuttle anim 0x2c, doors open, the
// shuttling and the stop trigger start
static void T2_OnShuttleAnim28(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x28)
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x2c, 0x40);
    T2_ShuttleDoors(500, 0x18, 0x19);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnShuttleArrived);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerShuttleStop);
}

// 0x10001e80  EV_TRIGGER_ENTER 0x4002e: shuttle anim 0x28
static void T2_OnTrigger2e(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x2e))
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x28, 0x40);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnShuttleAnim28);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger2e);
}

// 0x10001ee0  EV_TRIGGER_ENTER 0x4002f: shuttle anim 0x27
static void T2_OnTrigger2f(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x2f))
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x27, 0x40);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger2f);
}

// 0x10001f30  EV_TRIGGER_ENTER 0x40032: shuttle anim 0x24
static void T2_OnTrigger32(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x32))
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x24, 0x40);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger32);
}

// 0x10001f80  EV_TRIGGER_ENTER 0x40033: shuttle anim 0x23
static void T2_OnTrigger33(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x33))
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x23, 0x40);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger33);
}

// 0x10001fd0  EV_ANIM_FINISHED of crane 0x103000f: both cranes stop
static void T2_OnCranesDone(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T2_CRANE_F)
        return;
    AnimStop(T2_CRANE_F, 0);
    AnimStop(T2_CRANE_2, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnCranesDone);
}

// 0x10002010  EV_TRIGGER_ENTER 0x40035: hatch 0x30005, shuttle anim 0x21, cranes, gate 0x3002a
static void T2_OnTrigger35(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x35))
        return;
    InTime3(500, AnimPlay, T2_ANIM(0x05), 0x1e, 0x80);
    InTime2(2500, Sound_PlayOnce, T2_ANIM(0x05), 0x312);
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x21, 0x40);
    AnimPlay(T2_CRANE_F, 0x12, 0);
    AnimPlay(T2_CRANE_2, 0x13, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnCranesDone);
    AnimPlay(T2_ANIM(0x2a), 0xc, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger35);
}

// 0x100020e0  EV_ANIM_FINISHED of 0x30049: fans 0x30049/0x3004a loop
static void T2_OnFansOpen(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T2_ANIM(0x49))
        return;
    AnimPlay(T2_ANIM(0x49), 5, 1);
    AnimSetUpdateInterval(T2_ANIM(0x49), 0xf);
    Sound_PlayLoop(T2_ANIM(0x49), 0x31c, -1);
    AnimPlay(T2_ANIM(0x4a), 5, 1);
    AnimSetUpdateInterval(T2_ANIM(0x4a), 0xf);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnFansOpen);
}

// 0x10002160  EV_ANIM_FINISHED of 0x30000: fans 0x30049/0x3004a unfold (anim 2)
static void T2_OnReactorCoverDone(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T2_ANIM(0x00))
        return;
    AnimPlay(T2_ANIM(0x49), 2, 0);
    AnimSetUpdateInterval(T2_ANIM(0x49), 3);
    Sound_PlayOnce(T2_ANIM(0x49), 0xa9);
    AnimPlay(T2_ANIM(0x4a), 2, 0);
    AnimSetUpdateInterval(T2_ANIM(0x4a), 3);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnFansOpen);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnReactorCoverDone);
}

// 0x100021f0  EV_DESTROYED: both blocks 0x80020 / 0x80021 gone -> reactor cover 0x30000 opens,
// emitters 0xd0067/68 and light 0x160029 on
static void T2_OnDestroyedReactorBlocks(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T2_DESTR(0x20) || ev->arg[0] == (intptr_t)T2_DESTR(0x21))
        st.reactorBlocks++;
    if (st.reactorBlocks != 2)
        return;
    InTime3(500, AnimPlay, T2_ANIM(0x00), 0x16, 0);
    Object_SetAlive(T2_EMITTER(0x67));
    Object_SetAlive(T2_EMITTER(0x68));
    Object_SetAlive(T2_LIGHT(0x29));
    Sound_PlayChain(T2_ANIM(0x00), 21, 0x31f, 0x31e, 0x311, 0x311, 0x31e, 0x31e, 0x31e, 0x31e, 0x311,
                    0x30f, 0x311, 0x31e, 0x31f, 0x31f, 0x31e, 0x311, 0x31e, 0x31a, 0x31e, 0x31f, 0x312);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnReactorCoverDone);
    EventType_RemoveHandler(EV_DESTROYED, T2_OnDestroyedReactorBlocks);
}

// 0x10002300  EV_TRIGGER_ENTER 0x40034: shuttle anim 0x22, the reactor hall machinery starts
static void T2_OnTrigger34(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x34))
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x22, 0x40);
    InTime2(2000, Sound_PlayOnce, T2_ANIMDESTR(0x0e), 0x313);
    InTime3(2500, Sound_PlayLoop, T2_ANIMDESTR(0x0e), 0x314, -1);
    InTime3(500, AnimPlay, T2_ANIM(0x5a), 0x11, 0);
    AnimSetUpdateInterval(T2_ANIM(0x5a), 8);
    InTime2(0, Sound_PlayOnce, T2_ANIM(0x5a), 0x319);
    InTime3(500, AnimPlay, T2_ANIM(0x58), 0xd, 0);
    InTime2(500, Sound_PlayOnce, T2_ANIM(0x58), 0x319);
    InTime3(500, AnimPlay, T2_ANIM(0x59), 0xd, 0);
    InTime3(500, AnimPlay, T2_ANIM(0x5b), 0xe, 0);
    InTime3(500, AnimPlay, T2_ANIM(0x5c), 0xf, 0);
    InTime3(500, AnimPlay, T2_ANIM(0x01), 0x10, 0);
    InTime3(500, AnimPlay, T2_CRANE_2, 0x15, 0);
    Sound_PlayChain(T2_CRANE_2, 21, 0x31f, 0x31e, 0x31f, 0x311, 0x31e, 0x310, 0x311, 0x31e, 0x31f, 0x30f,
                    0x311, 0x31e, 0x31f, 0x31f, 0x31e, 0x311, 0x310, 0x31e, 0x31e, 0x31f, 0x312);
    InTime3(500, AnimPlay, T2_CRANE_F, 0x14, 0);
    EventType_AddHandler(EV_DESTROYED, NULL, T2_OnDestroyedReactorBlocks);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger34);
}

// 0x10002510  EV_ANIM_FINISHED anim 0x29: shuttle anim 0x2b, crane 0x103000d anim 0x20
static void T2_OnCraneAnim29(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x29)
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x2b, 0x40);
    InTime3(200, AnimPlay, T2_CRANE_D, 0x20, 0);
    Sound_PlayChain(T2_CRANE_D, 9, 0x31f, 0x31f, 0x31e, 0x311, 0x314, 0x31a, 0x31e, 0x31f, 0x312);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnCraneAnim29);
}

// 0x100025b0  EV_TRIGGER_ENTER / LEAVE 0x40050: doors 0x30045/0x30046
static void T2_OnTriggerDoor45(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T2_TRIGGER(0x50))
        T2_DoorPair(T2_ANIM(0x45), T2_ANIM(0x46), 2, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
}

// 0x10002640  EV_TRIGGER_ENTER 0x40031: shuttle anim 0x25, crane 0x103000d anim 0x29; door
// 0x40050 stops opening (its leave handler stays)
static void T2_OnTrigger31(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x31))
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x25, 0x40);
    AnimSetUpdateInterval(T2_SHUTTLE, 6);
    InTime3(800, AnimPlay, T2_CRANE_D, 0x29, 0);
    AnimSetUpdateInterval(T2_CRANE_D, 6);
    Sound_PlayChain(T2_CRANE_D, 11, 0x31f, 0x31e, 0x31f, 0x31f, 0x31e, 0x311, 0x314, 0x31a, 0x31e, 0x31f,
                    0x312);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnCraneAnim29);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger31);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTriggerDoor45);
}

// 0x10002730  EV_TRIGGER_ENTER 0x40036: shuttle anim 0x28
static void T2_OnTrigger36(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x36))
        return;
    Sound_PlayOnce(T2_SHUTTLE, 0xe7);
    AnimPlay(T2_SHUTTLE, 0x28, 0x40);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTrigger36);
}

// 0x10002780  EV_TRIGGER_ENTER / LEAVE 0x4004f: doors 0x30041/0x30042
static void T2_OnTriggerDoor41(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T2_TRIGGER(0x4f))
        T2_DoorPair(T2_ANIM(0x41), T2_ANIM(0x42), 2, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
}

static void T2_OnTriggerDoor4b(const LanderEvent *ev);

// 0x10002810  EV_POD_HOOKED (any pod - the fuel rods are the only one): the return objective, the
// evac check, the plant wakes up
static void T2_OnPodHooked(const LanderEvent *ev)
{
    WindowSetText(st.win, st.text[2]);
    st.rodsHooked = 1;
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0x80, 0x80, 0x80, 1000);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    EventType_AddHandler(EV_TICK_1S, NULL, T2_EvacTick);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTriggerShaftCamIn);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, T2_OnTriggerShaftCamOut);
    Object_SetAlive(T2_DESTR(0x0a));
    Object_SetAlive(T2_DESTR(0x06));
    Object_SetDead(T2_FORCEBOX(0x12));
    Object_SetDead(T2_FORCEBOX(0x14));
    Object_SetProperty(T2_DESTR(0x24), PROP_HITPOINTS, 0);
    Object_SetProperty(T2_ANIMDESTR(0x0e), PROP_HITPOINTS, 0);
    AnimStop(T2_ANIMDESTR(0x0e), 1);
    Sound_StopObject(T2_ANIMDESTR(0x0e));
    AnimPlay(T2_ANIMDESTR(0x0e), 8, 0x81);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTriggerDoor4b);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, T2_OnTriggerDoor4b);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTriggerDoor41);   // (its leave handler stays)
    Object_SetAlive(T2_DESTR(0x1e));
    Object_SetAlive(T2_DESTR(0x1f));
    Object_SetAlive(T2_GUARD(0x2b));
    Object_SetAlive(T2_GUARD(0x2c));
    Object_SetAlive(T2_GUARD(0x2d));
    AnimPlay(T2_ANIM(0x23), 0x17, 4);
    AnimPlay(T2_GATE_LIGHT, 0x17, 0x44);
    for (int32_t n = 0x5f; n <= 0x66; n++)                // (unrolled)
        Object_SetAlive(T2_EMITTER(n));
    for (int32_t n = 0x25; n <= 0x28; n++)                // (unrolled)
        Object_SetAlive(T2_LIGHT(n));
    AnimPlay(T2_ANIM(0x0b), 9, 0);
    AnimPlay(T2_CRANE_D, 0x2a, 0);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger35);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger34);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger33);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger32);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger31);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger2f);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger2e);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger36);
    EventType_RemoveHandler(EV_POD_HOOKED, T2_OnPodHooked);
}

// 0x10002ad0  EV_SWITCH 0xa006f on: gate 0x30023 opens (light 0x116000b with it)
static void T2_OnSwitchGate(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T2_SWITCH(0x6f) || ev->arg[1] != 1)
        return;
    AnimPlay(T2_ANIM(0x23), 0x17, 0);
    AnimPlay(T2_GATE_LIGHT, 0x17, 0x40);
    Sound_PlayChain(T2_ANIM(0x23), 13, 0xcb, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                    0xcd);
    AnimSetUpdateInterval(T2_ANIM(0x23), 0x1b);
    AnimSetUpdateInterval(T2_GATE_LIGHT, 0x1b);
}

// 0x10002b80  EV_SWITCH (after the lift's last trip): lift switches 0xa006e / 0xa006d snap back off
static void T2_OnSwitchLiftDisabled(const LanderEvent *ev)
{
    if ((ev->arg[0] == (intptr_t)T2_SWITCH(0x6e) || ev->arg[0] == (intptr_t)T2_SWITCH(0x6d)) &&
        ev->arg[1] == 1)
        Object_SetProperty((ObjId)ev->arg[0], PROP_STATE, 0);
}

static void T2_OnSwitchLiftUp(const LanderEvent *ev);
static void T2_OnSwitchLiftDown(const LanderEvent *ev);

// 0x10002bb0  EV_ANIM_FINISHED of the lift 0x30007: step anims 0x1b (one step out, at most 12),
// 0x1c (one step back), 0x1d (turn round, requested by switch 0xa006e). After 12 steps out it
// comes all the way back; turned round at exactly step 6 it stops there for good (door 0x30055)
static void T2_OnLiftStep(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T2_LIFT)
        return;
    if (ev->arg[1] == 0x1b && st.liftSteps != 0xc) {
        AnimStop(T2_LIFT, 0);
        AnimStop(T2_LIFT_LIGHT, 0);
        st.liftSteps++;
        if (st.liftReverse == 1) {
            AnimPlay(T2_LIFT, 0x1d, 0);
            AnimPlay(T2_LIFT_LIGHT, 0x1d, 0x40);
            InTime2(0, Sound_PlayOnce, T2_LIFT, 0x317);
            st.liftReverse = 0;
        } else {
            AnimPlay(T2_LIFT, 0x1b, 0);
            AnimPlay(T2_LIFT_LIGHT, 0x1b, 0x40);
        }
    }
    if (ev->arg[1] == 0x1c) {                            // 0x10002c7d
        AnimStop(T2_LIFT, 0);
        AnimStop(T2_LIFT_LIGHT, 0);
        AnimPlay(T2_LIFT, 0x1c, 0);
        AnimPlay(T2_LIFT_LIGHT, 0x1c, 0x40);
        if (--st.liftSteps != 0)
            return;
        st.liftRunning = 0;
        st.liftDescending = 0;
        EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnLiftStep);
        Object_SetProperty(T2_SWITCH(0x6d), PROP_STATE, 0);
        Object_SetProperty(T2_SWITCH(0x6e), PROP_STATE, 0);
        st.liftReverse = 0;
        return;
    }
    if (st.liftSteps == 0xc || st.liftDescending == 1) { // 0x10002d17 (any other anim end)
        AnimPlay(T2_LIFT, 0x1c, 0);
        AnimPlay(T2_LIFT_LIGHT, 0x1c, 0x40);
        st.liftSteps--;
        st.liftDescending = 1;
    }
    if (ev->arg[1] != 0x1d || st.liftDescending != 0)
        return;
    AnimPlay(T2_LIFT, 0x1d, 4);
    AnimPlay(T2_LIFT_LIGHT, 0x1d, 0x44);
    InTime2(0, Sound_PlayOnce, T2_LIFT, 0x317);
    if (st.liftSteps == 6) {
        InTime2(500, Sound_PlayOnce, T2_ANIM(0x55), 0x318);
        AnimPlay(T2_ANIM(0x55), 0x1a, 0);
        EventType_RemoveHandler(EV_SWITCH, T2_OnSwitchLiftUp);
        EventType_RemoveHandler(EV_SWITCH, T2_OnSwitchLiftDown);
        EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnLiftStep);
        EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitchLiftDisabled);
    }
    st.liftDescending = 1;
}

// 0x10002e60  EV_SWITCH 0xa006d: start the lift (on, idle); the switch cannot be turned off; while
// running the other switch is reset
static void T2_OnSwitchLiftUp(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T2_SWITCH(0x6d))
        return;
    if (ev->arg[1] == 1 && st.liftRunning == 0) {
        st.liftRunning = 1;
        AnimPlay(T2_LIFT, 0x1b, 0);
        AnimPlay(T2_LIFT_LIGHT, 0x1b, 0x40);
        InTime2(0, Sound_PlayOnce, T2_LIFT, 0x31a);
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnLiftStep);
    }
    if (ev->arg[1] == 0)
        Object_SetProperty(T2_SWITCH(0x6d), PROP_STATE, 1);
    if (ev->arg[1] == 1 && st.liftRunning == 1)
        Object_SetProperty(T2_SWITCH(0x6e), PROP_STATE, 0);
}

// 0x10002f20  EV_SWITCH 0xa006e: while the lift runs, "on" requests the turn round; otherwise the
// switch is reset
static void T2_OnSwitchLiftDown(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T2_SWITCH(0x6e))
        return;
    intptr_t state = ev->arg[1];
    if (state == 1 && st.liftRunning == 0) {
        Object_SetProperty(T2_SWITCH(0x6e), PROP_STATE, 0);
        return;
    }
    if (state == 1 && st.liftRunning == 1) {
        st.liftReverse = 1;
        return;
    }
    if (state == 0 && st.liftRunning == 1)
        Object_SetProperty(T2_SWITCH(0x6e), PROP_STATE, 0);
}

// 0x10002f70  EV_TRIGGER_ENTER / LEAVE 0x40051: doors 0x3004b/0x3004c
static void T2_OnTriggerDoor4b(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T2_TRIGGER(0x51))
        T2_DoorPair(T2_ANIM(0x4b), T2_ANIM(0x4c), 2, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
}

// 0x10003000  EV_TRIGGER_ENTER / LEAVE 0x40052: doors 0x3004d/0x3004e
static void T2_OnTriggerDoor4d(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T2_TRIGGER(0x52))
        T2_DoorPair(T2_ANIM(0x4d), T2_ANIM(0x4e), 2, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
}

// 0x10003090  EV_TRIGGER_ENTER / LEAVE 0x40054: doors 0x30047/0x30048
static void T2_OnTriggerDoor47(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T2_TRIGGER(0x54))
        T2_DoorPair(T2_ANIM(0x47), T2_ANIM(0x48), 2, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
}

// 0x10003120  EV_TRIGGER_ENTER 0x40053: arms door 0x40054 (registered again on every entry)
static void T2_OnTrigger53(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x53))
        return;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerDoor47);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerDoor47);
}

// 0x10003160  EV_ANIM_FINISHED (any): the guards are up
static void T2_OnGuardsAnyAnimDone(const LanderEvent *ev)
{
    (void)ev;
    st.guardsReady = 1;
    EventType_RemoveHandler(EV_ANIM_FINISHED, T2_OnGuardsAnyAnimDone);
}

// 0x10003180  EV_ANIM_FINISHED anims 6 / 0x1f (any object): press 0x30009 cycles; the first down
// stroke after the guards are up crushes guards 0xe002c / 0xe002d
static void T2_OnPressCycle(const LanderEvent *ev)
{
    if (ev->arg[1] == 6) {
        AnimStop(T2_PRESS, 0);
        AnimPlay(T2_PRESS, 0x1f, 0);
        Sound_PlayChain(T2_PRESS, 4, 0xbc, 0xbc, 0xbc, 0xbc);
        AnimSetUpdateInterval(T2_PRESS, 0xd);
        if (st.guardsReady != 0) {
            Object_SetProperty(T2_GUARD(0x2c), PROP_HITPOINTS, 0);
            Object_SetProperty(T2_GUARD(0x2d), PROP_HITPOINTS, 0);
            st.guardsReady = 0;
        }
    }
    if (ev->arg[1] == 0x1f) {
        AnimStop(T2_PRESS, 0);
        AnimPlay(T2_PRESS, 6, 0);
        Sound_PlayChain(T2_PRESS, 3, 0xbc, 0xbc, 0xd8);
        AnimSetUpdateInterval(T2_PRESS, 6);
    }
}

// 0x10003280  EV_TRIGGER_ENTER 0x40022: interior lighting from now on
static void T2_OnTriggerInterior(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T2_TRIGGER(0x22))
        return;
    gInOutOverride = 1;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T2_OnTriggerInterior);
}

// 0x10003340  EV_TRIGGER_ENTER / LEAVE: objective line. Compares the *entering object* (arg0) with
// trigger ids, so the enter branch never matches; every trigger leave resets the line to entry 0
// (entry 3 once the rods are hooked).
static void T2_OnTriggerObjective(const LanderEvent *ev)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        if (ev->arg[0] == (intptr_t)T2_TRIGGER(0x70))
            WindowSetText(st.win, st.rodsHooked ? st.text[2] : st.text[1]);
        if (ev->arg[0] == (intptr_t)T2_TRIGGER(0x39))
            st.inGate = 1;
        return;
    }
    WindowSetText(st.win, st.rodsHooked ? st.text[3] : st.text[0]);
    if (ev->arg[0] == (intptr_t)T2_TRIGGER(0x39))
        st.inGate = 0;
}

// 0x100033d0  EV_PICKUP 0x130057: +50 % shield
static void T2_OnPickupShield(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T2_PICKUP(0x57)) {
        int32_t shield = (int32_t)Object_GetProperty(T2_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(T2_LANDER, 50);
        Object_SetProperty(T2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
}

// 0x10003440  EV_PICKUP: full shield 0x130056, cash 0x130002..04, missiles 0x130016
static void T2_OnPickup(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T2_PICKUP(0x56)) {
        Object_SetProperty(T2_LANDER, PROP_HITPOINTS, 0x100);
        Voice_ShieldAcquired(T2_LANDER, 100);
    }
    if (ev->arg[0] == (intptr_t)T2_PICKUP(0x03) || ev->arg[0] == (intptr_t)T2_PICKUP(0x02) ||
        ev->arg[0] == (intptr_t)T2_PICKUP(0x04)) {
        Lander_SetCash(T2_LANDER, Lander_GetCash(T2_LANDER) + gLargeCash);
        Voice_CashAcquired(T2_LANDER);
    }
    if (ev->arg[0] == (intptr_t)T2_PICKUP(0x16)) {
        if (Lander_AddMissiles(T2_LANDER, 4) == 0) {
            Voice_MissilesFull(T2_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(T2_LANDER);
    }
}

// 0x10003510  common set-up
static void T2_Setup(void)
{
    st.text = t2_text[gLanguage];
    for (int32_t id = T2_DESTR(0x1e); id <= (int32_t)T2_DESTR(0x21); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
    Object_SetProperty(T2_DESTR(0x3c), PROP_ARMOUR, 10);
    for (int32_t id = OBJ_ID(OT_TURRET, 0x1a); id <= (int32_t)OBJ_ID(OT_TURRET, 0x1d); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    for (int32_t id = T2_GUARD(0x2b); id <= (int32_t)T2_GUARD(0x2d); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    for (int32_t id = T2_FUELDUMP(0x17); id <= (int32_t)T2_FUELDUMP(0x19); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetDead(T2_DESTR(0x1e));
    Object_SetDead(T2_DESTR(0x1f));
    Object_SetDead(T2_GUARD(0x2b));
    Object_SetDead(T2_GUARD(0x2c));
    Object_SetDead(T2_GUARD(0x2d));
    Object_SetDead(T2_DESTR(0x0a));
    Object_SetDead(T2_DESTR(0x06));
    for (int32_t n = 0x41; n <= 0x4e; n++)                // (unrolled)
        AnimSetUpdateInterval(T2_ANIM(n), 7);
    gTurretTypes[2] = &t2_turretType2;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 250, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerInterior);
    EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitchDoor71);
    EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitchDoor43);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerDoorsByEvent);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerDoorsByEvent);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerDoor4b);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerDoor4b);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerDoor41);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerDoor41);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerDoor4d);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerDoor4d);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerDoor45);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerDoor45);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTrigger53);
    EventType_AddHandler(EV_POD_HOOKED, NULL, T2_OnPodHooked);
    EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitchGate);
    EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitchLiftUp);
    EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitchLiftDown);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T2_OnPressCycle);
    EventType_AddHandler(EV_DESTROYED, NULL, T2_OnDestroyedGateBlocks);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x100032b0
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerShaftCamIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerShaftCamOut);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T2_OnTriggerObjective);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T2_OnTriggerObjective);
    EventType_AddHandler(EV_PICKUP, NULL, T2_OnPickupShield);
    EventType_AddHandler(EV_PICKUP, NULL, T2_OnPickup);
    if (Briefing_GetExitCode(T2_WORLD_TITAN, 0) == 2) {  // Titan 1 with the bonus pod
        AnimPlay(T2_ANIM(0x73), 3, 0);
        AnimPlay(T2_ANIM(0x74), 3, 0);
    }
    for (int32_t n = 0x0c; n <= 0x0f; n++) {              // (unrolled)
        AnimPlay(T2_ANIMDESTR(n), 7, 0x81);
        AnimSetUpdateInterval(T2_ANIMDESTR(n), 2);
    }
    for (int32_t id = T2_ANIMDESTR(0x0c); id <= (int32_t)T2_ANIMDESTR(0x0f); id++)
        Sound_PlayLoop((ObjId)id, 0xb5, -1);
    AnimPlay(T2_ANIM(0x3a), 4, 0x81);
    AnimSetUpdateInterval(T2_ANIM(0x3a), 0x28);
    AnimPlay(T2_ANIM(0x3b), 4, 0x81);
    AnimSetUpdateInterval(T2_ANIM(0x3b), 0x14);
    for (int32_t id = T2_ANIM(0x3a); id <= (int32_t)T2_ANIM(0x3b); id++)
        Sound_PlayLoop((ObjId)id, 0xb3, -1);
    AnimPlay(T2_PRESS, 6, 0);
    MobObj_Wake(0xb);
    MobObj_Wake(7);
    MobObj_Wake(9);
    MobObj_Wake(2);
    MobObj_Wake(0xf);
    MobObj_Wake(0xd);
    MobObj_Wake(0xe);
    for (int32_t n = 0x5f; n <= 0x68; n++)                // (unrolled)
        Object_SetDead(T2_EMITTER(n));
    for (int32_t n = 0x25; n <= 0x29; n++)                // (unrolled)
        Object_SetDead(T2_LIGHT(n));
    SetGravity(13);
    gAirResistance = 0xc;
    gMainThrottlePower = 0x1a;
    gNbWaypoints = t2_nbWaypoints;
    gWaypoints = t2_waypoints;
    MissionScore_Init(T2_LANDER, 300000, 6);             // 0x10001520
    Media_AudioPlay(0);
}

// 0x10003ad0  LevelStartLevel_Lander (export 0x10001010)
static void T2_StartLander(void)
{
    MobObj_Wake(T2_MOB_LANDER);
    MobObj_Wake(T2_MOB_CAMERA);
    T2_Setup();
    MobObj_Wake(3);
    MobObj_Wake(4);
    MobObj_Wake(5);
    MobObj_Wake(6);
    Object_SetProperty(T2_CAMERA, PROP_TARGET, T2_LANDER);
}

// 0x10003b20  LevelStartLevel_Flycam (export 0x10001020)
static void T2_StartFlycam(void)
{
    T2_Setup();
    MobObj_Wake(T2_MOB_FLYCAM);
}

// 0x10003b40  LevelStopLevel (export 0x10001030): empty
static void T2_Stop(void)
{
}

// 0x10001040
static uint32_t T2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel: two calls of the empty 0x10003b40, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t T2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10008170, .count = 3 * 4, .out = t2_text },
    { 0 },
};

const mission_script_t mission_titan_2 = {
    .name          = "TITAN\\2",
    .get_version   = T2_GetVersion,
    .load          = T2_Load,
    .start_lander  = T2_StartLander,
    .start_flycam  = T2_StartFlycam,
    .start_animcam = NULL,
    .stop          = T2_Stop,
    .text          = t2_gametext,
};
