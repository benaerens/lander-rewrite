// MISSIONS\NEPTUNE\1\MISSIONR.DLL - faithful C port.
//
// Mission: a station deep in Neptune's atmosphere (gravity 22). Find the low-level entrance at
// waypoint 1; trigger 0x40066 inside removes two gate turrets and starts the station's machinery
// (the AI core objective). Destroying the four core guards (turrets 0x1e..0x21)
// raises the core's shutters, whose 17 panels (destructibles 0x80009..0x8001d) must be destroyed while
// a charging cannon (0x8000e) fires a beam (particle emitter mob 0x11 between the cannon and the
// lander, damaging it while there is line of sight). Then "Exit station"; trigger 0x40063 ends the
// mission (MissionScore_End(1)). Below y = -108800 the pressure hurts (shield -0x20/-0x40/-0x80, then
// destroyed at -160000), the exterior ambient darkens with depth. Par 600 s, 8 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10004590), LevelStartLevel_Flycam 0x10001020
// (-> 0x100045d0), LevelStopLevel 0x10001030 (-> empty 0x10001050), LevelGetVersion 0x10001040.
// The score library (0x10001060-0x100015ff, Init 0x10001530, End 0x100013b0) and the
// destroyed-sound handler (0x10003b20) are the common code in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define NE1_LANDER        MOB_ID(OT_LANDER, 0xc)            // 0x0101000c
#define NE1_CAMERA        MOB_ID(OT_CAMERA, 1)              // 0x01060001
#define NE1_MOB_LANDER    0xc
#define NE1_MOB_CAMERA    1
#define NE1_MOB_FLYCAM    0xa
#define NE1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))        // 0x000300nn
#define NE1_MOBANIM(n)    MOB_ID(OT_ANIMOBJECT, (n))        // 0x010300nn
#define NE1_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))        // 0x000400nn
#define NE1_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))            // 0x000a00nn
#define NE1_TURRET(n)     OBJ_ID(OT_TURRET, (n))
#define NE1_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))      // 0x000800nn
#define NE1_ANIMDESTR(n)  OBJ_ID(OT_ANIMDESTRUCTIBLE, (n))  // 0x000f00nn
#define NE1_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))
#define NE1_BOSS          MOB_ID(OT_ANIMTURRET, 2)          // 0x010e0002 the station's AI defence
#define NE1_CANNON        NE1_DESTR(0x0e)                   // 0x0008000e
#define NE1_CHARGE_LIGHT  OBJ_ID(OT_LIGHTOBJECT, 0x54)      // 0x00160054
#define NE1_BEAM_LIGHT    MOB_ID(OT_LIGHTOBJECT, 0x12)      // 0x01160012
#define NE1_BEAM_END      MOB_ID(OT_PARTICLEEMITTER, 0x11)  // 0x010d0011 emitter mob 0x11: target point
#define NE1_BEAM_START    (NE1_BEAM_END | 0x04000000u)      // 0x050d0011 (sic: the emitter's other end)
#define NE1_SHAKER        MOB_ID(OT_INERTIALFRAME, 0x16)    // 0x01140016
#define NE1_LIFT          NE1_MOBANIM(0x17)                 // 0x01030017

// ---- static data (.data) --------------------------------------------------------------------------
// 0x10009160: [language][4]
static const char *ne1_text[3][4];
static const int32_t  ne1_nbWaypoints = 1;           // 0x10009190
static const Waypoint ne1_waypoints[1] = {           // 0x10009198
    { -44800, 3840 },
};
static const TurretType ne1_turretType21 = {         // 0x100091a0 -> gTurretTypes[21]
    { 52, 27, 518, 0 }, 0x28, 0, 4, 10, 70, 0x30, 1024, 12000, 0
};
static const TurretType ne1_turretType22 = {         // 0x100091c0 -> gTurretTypes[22]
    { 134, 0, 1026, 0 }, 0x01, 1, 4, 6, 80, 0x3c, 1024, 12000, 0
};
static const TurretType ne1_turretType13 = {         // 0x100091e0 -> gTurretTypes[13]
    { 61, 0, 198, 0 }, 0x09, 2, 4, 6, 80, 0x30, 1024, 7680, 0
};
static const TurretType ne1_turretType15 = {         // 0x10009200 -> gTurretTypes[15]
    { 61, 0, 198, 0 }, 0x05, 2, 3, 5, 60, 0x20, 800, 12000, 0
};
static const int32_t ne1_depthInit[2] = { -108800, -108800 };   // 0x1000921c / 0x10009220

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      depthNext;     // 0x1000921c (.data) next depth threshold that hurts
    int32_t      depthLast;     // 0x10009220 (.data) last threshold passed
    int32_t      gateGuards;    // 0x1000baec gate turrets 0x10b / 0x10e destroyed
    int32_t      liftPhase;     // 0x1000baf4
    int32_t      coreGuards;    // 0x1000bafc core guards destroyed
    int32_t      panels;        // 0x1000bb00 core panels destroyed
    int32_t      inLitZone;     // 0x1000bb08
    int32_t      lightsOn;      // 0x1000bb0c turrets 0x116..11a revealed
    int32_t      crushersStopped;// 0x1000bb10
    int32_t      gate22Open;    // 0x1000bb14 gate 0x30022..25 open
    int32_t      headLightOn;   // 0x1000bb18
    int32_t      hatch0BOpen;   // 0x1000bb1c
    int32_t      door0E9Open;   // 0x1000bb20
    int32_t      hum;           // 0x1000bb24
    Window      *win;           // 0x1000bb28 objective line
    int32_t      cannonOn;      // 0x1000bb2c
    int32_t      charging;      // 0x1000bb30
    int32_t      beamOn;        // 0x1000bb34
    int32_t      alarmTicks;    // 0x1000bb38
    int32_t      quakeTicks;    // 0x1000bb3c
    int32_t      depth;         // 0x1000bb40 lander y
    int32_t      charge;        // 0x1000bf80 cannon charge ticks
    int32_t      depthShield;   // 0x1000bf84 (written, never read)
    const char *const *text;    // 0x1000bf88 = ne1_text[gLanguage]
} st;

// 0x10001610  EV_DESTROYED: both gate turrets 0x10b/0x10e destroyed: gate 0x3006e opens. (Masks the
// event's object id in place.)
static void NE1_OnGateGuardDestroyed(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if ((ObjId)e->arg[0] != NE1_TURRET(0x10b) && (ObjId)e->arg[0] != NE1_TURRET(0x10e))
        return;
    if (++st.gateGuards != 2)
        return;
    Sound_PlayOnce(NE1_ANIM(0x6e), 0xa8);
    AnimPlay(NE1_ANIM(0x6e), 0x1c, 0);
    EventType_RemoveHandler(EV_DESTROYED, NE1_OnGateGuardDestroyed);
}

// 0x10001680  EV_TICK_1S: machine 0x30060 hums every 5 s
static void NE1_HumTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.hum != 5)
        return;
    Sound_PlayOnce(NE1_ANIM(0x60), 0xb2);
    st.hum = 0;
}

// 0x100016b0  EV_TRIGGER_ENTER 0x40066: inside the station: the gate turrets go, the gate closes
// behind, the machinery starts
static void NE1_OnStationEntered(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0x66))
        return;
    Object_SetDead(NE1_TURRET(0x10b));
    Object_SetDead(NE1_TURRET(0x10e));
    WindowSetText(st.win, st.text[1]);           // the AI core objective
    AnimPlay(NE1_ANIM(0x6e), 0x1c, 4);
    Sound_PlayOnce(NE1_ANIM(0x6e), 0xa8);
    AnimPlay(NE1_ANIM(0x6b), 0x1e, 0x81);
    Sound_PlayLoop(NE1_ANIM(0x6b), 0xbc, -1);
    AnimSetUpdateInterval(NE1_ANIM(0x6b), 0xc8);
    AnimPlay(NE1_ANIM(0x6c), 0x1f, 0x81);
    Sound_PlayLoop(NE1_ANIM(0x6c), 0xbc, -1);
    AnimSetUpdateInterval(NE1_ANIM(0x6c), 0xc8);
    AnimPlay(NE1_ANIM(0x6d), 0x20, 0x81);
    Sound_PlayLoop(NE1_ANIM(0x6d), 0xbc, -1);
    AnimSetUpdateInterval(NE1_ANIM(0x6d), 0xc8);
    AnimPlay(NE1_ANIM(0xe8), 0x22, 0x81);
    Sound_PlayLoop(NE1_ANIM(0xe8), 0x370, -1);
    AnimSetUpdateInterval(NE1_ANIM(0xe8), 0xc8);
    AnimPlay(NE1_ANIM(0x10a), 0x23, 0x82);
    Sound_PlayLoop(NE1_ANIM(0x10a), 0x370, -1);
    AnimSetUpdateInterval(NE1_ANIM(0x10a), 0xc8);
    AnimPlay(NE1_ANIM(0x85), 0x24, 0x82);
    Sound_PlayLoop(NE1_ANIM(0x85), 0x370, -1);
    AnimSetUpdateInterval(NE1_ANIM(0x85), 0xc8);
    AnimPlay(NE1_ANIM(0x84), 0x25, 0x82);
    Sound_PlayLoop(NE1_ANIM(0x84), 0x370, -1);
    AnimSetUpdateInterval(NE1_ANIM(0x84), 0xc8);
    AnimPlay(NE1_ANIM(0x60), 0x26, 1);
    EventType_AddHandler(EV_TICK_1S, NULL, NE1_HumTick);
    AnimPlay(NE1_ANIM(0x5d), 0x27, 0x81);
    Sound_PlayLoop(NE1_ANIM(0x5d), 0xb1, -1);
    AnimPlay(NE1_ANIM(0x5c), 0x28, 0x81);
    Sound_PlayLoop(NE1_ANIM(0x5c), 0xb3, -1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnStationEntered);
}

// 0x10001900  EV_SWITCH 0xa005f: door 0x300e9
static void NE1_OnSwitchDoorE9(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0x5f))
        return;
    if (ev->arg[1] == 1) {
        Sound_PlayOnce(NE1_ANIM(0xe9), 0xaa);
        AnimPlay(NE1_ANIM(0xe9), 0x21, 0);
        st.door0E9Open = 1;
        return;
    }
    if (ev->arg[1] == 0) {
        Sound_PlayOnce(NE1_ANIM(0xe9), 0xaa);
        AnimPlay(NE1_ANIM(0xe9), 0x21, 4);
        st.door0E9Open = 0;
    }
}

// 0x10001980  EV_TRIGGER_ENTER 0x400bb: door 0x300e9 shuts, its switch goes off
static void NE1_OnTriggerBB(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0xbb))
        return;
    st.door0E9Open = 1;
    AnimPlay(NE1_ANIM(0xe9), 0x21, 4);
    Sound_PlayOnce(NE1_ANIM(0xe9), 0xaa);
    st.door0E9Open = 0;
    Object_SetProperty(NE1_SWITCH(0x5f), PROP_STATE, 0);
}

// 0x100019e0  EV_TRIGGER_ENTER 0x400da: 0xf00d8 anim 0x29
static void NE1_OnTriggerDA(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0xda))
        return;
    AnimPlay(NE1_ANIMDESTR(0xd8), 0x29, 0);
    Sound_PlayOnce(NE1_ANIMDESTR(0xd8), 0xa8);
}

// 0x10001a20  EV_TRIGGER_ENTER 0x400d9: 0xf00d8 back (once)
static void NE1_OnTriggerD9(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0xd9))
        return;
    AnimPlay(NE1_ANIMDESTR(0xd8), 0x29, 4);
    Sound_PlayOnce(NE1_ANIMDESTR(0xd8), 0xa8);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnTriggerD9);
}

// 0x10001a70  EV_TRIGGER_ENTER/LEAVE 0x400ba: door 0x300b9
static void NE1_OnDoorB9(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0xba))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(NE1_ANIM(0xb9), 0xab);
        AnimPlay(NE1_ANIM(0xb9), 0x1d, 0);
        return;
    }
    Sound_PlayOnce(NE1_ANIM(0xb9), 0xab);
    AnimPlay(NE1_ANIM(0xb9), 0x1d, 4);
}

// 0x10001ad0  EV_ANIM_WRAPPED: crushers 0x300c5..c9 thump at each cycle and 8.2 s later
static void NE1_OnCrusherCycle(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)NE1_ANIM(0xc5) || id > (int32_t)NE1_ANIM(0xc9))
        return;
    Sound_PlayOnce((ObjId)id, 0x373);
    InTime2(0x2008, Sound_PlayOnce, ev->arg[0], 0x373);
}

// 0x10001b20  EV_TRIGGER_ENTER 0x400d2: the crusher hall starts (doors 0x300cf/d0/d1, crushers
// 0x300c5..c9 staggered); 0x400cb: the doors close (once)
static void NE1_OnCrusherHall(const LanderEvent *ev)
{
    ObjId t = (ObjId)ev->arg[1];
    if (t == NE1_TRIGGER(0xd2)) {
        AnimPlay(NE1_ANIM(0xd1), 0x2a, 0);
        InTime3(250, AnimPlay, NE1_ANIM(0xcf), 0x2b, 0);
        InTime3(250, AnimPlay, NE1_ANIM(0xd0), 0x2b, 0);
        Sound_PlayOnce(NE1_ANIM(0xd1), 0xda);
        InTime2(250, Sound_PlayOnce, NE1_ANIM(0xcf), 0x3f7);
        InTime2(250, Sound_PlayOnce, NE1_ANIM(0xd0), 0x3f7);
        InTime3(1500, AnimPlay, NE1_ANIM(0xc5), 0x2d, 1);
        InTime2(1500, Sound_PlayOnce, NE1_ANIM(0xc5), 0x373);
        InTime2(9700, Sound_PlayOnce, NE1_ANIM(0xc5), 0x373);
        AnimSetUpdateInterval(NE1_ANIM(0xc5), 0x14);
        InTime3(750, AnimPlay, NE1_ANIM(0xc6), 0x2d, 1);
        InTime2(750, Sound_PlayOnce, NE1_ANIM(0xc8), 0x373);      // sic: 0x300c8, not c6
        InTime2(8950, Sound_PlayOnce, NE1_ANIM(0xc8), 0x373);
        AnimSetUpdateInterval(NE1_ANIM(0xc6), 0x14);
        InTime3(2100, AnimPlay, NE1_ANIM(0xc7), 0x2d, 1);
        InTime2(2100, Sound_PlayOnce, NE1_ANIM(0xc8), 0x373);     // sic
        InTime2(10300, Sound_PlayOnce, NE1_ANIM(0xc8), 0x373);
        AnimSetUpdateInterval(NE1_ANIM(0xc7), 0x14);
        InTime3(1200, AnimPlay, NE1_ANIM(0xc8), 0x2d, 1);
        InTime2(1200, Sound_PlayOnce, NE1_ANIM(0xc8), 0x373);
        InTime2(9400, Sound_PlayOnce, NE1_ANIM(0xc8), 0x373);
        AnimSetUpdateInterval(NE1_ANIM(0xc8), 0x14);
        InTime3(300, AnimPlay, NE1_ANIM(0xc9), 0x2d, 1);
        InTime2(300, Sound_PlayOnce, NE1_ANIM(0xc9), 0x373);
        InTime2(8500, Sound_PlayOnce, NE1_ANIM(0xc9), 0x373);
        AnimSetUpdateInterval(NE1_ANIM(0xc9), 0x14);
        EventType_AddHandler(EV_ANIM_WRAPPED, NULL, NE1_OnCrusherCycle);
        return;
    }
    if (t != NE1_TRIGGER(0xcb))
        return;
    InTime3(250, AnimPlay, NE1_ANIM(0xd1), 0x2a, 4);
    Sound_PlayOnce(NE1_ANIM(0xd1), 0xda);
    AnimPlay(NE1_ANIM(0xcf), 0x2b, 4);
    AnimPlay(NE1_ANIM(0xd0), 0x2b, 4);
    Sound_PlayOnce(NE1_ANIM(0xcf), 0x3f7);
    Sound_PlayOnce(NE1_ANIM(0xd0), 0x3f7);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnCrusherHall);
}

// 0x10001e00  EV_TRIGGER_ENTER 0x40109 / 0x40108: door 0x30107 opens / closes (once)
static void NE1_OnDoor107(const LanderEvent *ev)
{
    ObjId t = (ObjId)ev->arg[1];
    if (t == NE1_TRIGGER(0x109)) {
        Sound_PlayOnce(NE1_ANIM(0x107), 0xab);
        AnimPlay(NE1_ANIM(0x107), 0x2f, 0);
        return;
    }
    if (t != NE1_TRIGGER(0x108))
        return;
    Sound_PlayOnce(NE1_ANIM(0x107), 0xab);
    AnimPlay(NE1_ANIM(0x107), 0x2f, 4);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnDoor107);
}

// 0x10001e80  EV_TRIGGER_ENTER: door 0x30106 (plays its sound on *every* trigger entry)
static void NE1_OnDoor106(const LanderEvent *ev)
{
    Sound_PlayOnce(NE1_ANIM(0x106), 0xab);
    ObjId t = (ObjId)ev->arg[1];
    if (t == NE1_TRIGGER(0x109)) {
        AnimPlay(NE1_ANIM(0x106), 0x30, 0);
        return;
    }
    if (t != NE1_TRIGGER(0x108))
        return;
    Sound_PlayOnce(NE1_ANIM(0x106), 0xab);
    AnimPlay(NE1_ANIM(0x106), 0x30, 4);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnDoor106);
}

// 0x10001f00  EV_SWITCH 0xa0031: the barrier 0xf00bf..c1 off (1) / on (0)
static void NE1_OnSwitchBarrier(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0x31))
        return;
    Sound_PlayOnce(NE1_SWITCH(0x31), 0xeb);
    if (ev->arg[1] == 1) {
        AnimStop(NE1_ANIMDESTR(0xbf), 0);
        AnimStop(NE1_ANIMDESTR(0xc0), 0);
        AnimStop(NE1_ANIMDESTR(0xc1), 0);
        Object_SetDead(NE1_ANIMDESTR(0xbf));
        Object_SetDead(NE1_ANIMDESTR(0xc0));
        Object_SetDead(NE1_ANIMDESTR(0xc1));
        return;
    }
    Object_SetAlive(NE1_ANIMDESTR(0xbf));
    Object_SetAlive(NE1_ANIMDESTR(0xc0));
    Object_SetAlive(NE1_ANIMDESTR(0xc1));
    AnimPlay(NE1_ANIMDESTR(0xbf), 0x33, 0x82);
    AnimPlay(NE1_ANIMDESTR(0xc0), 0x33, 0x82);
    AnimPlay(NE1_ANIMDESTR(0xc1), 0x34, 0x82);
}

// 0x10001fe0  EV_SWITCH 0xa0061: gate 0x30022..25
static void NE1_OnSwitchGate22(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0x61))
        return;
    Sound_PlayOnce(NE1_SWITCH(0x61), 0xeb);
    if (ev->arg[1] == 1) {
        AnimSetUpdateInterval(NE1_ANIM(0x22), 2);
        AnimSetUpdateInterval(NE1_ANIM(0x23), 2);
        AnimSetUpdateInterval(NE1_ANIM(0x24), 2);
        AnimSetUpdateInterval(NE1_ANIM(0x25), 2);
        for (int32_t id = (int32_t)NE1_ANIM(0x22); id <= (int32_t)NE1_ANIM(0x25); id++)
            Sound_PlayOnce((ObjId)id, 0xa9);
        AnimPlay(NE1_ANIM(0x22), 0x36, 0);
        AnimPlay(NE1_ANIM(0x23), 0x36, 0);
        InTime3(500, AnimPlay, NE1_ANIM(0x24), 0x36, 0);
        InTime3(500, AnimPlay, NE1_ANIM(0x25), 0x36, 0);
        st.gate22Open = 1;
        return;
    }
    if (ev->arg[1] != 0)
        return;
    for (int32_t id = (int32_t)NE1_ANIM(0x22); id <= (int32_t)NE1_ANIM(0x25); id++)
        Sound_PlayOnce((ObjId)id, 0xa9);
    InTime3(500, AnimPlay, NE1_ANIM(0x22), 0x36, 4);
    InTime3(500, AnimPlay, NE1_ANIM(0x23), 0x36, 4);
    AnimPlay(NE1_ANIM(0x24), 0x36, 4);
    AnimPlay(NE1_ANIM(0x25), 0x36, 4);
    st.gate22Open = 0;
}

// 0x10002150  (InTime target) door 0x30094 closes again, its switch off
static void NE1_Door94Close(void)
{
    Object_SetProperty(NE1_SWITCH(0x62), PROP_STATE, 0);
    Sound_PlayOnce(NE1_ANIM(0x94), 0xab);
    AnimPlay(NE1_ANIM(0x94), 0x35, 4);
}

// 0x10002190  EV_SWITCH 0xa0062: door 0x30094 opens for 15 s (switching it off is refused)
static void NE1_OnSwitchDoor94(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0x62))
        return;
    Sound_PlayOnce(NE1_SWITCH(0x62), 0xeb);
    if (ev->arg[1] == 1) {
        Sound_PlayOnce(NE1_ANIM(0x94), 0xab);
        AnimPlay(NE1_ANIM(0x94), 0x35, 0);
        InTime0(15000, NE1_Door94Close);
    }
    if (ev->arg[1] == 0)
        Object_SetProperty(NE1_SWITCH(0x62), PROP_STATE, 1);
}

static void NE1_OnLiftDone(const LanderEvent *ev);
static void NE1_OnLiftCall(const LanderEvent *ev);

// 0x10002210  EV_TRIGGER_ENTER 0x400db (lift up): the lift goes back down
static void NE1_OnLiftTop(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0xdb) || st.liftPhase != 1)
        return;
    Sound_PlayOnce(NE1_LIFT, 0xce);
    InTime3(840, Sound_PlayLoop, NE1_LIFT, 0xcf, -1);
    AnimPlay(NE1_LIFT, 0x37, 4);
}

// 0x10002270  EV_ANIM_FINISHED of the lift
static void NE1_OnLiftDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_LIFT)
        return;
    Sound_StopObject(NE1_LIFT);
    Sound_PlayOnce(NE1_LIFT, 0xd0);
    if (st.liftPhase == 0) {
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnLiftTop);
        st.liftPhase = 1;
        return;
    }
    if (st.liftPhase != 1)
        return;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnLiftCall);
    Sound_PlayOnce(NE1_ANIM(0x94), 0xab);
    AnimPlay(NE1_ANIM(0x94), 0x35, 0);
    InTime0(15000, NE1_Door94Close);
    Object_SetProperty(NE1_SWITCH(0x62), PROP_STATE, 1);
    st.liftPhase = 0;
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE1_OnLiftDone);
}

// 0x10002350  EV_TRIGGER_ENTER 0x400de (or 0x400db while the lift is down): the lift goes up
static void NE1_OnLiftCall(const LanderEvent *ev)
{
    ObjId t = (ObjId)ev->arg[1];
    if (t != NE1_TRIGGER(0xde)) {
        if (t != NE1_TRIGGER(0xdb) || st.liftPhase != 0)
            return;
    }
    Sound_PlayOnce(NE1_LIFT, 0xce);
    InTime3(840, Sound_PlayLoop, NE1_LIFT, 0xcf, -1);
    AnimPlay(NE1_LIFT, 0x37, 0);
    AnimSetUpdateInterval(NE1_LIFT, 0x1e);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnLiftCall);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE1_OnLiftDone);
}

// 0x100023f0  EV_TRIGGER_ENTER/LEAVE 0x400dc: door 0x300dd
static void NE1_OnDoorDD(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0xdc))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(NE1_ANIM(0xdd), 0xda);
        AnimPlay(NE1_ANIM(0xdd), 0x38, 0);
        return;
    }
    if (ev->type == EV_TRIGGER_LEAVE) {
        Sound_PlayOnce(NE1_ANIM(0xdd), 0xda);
        AnimPlay(NE1_ANIM(0xdd), 0x38, 4);
    }
}

// 0x10002460  EV_TRIGGER_ENTER/LEAVE 0x40065: door 0x30064
static void NE1_OnDoor64(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0x65))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(NE1_ANIM(0x64), 0xa9);
        AnimPlay(NE1_ANIM(0x64), 0x43, 0);
        return;
    }
    if (ev->type == EV_TRIGGER_LEAVE) {
        Sound_PlayOnce(NE1_ANIM(0x64), 0xa9);
        AnimPlay(NE1_ANIM(0x64), 0x43, 4);
    }
}

// 0x100024d0  EV_TRIGGER_ENTER/LEAVE 0x400c4: doors 0x300c2/c3
static void NE1_OnDoorsC2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0xc4))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(NE1_ANIM(0xc2), 0xa9);
        Sound_PlayOnce(NE1_ANIM(0xc3), 0xa9);
        AnimPlay(NE1_ANIM(0xc2), 0x44, 0);
        AnimPlay(NE1_ANIM(0xc3), 0x44, 0);
        return;
    }
    if (ev->type == EV_TRIGGER_LEAVE) {
        AnimPlay(NE1_ANIM(0xc2), 0x44, 4);
        AnimPlay(NE1_ANIM(0xc3), 0x44, 4);
        Sound_PlayOnce(NE1_ANIM(0xc2), 0xa9);
        Sound_PlayOnce(NE1_ANIM(0xc3), 0xa9);
    }
}

// 0x10002580  EV_TRIGGER_ENTER 0x40063 (after the core): out of the station - success
static void NE1_OnExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == NE1_TRIGGER(0x63))
        MissionScore_End(1);                                    // 0x100013b0
}

// 0x100025a0  out = (a + b) / 2 per axis (arithmetic shift)
static void NE1_Midpoint(const int32_t a[3], const int32_t b[3], int32_t out[3])
{
    out[0] = (b[0] + a[0]) >> 1;
    out[1] = (a[1] + b[1]) >> 1;
    out[2] = (a[2] + b[2]) >> 1;
}

// 0x100025e0  while the beam is on: it ends at the lander (and burns 2 shield) when the cannon sees
// it, else at a fixed point; the beam light sits half way
static void NE1_BeamUpdate(void)
{
    if (st.beamOn == 0)
        return;
    const int32_t *from = (const int32_t *)Object_GetProperty(NE1_CANNON, PROP_POSITION);
    const int32_t *to = (const int32_t *)Object_GetProperty(NE1_LANDER, PROP_POSITION);
    int32_t miss[3];
    if (LanderAPI_LineOfSight(from, to) != 0) {
        Object_SetProperty(NE1_LANDER, PROP_HITPOINTS, Object_GetProperty(NE1_LANDER, PROP_HITPOINTS) - 2);
    } else {
        miss[0] = -0xc300;
        miss[1] = -0x4f00;
        miss[2] = 0x8200;
        to = miss;
    }
    Object_SetProperty(NE1_BEAM_START, PROP_POSITION, (intptr_t)from);
    Object_SetProperty(NE1_BEAM_END, PROP_POSITION, (intptr_t)to);
    int32_t mid[3];
    NE1_Midpoint(from, to, mid);
    Object_SetProperty(NE1_BEAM_LIGHT, PROP_POSITION, (intptr_t)mid);
}

// 0x100026b0  EV_TICK_100MS while the beam is on
static void NE1_BeamTick(const LanderEvent *ev)
{
    (void)ev;
    NE1_BeamUpdate();
}

// 0x100026c0
static void NE1_BeamOn(void)
{
    if (st.beamOn != 0)
        return;
    st.beamOn = 1;
    MobObj_Wake(0x11);
    MobObj_Wake(0x12);
    Sound_PlayLoop(NE1_BEAM_LIGHT, 0xc5, -1);
    Sound_PlayLoop(NE1_BEAM_LIGHT, 0xc6, -1);
    Sound_PlayOnce(NE1_BEAM_LIGHT, 0x96);
    NE1_BeamUpdate();
    EventType_AddHandler(EV_TICK_100MS, NULL, NE1_BeamTick);
}

// 0x10002740  (also an InTime target)
static void NE1_BeamOff(void)
{
    if (st.beamOn == 0)
        return;
    st.beamOn = 0;
    MobObj_Sleep(0x11);
    MobObj_Sleep(0x12);
    Sound_StopObject(NE1_BEAM_LIGHT);
    EventType_RemoveHandler(EV_TICK_100MS, NE1_BeamTick);
}

// 0x10002790  a 2-s shot
static void NE1_BeamPulse(void)
{
    if (st.cannonOn == 0)
        return;
    NE1_BeamOn();
    InTime0(2000, NE1_BeamOff);
}

static void NE1_ChargeOff(void);
static void NE1_ChargeStart(void);

// 0x100027c0  EV_TICK_100MS while charging: the charge light brightens over 8 s, then it fires and
// recharges 4 s later
static void NE1_ChargeTick(const LanderEvent *ev)
{
    (void)ev;
    st.charge++;
    Object_SetProperty(NE1_CHARGE_LIGHT, PROP_STATE, (st.charge << 12) / 80);
    if (st.charge != 80)
        return;
    NE1_ChargeOff();
    NE1_BeamPulse();
    InTime0(4000, NE1_ChargeStart);
}

// 0x10002820  (InTime target)
static void NE1_ChargeStart(void)
{
    if (st.charging != 0 || st.cannonOn == 0)
        return;
    st.charging = 1;
    Sound_PlayOnce(NE1_CANNON, 0x37f);
    Object_SetAlive(NE1_CHARGE_LIGHT);
    st.charge = 0;
    EventType_AddHandler(EV_TICK_100MS, NULL, NE1_ChargeTick);
}

// 0x10002880
static void NE1_ChargeOff(void)
{
    if (st.charging == 0)
        return;
    st.charging = 0;
    Sound_StopObject(NE1_CANNON);
    Object_SetDead(NE1_CHARGE_LIGHT);
    EventType_RemoveHandler(EV_TICK_100MS, NE1_ChargeTick);
}

// 0x100028c0
static void NE1_CannonEnable(void)
{
    st.cannonOn = 1;
    InTime0(2000, NE1_ChargeStart);
}

// 0x100028e0
static void NE1_CannonDisable(void)
{
    NE1_ChargeOff();
    st.cannonOn = 0;
}

// 0x100028f0  EV_TICK_1S after the core: 2-D alarm every 5 s
static void NE1_AlarmTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.alarmTicks != 5)
        return;
    Sound_PlayLoop(OBJ_NONE, 0x37c, 3);
    st.alarmTicks = 0;
}

// 0x10002920  EV_TICK_1S after the core: the station shakes every 5..10 s
static void NE1_QuakeTick(const LanderEvent *ev)
{
    (void)ev;
    st.quakeTicks++;
    uint32_t wait = LndrRand() % 6u + 5u;
    if ((uint32_t)st.quakeTicks < wait)
        return;
    st.quakeTicks = 0;
    Sound_PlayOnce(NE1_LANDER, 0xf5);
    InTime2(300, Sound_PlayOnce, NE1_LANDER, 0xf5);
    InTime2(500, Sound_PlayOnce, NE1_LANDER, 0xf7);
    AnimPlay(NE1_SHAKER, 0x54, 0xc0);
}

// 0x100029b0  EV_ANIM_TURNED: the shake is over
static void NE1_OnQuakeDone(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x54)
        AnimStop(NE1_SHAKER, 1);
}

static void NE1_OnPanelDestroyed(const LanderEvent *ev);

// 0x100029d0  EV_DESTROYED: a core panel; all 17: the core is dead - get out
static void NE1_OnPanelDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    int panel = (id >= (int32_t)NE1_DESTR(0x16) && id <= (int32_t)NE1_DESTR(0x1d)) ||
                (id >= (int32_t)NE1_DESTR(0x11) && id <= (int32_t)NE1_DESTR(0x15)) ||
                (id >= (int32_t)NE1_DESTR(0x0f) && id <= (int32_t)NE1_DESTR(0x10)) ||
                (id >= (int32_t)NE1_DESTR(0x09) && id <= (int32_t)NE1_DESTR(0x0a));
    if (!panel)
        return;
    if (++st.panels != 0x11)
        return;
    Object_SetDead(NE1_DESTR(0x95));
    Object_SetProperty(NE1_CANNON, PROP_HITPOINTS, 0);
    NE1_CannonDisable();
    EventType_RemoveHandler(EV_DESTROYED, NE1_OnPanelDestroyed);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnDoor64);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE1_OnDoor64);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnDoorsC2);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE1_OnDoorsC2);
    EventType_AddHandler(EV_TICK_1S, NULL, NE1_AlarmTick);
    EventType_AddHandler(EV_TICK_1S, NULL, NE1_QuakeTick);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, NE1_OnQuakeDone);
    WindowSetText(st.win, st.text[2]);           // "Exit station"
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnExit);
}

static const uint8_t ne1_panels[17] = {
    0x09, 0x0a, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d
};

// 0x10002b00  EV_DESTROYED: the four core guards (turrets 0x1e..0x21); all four: the shutters open,
// the panels appear, the cannon starts. (Masks the event's object id in place.)
static void NE1_OnCoreGuardDestroyed(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    ObjId id = (ObjId)e->arg[0];
    if (id != NE1_TURRET(0x20) && id != NE1_TURRET(0x21) && id != NE1_TURRET(0x1e) && id != NE1_TURRET(0x1f))
        return;
    if (++st.coreGuards != 4)
        return;
    AnimPlay(NE1_ANIM(0x5a), 0x3c, st.coreGuards);   // flags = the count (4): reversed
    AnimPlay(NE1_ANIM(0x55), 0x3d, 4);
    AnimPlay(NE1_ANIM(0x56), 0x3e, 4);
    AnimPlay(NE1_ANIM(0x57), 0x3f, 4);
    AnimPlay(NE1_ANIM(0x58), 0x40, 4);
    AnimPlay(NE1_ANIM(0x59), 0x41, 4);
    AnimPlay(NE1_ANIM(0x5b), 0x42, 4);
    for (int i = 0; i < 17; i++)
        Object_SetAlive(NE1_DESTR(ne1_panels[i]));
    EventType_RemoveHandler(EV_DESTROYED, NE1_OnCoreGuardDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, NE1_OnPanelDestroyed);
    NE1_CannonEnable();
    InTime2(500, Sound_PlayOnce, NE1_ANIM(0x5a), 0xb2);
    InTime2(1000, Sound_PlayOnce, NE1_ANIM(0x55), 0xb2);
    InTime2(1500, Sound_PlayOnce, NE1_ANIM(0x56), 0xb2);
    InTime2(2000, Sound_PlayOnce, NE1_ANIM(0x57), 0xb2);
    InTime2(2500, Sound_PlayOnce, NE1_ANIM(0x58), 0xb2);
    InTime2(3000, Sound_PlayOnce, NE1_ANIM(0x59), 0xb2);
    InTime2(3500, Sound_PlayOnce, NE1_ANIM(0x5b), 0xb2);
}

// 0x10002d30  EV_ANIM_FINISHED: the shutters (anim 0x3c) closed: the panels disappear
static void NE1_OnShuttersClosed(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x3c)
        return;
    for (int i = 0; i < 17; i++)
        Object_SetDead(NE1_DESTR(ne1_panels[i]));
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE1_OnShuttersClosed);
}

// 0x10002e10  EV_TRIGGER_ENTER 0x4000b (after 0x4000d): the core's shutters close (once)
static void NE1_OnCoreApproach(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0x0b))
        return;
    AnimPlay(NE1_ANIM(0x5a), 0x3c, 0);
    AnimPlay(NE1_ANIM(0x55), 0x3d, 0);
    AnimPlay(NE1_ANIM(0x56), 0x3e, 0);
    AnimPlay(NE1_ANIM(0x57), 0x3f, 0);
    AnimPlay(NE1_ANIM(0x58), 0x40, 0);
    AnimPlay(NE1_ANIM(0x59), 0x41, 0);
    AnimPlay(NE1_ANIM(0x5b), 0x42, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE1_OnShuttersClosed);
    InTime2(500, Sound_PlayOnce, NE1_ANIM(0x5b), 0xb2);
    InTime2(1000, Sound_PlayOnce, NE1_ANIM(0x59), 0xb2);
    InTime2(1500, Sound_PlayOnce, NE1_ANIM(0x58), 0xb2);
    InTime2(2000, Sound_PlayOnce, NE1_ANIM(0x57), 0xb2);
    InTime2(2500, Sound_PlayOnce, NE1_ANIM(0x56), 0xb2);
    InTime2(3000, Sound_PlayOnce, NE1_ANIM(0x55), 0xb2);
    InTime2(3500, Sound_PlayOnce, NE1_ANIM(0x5a), 0xb2);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnCoreApproach);
}

// 0x10002f60  EV_TRIGGER_ENTER 0x4000d: door 0x3000c opens (once)
static void NE1_OnCoreDoorOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0x0d))
        return;
    Sound_PlayOnce(NE1_ANIM(0x0c), 0xa9);
    AnimPlay(NE1_ANIM(0x0c), 0x3b, 0);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnCoreApproach);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnCoreDoorOpen);
}

// 0x10002fc0  EV_TRIGGER_ENTER 0x4000b: door 0x3000c closes (once)
static void NE1_OnCoreDoorClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0x0b))
        return;
    Sound_PlayOnce(NE1_ANIM(0x0c), 0xa9);
    AnimPlay(NE1_ANIM(0x0c), 0x3b, 4);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnCoreDoorClose);
}

// 0x10003010  EV_TRIGGER_ENTER/LEAVE 0x40027..2a: doors 0x3002b..2e
static void NE1_OnDoors2B(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[1];
    if (t < (int32_t)NE1_TRIGGER(0x27) || t > (int32_t)NE1_TRIGGER(0x2a))
        return;
    ObjId door = (ObjId)(t - 0xfffc);
    if (ev->type == EV_TRIGGER_ENTER)
        AnimPlay(door, 0x39, 0);
    else if (ev->type == EV_TRIGGER_LEAVE)
        AnimPlay(door, 0x39, 4);
    else
        return;
    Sound_PlayOnce(door, 0xaa);
}

// 0x10003070  EV_TRIGGER_ENTER 0x40033: the core level comes alive (once): the AI defence 0x10e0002
// and its arms, the lift, gates, doors
static void NE1_OnCoreLevel(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0x33))
        return;
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchGate22);
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchDoor94);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnLiftCall);
    Object_SetProperty(NE1_TRIGGER(0xdc), PROP_TARGET, NE1_LIFT);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnDoorDD);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE1_OnDoorDD);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnCoreDoorOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnCoreDoorClose);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE1_OnDoors2B);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnDoors2B);
    AnimPlay(NE1_BOSS, 0x45, 0x60);
    AnimPlay(NE1_MOBANIM(0x10), 0x46, 0);
    AnimPlay(NE1_MOBANIM(0x15), 0x47, 0);
    AnimPlay(NE1_MOBANIM(0x14), 0x48, 0);
    AnimPlay(NE1_MOBANIM(0x0f), 0x49, 0);
    AnimPlay(NE1_MOBANIM(0x13), 0x4a, 0);
    InTime2(6000, Sound_PlayOnce, NE1_MOBANIM(0x0e), 0x374);
    InTime2(12000, Sound_PlayOnce, NE1_MOBANIM(0x0e), 0x374);
    InTime2(16000, Sound_PlayOnce, NE1_MOBANIM(0x0e), 0x374);
    InTime2(20000, Sound_PlayOnce, NE1_MOBANIM(0x0e), 0x374);
    InTime2(10000, Sound_PlayOnce, NE1_MOBANIM(0x13), 0x374);
    InTime2(14000, Sound_PlayOnce, NE1_MOBANIM(0x13), 0x374);
    InTime2(18000, Sound_PlayOnce, NE1_MOBANIM(0x13), 0x374);
    Sound_PlayLoop(NE1_BOSS, 0x8d, -1);
    AnimPlay(NE1_MOBANIM(0x0e), 0x4b, 0);
    AnimPlay(NE1_ANIM(0xdf), 0x3a, 0x81);
    for (int32_t id = (int32_t)NE1_ANIM(0xdf); id <= (int32_t)NE1_ANIM(0xe1); id++)
        Sound_PlayLoop((ObjId)id, 0xb4, -1);
    AnimPlay(NE1_ANIM(0xe0), 0x3a, 0x81);
    AnimPlay(NE1_ANIM(0xe1), 0x3a, 0x81);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnCoreLevel);
    if (st.hatch0BOpen == 1)
        AnimPlay(NE1_MOBANIM(0x0b), 0x53, 4);
}

// 0x100032d0  EV_DESTROYED: the AI defence 0x10e0002 destroyed: its arms collapse and vanish, the
// wreckage appears 5 s later. (Masks the event's object id in place.)
static void NE1_OnBossDestroyed(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if ((ObjId)e->arg[0] != NE1_BOSS)
        return;
    Sound_StopObject((ObjId)e->arg[0]);
    AnimStop(NE1_BOSS, 0);
    AnimStop(NE1_MOBANIM(0x10), 0);
    AnimStop(NE1_MOBANIM(0x15), 0);
    AnimStop(NE1_MOBANIM(0x14), 0);
    AnimStop(NE1_MOBANIM(0x0f), 0);
    AnimStop(NE1_MOBANIM(0x13), 0);
    AnimStop(NE1_MOBANIM(0x0e), 0);
    AnimSetUpdateInterval(NE1_BOSS, 5);
    InTime2(1500, Sound_PlayOnce, NE1_BOSS, 0xd8);
    InTime2(2500, Sound_PlayOnce, NE1_BOSS, 0xd8);
    InTime2(3500, Sound_PlayOnce, NE1_MOBANIM(0x0e), 0xf7);
    AnimSetUpdateInterval(NE1_MOBANIM(0x10), 5);
    AnimSetUpdateInterval(NE1_MOBANIM(0x15), 5);
    AnimSetUpdateInterval(NE1_MOBANIM(0x14), 5);
    AnimSetUpdateInterval(NE1_MOBANIM(0x0f), 5);
    AnimSetUpdateInterval(NE1_MOBANIM(0x13), 5);
    AnimSetUpdateInterval(NE1_MOBANIM(0x0e), 5);
    InTime3(200, AnimPlay, NE1_BOSS, 0x4c, 0x60);
    InTime3(200, AnimPlay, NE1_MOBANIM(0x10), 0x4d, 0);
    InTime3(200, AnimPlay, NE1_MOBANIM(0x15), 0x4e, 0);
    InTime3(200, AnimPlay, NE1_MOBANIM(0x14), 0x4f, 0);
    InTime3(200, AnimPlay, NE1_MOBANIM(0x0f), 0x50, 0);
    InTime3(200, AnimPlay, NE1_MOBANIM(0x13), 0x51, 0);
    InTime3(200, AnimPlay, NE1_MOBANIM(0x0e), 0x52, 0);
    InTime2(5000, AnimStop, NE1_MOBANIM(0x10), 0);
    InTime2(5000, AnimStop, NE1_MOBANIM(0x15), 0);
    InTime2(5000, AnimStop, NE1_MOBANIM(0x14), 0);
    InTime2(5000, AnimStop, NE1_MOBANIM(0x0f), 0);
    InTime2(5000, AnimStop, NE1_MOBANIM(0x13), 0);
    InTime2(5000, AnimStop, NE1_MOBANIM(0x0e), 0);
    InTime1(5000, MobObj_Sleep, 0x10);
    InTime1(5000, MobObj_Sleep, 0x15);
    InTime1(5000, MobObj_Sleep, 0x14);
    InTime1(5000, MobObj_Sleep, 0xf);
    InTime1(5000, MobObj_Sleep, 0x13);
    InTime1(5000, MobObj_Sleep, 0xe);
    InTime1(5000, MobObj_Sleep, 2);
    InTime1(5000, Object_SetAlive, NE1_DESTR(0x5e));
    InTime1(5000, Object_SetAlive, NE1_DESTR(0x98));
    InTime1(5000, Object_SetAlive, NE1_DESTR(0xd5));
    InTime1(5000, Object_SetAlive, NE1_DESTR(0xd4));
    InTime1(5000, Object_SetAlive, NE1_DESTR(0x97));
    InTime1(5000, Object_SetAlive, NE1_DESTR(0xd3));
    InTime1(5000, Object_SetAlive, NE1_DESTR(0x96));
}

// 0x10003610  EV_SWITCH 0xa00d6 on, shot by the lander: 0x300d7 moves (once)
static void NE1_OnSwitchD6(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0xd6) || ev->arg[1] != 1 || (ObjId)ev->arg[2] != NE1_LANDER)
        return;
    Sound_PlayChain(NE1_ANIM(0xd7), 5, 0xcb, 0xcc, 0xcc, 0xcc, 0xcd);
    AnimPlay(NE1_ANIM(0xd7), 0x32, 0);
    EventType_RemoveHandler(EV_SWITCH, NE1_OnSwitchD6);
}

// 0x10003680  EV_SWITCH 0xa00ce: doors 0x300cc/cd
static void NE1_OnSwitchDoorsCC(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0xce))
        return;
    Sound_PlayOnce(NE1_SWITCH(0xce), 0xeb);
    if (ev->arg[1] == 1) {
        for (int32_t id = (int32_t)NE1_ANIM(0xcc); id <= (int32_t)NE1_ANIM(0xcd); id++)
            Sound_PlayOnce((ObjId)id, 0x3f7);
        AnimPlay(NE1_ANIM(0xcc), 0x2e, 0);
        AnimPlay(NE1_ANIM(0xcd), 0x2e, 0);
        return;
    }
    if (ev->arg[1] != 0)
        return;
    for (int32_t id = (int32_t)NE1_ANIM(0xcc); id <= (int32_t)NE1_ANIM(0xcd); id++)
        Sound_PlayOnce((ObjId)id, 0x3f7);
    AnimPlay(NE1_ANIM(0xcc), 0x2e, 4);
    AnimPlay(NE1_ANIM(0xcd), 0x2e, 4);
}

// 0x10003730  EV_SWITCH 0xa007a: hatch mob 0x103000b
static void NE1_OnSwitchHatch(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0x7a))
        return;
    Sound_PlayChain(NE1_MOBANIM(0x0b), 5, 0xcb, 0xcc, 0xcc, 0xcc, 0xcd);
    if (ev->arg[1] == 1) {
        AnimPlay(NE1_MOBANIM(0x0b), 0x53, 0);
        st.hatch0BOpen = 0;
        return;
    }
    AnimPlay(NE1_MOBANIM(0x0b), 0x53, 4);
    st.hatch0BOpen = 1;
}

// 0x100037c0  EV_PICKUP 0x130000..07: +50 % shield
static void NE1_OnPickup(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)NE1_PICKUP(0) || id > (int32_t)NE1_PICKUP(7))
        return;
    int32_t shield = (int32_t)Object_GetProperty(NE1_LANDER, PROP_HITPOINTS) + 0x80;
    Voice_ShieldAcquired(NE1_LANDER, 50);
    Object_SetProperty(NE1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10003830  EV_TRIGGER_ENTER/LEAVE: head light on in the dark zones 0x400b2..b8, off when leaving
// 0x400ab..b1 for the outside
static void NE1_OnLightZones(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[1];
    if (ev->type == EV_TRIGGER_ENTER) {
        if (t >= (int32_t)NE1_TRIGGER(0xb2) && t <= (int32_t)NE1_TRIGGER(0xb8)) {
            Lander_SetHeadLight(NE1_LANDER, 0x31c, 0x999);
            st.inLitZone = 1;
            st.headLightOn = 1;
        }
        t = (int32_t)ev->arg[1];
        if (t >= (int32_t)NE1_TRIGGER(0xab) && t <= (int32_t)NE1_TRIGGER(0xb1))
            st.inLitZone = 0;
    }
    if (ev->type != EV_TRIGGER_LEAVE)
        return;
    t = (int32_t)ev->arg[1];
    if (t < (int32_t)NE1_TRIGGER(0xab) || t > (int32_t)NE1_TRIGGER(0xb1))
        return;
    if (st.inLitZone != 0 || st.headLightOn != 1)
        return;
    st.headLightOn = 0;
    Lander_SetHeadLight(NE1_LANDER, 0, 0);
}

// 0x100038e0  EV_COLLISION {body, other, impulse}: touching 0x300bc/bd stops three crushers;
// 0x300be reveals five turrets; touching the barrier while gate 0x30022 is open shuts the gate
static void NE1_OnTouch(const LanderEvent *ev)
{
    ObjId other = (ObjId)ev->arg[1];
    if ((other == NE1_ANIM(0xbc) || other == NE1_ANIM(0xbd)) && st.crushersStopped == 0) {
        AnimStop(NE1_ANIM(0xc7), 1);
        AnimStop(NE1_ANIM(0xc8), 1);
        AnimStop(NE1_ANIM(0xc9), 1);
        st.crushersStopped = 1;
    }
    if ((ObjId)ev->arg[1] == NE1_ANIM(0xbe) && st.lightsOn == 0) {
        Object_SetAlive(NE1_TURRET(0x116));
        Object_SetAlive(NE1_TURRET(0x117));
        Object_SetAlive(NE1_TURRET(0x118));
        Object_SetAlive(NE1_TURRET(0x119));
        Object_SetAlive(NE1_TURRET(0x11a));
        st.lightsOn = 1;
    }
    int32_t id = (int32_t)ev->arg[1];
    if (id < (int32_t)NE1_ANIMDESTR(0xbf) || id > (int32_t)NE1_ANIMDESTR(0xc1) || st.gate22Open != 1)
        return;
    for (int32_t g = (int32_t)NE1_ANIM(0x22); g <= (int32_t)NE1_ANIM(0x25); g++)
        Sound_PlayOnce((ObjId)g, 0xa9);
    InTime3(500, AnimPlay, NE1_ANIM(0x22), 0x36, 4);
    InTime3(500, AnimPlay, NE1_ANIM(0x23), 0x36, 4);
    AnimPlay(NE1_ANIM(0x24), 0x36, 4);
    AnimPlay(NE1_ANIM(0x25), 0x36, 4);
    st.gate22Open = 0;
    Object_SetProperty(NE1_SWITCH(0x61), PROP_STATE, 0);
}

// 0x10003a40  EV_SWITCH 0xa00ca: restart (1) / stop (0) the crushers 0x300c7..c9
static void NE1_OnSwitchCrushers(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE1_SWITCH(0xca))
        return;
    if (ev->arg[1] == 1) {
        if (st.crushersStopped != 1)
            return;
        AnimPlay(NE1_ANIM(0xc7), 0x2d, 1);
        AnimSetUpdateInterval(NE1_ANIM(0xc7), 0x14);
        InTime3(700, AnimPlay, NE1_ANIM(0xc8), 0x2d, 1);
        AnimSetUpdateInterval(NE1_ANIM(0xc8), 0x14);
        InTime3(300, AnimPlay, NE1_ANIM(0xc9), 0x2d, 1);
        AnimSetUpdateInterval(NE1_ANIM(0xc9), 0x14);
        return;
    }
    if (st.crushersStopped != 0)
        return;
    AnimStop(NE1_ANIM(0xc7), 1);
    AnimStop(NE1_ANIM(0xc8), 1);
    AnimStop(NE1_ANIM(0xc9), 1);
    st.crushersStopped = 1;
}

// 0x10003bb0  EV_TICK_100MS: the exterior ambient darkens with depth: v = (y + 200000) * 256 / 100000
// clamped to 0..256, colour (12v, 30v, 54v) / 256
static void NE1_DepthAmbient(const LanderEvent *ev)
{
    (void)ev;
    int32_t y = ((const int32_t *)Object_GetProperty(NE1_LANDER, PROP_POSITION))[1];
    int32_t v = ((y + 200000) << 8) / 100000;
    if (v < 0)
        v = 0;
    else if (v > 0x100)
        v = 0x100;
    Environment_SetExteriorAmbient((v * 12) >> 8, (v * 30) >> 8, (v * 54) >> 8);
}

// one step of the pressure damage below a threshold (0x10003c96 ..): sound, cockpit warning, damage
static void NE1_PressureHit(int32_t damage)
{
    Sound_PlayOnce(NE1_LANDER, 0x37e);
    Sound_PlayOnce(NE1_LANDER, 0x37d);
    if (CockVox(1))
        Sound_PlayChain(SND_COCKPIT_VOICE, 2, 8, 0x41c);
    if (damage == 0)
        return;
    st.depthShield = (int32_t)Object_GetProperty(NE1_LANDER, PROP_HITPOINTS) - damage;
    if (st.depthShield > 0) {
        Object_SetProperty(NE1_LANDER, PROP_HITPOINTS, st.depthShield);
        return;
    }
    Object_SetProperty(NE1_LANDER, PROP_HITPOINTS, 0);
}

static void NE1_DepthTick(const LanderEvent *ev);

// 0x10003c20  EV_TICK_1S: pressure below -108800 (warning), -121600 (-0x20), -134400 (-0x40),
// -147200 (-0x80), -160000 (destroyed); climbing back re-arms the thresholds
static void NE1_DepthTick(const LanderEvent *ev)
{
    (void)ev;
    st.depth = ((const int32_t *)Object_GetProperty(NE1_LANDER, PROP_POSITION))[1];
    if (st.depth < -108800 && st.depthNext >= -108800) {
        st.depthNext = -121600;
        Sound_PlayOnce(NE1_LANDER, 0x37e);
        if (CockVox(1))
            Sound_PlayChain(SND_COCKPIT_VOICE, 2, 8, 0x41c);
    }
    if (st.depth < -121600 && st.depthNext >= -121600) {
        st.depthNext = -134400;
        st.depthLast = -121600;
        NE1_PressureHit(0x20);
        if (st.depthShield <= 0)
            EventType_RemoveHandler(EV_TICK_1S, NE1_DepthTick);
    }
    if (st.depth < -134400 && st.depthNext >= -134400) {
        st.depthNext = -147200;
        st.depthLast = -134400;
        NE1_PressureHit(0x40);
        if (st.depthShield <= 0)
            EventType_RemoveHandler(EV_TICK_1S, NE1_DepthTick);
    }
    if (st.depth < -147200 && st.depthNext >= -147200) {
        st.depthNext = -160000;
        st.depthLast = -147200;
        NE1_PressureHit(0x80);
        if (st.depthShield <= 0)
            EventType_RemoveHandler(EV_TICK_1S, NE1_DepthTick);
    }
    if (st.depth < -160000 && st.depthNext >= -160000) {
        st.depthNext = -180000;
        st.depthLast = -160000;
        NE1_PressureHit(0);
        Object_SetProperty(NE1_LANDER, PROP_HITPOINTS, 0);
        EventType_RemoveHandler(EV_TICK_1S, NE1_DepthTick);
    }
    // 0x10003f4e: climbing above the last threshold moves both back up one step
    int32_t last = st.depthLast;
    if (st.depth <= last)
        return;
    if (last == -147200) {
        last = -134400;
        st.depthNext = -147200;
        st.depthLast = -134400;
    }
    if (st.depth <= last)
        return;
    if (last == -147200) {
        st.depthNext = -147200;
        st.depthLast = -134400;
        return;
    }
    if (last == -134400) {
        st.depthNext = -134400;
        st.depthLast = -121600;
        return;
    }
    if (last == -121600) {
        st.depthNext = -121600;
        st.depthLast = -108800;
        return;
    }
    if (last == -108800)
        st.depthNext = -108800;
}

// 0x10003fe0  EV_TRIGGER_ENTER 0x40032: the AI defence's arms wake (once)
static void NE1_OnCoreArms(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE1_TRIGGER(0x32))
        return;
    MobObj_Wake(0x10);
    MobObj_Wake(0x15);
    MobObj_Wake(0x14);
    MobObj_Wake(0xf);
    MobObj_Wake(0x13);
    MobObj_Wake(0xe);
    MobObj_Wake(2);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE1_OnCoreArms);
}

// 0x10004040  common set-up
static void NE1_Setup(void)
{
    st.text = ne1_text[gLanguage];
    for (int32_t id = 0x74; id <= 0x79; id++)                  // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = 0x10b; id <= 0x115; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = 0x1e; id <= 0x1f; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x2d);
    for (int32_t id = 0x20; id <= 0x21; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x2d);
    for (int32_t id = 0x34; id <= 0x47; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    for (int32_t id = 0x69; id <= 0x6a; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = 0x116; id <= 0x11a; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = (int32_t)OBJ_ID(OT_FUELDUMP, 0x6f); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0x73); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(NE1_BOSS, PROP_ARMOUR, 0x64);
    Object_SetProperty(NE1_BOSS, PROP_EXPLOSION, 0x8c);
    Media_AudioPlay(0);
    SetGravity(0x16);
    gAirResistance = 0x1e;
    gMainThrottlePower = 0x26;
    gNbWaypoints = ne1_nbWaypoints;
    gWaypoints = ne1_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    gTurretTypes[21] = &ne1_turretType21;
    gTurretTypes[22] = &ne1_turretType22;
    gTurretTypes[13] = &ne1_turretType13;
    gTurretTypes[15] = &ne1_turretType15;
    EventType_AddHandler(EV_DESTROYED, NULL, NE1_OnGateGuardDestroyed);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnStationEntered);
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchDoorE9);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnTriggerBB);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnTriggerDA);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnTriggerD9);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnDoorB9);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE1_OnDoorB9);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnCrusherHall);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnDoor107);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnDoor106);
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchBarrier);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnCoreLevel);
    EventType_AddHandler(EV_DESTROYED, NULL, NE1_OnBossDestroyed);
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchD6);
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchDoorsCC);
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchHatch);
    EventType_AddHandler(EV_PICKUP, NULL, NE1_OnPickup);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnLightZones);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE1_OnLightZones);
    EventType_AddHandler(EV_COLLISION, NULL, NE1_OnTouch);
    EventType_AddHandler(EV_SWITCH, NULL, NE1_OnSwitchCrushers);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10003b20
    EventType_AddHandler(EV_DESTROYED, NULL, NE1_OnCoreGuardDestroyed);
    EventType_AddHandler(EV_TICK_1S, NULL, NE1_DepthTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE1_OnCoreArms);
    EventType_AddHandler(EV_TICK_100MS, NULL, NE1_DepthAmbient);
    Object_SetDead(NE1_TURRET(0x76));
    MobObj_Wake(0xb);
    MobObj_Wake(0x17);
    MobObj_Wake(3);
    MobObj_Wake(4);
    MobObj_Wake(5);
    MobObj_Wake(6);
    MobObj_Wake(7);
    MobObj_Wake(8);
    MobObj_Wake(9);
    Sound_PlayLoop(MOB_ID(OT_AMBIENTFX, 3), 0x381, -1);
    MobObj_Wake(0x16);
    Object_SetDead(NE1_DESTR(0x5e));
    Object_SetDead(NE1_DESTR(0x98));
    Object_SetDead(NE1_DESTR(0xd5));
    Object_SetDead(NE1_DESTR(0xd4));
    Object_SetDead(NE1_DESTR(0x97));
    Object_SetDead(NE1_DESTR(0xd3));
    Object_SetDead(NE1_DESTR(0x96));
    Object_SetDead(NE1_TURRET(0x116));
    Object_SetDead(NE1_TURRET(0x117));
    Object_SetDead(NE1_TURRET(0x118));
    Object_SetDead(NE1_TURRET(0x119));
    Object_SetDead(NE1_TURRET(0x11a));
    Sound_PlayLoop(NE1_CANNON, 0xd3, -1);
    Object_SetDead(NE1_CHARGE_LIGHT);
    AnimPlay(NE1_ANIM(0xbc), 0x2c, 0x82);
    AnimSetUpdateInterval(NE1_ANIM(0xbc), 0x1e);
    AnimPlay(NE1_ANIM(0xbd), 0x2c, 0x82);
    AnimSetUpdateInterval(NE1_ANIM(0xbd), 0x1e);
    AnimPlay(NE1_ANIM(0xbe), 0x31, 0x82);
    AnimPlay(NE1_ANIMDESTR(0xbf), 0x33, 0x82);
    AnimPlay(NE1_ANIMDESTR(0xc0), 0x33, 0x82);
    AnimPlay(NE1_ANIMDESTR(0xc1), 0x34, 0x82);
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0x11b), PROP_STATE, 0x4b);
    TexAnimPlay(0x1a, 1, 100);
    TexAnimPlay(0x5a, 1, 100);
    TexAnimPlay(0x59, 1, 100);
    TexAnimPlay(0x49, 1, 100);
    Object_SetProperty(NE1_BOSS, PROP_HITPOINTS, 0xfa);
    Object_SetProperty(NE1_BOSS, PROP_ARMOUR, 0xaa);
    Object_SetProperty(NE1_BOSS, PROP_EXPLOSION, 0xc8);
    MissionScore_Init(NE1_LANDER, 600000, 8);                  // 0x10001530
}

// 0x10004590  LevelStartLevel_Lander (export 0x10001010)
static void NE1_StartLander(void)
{
    MobObj_Wake(NE1_MOB_LANDER);
    MobObj_Wake(NE1_MOB_CAMERA);
    NE1_Setup();
    Object_SetProperty(NE1_CAMERA, PROP_TARGET, NE1_LANDER);
}

// 0x100045d0  LevelStartLevel_Flycam (export 0x10001020)
static void NE1_StartFlycam(void)
{
    NE1_Setup();
    MobObj_Wake(NE1_MOB_FLYCAM);
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void NE1_Stop(void)
{
}

// 0x10001040
static uint32_t NE1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001050, return 0. Port: fresh DLL data.
static int32_t NE1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.depthNext = ne1_depthInit[0];
    st.depthLast = ne1_depthInit[1];
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText ne1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10009160, .count = 3 * 4, .out = ne1_text },
    { 0 },
};

const mission_script_t mission_neptune_1 = {
    .name          = "NEPTUNE\\1",
    .get_version   = NE1_GetVersion,
    .load          = NE1_Load,
    .start_lander  = NE1_StartLander,
    .start_flycam  = NE1_StartFlycam,
    .start_animcam = NULL,
    .stop          = NE1_Stop,
    .text          = ne1_gametext,
};
