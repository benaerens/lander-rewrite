// MISSIONS\TITAN\1\MISSIONR.DLL - faithful C port.
//
// Mission: find the Phoenix III plant (waypoint 1), get into it (entrance trigger 0x400ad: blast
// doors and lights cycle, mob 2 - the loose reactor core pod - wakes), hook the loose core (pod
// 0x10c0002: the reactor-room door 0x30000 stops following the lander) and drop it into its socket
// (trigger box 0x400ac targets the pod): the pod is replaced by the anim object 0x3007b, whose anim
// 0x17 starts the reactor (lights, rotor 0x30095 in five run-up stages with force boxes in the
// shaft, ~60 s), after which the reactor door opens, the exit iris doors 0x40051/0x40056 work and
// the lander must climb above y = 10000: +300000 cash, result 2 if the bonus pod 0x10c0003 (behind
// the destructibles 0x80004/0x800b7) was hooked, else 1. Par time 300 s, 8 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x100049f0), LevelStartLevel_Flycam 0x10001020
// (-> 0x10004a50), LevelStopLevel 0x10001030 (-> the empty 0x10001050), LevelGetVersion
// 0x10001040. The score library (0x10001060-0x10001600) is in mission_lib.c; the destroyed-sound
// handler 0x10004290 is MissionLib_OnDestroyedSound.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define T1_LANDER        MOB_ID(OT_LANDER, 7)            // 0x01010007
#define T1_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define T1_CORE_POD      MOB_ID(OT_POD, 2)               // 0x010c0002 the loose reactor core
#define T1_BONUS_POD     MOB_ID(OT_POD, 3)               // 0x010c0003
#define T1_LIFT_9        MOB_ID(OT_INERTIALFRAME, 9)     // 0x01140009
#define T1_LIFT_A        MOB_ID(OT_INERTIALFRAME, 0xa)   // 0x0114000a
#define T1_ROTOR         OBJ_ID(OT_ANIMOBJECT, 0x95)     // 0x00030095 reactor rotor
#define T1_CORE_ANIM     OBJ_ID(OT_ANIMOBJECT, 0x7b)     // 0x0003007b the core in its socket
#define T1_REACTOR_DOOR  OBJ_ID(OT_ANIMOBJECT, 0x00)     // 0x00030000
#define T1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define T1_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define T1_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define T1_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))    // 0x000800nn
#define T1_FORCEBOX(n)   OBJ_ID(OT_FORCEBOX, (n))        // 0x001000nn
#define T1_LIGHT(n)      OBJ_ID(OT_LIGHTOBJECT, (n))     // 0x001600nn
#define T1_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define T1_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn
#define T1_MOB_LANDER    7
#define T1_MOB_CAMERA    1
#define T1_MOB_FLYCAM    6

// ---- static data (.data) --------------------------------------------------------------------------
// 0x10009160: [language][6] string pointers
static const char *t1_text[3][6];
static const int32_t  t1_nbWaypoints = 1;                // 0x100091a8
static const Waypoint t1_waypoints[1] = {                // 0x100091b0
    { -5760, 3840 },
};

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    Window      *win;             // 0x1000bbbc objective line
    int32_t      bonusHooked;     // 0x1000bbc0 bonus pod hooked -> result 2
    int32_t      spinTicks;       // 0x1000bbc4 rotor run-up seconds
    int32_t      coreReplaced;    // 0x1000bbc8
    int32_t      reactorOnline;   // 0x1000bbcc enables the exit iris doors 0x40051 / 0x40056
    const char *const *text;      // 0x1000c008 = t1_text[gLanguage]
} st;

// Four-leaf iris door: leaf a now, b/c/d 200/400/600 ms later, anim 0x18 (flags 0 open, 4 close),
// each leaf with sound 0xaa. Inlined by the compiler at every site in the original.
static void T1_IrisDoor(ObjId a, ObjId b, ObjId c, ObjId d, int32_t flags)
{
    AnimPlay(a, 0x18, flags);
    InTime3(200, AnimPlay, b, 0x18, flags);
    InTime3(400, AnimPlay, c, 0x18, flags);
    InTime3(600, AnimPlay, d, 0x18, flags);
    Sound_PlayOnce(a, 0xaa);
    InTime2(200, Sound_PlayOnce, b, 0xaa);
    InTime2(400, Sound_PlayOnce, c, 0xaa);
    InTime2(600, Sound_PlayOnce, d, 0xaa);
}

// 0x10001610  EV_ANIM_FINISHED: anim 0x17 (the core settling into its socket) -> barrier 0x800b8 gone
static void T1_OnCoreSettledBarrier(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x17) {
        Object_SetDead(T1_DESTR(0xb8));
        EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnCoreSettledBarrier);
    }
}

// 0x10001640  EV_TRIGGER_ENTER 0x40001: reactor-room door opens
static void T1_OnTriggerReactorDoorIn(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T1_TRIGGER(0x01)) {
        AnimPlay(T1_REACTOR_DOOR, 4, 0);
        Sound_PlayOnce(T1_REACTOR_DOOR, 0xa9);
    }
}

// 0x10001680  EV_TRIGGER_LEAVE 0x40001: reactor-room door closes
static void T1_OnTriggerReactorDoorOut(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T1_TRIGGER(0x01)) {
        AnimPlay(T1_REACTOR_DOOR, 4, 4);
        Sound_PlayOnce(T1_REACTOR_DOOR, 0xa9);
    }
}

// Shared body of 0x100016c0 (enter, flags 0 = open) and 0x10001c40 (leave, flags 4 = close):
// the proximity iris doors; 0x40051 / 0x40056 only once the reactor is online.
static void T1_IrisTrigger(intptr_t trigger, int32_t flags)
{
    if (trigger == (intptr_t)T1_TRIGGER(0x25)) {
        T1_IrisDoor(T1_ANIM(0x24), T1_ANIM(0x26), T1_ANIM(0x27), T1_ANIM(0x28), flags);
        return;
    }
    if (trigger == (intptr_t)T1_TRIGGER(0x2a)) {
        T1_IrisDoor(T1_ANIM(0x29), T1_ANIM(0x2b), T1_ANIM(0x2c), T1_ANIM(0x2d), flags);
        return;
    }
    if (trigger == (intptr_t)T1_TRIGGER(0x3b)) {
        T1_IrisDoor(T1_ANIM(0x3a), T1_ANIM(0x3c), T1_ANIM(0x3d), T1_ANIM(0x3e), flags);
        return;
    }
    if (trigger == (intptr_t)T1_TRIGGER(0x4c)) {
        T1_IrisDoor(T1_ANIM(0x4b), T1_ANIM(0x4d), T1_ANIM(0x4e), T1_ANIM(0x4f), flags);
        return;
    }
    if (trigger == (intptr_t)T1_TRIGGER(0x51) && st.reactorOnline == 1) {
        T1_IrisDoor(T1_ANIM(0x50), T1_ANIM(0x52), T1_ANIM(0x53), T1_ANIM(0x54), flags);
        return;
    }
    if (trigger == (intptr_t)T1_TRIGGER(0x56) && st.reactorOnline == 1) {
        T1_IrisDoor(T1_ANIM(0x55), T1_ANIM(0x57), T1_ANIM(0x58), T1_ANIM(0x59), flags);
        return;
    }
    if (trigger == (intptr_t)T1_TRIGGER(0x5b))
        T1_IrisDoor(T1_ANIM(0x5a), T1_ANIM(0x5c), T1_ANIM(0x5d), T1_ANIM(0x5e), flags);
}

// 0x100016c0  EV_TRIGGER_ENTER: iris doors open
static void T1_OnTriggerIrisIn(const LanderEvent *ev)
{
    T1_IrisTrigger(ev->arg[1], 0);
}

// 0x10001c40  EV_TRIGGER_LEAVE: iris doors close
static void T1_OnTriggerIrisOut(const LanderEvent *ev)
{
    T1_IrisTrigger(ev->arg[1], 4);
}

// 0x100021c0  EV_TRIGGER_ENTER 0x400ad: plant entrance - five rows of blast-door leaves move in
// turn, then the loose core pod (mob 2) wakes
static void T1_OnTriggerEntrance(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0xad))
        return;
    InTime3(200, AnimPlay, T1_ANIM(0xe9), 7, 0);
    InTime3(200, AnimPlay, T1_ANIM(0xe2), 7, 0);
    InTime3(200, AnimPlay, T1_ANIM(0x05), 0x21, 0);
    InTime3(200, AnimPlay, T1_ANIM(0x07), 0x21, 0);
    Sound_PlayOnce(T1_ANIM(0xe9), 0x2d1);
    InTime3(500, AnimPlay, T1_ANIM(0xe3), 9, 0);
    InTime3(500, AnimPlay, T1_ANIM(0xe4), 9, 0);
    InTime3(500, AnimPlay, T1_ANIM(0x08), 0x1e, 0);
    InTime3(500, AnimPlay, T1_ANIM(0x09), 0x1e, 0);
    Sound_PlayOnce(T1_ANIM(0xe3), 0x2d1);
    InTime3(1000, AnimPlay, T1_ANIM(0xe5), 0xa, 0);
    InTime3(1000, AnimPlay, T1_ANIM(0xe0), 0xa, 0);
    InTime3(1000, AnimPlay, T1_ANIM(0x0b), 0x20, 0);
    InTime3(1000, AnimPlay, T1_ANIM(0x0c), 0x20, 0);
    Sound_PlayOnce(T1_ANIM(0xe5), 0x2d1);
    InTime3(1200, AnimPlay, T1_ANIM(0xe7), 8, 0);
    InTime3(1200, AnimPlay, T1_ANIM(0xe8), 8, 0);
    InTime3(1200, AnimPlay, T1_ANIM(0x0e), 0x1d, 0);
    InTime3(1200, AnimPlay, T1_ANIM(0x0a), 0x1d, 0);
    Sound_PlayOnce(T1_ANIM(0xe7), 0x2d1);
    InTime3(1500, AnimPlay, T1_ANIM(0xe6), 0xb, 0);
    InTime3(1500, AnimPlay, T1_ANIM(0xe1), 0xb, 0);
    InTime3(1500, AnimPlay, T1_ANIM(0x06), 0x1f, 0);
    InTime3(1500, AnimPlay, T1_ANIM(0x0d), 0x1f, 0);
    Sound_PlayOnce(T1_ANIM(0xe6), 0x2d1);
    AnimSetUpdateInterval(T1_ANIM(0xe9), 8);
    AnimSetUpdateInterval(T1_ANIM(0xe2), 8);
    AnimSetUpdateInterval(T1_ANIM(0x05), 8);
    AnimSetUpdateInterval(T1_ANIM(0x07), 8);
    AnimSetUpdateInterval(T1_ANIM(0xe3), 10);
    AnimSetUpdateInterval(T1_ANIM(0xe4), 10);
    AnimSetUpdateInterval(T1_ANIM(0x08), 10);
    AnimSetUpdateInterval(T1_ANIM(0x09), 10);
    AnimSetUpdateInterval(T1_ANIM(0xe5), 0xf);
    AnimSetUpdateInterval(T1_ANIM(0xe0), 0xf);
    AnimSetUpdateInterval(T1_ANIM(0x0b), 0xf);
    AnimSetUpdateInterval(T1_ANIM(0x0c), 0xf);
    AnimSetUpdateInterval(T1_ANIM(0xe7), 0x14);
    AnimSetUpdateInterval(T1_ANIM(0xe8), 0x14);
    AnimSetUpdateInterval(T1_ANIM(0x0a), 0x14);
    AnimSetUpdateInterval(T1_ANIM(0x0e), 0x14);
    AnimSetUpdateInterval(T1_ANIM(0xe6), 0x19);
    AnimSetUpdateInterval(T1_ANIM(0xe1), 0x19);
    AnimSetUpdateInterval(T1_ANIM(0x0d), 0x19);
    AnimSetUpdateInterval(T1_ANIM(0x06), 0x19);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerEntrance);
    MobObj_Wake(2);
}

// 0x10002500  EV_SWITCH 0xa00d4: shaft fans 0x3007c/0x3007d stop and run anim 0xf, force box
// 0x100080 off. The event's new state is overwritten with 1 (later handlers see 1).
static void T1_OnSwitchFans(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_SWITCH(0xd4))
        return;
    ((LanderEvent *)ev)->arg[1] = 1;
    Sound_StopObject(T1_ANIM(0x7c));
    Sound_StopObject(T1_ANIM(0x7d));
    AnimStop(T1_ANIM(0x7c), 1);
    AnimStop(T1_ANIM(0x7d), 1);
    AnimPlay(T1_ANIM(0x7c), 0xf, 0);
    AnimPlay(T1_ANIM(0x7d), 0xf, 0);
    Object_SetDead(T1_FORCEBOX(0x80));
    EventType_RemoveHandler(EV_SWITCH, T1_OnSwitchFans);
}

// 0x100025a0  EV_SWITCH 0xa00d8 / 0xa00d9: hatches 0x30020 (anim 5) / 0x30021 (anim 6), state 1
// opens, any other state closes
static void T1_OnSwitchHatches(const LanderEvent *ev)
{
    intptr_t state = ev->arg[1];
    intptr_t id = ev->arg[0];
    if (state == 1) {
        if (id == (intptr_t)T1_SWITCH(0xd8)) {
            AnimPlay(T1_ANIM(0x20), 5, 0);
            Sound_PlayOnce(T1_ANIM(0x20), 0xc1);
        } else if (id == (intptr_t)T1_SWITCH(0xd9)) {
            AnimPlay(T1_ANIM(0x21), 6, 0);
            Sound_PlayOnce(T1_ANIM(0x21), 0xc1);
        }
    } else {
        if (id == (intptr_t)T1_SWITCH(0xd8)) {
            AnimPlay(T1_ANIM(0x20), 5, 4);
            Sound_PlayOnce(T1_ANIM(0x20), 0xc1);
        } else if (id == (intptr_t)T1_SWITCH(0xd9)) {
            AnimPlay(T1_ANIM(0x21), 6, 4);
            Sound_PlayOnce(T1_ANIM(0x21), 0xc1);
        }
    }
}

// 0x10002620  EV_SWITCH 0xa00da / 0xa00db: iris door 0x3002e..0x30031 (state 1 open, else close)
static void T1_OnSwitchIris2e(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_SWITCH(0xda) || ev->arg[0] == (intptr_t)T1_SWITCH(0xdb))
        T1_IrisDoor(T1_ANIM(0x2e), T1_ANIM(0x2f), T1_ANIM(0x30), T1_ANIM(0x31), ev->arg[1] == 1 ? 0 : 4);
}

// 0x10002750  EV_SWITCH 0xa00dc / 0xa00dd: iris door 0x30036..0x30039
static void T1_OnSwitchIris36(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_SWITCH(0xdc) || ev->arg[0] == (intptr_t)T1_SWITCH(0xdd))
        T1_IrisDoor(T1_ANIM(0x36), T1_ANIM(0x37), T1_ANIM(0x38), T1_ANIM(0x39), ev->arg[1] == 1 ? 0 : 4);
}

// 0x10002880  EV_SWITCH 0xa00de / 0xa00df: iris door 0x30032..0x30035
static void T1_OnSwitchIris32(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_SWITCH(0xde) || ev->arg[0] == (intptr_t)T1_SWITCH(0xdf))
        T1_IrisDoor(T1_ANIM(0x32), T1_ANIM(0x33), T1_ANIM(0x34), T1_ANIM(0x35), ev->arg[1] == 1 ? 0 : 4);
}

// 0x100029b0  EV_SWITCH 0xa00d5: iris door 0x3003f..0x30042
static void T1_OnSwitchIris3f(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_SWITCH(0xd5))
        T1_IrisDoor(T1_ANIM(0x3f), T1_ANIM(0x40), T1_ANIM(0x41), T1_ANIM(0x42), ev->arg[1] == 1 ? 0 : 4);
}

// 0x10002ad0  EV_SWITCH 0xa00d7 on: iris door 0x30043..0x30046 opens for good
static void T1_OnSwitchIris43(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_SWITCH(0xd7) && ev->arg[1] == 1) {
        T1_IrisDoor(T1_ANIM(0x43), T1_ANIM(0x44), T1_ANIM(0x45), T1_ANIM(0x46), 0);
        EventType_RemoveHandler(EV_SWITCH, T1_OnSwitchIris43);
    }
}

// 0x10002bc0  EV_SWITCH 0xa00d6 on: iris door 0x30047..0x3004a opens for good
static void T1_OnSwitchIris47(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_SWITCH(0xd6) && ev->arg[1] == 1) {
        T1_IrisDoor(T1_ANIM(0x47), T1_ANIM(0x48), T1_ANIM(0x49), T1_ANIM(0x4a), 0);
        EventType_RemoveHandler(EV_SWITCH, T1_OnSwitchIris47);
    }
}

// 0x10002cb0  EV_DESTROYED 0x80004: the armoured cover 0x800b7 goes too and the bonus pod wakes
static void T1_OnDestroyed(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_DESTR(0x04)) {
        Object_SetProperty(T1_DESTR(0xb7), PROP_HITPOINTS, 0);
        MobObj_Wake(3);
    }
}

// 0x10002ce0  EV_POD_HOOKED core pod: heavy pod set-up, the replace objective, the reactor
// door stops reacting to the lander
static void T1_OnPodHookedCore(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_CORE_POD)
        return;
    Pod_Script_SetupHookPoint(T1_CORE_POD, 0, 0x122, 0);
    Pod_Script_SetupMass(T1_CORE_POD, 1000);
    Pod_Script_SetupDrag(T1_CORE_POD, 0x1333);
    WindowSetText(st.win, st.text[3]);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerReactorDoorIn);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, T1_OnTriggerReactorDoorOut);
}

// 0x10002d70  EV_POD_HOOKED bonus pod
static void T1_OnPodHookedBonus(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_BONUS_POD)
        return;
    Pod_Script_SetupHookPoint(T1_BONUS_POD, 0, 0x65, 0);
    Pod_Script_SetupMass(T1_BONUS_POD, 0x4e2);
    Pod_Script_SetupDrag(T1_BONUS_POD, 0x1333);
    st.bonusHooked = 1;
}

// 0x10002dd0  EV_TICK_1S (from the reactor start-up): evac above y = 10000
static void T1_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(T1_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    if (st.bonusHooked == 0) {
        Lander_SetCash(T1_LANDER, Lander_GetCash(T1_LANDER) + 300000);
        MissionScore_End(1);                             // 0x100013b0
    } else {
        Lander_SetCash(T1_LANDER, Lander_GetCash(T1_LANDER) + 300000);
        MissionScore_End(2);
    }
    EventType_RemoveHandler(EV_TICK_1S, T1_EvacTick);
}

// 0x10002e60  EV_TRIGGER_ENTER 0x40060: iris door 0x3005f/0x30061..0x30063 opens (the first sound
// is on leaf 0x30061, the delayed one on 0x3005f)
static void T1_OnTriggerIris60In(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0x60))
        return;
    AnimPlay(T1_ANIM(0x5f), 0x18, 0);
    InTime3(200, AnimPlay, T1_ANIM(0x61), 0x18, 0);
    InTime3(400, AnimPlay, T1_ANIM(0x62), 0x18, 0);
    InTime3(600, AnimPlay, T1_ANIM(0x63), 0x18, 0);
    Sound_PlayOnce(T1_ANIM(0x61), 0xaa);
    InTime2(200, Sound_PlayOnce, T1_ANIM(0x5f), 0xaa);
    InTime2(400, Sound_PlayOnce, T1_ANIM(0x62), 0xaa);
    InTime2(600, Sound_PlayOnce, T1_ANIM(0x63), 0xaa);
}

// 0x10002f30  EV_TRIGGER_LEAVE 0x40060: all four leaves close at once (sounds staggered)
static void T1_OnTriggerIris60Out(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0x60))
        return;
    AnimPlay(T1_ANIM(0x5f), 0x18, 4);
    AnimPlay(T1_ANIM(0x61), 0x18, 4);
    AnimPlay(T1_ANIM(0x62), 0x18, 4);
    AnimPlay(T1_ANIM(0x63), 0x18, 4);
    Sound_PlayOnce(T1_ANIM(0x5f), 0xaa);
    InTime2(200, Sound_PlayOnce, T1_ANIM(0x61), 0xaa);
    InTime2(400, Sound_PlayOnce, T1_ANIM(0x62), 0xaa);
    InTime2(600, Sound_PlayOnce, T1_ANIM(0x63), 0xaa);
}

// ---- reactor run-up (rotor 0x30095) ----------------------------------------------------------------
static void T1_RotorSpin1Wrapped(const LanderEvent *ev);
static void T1_RotorSpin2Wrapped(const LanderEvent *ev);
static void T1_RotorSpin3Wrapped(const LanderEvent *ev);
static void T1_RotorSpin4Wrapped(const LanderEvent *ev);

// Body of the four EV_TICK_1S run-up handlers (0x10003730, 0x10003550, 0x10003350, 0x10003170;
// inlined in each): rotor anim 0x13 looping, speeding up then down over 10 s, the shaft force
// box(es) on at 4 s and off at 7 s, then the next stage waits for the loop to wrap.
static void T1_RotorSpinTick(ObjId boxA, ObjId boxB, EventHandlerFn self, EventHandlerFn wrapped)
{
    if (st.spinTicks == 0) {
        AnimPlay(T1_ROTOR, 0x13, 0x81);
        AnimSetUpdateInterval(T1_ROTOR, 0x19);
    }
    if (++st.spinTicks > 0)
        AnimSetUpdateInterval(T1_ROTOR, ((5 - st.spinTicks) * 50) / 10);
    if (st.spinTicks == 4) {
        Object_SetAlive(boxA);
        if (boxB != 0)
            Object_SetAlive(boxB);
    }
    if (st.spinTicks > 4)
        AnimSetUpdateInterval(T1_ROTOR, 0xf);
    if (st.spinTicks == 7) {
        Object_SetDead(boxA);
        if (boxB != 0)
            Object_SetDead(boxB);
    }
    if (st.spinTicks > 7)
        AnimSetUpdateInterval(T1_ROTOR, ((st.spinTicks - 2) * 50) / 10);
    if (st.spinTicks == 10) {
        EventType_AddHandler(EV_ANIM_WRAPPED, NULL, wrapped);
        EventType_RemoveHandler(EV_TICK_1S, self);
    }
}

// 0x10002ff0  EV_ANIM_FINISHED rotor (anim 0x16): reactor online - socket lights off after 1 s,
// reactor door opens after 3 s, exit doors enabled, iris door 0x40060 frozen
static void T1_OnReactorOnline(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_ROTOR)
        return;
    st.reactorOnline = 1;
    InTime1(1000, Object_SetDead, T1_LIGHT(0x8c));
    InTime1(1000, Object_SetDead, T1_LIGHT(0x8d));
    InTime1(1000, Object_SetDead, T1_LIGHT(0x8e));
    InTime1(1000, Object_SetDead, T1_LIGHT(0x8f));
    InTime3(3000, AnimPlay, T1_REACTOR_DOOR, 4, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnReactorOnline);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerIris60In);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, T1_OnTriggerIris60Out);
}

// 0x100030b0  EV_ANIM_FINISHED anim 0x15 (last stage): rotor anim 0x16
static void T1_RotorStep5Done(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x15)
        return;
    AnimStop(T1_ROTOR, 0);
    AnimPlay(T1_ROTOR, 0x16, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnReactorOnline);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_RotorStep5Done);
}

// 0x10003100  EV_ANIM_WRAPPED rotor: anim 0x15 in 0.5 s
static void T1_RotorSpin4Wrapped(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_ROTOR)
        return;
    AnimStop(T1_ROTOR, 0);
    InTime3(500, AnimPlay, T1_ROTOR, 0x15, 0);
    AnimSetUpdateInterval(T1_ROTOR, 5);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_RotorStep5Done);
    EventType_RemoveHandler(EV_ANIM_WRAPPED, T1_RotorSpin4Wrapped);
}

// 0x10003170  EV_TICK_1S: run-up 4, force box 0x100090
static void T1_RotorSpin4Tick(const LanderEvent *ev)
{
    (void)ev;
    T1_RotorSpinTick(T1_FORCEBOX(0x90), 0, T1_RotorSpin4Tick, T1_RotorSpin4Wrapped);
}

// 0x10003290  EV_ANIM_FINISHED anim 0x1b: run-up 4 starts
static void T1_RotorStep4Done(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x1b)
        return;
    AnimStop(T1_ROTOR, 0);
    st.spinTicks = 0;
    EventType_AddHandler(EV_TICK_1S, NULL, T1_RotorSpin4Tick);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_RotorStep4Done);
}

// 0x100032e0  EV_ANIM_WRAPPED rotor: anim 0x1b in 0.5 s
static void T1_RotorSpin3Wrapped(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_ROTOR)
        return;
    AnimStop(T1_ROTOR, 0);
    InTime3(500, AnimPlay, T1_ROTOR, 0x1b, 0);
    AnimSetUpdateInterval(T1_ROTOR, 8);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_RotorStep4Done);
    EventType_RemoveHandler(EV_ANIM_WRAPPED, T1_RotorSpin3Wrapped);
}

// 0x10003350  EV_TICK_1S: run-up 3, force boxes 0x100092 and 0x100093
static void T1_RotorSpin3Tick(const LanderEvent *ev)
{
    (void)ev;
    T1_RotorSpinTick(T1_FORCEBOX(0x92), T1_FORCEBOX(0x93), T1_RotorSpin3Tick, T1_RotorSpin3Wrapped);
}

// 0x10003490  EV_ANIM_FINISHED anim 0x14: run-up 3 starts
static void T1_RotorStep3Done(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x14)
        return;
    AnimStop(T1_ROTOR, 0);
    st.spinTicks = 0;
    EventType_AddHandler(EV_TICK_1S, NULL, T1_RotorSpin3Tick);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_RotorStep3Done);
}

// 0x100034e0  EV_ANIM_WRAPPED rotor: anim 0x14 in 0.5 s
static void T1_RotorSpin2Wrapped(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_ROTOR)
        return;
    AnimStop(T1_ROTOR, 0);
    InTime3(500, AnimPlay, T1_ROTOR, 0x14, 0);
    AnimSetUpdateInterval(T1_ROTOR, 10);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_RotorStep3Done);
    EventType_RemoveHandler(EV_ANIM_WRAPPED, T1_RotorSpin2Wrapped);
}

// 0x10003550  EV_TICK_1S: run-up 2, force box 0x100090
static void T1_RotorSpin2Tick(const LanderEvent *ev)
{
    (void)ev;
    T1_RotorSpinTick(T1_FORCEBOX(0x90), 0, T1_RotorSpin2Tick, T1_RotorSpin2Wrapped);
}

// 0x10003670  EV_ANIM_FINISHED anim 0x1c: run-up 2 starts
static void T1_RotorStep2Done(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x1c)
        return;
    AnimStop(T1_ROTOR, 0);
    st.spinTicks = 0;
    EventType_AddHandler(EV_TICK_1S, NULL, T1_RotorSpin2Tick);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_RotorStep2Done);
}

// 0x100036c0  EV_ANIM_WRAPPED rotor: anim 0x1c in 0.5 s
static void T1_RotorSpin1Wrapped(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_ROTOR)
        return;
    AnimStop(T1_ROTOR, 0);
    InTime3(500, AnimPlay, T1_ROTOR, 0x1c, 0);
    AnimSetUpdateInterval(T1_ROTOR, 0xc);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_RotorStep2Done);
    EventType_RemoveHandler(EV_ANIM_WRAPPED, T1_RotorSpin1Wrapped);
}

// 0x10003730  EV_TICK_1S: run-up 1, force box 0x100091
static void T1_RotorSpin1Tick(const LanderEvent *ev)
{
    (void)ev;
    T1_RotorSpinTick(T1_FORCEBOX(0x91), 0, T1_RotorSpin1Tick, T1_RotorSpin1Wrapped);
}

// 0x10003850  EV_ANIM_FINISHED anim 0x15: run-up 1 starts
static void T1_RotorStep1Done(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x15)
        return;
    AnimStop(T1_ROTOR, 0);
    st.spinTicks = 0;
    EventType_AddHandler(EV_TICK_1S, NULL, T1_RotorSpin1Tick);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_RotorStep1Done);
}

// 0x100038a0  EV_ANIM_FINISHED rotor (any anim; anim 0xe started by the core): anim 0x15 in 0.5 s
static void T1_RotorArmed(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_ROTOR)
        return;
    AnimStop(T1_ROTOR, 0);
    InTime3(500, AnimPlay, T1_ROTOR, 0x15, 0);
    AnimSetUpdateInterval(T1_ROTOR, 0x10);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_RotorStep1Done);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_RotorArmed);
}

// 0x10003910  EV_ANIM_FINISHED anim 0x17 (any object; never removed): the core is in - socket lights,
// clamps and arms move, rotor assembly starts in 5 s, evac check armed
static void T1_OnCoreInstalled(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x17)
        return;
    InTime1(1000, Object_SetAlive, T1_LIGHT(0x8c));
    InTime1(1000, Object_SetAlive, T1_LIGHT(0x8d));
    InTime1(1000, Object_SetAlive, T1_LIGHT(0x8e));
    InTime1(1000, Object_SetAlive, T1_LIGHT(0x8f));
    InTime3(1500, AnimPlay, T1_ANIM(0x02), 0x10, 0);
    AnimSetUpdateInterval(T1_ANIM(0x02), 0x19);
    InTime2(1500, Sound_PlayOnce, T1_ANIM(0x02), 0xb0);
    InTime3(1500, AnimPlay, T1_ANIM(0x03), 0x11, 0);
    AnimSetUpdateInterval(T1_ANIM(0x03), 0x19);
    InTime3(1500, AnimPlay, T1_ANIM(0xae), 0xc, 0);
    InTime3(1500, AnimPlay, T1_ANIM(0xaf), 0xc, 0);
    InTime3(1500, AnimPlay, T1_ANIM(0xb0), 0xc, 0);
    InTime3(1500, AnimPlay, T1_ANIM(0xb1), 0xc, 0);
    InTime3(5000, AnimPlay, T1_ANIM(0x94), 0xd, 0);
    InTime3(5000, AnimPlay, T1_ROTOR, 0xe, 0);
    InTime3(5000, AnimPlay, T1_ANIM(0xab), 0x12, 0);
    AnimSetUpdateInterval(T1_ANIM(0x94), 0x32);
    AnimSetUpdateInterval(T1_ROTOR, 0x32);
    AnimSetUpdateInterval(T1_ANIM(0xab), 0x32);
    AnimSetUpdateInterval(T1_ANIM(0xae), 0xf);
    AnimSetUpdateInterval(T1_ANIM(0xaf), 0xf);
    AnimSetUpdateInterval(T1_ANIM(0xb0), 0xf);
    AnimSetUpdateInterval(T1_ANIM(0xb1), 0xf);
    InTime2(1500, Sound_PlayOnce, T1_ANIM(0xae), 0x2d8);
    InTime2(1500, Sound_PlayOnce, T1_ANIM(0xaf), 0x2d8);
    InTime2(1500, Sound_PlayOnce, T1_ANIM(0xb0), 0x2d8);
    InTime2(1500, Sound_PlayOnce, T1_ANIM(0xb1), 0x2d8);
    Sound_PlayLoop(T1_ANIM(0x94), 0x2d2, -1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_RotorArmed);
    EventType_AddHandler(EV_TICK_1S, NULL, T1_EvacTick);
}

// 0x10003b70  EV_TRIGGER_ENTER by the core pod (only trigger 0x400ac targets it): the pod is
// replaced by the socketed core 0x3007b (anim 0x17)
static void T1_OnCoreDelivered(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T1_CORE_POD)
        return;
    Object_SetProperty(T1_CORE_POD, PROP_TARGET, -1);    // drop it
    Object_SetDead(T1_CORE_POD);
    Object_SetAlive(T1_CORE_ANIM);
    AnimPlay(T1_CORE_ANIM, 0x17, 0);
    WindowSetText(st.win, st.text[4]);
    st.coreReplaced = 1;
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnCoreInstalled);
}

// 0x10003c00  EV_TRIGGER_ENTER 0x40065: lights along the lower corridor go out, lift 9 moves
static void T1_OnTriggerLift9(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0x65))
        return;
    InTime1(1000, Object_SetDead, T1_LIGHT(0x9d));
    InTime1(1250, Object_SetDead, T1_LIGHT(0x9f));
    InTime1(1500, Object_SetDead, T1_LIGHT(0xa0));
    InTime1(1500, Object_SetDead, T1_LIGHT(0x9e));
    MobObj_Wake(9);
    AnimPlay(T1_LIFT_9, 0x19, 0x40);
    AnimSetUpdateInterval(T1_LIFT_9, 5);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerLift9);
}

// 0x10003cb0  EV_TRIGGER_ENTER 0x400ad: plant entrance - the work lights come on after 6.5 s, the
// entrance lights go out one by one, lift A moves
static void T1_OnTriggerEntranceLights(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0xad))
        return;
    for (int32_t n = 0x66; n <= 0x6a; n++)                // (unrolled)
        InTime1(6500, Object_SetAlive, T1_LIGHT(n));
    for (int32_t n = 0x6d; n <= 0x7a; n++)                // (unrolled; 0x16006b/0x16006c not touched)
        InTime1(6500, Object_SetAlive, T1_LIGHT(n));
    InTime1(1000, Object_SetDead, T1_LIGHT(0x99));
    InTime1(1000, Object_SetDead, T1_LIGHT(0x9a));
    InTime1(1500, Object_SetDead, T1_LIGHT(0x98));
    InTime1(1500, Object_SetDead, T1_LIGHT(0xaa));
    InTime1(2000, Object_SetDead, T1_LIGHT(0xa5));
    InTime1(2500, Object_SetDead, T1_LIGHT(0xa1));
    InTime1(2500, Object_SetDead, T1_LIGHT(0xa2));
    InTime1(3000, Object_SetDead, T1_LIGHT(0xa7));
    InTime1(3000, Object_SetDead, T1_LIGHT(0xa6));
    InTime1(3500, Object_SetDead, T1_LIGHT(0xa8));
    InTime1(4000, Object_SetDead, T1_LIGHT(0xa9));
    InTime1(4500, Object_SetDead, T1_LIGHT(0xa3));
    InTime1(4500, Object_SetDead, T1_LIGHT(0xa4));
    MobObj_Wake(0xa);
    AnimPlay(T1_LIFT_A, 0x1a, 0x40);
    AnimSetUpdateInterval(T1_LIFT_A, 5);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerEntranceLights);
}

// 0x10003f60  EV_PICKUP 0x1300b3: full shield (the voice says 25 %)
static void T1_OnPickupShieldFull(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_PICKUP(0xb3)) {
        Object_SetProperty(T1_LANDER, PROP_HITPOINTS, 0x100);
        Voice_ShieldAcquired(T1_LANDER, 25);
    }
}

// 0x10003fa0  EV_PICKUP 0x1300b4: +50 % shield
static void T1_OnPickupShieldB4(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_PICKUP(0xb4)) {
        int32_t shield = (int32_t)Object_GetProperty(T1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(T1_LANDER, 50);
        Object_SetProperty(T1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
}

// 0x10004010  EV_PICKUP 0x1300b5: +50 % shield
static void T1_OnPickupShieldB5(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_PICKUP(0xb5)) {
        int32_t shield = (int32_t)Object_GetProperty(T1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(T1_LANDER, 50);
        Object_SetProperty(T1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
}

// 0x10004080  EV_PICKUP: shield 0x1300b6, cash 0x130017/0x130018, missiles 0x1300ea/0x1300eb
static void T1_OnPickup(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T1_PICKUP(0xb6)) {
        int32_t shield = (int32_t)Object_GetProperty(T1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(T1_LANDER, 50);
        Object_SetProperty(T1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if (ev->arg[0] == (intptr_t)T1_PICKUP(0x17) || ev->arg[0] == (intptr_t)T1_PICKUP(0x18)) {
        Lander_SetCash(T1_LANDER, Lander_GetCash(T1_LANDER) + gMediumCash);
        Voice_CashAcquired(T1_LANDER);
    }
    if (ev->arg[0] == (intptr_t)T1_PICKUP(0xea) || ev->arg[0] == (intptr_t)T1_PICKUP(0xeb)) {
        if (Lander_AddMissiles(T1_LANDER, 2) == 0) {
            Voice_MissilesFull(T1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(T1_LANDER);
    }
}

static void T1_OnTriggerPlant(const LanderEvent *ev);

// 0x10004170  EV_TRIGGER_ENTER 0x400c0 (the whole complex): mob 0xb (ambient) sleeps; before the
// core is replaced: the locate objective and the plant-area texts stop
static void T1_OnTriggerComplexIn(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0xc0))
        return;
    MobObj_Sleep(0xb);
    if (st.coreReplaced == 0) {
        WindowSetText(st.win, st.text[2]);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerPlant);
        EventType_RemoveHandler(EV_TRIGGER_LEAVE, T1_OnTriggerPlant);
    }
}

// 0x100041d0  EV_TRIGGER_LEAVE 0x400c0: mob 0xb wakes; core replaced -> the climb objective, else the
// plant-area texts come back
static void T1_OnTriggerComplexOut(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0xc0))
        return;
    MobObj_Wake(0xb);
    if (st.coreReplaced == 1) {
        WindowSetText(st.win, st.text[5]);
        return;
    }
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerPlant);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnTriggerPlant);
}

// 0x10004240  EV_TRIGGER_ENTER / EV_TRIGGER_LEAVE 0x40096 (around the plant): objective 1 / 0
static void T1_OnTriggerPlant(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T1_TRIGGER(0x96))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        WindowSetText(st.win, st.text[1]);
    else
        WindowSetText(st.win, st.text[0]);
}

// 0x10004320  common set-up
static void T1_Setup(void)
{
    st.text = t1_text[gLanguage];
    SetGravity(13);
    gAirResistance = 0xc;
    gMainThrottlePower = 0x1a;
    gNbWaypoints = t1_nbWaypoints;
    gWaypoints = t1_waypoints;
    Object_SetProperty(T1_DESTR(0x04), PROP_ARMOUR, 0x28);
    Object_SetProperty(T1_DESTR(0x04), PROP_EXPLOSION, 100);
    Object_SetProperty(T1_DESTR(0xb7), PROP_ARMOUR, 0xff);
    Object_SetProperty(T1_DESTR(0xb7), PROP_EXPLOSION, 0x50);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerComplexIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnTriggerComplexOut);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10004290
    EventType_AddHandler(EV_PICKUP, NULL, T1_OnPickupShieldFull);
    EventType_AddHandler(EV_PICKUP, NULL, T1_OnPickupShieldB4);
    EventType_AddHandler(EV_PICKUP, NULL, T1_OnPickupShieldB5);
    EventType_AddHandler(EV_PICKUP, NULL, T1_OnPickup);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerLift9);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerEntranceLights);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerReactorDoorIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnTriggerReactorDoorOut);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerIrisIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnTriggerIrisOut);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerIris60In);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnTriggerIris60Out);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerEntrance);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchFans);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchHatches);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchIris2e);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchIris36);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchIris32);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchIris3f);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchIris43);
    EventType_AddHandler(EV_SWITCH, NULL, T1_OnSwitchIris47);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnCoreSettledBarrier);
    EventType_AddHandler(EV_DESTROYED, NULL, T1_OnDestroyed);
    EventType_AddHandler(EV_POD_HOOKED, NULL, T1_OnPodHookedCore);
    EventType_AddHandler(EV_POD_HOOKED, NULL, T1_OnPodHookedBonus);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerPlant);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnTriggerPlant);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnCoreDelivered);
    for (int32_t n = 0x7c; n <= 0x7f; n++)                // (unrolled) shaft fans
        AnimPlay(T1_ANIM(n), 0, 0x81);
    for (int32_t n = 0x7c; n <= 0x7f; n++)
        Sound_PlayLoop(T1_ANIM(n), 0xb7, -1);
    AnimPlay(T1_ANIM(0xb2), 1, 0x81);
    Sound_PlayLoop(T1_ANIM(0xb2), 0xb8, -1);
    for (int32_t n = 0xc1; n <= 0xc4; n++)                // (unrolled)
        AnimPlay(T1_ANIM(n), 1, 0x85);
    for (int32_t n = 0xc1; n <= 0xc4; n++)
        Sound_PlayLoop(T1_ANIM(n), 0xb9, -1);
    AnimPlay(T1_ANIM(0x22), 2, 1);
    AnimPlay(T1_ANIM(0x23), 2, 1);
    for (int32_t id = T1_ANIM(0x22); id <= (int32_t)T1_ANIM(0x23); id++)
        Sound_PlayLoop((ObjId)id, 0xbc, -1);
    AnimPlay(T1_ANIM(0x64), 3, 0x81);
    AnimSetUpdateInterval(T1_ANIM(0x64), 0x19);
    Sound_PlayLoop(T1_ANIM(0x64), 0xf1, -1);
    for (int32_t id = T1_EMITTER(0xb9); id <= (int32_t)T1_EMITTER(0xbf); id++)
        Sound_PlayLoop((ObjId)id, 0xff, -1);
    for (int32_t id = T1_EMITTER(0xc5); id <= (int32_t)T1_EMITTER(0xd3); id++)
        Sound_PlayLoop((ObjId)id, 0xf3, -1);
    TexAnimPlay(0x30, 1, 100);
    MobObj_Wake(0xb);
    MobObj_Wake(8);
    Object_SetDead(T1_CORE_ANIM);
    for (int32_t n = 0x8c; n <= 0x8f; n++)                // (unrolled) socket lights
        Object_SetDead(T1_LIGHT(n));
    for (int32_t n = 0x66; n <= 0x6a; n++)                // (unrolled) work lights
        Object_SetDead(T1_LIGHT(n));
    for (int32_t n = 0x6d; n <= 0x7a; n++)
        Object_SetDead(T1_LIGHT(n));
    for (int32_t n = 0x90; n <= 0x93; n++)                // (unrolled) shaft force boxes
        Object_SetDead(T1_FORCEBOX(n));
    Object_SetProperty(T1_FORCEBOX(0x90), PROP_STATE, 200);
    Object_SetProperty(T1_FORCEBOX(0x91), PROP_STATE, -150);
    Object_SetProperty(T1_FORCEBOX(0x92), PROP_STATE, -150);
    Object_SetProperty(T1_FORCEBOX(0x93), PROP_STATE, 200);
    Object_SetProperty(T1_FORCEBOX(0x80), PROP_STATE, 0x28);
    Object_SetProperty(T1_FORCEBOX(0x80), PROP_FLAGS, 2);
    Object_SetProperty(T1_FORCEBOX(0x81), PROP_STATE, 1000);
    Object_SetProperty(T1_FORCEBOX(0x82), PROP_STATE, 1000);
    for (int32_t n = 0x83; n <= 0x88; n++)                // (unrolled)
        Object_SetProperty(T1_FORCEBOX(n), PROP_STATE, 800);
    Object_SetProperty(T1_TRIGGER(0xac), PROP_TARGET, T1_CORE_POD);
    MissionScore_Init(T1_LANDER, 300000, 8);             // 0x10001530
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    Media_AudioPlay(0);
}

// 0x100049f0  LevelStartLevel_Lander (export 0x10001010)
static void T1_StartLander(void)
{
    MobObj_Wake(T1_MOB_LANDER);
    MobObj_Wake(T1_MOB_CAMERA);
    MobObj_Wake(4);
    MobObj_Wake(5);
    Lander_SetHeadLight(T1_LANDER, 0x31c, 0x800);
    Object_SetProperty(T1_CAMERA, PROP_TARGET, T1_LANDER);
    T1_Setup();
}

// 0x10004a50  LevelStartLevel_Flycam (export 0x10001020)
static void T1_StartFlycam(void)
{
    T1_Setup();
    MobObj_Wake(T1_MOB_FLYCAM);
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void T1_Stop(void)
{
}

// 0x10001040
static uint32_t T1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel: two calls of the empty 0x10001050, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t T1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10009160, .count = 3 * 6, .out = t1_text },
    { 0 },
};

const mission_script_t mission_titan_1 = {
    .name          = "TITAN\\1",
    .get_version   = T1_GetVersion,
    .load          = T1_Load,
    .start_lander  = T1_StartLander,
    .start_flycam  = T1_StartFlycam,
    .start_animcam = NULL,
    .stop          = T1_Stop,
    .text          = t1_gametext,
};
