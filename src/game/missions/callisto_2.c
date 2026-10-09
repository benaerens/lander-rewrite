// MISSIONS\CALLISTO\2\MISSIONR.DLL - faithful C port.
//
// Mission: the Callisto maximum-security prison (waypoint 1). Enter through scan chamber 1 (trigger
// 0x4004a: entry scan with lights, mobs 0x12/0x13 and 6/7), then follow the security droid
// (AnimTurret mob 0x14) through the complex - straying more than 5000 units, or firing, raises the
// alarm: the droid zaps the lander while it has line of sight, all gun turrets activate. Drop
// the hooked pod 2 on one of the crane pads (0x40058/0x40059): the crane (mobs 0x2b..0x32) swaps it
// for the cargo pod 0xd / 0xf. Back through scan chamber 2 (0x40053): carrying pod 2 (the prisoner)
// fails the scan and raises the alarm. Hook the cargo and climb above y = 10000: success,
// +150000 cash. Par time 150 s, 6 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10003f70), LevelStartLevel_Flycam 0x10001020
// (-> 0x10003fb0), LevelStopLevel 0x10001030 (-> the empty 0x10003fd0), LevelGetVersion
// 0x10001040. The score library (0x10001050-0x100015f4) is in mission_lib.c; the destroyed-sound
// handler 0x10003a80 is MissionLib_OnDestroyedSound.
#include <stdlib.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define C2_LANDER        MOB_ID(OT_LANDER, 0x0a)         // 0x0101000a
#define C2_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define C2_POD           MOB_ID(OT_POD, 2)               // 0x010c0002 the prisoner pod
#define C2_DROID         MOB_ID(OT_ANIMTURRET, 0x14)     // 0x010e0014 security droid
#define C2_BEAM_START    ((ObjId)0x050d0009)             // emitter mob 9, part 1 (bits 26-31)
#define C2_BEAM_END      MOB_ID(OT_PARTICLEEMITTER, 9)   // 0x010d0009
#define C2_BEAM_LIGHT    MOB_ID(OT_LIGHTOBJECT, 0x0b)    // 0x0116000b
#define C2_CRANE(n)      MOB_ID(OT_ANIMOBJECT, (n))      // 0x0103002b..0x01030032 crane parts
#define C2_GUN(n)        MOB_ID(OT_ANIMTURRET, (n))      // 0x010e0016..0x010e002a
#define C2_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define C2_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define C2_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define C2_MOB_CAMERA    1
#define C2_MOB_POD       2
#define C2_MOB_FLYCAM    3
#define C2_MOB_LANDER    0x0a
#define C2_MOB_DROID     0x14

// ---- static data (.data) --------------------------------------------------------------------------
// 0x10009160: [language][19] string pointers (entry 18 is the English "Retrieve Cargo" in all three)
static const char *c2_text[3][19];
enum {
    C2_T_FIND = 0, C2_T_WELCOME, C2_T_BETA, C2_T_OFF_LIMITS, C2_T_SCANNING, C2_T_SCANNED,
    C2_T_FOLLOW, C2_T_PENALTIES, C2_T_TOO_FAR, C2_T_FINAL_WARNING, C2_T_VIOLATION, C2_T_LETHAL,
    C2_T_SCAN_FAILED, C2_T_ENTER_SCANNER, C2_T_RETRIEVE_RETURN, C2_T_EXIT, C2_T_CLIMB,
    C2_T_PRISONER, C2_T_RETRIEVE
};
static const int32_t  c2_nbWaypoints = 1;                // 0x10009244
static const Waypoint c2_waypoints[1] = {                // 0x10009248
    { 19328, -70528 },
};
static const TurretType c2_turretType4 = {               // 0x10009250 -> gTurretTypes[4]
    { 0, 24, 240, 0 }, 0x00, 0, 2, 25, 100, 0x5a, 1024, 5000, 0
};
static const TurretType c2_turretType16 = {              // 0x10009270 -> gTurretTypes[16]
    { 0, 0, 134, 0 }, 0x01, 0, 1, 0, 70, 0x5a, 1024, 5000, 0
};
// 0x1000928c: .data = 1, set to 0 when the cargo is dropped on a pad; picks which way the crane's
// gate 0x30057 moves at triggers 0x4005a / 0x4005b

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    int32_t      gateDir;         // 0x1000928c (.data = 1)
    Window      *win;             // 0x1000c5ac objective line
    int32_t      hasPrisoner;     // 0x1000c5b0 pod 2 was hooked
    int32_t      inScanner1;      // 0x1000c5b4 lander in scan chamber 1 (0x4004a)
    int32_t      inScanner2;      // 0x1000c5b8 lander in scan chamber 2 (0x40053)
    int32_t      scanState;       // 0x1000c5bc 1 waiting, 2 lights down, 3 scanning, 4 done
    int32_t      exitSide;        // 0x1000c5c0 which door set scan chamber 2 opens (toggles)
    int32_t      alarm;           // 0x1000c5c4 alarm raised (one shot)
    int32_t      escorted;        // 0x1000c5cc the escort reached trigger 0x40011
    int32_t      followActive;    // 0x1000c5d0 C2_FollowTick is registered
    int32_t      welcomeTicks;    // 0x1000c5d4
    int32_t      strayTicks;      // 0x1000c5d8 seconds away from the droid
    int32_t      pad;             // 0x1000c5dc 1 = cargo on pad 0x40058, 2 = on 0x40059
    int32_t      podHooked;       // 0x1000c5e0
    int32_t      zapTicks;        // 0x1000c5e8
    const char *const *text;      // 0x1000ca28 = c2_text[gLanguage]
} st;

static void C2_FollowTick(const LanderEvent *ev);
static void C2_OnFired(const LanderEvent *ev);
static void C2_TurretZapCheck(const LanderEvent *ev);
static void C2_OnTriggerChamberIn(const LanderEvent *ev);
static void C2_Alarm(void);
static void C2_Scanner2Wait(void);

// 0x10001600  stop the follow-the-droid check
static void C2_StopFollowCheck(void)
{
    if (st.followActive) {
        EventType_RemoveHandler(EV_TICK_1S, C2_FollowTick);
        st.followActive = 0;
    }
}

// 0x10001630  midpoint of two positions (arithmetic shift)
static void C2_Midpoint(const int32_t *a, const int32_t *b, int32_t *out)
{
    out[0] = (a[0] + b[0]) >> 1;
    out[1] = (a[1] + b[1]) >> 1;
    out[2] = (a[2] + b[2]) >> 1;
}

// 0x10001670  EV_TICK_100MS for 8 ticks: the droid's beam (emitter mob 9 + light mob 0xb) hits
// the lander for 10 shield per tick
static void C2_ZapTick(const LanderEvent *ev)
{
    (void)ev;
    int32_t mid[3];
    const int32_t *droid = (const int32_t *)Object_GetProperty(C2_DROID, PROP_POSITION);
    const int32_t *lander = (const int32_t *)Object_GetProperty(C2_LANDER, PROP_POSITION);
    Object_SetProperty(C2_BEAM_START, PROP_POSITION, (intptr_t)droid);
    Object_SetProperty(C2_BEAM_END, PROP_POSITION, (intptr_t)lander);
    C2_Midpoint(droid, lander, mid);
    Object_SetProperty(C2_BEAM_LIGHT, PROP_POSITION, (intptr_t)mid);
    Object_SetProperty(C2_LANDER, PROP_HITPOINTS, Object_GetProperty(C2_LANDER, PROP_HITPOINTS) - 10);
    if (++st.zapTicks == 8) {
        EventType_RemoveHandler(EV_TICK_100MS, C2_ZapTick);
        MobObj_Sleep(0x0b);
        MobObj_Sleep(9);
        Sound_StopObject(C2_BEAM_LIGHT);
    }
}

// 0x10001740  EV_TICK_1S after the alarm: zap whenever the droid can see the lander
// (LanderAPI_LineOfSight 0x4447a0 returns World_SegmentHit(..., 0, ...): nonzero = clear and close)
static void C2_TurretZapCheck(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *droid = (const int32_t *)Object_GetProperty(C2_DROID, PROP_POSITION);
    const int32_t *lander = (const int32_t *)Object_GetProperty(C2_LANDER, PROP_POSITION);
    if (LanderAPI_LineOfSight(droid, lander) == 0)
        return;
    int32_t mid[3];
    MobObj_Wake(9);
    Object_SetProperty(C2_BEAM_START, PROP_POSITION, (intptr_t)droid);
    Object_SetProperty(C2_BEAM_END, PROP_POSITION, (intptr_t)lander);
    MobObj_Wake(0x0b);
    Sound_PlayLoop(C2_BEAM_LIGHT, 0xfa, -1);
    C2_Midpoint(droid, lander, mid);
    Object_SetProperty(C2_BEAM_LIGHT, PROP_POSITION, (intptr_t)mid);
    EventType_AddHandler(EV_TICK_100MS, NULL, C2_ZapTick);
    st.zapTicks = 0;
}

// 0x10001810  EV_LANDER_FIRED (from scan chamber 1 on): firing inside raises the alarm
static void C2_OnFired(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != C2_LANDER)
        return;
    AnimStop(C2_DROID, 0);
    WindowSetText(st.win, st.text[C2_T_CLIMB]);
    C2_Alarm();                                          // 0x10002120
    EventType_AddHandler(EV_TICK_1S, NULL, C2_TurretZapCheck);
    EventType_RemoveHandler(EV_LANDER_FIRED, C2_OnFired);
    C2_StopFollowCheck();                                // 0x10001600
    if (st.escorted)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerChamberIn);
}

// 0x10001890  EV_TICK_1S once the cargo is hooked: above 10000 -> success
static void C2_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(C2_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        Lander_SetCash(C2_LANDER, Lander_GetCash(C2_LANDER) + 150000);
        MissionScore_End(1);                             // 0x100013a0
        EventType_RemoveHandler(EV_TICK_1S, C2_EvacTick);
    }
}

// 0x100018f0  EV_POD_HOOKED (after the crane delivered): the cargo is on the hook
static void C2_OnCargoHooked(const LanderEvent *ev)
{
    (void)ev;
    EventType_AddHandler(EV_TICK_1S, NULL, C2_EvacTick);
    WindowSetText(st.win, st.text[C2_T_CLIMB]);
}

// 0x10001920  EV_ANIM_FINISHED: the crane is done -> release the lander, the cargo pod appears
static void C2_OnCraneDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != C2_CRANE(0x2b))
        return;
    AnimPlay(C2_ANIM(0x34), 8, 4);
    Sound_PlayChain(C2_ANIM(0x34), 2, 0xce, 0xca);
    AnimPlay(C2_ANIM(0x35), 8, 4);
    Sound_PlayChain(C2_ANIM(0x35), 2, 0xce, 0xca);
    Object_SetProperty(C2_LANDER, PROP_FLAGS, (intptr_t)(int32_t)0x800002fc);   // unlock the controls
    if (st.pad == 1) {
        MobObj_Sleep(0x0c);
        MobObj_Wake(0x0d);
    }
    if (st.pad == 2) {
        MobObj_Sleep(0x0e);
        MobObj_Wake(0x0f);
    }
    EventType_RemoveHandler(EV_ANIM_FINISHED, C2_OnCraneDone);
    WindowSetText(st.win, st.text[C2_T_RETRIEVE]);
    EventType_AddHandler(EV_POD_HOOKED, NULL, C2_OnCargoHooked);
}

// 0x10001a20  EV_TRIGGER_ENTER 0x40058 / 0x40059 (now targeting the lander): the lander is on the
// pad -> controls locked, the crane loads the cargo
static void C2_OnTriggerPadLander(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig != C2_TRIGGER(0x58) && trig != C2_TRIGGER(0x59))
        return;
    Object_SetProperty(C2_LANDER, PROP_FLAGS, 0x2fc);    // lock all controls
    st.gateDir = 0;
    AnimPlay(C2_ANIM(0x36), 8, 0);
    Sound_PlayChain(C2_ANIM(0x36), 2, 0xce, 0xca);
    AnimPlay(C2_ANIM(0x37), 8, 0);
    Sound_PlayChain(C2_ANIM(0x37), 2, 0xce, 0xca);
    for (int32_t n = 0x2b; n <= 0x32; n++)               // (unrolled)
        InTime3(2000, AnimPlay, C2_CRANE(n), 0x19, 0);
    InTime2(2000, Sound_PlayOnce, C2_CRANE(0x2b), 0x8c);
    for (int32_t n = 0x2b; n <= 0x32; n++)               // (unrolled)
        AnimSetUpdateInterval(C2_CRANE(n), 0x14);
    InTime2(2000, Sound_PlayOnce, C2_CRANE(0x32), 0x8c);
    if (st.pad == 1) {
        InTime3(2000, AnimPlay, MOB_ID(OT_ANIMOBJECT, 0x0c), 0xa, 0);
        AnimSetUpdateInterval(MOB_ID(OT_ANIMOBJECT, 0x0c), 0x14);
    }
    if (st.pad == 2) {
        InTime3(2000, AnimPlay, MOB_ID(OT_ANIMOBJECT, 0x0e), 0xa, 0);
        AnimSetUpdateInterval(MOB_ID(OT_ANIMOBJECT, 0x0e), 0x14);
    }
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C2_OnCraneDone);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerPadLander);
}

// 0x10001c80  EV_TRIGGER_ENTER 0x40058 / 0x40059 (targeting pod 2): the pod is dropped on a pad
static void C2_OnTriggerPadPod(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig != C2_TRIGGER(0x58) && trig != C2_TRIGGER(0x59))
        return;
    Object_SetProperty(C2_POD, PROP_TARGET, -1);
    Object_SetProperty((ObjId)ev->arg[1], PROP_TARGET, -1);
    Object_SetDead((ObjId)ev->arg[1]);
    MobObj_Sleep(C2_MOB_POD);
    if ((ObjId)ev->arg[1] == C2_TRIGGER(0x58)) {
        Object_SetProperty(C2_TRIGGER(0x59), PROP_TARGET, C2_LANDER);
        MobObj_Wake(0x0c);
        st.pad = 1;
    } else {
        Object_SetProperty(C2_TRIGGER(0x58), PROP_TARGET, C2_LANDER);
        MobObj_Wake(0x0e);
        st.pad = 2;
    }
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerPadLander);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerPadPod);
}

// 0x10001d50  EV_POD_HOOKED (pod 2): both pads now wait for that pod
static void C2_OnPodHooked(const LanderEvent *ev)
{
    Object_SetProperty(C2_TRIGGER(0x58), PROP_TARGET, C2_POD);
    Object_SetProperty(C2_TRIGGER(0x59), PROP_TARGET, C2_POD);
    Pod_Script_SetupMass((ObjId)ev->arg[0], 1000);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    st.podHooked = 1;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerPadPod);
    st.hasPrisoner = 1;
    EventType_RemoveHandler(EV_POD_HOOKED, C2_OnPodHooked);
}

// 0x10001de0  EV_TRIGGER_ENTER 0x40056 (after the alarm): blast doors close, four guns rise
static void C2_OnTriggerBlastDoors(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x56))
        return;
    InTime3(3000, AnimPlay, C2_ANIM(0x27), 2, 0);
    InTime2(3000, Sound_PlayOnce, C2_ANIM(0x27), 0xab);
    InTime3(3100, AnimPlay, C2_ANIM(0x31), 2, 0);
    InTime2(3100, Sound_PlayOnce, C2_ANIM(0x31), 0xab);
    InTime3(3200, AnimPlay, C2_ANIM(0x28), 2, 0);
    InTime2(3200, Sound_PlayOnce, C2_ANIM(0x28), 0xab);
    InTime3(3000, AnimPlay, C2_ANIM(0x2a), 2, 0);
    InTime2(3000, Sound_PlayOnce, C2_ANIM(0x2a), 0xab);
    InTime3(3100, AnimPlay, C2_ANIM(0x32), 2, 0);
    InTime2(3100, Sound_PlayOnce, C2_ANIM(0x32), 0xab);
    InTime3(3200, AnimPlay, C2_ANIM(0x29), 2, 0);
    InTime2(3200, Sound_PlayOnce, C2_ANIM(0x29), 0xab);
    AnimSetUpdateInterval(C2_GUN(0x25), 0x1e);
    AnimSetUpdateInterval(C2_GUN(0x29), 0x1e);
    AnimSetUpdateInterval(C2_GUN(0x2a), 0x1e);
    AnimSetUpdateInterval(C2_GUN(0x28), 0x1e);
    AnimPlay(C2_GUN(0x25), 0x13, 0x60);
    AnimPlay(C2_GUN(0x29), 0x17, 0x60);
    AnimPlay(C2_GUN(0x2a), 0x18, 0x60);
    AnimPlay(C2_GUN(0x28), 0x16, 0x60);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerBlastDoors);
}

// 0x10001fa0  EV_TRIGGER_ENTER 0x40033 (after the alarm): gun 0x27 rises
static void C2_OnTriggerGun27(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x33))
        return;
    AnimPlay(C2_GUN(0x27), 0x15, 0x60);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerGun27);
}

// 0x10001fd0  EV_ANIM_FINISHED (after the alarm): the patrolling gun groups 0x21..24 / 0x16..19
// restart their patrol 100 ms later
static void C2_OnGunPatrolDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == C2_GUN(0x21)) {
        AnimStop(C2_GUN(0x21), 0);
        AnimStop(C2_GUN(0x22), 0);
        AnimStop(C2_GUN(0x23), 0);
        AnimStop(C2_GUN(0x24), 0);
        InTime3(100, AnimPlay, C2_GUN(0x21), 0xf, 0xe0);
        InTime3(100, AnimPlay, C2_GUN(0x22), 0x10, 0xe0);
        InTime3(100, AnimPlay, C2_GUN(0x23), 0x11, 0xe0);
        InTime3(100, AnimPlay, C2_GUN(0x24), 0x12, 0xe0);
    }
    if ((ObjId)ev->arg[0] == C2_GUN(0x16)) {
        AnimStop(C2_GUN(0x16), 0);
        AnimStop(C2_GUN(0x17), 0);
        AnimStop(C2_GUN(0x18), 0);
        AnimStop(C2_GUN(0x19), 0);
        InTime3(100, AnimPlay, C2_GUN(0x16), 0xb, 0xe0);
        InTime3(100, AnimPlay, C2_GUN(0x17), 0xc, 0xe0);
        InTime3(100, AnimPlay, C2_GUN(0x18), 0xd, 0xe0);
        InTime3(100, AnimPlay, C2_GUN(0x19), 0xe, 0xe0);
    }
}

// 0x10002120  the alarm (once): the droid arms, sirens, the guns wake and patrol, turrets 3..11 on
static void C2_Alarm(void)
{
    if (st.alarm)
        return;
    st.alarm = 1;
    Object_SetProperty(C2_DROID, PROP_STATE, 1);
    for (int32_t n = 0x18; n <= 0x23; n++)               // (unrolled) warning lights
        AnimPlay(C2_ANIM(n), 2, 0);
    MobObj_Wake(0x16);
    MobObj_Wake(0x17);
    MobObj_Wake(0x18);
    MobObj_Wake(0x19);
    for (int32_t n = 0x21; n <= 0x2a; n++)               // (unrolled)
        MobObj_Wake(n);
    for (int32_t n = 0x16; n <= 0x2a; n++)               // (unrolled)
        Sound_PlayLoop(C2_GUN(n), 0x84, -1);
    AnimSetUpdateInterval(C2_GUN(0x16), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x17), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x18), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x19), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x21), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x22), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x23), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x24), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x25), 0x14);
    AnimSetUpdateInterval(C2_GUN(0x26), 0x1e);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerBlastDoors);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C2_OnGunPatrolDone);
    AnimPlay(C2_GUN(0x16), 0xb, 0xe0);
    AnimPlay(C2_GUN(0x17), 0xc, 0xe0);
    AnimPlay(C2_GUN(0x18), 0xd, 0xe0);
    AnimPlay(C2_GUN(0x19), 0xe, 0xe0);
    AnimPlay(C2_GUN(0x21), 0xf, 0xe0);
    AnimPlay(C2_GUN(0x22), 0x10, 0xe0);
    AnimPlay(C2_GUN(0x23), 0x11, 0xe0);
    AnimPlay(C2_GUN(0x24), 0x12, 0xe0);
    AnimPlay(C2_GUN(0x26), 0x14, 0xe0);
    for (int32_t id = OBJ_ID(OT_TURRET, 3); id <= (int32_t)OBJ_ID(OT_TURRET, 0x0b); id++)   // (unrolled)
        Object_SetProperty((ObjId)id, PROP_STATE, 1);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerGun27);
}

// 0x10002580  EV_TRIGGER_ENTER 0x40053 (re-entering scan chamber 2): the entry doors close behind
// the lander and the scan restarts
static void C2_OnTriggerChamberReenter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x53))
        return;
    AnimPlay(C2_ANIM(0x2e), 2, 4);
    Sound_PlayOnce(C2_ANIM(0x27), 0xab);
    InTime3(64, AnimPlay, C2_ANIM(0x30), 2, 4);
    InTime2(64, Sound_PlayOnce, C2_ANIM(0x30), 0xab);
    InTime3(128, AnimPlay, C2_ANIM(0x2d), 2, 4);
    InTime2(128, Sound_PlayOnce, C2_ANIM(0x2d), 0xab);
    C2_Scanner2Wait();                                   // 0x10002d00
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerChamberReenter);
}

// 0x10002630  EV_TRIGGER_LEAVE 0x4004b (left scan chamber 2 towards the cargo side)
static void C2_OnTriggerLeaveToCargo(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x4b) || st.inScanner2 != 0)
        return;
    if (st.podHooked == 0)
        WindowSetText(st.win, st.text[C2_T_RETRIEVE_RETURN]);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerChamberReenter);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C2_OnTriggerLeaveToCargo);
}

// 0x10002690  EV_TRIGGER_LEAVE 0x40052 (left scan chamber 2 the other way)
static void C2_OnTriggerLeaveToExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x52) || st.inScanner2 != 0)
        return;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerChamberReenter);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C2_OnTriggerLeaveToExit);
}

// 0x100026d0  InTime: poll every second until the lander is back in scan chamber 2
static void C2_Scanner2Poll(void)
{
    if (st.inScanner2) {
        C2_Scanner2Wait();                               // (tail jump)
        return;
    }
    InTime0(1000, C2_Scanner2Poll);
}

// 0x10002700  EV_ANIM_FINISHED, anim 5 (the scanner's lights are back up after an abort)
static void C2_OnScanner2Reset(const LanderEvent *ev)
{
    if (ev->arg[1] != 5)
        return;
    C2_Scanner2Poll();
    EventType_RemoveHandler(EV_ANIM_FINISHED, C2_OnScanner2Reset);
}

static void C2_OnScanner2Turned(const LanderEvent *ev);
static void C2_OnScanner2Wrapped(const LanderEvent *ev);

// 0x10002720  EV_TRIGGER_LEAVE 0x40053 during the scan: abort it
static void C2_OnScanner2Abort(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x53))
        return;
    if (st.scanState == 2) {
        AnimPlay(C2_ANIM(0x47), 5, 4);
        Sound_PlayOnce(C2_ANIM(0x47), 0xd6);
        AnimPlay(C2_ANIM(0x48), 5, 4);
        AnimPlay(C2_ANIM(0x49), 5, 4);
        EventType_RemoveHandler(EV_ANIM_TURNED, C2_OnScanner2Turned);
    } else if (st.scanState == 3) {
        EventType_RemoveHandler(EV_ANIM_WRAPPED, C2_OnScanner2Wrapped);
    }
    MobObj_Sleep(0x10);
    MobObj_Sleep(0x11);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C2_OnScanner2Reset);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C2_OnScanner2Abort);
}

// 0x100027e0  InTime: the security messages after a failed scan
static void C2_SayLater(intptr_t n)
{
    if (n == 1)
        WindowSetText(st.win, st.text[C2_T_PRISONER]);
    if (n == 2)
        WindowSetText(st.win, st.text[C2_T_LETHAL]);
    if (n == 3)
        WindowSetText(st.win, st.text[C2_T_EXIT]);
}

// 0x10002840  EV_ANIM_WRAPPED, anim 5: scan chamber 2 finished scanning. With the prisoner on board
// the scan fails (alarm); either way one of the two door sets opens (alternating).
static void C2_OnScanner2Wrapped(const LanderEvent *ev)
{
    if (ev->arg[1] != 5)
        return;
    st.scanState = 4;
    if (st.hasPrisoner) {
        WindowSetText(st.win, st.text[C2_T_SCAN_FAILED]);
        InTime1(2000, C2_SayLater, 1);
        InTime1(4000, C2_SayLater, 2);
        InTime1(10000, C2_SayLater, 3);
        C2_Alarm();
        st.exitSide = 1 - st.exitSide;
    } else {
        MobObj_Sleep(0x10);
        MobObj_Sleep(0x11);
        MobObj_Wake(4);                                  // green lights for 2 s
        MobObj_Wake(5);
        InTime1(2000, MobObj_Sleep, 4);
        InTime1(2000, MobObj_Sleep, 5);
        WindowSetText(st.win, st.text[C2_T_SCANNED]);
    }
    if (st.exitSide != 0) {
        InTime3(2000, AnimPlay, C2_ANIM(0x2b), 2, 0);
        InTime2(2000, Sound_PlayOnce, C2_ANIM(0x2b), 0xab);
        InTime3(2100, AnimPlay, C2_ANIM(0x2f), 2, 0);
        InTime2(2100, Sound_PlayOnce, C2_ANIM(0x2f), 0xab);
        InTime3(2200, AnimPlay, C2_ANIM(0x2c), 2, 0);
        InTime2(2200, Sound_PlayOnce, C2_ANIM(0x2c), 0xab);
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnTriggerLeaveToExit);
        AnimPlay(C2_ANIM(0x26), 2, 4);
        Sound_PlayOnce(C2_ANIM(0x26), 0xab);
        InTime3(64, AnimPlay, C2_ANIM(0x24), 2, 4);
        InTime2(64, Sound_PlayOnce, C2_ANIM(0x24), 0xab);
        InTime3(128, AnimPlay, C2_ANIM(0x25), 2, 4);
        InTime2(128, Sound_PlayOnce, C2_ANIM(0x25), 0xab);
    } else {
        InTime3(2000, AnimPlay, C2_ANIM(0x2e), 2, 0);
        InTime2(2000, Sound_PlayOnce, C2_ANIM(0x2e), 0xab);
        InTime3(2100, AnimPlay, C2_ANIM(0x30), 2, 0);
        InTime2(2100, Sound_PlayOnce, C2_ANIM(0x30), 0xab);
        InTime3(2200, AnimPlay, C2_ANIM(0x2d), 2, 0);
        InTime2(2200, Sound_PlayOnce, C2_ANIM(0x2d), 0xab);
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnTriggerLeaveToCargo);
        AnimPlay(C2_ANIM(0x25), 2, 0);
        Sound_PlayOnce(C2_ANIM(0x27), 0xab);
        InTime3(64, AnimPlay, C2_ANIM(0x24), 2, 0);
        InTime2(64, Sound_PlayOnce, C2_ANIM(0x24), 0xab);
        InTime3(128, AnimPlay, C2_ANIM(0x26), 2, 0);
        InTime2(128, Sound_PlayOnce, C2_ANIM(0x27), 0xab);
    }
    st.exitSide = 1 - st.exitSide;
    EventType_RemoveHandler(EV_ANIM_WRAPPED, C2_OnScanner2Wrapped);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C2_OnScanner2Abort);
}

// 0x10002ba0  EV_ANIM_TURNED, anim 5: the scanner lights are down -> scanning
static void C2_OnScanner2Turned(const LanderEvent *ev)
{
    if (ev->arg[1] != 5)
        return;
    AnimPlay(C2_ANIM(0x47), 5, 4);
    Sound_PlayOnce(C2_ANIM(0x47), 0xda);
    AnimPlay(C2_ANIM(0x48), 5, 4);
    AnimPlay(C2_ANIM(0x49), 5, 4);
    st.scanState = 3;
    EventType_AddHandler(EV_ANIM_WRAPPED, NULL, C2_OnScanner2Wrapped);
    EventType_RemoveHandler(EV_ANIM_TURNED, C2_OnScanner2Turned);
}

// 0x10002c20  InTime: start scan chamber 2 if the lander is still inside
static void C2_Scanner2Scan(void)
{
    if (st.inScanner2) {
        WindowSetText(st.win, st.text[C2_T_SCANNING]);
        AnimPlay(C2_ANIM(0x47), 5, 0);
        Sound_PlayOnce(C2_ANIM(0x47), 0xda);
        AnimPlay(C2_ANIM(0x48), 5, 0);
        AnimPlay(C2_ANIM(0x49), 5, 0);
        st.scanState = 2;
        EventType_AddHandler(EV_ANIM_TURNED, NULL, C2_OnScanner2Turned);
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnScanner2Abort);
        return;
    }
    MobObj_Sleep(0x10);
    MobObj_Sleep(0x11);
    C2_Scanner2Poll();                                   // 0x100026d0
}

// 0x10002ce0  EV_TRIGGER_ENTER / LEAVE 0x40053: track the lander in scan chamber 2
static void C2_OnTriggerScanner2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == C2_TRIGGER(0x53))
        st.inScanner2 = ev->type == EV_TRIGGER_ENTER;
}

// 0x10002d00  scan chamber 2: lights on, scan in 2 s (or poll every second until the lander is in)
static void C2_Scanner2Wait(void)
{
    if (st.inScanner2) {
        MobObj_Wake(0x10);
        MobObj_Wake(0x11);
        InTime0(2000, C2_Scanner2Scan);
        st.scanState = 1;
        return;
    }
    InTime0(1000, C2_Scanner2Wait);
}

// 0x10002d60  EV_TRIGGER_ENTER 0x40053 (first time, escorted): doors close, scan chamber 2 starts
static void C2_OnTriggerChamberIn(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x53))
        return;
    EventType_RemoveHandler(EV_LANDER_FIRED, C2_OnFired);
    AnimPlay(C2_ANIM(0x2c), 2, 4);
    Sound_PlayOnce(C2_ANIM(0x2c), 0xab);
    InTime3(64, AnimPlay, C2_ANIM(0x2f), 2, 4);
    InTime2(64, Sound_PlayOnce, C2_ANIM(0x2f), 0xab);
    InTime3(128, AnimPlay, C2_ANIM(0x2b), 2, 4);
    InTime2(128, Sound_PlayOnce, C2_ANIM(0x2b), 0xab);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerScanner2);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnTriggerScanner2);
    st.inScanner2 = 1;
    C2_Scanner2Wait();
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerChamberIn);
}

// 0x10002e50  EV_TRIGGER_ENTER 0x40015 (in front of scan chamber 2)
static void C2_OnTriggerBeforeChamber(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x15))
        return;
    WindowSetText(st.win, st.text[C2_T_ENTER_SCANNER]);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerBeforeChamber);
}

// 0x10002e90  EV_ANIM_TURNED, anim 7: the droid reached the end of its path -> scan chamber 2's
// doors open and the escort ends
static void C2_OnDroidArrived(const LanderEvent *ev)
{
    if (ev->arg[1] != 7)
        return;
    AnimPlay(C2_ANIM(0x2b), 2, 0);
    Sound_PlayOnce(C2_ANIM(0x27), 0xab);
    InTime3(64, AnimPlay, C2_ANIM(0x2f), 2, 0);
    InTime2(64, Sound_PlayOnce, C2_ANIM(0x2f), 0xab);
    InTime3(128, AnimPlay, C2_ANIM(0x2c), 2, 0);
    InTime2(128, Sound_PlayOnce, C2_ANIM(0x27), 0xab);
    EventType_RemoveHandler(EV_ANIM_TURNED, C2_OnDroidArrived);
    C2_StopFollowCheck();
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerBeforeChamber);
}

// 0x10002f50  EV_TRIGGER_ENTER 0x40011 (targets the droid): the doors behind the escort close
static void C2_OnTriggerEscort2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x11))
        return;
    AnimPlay(C2_ANIM(0x26), 2, 4);
    Sound_PlayOnce(C2_ANIM(0x27), 0xab);
    InTime3(64, AnimPlay, C2_ANIM(0x24), 2, 4);
    InTime2(64, Sound_PlayOnce, C2_ANIM(0x27), 0xab);
    InTime3(128, AnimPlay, C2_ANIM(0x25), 2, 4);
    InTime2(128, Sound_PlayOnce, C2_ANIM(0x27), 0xab);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerChamberIn);
    st.escorted = 1;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerEscort2);
}

// 0x10003020  EV_TRIGGER_ENTER 0x40010 (targets the droid): the doors in front of it open
static void C2_OnTriggerEscort1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x10))
        return;
    AnimPlay(C2_ANIM(0x25), 2, 0);
    Sound_PlayOnce(C2_ANIM(0x27), 0xab);
    InTime3(64, AnimPlay, C2_ANIM(0x24), 2, 0);
    InTime2(64, Sound_PlayOnce, C2_ANIM(0x27), 0xab);
    InTime3(128, AnimPlay, C2_ANIM(0x26), 2, 0);
    InTime2(128, Sound_PlayOnce, C2_ANIM(0x27), 0xab);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerEscort2);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, C2_OnDroidArrived);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerEscort1);
}

// 0x100030f0  InTime: the follow check is now registered
static void C2_FollowStarted(void)
{
    st.followActive = 1;
}

// 0x10003100  EV_TICK_1S while escorted: stay within 5000 (|dx|+|dy|+|dz|) of the droid; warnings
// after 1..4 s away, a final warning at 8 s, the alarm at 11 s
static void C2_FollowTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *d = (const int32_t *)Object_GetProperty(C2_DROID, PROP_POSITION);
    const int32_t *l = (const int32_t *)Object_GetProperty(C2_LANDER, PROP_POSITION);
    int32_t dist = abs(d[2] - l[2]) + abs(d[1] - l[1]) + abs(d[0] - l[0]);
    st.strayTicks++;
    if (dist < 5000) {
        WindowSetText(st.win, st.text[C2_T_FOLLOW]);
        st.strayTicks = 0;
        return;
    }
    if (st.strayTicks < 5)
        WindowSetText(st.win, st.text[C2_T_TOO_FAR]);
    if (st.strayTicks == 8)
        WindowSetText(st.win, st.text[C2_T_FINAL_WARNING]);
    if (st.strayTicks == 11) {
        WindowSetText(st.win, st.text[C2_T_LETHAL]);
        AnimStop(C2_DROID, 0);
        C2_StopFollowCheck();
        C2_Alarm();
        EventType_AddHandler(EV_TICK_1S, NULL, C2_TurretZapCheck);
        EventType_RemoveHandler(EV_LANDER_FIRED, C2_OnFired);
    }
    if (st.escorted)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerChamberIn);
}

// 0x10003240  InTime (2 s after scan chamber 1 passed the lander): the droid starts its escort
static void C2_DroidStart(void)
{
    WindowSetText(st.win, st.text[C2_T_FOLLOW]);
    AnimPlay(C2_DROID, 7, 0xe0);
    AnimSetUpdateInterval(C2_DROID, 0x1e);
    Object_SetProperty(C2_TRIGGER(0x10), PROP_TARGET, C2_DROID);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerEscort1);
    InTime3(3000, EventType_AddHandler, EV_TICK_1S, 0, (intptr_t)C2_FollowTick);
    InTime0(3000, C2_FollowStarted);
}

static void C2_Scanner1Start(void);

// 0x100032e0  InTime: poll every second until the lander is back in scan chamber 1
static void C2_Scanner1Poll(void)
{
    if (st.inScanner1) {
        C2_Scanner1Start();                              // (tail jump to 0x10003600)
        return;
    }
    InTime0(1000, C2_Scanner1Poll);
}

// 0x10003310  EV_ANIM_FINISHED, anim 4: scan chamber 1 reset after an abort
static void C2_OnScanner1Reset(const LanderEvent *ev)
{
    if (ev->arg[1] != 4)
        return;
    C2_Scanner1Poll();
    EventType_RemoveHandler(EV_ANIM_FINISHED, C2_OnScanner1Reset);
}

static void C2_OnScanner1Turned(const LanderEvent *ev);
static void C2_OnScanner1Wrapped(const LanderEvent *ev);

// 0x10003330  EV_TRIGGER_LEAVE 0x4004a during the entry scan: abort it
static void C2_OnScanner1Abort(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x4a))
        return;
    if (st.scanState == 2) {
        AnimPlay(C2_ANIM(0x44), 4, 4);
        Sound_PlayOnce(C2_ANIM(0x44), 0xda);
        AnimPlay(C2_ANIM(0x45), 4, 4);
        AnimPlay(C2_ANIM(0x46), 4, 4);
        EventType_RemoveHandler(EV_ANIM_TURNED, C2_OnScanner1Turned);
    } else if (st.scanState == 3) {
        EventType_RemoveHandler(EV_ANIM_WRAPPED, C2_OnScanner1Wrapped);
    }
    MobObj_Sleep(0x12);
    MobObj_Sleep(0x13);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C2_OnScanner1Reset);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C2_OnScanner1Abort);
}

// 0x100033f0  EV_ANIM_WRAPPED, anim 4: the entry scan is done -> pass, the droid comes
static void C2_OnScanner1Wrapped(const LanderEvent *ev)
{
    if (ev->arg[1] != 4)
        return;
    st.scanState = 4;
    MobObj_Sleep(0x12);
    MobObj_Sleep(0x13);
    MobObj_Wake(6);
    MobObj_Wake(7);
    InTime1(2000, MobObj_Sleep, 6);
    InTime1(2000, MobObj_Sleep, 7);
    WindowSetText(st.win, st.text[C2_T_SCANNED]);
    InTime3(2000, AnimPlay, C2_ANIM(0x3f), 1, 0);
    InTime0(2000, C2_DroidStart);
    EventType_RemoveHandler(EV_ANIM_WRAPPED, C2_OnScanner1Wrapped);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C2_OnScanner1Abort);
}

// 0x100034c0  EV_ANIM_TURNED, anim 4: the lights of scan chamber 1 are down -> scanning
static void C2_OnScanner1Turned(const LanderEvent *ev)
{
    if (ev->arg[1] != 4)
        return;
    AnimPlay(C2_ANIM(0x44), 4, 4);
    Sound_PlayOnce(C2_ANIM(0x44), 0xda);
    AnimPlay(C2_ANIM(0x45), 4, 4);
    AnimPlay(C2_ANIM(0x46), 4, 4);
    st.scanState = 3;
    EventType_AddHandler(EV_ANIM_WRAPPED, NULL, C2_OnScanner1Wrapped);
    EventType_RemoveHandler(EV_ANIM_TURNED, C2_OnScanner1Turned);
}

// 0x10003540  InTime: start the entry scan if the lander is still in scan chamber 1
static void C2_Scanner1Scan(void)
{
    if (st.inScanner1) {
        AnimPlay(C2_ANIM(0x44), 4, 0);
        Sound_PlayOnce(C2_ANIM(0x44), 0xda);
        AnimPlay(C2_ANIM(0x45), 4, 0);
        AnimPlay(C2_ANIM(0x46), 4, 0);
        st.scanState = 2;
        EventType_AddHandler(EV_ANIM_TURNED, NULL, C2_OnScanner1Turned);
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnScanner1Abort);
        return;
    }
    MobObj_Sleep(0x12);
    MobObj_Sleep(0x13);
    C2_Scanner1Poll();                                   // 0x100032e0
}

// 0x100035e0  EV_TRIGGER_ENTER / LEAVE 0x4004a: track the lander in scan chamber 1
static void C2_OnTriggerScanner1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == C2_TRIGGER(0x4a))
        st.inScanner1 = ev->type == EV_TRIGGER_ENTER;
}

// 0x10003600  scan chamber 1: lights on, scan in 2 s
static void C2_Scanner1Start(void)
{
    MobObj_Wake(0x12);
    MobObj_Wake(0x13);
    st.inScanner1 = 1;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerScanner1);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnTriggerScanner1);
    InTime0(2000, C2_Scanner1Scan);
    st.scanState = 1;
}

// 0x10003670  EV_TRIGGER_ENTER / LEAVE 0x40040: the entrance door 0x3003e
static void C2_OnTriggerEntrance(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x40))
        return;
    AnimPlay(C2_ANIM(0x3e), 1, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
    Sound_PlayOnce(C2_ANIM(0x3e), 0xab);
}

// 0x100036b0  EV_TICK_1S in scan chamber 1: the welcome messages, then the entry scan.
// The original tests the zero flag of its increment, so the welcome line (count 0) never shows.
static void C2_WelcomeTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.welcomeTicks == 0)
        WindowSetText(st.win, st.text[C2_T_WELCOME]);
    if (st.welcomeTicks == 3)
        WindowSetText(st.win, st.text[C2_T_BETA]);
    if (st.welcomeTicks == 6)
        WindowSetText(st.win, st.text[C2_T_OFF_LIMITS]);
    if (st.welcomeTicks == 9) {
        WindowSetText(st.win, st.text[C2_T_SCANNING]);
        C2_Scanner1Start();
    }
    if (st.welcomeTicks == 10)
        EventType_RemoveHandler(EV_TICK_1S, C2_WelcomeTick);
}

// 0x10003760  EV_TRIGGER_ENTER 0x4004a (first time): the entrance closes for good, the welcome
// starts and firing is watched from now on
static void C2_OnTriggerScanner1First(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x4a))
        return;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerEntrance);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C2_OnTriggerEntrance);
    EventType_AddHandler(EV_TICK_1S, NULL, C2_WelcomeTick);
    EventType_AddHandler(EV_LANDER_FIRED, NULL, C2_OnFired);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerScanner1First);
}

// 0x100037e0  EV_ANIM_FINISHED: the crane stopped (anim 6 parking, 0x19 loading)
static void C2_OnCraneStopped(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != C2_CRANE(0x2b) || (ev->arg[1] != 6 && ev->arg[1] != 0x19))
        return;
    for (int32_t n = 0x2b; n <= 0x32; n++)               // (unrolled)
        AnimStop(C2_CRANE(n), 0);
}

// 0x10003870  the crane moves to its parking position (once)
static void C2_CranePark(void);

// 0x100037c0  EV_TRIGGER_ENTER 0x40016 / 0x40040: tail-jumps to C2_CranePark
static void C2_OnTriggerCrane(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig == C2_TRIGGER(0x16) || trig == C2_TRIGGER(0x40))
        C2_CranePark();
}

static void C2_CranePark(void)
{
    AnimSetUpdateInterval(C2_ANIM(0x34), 2);
    AnimSetUpdateInterval(C2_ANIM(0x35), 2);
    AnimPlay(C2_ANIM(0x34), 8, 0);
    Sound_PlayChain(C2_ANIM(0x34), 2, 0xce, 0xca);
    AnimPlay(C2_ANIM(0x35), 8, 0);
    Sound_PlayChain(C2_ANIM(0x35), 2, 0xce, 0xca);
    for (int32_t n = 0x2b; n <= 0x32; n++)               // (unrolled)
        InTime3(500, AnimPlay, C2_CRANE(n), 6, 0);
    InTime2(500, Sound_PlayOnce, C2_CRANE(0x2b), 0x8c);
    InTime2(500, Sound_PlayOnce, C2_CRANE(0x32), 0x8c);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C2_OnCraneStopped);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C2_OnTriggerCrane);
}

// 0x10003a00 / 0x10003a40  EV_TRIGGER_ENTER 0x4005a / 0x4005b (target: the crane): gate 0x30057
static void C2_OnTriggerGateA(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x5a))
        return;
    if (st.gateDir == 1) {
        AnimPlay(C2_ANIM(0x57), 9, 0);
        return;
    }
    AnimPlay(C2_ANIM(0x57), 9, 4);
}

static void C2_OnTriggerGateB(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C2_TRIGGER(0x5b))
        return;
    if (st.gateDir == 1) {
        AnimPlay(C2_ANIM(0x57), 9, 4);
        return;
    }
    AnimPlay(C2_ANIM(0x57), 9, 0);
}

// 0x10003b10  EV_PICKUP {pickup, player}
static void C2_OnPickup(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)C2_PICKUP(1) && id <= (int32_t)C2_PICKUP(2)) {
        int32_t shield = (int32_t)Object_GetProperty(C2_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(C2_LANDER, 50);
        Object_SetProperty(C2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    id = (int32_t)ev->arg[0];
    if (id >= (int32_t)C2_PICKUP(0x0c) && id <= (int32_t)C2_PICKUP(0x0e)) {
        Lander_SetCash(C2_LANDER, Lander_GetCash(C2_LANDER) + gMediumCash);
        Voice_CashAcquired(C2_LANDER);
    }
    if ((ObjId)ev->arg[0] == C2_PICKUP(0)) {
        if (Lander_AddMissiles(C2_LANDER, 2) == 0) {
            Voice_MissilesFull(C2_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(C2_LANDER);
    }
}

// 0x10003c00 / 0x10003c30  EV_TRIGGER_ENTER / LEAVE 0x40039..0x4003d: head light in the tunnels
static void C2_OnTriggerTunnelEnter(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig >= (int32_t)C2_TRIGGER(0x39) && trig <= (int32_t)C2_TRIGGER(0x3d))
        Lander_SetHeadLight(C2_LANDER, 0x31c, 0xccc);
}

static void C2_OnTriggerTunnelLeave(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig >= (int32_t)C2_TRIGGER(0x39) && trig <= (int32_t)C2_TRIGGER(0x3d))
        Lander_SetHeadLight(C2_LANDER, 0, 0);
}

// 0x10003c60  common set-up
static void C2_Setup(void)
{
    st.text = c2_text[gLanguage];
    for (int32_t n = 0x16; n <= 0x2a; n++) {
        Object_SetProperty(C2_GUN(n), PROP_ARMOUR, 0x3c);
        Object_SetProperty(C2_GUN(n), PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = OBJ_ID(OT_TURRET, 3); id <= (int32_t)OBJ_ID(OT_TURRET, 0x0b); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x17), PROP_ARMOUR, 10);
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x17), PROP_EXPLOSION, 0x50);
    SetGravity(12);
    gAirResistance = 0x14;
    gMainThrottlePower = 0x18;
    gNbWaypoints = c2_nbWaypoints;
    gWaypoints = c2_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[C2_T_FIND]);
    EventType_AddHandler(EV_POD_HOOKED, NULL, C2_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerEntrance);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnTriggerEntrance);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerScanner1First);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerCrane);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerGateA);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerGateB);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10003a80
    EventType_AddHandler(EV_PICKUP, NULL, C2_OnPickup);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C2_OnTriggerTunnelEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C2_OnTriggerTunnelLeave);
    MobObj_Wake(C2_MOB_POD);
    MobObj_Wake(C2_MOB_DROID);
    Sound_PlayLoop(C2_DROID, 0xd6, -1);
    for (int32_t n = 0x2b; n <= 0x32; n++)               // (unrolled) the crane
        MobObj_Wake(n);
    Sound_PlayLoop(C2_CRANE(0x2b), 0x8e, -1);
    Sound_PlayLoop(C2_CRANE(0x32), 0x8e, -1);
    Object_SetProperty(C2_TRIGGER(0x5a), PROP_TARGET, C2_CRANE(0x2b));
    Object_SetProperty(C2_TRIGGER(0x5b), PROP_TARGET, C2_CRANE(0x2b));
    for (int32_t id = OBJ_ID(OT_TURRET, 3); id <= (int32_t)OBJ_ID(OT_TURRET, 0x0b); id++)   // (unrolled)
        Object_SetProperty((ObjId)id, PROP_STATE, 0);
    Object_SetProperty(C2_DROID, PROP_STATE, 0);
    MissionScore_Init(C2_LANDER, 150000, 6);             // 0x10001520
    gTurretTypes[16] = &c2_turretType16;
    gTurretTypes[4] = &c2_turretType4;
    Media_AudioPlay(0);
    TexAnimPlay(0xa8, 1, 100);
    MobObj_Wake(0x15);                                   // AmbientFX
    MobObj_Wake(8);                                      // AmbientFX
}

// 0x10003f70  LevelStartLevel_Lander (export 0x10001010)
static void C2_StartLander(void)
{
    MobObj_Wake(C2_MOB_LANDER);
    MobObj_Wake(C2_MOB_CAMERA);
    Object_SetProperty(C2_CAMERA, PROP_TARGET, C2_LANDER);
    C2_Setup();
}

// 0x10003fb0  LevelStartLevel_Flycam (export 0x10001020)
static void C2_StartFlycam(void)
{
    C2_Setup();
    MobObj_Wake(C2_MOB_FLYCAM);
}

// 0x10003fd0  LevelStopLevel (export 0x10001030): empty
static void C2_Stop(void)
{
}

// 0x10001040
static uint32_t C2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel: two calls of the empty 0x10003fd0, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t C2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.gateDir = 1;
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText c2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10009160, .count = 3 * 19, .out = c2_text },
    { 0 },
};

const mission_script_t mission_callisto_2 = {
    .name          = "CALLISTO\\2",
    .get_version   = C2_GetVersion,
    .load          = C2_Load,
    .start_lander  = C2_StartLander,
    .start_flycam  = C2_StartFlycam,
    .start_animcam = NULL,
    .stop          = C2_Stop,
    .text          = c2_gametext,
};
