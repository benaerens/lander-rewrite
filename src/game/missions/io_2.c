// MISSIONS\IO\2\MISSIONR.DLL - faithful C port.
//
// Mission: enter the underground complex and steal the cache (pod 0x10c0024). On the way: a drill
// site (trigger 0x4003a: a crawler crew arrives 10 s later, ten mob pairs light up and go out in
// turn), proximity doors, a lift with call switches at both ends and a power switch, clamps, a
// sweeper and a shuttle (trigger planes), mines that go off when bumped, guard patrols (an AnimSeq
// sequence), a hot zone (-1 shield per 100 ms, until the heat-shield pickup 0x130060). Hooking the
// cache sends the guards after the lander and starts the evac check: above y = 65000, +100000 cash,
// result 2 if within 600 s of the mission timer, else 1. Par time 600 s, 5 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x100011d0,
// LevelStartLevel_Lander 0x100011e0 (-> 0x10003c00), LevelStartLevel_Flycam 0x100011f0
// (-> 0x10003c40), LevelStopLevel 0x10001200 (-> the empty 0x10003c60), LevelGetVersion 0x10001210.
// AnimSeq (0x10001000-0x100011c1) and the score library (0x10001220-0x100017c4) are in
// mission_lib.c; the destroyed-sound handler 0x10003220 is MissionLib_OnDestroyedSound.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define I2_LANDER        MOB_ID(OT_LANDER, 0x23)         // 0x01010023
#define I2_CAMERA        MOB_ID(OT_CAMERA, 0x0b)         // 0x0106000b
#define I2_CACHE         MOB_ID(OT_POD, 0x24)            // 0x010c0024 the target
#define I2_POD3D         MOB_ID(OT_POD, 0x3d)            // 0x010c003d drill pod
#define I2_CRAWLER       MOB_ID(OT_ANIMOBJECT, 0x18)     // 0x01030018
#define I2_SHUTTLE       MOB_ID(OT_ANIMOBJECT, 0x1c)     // 0x0103001c
#define I2_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define I2_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define I2_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define I2_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define I2_MOB_LANDER    0x23
#define I2_MOB_CAMERA    0x0b
#define I2_MOB_FLYCAM    0x1a

// ---- static data (.data) --------------------------------------------------------------------------
// 0x10008160: [language][7] string pointers (German entries 3 and 4 are the same string)
static const char *i2_text[3][7];
// 0x100081b8: AnimSeq sequence of the guard 0x10e002d: anim 0x1b three times (flags 0x60), end
static const uint8_t i2_seqPatrol[8] = { 0x1b, 0x03, 0x60, 0xff, 0x00, 0x00, 0x00, 0x00 };
static const int32_t  i2_nbWaypoints = 1;                // 0x100081c0
static const Waypoint i2_waypoints[1] = {                // 0x100081c8
    { 6400, -44800 },
};
static const TurretType i2_turretType0 = {               // 0x100081d0 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 80, 0x18, 1024, 5760, 0
};
static const TurretType i2_turretType21 = {              // 0x100081f0 -> gTurretTypes[21]
    { 0, 53, 1392, 0 }, 0x28, 0, 2, 10, 85, 0x18, 1024, 5760, 0
};
static const TurretType i2_turretType3 = {               // 0x10008210 -> gTurretTypes[3]
    { 0, 18, 158, 0 }, 0x05, 0, 4, 10, 100, 0x10, 1024, 5760, 0
};
static const TurretType i2_turretType5 = {               // 0x10008230 -> gTurretTypes[5]
    { 85, 434, 461, 0 }, 0x02, 2, 2, 25, 100, 0x10, 512, 12000, 0
};
static const TurretType i2_turretType7 = {               // 0x10008250 -> gTurretTypes[7]
    { -19, 0, 793, 0 }, 0x00, 0, 2, 7, 70, 0x5a, 1024, 5760, 0
};
static const TurretType i2_turretType13 = {              // 0x10008270 -> gTurretTypes[13]
    { 61, 0, 198, 0 }, 0x05, 2, 4, 10, 100, 0x20, 800, 5760, 0
};
static const TurretType i2_turretType15 = {              // 0x10008290 -> gTurretTypes[15]
    { 61, 0, 198, 0 }, 0x05, 2, 4, 10, 160, 0x20, 800, 5760, 0
};

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    Window      *win;             // 0x1000acec objective line
    int32_t      patrolStarted;   // 0x1000acf4
    int32_t      liftGate;        // 0x1000acf8 never written (0)
    int32_t      drillStarted;    // 0x1000ad00 written, never read
    int32_t      gateOpen;        // 0x1000ad08 written, never read
    int32_t      pairCount;       // 0x1000ad0c mob-pair counter (100-ms ticks)
    int32_t      heatShield;      // 0x1000ad10 written, never read
    int32_t      timer;           // 0x1000ad14 seconds
    int32_t      liftMoving;      // 0x1000ad18
    int32_t      shuttleArmed;    // 0x1000ad1c
    int32_t      shuttleRunning;  // 0x1000ad20
    const char *const *text;      // 0x1000b160 = i2_text[gLanguage]
    uint8_t      seqPatrol[8];    // 0x100081b8 (writable for AnimSeq)
} st;

// 0x100017d0  EV_TICK_100MS: every other tick the next of ten mob pairs (i, i + 0xc) wakes
static void I2_WakeTick(const LanderEvent *ev)
{
    (void)ev;
    int32_t v = st.pairCount;
    if (v >= 2 && v <= 20 && (v & 1) == 0) {        // byte table 0x10001978 -> jump table 0x1000194c
        int32_t i = (v - 2) / 2;
        MobObj_Wake(i);
        MobObj_Wake(i + 0xc);
        if (i == 9)
            EventType_RemoveHandler(EV_TICK_100MS, I2_WakeTick);
    }
    st.pairCount++;
}

// 0x10001990  EV_TICK_100MS: the same pairs go to sleep again, last one first
static void I2_SleepTick(const LanderEvent *ev)
{
    (void)ev;
    int32_t v = st.pairCount;
    if (v >= 2 && v <= 20 && (v & 1) == 0) {        // byte table 0x10001b38 -> jump table 0x10001b0c
        int32_t i = (v - 2) / 2;
        MobObj_Sleep(i);
        MobObj_Sleep(i + 0xc);
        if (i == 0)
            EventType_RemoveHandler(EV_TICK_100MS, I2_SleepTick);
    }
    st.pairCount--;
}

// 0x10001b50  EV_ANIM_FINISHED anim 0xa: the drill 0x30058 is down -> the pairs wake
static void I2_OnDrillDown(const LanderEvent *ev)
{
    if (ev->arg[1] != 0xa)
        return;
    EventType_AddHandler(EV_TICK_100MS, NULL, I2_WakeTick);
    MobObj_Sleep(0x38);
    MobObj_Sleep(0x39);
    MobObj_Sleep(0x3a);
    MobObj_Sleep(0x3b);
    Sound_StopObject(I2_ANIM(0x58));
    EventType_RemoveHandler(EV_ANIM_FINISHED, I2_OnDrillDown);
}

// 0x10001bb0  EV_TRIGGER_ENTER 0x4003a: the drill site - 10 s later the drill crew arrives
static void I2_OnTriggerDrill(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x3a))
        return;
    InTime1(10000, MobObj_Wake, 0x18);
    InTime1(10000, MobObj_Wake, 0x37);
    InTime1(10000, MobObj_Wake, 0x3c);
    InTime1(10000, MobObj_Wake, 0x25);
    InTime1(10000, MobObj_Wake, 0x3e);
    InTime1(10000, MobObj_Wake, 0x1b);
    InTime1(10000, MobObj_Wake, 0x26);
    InTime3(10100, AnimPlay, I2_CRAWLER, 7, 0);
    InTime3(10100, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x25), 7, 0x60);
    InTime3(10100, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x3c), 7, 0x60);
    InTime3(10100, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x37), 7, 0x60);
    InTime3(10100, AnimPlay, MOB_ID(OT_LIGHTOBJECT, 0x3e), 0x3c, 0x40);
    InTime3(10100, AnimPlay, MOB_ID(OT_LIGHTOBJECT, 0x1b), 0x3d, 0x40);
    InTime3(10100, AnimPlay, MOB_ID(OT_LIGHTOBJECT, 0x26), 0x3e, 0x40);
    st.drillStarted = 1;
    AnimPlay(I2_ANIM(0x58), 0xa, 0x80);
    AnimSetUpdateInterval(I2_ANIM(0x58), 3);
    AnimPlay(I2_ANIM(0x56), 9, 0x80);
    AnimSetUpdateInterval(I2_ANIM(0x56), 3);
    Sound_PlayLoop(I2_ANIM(0x58), 1, -1);
    InTime2(20000, Sound_PlayOnce, I2_CRAWLER, 0x8b);
    MobObj_Wake(0x38);
    MobObj_Wake(0x39);
    MobObj_Wake(0x3a);
    MobObj_Wake(0x3b);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I2_OnDrillDown);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerDrill);
}

static void I2_OnTriggerDoor22In(const LanderEvent *ev);
static void I2_OnTriggerDoor22Out(const LanderEvent *ev);

// 0x10001da0  EV_ANIM_FINISHED 0x30056: the drill is up again
static void I2_OnDrillUp(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_ANIM(0x56))
        return;
    MobObj_Sleep(0x38);
    MobObj_Sleep(0x39);
    MobObj_Sleep(0x3a);
    MobObj_Sleep(0x3b);
    MobObj_Sleep(0x3e);
    MobObj_Sleep(0x1b);
    MobObj_Sleep(0x26);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerDoor22In);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerDoor22Out);
    Sound_StopObject(I2_ANIM(0x58));
    EventType_RemoveHandler(EV_ANIM_FINISHED, I2_OnDrillUp);
}

// 0x10001e30  EV_ANIM_FINISHED 0x1030018 (the crawler left; never removed): the pairs sleep, the
// drill rises
static void I2_OnCrawlerDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_CRAWLER)
        return;
    EventType_AddHandler(EV_TICK_100MS, NULL, I2_SleepTick);
    Sound_PlayLoop(I2_ANIM(0x58), 1, -1);
    MobObj_Wake(0x38);
    MobObj_Wake(0x39);
    MobObj_Wake(0x3a);
    MobObj_Wake(0x3b);
    InTime3(5000, AnimPlay, I2_ANIM(0x58), 0xa, 4);
    InTime3(5000, AnimPlay, I2_ANIM(0x56), 9, 4);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I2_OnDrillUp);
}

// 0x10001ee0 / 0x10001f20  EV_TRIGGER_ENTER / LEAVE 0x4005f: door 0x30022
static void I2_OnTriggerDoor22In(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x5f))
        return;
    AnimPlay(I2_ANIM(0x22), 1, 0);
    Sound_PlayOnce(I2_ANIM(0x22), 0xa9);
}

static void I2_OnTriggerDoor22Out(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x5f))
        return;
    AnimPlay(I2_ANIM(0x22), 1, 4);
    Sound_PlayOnce(I2_ANIM(0x22), 0xa9);
}

// 0x10001f60 / 0x10001fa0  EV_TRIGGER_ENTER / LEAVE 0x400d1: door 0x30025
static void I2_OnTriggerDoor25In(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xd1))
        return;
    AnimPlay(I2_ANIM(0x25), 2, 0);
    Sound_PlayOnce(I2_ANIM(0x25), 0xa9);
}

static void I2_OnTriggerDoor25Out(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xd1))
        return;
    AnimPlay(I2_ANIM(0x25), 2, 4);
    Sound_PlayOnce(I2_ANIM(0x25), 0xa9);
}

// 0x10001fe0  EV_TRIGGER_ENTER 0x400d5: door 0x30027; the first time the guard 0x10e002d starts
// its patrol sequence
static void I2_OnTriggerDoor27In(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xd5))
        return;
    if (st.patrolStarted == 0) {
        AnimPlay(I2_ANIM(0x27), 2, 0);
        AnimSeq_Start(MOB_ID(OT_ANIMTURRET, 0x2d), st.seqPatrol, 2);   // 0x10001120
        st.patrolStarted = 1;
        Sound_PlayOnce(I2_ANIM(0x27), 0xa9);
        return;
    }
    if (st.patrolStarted == 1)
        AnimPlay(I2_ANIM(0x27), 2, 0);
    Sound_PlayOnce(I2_ANIM(0x27), 0xa9);
}

// 0x10002070  EV_TRIGGER_LEAVE 0x400d5
static void I2_OnTriggerDoor27Out(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xd5))
        return;
    AnimPlay(I2_ANIM(0x27), 2, 4);
    Sound_PlayOnce(I2_ANIM(0x27), 0xa9);
}

// 0x100020b0 / 0x100020f0  EV_TRIGGER_ENTER / LEAVE 0x400d0: door 0x30028
static void I2_OnTriggerDoor28In(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xd0))
        return;
    AnimPlay(I2_ANIM(0x28), 2, 0);
    Sound_PlayOnce(I2_ANIM(0x28), 0xa9);
}

static void I2_OnTriggerDoor28Out(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xd0))
        return;
    AnimPlay(I2_ANIM(0x28), 2, 4);
    Sound_PlayOnce(I2_ANIM(0x28), 0xa9);
}

// 0x10002130  EV_TRIGGER_ENTER 0x40039: the gate 0x3002a and the clamps 0x300b5..b8 open (once)
static void I2_OnTriggerGate(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x39))
        return;
    AnimPlay(I2_ANIM(0x2a), 4, 0);
    AnimPlay(I2_ANIM(0xb7), 0x15, 0);
    AnimPlay(I2_ANIM(0xb5), 0x13, 0);
    AnimPlay(I2_ANIM(0xb8), 0x16, 0);
    AnimPlay(I2_ANIM(0xb6), 0x14, 0);
    st.gateOpen = 1;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerGate);
}

// 0x100021b0  EV_SWITCH 0xa00b9: the clamps close in two pairs, 3 s apart (both states close)
static void I2_OnSwitchClamps(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_SWITCH(0xb9))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(I2_ANIM(0xb7), 0x15, 4);
        AnimPlay(I2_ANIM(0xb5), 0x13, 4);
        InTime3(3000, AnimPlay, I2_ANIM(0xb8), 0x16, 4);
        InTime3(3000, AnimPlay, I2_ANIM(0xb6), 0x14, 4);
        return;
    }
    if (ev->arg[1] != 0)
        return;
    AnimPlay(I2_ANIM(0xb8), 0x16, 4);
    AnimPlay(I2_ANIM(0xb6), 0x14, 4);
    InTime3(3000, AnimPlay, I2_ANIM(0xb7), 0x15, 4);
    InTime3(3000, AnimPlay, I2_ANIM(0xb5), 0x13, 4);
}

// 0x10002280  EV_TRIGGER_ENTER 0x40038 / 0x100022c0  EV_TRIGGER_LEAVE 0x400d5 (sic): door 0x30023
static void I2_OnTriggerDoor23In(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x38))
        return;
    AnimPlay(I2_ANIM(0x23), 1, 0);
    Sound_PlayOnce(I2_ANIM(0x23), 0xa9);
}

static void I2_OnTriggerDoor23Out(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xd5))
        return;
    AnimPlay(I2_ANIM(0x23), 1, 4);
    Sound_PlayOnce(I2_ANIM(0x23), 0xa9);
}

// 0x10002300  EV_ANIM_FINISHED 0x3006b: the lift stopped
static void I2_OnLiftStopped(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_ANIM(0x6b))
        return;
    Sound_StopObject(I2_ANIM(0x6b));
    Sound_PlayOnce(I2_ANIM(0x6b), 0xcd);
    st.liftMoving = 0;
    EventType_RemoveHandler(EV_ANIM_FINISHED, I2_OnLiftStopped);
}

// 0x10002350  EV_SWITCH 0xa005d / 0xa005e (lift call switches at both ends): the lift 0x3006b
// moves up (on) or down; the other switch follows
static void I2_OnSwitchLift(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != I2_SWITCH(0x5d) && id != I2_SWITCH(0x5e))
        return;
    AnimPlay(I2_ANIM(0x6b), 0x10, ev->arg[1] == 1 ? 0 : 4);
    if (st.liftMoving != 0) {
        EventType_RemoveHandler(EV_ANIM_FINISHED, I2_OnLiftStopped);
        Sound_StopObject(I2_ANIM(0x6b));
    }
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I2_OnLiftStopped);
    Sound_PlayOnce(I2_ANIM(0x6b), 0xcb);
    InTime3(1000, Sound_PlayLoop, I2_ANIM(0x6b), 0xcc, 0x0fffffff);
    st.liftMoving = 1;
    ObjId other = (ObjId)(0x1400bb - (int32_t)ev->arg[0]);   // 0xa005d <-> 0xa005e
    int32_t state = (int32_t)ev->arg[1];
    if ((int32_t)Object_GetProperty(other, PROP_STATE) != state)
        Object_SetProperty(other, PROP_STATE, state);
}

// 0x10002440  EV_SWITCH 0xa005b: the lift's power - on: generator 0x80059 gone and call switch
// 0xa005d live; off: the other way round (and 0x8005a / 0xa005e likewise)
static void I2_OnSwitchLiftPower(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_SWITCH(0x5b))
        return;
    void (*hide)(ObjId), (*show)(ObjId);
    if (ev->arg[1] == 1) {
        hide = Object_SetDead;
        show = Object_SetAlive;
    } else if (ev->arg[1] == 0) {
        hide = Object_SetAlive;
        show = Object_SetDead;
    } else {
        return;
    }
    hide(OBJ_ID(OT_DESTRUCTIBLE, 0x59));
    show(I2_SWITCH(0x5d));
    hide(OBJ_ID(OT_DESTRUCTIBLE, 0x5a));
    show(I2_SWITCH(0x5e));
}

// 0x100024b0  EV_SWITCH 0xa00cb: door 0x30024 follows the switch
static void I2_OnSwitchDoor24(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_SWITCH(0xcb))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(I2_ANIM(0x24), 1, 0);
        Sound_PlayOnce(I2_ANIM(0x24), 0xa9);
    }
    if (ev->arg[1] == 0) {
        AnimPlay(I2_ANIM(0x24), 1, 4);
        Sound_PlayOnce(I2_ANIM(0x24), 0xa9);
    }
}

// 0x10002520  EV_ANIM_FINISHED 0x3006b: while 0x1000acf8 is 0 or 1, the lift's gate 0x30029 opens
static void I2_OnLiftGate(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_ANIM(0x6b))
        return;
    if (st.liftGate == 0 || st.liftGate == 1)
        AnimPlay(I2_ANIM(0x29), 3, 0);
}

// 0x10002550  EV_SWITCH 0xa005c: door 0x30026 follows the switch
static void I2_OnSwitchDoor26(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_SWITCH(0x5c))
        return;
    int32_t flags;
    if (ev->arg[1] == 1)
        flags = 0;
    else if (ev->arg[1] == 0)
        flags = 4;
    else
        return;
    AnimPlay(I2_ANIM(0x26), 2, flags);
    Sound_PlayOnce(I2_ANIM(0x26), 0xa9);
}

// 0x100025a0  EV_TRIGGER_ENTER 0x400e2: the heat-shield pod (mob 0x3d) appears; trigger 0x400e1
// waits for it
static void I2_OnTriggerPodAppear(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xe2))
        return;
    MobObj_Wake(0x3d);
    Object_SetProperty(I2_TRIGGER(0xe1), PROP_TARGET, I2_POD3D);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerPodAppear);
}

// 0x100025e0  EV_TRIGGER_ENTER 0x400e1 (the pod was delivered): the pod is taken in, the
// lift 0x300ca / mob 0x19 move
static void I2_OnTriggerPodDelivered(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0xe1))
        return;
    Object_SetProperty(I2_POD3D, PROP_TARGET, -1);
    Object_SetProperty(I2_TRIGGER(0xe1), PROP_TARGET, -1);
    MobObj_Sleep(0x3d);
    MobObj_Wake(0x19);
    AnimPlay(I2_ANIM(0xca), 0x18, 0x80);
    AnimPlay(MOB_ID(OT_ANIMOBJECT, 0x19), 0x28, 0x80);
    Sound_PlayOnce(I2_ANIM(0xca), 0x44c);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerPodDelivered);
}

// 0x10002680  EV_TRIGGER_ENTER 0x400dd / 0x400de: turrets 0x53 / 0x54 on (once)
static void I2_OnTriggerTurrets53(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig != I2_TRIGGER(0xdd) && trig != I2_TRIGGER(0xde))
        return;
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x53), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x54), PROP_STATE, 1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerTurrets53);
}

// 0x100026d0  EV_TRIGGER_ENTER 0x400cc..0x400cf: gun 0xe0087 rises (once)
static void I2_OnTriggerGun87(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)I2_TRIGGER(0xcc) || trig > (int32_t)I2_TRIGGER(0xcf))
        return;
    AnimPlay(OBJ_ID(OT_ANIMTURRET, 0x87), 0x11, 0x60);
    Object_SetProperty(OBJ_ID(OT_ANIMTURRET, 0x87), PROP_STATE, 1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerGun87);
}

// 0x10002720  EV_COLLISION {body, other, impulse}: bumping into a mine 0xf0088..0xf00a6 sets it off
static void I2_OnMineBumped(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[1];
    if (id < (int32_t)OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x88) || id > (int32_t)OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xa6))
        return;
    Sound_PlayOnce((ObjId)id, 0x102);
    Object_SetProperty((ObjId)ev->arg[1], PROP_EXPLOSION, 0x64);
    Object_SetProperty((ObjId)ev->arg[1], PROP_HITPOINTS, 0);
}

// 0x10002770  EV_ANIM_FINISHED 0x103001e: the sweeper (mob 0x1e + light 0x20) loops its run
static void I2_OnSweeperLoop(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MOB_ID(OT_ANIMOBJECT, 0x1e))
        return;
    AnimStop(MOB_ID(OT_LIGHTOBJECT, 0x20), 1);
    AnimStop(MOB_ID(OT_ANIMOBJECT, 0x1e), 1);
    Sound_StopObject(MOB_ID(OT_ANIMOBJECT, 0x1e));
    Sound_PlayOnce(MOB_ID(OT_ANIMOBJECT, 0x1e), 0xd7);
    MobObj_Sleep(0x20);
    MobObj_Wake(0x20);
    AnimPlay(MOB_ID(OT_ANIMOBJECT, 0x1e), 0xc, 0x80);
    AnimPlay(MOB_ID(OT_LIGHTOBJECT, 0x20), 0xc, 0x40);
    InTime3(1000, Sound_PlayLoop, MOB_ID(OT_ANIMOBJECT, 0x1e), 0xd6, -1);
}

// 0x10002830  EV_TRIGGER_ENTER 0x40064: the sweeper starts
static void I2_OnTriggerSweeperOn(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x64))
        return;
    MobObj_Wake(0x1e);
    MobObj_Wake(0x1f);
    MobObj_Wake(0x20);
    AnimPlay(MOB_ID(OT_LIGHTOBJECT, 0x20), 0xc, 0x40);
    AnimPlay(MOB_ID(OT_ANIMOBJECT, 0x1e), 0xc, 0x80);
    Sound_PlayOnce(MOB_ID(OT_ANIMOBJECT, 0x1e), 0xd5);
    InTime3(1000, Sound_PlayLoop, MOB_ID(OT_ANIMOBJECT, 0x1e), 0xd6, -1);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMOBJECT, 0x1e), 0x14);
    AnimSetUpdateInterval(MOB_ID(OT_LIGHTOBJECT, 0x20), 0x14);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I2_OnSweeperLoop);
}

// 0x100028f0  EV_TRIGGER_LEAVE 0x40064: the sweeper stops
static void I2_OnTriggerSweeperOff(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x64))
        return;
    EventType_RemoveHandler(EV_ANIM_FINISHED, I2_OnSweeperLoop);
    AnimStop(MOB_ID(OT_ANIMOBJECT, 0x1e), 1);
    AnimStop(MOB_ID(OT_LIGHTOBJECT, 0x20), 1);
    Sound_StopObject(MOB_ID(OT_ANIMOBJECT, 0x1e));
    Sound_PlayOnce(MOB_ID(OT_ANIMOBJECT, 0x1e), 0xd7);
    MobObj_Sleep(0x1e);
    MobObj_Sleep(0x1f);
    MobObj_Sleep(0x20);
}

// 0x10002970  the shuttle (mob 0x1c) starts a run
static void I2_ShuttleGo(void)
{
    MobObj_Wake(0x1c);
    AnimPlay(I2_SHUTTLE, 0xb, 0);
    Sound_PlayOnce(I2_SHUTTLE, 0xd5);
    InTime3(1000, Sound_PlayLoop, I2_SHUTTLE, 0xd6, -1);
    st.shuttleRunning = 1;
}

// 0x100029d0  the shuttle stops
static void I2_ShuttleStop(void)
{
    AnimStop(I2_SHUTTLE, 1);
    Sound_StopObject(I2_SHUTTLE);
    Sound_PlayOnce(I2_SHUTTLE, 0xd7);
    MobObj_Sleep(0x1c);
    st.shuttleRunning = 0;
}

// 0x10002a20  InTime: next run if the lander is still beyond plane 0x1a0061/62
static void I2_ShuttleAgain(void)
{
    if (st.shuttleArmed != 0 && st.shuttleRunning == 0)
        I2_ShuttleGo();                                  // (tail jump)
}

// 0x10002a40  EV_ANIM_FINISHED 0x103001c: a run is over, the next one in 0.5 s
static void I2_OnShuttleRun(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_SHUTTLE)
        return;
    InTime0(500, I2_ShuttleAgain);
}

// 0x10002a70  EV_TRIGGER_PLANE {player, plane, side} 0x1a0061 / 0x1a0062
static void I2_OnPlaneShuttle(const LanderEvent *ev)
{
    ObjId plane = (ObjId)ev->arg[1];
    if (plane != OBJ_ID(OT_TRIGGERPLANE, 0x61) && plane != OBJ_ID(OT_TRIGGERPLANE, 0x62))
        return;
    if (ev->arg[2] == 1) {
        st.shuttleArmed = 1;
        MobObj_Wake(0x30);
        MobObj_Wake(0x31);
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, I2_OnShuttleRun);
        return;
    }
    st.shuttleArmed = 0;
    if (st.shuttleRunning != 0)
        I2_ShuttleStop();
    MobObj_Sleep(0x30);
    MobObj_Sleep(0x31);
    EventType_RemoveHandler(EV_ANIM_FINISHED, I2_OnShuttleRun);
}

// 0x10002b00  EV_TRIGGER_ENTER 0x40086: the back way in - guards wake, gate and clamps open (once)
static void I2_OnTriggerBackDoor(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x86))
        return;
    MobObj_Wake(0x24);
    MobObj_Wake(0x34);
    MobObj_Wake(0x36);
    MobObj_Wake(0x35);
    MobObj_Wake(0x33);
    MobObj_Wake(0x32);
    AnimPlay(I2_ANIM(0x2a), 4, 0);
    AnimPlay(I2_ANIM(0xb7), 0x15, 0);
    AnimPlay(I2_ANIM(0xb5), 0x13, 0);
    AnimPlay(I2_ANIM(0xb8), 0x16, 0);
    AnimPlay(I2_ANIM(0xb6), 0x14, 0);
    st.gateOpen = 1;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerBackDoor);
}

// 0x10002bb0  EV_TICK_1S once the cache is hooked: above y = 65000 -> success, 2 = within 600
// seconds of the mission timer, 1 = later. Never removed (LevelEnd stops the scheduler).
static void I2_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(I2_LANDER, PROP_POSITION);
    if (pos[1] <= 65000)
        return;
    if (st.timer > 600) {
        Lander_SetCash(I2_LANDER, Lander_GetCash(I2_LANDER) + 100000);
        MissionScore_End(1);                             // 0x10001570
        return;
    }
    Lander_SetCash(I2_LANDER, Lander_GetCash(I2_LANDER) + 100000);
    MissionScore_End(2);
}

// 0x10002c30  EV_POD_HOOKED: the drill pod 0x10c003d, or the cache 0x10c0024 (the guards attack,
// the exit opens, the evac check starts)
static void I2_OnPodHooked(const LanderEvent *ev)
{
    ObjId pod = (ObjId)ev->arg[0];
    if (pod == I2_POD3D) {
        Pod_Script_SetupMass(pod, 2000);
        Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0x16a, 0);
        Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x802);
        return;
    }
    if (pod != I2_CACHE)
        return;
    Pod_Script_SetupMass(pod, 1000);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    WindowSetText(st.win, st.text[5]);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x34), 0x37, 0xe0);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x36), 0x38, 0xe0);
    InTime3(1000, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x35), 0x39, 0xe0);
    InTime3(1000, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x33), 0x3a, 0xe0);
    InTime3(2000, AnimPlay, MOB_ID(OT_ANIMTURRET, 0x32), 0x3b, 0xe0);
    AnimPlay(I2_ANIM(0xb4), 0x15, 4);
    AnimPlay(I2_ANIM(0xb3), 0x13, 4);
    AnimPlay(I2_ANIM(0x26), 2, 0);
    EventType_AddHandler(EV_TICK_1S, NULL, I2_EvacTick);
    Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, 0x34), 0x91, -1);
    Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, 0x36), 0x91, -1);
    Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, 0x35), 0x91, -1);
    Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, 0x33), 0x91, -1);
    Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, 0x32), 0x91, -1);
    Sound_PlayOnce(I2_ANIM(0x26), 0xa9);
}

// 0x10002e00  EV_TRIGGER_PLANE 0x1a007e / 0x1a007f: head light on the far side
static void I2_OnPlaneHeadLight(const LanderEvent *ev)
{
    ObjId plane = (ObjId)ev->arg[1];
    if (plane != OBJ_ID(OT_TRIGGERPLANE, 0x7e) && plane != OBJ_ID(OT_TRIGGERPLANE, 0x7f))
        return;
    if (ev->arg[2] == 1) {
        Lander_SetHeadLight(I2_LANDER, 0x31c, 0xc00);
        return;
    }
    Lander_SetHeadLight(I2_LANDER, 0x31c, 0);
}

// 0x10002e50  EV_TRIGGER_ENTER 0x4002c / 0x400d3: inside the complex (once)
static void I2_OnTriggerInside(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig != I2_TRIGGER(0x2c) && trig != I2_TRIGGER(0xd3))
        return;
    WindowSetText(st.win, st.text[1]);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerInside);
}

// 0x10002e90  EV_PICKUP {pickup, player}
static void I2_OnPickup(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)I2_PICKUP(0xc2) && id <= (int32_t)I2_PICKUP(0xc5)) {
        int32_t shield = (int32_t)Object_GetProperty(I2_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(I2_LANDER, 25);
        Object_SetProperty(I2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    id = (int32_t)ev->arg[0];
    if (id >= (int32_t)I2_PICKUP(0xc6) && id <= (int32_t)I2_PICKUP(0xc8)) {
        int32_t shield = (int32_t)Object_GetProperty(I2_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(I2_LANDER, 50);
        Object_SetProperty(I2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    id = (int32_t)ev->arg[0];
    if (id >= (int32_t)I2_PICKUP(0x2e) && id <= (int32_t)I2_PICKUP(0x30)) {
        Lander_SetCash(I2_LANDER, Lander_GetCash(I2_LANDER) + gMediumCash);
        Voice_CashAcquired(I2_LANDER);
    }
    id = (int32_t)ev->arg[0];
    if (id >= (int32_t)I2_PICKUP(0x31) && id <= (int32_t)I2_PICKUP(0x33)) {
        Lander_SetCash(I2_LANDER, Lander_GetCash(I2_LANDER) + gSmallCash);
        Voice_CashAcquired(I2_LANDER);
    }
    id = (int32_t)ev->arg[0];
    if (id >= (int32_t)I2_PICKUP(0x80) && id <= (int32_t)I2_PICKUP(0x81)) {
        if (Lander_AddMissiles(I2_LANDER, 1) == 0) {
            Voice_MissilesFull(I2_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(I2_LANDER);
    }
}

// 0x10003030  EV_TRIGGER_ENTER / LEAVE 0x40055: the hot zone's edge (until the heat shield)
static void I2_OnTriggerHotEdge(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != I2_TRIGGER(0x55))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        WindowSetText(st.win, st.text[2]);
        return;
    }
    WindowSetText(st.win, st.text[1]);
}

// 0x10003080  EV_TICK_100MS in the hot zone: -1 shield per tick, the alarm text below 0x32
static void I2_HeatTick(const LanderEvent *ev)
{
    (void)ev;
    if ((int32_t)Object_GetProperty(I2_LANDER, PROP_HITPOINTS) < 0x32)
        WindowSetText(st.win, st.text[4]);
    Sound_PlayOnce(I2_LANDER, 0xf1);
    Object_SetProperty(I2_LANDER, PROP_HITPOINTS, Object_GetProperty(I2_LANDER, PROP_HITPOINTS) - 1);
}

// 0x100030f0  EV_TRIGGER_ENTER / LEAVE 0x40065 / 0x40066: the hot zone
static void I2_OnTriggerHot(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig != I2_TRIGGER(0x65) && trig != I2_TRIGGER(0x66))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        WindowSetText(st.win, st.text[3]);
        EventType_AddHandler(EV_TICK_100MS, NULL, I2_HeatTick);
        return;
    }
    WindowSetText(st.win, st.text[1]);
    EventType_RemoveHandler(EV_TICK_100MS, I2_HeatTick);
}

// 0x10003160  InTime: back to the main objective
static void I2_SayFindCache(void)
{
    WindowSetText(st.win, st.text[1]);
}

// 0x10003180  EV_PICKUP 0x130060: the heat shield - the hot zone no longer hurts
static void I2_OnPickupHeatShield(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != I2_PICKUP(0x60))
        return;
    st.heatShield = 1;
    Voice_HeatShieldAcquired(I2_LANDER);
    WindowSetText(st.win, st.text[6]);
    InTime0(5000, I2_SayFindCache);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerHotEdge);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, I2_OnTriggerHotEdge);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, I2_OnTriggerHot);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, I2_OnTriggerHot);
}

// 0x10003210  EV_TICK_1S: mission timer (seconds) for the result
static void I2_TimerTick(const LanderEvent *ev)
{
    (void)ev;
    st.timer++;
}

// 0x100032b0  common set-up
static void I2_Setup(void)
{
    st.text = i2_text[gLanguage];
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x25), PROP_ARMOUR, 0x14);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x3c), PROP_ARMOUR, 0x14);
    Object_SetProperty(MOB_ID(OT_ANIMTURRET, 0x37), PROP_ARMOUR, 0x14);
    Object_SetProperty(OBJ_ID(OT_ANIMTURRET, 0x87), PROP_ARMOUR, 0x23);
    for (int32_t id = OBJ_ID(OT_TURRET, 0x44); id <= (int32_t)OBJ_ID(OT_TURRET, 0x54); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x19);
    for (int32_t id = OBJ_ID(OT_TURRET, 0x82); id <= (int32_t)OBJ_ID(OT_TURRET, 0x85); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = OBJ_ID(OT_TURRET, 0xba); id <= (int32_t)OBJ_ID(OT_TURRET, 0xc1); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x19);
    for (int32_t n = 0x29; n <= 0x36; n++) {
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, n), PROP_ARMOUR, 0x28);
        Object_SetProperty(MOB_ID(OT_ANIMTURRET, n), PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = OBJ_ID(OT_FUELDUMP, 0x3e); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0x42); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    SetGravity(13);
    gAirResistance = 0xf;
    gMainThrottlePower = 0x19;
    gNbWaypoints = i2_nbWaypoints;
    gWaypoints = i2_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 550, 12);
    WindowSetText(st.win, st.text[0]);
    gTurretTypes[0] = &i2_turretType0;
    gTurretTypes[21] = &i2_turretType21;
    gTurretTypes[3] = &i2_turretType3;
    gTurretTypes[5] = &i2_turretType5;
    gTurretTypes[7] = &i2_turretType7;
    gTurretTypes[13] = &i2_turretType13;
    gTurretTypes[15] = &i2_turretType15;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerDrill);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I2_OnCrawlerDone);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerDoor25In);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerDoor25Out);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerDoor27In);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerDoor27Out);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerDoor28In);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerDoor28Out);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerGate);
    EventType_AddHandler(EV_SWITCH, NULL, I2_OnSwitchClamps);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerDoor23In);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerDoor23Out);
    EventType_AddHandler(EV_SWITCH, NULL, I2_OnSwitchLift);
    EventType_AddHandler(EV_SWITCH, NULL, I2_OnSwitchLiftPower);
    EventType_AddHandler(EV_SWITCH, NULL, I2_OnSwitchDoor24);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, I2_OnLiftGate);
    EventType_AddHandler(EV_ANIM_WRAPPED, NULL, I2_OnLiftGate);
    EventType_AddHandler(EV_SWITCH, NULL, I2_OnSwitchDoor26);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerPodAppear);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerPodDelivered);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerTurrets53);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerGun87);
    EventType_AddHandler(EV_COLLISION, NULL, I2_OnMineBumped);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerSweeperOn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerSweeperOff);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, I2_OnPlaneShuttle);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerBackDoor);
    EventType_AddHandler(EV_POD_HOOKED, NULL, I2_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, I2_OnPlaneHeadLight);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerInside);
    EventType_AddHandler(EV_PICKUP, NULL, I2_OnPickup);
    EventType_AddHandler(EV_PICKUP, NULL, I2_OnPickupHeatShield);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerHotEdge);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerHotEdge);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, I2_OnTriggerHot);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, I2_OnTriggerHot);
    EventType_AddHandler(EV_TICK_1S, NULL, I2_TimerTick);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10003220
    Object_SetDead(I2_SWITCH(0x5d));
    Object_SetDead(I2_SWITCH(0x5e));
    AnimPlay(I2_ANIM(0xc9), 0x17, 0x81);
    Sound_PlayLoop(I2_ANIM(0xc9), 0x44d, -1);
    static const uint16_t mineDelay[20] = {               // (unrolled) mines 0xf0088..0xf009b
        1000, 2000, 3000, 1500, 2500, 500, 750, 1250, 1750, 2250, 2750, 1000, 2000, 3000, 1500, 2500,
        250, 750, 1750, 2750,
    };
    for (int32_t i = 0; i < 20; i++)
        InTime3(mineDelay[i], AnimPlay, OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x88 + i), 0x12, 1);
    AnimPlay(I2_ANIM(0x3c), 8, 0x81);
    Sound_PlayLoop(I2_ANIM(0x3c), 0xb5, -1);
    AnimSetUpdateInterval(I2_ANIM(0x3c), 0x1e);
    AnimPlay(I2_ANIM(0x3d), 8, 0x81);
    Sound_PlayLoop(I2_ANIM(0x3d), 0xb5, -1);
    AnimSetUpdateInterval(I2_ANIM(0x3d), 0x3c);
    AnimPlay(I2_ANIM(0xe3), 0x29, 0x81);
    Sound_PlayLoop(I2_ANIM(0xe3), 0xb5, -1);
    AnimSetUpdateInterval(I2_ANIM(0xe3), 0x96);
    AnimPlay(I2_ANIM(0x67), 0xd, 1);
    Sound_PlayLoop(I2_ANIM(0x67), 0xee, -1);
    AnimPlay(I2_ANIM(0x68), 0xd, 1);
    Sound_PlayLoop(I2_ANIM(0x68), 0xee, -1);
    AnimPlay(I2_ANIM(0x36), 6, 1);
    Sound_PlayLoop(I2_ANIM(0x36), 0xbc, -1);
    AnimSetUpdateInterval(I2_ANIM(0x36), 0x64);
    InTime3(1000, AnimPlay, I2_ANIM(0x37), 6, 1);
    Sound_PlayLoop(I2_ANIM(0x37), 0xbc, -1);
    AnimSetUpdateInterval(I2_ANIM(0x37), 0x32);
    AnimPlay(I2_ANIM(0x13), 0, 0x81);
    AnimPlay(I2_ANIM(0x1e), 0, 0x81);
    AnimPlay(I2_ANIM(0x21), 0, 0x81);
    AnimPlay(I2_ANIM(0x69), 0xe, 1);
    Sound_PlayLoop(I2_ANIM(0x69), 0xee, -1);
    AnimSetUpdateInterval(I2_ANIM(0x69), 0x64);
    AnimPlay(I2_ANIM(0x6a), 0xf, 1);
    Sound_PlayLoop(I2_ANIM(0x6a), 0xee, -1);
    AnimPlay(I2_ANIM(0x34), 0x2a, 1);
    for (int32_t n = 0xa7; n <= 0xb2; n++)               // (unrolled) anims 0x2b..0x36
        AnimPlay(I2_ANIM(n), 0x2b + (n - 0xa7), 1);
    AnimPlay(MOB_ID(OT_ANIMTURRET, 0x29), 0x19, 0x62);
    Sound_PlayLoop(MOB_ID(OT_ANIMTURRET, 0x29), 0x91, -1);
    AnimSetUpdateInterval(MOB_ID(OT_ANIMTURRET, 0x29), 0x14);
    AnimPlay(I2_ANIM(0xb4), 0x15, 0);
    AnimPlay(I2_ANIM(0xb3), 0x13, 0);
    AnimSeq_Init();                                      // 0x10001190
    TexAnimPlay(0x3e, 2, 100);
    TexAnimPlay(0x79, 1, 100);
    MobObj_Wake(0x29);
    MobObj_Wake(0x2b);
    MobObj_Wake(0x2c);
    MobObj_Wake(0x2d);
    MobObj_Wake(0x2e);
    MobObj_Wake(0x2f);
    MobObj_Wake(0x16);
    MobObj_Wake(0x17);
    MobObj_Wake(0x28);
    MobObj_Wake(0x21);
    Object_SetProperty(OBJ_ID(OT_ANIMTURRET, 0x87), PROP_STATE, 0);
    Object_SetProperty(I2_TRIGGER(0x57), PROP_TARGET, I2_CRAWLER);
    MissionScore_Init(I2_LANDER, 600000, 5);             // 0x100016f0
    Media_AudioPlay(0);
}

// 0x10003c00  LevelStartLevel_Lander (export 0x100011e0)
static void I2_StartLander(void)
{
    MobObj_Wake(I2_MOB_LANDER);
    MobObj_Wake(I2_MOB_CAMERA);
    Object_SetProperty(I2_CAMERA, PROP_TARGET, I2_LANDER);
    I2_Setup();
}

// 0x10003c40  LevelStartLevel_Flycam (export 0x100011f0)
static void I2_StartFlycam(void)
{
    I2_Setup();
    MobObj_Wake(I2_MOB_FLYCAM);
}

// 0x10003c60  LevelStopLevel (export 0x10001200): empty
static void I2_Stop(void)
{
}

// 0x10001210
static uint32_t I2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x100011d0  LevelLoadLevel: two calls of the empty 0x10003c60, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t I2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    memcpy(st.seqPatrol, i2_seqPatrol, sizeof st.seqPatrol);
    MissionScore_Reset();
    AnimSeq_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText i2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10008160, .count = 3 * 7, .out = i2_text },
    { 0 },
};

const mission_script_t mission_io_2 = {
    .name          = "IO\\2",
    .get_version   = I2_GetVersion,
    .load          = I2_Load,
    .start_lander  = I2_StartLander,
    .start_flycam  = I2_StartFlycam,
    .start_animcam = NULL,
    .stop          = I2_Stop,
    .text          = i2_gametext,
};
