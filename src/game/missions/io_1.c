// MISSIONS\IO\1\MISSIONR.DLL - faithful C port.
//
// Mission: a raid on an Io base with "Alpha One", an escort whose craft is a chain of AnimTurret
// mobs (5 -> 8 -> 9 -> 0xa -> 0xb -> 0xd) that each fly a path and wait (trigger + anim-finished
// flag pairs) for the lander at every stop. Alpha Two (mobs 0x11 / 0x14 / 0x15) splits off. On the
// way: the base gates (shooting Alpha One closes them), a convoy blocker (0x800c9), a bridge
// switch (0xa008d), a crane brought down by its generators (0xf004d..54), steam vents (-2 shield per
// 100 ms unless their valve is shut), the lava chamber (-1 shield per second until the heat shield
// pickup 0x130058), a gate guarded by four guns, a lift, a piston trap with guns, and four valves
// (switches 0xa0062..65, each re-opens 14 s after being shut): all four shut at once stops the
// steam and frees the cargo pod 0x10c0020. Entering the core room (0x4005f) starts the evac check:
// above y = 10000, +80000 cash, result 2 if the mission timer is <= 900 s, else 1. Then the base
// alarm wakes the garrison. Par time 30 s, 8 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10005bd0), LevelStartLevel_Flycam and LevelStopLevel
// 0x10001020 (-> the empty 0x10001040: the fly-cam start does no set-up), LevelGetVersion
// 0x10001030. The score library (0x10001050-0x100015f4) is in mission_lib.c; the destroyed-sound
// handler 0x10004520 is MissionLib_OnDestroyedSound.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define I1_LANDER        MOB_ID(OT_LANDER, 0x1f)         // 0x0101001f
#define I1_CAMERA        MOB_ID(OT_CAMERA, 4)            // 0x01060004
#define I1_POD           MOB_ID(OT_POD, 0x20)            // 0x010c0020 the cargo
#define I1_GUIDE_5       MOB_ID(OT_ANIMTURRET, 5)        // 0x010e0005 Alpha One, first leg
#define I1_GUIDE_8       MOB_ID(OT_ANIMTURRET, 8)        // 0x010e0008
#define I1_GUIDE_A       MOB_ID(OT_ANIMTURRET, 9)        // 0x010e0009
#define I1_GUIDE_B       MOB_ID(OT_ANIMTURRET, 0x0a)     // 0x010e000a
#define I1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define I1_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define I1_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define I1_ATURRET(n)    OBJ_ID(OT_ANIMTURRET, (n))      // 0x000e00nn
#define I1_ADESTR(n)     OBJ_ID(OT_ANIMDESTRUCTIBLE, (n)) // 0x000f00nn
#define I1_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define I1_MOB_LANDER    0x1f
#define I1_MOB_CAMERA    4

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  i1_nbWaypoints = 1;                // 0x1000a160
static const Waypoint i1_waypoints[1] = {                // 0x1000a168
    { -18432, 19584 },
};
// 0x1000a170: [language][30] string pointers
static const char *i1_text[3][30];
static const TurretType i1_turretType21 = {              // 0x1000a2d8 -> gTurretTypes[21]
    { 0, 53, 192, 0 }, 0x05, 2, 4, 3, 50, 0xff, 1024, 16000, 0
};
static const TurretType i1_turretType15 = {              // 0x1000a2f8 -> gTurretTypes[15]
    { 61, 0, 198, 0 }, 0x05, 2, 4, 5, 100, 0x10, 800, 8192, 0
};
static const TurretType i1_turretTypeUnused = {          // 0x1000a318: not referenced
    { 0, 18, 158, 0 }, 0x05, 0, 4, 5, 100, 0x18, 800, 12000, 0
};
static const TurretType i1_turretType0 = {               // 0x1000a338 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 80, 0x18, 1024, 12000, 0
};
static const TurretType i1_turretType2 = {               // 0x1000a358 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 1, 0, 85, 0x18, 1024, 12000, 0
};

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      mastPair[3];     // 0x1000a37c / a378 / a374 (.data = 1) mast pairs 43/44, 45/46, 47/48
    Window      *win;             // 0x1000df10 objective line
    int32_t      convoyWaiting;   // 0x1000df14
    int32_t      blockerDown;     // 0x1000df18
    int32_t      guideAtGates;    // 0x1000df1c
    int32_t      landerAtGates;   // 0x1000df20
    int32_t      innerGates;      // 0x1000df24 the inner gates are (being) opened
    int32_t      craneDown;       // 0x1000df28 (set by the generators and by the crane's fall)
    int32_t      guideAtCrane;    // 0x1000df2c
    int32_t      guideAtLift;     // 0x1000df30
    int32_t      landerAtLift;    // 0x1000df34
    int32_t      landerAtGate;    // 0x1000df38
    int32_t      guideAtGate;     // 0x1000df3c
    int32_t      gateGunsDown;    // 0x1000df40
    int32_t      landerAtDoor;    // 0x1000df44
    int32_t      guideAtDoor;     // 0x1000df48
    int32_t      guideWaiting;    // 0x1000df4c
    int32_t      landerAtGuide;   // 0x1000df50
    int32_t      inSteam;         // 0x1000df54
    int32_t      coreTicks;       // 0x1000df58
    int32_t      waveTicks;       // 0x1000df5c
    int32_t      escortIn[3];     // 0x1000df60..68
    int32_t      timer;           // 0x1000df6c seconds
    int32_t      fieldOff;        // 0x1000df80
    int32_t      guideBStarted;   // 0x1000df8c
    int32_t      valve[4];        // 0x1000dfc0..cc valves 0x30022..25 (0 open, 1..14 s shut)
    int32_t      coreRoom;        // 0x1000dfd0 written, never read
    int32_t      pylonDown[4];    // 0x1000dfd4..e0 generators 0xf0052, 53, 54, 51 destroyed
    int32_t      generatorHits;   // 0x1000dfe4
    int32_t      generatorLock;   // 0x1000dfe8 never written
    int32_t      gateSwitched;    // 0x1000dfec
    int32_t      word1000e038;    // 0x1000e038 read as "valve" of trigger 0x40021, never written
    int32_t      startShield;     // 0x1000e428 written, never read
    const char *const *text;      // 0x1000e42c = i1_text[gLanguage]
} st;

static void I1_OnGateDown(const LanderEvent *ev);
static void I1_OnGateStep2(const LanderEvent *ev);

// 0x10001600  EV_ANIM_FINISHED 0x300d4 (the lift gate closed after the guard came down): the gate
// machinery 0x300c3..c6 / 0x300d5 runs back, the guard (mob 0x10e0009) leaves
static void I1_OnGateClosed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_ANIM(0xd4))
        return;
    AnimPlay(I1_ANIM(0xc3), 0x12, 0x84);
    AnimPlay(I1_ANIM(0xd5), 0x11, 0x84);
    AnimPlay(I1_ANIM(0xc4), 0x19, 0x84);
    AnimPlay(I1_ANIM(0xc5), 0x18, 0x84);
    AnimPlay(I1_ANIM(0xc6), 0x17, 0x84);
    AnimPlay(I1_GUIDE_A, 0x11, 0xe4);
    AnimSetUpdateInterval(I1_GUIDE_A, 0x96);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGateClosed);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGateDown);
    Sound_PlayChain(I1_ANIM(0xd5), 6, 0x25a, 0x25b, 0x25b, 0x25b, 0x25b, 0x25c);
}

// 0x100016f0  EV_ANIM_FINISHED 0x300d5 anim 0x11
static void I1_OnGateDown(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x11 || (ObjId)ev->arg[0] != I1_ANIM(0xd5))
        return;
    AnimStop(I1_GUIDE_A, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGateDown);
    AnimPlay(I1_ANIM(0xd2), 0xd, 0x80);
    InTime3(200, AnimPlay, I1_GUIDE_A, 0x49, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_A, 0x32);
    Sound_PlayOnce(I1_ANIM(0xd2), 0xaa);
}

// 0x10001790  EV_ANIM_FINISHED 0x300c3: second stage of the gate machinery
static void I1_OnGateStep1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_ANIM(0xc3))
        return;
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGateStep1);
    AnimPlay(I1_ANIM(0xc3), 0x12, 0x80);
    AnimPlay(I1_ANIM(0xc4), 0x19, 0x80);
    AnimPlay(I1_ANIM(0xc5), 0x18, 0x80);
    AnimPlay(I1_ANIM(0xc6), 0x17, 0x80);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGateStep2);
}

// 0x10001810  EV_ANIM_FINISHED 0x300c3: the gate 0x300d4 rises with four guns
static void I1_OnGateStep2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_ANIM(0xc3))
        return;
    WindowSetText(st.win, st.text[18]);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGateStep2);
    AnimPlay(I1_ANIM(0xd4), 0x13, 0x80);
    AnimPlay(I1_ATURRET(0xa6), 0x13, 0x60);
    AnimPlay(I1_ATURRET(0xa7), 0x13, 0x60);
    AnimPlay(I1_ATURRET(0xa8), 0x13, 0x60);
    AnimPlay(I1_ATURRET(0xa9), 0x13, 0x60);
    Object_SetProperty(I1_ATURRET(0xa6), PROP_STATE, 1);
    Object_SetProperty(I1_ATURRET(0xa7), PROP_STATE, 1);
    Object_SetProperty(I1_ATURRET(0xa8), PROP_STATE, 1);
    Object_SetProperty(I1_ATURRET(0xa9), PROP_STATE, 1);
    Sound_PlayOnce(I1_ANIM(0xd4), 0xbe);
}

// 0x100018f0  EV_TRIGGER_ENTER: inside/outside lighting overrides and two doors that close
static void I1_OnTriggerMisc(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0xd0))
        gInOutOverride = 1;
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0x77))
        gInOutOverride = 0;
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0x5f))
        gInOutOverride = 1;
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0x14)) {
        AnimPlay(I1_ANIM(0x5b), 0xd, 4);
        Sound_PlayOnce(I1_ANIM(0x5b), 0xaa);
    }
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0x93)) {
        AnimPlay(I1_ANIM(0x60), 0xd, 4);
        AnimPlay(I1_ANIM(0x61), 0xd, 0);
        Sound_PlayOnce(I1_ANIM(0x60), 0xaa);
    }
}

// 0x100019a0  EV_DESTROYED: the generators and pylons
static void I1_OnDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == I1_ADESTR(0x51))
        st.pylonDown[3] = 1;
    if ((ObjId)ev->arg[0] == I1_ADESTR(0x52))
        st.pylonDown[0] = 1;
    if ((ObjId)ev->arg[0] == I1_ADESTR(0x53))
        st.pylonDown[1] = 1;
    if ((ObjId)ev->arg[0] == I1_ADESTR(0x54))
        st.pylonDown[2] = 1;
    int32_t id = (int32_t)ev->arg[0];
    // generators 0xf004d..54: the first one destroyed brings the crane 0xf0016..18 down, the rest
    // of the set goes with it (survivors play their wreck anim) and turrets 0xa0 / 0xa1 blow
    if (id >= (int32_t)I1_ADESTR(0x4d) && id <= (int32_t)I1_ADESTR(0x54) && ++st.generatorHits == 1 &&
        st.generatorLock == 0) {
        AnimPlay(I1_ADESTR(0x16), 0x2f, 0x80);
        AnimPlay(I1_ADESTR(0x17), 0x2c, 0x80);
        AnimPlay(I1_ADESTR(0x18), 0x2b, 0x80);
        Object_SetDead(I1_ADESTR(0x4d));
        Object_SetDead(I1_ADESTR(0x51));
        Object_SetDead(I1_ADESTR(0x52));
        Object_SetDead(I1_ADESTR(0x53));
        Object_SetDead(I1_ADESTR(0x54));
        st.craneDown = 1;
        if (st.pylonDown[3] == 0)
            AnimPlay(I1_ADESTR(0x51), 0x2e, 0);
        if (st.pylonDown[0] == 0)
            AnimPlay(I1_ADESTR(0x52), 0x2e, 0);
        if (st.pylonDown[1] == 0)
            AnimPlay(I1_ADESTR(0x53), 0x2e, 0);
        if (st.pylonDown[2] == 0)
            AnimPlay(I1_ADESTR(0x54), 0x2e, 0);
        Object_SetDead(I1_ANIM(0x4e));
        InTime3(500, Object_SetProperty, OBJ_ID(OT_TURRET, 0xa0), PROP_HITPOINTS, 0);
        InTime3(500, Object_SetProperty, OBJ_ID(OT_TURRET, 0xa1), PROP_HITPOINTS, 0);
        Sound_PlayOnce(I1_ADESTR(0x17), 0x258);
    }
    // three pairs of masts: the second of a pair destroyed plays both wreck anims
    id = (int32_t)ev->arg[0];
    if (id == (int32_t)I1_ADESTR(0x43) || id == (int32_t)I1_ADESTR(0x44)) {
        if (st.mastPair[0] == 1) {
            AnimPlay(I1_ADESTR(0x43), 0x2d, 0x80);
            AnimPlay(I1_ADESTR(0x44), 0x2e, 0x80);
        }
        st.mastPair[0] = 1;
    }
    id = (int32_t)ev->arg[0];
    if (id == (int32_t)I1_ADESTR(0x45) || id == (int32_t)I1_ADESTR(0x46)) {
        if (st.mastPair[1] == 1) {
            AnimPlay(I1_ADESTR(0x45), 0x2d, 0x80);
            AnimPlay(I1_ADESTR(0x46), 0x2e, 0x80);
        }
        st.mastPair[1] = 1;
    }
    id = (int32_t)ev->arg[0];
    if (id == (int32_t)I1_ADESTR(0x47) || id == (int32_t)I1_ADESTR(0x48)) {
        if (st.mastPair[2] == 1) {
            AnimPlay(I1_ADESTR(0x47), 0x2d, 0x80);
            AnimPlay(I1_ADESTR(0x48), 0x2e, 0x80);
        }
        st.mastPair[2] = 1;
    }
    // the dish 0xf0049..4c / 0xf0030..31: any part destroyed brings it all down
    id = (int32_t)ev->arg[0];
    if (id == (int32_t)I1_ADESTR(0x49) || id == (int32_t)I1_ADESTR(0x4a) || id == (int32_t)I1_ADESTR(0x4b) ||
        id == (int32_t)I1_ADESTR(0x4c) || id == (int32_t)I1_ADESTR(0x30) || id == (int32_t)I1_ADESTR(0x31)) {
        InTime3(1000, Object_SetProperty, OBJ_ID(OT_TURRET, 0x9e), PROP_HITPOINTS, 0);
        InTime3(1000, Object_SetProperty, OBJ_ID(OT_TURRET, 0x9f), PROP_HITPOINTS, 0);
        AnimPlay(I1_ADESTR(0x49), 0x2d, 0x80);
        AnimPlay(I1_ADESTR(0x4a), 0x2e, 0x80);
        AnimPlay(I1_ADESTR(0x4b), 0x2d, 0x80);
        AnimPlay(I1_ADESTR(0x4c), 0x2e, 0x80);
        AnimPlay(I1_ADESTR(0x30), 0x29, 0);
        AnimPlay(I1_ADESTR(0x31), 0x2a, 0);
        Sound_PlayOnce(I1_ADESTR(0x30), 0x259);
    }
}

// 0x10001d00  EV_SWITCH
static void I1_OnSwitch(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == I1_SWITCH(0x91)) {        // lift gate (once; afterwards stays on)
        if (st.gateSwitched == 1) {
            Object_SetProperty(I1_SWITCH(0x91), PROP_STATE, 1);
        } else {
            st.gateSwitched = 1;
            AnimPlay(I1_ANIM(0xd4), 0x13, 4);
            Sound_PlayOnce(I1_ANIM(0xd3), 0xaa);
            Sound_PlayOnce(I1_ANIM(0xd4), 0xbe);
            Object_SetDead(I1_ATURRET(0xa6));
            Object_SetDead(I1_ATURRET(0xa7));
            Object_SetDead(I1_ATURRET(0xa8));
            Object_SetDead(I1_ATURRET(0xa9));
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGateClosed);
        }
    }
    if ((ObjId)ev->arg[0] == I1_SWITCH(0x59) && st.fieldOff == 0) {
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x9a));      // force field
        st.fieldOff = 1;
    }
    if ((ObjId)ev->arg[0] == I1_SWITCH(0x8e)) {
        AnimPlay(I1_ANIM(0x5b), 0xd, 0);
        Sound_PlayOnce(I1_ANIM(0x5b), 0xaa);
    }
    if ((ObjId)ev->arg[0] == I1_SWITCH(0x8f) && ev->arg[1] == 1) {
        AnimPlay(I1_ANIM(0x0c), 0xe, 0);
        AnimPlay(I1_ANIM(0x0d), 0xf, 0);
        AnimPlay(I1_ANIM(0x0e), 0xe, 4);
        AnimPlay(I1_ANIM(0x0f), 0xe, 4);
        AnimPlay(I1_ANIM(0x5c), 0xd, 0);
        Sound_PlayOnce(I1_ANIM(0x5c), 0xaa);
        Sound_PlayOnce(I1_ANIM(0x0c), 0xda);
        Sound_PlayOnce(I1_ANIM(0x0f), 0xda);
    }
    if ((ObjId)ev->arg[0] == I1_SWITCH(0xc2)) {
        AnimPlay(I1_ANIM(0x26), 1, 0);
        Sound_PlayOnce(I1_ANIM(0x5c), 0xa9);
    }
}

// 0x10001eb0  EV_TICK_1S (after the core is opened): above 10000 -> success; 2 = done within
// 900 one-second ticks of the timer 0x1000df6c, 1 = later
static void I1_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(I1_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    if (st.timer > 900) {
        Lander_SetCash(I1_LANDER, Lander_GetCash(I1_LANDER) + 80000);
        MissionScore_End(1);                             // 0x100013a0
    } else {
        Lander_SetCash(I1_LANDER, Lander_GetCash(I1_LANDER) + 80000);
        MissionScore_End(2);
    }
    EventType_RemoveHandler(EV_TICK_1S, I1_EvacTick);
}

// 0x10001f40  EV_ANIM_FINISHED anim 0x27 (after the pumps blew): the guards 0x10e0013..15 attack
// for 13 s
static void I1_OnPumpsDown(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x27)
        return;
    Object_SetDead(MOB_ID(OT_ANIMTURRET, 0x11));
    MobObj_Wake(0x13);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x13), PROP_STATE, 0);
    InTime3(500, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x13), 0x3f, 0xe0);
    InTime3(200, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x14), 0x40, 0xe0);
    InTime3(1500, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x15), 0x41, 0xe0);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x13), 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x14), 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x15), 0x28);
    InTime1(13000, MobObj_Sleep, 0x13);
    InTime1(13000, Object_SetDead, MOB_ID(OT_ANIMTURRET, 0x14));
    InTime1(13000, Object_SetDead, MOB_ID(OT_ANIMTURRET, 0x15));
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnPumpsDown);
}

// 0x10002050  EV_DESTROYED: one of the masts 0xf0043..48 -> the whole pumping station goes
static void I1_OnMastDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id != (int32_t)I1_ADESTR(0x43) && id != (int32_t)I1_ADESTR(0x44) && id != (int32_t)I1_ADESTR(0x45) &&
        id != (int32_t)I1_ADESTR(0x46) && id != (int32_t)I1_ADESTR(0x47) && id != (int32_t)I1_ADESTR(0x48))
        return;
    for (int32_t n = 0x43; n <= 0x48; n++)               // (unrolled)
        InTime3(500, Object_SetProperty, I1_ADESTR(n), PROP_HITPOINTS, 0);
    InTime3(2000, Object_SetProperty, OBJ_ID(OT_TURRET, 0xb0), PROP_HITPOINTS, 0);
    for (int32_t n = 0x43; n <= 0x47; n++)               // (unrolled)
        InTime1(500, Object_SetDead, I1_ADESTR(n));
    InTime1(500, Object_SetDead, OBJ_ID(OT_DESTRUCTIBLE, 0xca));
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xb0), PROP_HITPOINTS, 0);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0xca), PROP_HITPOINTS, 0);
    InTime3(500, AnimPlay, I1_ANIM(0x96), 0x27, 0);
    InTime3(500, AnimPlay, I1_ANIM(0x97), 0x28, 0);
    InTime2(500, Sound_PlayOnce, I1_ANIM(0x96), 0x258);
    AnimSetUpdateInterval(I1_ANIM(0x96), 0xf);
    AnimSetUpdateInterval(I1_ANIM(0x97), 0xf);
    InTime3(500, AnimPlay, I1_ADESTR(0x48), 0x2d, 0);
    AnimSetUpdateInterval(I1_ADESTR(0x48), 0xc);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnPumpsDown);
    EventType_RemoveHandler(EV_DESTROYED, I1_OnMastDestroyed);
}

// 0x10002280  EV_TICK_1S: a closed valve 0x30022..25 re-opens 14 s after it was shut
static void I1_ValveTick(const LanderEvent *ev)
{
    (void)ev;
    for (int32_t i = 0; i < 4; i++) {
        if (st.valve[i] < 1)
            continue;
        if (++st.valve[i] == 15) {
            Object_SetAlive(I1_ANIM(0x22 + i));
            st.valve[i] = 0;
        }
    }
}

// 0x100022d0  EV_TICK_100MS in a steam vent: -2 shield
static void I1_SteamTick(const LanderEvent *ev)
{
    (void)ev;
    Object_SetProperty(I1_LANDER, PROP_HITPOINTS, Object_GetProperty(I1_LANDER, PROP_HITPOINTS) - 2);
}

// 0x10002300  EV_TRIGGER_ENTER / LEAVE 0x40003..06 and 0x40021: the steam vents (a vent whose
// valve is shut does not hurt). The original indexes its valve array with trigger - 0x40003, so
// 0x40021 reads the word 0x1000e038, 30 ints past the array (zero: always hurts).
static void I1_OnTriggerSteam(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig != (int32_t)I1_TRIGGER(3) && trig != (int32_t)I1_TRIGGER(4) && trig != (int32_t)I1_TRIGGER(5) &&
        trig != (int32_t)I1_TRIGGER(6) && trig != (int32_t)I1_TRIGGER(0x21))
        return;
    int32_t shut = trig <= (int32_t)I1_TRIGGER(6) ? st.valve[trig - (int32_t)I1_TRIGGER(3)] : st.word1000e038;
    if (ev->type == EV_TRIGGER_ENTER && shut == 0) {
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I1_OnTriggerSteam);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerSteam);
        EventType_AddHandler(EV_TICK_100MS, NULL, I1_SteamTick);
        st.inSteam = 1;
    }
    if (ev->type == EV_TRIGGER_LEAVE) {
        EventType_RemoveHandler(EV_TICK_100MS, I1_SteamTick);
        EventType_RemoveHandler(EV_TRIGGER_LEAVE, I1_OnTriggerSteam);
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerSteam);
        st.inSteam = 0;
    }
}

// 0x100023c0  EV_TRIGGER_ENTER 0x400cd..cf (they target the escort guns 0x10e0026..28)
static void I1_OnTriggerEscort(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0xcd))
        st.escortIn[0] = 1;
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0xce))
        st.escortIn[1] = 1;
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0xcf))
        st.escortIn[2] = 1;
}

// 0x10002400  EV_TICK_1S: 15 s after the base alarm the second wave comes
static void I1_WaveTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.waveTicks != 15)
        return;
    AnimPlay(MOB_ID(OT_ANIMOBJECT, 0x21), 0x35, 0x80);
    if (st.escortIn[0] == 1)
        InTime1(200, MobObj_Sleep, 0x26);
    if (st.escortIn[1] == 1)
        InTime1(200, MobObj_Sleep, 0x27);
    if (st.escortIn[2] == 1)
        InTime1(200, MobObj_Sleep, 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMOBJECT, 0x21), 0xf);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMOBJECT, 0x1b), 0xf);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0), 0xf);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 1), 0xf);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 2), 0xf);
    AnimSetUpdateInterval(MOB_ID(OT_LIGHTOBJECT, 0x29), 0xf);
    AnimSetUpdateInterval(MOB_ID(OT_LIGHTOBJECT, 0x1d), 0xf);
    AnimSetUpdateInterval(MOB_ID(OT_LIGHTOBJECT, 0x22), 0xf);
    AnimPlay(MOB_ID(OT_ANIMOBJECT, 0x1b), 0x31, 0x80);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0), 0x32, 0xe0);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 1), 0x33, 0xe0);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 2), 0x34, 0xe0);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x10), 0x56, 0xe0);
    AnimPlay(MOB_ID(OT_LIGHTOBJECT, 0x29), 0x62, 0x60);
    AnimPlay(MOB_ID(OT_LIGHTOBJECT, 0x1d), 0x63, 0x60);
    AnimPlay(MOB_ID(OT_LIGHTOBJECT, 0x22), 0x64, 0x60);
    Sound_PlayOnce(MOB_ID(OT_ANIMOBJECT, 0x1b), 0x8c);
    WindowSetText(st.win, st.text[28]);
}

// 0x100025a0  EV_TRIGGER_ENTER 0x40092: the base alarm - the whole garrison wakes
static void I1_OnTriggerAlarm(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x92))
        return;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerAlarm);
    MobObj_Wake(0x17);
    MobObj_Wake(0x16);
    MobObj_Wake(0x26);
    MobObj_Wake(0x27);
    MobObj_Wake(0x28);
    MobObj_Wake(0x1b);
    MobObj_Wake(0x29);
    MobObj_Wake(0x1d);
    MobObj_Wake(0x22);
    MobObj_Wake(0);
    MobObj_Wake(1);
    MobObj_Wake(2);
    Object_SetProperty(I1_TRIGGER(0xcd), PROP_TARGET, MOB_ID(OT_ANIMTURRET, 0x26));
    Object_SetProperty(I1_TRIGGER(0xce), PROP_TARGET, MOB_ID(OT_ANIMTURRET, 0x27));
    Object_SetProperty(I1_TRIGGER(0xcf), PROP_TARGET, MOB_ID(OT_ANIMTURRET, 0x28));
    MobObj_Wake(0x21);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x17), PROP_TARGET, MOB_ID(OT_ANIMTURRET, 0x27));
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x16), PROP_TARGET, MOB_ID(OT_ANIMTURRET, 0x26));
    WindowSetText(st.win, st.text[23]);
    InTime3(200, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x26), 0x59, 0xe0);
    InTime3(200, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x27), 0x5a, 0xe0);
    InTime3(200, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x28), 0x5b, 0xe0);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x26), 0x1e);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x27), 0x1e);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x28), 0x1e);
    EventType_AddHandler(EV_TICK_1S, NULL, I1_WaveTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerEscort);
}

// 0x10002740  EV_TICK_1S (after the core room): at the 9th second the core machines stop, the
// core 0x8003a blows and the way to the alarm trigger opens
static void I1_CoreTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.coreTicks == 8) {
        Sound_StopObject(I1_ANIM(0x1c));
        Sound_StopObject(I1_ANIM(0x1d));
        AnimStop(I1_ANIM(0x1c), 0);
        AnimStop(I1_ANIM(0x1d), 0);
        AnimStop(I1_ANIM(0x1f), 0);
        AnimStop(I1_ANIM(0x20), 0);
        WindowSetText(st.win, st.text[19]);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x3a), PROP_HITPOINTS, 0);
        EventType_RemoveHandler(EV_TICK_1S, I1_CoreTick);
        MobObj_Wake(0x12);
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x12), PROP_STATE, 0);
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerAlarm);
    }
    st.coreTicks++;
}

// 0x10002810  EV_TRIGGER_ENTER 0x4005f: the core room - machines start, the evac check runs
static void I1_OnTriggerCoreRoom(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x5f))
        return;
    InTime3(2000, AnimPlay, I1_ANIM(0x5d), 0x1c, 4);
    AnimSetUpdateInterval(I1_ANIM(0x1c), 0x1f4);
    AnimSetUpdateInterval(I1_ANIM(0x1f), 0x8a);
    AnimSetUpdateInterval(I1_ANIM(0x1d), 0x1f4);
    AnimSetUpdateInterval(I1_ANIM(0x20), 0x8a);
    AnimPlay(I1_ANIM(0x1c), 0x23, 0x60);
    AnimPlay(I1_ANIM(0x1d), 0x23, 0x60);
    AnimPlay(I1_ANIM(0x1f), 0x22, 0xe0);
    AnimPlay(I1_ANIM(0x20), 0x22, 0xe0);
    Sound_PlayLoop(I1_ANIM(0x1c), 0xbc, 0x0fffffff);
    Sound_PlayLoop(I1_ANIM(0x1d), 0xbc, 0x0fffffff);
    st.coreRoom = 1;
    EventType_AddHandler(EV_TICK_1S, NULL, I1_CoreTick);
    EventType_AddHandler(EV_TICK_1S, NULL, I1_EvacTick);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerCoreRoom);
}

static void I1_OnPodHooked(const LanderEvent *ev);

// 0x10002930  EV_SWITCH 0xa0062..65 on: shut valve 0x30022..25 (re-opens after 14 s); all four
// shut at once -> the steam stops, the reactor seal 0x30072 opens, the heat-shield pod appears
static void I1_OnSwitchValve(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id != (int32_t)I1_SWITCH(0x62) && id != (int32_t)I1_SWITCH(0x63) && id != (int32_t)I1_SWITCH(0x64) &&
        id != (int32_t)I1_SWITCH(0x65))
        return;
    if (ev->arg[1] != 1)
        return;
    int32_t i = id - (int32_t)I1_SWITCH(0x62);
    if (st.valve[i] == 0)
        Object_SetDead((ObjId)(id - 0x70040));            // valve 0x30022 + i
    st.valve[i] = 1;
    Object_SetProperty((ObjId)ev->arg[0], PROP_STATE, 0);
    if (st.valve[0] < 1 || st.valve[1] < 1 || st.valve[2] < 1 || st.valve[3] < 1)
        return;
    Object_SetDead(I1_ANIM(0x72));
    Sound_PlayOnce(I1_ANIM(0x72), 0x25e);
    if (st.inSteam != 0) {
        EventType_RemoveHandler(EV_TRIGGER_LEAVE, I1_OnTriggerSteam);
        EventType_RemoveHandler(EV_TICK_100MS, I1_SteamTick);
    } else {
        EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerSteam);
    }
    EventType_RemoveHandler(EV_SWITCH, I1_OnSwitchValve);
    EventType_RemoveHandler(EV_TICK_1S, I1_ValveTick);
    Object_SetDead(I1_ANIM(0x94));
    MobObj_Wake(0x20);
    WindowSetText(st.win, st.text[15]);
    EventType_AddHandler(EV_POD_HOOKED, NULL, I1_OnPodHooked);
}

// 0x10002a90  EV_TRIGGER_ENTER 0x4003e: two guns lock on the masts 0xf0045 / 0xf0046
static void I1_OnTriggerMastGuns(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x3e))
        return;
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 5), PROP_STATE, 1);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x11), PROP_STATE, 1);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 5), PROP_TARGET, I1_ADESTR(0x45));
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x11), PROP_TARGET, I1_ADESTR(0x46));
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerMastGuns);
}

// The guide: AnimTurret mobs 0x10e0009 -> 0x10e000a -> 0x10e000b -> 0x10e000d take turns walking
// the lander through the base, each leg started when the previous anim finishes.

// 0x10002b00  EV_ANIM_FINISHED anim 0x4b (never removed): last leg -> guide mob 0xd
static void I1_OnGuideLeg4(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x4b)
        return;
    WindowSetText(st.win, st.text[13]);
    MobObj_Sleep(0x0b);
    MobObj_Wake(0x0d);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x0d), PROP_STATE, 0);
}

static void I1_OnGuideWaitA(const LanderEvent *ev);
static void I1_OnTriggerGuideWait(const LanderEvent *ev);

// 0x10002b50  third leg: guide mob 0xb
static void I1_GuideLeg3(void)
{
    WindowSetText(st.win, st.text[12]);
    MobObj_Sleep(0x0a);
    MobObj_Wake(0x0b);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x0b), PROP_STATE, 0);
    InTime3(200, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x0b), 0x4b, 0xe0);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x0b), 0x14);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideWaitA);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerGuideWait);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, I1_OnTriggerGuideWait);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideLeg4);
}

// 0x10002c10  EV_ANIM_FINISHED 0x10e000a anim 0x43: the guide waits; leg 3 once the lander is in
// trigger 0x400d9
static void I1_OnGuideWaitA(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x43 || (ObjId)ev->arg[0] != I1_GUIDE_B)
        return;
    st.guideWaiting = 1;
    if (st.landerAtGuide == 1) {
        AnimStop(I1_GUIDE_B, 0);
        I1_GuideLeg3();                                  // (tail jump)
    }
}

// 0x10002c50  EV_TRIGGER_ENTER / LEAVE 0x400d9
static void I1_OnTriggerGuideWait(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0xd9))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        st.landerAtGuide = 1;
        if (st.guideWaiting == 1) {
            AnimStop(I1_GUIDE_B, 0);
            I1_GuideLeg3();                              // (tail jump)
        }
        return;
    }
    st.landerAtGuide = 0;
}

// 0x10002ca0  EV_ANIM_FINISHED 0x10e000a anim 0x43 (first time): walk on and wait
static void I1_OnGuideB43(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_GUIDE_B || ev->arg[1] != 0x43)
        return;
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideB43);
    if (st.guideBStarted != 0)
        return;
    st.guideBStarted = 1;
    InTime3(200, AnimPlay, I1_GUIDE_B, 0x43, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_B, 0x14);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideWaitA);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerGuideWait);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I1_OnTriggerGuideWait);
}

// 0x10002d50  EV_ANIM_FINISHED 0x10e000a anim 0x21 (the lift ride is over)
static void I1_OnGuideB21(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_GUIDE_B || ev->arg[1] != 0x21)
        return;
    InTime3(200, AnimPlay, I1_GUIDE_B, 0x43, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_B, 0x14);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideB43);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideB21);
}

// 0x10002dc0  EV_ANIM_FINISHED anim 0x4a (never removed): the guide rides lift 0x300cc
static void I1_OnGuideB4A(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x4a)
        return;
    AnimStop(I1_GUIDE_B, 0);
    InTime3(200, AnimPlay, I1_GUIDE_B, 0x21, 0xe0);
    InTime3(200, AnimPlay, I1_ANIM(0xcc), 0x21, 0);
    Sound_PlayChain(I1_ANIM(0xcc), 6, 0xcb, 0xcc, 0xcc, 0xcc, 0xcc, 0xcd);
    AnimSetUpdateInterval(I1_GUIDE_B, 0xa);
    AnimSetUpdateInterval(I1_ANIM(0xcc), 0xa);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideB21);
}

// 0x10002e80  EV_ANIM_FINISHED 0x10e0009 anim 0x43: second leg - guide mob 0xa
static void I1_OnGuideA43b(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_GUIDE_A || ev->arg[1] != 0x43)
        return;
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideA43b);
    MobObj_Sleep(9);
    MobObj_Wake(0x0a);
    Object_SetProperty(I1_GUIDE_B, PROP_STATE, 0);
    InTime3(200, AnimPlay, I1_GUIDE_B, 0x4a, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_B, 0x14);
    AnimPlay(I1_ANIM(0x61), 0xd, 0);
    Sound_PlayOnce(I1_ANIM(0x61), 0xaa);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideB4A);
}

// 0x10002f40  EV_ANIM_FINISHED 0x10e0009 anim 0x43 (first)
static void I1_OnGuideA43a(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_GUIDE_A || ev->arg[1] != 0x43)
        return;
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideA43a);
    AnimStop(I1_GUIDE_A, 0);
    WindowSetText(st.win, st.text[8]);
    InTime3(200, AnimPlay, I1_GUIDE_A, 0x43, 0xe0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideA43b);
}

// 0x10002fd0  EV_ANIM_FINISHED 0x10e0009 anim 0x1b: the guide arrived at the base door
static void I1_OnGuideA1B(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_GUIDE_A || ev->arg[1] != 0x1b)
        return;
    Sound_StopObject(I1_ANIM(0x08));
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideA1B);
    WindowSetText(st.win, st.text[7]);
    AnimStop(I1_GUIDE_A, 0);
    AnimPlay(I1_ANIM(0x60), 0xd, 0);
    Sound_PlayOnce(I1_ANIM(0x60), 0xaa);
    AnimSetUpdateInterval(I1_GUIDE_A, 0x1e);
    InTime3(200, AnimPlay, I1_GUIDE_A, 0x43, 0xe0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideA43a);
}

// 0x100030a0  EV_DESTROYED 0xab: the guide now shoots at turret 0xb1
static void I1_OnTurretABDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_TURRET, 0xab))
        return;
    Object_SetProperty(I1_GUIDE_A, PROP_TARGET, OBJ_ID(OT_TURRET, 0xb1));
    EventType_RemoveHandler(EV_DESTROYED, I1_OnTurretABDestroyed);
}

// 0x100030e0  EV_DESTROYED 0xaa: the guide now shoots at turret 0xab
static void I1_OnTurretAADestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_TURRET, 0xaa))
        return;
    Object_SetProperty(I1_GUIDE_A, PROP_TARGET, OBJ_ID(OT_TURRET, 0xab));
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnTurretABDestroyed);
    EventType_RemoveHandler(EV_DESTROYED, I1_OnTurretAADestroyed);
}

static void I1_OnGuideA49(const LanderEvent *ev);
static void I1_OnTriggerGuideA49(const LanderEvent *ev);

// 0x10003130  the guide (mob 9) leads the assault on the base door: turrets wake, it fights
static void I1_GuideAAssault(void)
{
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerGuideA49);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideA49);
    AnimPlay(I1_ANIM(0xd2), 0xd, 4);
    Sound_PlayOnce(I1_ANIM(0xd2), 0xaa);
    AnimSetUpdateInterval(I1_ANIM(0x08), 0x96);
    AnimSetUpdateInterval(I1_GUIDE_A, 0x96);
    InTime3(1000, AnimPlay, I1_ANIM(0x08), 0x1b, 0);
    Sound_PlayOnce(I1_ANIM(0x08), 0xc8);
    InTime3(1000, Sound_PlayLoop, I1_ANIM(0x08), 0xc9, -1);
    InTime3(1000, AnimPlay, I1_GUIDE_A, 0x1b, 0xe0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideA1B);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x9b), PROP_STATE, 1);
    for (int32_t n = 0xaa; n <= 0xaf; n++)               // (unrolled)
        Object_SetProperty(OBJ_ID(OT_TURRET, n), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xb1), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xb2), PROP_STATE, 1);
    Object_SetProperty(I1_GUIDE_A, PROP_STATE, 1);
    Object_SetProperty(I1_GUIDE_A, PROP_TARGET, OBJ_ID(OT_TURRET, 0xaa));
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnTurretAADestroyed);
}

// 0x100032d0  EV_ANIM_FINISHED anim 0x49: the guide waits at the door until the lander is there
static void I1_OnGuideA49(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x49)
        return;
    st.guideAtDoor = 1;
    if (st.landerAtDoor == 1) {
        AnimStop(I1_GUIDE_A, 0);
        I1_GuideAAssault();                              // (tail jump)
    }
}

// 0x10003310  EV_TRIGGER_ENTER / LEAVE 0x40007
static void I1_OnTriggerGuideA49(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(7))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        st.landerAtDoor = 1;
        if (st.guideAtDoor == 1) {
            AnimStop(I1_GUIDE_A, 0);
            I1_GuideAAssault();                          // (tail jump)
        }
        return;
    }
    st.landerAtDoor = 0;
}

// 0x10003360  EV_DESTROYED: the four gate guns 0xe00a6..a9 (part bits masked in the event); all
// four gone -> the gate switch 0xa0091 appears
static void I1_OnGateGunDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0] & 0x3ffffffu;
    ((LanderEvent *)ev)->arg[0] = (intptr_t)id;
    if (id != I1_ATURRET(0xa6) && id != I1_ATURRET(0xa7) && id != I1_ATURRET(0xa8) && id != I1_ATURRET(0xa9))
        return;
    if (++st.gateGunsDown != 4)
        return;
    WindowSetText(st.win, st.text[20]);
    Object_SetAlive(I1_SWITCH(0x91));
    AnimPlay(I1_ANIM(0xd3), 0x1a, 0x80);
    Sound_PlayOnce(I1_ANIM(0xd3), 0xaa);
}

static void I1_OnGuideA48(const LanderEvent *ev);
static void I1_OnTriggerGuideA48(const LanderEvent *ev);

// 0x100033f0  the guide stops at the gate, which is raised (its guns wake)
static void I1_GuideAAtGate(void)
{
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerGuideA48);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuideA48);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, I1_OnTriggerGuideA48);
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnGateGunDestroyed);
    AnimPlay(I1_GUIDE_A, 0x11, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_A, 0x96);
    AnimPlay(I1_ANIM(0xc3), 0x12, 0x80);
    AnimPlay(I1_ANIM(0xd5), 0x11, 0x80);
    AnimPlay(I1_ANIM(0xc4), 0x19, 0x80);
    AnimPlay(I1_ANIM(0xc5), 0x18, 0x80);
    AnimPlay(I1_ANIM(0xc6), 0x17, 0x80);
    Sound_PlayChain(I1_ANIM(0xd5), 6, 0x25a, 0x25b, 0x25b, 0x25b, 0x25b, 0x25c);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGateStep1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideA49);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerGuideA49);
}

// 0x10003510  EV_ANIM_FINISHED anim 0x48
static void I1_OnGuideA48(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x48)
        return;
    st.guideAtGate = 1;
    if (st.landerAtGate == 1) {
        AnimStop(I1_GUIDE_A, 0);
        I1_GuideAAtGate();                               // (tail jump)
    }
}

// 0x10003550  EV_TRIGGER_ENTER / LEAVE 0x400d1
static void I1_OnTriggerGuideA48(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0xd1))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        st.landerAtGate = 1;
        if (st.guideAtGate == 1) {
            AnimStop(I1_GUIDE_8, 0);                     // (sic: mob 8, the guide is mob 9 by now)
            I1_GuideAAtGate();                           // (tail jump)
        }
        return;
    }
    st.landerAtGate = 0;
}

// 0x100035a0  EV_SWITCH 0xa008f: the guide hands over from mob 8 to mob 9
static void I1_OnSwitchGuideA(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_SWITCH(0x8f))
        return;
    AnimStop(I1_GUIDE_8, 0);
    MobObj_Sleep(8);
    MobObj_Wake(9);
    Object_SetProperty(I1_GUIDE_A, PROP_STATE, 0);
    InTime3(200, AnimPlay, I1_GUIDE_A, 0x48, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_A, 0x50);
    WindowSetText(st.win, st.text[6]);
    EventType_RemoveHandler(EV_SWITCH, I1_OnSwitchGuideA);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerGuideA48);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideA48);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I1_OnTriggerGuideA48);
}

static void I1_OnGuide847(const LanderEvent *ev);
static void I1_OnTriggerGuide847(const LanderEvent *ev);

// 0x10003680  guide mob 8 rides lift 0x30078
static void I1_Guide8Lift(void)
{
    AnimPlay(I1_ANIM(0x78), 0xc, 0);
    Sound_PlayChain(I1_ANIM(0x78), 10, 0xc8, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xca);
    AnimPlay(I1_GUIDE_8, 0xc, 0xe0);
    AnimSetUpdateInterval(I1_ANIM(0x78), 0x14);
    AnimSetUpdateInterval(I1_GUIDE_8, 0x14);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerGuide847);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, I1_OnTriggerGuide847);
    WindowSetText(st.win, st.text[5]);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuide847);
    EventType_AddHandler(EV_SWITCH, NULL, I1_OnSwitchGuideA);
}

// 0x10003760  EV_ANIM_FINISHED anim 0x47
static void I1_OnGuide847(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x47)
        return;
    st.guideAtLift = 1;
    if (st.landerAtLift == 1) {
        AnimStop(I1_GUIDE_5, 0);                         // (sic: mob 5)
        I1_Guide8Lift();                                 // (tail jump)
    }
}

// 0x100037a0  EV_TRIGGER_ENTER / LEAVE 0x4002f
static void I1_OnTriggerGuide847(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x2f))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        st.landerAtLift = 1;
        if (st.guideAtLift == 1) {
            AnimStop(I1_GUIDE_5, 0);                     // (sic: mob 5)
            I1_Guide8Lift();                             // (tail jump)
        }
        return;
    }
    st.landerAtLift = 0;
}

static void I1_OnGuide846(const LanderEvent *ev);
static void I1_OnCraneFallen(const LanderEvent *ev);

// 0x100037f0  guide mob 8 walks on once the crane is down
static void I1_Guide8Walk(void)
{
    WindowSetText(st.win, st.text[4]);
    AnimPlay(I1_GUIDE_8, 0x47, 0xe0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuide846);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnCraneFallen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerGuide847);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I1_OnTriggerGuide847);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuide847);
}

// 0x10003870  EV_ANIM_FINISHED anim 0x46: the guide waits for the crane
static void I1_OnGuide846(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x46)
        return;
    st.guideAtCrane = 1;
    if (st.craneDown == 1) {
        AnimStop(I1_GUIDE_5, 0);                         // (sic: mob 5)
        I1_Guide8Walk();                                 // (tail jump)
    }
}

// 0x100038b0  EV_ANIM_FINISHED anim 0x2f (the crane 0xf0016 fell)
static void I1_OnCraneFallen(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x2f)
        return;
    st.craneDown = 1;
    if (st.guideAtCrane == 1) {
        AnimStop(I1_GUIDE_8, 0);
        I1_Guide8Walk();                                 // (tail jump)
    }
}

// 0x100038f0  InTime: guide mob 8 walks to the crane
static void I1_Guide8Start(void)
{
    AnimStop(I1_GUIDE_8, 0);
    InTime3(200, AnimPlay, I1_GUIDE_8, 0x46, 0xe0);
    WindowSetText(st.win, st.text[3]);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuide846);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnCraneFallen);
}

// 0x10003960  EV_TRIGGER_ENTER 0x400c1: turrets 0xa0..0xa3 on
static void I1_OnTriggerTurretsA0(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0xc1))
        return;
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa0), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa1), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa2), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa3), PROP_STATE, 1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerTurretsA0);
}

// 0x100039c0  EV_ANIM_FINISHED anim 0x45: guide mob 8 arrived; it starts again 6 s later
static void I1_OnGuide845(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x45)
        return;
    WindowSetText(st.win, st.text[2]);
    InTime3(5000, AnimPlay, I1_ANIM(0x5e), 0x61, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuide845);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerTurretsA0);
    InTime0(6000, I1_Guide8Start);
}

// 0x10003a40  the outer gates 0x3002b..e (open: flags 0, close: 4)
static void I1_OuterGates(intptr_t flags)
{
    WindowSetText(st.win, st.text[1]);
    AnimPlay(I1_ANIM(0x2b), 5, (int32_t)flags);
    AnimPlay(I1_ANIM(0x2c), 6, (int32_t)flags);
    AnimPlay(I1_ANIM(0x2d), 7, (int32_t)flags);
    AnimPlay(I1_ANIM(0x2e), 8, (int32_t)flags);
    Sound_PlayOnce(I1_ANIM(0x2b), 0xda);
}

// 0x10003ab0  the inner gates 0x30027..2a
static void I1_InnerGates(intptr_t flags)
{
    AnimPlay(I1_ANIM(0x27), 9, (int32_t)flags);
    AnimPlay(I1_ANIM(0x28), 9, (int32_t)flags);
    AnimPlay(I1_ANIM(0x29), 9, (int32_t)flags);
    AnimPlay(I1_ANIM(0x2a), 9, (int32_t)flags);
    Sound_PlayOnce(I1_ANIM(0x2b), 0xda);
}

// 0x10003b10  EV_ANIM_FINISHED anim 0x44
static void I1_OnGuide844(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x44)
        return;
    WindowSetText(st.win, st.text[9]);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuide844);
}

// 0x10003b50  EV_ANIM_FINISHED anim 9 (the inner gates are open): the guide hands over to mob 8
static void I1_OnInnerGatesOpen(const LanderEvent *ev)
{
    if (ev->arg[1] != 9)
        return;
    AnimStop(I1_GUIDE_5, 0);
    InTime3(1000, AnimPlay, I1_ANIM(0x0b), 4, 0);
    InTime2(1000, Sound_PlayOnce, I1_ANIM(0x0b), 0xaa);
    InTime3(6000, AnimPlay, I1_GUIDE_8, 0x44, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_8, 0x14);
    WindowSetText(st.win, st.text[10]);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuide844);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnInnerGatesOpen);
}

// 0x10003c20  EV_ANIM_FINISHED 0x3002e (the outer gates are open): open the inner gates
static void I1_OnOuterGatesOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_ANIM(0x2e))
        return;
    st.innerGates = 1;
    I1_InnerGates(0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnOuterGatesOpen);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnInnerGatesOpen);
}

static void I1_OnGuideShot(const LanderEvent *ev);

// 0x10003c70  InTime: reopen the outer gates
static void I1_GatesReopen(void)
{
    I1_OuterGates(0);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, I1_OnGuideShot);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnOuterGatesOpen);
}

// 0x10003ca0  EV_ANIM_FINISHED (after the guide was shot): the gates close; the original then
// compares the anim index with 0x3002e (an object id), so the reopening branch never runs
static void I1_OnGatesClosing(const LanderEvent *ev)
{
    if (ev->arg[1] == 9) {
        st.innerGates--;
        EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnInnerGatesOpen);
        I1_OuterGates(4);
    }
    if (ev->arg[1] == (intptr_t)I1_ANIM(0x2e)) {
        EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGatesClosing);
        InTime0(2000, I1_GatesReopen);
    }
}

// 0x10003d10  EV_PROJECTILE_HIT on the guide mob 5 (the lander shot its guide): the gates close
static void I1_OnGuideShot(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_GUIDE_5)
        return;
    if (st.innerGates == 0) {
        EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnOuterGatesOpen);
        I1_OuterGates(4);
    } else {
        EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnInnerGatesOpen);
        I1_InnerGates(4);
    }
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGatesClosing);
    EventType_RemoveHandler(EV_PROJECTILE_HIT, I1_OnGuideShot);
}

static void I1_OnTriggerGuide542(const LanderEvent *ev);
static void I1_OnGuide542(const LanderEvent *ev);

// 0x10003d80  at the base gates: two gate guards wake, the outer gates open
static void I1_AtBaseGates(void)
{
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerGuide542);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuide542);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, I1_OnTriggerGuide542);
    MobObj_Wake(0x24);
    MobObj_Wake(0x25);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x24), 0x5c, 0x60);
    InTime3(750, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x25), 0x5c, 0x60);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x24), 0x14);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x25), 0x14);
    I1_OuterGates(0);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, I1_OnGuideShot);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnOuterGatesOpen);
}

// 0x10003e40  EV_TRIGGER_ENTER / LEAVE 0x400d0
static void I1_OnTriggerGuide542(const LanderEvent *ev)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        if ((ObjId)ev->arg[1] != I1_TRIGGER(0xd0))
            return;
        st.landerAtGates = 1;
        if (st.guideAtGates == 1) {
            AnimStop(I1_GUIDE_5, 0);
            I1_AtBaseGates();                            // (tail jump)
        }
        return;
    }
    if ((ObjId)ev->arg[1] == I1_TRIGGER(0xd0))
        st.landerAtGates = 0;
}

// 0x10003ea0  EV_ANIM_FINISHED anim 0x42
static void I1_OnGuide542(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x42)
        return;
    st.guideAtGates = 1;
    if (st.landerAtGates == 1) {
        AnimStop(I1_GUIDE_5, 0);
        I1_AtBaseGates();                                // (tail jump)
    }
}

// 0x10003ee0  EV_ANIM_FINISHED anim 0x43: guide mob 5 hands over to mob 8
static void I1_OnGuide543(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x43)
        return;
    AnimStop(I1_GUIDE_5, 0);
    Object_SetDead(I1_GUIDE_5);
    MobObj_Wake(8);
    Object_SetProperty(I1_GUIDE_8, PROP_STATE, 0);
    InTime3(200, AnimPlay, I1_GUIDE_8, 0x42, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_8, 0x14);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerGuide542);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I1_OnTriggerGuide542);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuide542);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuide543);
}

// 0x10003fa0  EV_TRIGGER_ENTER 0x40075
static void I1_OnTriggerGuide543(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x75))
        return;
    AnimStop(I1_GUIDE_5, 0);
    InTime3(200, AnimPlay, I1_GUIDE_5, 0x43, 0xe0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuide543);
    WindowSetText(st.win, st.text[27]);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerGuide543);
}

// 0x10004020  EV_ANIM_FINISHED anim 0x3e: the guide rides lift 0x30074
static void I1_OnGuide53E(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x3e)
        return;
    AnimStop(I1_GUIDE_5, 0);
    InTime3(500, AnimPlay, I1_ANIM(0x74), 2, 0);
    InTime3(500, AnimPlay, I1_GUIDE_5, 2, 0xe0);
    Sound_PlayChain(I1_ANIM(0x74), 14, 0xc8, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9, 0xc9,
                    0xc9, 0xca);
    AnimSetUpdateInterval(I1_GUIDE_5, 0xa);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerGuide543);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnGuide53E);
}

// 0x10004110  EV_SWITCH 0xa00c2: door 0x30026 opens, the guide goes on
static void I1_OnSwitchDoor26(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_SWITCH(0xc2))
        return;
    AnimPlay(I1_ANIM(0x26), 1, 0);
    Sound_PlayOnce(I1_ANIM(0x26), 0xaa);
    InTime3(1000, AnimPlay, I1_GUIDE_5, 0x3e, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_5, 0x28);
    WindowSetText(st.win, st.text[26]);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuide53E);
    EventType_RemoveHandler(EV_SWITCH, I1_OnSwitchDoor26);
}

// 0x100041c0  EV_ANIM_FINISHED anim 0x3a: the convoy stops at door 0x30026
static void I1_OnConvoyStopped(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x3a)
        return;
    AnimStop(I1_GUIDE_5, 0);
    WindowSetText(st.win, st.text[29]);
    AnimStop(MOB_ID(OT_ANIMTURRET, 0x11), 0);
    AnimStop(MOB_ID(OT_ANIMTURRET, 0x14), 0);
    AnimStop(MOB_ID(OT_ANIMTURRET, 0x15), 0);
    AnimPlay(I1_ANIM(0x3d), 0, 0);
    EventType_AddHandler(EV_SWITCH, NULL, I1_OnSwitchDoor26);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnConvoyStopped);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xb0), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbb), PROP_STATE, 1);
}

// 0x10004280  the convoy (guide mob 5 with guards 0x11 / 0x14 / 0x15) moves on
static void I1_ConvoyGo(void)
{
    AnimPlay(I1_GUIDE_5, 0x3a, 0xe0);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x11), 0x3c, 0xe0);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x14), 0x3b, 0xe0);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x15), 0x3d, 0xe0);
    AnimSetUpdateInterval(I1_GUIDE_5, 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x11), 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x14), 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x15), 0x28);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerMastGuns);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnConvoyStopped);
}

// 0x10004330  EV_DESTROYED 0x800c9 (the convoy's blocker): removed; the convoy goes on if it waits
static void I1_OnBlockerDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_DESTRUCTIBLE, 0xc9))
        return;
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0xc9));
    st.blockerDown = 1;
    EventType_RemoveHandler(EV_DESTROYED, I1_OnBlockerDestroyed);
    if (st.convoyWaiting == 1)
        I1_ConvoyGo();                                   // (tail jump)
}

// 0x10004380  EV_ANIM_FINISHED anim 0x36: the convoy reached the blocker and waits for it to go
static void I1_OnConvoyAtBlocker(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x36)
        return;
    AnimStop(I1_GUIDE_5, 0);
    AnimStop(MOB_ID(OT_ANIMTURRET, 0x11), 0);
    AnimStop(MOB_ID(OT_ANIMTURRET, 0x14), 0);
    AnimStop(MOB_ID(OT_ANIMTURRET, 0x15), 0);
    st.convoyWaiting = 1;
    if (st.blockerDown == 1)
        I1_ConvoyGo();
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnConvoyAtBlocker);
}

// 0x100043f0  EV_DESTROYED: the force-field generators 0x80055..57 are removed
static void I1_OnFieldGenDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_DESTRUCTIBLE, 0x55))
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x55));
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_DESTRUCTIBLE, 0x56))
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x56));
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_DESTRUCTIBLE, 0x57))
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x57));
}

// 0x10004440  EV_TICK_1S in the lava chamber without the heat shield: -1 shield per second
static void I1_LavaTick(const LanderEvent *ev)
{
    (void)ev;
    Object_SetProperty(I1_LANDER, PROP_HITPOINTS, Object_GetProperty(I1_LANDER, PROP_HITPOINTS) - 1);
}

// 0x10004470  EV_TRIGGER_ENTER / LEAVE 0x4005a: the lava chamber
static void I1_OnTriggerLava(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x5a))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        EventType_AddHandler(EV_TICK_1S, NULL, I1_LavaTick);
        return;
    }
    EventType_RemoveHandler(EV_TICK_1S, I1_LavaTick);
}

// 0x100044b0  EV_PICKUP 0x130058: the heat shield - the lava no longer hurts
static void I1_OnPickupHeatShield(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_PICKUP(0x58))
        return;
    Voice_HeatShieldAcquired(I1_LANDER);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerLava);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, I1_OnTriggerLava);
}

// 0x100044f0  EV_TICK_1S: mission timer (seconds) for the result
static void I1_TimerTick(const LanderEvent *ev)
{
    (void)ev;
    st.timer++;
}

// 0x10004500  InTime (3 s): first objective line
static void I1_SayStart(void)
{
    WindowSetText(st.win, st.text[0]);
}

// 0x100045b0  EV_DESTROYED: a gun's target is gone -> that gun stops
static void I1_OnGunTargetDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id == I1_ATURRET(0xc8)) {
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x15), PROP_STATE, 0);
        return;
    }
    if (id == I1_ATURRET(0xc7))
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x14), PROP_STATE, 0);
}

// 0x100045f0  EV_PICKUP 0x130000: full shield
static void I1_OnPickupShield100(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_PICKUP(0))
        return;
    int32_t shield = (int32_t)Object_GetProperty(I1_LANDER, PROP_HITPOINTS) + 0x100;
    Voice_ShieldAcquired(I1_LANDER, 100);
    Object_SetProperty(I1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10004660  EV_PICKUP 0x130002: +50 % shield
static void I1_OnPickupShield50(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_PICKUP(2))
        return;
    int32_t shield = (int32_t)Object_GetProperty(I1_LANDER, PROP_HITPOINTS) + 0x80;
    Voice_ShieldAcquired(I1_LANDER, 50);
    Object_SetProperty(I1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x100046d0  EV_PICKUP: +25 % shield, cash, missiles (any pickup id >= 0x1300d6)
static void I1_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == I1_PICKUP(1)) {
        int32_t shield = (int32_t)Object_GetProperty(I1_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(I1_LANDER, 25);
        Object_SetProperty(I1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == I1_PICKUP(0x11)) {
        Lander_SetCash(I1_LANDER, Lander_GetCash(I1_LANDER) + gSmallCash);
        Voice_CashAcquired(I1_LANDER);
    }
    if ((ObjId)ev->arg[0] == I1_PICKUP(0x10)) {
        Lander_SetCash(I1_LANDER, Lander_GetCash(I1_LANDER) + gMediumCash);
        Voice_CashAcquired(I1_LANDER);
    }
    if ((ObjId)ev->arg[0] == I1_PICKUP(0x12)) {
        Lander_SetCash(I1_LANDER, Lander_GetCash(I1_LANDER) + gMediumCash);
        Voice_CashAcquired(I1_LANDER);
    }
    if ((ObjId)ev->arg[0] == I1_PICKUP(0x13)) {
        Lander_SetCash(I1_LANDER, Lander_GetCash(I1_LANDER) + gLargeCash);
        Voice_CashAcquired(I1_LANDER);
    }
    if ((int32_t)ev->arg[0] >= (int32_t)I1_PICKUP(0xd6)) {
        if (Lander_AddMissiles(I1_LANDER, 2) == 0) {
            Voice_MissilesFull(I1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(I1_LANDER);
    }
}

// 0x10004850  EV_SWITCH 0xa008d: the bridge (0x3001b / 0x3000a) stops (on) or runs (off); on
// also sends guide mob 8 on
static void I1_OnSwitchBridge(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_SWITCH(0x8d))
        return;
    if (ev->arg[1] == 1) {
        WindowSetText(st.win, st.text[10]);
        AnimPlay(I1_ANIM(0x1b), 0xa, 4);
        Sound_StopObject(I1_ANIM(0x1b));
        AnimPlay(I1_ANIM(0x0a), 0xb, 4);
        AnimSetUpdateInterval(I1_ANIM(0x1b), 0x14);
        AnimSetUpdateInterval(I1_ANIM(0x0a), 0x14);
        InTime3(2000, AnimPlay, I1_GUIDE_8, 0x45, 0xe0);
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuide845);
        EventType_RemoveHandler(EV_SWITCH, I1_OnSwitchBridge);
        AnimSetUpdateInterval(I1_GUIDE_8, 0x14);
        return;
    }
    if (ev->arg[1] != 0)
        return;
    AnimPlay(I1_ANIM(0x1b), 0xa, 2);
    AnimPlay(I1_ANIM(0x0a), 0xb, 2);
    AnimSetUpdateInterval(I1_ANIM(0x1b), 0x14);
    AnimSetUpdateInterval(I1_ANIM(0x0a), 0x14);
}

// 0x10004970  EV_ANIM_FINISHED anim 0x65 (never removed): the guide's last leg is over
static void I1_OnGuideD65(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x65)
        return;
    WindowSetText(st.win, st.text[17]);
    MobObj_Sleep(0x0d);
}

static void I1_OnTriggerPiston(const LanderEvent *ev);

// 0x100049a0  EV_ANIM_FINISHED anim 0x5f: the piston 0x30009 is back -> its trigger works again
static void I1_OnPistonBack(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x5f)
        return;
    Sound_StopObject(I1_ANIM(0x09));
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerPiston);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnPistonBack);
}

// 0x100049e0  EV_ANIM_FINISHED anim 0x5f: the piston is out -> its guns off, back in 1 s
static void I1_OnPistonOut(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x5f)
        return;
    Sound_StopObject(I1_ANIM(0x09));
    for (int32_t n = 0xb3; n <= 0xba; n++)               // (unrolled)
        Object_SetProperty(OBJ_ID(OT_TURRET, n), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbc), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbd), PROP_STATE, 0);
    InTime3(1000, AnimPlay, I1_ANIM(0x09), 0x5f, 4);
    InTime2(1000, Sound_PlayOnce, I1_ANIM(0x09), 0xc8);
    InTime3(2000, Sound_PlayLoop, I1_ANIM(0x09), 0xc9, -1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnPistonBack);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I1_OnPistonOut);
}

// 0x10004b10  EV_TRIGGER_ENTER 0x400d7: the piston trap fires and its guns wake
static void I1_OnTriggerPiston(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0xd7))
        return;
    AnimPlay(I1_ANIM(0x09), 0x5f, 0);
    AnimSetUpdateInterval(I1_ANIM(0x09), 0x96);
    Sound_PlayOnce(I1_ANIM(0x09), 0xc8);
    InTime3(1000, Sound_PlayLoop, I1_ANIM(0x09), 0xc9, -1);
    for (int32_t n = 0xb3; n <= 0xba; n++)               // (unrolled)
        Object_SetProperty(OBJ_ID(OT_TURRET, n), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbc), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbd), PROP_STATE, 1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnPistonOut);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I1_OnTriggerPiston);
}

static void I1_OnTriggerDoor33(const LanderEvent *ev);

// 0x10004c40  EV_POD_HOOKED (after the valves were shut): the cargo pod 0x10c0020 is on the hook
static void I1_OnPodHooked(const LanderEvent *ev)
{
    (void)ev;
    Pod_Script_SetupDynamicsCuboid(I1_POD, 0xe6, 0xe6, 0xe6, 0x1f4);
    Pod_Script_SetupHookPoint(I1_POD, 0, 0, 0);
    Pod_Script_SetupMass(I1_POD, 1000);
    Pod_Script_SetupDrag(I1_POD, 0x1333);
    EventType_RemoveHandler(EV_POD_HOOKED, I1_OnPodHooked);
    WindowSetText(st.win, st.text[16]);
    InTime3(3000, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x0d), 0x65, 0xe0);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x0d), 0x14);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnGuideD65);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerDoor33);
}

// 0x10004d20  open (entered) / close an animated door
static void I1_DoorAnim(int32_t type, ObjId obj, int32_t anim)
{
    if (type == EV_TRIGGER_ENTER) {
        AnimPlay(obj, anim, 0);
        return;
    }
    AnimPlay(obj, anim, 4);
}

// 0x10004d60  EV_TRIGGER_ENTER / LEAVE 0x40036: door 0x30032
static void I1_OnTriggerDoor32(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x36))
        return;
    AnimSetUpdateInterval(I1_ANIM(0x32), 0x64);
    I1_DoorAnim(ev->type, I1_ANIM(0x32), 0x1e);
    Sound_PlayOnce(I1_ANIM(0x32), 0x3f7);
}

// 0x10004db0  EV_TRIGGER_ENTER 0x40037 (added with the pod): door 0x30033
static void I1_OnTriggerDoor33(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I1_TRIGGER(0x37))
        return;
    AnimSetUpdateInterval(I1_ANIM(0x33), 0x64);
    I1_DoorAnim(ev->type, I1_ANIM(0x33), 0x1e);
    Sound_PlayOnce(I1_ANIM(0x33), 0x3f7);
}

// 0x10004e00  EV_SWITCH 0xa0090: door 0x30071 follows the switch
static void I1_OnSwitchDoor71(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I1_SWITCH(0x90))
        return;
    AnimPlay(I1_ANIM(0x71), 0x61, ev->arg[1] == 1 ? 0 : 4);
    Sound_PlayOnce(I1_ANIM(0x71), 0xaa);
}

// 0x10004e40  set-up (only the lander start calls it)
static void I1_Setup(void)
{
    st.text = i1_text[gLanguage];
    SetGravity(13);
    gAirResistance = 0xf;
    gMainThrottlePower = 0x19;
    gNbWaypoints = i1_nbWaypoints;
    gWaypoints = i1_waypoints;
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0xda), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0xdb), PROP_STATE, 0);
    gTurretTypes[21] = &i1_turretType21;
    gTurretTypes[15] = &i1_turretType15;
    gTurretTypes[0] = &i1_turretType0;
    gTurretTypes[2] = &i1_turretType2;
    for (int32_t id = OBJ_ID(OT_TURRET, 0x9b); id <= (int32_t)OBJ_ID(OT_TURRET, 0xc0); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t n = 0x24; n <= 0x28; n++)
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, n), PROP_ARMOUR, 0x19);
    for (int32_t n = 0; n <= 2; n++)
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, n), PROP_ARMOUR, 0x14);
    for (int32_t n = 0x16; n <= 0x18; n++)
        Object_SetProperty(I1_ADESTR(n), PROP_ARMOUR, 0xff);
    for (int32_t n = 0x30; n <= 0x31; n++)
        Object_SetProperty(I1_ADESTR(n), PROP_ARMOUR, 0x19);
    for (int32_t id = OBJ_ID(OT_FUELDUMP, 0x3f); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0x42); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t n = 0x43; n <= 0x4d; n++)
        Object_SetProperty(I1_ADESTR(n), PROP_ARMOUR, 0xf);
    for (int32_t n = 0x51; n <= 0x54; n++)
        Object_SetProperty(I1_ADESTR(n), PROP_ARMOUR, 0xf);
    Object_SetProperty(I1_ATURRET(0xc7), PROP_ARMOUR, 0xf);
    Object_SetProperty(I1_ATURRET(0xc8), PROP_ARMOUR, 0x14);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x3a), PROP_ARMOUR, 0xff);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x3a), PROP_EXPLOSION, 0x19);
    Object_SetProperty(I1_ADESTR(0x4f), PROP_ARMOUR, 0xf);
    MobObj_Wake(5);                                      // Alpha One (guide) and Alpha Two's guns
    MobObj_Wake(0x11);
    MobObj_Wake(0x14);
    MobObj_Wake(0x15);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x14), PROP_TARGET, I1_ATURRET(0xc7));
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x15), PROP_TARGET, I1_ATURRET(0xc8));
    Object_SetProperty(I1_GUIDE_5, PROP_STATE, 0);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x11), PROP_STATE, 0);
    static const uint8_t armoured[] = {                  // (unrolled; 0x11/0x14/0x15 twice)
        0x05, 0x11, 0x14, 0x15, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
        0x14, 0x15, 0x16, 0x17,
    };
    for (size_t i = 0; i < sizeof armoured; i++)
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, armoured[i]), PROP_ARMOUR, 0xff);
    static const uint8_t humming[] = {                   // (unrolled; not 0x11)
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x12, 0x13, 0x14, 0x15,
        0x16, 0x17,
    };
    for (size_t i = 0; i < sizeof humming; i++)
        Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, humming[i]), 0x92, -1);
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x55));
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x57));
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0xda), PROP_STATE, 0xc8);
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x56));
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0xdb), PROP_STATE, -200);
    Object_SetDead(I1_SWITCH(0x91));
    TexAnimPlay(0x3e, 2, 100);
    InTime3(3000, AnimPlay, I1_GUIDE_5, 0x36, 0xe0);
    InTime3(3000, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x11), 0x37, 0xe0);
    InTime3(4000, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x14), 0x38, 0xe0);
    InTime3(4000, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x15), 0x39, 0xe0);
    InTime0(3000, I1_SayStart);
    AnimPlay(I1_ANIM(0x5d), 0x1c, 0);
    AnimSetUpdateInterval(I1_GUIDE_5, 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x11), 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x14), 0x28);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x15), 0x28);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I1_OnConvoyAtBlocker);
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnBlockerDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnMastDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnFieldGenDestroyed);
    static const uint8_t vents[] = { 0x67, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x68, 0x69, 0x6a };   // (unrolled)
    for (size_t i = 0; i < sizeof vents; i++)
        AnimPlay(I1_ANIM(vents[i]), 0x20, 2);
    for (size_t i = 0; i < sizeof vents; i++)
        Sound_PlayLoop(I1_ANIM(vents[i]), 0xee, -1);
    for (size_t i = 0; i < sizeof vents; i++)
        AnimSetUpdateInterval(I1_ANIM(vents[i]), 0x2d);
    MobObj_Wake(0x23);
    MobObj_Wake(0x1e);
    MobObj_Wake(0x18);
    AnimPlay(I1_ANIM(0x0e), 0xe, 0);
    AnimPlay(I1_ANIM(0x0f), 0xe, 0);
    AnimPlay(I1_ANIM(0x66), 0x25, 0);
    AnimPlay(I1_ANIM(0x3b), 3, 0x81);
    AnimPlay(I1_ANIM(0x3c), 3, 0x85);
    AnimSetUpdateInterval(I1_ANIM(0x3b), 2);
    AnimSetUpdateInterval(I1_ANIM(0x3c), 2);
    for (int32_t n = 0x3b; n <= 0x3c; n++)
        Sound_PlayLoop(I1_ANIM(n), 0xb4, -1);
    for (int32_t n = 0x24; n <= 0x28; n++)
        Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, n), 0x90, -1);
    for (int32_t n = 0xc7; n <= 0xc8; n++)
        Sound_PlayLoop(I1_ATURRET(n), 0x90, -1);
    AnimSetUpdateInterval(I1_ANIM(0xc3), 0x96);
    AnimSetUpdateInterval(I1_ANIM(0xc4), 0x96);
    AnimSetUpdateInterval(I1_ANIM(0xc5), 0x96);
    AnimSetUpdateInterval(I1_ANIM(0xc6), 0x96);
    AnimSetUpdateInterval(I1_ANIM(0xd5), 0x96);
    AnimSetUpdateInterval(I1_ANIM(0xd4), 0x96);
    AnimSetUpdateInterval(I1_ANIM(0xd3), 0x96);
    for (int32_t n = 0xa6; n <= 0xa9; n++)
        AnimSetUpdateInterval(I1_ATURRET(n), 0x96);
    for (int32_t n = 0x27; n <= 0x2e; n++)               // (unrolled) the base gates
        AnimSetUpdateInterval(I1_ANIM(n), 0x32);
    AnimSetUpdateInterval(I1_ANIM(0x0b), 0x64);
    AnimPlay(I1_ANIM(0x78), 0xc, 4);
    AnimSetUpdateInterval(I1_ADESTR(0x16), 0xa);
    AnimSetUpdateInterval(I1_ADESTR(0x17), 0xa);
    AnimSetUpdateInterval(I1_ADESTR(0x18), 0xa);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x9a), PROP_ARMOUR, 0xff);
    EventType_AddHandler(EV_PICKUP, NULL, I1_OnPickupShield100);
    EventType_AddHandler(EV_PICKUP, NULL, I1_OnPickupShield50);
    EventType_AddHandler(EV_PICKUP, NULL, I1_OnPickup);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerLava);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I1_OnTriggerLava);
    EventType_AddHandler(EV_PICKUP, NULL, I1_OnPickupHeatShield);
    EventType_AddHandler(EV_SWITCH, NULL, I1_OnSwitch);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerMisc);
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnDestroyed);
    EventType_AddHandler(EV_SWITCH, NULL, I1_OnSwitchBridge);
    EventType_AddHandler(EV_TICK_1S, NULL, I1_TimerTick);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10004520
    EventType_AddHandler(EV_DESTROYED, NULL, I1_OnGunTargetDestroyed);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerDoor32);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I1_OnTriggerDoor32);
    EventType_AddHandler(EV_SWITCH, NULL, I1_OnSwitchValve);
    EventType_AddHandler(EV_TICK_1S, NULL, I1_ValveTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerSteam);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerPiston);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I1_OnTriggerCoreRoom);
    EventType_AddHandler(EV_SWITCH, NULL, I1_OnSwitchDoor71);
    for (int32_t n = 0xa6; n <= 0xa9; n++)               // (unrolled)
        Object_SetProperty(I1_ATURRET(n), PROP_STATE, 0);
    for (int32_t n = 0xa0; n <= 0xa3; n++)               // (unrolled)
        Object_SetProperty(OBJ_ID(OT_TURRET, n), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xb0), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbb), PROP_STATE, 0);
    for (int32_t n = 0xb3; n <= 0xba; n++)               // (unrolled)
        Object_SetProperty(OBJ_ID(OT_TURRET, n), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbc), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xbd), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x9b), PROP_STATE, 0);
    for (int32_t n = 0xaa; n <= 0xaf; n++)               // (unrolled)
        Object_SetProperty(OBJ_ID(OT_TURRET, n), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xb1), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xb2), PROP_STATE, 0);
    AnimPlay(I1_ANIM(0x1b), 0xa, 2);                     // the bridge runs
    Sound_PlayLoop(I1_ANIM(0x1b), 0xbc, -1);
    AnimPlay(I1_ANIM(0x0a), 0xb, 2);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 550, 12);
    WindowSetText(st.win, st.text[24]);
    MissionScore_Init(I1_LANDER, 30000, 8);              // 0x10001520
    st.startShield = (int32_t)Object_GetProperty(I1_LANDER, PROP_HITPOINTS);
    Media_AudioPlay(0);
}

// 0x10005bd0  LevelStartLevel_Lander (export 0x10001010)
static void I1_StartLander(void)
{
    MobObj_Wake(I1_MOB_LANDER);
    MobObj_Wake(I1_MOB_CAMERA);
    Lander_SetHeadLight(I1_LANDER, 0x271, 0x8cc);
    Object_SetProperty(I1_CAMERA, PROP_TARGET, I1_LANDER);
    I1_Setup();
}

// 0x10001040  LevelStartLevel_Flycam and LevelStopLevel (exports 0x10001020): empty
static void I1_Nothing(void)
{
}

// 0x10001030
static uint32_t I1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel: two calls of the empty 0x10001040, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t I1_Load(const char *dllPath)
{
    (void)dllPath;
    (void)i1_turretTypeUnused;
    memset(&st, 0, sizeof st);
    st.mastPair[0] = st.mastPair[1] = st.mastPair[2] = 1;
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText i1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000a170, .count = 3 * 30, .out = i1_text },
    { 0 },
};

const mission_script_t mission_io_1 = {
    .name          = "IO\\1",
    .get_version   = I1_GetVersion,
    .load          = I1_Load,
    .start_lander  = I1_StartLander,
    .start_flycam  = I1_Nothing,
    .start_animcam = NULL,
    .stop          = I1_Nothing,
    .text          = i1_gametext,
};
