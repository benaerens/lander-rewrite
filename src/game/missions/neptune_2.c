// MISSIONS\NEPTUNE\2\MISSIONR.DLL - faithful C port.
//
// Mission: a second station in Neptune's atmosphere (gravity 22), built on the Neptune 1 script
// (same objective texts, waypoint, turret types, depth pressure and core/cannon code, new map ids).
// Find the lower entrance at waypoint 1; trigger 0x4009b inside removes two gate turrets and
// starts the machinery (the AI core objective). Reaching the core (trigger
// 0x40009) closes its shutters; destroying the four core guards (turrets 0x1c..0x1f) opens them,
// shows 17 panels (destructibles 0x80007..0x8001b) and starts the charging cannon 0x8000c (beam =
// particle emitter mob 0x1b, light mob 0x1c). All 17 panels destroyed: "Exit station"; trigger
// 0x40096 ends the mission (MissionScore_End(1)). New in this level: a main generator switch
// (0xa0130: 30 s blackout, force fields down, some doors dead), a transporter ride with its own
// cameras (0x30052, camera mobs 0x19/0x1a), a pod lift (0x301a4) and pod sockets (0x4012c..2f) that
// destroy the barrier 0xf00be, the AI defence 0x10e0002 with six arms, piston chains (0x8013c/d).
// Below y = -108800 the pressure hurts (shield -0x20/-0x40/-0x80 clamped at 0, destroyed at
// -160000); the exterior ambient darkens with depth. Par 600 s, 8 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10005bf0), LevelStartLevel_Flycam 0x10001020
// (-> 0x10005c30), LevelStopLevel 0x10001030 (-> empty 0x10001050), LevelGetVersion 0x10001040.
// The score library (0x10001060-0x100015ff, Init 0x10001530, End 0x100013b0) and the
// destroyed-sound handler (0x10005220) are the common code in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define NE2_LANDER        MOB_ID(OT_LANDER, 0x13)           // 0x01010013
#define NE2_CAMERA        MOB_ID(OT_CAMERA, 1)              // 0x01060001
#define NE2_CAMERA19      MOB_ID(OT_CAMERA, 0x19)           // 0x01060019 transporter camera (out)
#define NE2_CAMERA1A      MOB_ID(OT_CAMERA, 0x1a)           // 0x0106001a transporter camera (back)
#define NE2_MOB_LANDER    0x13
#define NE2_MOB_CAMERA    1
#define NE2_MOB_FLYCAM    0x12
#define NE2_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))        // 0x000300nn
#define NE2_MOBANIM(n)    MOB_ID(OT_ANIMOBJECT, (n))        // 0x010300nn
#define NE2_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))        // 0x000400nn
#define NE2_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))            // 0x000a00nn
#define NE2_TURRET(n)     OBJ_ID(OT_TURRET, (n))
#define NE2_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))      // 0x000800nn
#define NE2_ANIMDESTR(n)  OBJ_ID(OT_ANIMDESTRUCTIBLE, (n))  // 0x000f00nn
#define NE2_FORCEBOX(n)   OBJ_ID(OT_FORCEBOX, (n))          // 0x001000nn
#define NE2_LIGHT(n)      OBJ_ID(OT_LIGHTOBJECT, (n))       // 0x001600nn
#define NE2_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))
#define NE2_POD(n)        MOB_ID(OT_POD, (n))               // 0x010c00nn
#define NE2_BOSS          MOB_ID(OT_ANIMTURRET, 2)          // 0x010e0002 the station's AI defence
#define NE2_ANIMTURRET(n) MOB_ID(OT_ANIMTURRET, (n))        // 0x010e00nn
#define NE2_CANNON        NE2_DESTR(0x0c)                   // 0x0008000c
#define NE2_CHARGE_LIGHT  NE2_LIGHT(0x6e)                   // 0x0016006e
#define NE2_BEAM_LIGHT    MOB_ID(OT_LIGHTOBJECT, 0x1c)      // 0x0116001c
#define NE2_BEAM_END      MOB_ID(OT_PARTICLEEMITTER, 0x1b)  // 0x010d001b emitter mob 0x1b: target point
#define NE2_BEAM_START    (NE2_BEAM_END | 0x04000000u)      // 0x050d001b (sic: the emitter's other end)
#define NE2_SHAKER        MOB_ID(OT_INERTIALFRAME, 0x29)    // 0x01140029
#define NE2_LIFT          NE2_ANIM(0x131)                   // 0x00030131 lift (switch 0xa00db)
#define NE2_TRANSPORTER   NE2_ANIM(0x52)                    // 0x00030052
#define NE2_PODLIFT       NE2_ANIM(0x1a4)                   // 0x000301a4 pod lift (switch 0xa00e6)
#define NE2_BARRIER       NE2_ANIMDESTR(0xbe)               // 0x000f00be destroyed by a socketed pod

// ---- static data (.data) --------------------------------------------------------------------------
// 0x1000a160: [language][6]. French and German miss a comma in the original source: text[3] and
// text[4] are concatenated, "generator off" shows the "ON" text and "generator on" is NULL.
static const char *ne2_text[3][6];
static const int32_t  ne2_nbWaypoints = 1;           // 0x1000a1a8
static const Waypoint ne2_waypoints[1] = {           // 0x1000a1b0
    { -44800, 3840 },
};
static const TurretType ne2_turretType21 = {         // 0x1000a1b8 -> gTurretTypes[21]
    { 52, 27, 518, 0 }, 0x28, 0, 4, 10, 70, 0x30, 1024, 12000, 0
};
static const TurretType ne2_turretType22 = {         // 0x1000a1d8 -> gTurretTypes[22]
    { 134, 0, 1026, 0 }, 0x01, 1, 4, 6, 80, 0x3c, 1024, 12000, 0
};
static const TurretType ne2_turretType13 = {         // 0x1000a1f8 -> gTurretTypes[13]
    { 61, 0, 198, 0 }, 0x09, 2, 4, 6, 80, 0x30, 1024, 7680, 0
};
static const TurretType ne2_turretType15 = {         // 0x1000a218 -> gTurretTypes[15]
    { 61, 0, 198, 0 }, 0x05, 2, 3, 5, 60, 0x20, 800, 12000, 0
};
static const int32_t ne2_depthInit[2] = { -108800, -108800 };   // 0x1000a234 / 0x1000a238

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      depthNext;     // 0x1000a234 (.data) next depth threshold that hurts
    int32_t      depthLast;     // 0x1000a238 (.data) last threshold passed
    Window      *win;           // 0x1000cb8c objective line
    int32_t      gateGuards;    // 0x1000cb90 gate turrets 0x188 / 0x193 destroyed
    int32_t      liftMoving;    // 0x1000cb94 (written, never read)
    int32_t      door54Open;    // 0x1000cb98
    int32_t      liftCalled;    // 0x1000cb9c
    int32_t      generatorOff;  // 0x1000cba0
    int32_t      doorD7Ticks;   // 0x1000cba4 (never reset)
    int32_t      coreGuards;    // 0x1000cbac core guards destroyed
    int32_t      podLiftMoving; // 0x1000cbb0
    int32_t      podInBay;      // 0x1000cbb4
    int32_t      podLiftUp;     // 0x1000cbb8
    int32_t      panels;        // 0x1000cbbc core panels destroyed
    int32_t      bay;           // 0x1000cbc0 bay / socket index 0..3 of the last pod
    int32_t      inDarkZone;    // 0x1000cbc4
    int32_t      headLightOn;   // 0x1000cbc8
    int32_t      mob2AWoken;    // 0x1000cbcc
    int32_t      doorD9Closed;  // 0x1000cbd0 (written, never read)
    int32_t      gate20Open;    // 0x1000cbd4 gate 0x30020..23 open
    int32_t      doorD7Open;    // 0x1000cbd8
    int32_t      generatorTicks;// 0x1000cbdc
    int32_t      hum;           // 0x1000cbe0
    int32_t      cannonOn;      // 0x1000cbe4
    int32_t      charging;      // 0x1000cbe8
    int32_t      beamOn;        // 0x1000cbec
    int32_t      alarmTicks;    // 0x1000cbf0
    int32_t      quakeTicks;    // 0x1000cbf4
    int32_t      depth;         // 0x1000cbf8 lander y
    int32_t      charge;        // 0x1000d038 cannon charge ticks
    int32_t      depthShield;   // 0x1000d03c (written, never read)
    const char *const *text;    // 0x1000d040 = ne2_text[gLanguage]
} st;

// 0x10001610  EV_DESTROYED: both gate turrets 0x188/0x193 destroyed: gate 0x300a3 opens. (Masks the
// event's object id in place.)
static void NE2_OnGateGuardDestroyed(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if ((ObjId)e->arg[0] != NE2_TURRET(0x188) && (ObjId)e->arg[0] != NE2_TURRET(0x193))
        return;
    if (++st.gateGuards != 2)
        return;
    Sound_PlayOnce(NE2_ANIM(0xa3), 0xa8);
    AnimPlay(NE2_ANIM(0xa3), 0x1c, 0);
    EventType_RemoveHandler(EV_DESTROYED, NE2_OnGateGuardDestroyed);
}

// 0x10001680  EV_TICK_1S: machine 0x30084 hums every 5 s
static void NE2_HumTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.hum != 5)
        return;
    Sound_PlayOnce(NE2_ANIM(0x84), 0xb2);
    st.hum = 0;
}

// 0x100016b0  EV_TRIGGER_ENTER 0x4009b: inside the station: the gate closes behind, the machinery
// starts, the gate turrets go
static void NE2_OnStationEntered(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x9b))
        return;
    Sound_PlayOnce(NE2_ANIM(0xa3), 0xa8);
    WindowSetText(st.win, st.text[1]);           // the AI core objective
    AnimPlay(NE2_ANIM(0xa3), 0x1c, 4);
    AnimPlay(NE2_ANIM(0xa0), 0x1e, 0x81);
    AnimSetUpdateInterval(NE2_ANIM(0xa0), 0xc8);
    Sound_PlayLoop(NE2_ANIM(0xa0), 0xbc, -1);
    AnimPlay(NE2_ANIM(0xa1), 0x1f, 0x81);
    AnimSetUpdateInterval(NE2_ANIM(0xa1), 0xc8);
    Sound_PlayLoop(NE2_ANIM(0xa1), 0xbc, -1);
    AnimPlay(NE2_ANIM(0xa2), 0x20, 0x81);
    AnimSetUpdateInterval(NE2_ANIM(0xa2), 0xc8);
    Sound_PlayLoop(NE2_ANIM(0xa2), 0xbc, -1);
    AnimPlay(NE2_ANIM(0x15a), 0x22, 0x81);
    AnimSetUpdateInterval(NE2_ANIM(0x15a), 0xc8);
    Sound_PlayLoop(NE2_ANIM(0x15a), 0x370, -1);
    AnimPlay(NE2_ANIM(0x184), 0x23, 0x82);
    AnimSetUpdateInterval(NE2_ANIM(0x184), 0xc8);
    Sound_PlayLoop(NE2_ANIM(0x184), 0x370, -1);
    AnimPlay(NE2_ANIM(0xc6), 0x24, 0x82);
    AnimSetUpdateInterval(NE2_ANIM(0xc6), 0xc8);
    Sound_PlayLoop(NE2_ANIM(0xc6), 0x370, -1);
    AnimPlay(NE2_ANIM(0xc5), 0x25, 0x82);
    AnimSetUpdateInterval(NE2_ANIM(0xc5), 0xc8);
    Sound_PlayLoop(NE2_ANIM(0xc5), 0x370, -1);
    AnimPlay(NE2_ANIM(0x84), 0x26, 1);
    EventType_AddHandler(EV_TICK_1S, NULL, NE2_HumTick);
    AnimPlay(NE2_ANIM(0x77), 0x27, 0x81);
    Sound_PlayLoop(NE2_ANIM(0x77), 0xb1, -1);
    AnimPlay(NE2_ANIM(0x76), 0x28, 0x81);
    Sound_PlayLoop(NE2_ANIM(0x76), 0xb3, -1);
    Object_SetDead(NE2_TURRET(0x188));
    Object_SetDead(NE2_TURRET(0x193));
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnStationEntered);
}

// 0x10001900  EV_SWITCH 0xa007d: door 0x3015b
static void NE2_OnSwitchDoor15B(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x7d))
        return;
    Sound_PlayOnce(NE2_SWITCH(0x7d), 0xeb);
    Sound_PlayOnce(NE2_ANIM(0x15b), 0xaa);
    if (ev->arg[1] == 1) {
        AnimPlay(NE2_ANIM(0x15b), 0x21, 0);
        return;
    }
    if (ev->arg[1] == 0)
        AnimPlay(NE2_ANIM(0x15b), 0x21, 4);
}

// 0x10001970  EV_TRIGGER_ENTER 0x4014e: 0xf014c anim 0x29
static void NE2_OnTrigger14E(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x14e))
        return;
    Sound_PlayOnce(NE2_ANIMDESTR(0x14c), 0xa8);
    AnimPlay(NE2_ANIMDESTR(0x14c), 0x29, 0);
}

// 0x100019b0  EV_TRIGGER_ENTER 0x4014d: 0xf014c back (once)
static void NE2_OnTrigger14D(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x14d))
        return;
    Sound_PlayOnce(NE2_ANIMDESTR(0x14c), 0xa8);
    AnimPlay(NE2_ANIMDESTR(0x14c), 0x29, 4);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnTrigger14D);
}

// 0x10001a00  EV_TRIGGER_ENTER/LEAVE 0x40105: door 0x30104
static void NE2_OnDoor104(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x105))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(NE2_ANIM(0x104), 0xab);
        AnimPlay(NE2_ANIM(0x104), 0x1d, 0);
        return;
    }
    Sound_PlayOnce(NE2_ANIM(0x104), 0xab);
    AnimPlay(NE2_ANIM(0x104), 0x1d, 4);
}

// 0x10001a60  EV_TRIGGER_ENTER 0x40182 / 0x40181: doors 0x30180/0x3017f open / close (once)
static void NE2_OnDoors17F(const LanderEvent *ev)
{
    ObjId t = (ObjId)ev->arg[1];
    if (t == NE2_TRIGGER(0x182)) {
        Sound_PlayOnce(NE2_ANIM(0x180), 0xab);
        Sound_PlayOnce(NE2_ANIM(0x17f), 0xab);
        AnimPlay(NE2_ANIM(0x180), 0x2a, 0);
        AnimPlay(NE2_ANIM(0x17f), 0x2b, 0);
        return;
    }
    if (t != NE2_TRIGGER(0x181))
        return;
    Sound_PlayOnce(NE2_ANIM(0x180), 0xab);
    Sound_PlayOnce(NE2_ANIM(0x17f), 0xab);
    AnimPlay(NE2_ANIM(0x180), 0x2a, 4);
    AnimPlay(NE2_ANIM(0x17f), 0x2b, 4);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnDoors17F);
}

// 0x10001b20  EV_SWITCH 0xa0122: door 0x30121
static void NE2_OnSwitchDoor121(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x122))
        return;
    Sound_PlayOnce(NE2_SWITCH(0x122), 0xeb);
    Sound_PlayOnce(NE2_ANIM(0x121), 0xaa);
    if (ev->arg[1] == 1) {
        AnimPlay(NE2_ANIM(0x121), 0x51, 0);
        return;
    }
    if (ev->arg[1] == 0)
        AnimPlay(NE2_ANIM(0x121), 0x51, 4);
}

// 0x10001b90  EV_SWITCH 0xa0111: door 0x30054 (blocks the lift while open)
static void NE2_OnSwitchDoor54(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x111))
        return;
    Sound_PlayOnce(NE2_SWITCH(0x111), 0xeb);
    if (ev->arg[1] == 1) {
        Sound_PlayOnce(NE2_ANIM(0x54), 0xa8);
        AnimPlay(NE2_ANIM(0x54), 0x52, 0);
        st.door54Open = 1;
    }
    if (ev->arg[1] == 0 && st.door54Open == 1) {
        Sound_PlayOnce(NE2_ANIM(0x54), 0xa8);
        AnimPlay(NE2_ANIM(0x54), 0x52, 4);
        st.door54Open = 0;
    }
}

// 0x10001c30  EV_SWITCH 0xa00db: the lift 0x30131 runs (door 0x30054 shut, not already called);
// the switch springs back
static void NE2_OnSwitchLift(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0xdb))
        return;
    Sound_PlayOnce(NE2_SWITCH(0xdb), 0xeb);
    if (ev->arg[1] != 1 || st.door54Open != 0 || st.liftCalled != 0) {
        Object_SetProperty(NE2_SWITCH(0xdb), PROP_STATE, 0);
        return;
    }
    AnimPlay(NE2_LIFT, 0x53, 0);
    Sound_PlayOnce(NE2_LIFT, 0x371);
    MobObj_Wake(0x23);
    InTime1(150, MobObj_Wake, 0x24);
    InTime1(300, MobObj_Wake, 0x25);
    st.liftCalled = 1;
    InTime3(1000, Object_SetProperty, NE2_SWITCH(0xdb), PROP_STATE, 0);
    st.liftMoving = 1;
    AnimPlay(NE2_ANIM(0x26), 0x55, 0);
    AnimPlay(NE2_ANIM(0x10d), 0x54, 0);
    AnimPlay(NE2_ANIM(0x10e), 0x54, 0);
}

// 0x10001d50  EV_ANIM_FINISHED: the lift 0x30131 arrived (doors 0x3010d/e shut again); 0x30026
static void NE2_OnLiftDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == NE2_LIFT) {
        st.liftMoving = 0;
        st.liftCalled = 0;
        Sound_PlayOnce(NE2_ANIM(0x10d), 0xa8);
        Sound_PlayOnce(NE2_ANIM(0x10e), 0xa8);
        AnimPlay(NE2_ANIM(0x10d), 0x54, 4);
        AnimPlay(NE2_ANIM(0x10e), 0x54, 4);
        AnimStop(NE2_LIFT, 0);
    }
    if ((ObjId)ev->arg[0] == NE2_ANIM(0x26))
        AnimStop(NE2_ANIM(0x26), 0);
}

// 0x10001df0  EV_TRIGGER_ENTER/LEAVE 0x40027 (target 0x30026): doors 0x3010f/0x30110
static void NE2_OnDoors10F(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x27))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(NE2_ANIM(0x10f), 0xa8);
        Sound_PlayOnce(NE2_ANIM(0x110), 0xa8);
        AnimPlay(NE2_ANIM(0x10f), 0x56, 0);
        AnimPlay(NE2_ANIM(0x110), 0x56, 0);
        return;
    }
    Sound_PlayOnce(NE2_ANIM(0x10f), 0xa8);
    Sound_PlayOnce(NE2_ANIM(0x110), 0xa8);
    AnimPlay(NE2_ANIM(0x10f), 0x56, 4);
    AnimPlay(NE2_ANIM(0x110), 0x56, 4);
}

// 0x10001e90  EV_SWITCH 0xa0145 on, shot by the lander: 0x3014a moves (once)
static void NE2_OnSwitch145(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x145) || ev->arg[1] != 1 || (ObjId)ev->arg[2] != NE2_LANDER)
        return;
    Sound_PlayChain(NE2_ANIM(0x14a), 5, 0xcb, 0xcc, 0xcc, 0xcc, 0xcd);
    AnimPlay(NE2_ANIM(0x14a), 0x2d, 0);
    EventType_RemoveHandler(EV_SWITCH, NE2_OnSwitch145);
}

static void NE2_OnDoors14F(const LanderEvent *ev);
static void NE2_OnSwitchDoorD7(const LanderEvent *ev);
static void NE2_OnSwitchGate20(const LanderEvent *ev);
static void NE2_OnCoreDoorOpen(const LanderEvent *ev);
static void NE2_OnTriggerD8(const LanderEvent *ev);
static void NE2_GeneratorTick(const LanderEvent *ev);
static void NE2_DoorD7Tick(const LanderEvent *ev);

// 0x10001f00  the main generator back on: lights, force fields and door controls return
static void NE2_GeneratorOn(void)
{
    st.generatorOff = 0;
    Object_SetProperty(NE2_SWITCH(0x8a), PROP_STATE, 1);
    Object_SetProperty(NE2_SWITCH(0x8b), PROP_STATE, 1);
    WindowSetText(st.win, st.text[5]);           // generator on (NULL in French/German)
    Object_SetAlive(NE2_ANIMDESTR(0x108));
    Object_SetAlive(NE2_ANIMDESTR(0x109));
    Object_SetAlive(NE2_ANIMDESTR(0x10a));
    Object_SetAlive(NE2_ANIMDESTR(0xb9));
    Object_SetDead(NE2_LIGHT(0x8c));                // emergency lights off
    Object_SetDead(NE2_LIGHT(0x8e));
    Object_SetDead(NE2_LIGHT(0x8f));
    Object_SetDead(NE2_LIGHT(0x90));
    Object_SetDead(NE2_LIGHT(0x91));
    Object_SetDead(NE2_LIGHT(0x92));
    Object_SetDead(NE2_LIGHT(0x93));
    Object_SetDead(NE2_LIGHT(0x94));
    Object_SetDead(NE2_LIGHT(0x95));
    Object_SetDead(NE2_LIGHT(0x8d));
    Object_SetAlive(NE2_LIGHT(0x113));              // main lights on
    Object_SetAlive(NE2_LIGHT(0x115));
    Object_SetAlive(NE2_LIGHT(0x116));
    Object_SetAlive(NE2_LIGHT(0x117));
    Object_SetAlive(NE2_LIGHT(0x118));
    Object_SetAlive(NE2_LIGHT(0x119));
    Object_SetAlive(NE2_LIGHT(0x11a));
    Object_SetAlive(NE2_LIGHT(0x11b));
    Object_SetAlive(NE2_LIGHT(0x11c));
    Object_SetAlive(NE2_LIGHT(0x114));
    AnimPlay(NE2_ANIMDESTR(0x108), 0x2e, 0x82);
    AnimPlay(NE2_ANIMDESTR(0x109), 0x2e, 0x82);
    AnimPlay(NE2_ANIMDESTR(0x10a), 0x2f, 0x82);
    AnimPlay(NE2_ANIMDESTR(0xb9), 0x6c, 0x82);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoors14F);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoors14F);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchDoorD7);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchGate20);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnCoreDoorOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTriggerD8);
    EventType_RemoveHandler(EV_TICK_1S, NE2_GeneratorTick);
    st.generatorTicks = 0;
    Object_SetProperty(NE2_SWITCH(0x130), PROP_STATE, 0);
}

// 0x10002120  EV_TICK_1S while the generator is off: back on after 30 s
static void NE2_GeneratorTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.generatorTicks == 30)
        NE2_GeneratorOn();
}

// 0x10002140  EV_SWITCH 0xa0130: main generator off (1) for 30 s / on again (0)
static void NE2_OnSwitchGenerator(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x130))
        return;
    Sound_PlayOnce(NE2_SWITCH(0x130), 0xeb);
    if (ev->arg[1] == 1) {
        st.generatorOff = 1;
        WindowSetText(st.win, st.text[4]);       // generator off (French/German: the "on" text)
        AnimStop(NE2_ANIMDESTR(0x108), 1);
        AnimStop(NE2_ANIMDESTR(0x109), 1);
        AnimStop(NE2_ANIMDESTR(0x10a), 1);
        Object_SetDead(NE2_ANIMDESTR(0x108));    // force fields down
        Object_SetDead(NE2_ANIMDESTR(0x109));
        Object_SetDead(NE2_ANIMDESTR(0x10a));
        Object_SetDead(NE2_LIGHT(0x113));
        Object_SetDead(NE2_LIGHT(0x115));
        Object_SetDead(NE2_LIGHT(0x116));
        Object_SetDead(NE2_LIGHT(0x117));
        Object_SetDead(NE2_LIGHT(0x118));
        Object_SetDead(NE2_LIGHT(0x119));
        Object_SetDead(NE2_LIGHT(0x11a));
        Object_SetDead(NE2_LIGHT(0x11b));
        Object_SetDead(NE2_LIGHT(0x11c));
        Object_SetDead(NE2_LIGHT(0x114));
        Object_SetAlive(NE2_LIGHT(0x8c));
        Object_SetAlive(NE2_LIGHT(0x8e));
        Object_SetAlive(NE2_LIGHT(0x8f));
        Object_SetAlive(NE2_LIGHT(0x90));
        Object_SetAlive(NE2_LIGHT(0x91));
        Object_SetAlive(NE2_LIGHT(0x92));
        Object_SetAlive(NE2_LIGHT(0x93));
        Object_SetAlive(NE2_LIGHT(0x94));
        Object_SetAlive(NE2_LIGHT(0x95));
        Object_SetAlive(NE2_LIGHT(0x8d));
        AnimStop(NE2_ANIMDESTR(0xb9), 0);
        Object_SetDead(NE2_ANIMDESTR(0xb9));
        EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnDoors14F);
        EventType_RemoveHandler(EV_TRIGGER_LEAVE, NE2_OnDoors14F);
        EventType_RemoveHandler(EV_SWITCH, NE2_OnSwitchDoorD7);
        EventType_RemoveHandler(EV_SWITCH, NE2_OnSwitchGate20);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnCoreDoorOpen);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnTriggerD8);
        Object_SetProperty(NE2_SWITCH(0x8a), PROP_STATE, 0);
        Object_SetProperty(NE2_SWITCH(0x8b), PROP_STATE, 0);
        if (st.doorD7Open == 1)
            EventType_RemoveHandler(EV_TICK_1S, NE2_DoorD7Tick);
        EventType_AddHandler(EV_TICK_1S, NULL, NE2_GeneratorTick);
    }
    if (ev->arg[1] == 0)
        NE2_GeneratorOn();
}

// 0x10002380  EV_TICK_1S while door 0x300d7 is open: it closes after 20 s (sic: the counter is never
// reset and the handler never removed, so later openings do not close by themselves)
static void NE2_DoorD7Tick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.doorD7Ticks != 20)
        return;
    Sound_PlayOnce(NE2_ANIM(0xd7), 0xab);
    AnimPlay(NE2_ANIM(0xd7), 0x30, 4);
    st.doorD7Open = 0;
    Object_SetProperty(NE2_SWITCH(0x8b), PROP_STATE, 0);
}

// 0x100023e0  EV_SWITCH 0xa008b: door 0x300d7 opens (switching it off is refused)
static void NE2_OnSwitchDoorD7(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x8b))
        return;
    if (ev->arg[1] == 1) {
        Sound_PlayOnce(NE2_ANIM(0xd7), 0xab);
        AnimPlay(NE2_ANIM(0xd7), 0x30, 0);
        st.doorD7Open = 1;
        EventType_AddHandler(EV_TICK_1S, NULL, NE2_DoorD7Tick);
    }
    if (ev->arg[1] == 0)
        Object_SetProperty(NE2_SWITCH(0x8b), PROP_STATE, 1);
}

// 0x10002450  EV_SWITCH 0xa008a (generator on): gate 0x30020..23
static void NE2_OnSwitchGate20(const LanderEvent *ev)
{
    if (st.generatorOff != 0 || (ObjId)ev->arg[0] != NE2_SWITCH(0x8a))
        return;
    Sound_PlayOnce(NE2_SWITCH(0x8a), 0xeb);
    if (ev->arg[1] == 1) {
        for (int32_t id = (int32_t)NE2_ANIM(0x20); id <= (int32_t)NE2_ANIM(0x23); id++)
            Sound_PlayOnce((ObjId)id, 0xa9);
        st.gate20Open = 1;
        AnimPlay(NE2_ANIM(0x20), 0x31, 0);
        AnimPlay(NE2_ANIM(0x21), 0x31, 0);
        InTime3(500, AnimPlay, NE2_ANIM(0x22), 0x31, 0);
        InTime3(500, AnimPlay, NE2_ANIM(0x23), 0x31, 0);
    }
    if (ev->arg[1] != 0)
        return;
    for (int32_t id = (int32_t)NE2_ANIM(0x20); id <= (int32_t)NE2_ANIM(0x23); id++)
        Sound_PlayOnce((ObjId)id, 0xa9);
    st.gate20Open = 0;
    InTime3(500, AnimPlay, NE2_ANIM(0x20), 0x31, 4);
    InTime3(500, AnimPlay, NE2_ANIM(0x21), 0x31, 4);
    AnimPlay(NE2_ANIM(0x22), 0x31, 4);
    AnimPlay(NE2_ANIM(0x23), 0x31, 4);
}

// 0x10002590  EV_TRIGGER_ENTER 0x4000b (generator on): the core door 0x3000a opens
static void NE2_OnCoreDoorOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x0b))
        return;
    Sound_PlayOnce(NE2_ANIM(0x0a), 0xa9);
    AnimPlay(NE2_ANIM(0x0a), 0x36, 0);
}

// 0x100025d0  EV_TRIGGER_ENTER 0x40009: the core door 0x3000a slams shut
static void NE2_OnCoreDoorClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x09))
        return;
    Sound_PlayOnce(NE2_ANIM(0x0a), 0x3f6);
    AnimSetUpdateInterval(NE2_ANIM(0x0a), 3);
    AnimPlay(NE2_ANIM(0x0a), 0x36, 4);
}

// 0x10002620  EV_TRIGGER_ENTER/LEAVE 0x40098 (after the core): door 0x30097
static void NE2_OnDoor97(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x98))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Sound_PlayOnce(NE2_ANIM(0x97), 0xa9);
        AnimPlay(NE2_ANIM(0x97), 0x3e, 0);
        return;
    }
    Sound_PlayOnce(NE2_ANIM(0x97), 0xa9);
    AnimPlay(NE2_ANIM(0x97), 0x3e, 4);
}

// 0x10002680  EV_TRIGGER_ENTER/LEAVE 0x40120 (after the core): doors 0x3011e/0x3011f
static void NE2_OnDoors11E(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x120))
        return;
    Sound_PlayOnce(NE2_ANIM(0x11e), 0xa9);
    Sound_PlayOnce(NE2_ANIM(0x11f), 0xa9);
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(NE2_ANIM(0x11e), 0x3f, 0);
        AnimPlay(NE2_ANIM(0x11f), 0x3f, 0);
        return;
    }
    AnimPlay(NE2_ANIM(0x11e), 0x3f, 4);
    AnimPlay(NE2_ANIM(0x11f), 0x3f, 4);
}

// 0x10002710  EV_TRIGGER_ENTER 0x40096 (after the core): out of the station - success
static void NE2_OnExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == NE2_TRIGGER(0x96))
        MissionScore_End(1);                                    // 0x100013b0
}

// 0x10002730  out = (a + b) / 2 per axis (arithmetic shift)
static void NE2_Midpoint(const int32_t a[3], const int32_t b[3], int32_t out[3])
{
    out[0] = (b[0] + a[0]) >> 1;
    out[1] = (a[1] + b[1]) >> 1;
    out[2] = (a[2] + b[2]) >> 1;
}

// 0x10002770  while the beam is on: it ends at the lander (and burns 2 shield) when the cannon sees
// it, else at a fixed point; the beam light sits half way
static void NE2_BeamUpdate(void)
{
    if (st.beamOn == 0)
        return;
    const int32_t *from = (const int32_t *)Object_GetProperty(NE2_CANNON, PROP_POSITION);
    const int32_t *to = (const int32_t *)Object_GetProperty(NE2_LANDER, PROP_POSITION);
    int32_t miss[3];
    if (LanderAPI_LineOfSight(from, to) != 0) {
        Object_SetProperty(NE2_LANDER, PROP_HITPOINTS, Object_GetProperty(NE2_LANDER, PROP_HITPOINTS) - 2);
    } else {
        miss[0] = -0xc300;
        miss[1] = -0x4f00;
        miss[2] = 0x8200;
        to = miss;
    }
    Object_SetProperty(NE2_BEAM_START, PROP_POSITION, (intptr_t)from);
    Object_SetProperty(NE2_BEAM_END, PROP_POSITION, (intptr_t)to);
    int32_t mid[3];
    NE2_Midpoint(from, to, mid);
    Object_SetProperty(NE2_BEAM_LIGHT, PROP_POSITION, (intptr_t)mid);
}

// 0x10002840  EV_TICK_100MS while the beam is on
static void NE2_BeamTick(const LanderEvent *ev)
{
    (void)ev;
    NE2_BeamUpdate();
}

// 0x10002850
static void NE2_BeamOn(void)
{
    if (st.beamOn != 0)
        return;
    st.beamOn = 1;
    MobObj_Wake(0x1b);
    MobObj_Wake(0x1c);
    Sound_PlayLoop(NE2_BEAM_LIGHT, 0xc5, -1);
    Sound_PlayLoop(NE2_BEAM_LIGHT, 0xc6, -1);
    Sound_PlayOnce(NE2_BEAM_LIGHT, 0x96);
    NE2_BeamUpdate();
    EventType_AddHandler(EV_TICK_100MS, NULL, NE2_BeamTick);
}

// 0x100028d0  (also an InTime target)
static void NE2_BeamOff(void)
{
    if (st.beamOn == 0)
        return;
    st.beamOn = 0;
    MobObj_Sleep(0x1b);
    MobObj_Sleep(0x1c);
    Sound_StopObject(NE2_BEAM_LIGHT);
    EventType_RemoveHandler(EV_TICK_100MS, NE2_BeamTick);
}

// 0x10002920  a 2-s shot
static void NE2_BeamPulse(void)
{
    if (st.cannonOn == 0)
        return;
    NE2_BeamOn();
    InTime0(2000, NE2_BeamOff);
}

static void NE2_ChargeOff(void);
static void NE2_ChargeStart(void);

// 0x10002950  EV_TICK_100MS while charging: the charge light brightens over 8 s, then it fires and
// recharges 4 s later
static void NE2_ChargeTick(const LanderEvent *ev)
{
    (void)ev;
    st.charge++;
    Object_SetProperty(NE2_CHARGE_LIGHT, PROP_STATE, (st.charge << 12) / 80);
    if (st.charge != 80)
        return;
    NE2_ChargeOff();
    NE2_BeamPulse();
    InTime0(4000, NE2_ChargeStart);
}

// 0x100029b0  (InTime target)
static void NE2_ChargeStart(void)
{
    if (st.charging != 0 || st.cannonOn == 0)
        return;
    st.charging = 1;
    Sound_PlayOnce(NE2_CANNON, 0x37f);
    Object_SetAlive(NE2_CHARGE_LIGHT);
    st.charge = 0;
    EventType_AddHandler(EV_TICK_100MS, NULL, NE2_ChargeTick);
}

// 0x10002a10
static void NE2_ChargeOff(void)
{
    if (st.charging == 0)
        return;
    st.charging = 0;
    Sound_StopObject(NE2_CANNON);
    Object_SetDead(NE2_CHARGE_LIGHT);
    EventType_RemoveHandler(EV_TICK_100MS, NE2_ChargeTick);
}

// 0x10002a50
static void NE2_CannonEnable(void)
{
    st.cannonOn = 1;
    InTime0(2000, NE2_ChargeStart);
}

// 0x10002a70
static void NE2_CannonDisable(void)
{
    NE2_ChargeOff();
    st.cannonOn = 0;
}

// 0x10002a80  EV_TICK_1S after the core: 2-D alarm every 5 s
static void NE2_AlarmTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.alarmTicks != 5)
        return;
    Sound_PlayLoop(OBJ_NONE, 0x37c, 3);
    st.alarmTicks = 0;
}

// 0x10002ab0  EV_TICK_1S after the core: the station shakes every 5..10 s
static void NE2_QuakeTick(const LanderEvent *ev)
{
    (void)ev;
    st.quakeTicks++;
    uint32_t wait = LndrRand() % 6u + 5u;
    if ((uint32_t)st.quakeTicks < wait)
        return;
    st.quakeTicks = 0;
    Sound_PlayOnce(NE2_LANDER, 0xf5);
    InTime2(300, Sound_PlayOnce, NE2_LANDER, 0xf5);
    InTime2(500, Sound_PlayOnce, NE2_LANDER, 0xf7);
    AnimPlay(NE2_SHAKER, 0x6d, 0xc0);
}

// 0x10002b40  EV_ANIM_TURNED: the shake is over
static void NE2_OnQuakeDone(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x6d)
        AnimStop(NE2_SHAKER, 1);
}

static void NE2_DepthTick(const LanderEvent *ev);
static void NE2_DepthAmbient(const LanderEvent *ev);

// 0x10002b60  EV_DESTROYED: a core panel; all 17: the core is dead - get out (the pressure damage
// and the depth ambient stop)
static void NE2_OnPanelDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    int panel = (id >= (int32_t)NE2_DESTR(0x14) && id <= (int32_t)NE2_DESTR(0x1b)) ||
                (id >= (int32_t)NE2_DESTR(0x0f) && id <= (int32_t)NE2_DESTR(0x13)) ||
                (id >= (int32_t)NE2_DESTR(0x0d) && id <= (int32_t)NE2_DESTR(0x0e)) ||
                (id >= (int32_t)NE2_DESTR(0x07) && id <= (int32_t)NE2_DESTR(0x08));
    if (!panel)
        return;
    if (++st.panels != 0x11)
        return;
    Object_SetDead(NE2_DESTR(0xda));
    Object_SetProperty(NE2_CANNON, PROP_HITPOINTS, 0);
    EventType_AddHandler(EV_TICK_1S, NULL, NE2_AlarmTick);
    EventType_AddHandler(EV_TICK_1S, NULL, NE2_QuakeTick);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, NE2_OnQuakeDone);
    EventType_RemoveHandler(EV_TICK_1S, NE2_DepthTick);
    EventType_RemoveHandler(EV_TICK_100MS, NE2_DepthAmbient);
    EventType_RemoveHandler(EV_DESTROYED, NE2_OnPanelDestroyed);
    NE2_CannonDisable();
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoor97);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoor97);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoors11E);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoors11E);
    WindowSetText(st.win, st.text[2]);           // "Exit station"
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnExit);
}

static const uint8_t ne2_panels[17] = {
    0x07, 0x08, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b
};

// 0x10002ca0  EV_DESTROYED: the four core guards (turrets 0x1c..0x1f); all four: the shutters open,
// the panels appear, the cannon starts. (Masks the event's object id in place.)
static void NE2_OnCoreGuardDestroyed(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    ObjId id = (ObjId)e->arg[0];
    if (id != NE2_TURRET(0x1e) && id != NE2_TURRET(0x1f) && id != NE2_TURRET(0x1c) && id != NE2_TURRET(0x1d))
        return;
    if (++st.coreGuards != 4)
        return;
    AnimPlay(NE2_ANIM(0x74), 0x37, st.coreGuards);   // flags = the count (4): reversed
    AnimPlay(NE2_ANIM(0x6f), 0x38, 4);
    AnimPlay(NE2_ANIM(0x70), 0x39, 4);
    AnimPlay(NE2_ANIM(0x71), 0x3a, 4);
    AnimPlay(NE2_ANIM(0x72), 0x3b, 4);
    AnimPlay(NE2_ANIM(0x73), 0x3c, 4);
    AnimPlay(NE2_ANIM(0x75), 0x3d, 4);
    for (int i = 0; i < 17; i++)
        Object_SetAlive(NE2_DESTR(ne2_panels[i]));
    EventType_RemoveHandler(EV_DESTROYED, NE2_OnCoreGuardDestroyed);
    NE2_CannonEnable();
    EventType_AddHandler(EV_DESTROYED, NULL, NE2_OnPanelDestroyed);
    InTime2(500, Sound_PlayOnce, NE2_ANIM(0x74), 0xb2);
    InTime2(1000, Sound_PlayOnce, NE2_ANIM(0x6f), 0xb2);
    InTime2(1500, Sound_PlayOnce, NE2_ANIM(0x70), 0xb2);
    InTime2(2000, Sound_PlayOnce, NE2_ANIM(0x71), 0xb2);
    InTime2(2500, Sound_PlayOnce, NE2_ANIM(0x72), 0xb2);
    InTime2(3000, Sound_PlayOnce, NE2_ANIM(0x73), 0xb2);
    InTime2(3500, Sound_PlayOnce, NE2_ANIM(0x75), 0xb2);
}

// 0x10002ed0  EV_ANIM_FINISHED: the shutters (anim 0x37) closed: the panels disappear
static void NE2_OnShuttersClosed(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x37)
        return;
    for (int i = 0; i < 17; i++)
        Object_SetDead(NE2_DESTR(ne2_panels[i]));
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnShuttersClosed);
}

// 0x10002fb0  EV_TRIGGER_ENTER 0x40009: at the core: its shutters close and the core guards count
// (once)
static void NE2_OnCoreApproach(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x09))
        return;
    AnimPlay(NE2_ANIM(0x74), 0x37, 0);
    AnimPlay(NE2_ANIM(0x6f), 0x38, 0);
    AnimPlay(NE2_ANIM(0x70), 0x39, 0);
    AnimPlay(NE2_ANIM(0x71), 0x3a, 0);
    AnimPlay(NE2_ANIM(0x72), 0x3b, 0);
    AnimPlay(NE2_ANIM(0x73), 0x3c, 0);
    AnimPlay(NE2_ANIM(0x75), 0x3d, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnCoreApproach);
    EventType_AddHandler(EV_DESTROYED, NULL, NE2_OnCoreGuardDestroyed);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnShuttersClosed);
    InTime2(500, Sound_PlayOnce, NE2_ANIM(0x75), 0xb2);
    InTime2(1000, Sound_PlayOnce, NE2_ANIM(0x73), 0xb2);
    InTime2(1500, Sound_PlayOnce, NE2_ANIM(0x72), 0xb2);
    InTime2(2000, Sound_PlayOnce, NE2_ANIM(0x71), 0xb2);
    InTime2(2500, Sound_PlayOnce, NE2_ANIM(0x70), 0xb2);
    InTime2(3000, Sound_PlayOnce, NE2_ANIM(0x6f), 0xb2);
    InTime2(3500, Sound_PlayOnce, NE2_ANIM(0x74), 0xb2);
}

// 0x10003110  EV_DESTROYED: the AI defence 0x10e0002 destroyed: its arms collapse and vanish, the
// wreckage appears 5 s later. (Masks the event's object id in place.)
static void NE2_OnBossDestroyed(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if ((ObjId)e->arg[0] != NE2_BOSS)
        return;
    InTime2(1500, Sound_PlayOnce, e->arg[0], 0xd8);
    InTime2(2500, Sound_PlayOnce, NE2_BOSS, 0xd8);
    InTime2(3500, Sound_PlayOnce, NE2_MOBANIM(0x16), 0xf7);
    AnimStop(NE2_BOSS, 0);
    AnimStop(NE2_MOBANIM(0x18), 0);
    AnimStop(NE2_MOBANIM(0x28), 0);
    AnimStop(NE2_MOBANIM(0x27), 0);
    AnimStop(NE2_MOBANIM(0x17), 0);
    AnimStop(NE2_MOBANIM(0x26), 0);
    AnimStop(NE2_MOBANIM(0x16), 0);
    AnimSetUpdateInterval(NE2_BOSS, 5);
    AnimSetUpdateInterval(NE2_MOBANIM(0x18), 5);
    AnimSetUpdateInterval(NE2_MOBANIM(0x28), 5);
    AnimSetUpdateInterval(NE2_MOBANIM(0x27), 5);
    AnimSetUpdateInterval(NE2_MOBANIM(0x17), 5);
    AnimSetUpdateInterval(NE2_MOBANIM(0x26), 5);
    AnimSetUpdateInterval(NE2_MOBANIM(0x16), 5);
    InTime3(200, AnimPlay, NE2_BOSS, 0x47, 0x60);
    InTime3(200, AnimPlay, NE2_MOBANIM(0x18), 0x48, 0);
    InTime3(200, AnimPlay, NE2_MOBANIM(0x28), 0x49, 0);
    InTime3(200, AnimPlay, NE2_MOBANIM(0x27), 0x4a, 0);
    InTime3(200, AnimPlay, NE2_MOBANIM(0x17), 0x4b, 0);
    InTime3(200, AnimPlay, NE2_MOBANIM(0x26), 0x4c, 0);
    InTime3(200, AnimPlay, NE2_MOBANIM(0x16), 0x4d, 0);
    InTime2(5000, AnimStop, NE2_MOBANIM(0x18), 0);
    InTime2(5000, AnimStop, NE2_MOBANIM(0x28), 0);
    InTime2(5000, AnimStop, NE2_MOBANIM(0x27), 0);
    InTime2(5000, AnimStop, NE2_MOBANIM(0x17), 0);
    InTime2(5000, AnimStop, NE2_MOBANIM(0x26), 0);
    InTime2(5000, AnimStop, NE2_MOBANIM(0x16), 0);
    InTime1(5000, MobObj_Sleep, 0x18);
    InTime1(5000, MobObj_Sleep, 0x28);
    InTime1(5000, MobObj_Sleep, 0x27);
    InTime1(5000, MobObj_Sleep, 0x17);
    InTime1(5000, MobObj_Sleep, 0x26);
    InTime1(5000, MobObj_Sleep, 0x16);
    InTime1(5000, MobObj_Sleep, 2);
    InTime1(5000, Object_SetAlive, NE2_DESTR(0x78));
    InTime1(5000, Object_SetAlive, NE2_DESTR(0xde));
    InTime1(5000, Object_SetAlive, NE2_DESTR(0x141));
    InTime1(5000, Object_SetAlive, NE2_DESTR(0x140));
    InTime1(5000, Object_SetAlive, NE2_DESTR(0xdd));
    InTime1(5000, Object_SetAlive, NE2_DESTR(0x13f));
    InTime1(5000, Object_SetAlive, NE2_DESTR(0xdc));
}

// 0x10003440  EV_TRIGGER_ENTER 0x40055: the AI defence and its arms come alive (once)
static void NE2_OnBossLevel(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x55))
        return;
    AnimPlay(NE2_BOSS, 0x40, 0x60);
    AnimPlay(NE2_MOBANIM(0x18), 0x41, 0);
    AnimPlay(NE2_MOBANIM(0x28), 0x42, 0);
    AnimPlay(NE2_MOBANIM(0x27), 0x43, 0);
    AnimPlay(NE2_MOBANIM(0x17), 0x44, 0);
    AnimPlay(NE2_MOBANIM(0x26), 0x45, 0);
    AnimPlay(NE2_MOBANIM(0x16), 0x46, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnBossLevel);
    EventType_AddHandler(EV_DESTROYED, NULL, NE2_OnBossDestroyed);
    InTime2(6000, Sound_PlayOnce, NE2_MOBANIM(0x16), 0x374);
    InTime2(12000, Sound_PlayOnce, NE2_MOBANIM(0x16), 0x374);
    InTime2(16000, Sound_PlayOnce, NE2_MOBANIM(0x16), 0x374);
    InTime2(20000, Sound_PlayOnce, NE2_MOBANIM(0x16), 0x374);
    InTime2(10000, Sound_PlayOnce, NE2_MOBANIM(0x26), 0x374);
    InTime2(14000, Sound_PlayOnce, NE2_MOBANIM(0x26), 0x374);
    InTime2(18000, Sound_PlayOnce, NE2_MOBANIM(0x26), 0x374);
    Sound_PlayLoop(NE2_BOSS, 0x8d, -1);
}

// 0x100035b0  EV_TRIGGER_ENTER/LEAVE 0x4017d / 0x4017e: door pairs 0x30174/76 and 0x30175/77
static void NE2_OnDoors174(const LanderEvent *ev)
{
    int32_t flags = ev->type == EV_TRIGGER_ENTER ? 0 : 4;
    if ((ObjId)ev->arg[1] == NE2_TRIGGER(0x17d)) {
        Sound_PlayOnce(NE2_ANIM(0x174), 0xa9);
        Sound_PlayOnce(NE2_ANIM(0x176), 0xa9);
        AnimPlay(NE2_ANIM(0x174), 0x57, flags);
        AnimPlay(NE2_ANIM(0x176), 0x58, flags);
    }
    if ((ObjId)ev->arg[1] == NE2_TRIGGER(0x17e)) {
        Sound_PlayOnce(NE2_ANIM(0x175), 0xa9);
        Sound_PlayOnce(NE2_ANIM(0x177), 0xa9);
        AnimPlay(NE2_ANIM(0x175), 0x57, flags);
        AnimPlay(NE2_ANIM(0x177), 0x58, flags);
    }
}

// 0x10003700  EV_ANIM_FINISHED 0x300df: 0x30142/0x30143 follow (once)
static void NE2_OnGateDFDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_ANIM(0xdf))
        return;
    AnimPlay(NE2_ANIM(0x142), 0x5a, 0x80);
    AnimPlay(NE2_ANIM(0x143), 0x5a, 0x80);
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnGateDFDone);
}

// 0x10003750  EV_TRIGGER_ENTER 0x4017e: 0x300df moves (once)
static void NE2_OnTrigger17E(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x17e))
        return;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnTrigger17E);
    AnimPlay(NE2_ANIM(0xdf), 0x59, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnGateDFDone);
}

// 0x100037a0  EV_ANIM_FINISHED 0x3013a: the first piston chain crushed turret 0x19d (once)
static void NE2_OnPistonsADone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_ANIM(0x13a))
        return;
    Object_SetProperty(NE2_TURRET(0x19d), PROP_HITPOINTS, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnPistonsADone);
}

// 0x100037d0  EV_ANIM_FINISHED 0x3013b: the second piston chain crushed turret 0x19e (once)
static void NE2_OnPistonsBDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_ANIM(0x13b))
        return;
    Object_SetProperty(NE2_TURRET(0x19e), PROP_HITPOINTS, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnPistonsBDone);
}

// 0x10003800  EV_DESTROYED 0x8013c / 0x8013d (unmasked id): a chain of five pistons fires
static void NE2_OnPistonBlockDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == NE2_DESTR(0x13c)) {
        AnimSetUpdateInterval(NE2_ANIM(0x132), 2);
        AnimSetUpdateInterval(NE2_ANIM(0x134), 2);
        AnimSetUpdateInterval(NE2_ANIM(0x136), 2);
        AnimSetUpdateInterval(NE2_ANIM(0x138), 2);
        AnimSetUpdateInterval(NE2_ANIM(0x13a), 2);
        AnimPlay(NE2_ANIM(0x132), 0x5b, 0);
        Sound_PlayOnce(NE2_ANIM(0x132), 0x372);
        AnimPlay(NE2_ANIM(0x134), 0x5c, 0);
        InTime2(100, Sound_PlayOnce, NE2_ANIM(0x134), 0x372);
        AnimPlay(NE2_ANIM(0x136), 0x5d, 0);
        InTime2(200, Sound_PlayOnce, NE2_ANIM(0x136), 0x372);
        AnimPlay(NE2_ANIM(0x138), 0x5e, 0);
        InTime2(300, Sound_PlayOnce, NE2_ANIM(0x138), 0x372);
        AnimPlay(NE2_ANIM(0x13a), 0x5f, 0);
        InTime2(400, Sound_PlayOnce, NE2_ANIM(0x13a), 0x372);
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnPistonsADone);
    }
    if ((ObjId)ev->arg[0] != NE2_DESTR(0x13d))
        return;
    AnimSetUpdateInterval(NE2_ANIM(0x133), 2);
    AnimSetUpdateInterval(NE2_ANIM(0x135), 2);
    AnimSetUpdateInterval(NE2_ANIM(0x137), 2);
    AnimSetUpdateInterval(NE2_ANIM(0x139), 2);
    AnimSetUpdateInterval(NE2_ANIM(0x13b), 2);
    AnimPlay(NE2_ANIM(0x133), 0x5b, 0);
    Sound_PlayOnce(NE2_ANIM(0x133), 0x372);
    AnimPlay(NE2_ANIM(0x135), 0x5c, 0);
    InTime2(100, Sound_PlayOnce, NE2_ANIM(0x135), 0x372);
    AnimPlay(NE2_ANIM(0x137), 0x5d, 0);
    InTime2(200, Sound_PlayOnce, NE2_ANIM(0x137), 0x372);
    AnimPlay(NE2_ANIM(0x139), 0x5e, 0);
    InTime2(300, Sound_PlayOnce, NE2_ANIM(0x139), 0x372);
    AnimPlay(NE2_ANIM(0x13b), 0x5f, 0);
    InTime2(400, Sound_PlayOnce, NE2_ANIM(0x13b), 0x372);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnPistonsBDone);
}

// 0x10003a40  EV_SWITCH 0xa0149: 0x30148 moves (switching it off is refused)
static void NE2_OnSwitch149(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x149))
        return;
    Sound_PlayOnce(NE2_SWITCH(0x149), 0xeb);
    if (ev->arg[1] == 1) {
        Sound_PlayOnce(NE2_ANIM(0x148), 0xab);
        AnimPlay(NE2_ANIM(0x148), 0x60, 0);
    }
    if (ev->arg[1] == 0)
        Object_SetProperty(NE2_SWITCH(0x149), PROP_STATE, 1);
}

// 0x10003ab0  EV_TRIGGER_ENTER/LEAVE 0x400e5 (pod lift not moving): doors 0x300e1/e2, 0x300e3/e4
// half a second apart
static void NE2_OnDoorsE1(const LanderEvent *ev)
{
    if (st.podLiftMoving != 0 || (ObjId)ev->arg[1] != NE2_TRIGGER(0xe5))
        return;
    Sound_PlayOnce(NE2_ANIM(0xe1), 0xa9);
    Sound_PlayOnce(NE2_ANIM(0xe1), 0xa9);        // sic: 0x300e1 twice (0x300e2 silent)
    InTime2(500, Sound_PlayOnce, NE2_ANIM(0xe3), 0xa9);
    InTime2(500, Sound_PlayOnce, NE2_ANIM(0xe4), 0xa9);
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(NE2_ANIM(0xe1), 0x61, 0);
        AnimPlay(NE2_ANIM(0xe2), 0x61, 0);
        InTime3(500, AnimPlay, NE2_ANIM(0xe3), 0x61, 0);
        InTime3(500, AnimPlay, NE2_ANIM(0xe4), 0x61, 0);
    }
    if (ev->type != EV_TRIGGER_LEAVE)
        return;
    AnimPlay(NE2_ANIM(0xe3), 0x61, 4);
    AnimPlay(NE2_ANIM(0xe4), 0x61, 4);
    InTime3(500, AnimPlay, NE2_ANIM(0xe1), 0x61, 4);
    InTime3(500, AnimPlay, NE2_ANIM(0xe2), 0x61, 4);
}

// 0x10003bd0  EV_ANIM_FINISHED / EV_TRIGGER_ENTER: the transporter is back (anim 0x63): the camera
// returns to the lander, doors 0x30185/86 open; trigger 0x400e0 closes them, wakes pod 0x2b (once)
static void NE2_OnTransporterBack(const LanderEvent *ev)
{
    if (ev->type == EV_ANIM_FINISHED && ev->arg[1] == 0x63) {
        Object_SetProperty(NE2_CAMERA1A, PROP_FLAGS, (intptr_t)0x80000820u);
        Object_SetProperty(NE2_CAMERA1A, PROP_TARGET, NE2_LANDER);
        Sound_StopObject(NE2_TRANSPORTER);
        Sound_PlayOnce(NE2_TRANSPORTER, 0xd0);
        Sound_PlayOnce(NE2_ANIM(0x185), 0xa9);
        Sound_PlayOnce(NE2_ANIM(0x186), 0xa9);
        AnimPlay(NE2_ANIM(0x185), 0x64, 0);
        AnimPlay(NE2_ANIM(0x186), 0x64, 0);
    }
    if (ev->type != EV_TRIGGER_ENTER || (ObjId)ev->arg[1] != NE2_TRIGGER(0xe0))
        return;
    Sound_PlayOnce(NE2_ANIM(0x185), 0xa9);
    Sound_PlayOnce(NE2_ANIM(0x186), 0xa9);
    AnimPlay(NE2_ANIM(0x185), 0x64, 4);
    AnimPlay(NE2_ANIM(0x186), 0x64, 4);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnTransporterBack);
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnTransporterBack);
    MobObj_Wake(0x2b);
}

// 0x10003cf0  EV_TRIGGER_ENTER 0x400c4 (armed 11 s after the outward ride): the ride back, watched
// by camera mob 0x1a
static void NE2_OnTransporterReturn(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0xc4))
        return;
    MobObj_Sleep(0x19);
    MobObj_Wake(0x1a);
    Object_SetProperty(NE2_CAMERA1A, PROP_FLAGS, 0x820);
    Object_SetProperty(NE2_CAMERA1A, PROP_TARGET, NE2_TRANSPORTER);
    Sound_PlayOnce(NE2_ANIM(0x10b), 0xa9);
    Sound_PlayOnce(NE2_ANIM(0x10c), 0xa9);
    AnimPlay(NE2_ANIM(0x10b), 0x64, 4);
    AnimPlay(NE2_ANIM(0x10c), 0x64, 4);
    InTime3(1000, AnimPlay, NE2_TRANSPORTER, 0x63, 0);
    InTime2(1000, Sound_PlayOnce, NE2_TRANSPORTER, 0xce);
    InTime3(1840, Sound_PlayLoop, NE2_TRANSPORTER, 0xcf, -1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnTransporterReturn);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnTransporterBack);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTransporterBack);
}

// 0x10003e10  EV_ANIM_FINISHED anim 0x62: the transporter arrived: the camera returns to the lander,
// doors 0x3010b/c open 1 s later, the return trigger is armed 11 s later (once)
static void NE2_OnTransporterArrived(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x62)
        return;
    Object_SetProperty(NE2_CAMERA19, PROP_FLAGS, (intptr_t)0x80000820u);
    Object_SetProperty(NE2_CAMERA19, PROP_TARGET, NE2_LANDER);
    Sound_StopObject(NE2_TRANSPORTER);
    Sound_PlayOnce(NE2_TRANSPORTER, 0xd0);
    AnimStop(NE2_TRANSPORTER, 0);
    InTime2(1000, Sound_PlayOnce, NE2_ANIM(0x10b), 0xa9);
    InTime2(1000, Sound_PlayOnce, NE2_ANIM(0x10c), 0xa9);
    InTime3(1000, AnimPlay, NE2_ANIM(0x10b), 0x64, 0);
    InTime3(1000, AnimPlay, NE2_ANIM(0x10c), 0x64, 0);
    InTime3(11000, EventType_AddHandler, EV_TRIGGER_ENTER, 0, NE2_OnTransporterReturn);
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnTransporterArrived);
}

// 0x10003f10  EV_TRIGGER_ENTER 0x400c3: the transporter 0x30052 leaves, watched by camera mob 0x19
// (once)
static void NE2_OnTransporterStart(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0xc3))
        return;
    MobObj_Sleep(NE2_MOB_CAMERA);
    MobObj_Wake(0x19);
    Object_SetProperty(NE2_CAMERA19, PROP_FLAGS, 0x820);
    Object_SetProperty(NE2_CAMERA19, PROP_TARGET, NE2_TRANSPORTER);
    AnimPlay(NE2_TRANSPORTER, 0x62, 0);
    Sound_PlayOnce(NE2_TRANSPORTER, 0xce);
    InTime3(840, Sound_PlayLoop, NE2_TRANSPORTER, 0xcf, -1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnTransporterArrived);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnTransporterStart);
}

// 0x10003fd0  EV_TRIGGER_ENTER 0x40144: doors 0x30087/88 close, their switches go off (once)
static void NE2_OnTrigger144(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x144))
        return;
    Sound_PlayOnce(NE2_ANIM(0x87), 0xab);
    Sound_PlayOnce(NE2_ANIM(0x88), 0xab);
    AnimPlay(NE2_ANIM(0x87), 0x66, 4);
    AnimPlay(NE2_ANIM(0x88), 0x66, 4);
    Object_SetProperty(NE2_SWITCH(0x81), PROP_STATE, 0);
    Object_SetProperty(NE2_SWITCH(0x82), PROP_STATE, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnTrigger144);
}

// 0x10004060  EV_ANIM_FINISHED 0x300c7: the two anim turrets 0x10e0021/22 deploy and arm (once)
static void NE2_OnTurretHatchOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_ANIM(0xc7))
        return;
    Sound_PlayOnce(NE2_ANIMTURRET(0x21), 0xbf);
    Sound_PlayOnce(NE2_ANIMTURRET(0x22), 0xbf);
    AnimPlay(NE2_ANIMTURRET(0x21), 0x67, 0xe0);
    AnimPlay(NE2_ANIMTURRET(0x22), 0x67, 0xe0);
    Object_SetProperty(NE2_ANIMTURRET(0x21), PROP_STATE, 1);
    Object_SetProperty(NE2_ANIMTURRET(0x22), PROP_STATE, 1);
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnTurretHatchOpen);
}

// 0x10004100  EV_SWITCH: one-way door switches (switching off is refused): 0xa0083 door 0x30089,
// 0xa0080 doors 0x30085/86, 0xa0081/0xa0082 doors 0x30087/88, 0xa007e doors 0x30079/7a (+ the turret
// hatch 0x300c7/c8); 0xa007f doors 0x3007b/7c + 0x3017c both ways
static void NE2_OnSwitches(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == NE2_SWITCH(0x83)) {
        Sound_PlayOnce(NE2_SWITCH(0x83), 0xeb);
        if (ev->arg[1] == 1) {
            Sound_PlayOnce(NE2_ANIM(0x89), 0xab);
            AnimPlay(NE2_ANIM(0x89), 0x65, 0);
        }
        if (ev->arg[1] == 0)
            Object_SetProperty(NE2_SWITCH(0x83), PROP_STATE, 1);
    }
    if ((ObjId)ev->arg[0] == NE2_SWITCH(0x80)) {
        Sound_PlayOnce(NE2_SWITCH(0x80), 0xeb);
        if (ev->arg[1] == 1) {
            Sound_PlayOnce(NE2_ANIM(0x85), 0xab);
            Sound_PlayOnce(NE2_ANIM(0x86), 0xab);
            AnimPlay(NE2_ANIM(0x85), 0x66, 0);
            AnimPlay(NE2_ANIM(0x86), 0x66, 0);
        }
        if (ev->arg[1] == 0)
            Object_SetProperty(NE2_SWITCH(0x80), PROP_STATE, 1);
    }
    if ((ObjId)ev->arg[0] == NE2_SWITCH(0x81)) {
        Sound_PlayOnce(NE2_SWITCH(0x81), 0xeb);
        if (ev->arg[1] == 1) {
            Sound_PlayOnce(NE2_ANIM(0x87), 0xab);
            Sound_PlayOnce(NE2_ANIM(0x88), 0xab);
            AnimPlay(NE2_ANIM(0x87), 0x66, 0);
            AnimPlay(NE2_ANIM(0x88), 0x66, 0);
            EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTrigger144);
            Object_SetProperty(NE2_SWITCH(0x82), PROP_STATE, 1);
        }
        if (ev->arg[1] == 0)
            Object_SetProperty(NE2_SWITCH(0x81), PROP_STATE, 1);
    }
    if ((ObjId)ev->arg[0] == NE2_SWITCH(0x82)) {
        Sound_PlayOnce(NE2_SWITCH(0x82), 0xeb);
        if (ev->arg[1] == 1) {
            Sound_PlayOnce(NE2_ANIM(0x87), 0xab);
            Sound_PlayOnce(NE2_ANIM(0x88), 0xab);
            Object_SetProperty(NE2_SWITCH(0x81), PROP_STATE, 1);
            AnimPlay(NE2_ANIM(0x87), 0x66, 0);
            AnimPlay(NE2_ANIM(0x88), 0x66, 0);
        }
        if (ev->arg[1] == 0)
            Object_SetProperty(NE2_SWITCH(0x82), PROP_STATE, 1);
    }
    if ((ObjId)ev->arg[0] == NE2_SWITCH(0x7e)) {
        Sound_PlayOnce(NE2_SWITCH(0x7e), 0xeb);
        if (ev->arg[1] == 1) {
            Sound_PlayOnce(NE2_ANIM(0x79), 0xab);
            Sound_PlayOnce(NE2_ANIM(0x7a), 0xab);
            AnimPlay(NE2_ANIM(0x79), 0x66, 0);
            AnimPlay(NE2_ANIM(0x7a), 0x66, 0);
            MobObj_Wake(0x21);
            MobObj_Wake(0x22);
            Object_SetProperty(NE2_ANIMTURRET(0x21), PROP_STATE, 0);
            Object_SetProperty(NE2_ANIMTURRET(0x22), PROP_STATE, 0);
            Sound_PlayOnce(NE2_ANIM(0xc7), 0xda);
            Sound_PlayOnce(NE2_ANIM(0xc8), 0xda);
            AnimPlay(NE2_ANIM(0xc7), 0x68, 0);
            AnimPlay(NE2_ANIM(0xc8), 0x68, 0);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnTurretHatchOpen);
        }
        if (ev->arg[1] == 0)
            Object_SetProperty(NE2_SWITCH(0x7e), PROP_STATE, 1);
    }
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0x7f))
        return;
    Sound_PlayOnce(NE2_SWITCH(0x7f), 0xeb);
    Sound_PlayOnce(NE2_ANIM(0x17c), 0xab);
    Sound_PlayOnce(NE2_ANIM(0x7c), 0xab);
    Sound_PlayOnce(NE2_ANIM(0x7b), 0xab);
    if (ev->arg[1] == 1) {
        AnimPlay(NE2_ANIM(0x7b), 0x66, 0);
        AnimPlay(NE2_ANIM(0x7c), 0x66, 0);
        AnimPlay(NE2_ANIM(0x17c), 0x69, 0);
    }
    if (ev->arg[1] == 0) {
        AnimPlay(NE2_ANIM(0x7b), 0x66, 4);
        AnimPlay(NE2_ANIM(0x7c), 0x66, 4);
        AnimPlay(NE2_ANIM(0x17c), 0x69, 4);
    }
}

// 0x100044a0  EV_TRIGGER_ENTER 0x40128..2b (pod lift down): a pod dropped in bay k is swapped for the
// lift's pod model (anim mob 0xe + k); trigger 0x40124 + k now waits for that model
static void NE2_OnPodInBay(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[1];
    if (t < (int32_t)NE2_TRIGGER(0x128) || t > (int32_t)NE2_TRIGGER(0x12b) || st.podLiftUp != 0)
        return;
    Object_SetProperty((ObjId)ev->arg[0], PROP_TARGET, -1);
    st.podInBay = 1;
    st.bay = (int32_t)ev->arg[1] - (int32_t)NE2_TRIGGER(0x128);
    MobObj_Sleep((int32_t)(ev->arg[0] & 0xffff));
    MobObj_Wake((st.bay + 0xe) & 0xffff);
    Object_SetProperty((ObjId)(st.bay + (int32_t)NE2_TRIGGER(0x124)), PROP_TARGET,
                       (intptr_t)(st.bay + (int32_t)NE2_MOBANIM(0x0e)));
}

// 0x10004540  EV_ANIM_FINISHED of a lift pod model 0x103000e..11: back to a real pod (mob 0x1d + k)
static void NE2_OnPodModelDone(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)NE2_MOBANIM(0x0e) || id > (int32_t)NE2_MOBANIM(0x11))
        return;
    MobObj_Sleep((st.bay + 0xe) & 0xffff);
    MobObj_Wake((st.bay + 0x1d) & 0xffff);
}

// 0x10004590  EV_POD_HOOKED: a pod 0x10c001d..20 picked up: the bay / socket triggers wait for it
static void NE2_OnPodHooked(const LanderEvent *ev)
{
    if (st.podInBay == 1)
        st.podInBay = 0;
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)NE2_POD(0x1d) || id > (int32_t)NE2_POD(0x20))
        return;
    Object_SetProperty(NE2_TRIGGER(0x12c), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x12d), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x12e), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x12f), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x128), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x129), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x12a), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x12b), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x124), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x125), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x126), PROP_TARGET, ev->arg[0]);
    Object_SetProperty(NE2_TRIGGER(0x127), PROP_TARGET, ev->arg[0]);
}

// 0x10004690  EV_ANIM_FINISHED 0x301a4: the pod lift stopped (once per ride)
static void NE2_OnPodLiftDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_PODLIFT)
        return;
    Sound_StopObject(NE2_PODLIFT);
    Sound_PlayOnce(NE2_PODLIFT, 0xcc);
    st.podLiftMoving = 0;
    st.podLiftUp = st.podLiftUp == 0;
    EventType_RemoveHandler(EV_ANIM_FINISHED, NE2_OnPodLiftDone);
}

// 0x100046f0  EV_SWITCH 0xa00e6: pod lift up (1) / down (0) with the pod in the bay; refused (the
// switch flips back) while it moves
static void NE2_OnSwitchPodLift(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0xe6))
        return;
    Sound_PlayOnce(NE2_SWITCH(0xe6), 0xeb);
    if (ev->arg[1] == 1) {
        if (st.podLiftMoving != 0) {
            Object_SetProperty(NE2_SWITCH(0xe6), PROP_STATE, 0);
            return;
        }
        Sound_PlayOnce(NE2_PODLIFT, 0xcb);
        InTime3(1190, Sound_PlayLoop, NE2_PODLIFT, 0xcc, -1);
        AnimPlay(NE2_PODLIFT, 0x6a, 0);
        if (st.podInBay == 1) {
            AnimPlay((ObjId)(st.bay + (int32_t)NE2_MOBANIM(0x0e)), 0x6a, 0);
            Object_SetDead(NE2_FORCEBOX(0x13e));
        }
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnPodLiftDone);
        return;
    }
    if (st.podLiftMoving != 0) {
        Object_SetProperty(NE2_SWITCH(0xe6), PROP_STATE, 1);
        return;
    }
    Sound_PlayOnce(NE2_PODLIFT, 0xcb);
    InTime3(1190, Sound_PlayLoop, NE2_PODLIFT, 0xcc, -1);
    AnimPlay(NE2_PODLIFT, 0x6a, 4);
    if (st.podInBay == 1) {
        AnimPlay((ObjId)(st.bay + (int32_t)NE2_POD(0x1d)), 0x6a, 4);     // sic: the pod, not the model
        Object_SetAlive(NE2_FORCEBOX(0x13e));
    }
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnPodLiftDone);
}

// 0x10004860  EV_TRIGGER_ENTER 0x4011d with the pod lift up: it comes down
static void NE2_OnPodLiftCall(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x11d) || st.podLiftUp != 1)
        return;
    Sound_PlayOnce(NE2_PODLIFT, 0xcb);
    InTime3(1190, Sound_PlayLoop, NE2_PODLIFT, 0xcc, -1);
    st.podLiftMoving = 1;
    AnimPlay(NE2_PODLIFT, 0x6a, 4);
    if (st.podInBay == 1)
        AnimPlay((ObjId)(st.bay + (int32_t)NE2_POD(0x2b)), 0x6a, 4);     // sic: pod mob 0x2b + k
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnPodLiftDone);
}

// 0x10004910  EV_ANIM_FINISHED anim 0x6b: the socketed pod's model is done: the barrier 0xf00be is
// destroyed, force box 0x1001a5 goes
static void NE2_OnPodSocketed(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x6b)
        return;
    MobObj_Sleep((st.bay + 0xa) & 0xffff);
    AnimStop(NE2_BARRIER, 0);
    Object_SetProperty(NE2_BARRIER, PROP_HITPOINTS, 0);
    Object_SetDead(NE2_FORCEBOX(0x1a5));
}

// 0x10004970  EV_TRIGGER_ENTER 0x4012c..2f: a pod in socket k is swapped for the socket model (anim
// mob 0xa + k), which plays anim 0x6b
static void NE2_OnPodAtSocket(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[1];
    if (t < (int32_t)NE2_TRIGGER(0x12c) || t > (int32_t)NE2_TRIGGER(0x12f))
        return;
    Object_SetProperty((ObjId)ev->arg[0], PROP_TARGET, -1);
    st.bay = (int32_t)ev->arg[1] - (int32_t)NE2_TRIGGER(0x12c);
    MobObj_Sleep((int32_t)(ev->arg[0] & 0xffff));
    MobObj_Wake((st.bay + 0xa) & 0xffff);
    InTime3(100, AnimPlay, st.bay + (int32_t)NE2_MOBANIM(0x0a), 0x6b, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnPodSocketed);
}

// 0x10004a20  EV_TRIGGER_ENTER/LEAVE 0x40146 / 0x40147 (generator on): doors 0x3014f / 0x30150; the
// 0x30150 door closes 30 s after leaving (sic: with 0x3014f's sound)
static void NE2_OnDoors14F(const LanderEvent *ev)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        if ((ObjId)ev->arg[1] == NE2_TRIGGER(0x146) && st.generatorOff == 0) {
            Sound_PlayOnce(NE2_ANIM(0x14f), 0xab);
            AnimPlay(NE2_ANIM(0x14f), 0x30, 0);
        }
        if ((ObjId)ev->arg[1] == NE2_TRIGGER(0x147) && st.generatorOff == 0) {
            Sound_PlayOnce(NE2_ANIM(0x150), 0xab);
            AnimPlay(NE2_ANIM(0x150), 0x30, 0);
        }
        return;
    }
    if ((ObjId)ev->arg[1] == NE2_TRIGGER(0x146) && st.generatorOff == 0) {
        Sound_PlayOnce(NE2_ANIM(0x14f), 0xab);
        AnimPlay(NE2_ANIM(0x14f), 0x30, 4);
    }
    if ((ObjId)ev->arg[1] == NE2_TRIGGER(0x147) && st.generatorOff == 0) {
        InTime2(30000, Sound_PlayOnce, NE2_ANIM(0x14f), 0xab);
        InTime3(30000, AnimPlay, NE2_ANIM(0x150), 0x30, 4);
    }
}

// 0x10004b30  EV_PICKUP 0x130000..05: +50 % shield
static void NE2_OnPickup(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)NE2_PICKUP(0) || id > (int32_t)NE2_PICKUP(5))
        return;
    int32_t shield = (int32_t)Object_GetProperty(NE2_LANDER, PROP_HITPOINTS) + 0x80;
    Voice_ShieldAcquired(NE2_LANDER, 50);
    Object_SetProperty(NE2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10004ba0  EV_TRIGGER_ENTER/LEAVE: head light on in the dark zones 0x400fe..103, off when entering
// 0x400f8..fd from outside them
static void NE2_OnLightZones(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[1];
    if (ev->type == EV_TRIGGER_ENTER) {
        if (t >= (int32_t)NE2_TRIGGER(0xfe) && t <= (int32_t)NE2_TRIGGER(0x103)) {
            st.inDarkZone = 1;
            if (st.headLightOn == 0) {
                Lander_SetHeadLight(NE2_LANDER, 0x31c, 0xccc);
                st.headLightOn = 1;
            }
        }
        t = (int32_t)ev->arg[1];
        if (t < (int32_t)NE2_TRIGGER(0xf8) || t > (int32_t)NE2_TRIGGER(0xfd))
            return;
        if (st.inDarkZone != 0 || st.headLightOn != 1)
            return;
        Lander_SetHeadLight(NE2_LANDER, 0, 0);
        st.headLightOn = 0;
        return;
    }
    if (t >= (int32_t)NE2_TRIGGER(0xfe) && t <= (int32_t)NE2_TRIGGER(0x103))
        st.inDarkZone = 0;
}

// 0x10004c60  EV_SWITCH 0xa00c2: door 0x300d9
static void NE2_OnSwitchDoorD9(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != NE2_SWITCH(0xc2))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(NE2_ANIM(0xd9), 0x4e, 0);
        st.doorD9Closed = 0;
        return;
    }
    AnimPlay(NE2_ANIM(0xd9), 0x4e, 4);
    st.doorD9Closed = 1;
}

// 0x10004cb0  EV_COLLISION {body, other, impulse}: touching 0x30107 wakes mob 0x2a (once); touching a
// force field 0xf0108..0a while gate 0x30020 is open shuts the gate
static void NE2_OnTouch(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == NE2_ANIM(0x107) && st.mob2AWoken == 0) {
        MobObj_Wake(0x2a);
        st.mob2AWoken = 1;
    }
    int32_t id = (int32_t)ev->arg[1];
    if (id < (int32_t)NE2_ANIMDESTR(0x108) || id > (int32_t)NE2_ANIMDESTR(0x10a) || st.gate20Open != 1)
        return;
    AnimSetUpdateInterval(NE2_ANIM(0x20), 2);
    AnimSetUpdateInterval(NE2_ANIM(0x21), 2);
    AnimSetUpdateInterval(NE2_ANIM(0x22), 2);
    AnimSetUpdateInterval(NE2_ANIM(0x23), 2);
    InTime3(100, AnimPlay, NE2_ANIM(0x20), 0x31, 4);
    InTime3(100, AnimPlay, NE2_ANIM(0x21), 0x31, 4);
    AnimPlay(NE2_ANIM(0x22), 0x31, 4);
    AnimPlay(NE2_ANIM(0x23), 0x31, 4);
    st.gate20Open = 0;
    Object_SetProperty(NE2_SWITCH(0x8a), PROP_STATE, 0);
}

// 0x10004db0  EV_TRIGGER_ENTER 0x400d8 (generator on, door shut): door 0x300d7 opens for 20 s
static void NE2_OnTriggerD8(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0xd8) || st.doorD7Open != 0)
        return;
    Sound_PlayOnce(NE2_ANIM(0xd7), 0xab);
    AnimPlay(NE2_ANIM(0xd7), 0x30, 0);
    st.doorD7Open = 1;
    EventType_AddHandler(EV_TICK_1S, NULL, NE2_DoorD7Tick);
}

// 0x10004e10  EV_TICK_100MS: the exterior ambient darkens with depth: v = (y + 200000) * 256 / 100000
// clamped to 0..256, colour (12v, 30v, 54v) / 256
static void NE2_DepthAmbient(const LanderEvent *ev)
{
    (void)ev;
    int32_t y = ((const int32_t *)Object_GetProperty(NE2_LANDER, PROP_POSITION))[1];
    int32_t v = ((y + 200000) << 8) / 100000;
    if (v < 0)
        v = 0;
    else if (v > 0x100)
        v = 0x100;
    Environment_SetExteriorAmbient((v * 12) >> 8, (v * 30) >> 8, (v * 54) >> 8);
}

// one pressure warning (0x10004f2x ..): sounds and the cockpit voice
static void NE2_PressureWarn(void)
{
    Sound_PlayOnce(NE2_LANDER, 0x37e);
    Sound_PlayOnce(NE2_LANDER, 0x37d);
    if (CockVox(1))
        Sound_PlayChain(SND_COCKPIT_VOICE, 2, 8, 0x41c);
}

// shield -= damage, clamped at 0
static void NE2_PressureDamage(int32_t damage)
{
    st.depthShield = (int32_t)Object_GetProperty(NE2_LANDER, PROP_HITPOINTS) - damage;
    Object_SetProperty(NE2_LANDER, PROP_HITPOINTS, st.depthShield < 0 ? 0 : st.depthShield);
}

// 0x10004e80  EV_TICK_1S: pressure below -108800 (warning), -121600 (-0x20), -134400 (-0x40),
// -147200 (-0x80), -160000 (destroyed); climbing back re-arms the thresholds. Unlike Neptune 1 the
// handler stays registered.
static void NE2_DepthTick(const LanderEvent *ev)
{
    (void)ev;
    st.depth = ((const int32_t *)Object_GetProperty(NE2_LANDER, PROP_POSITION))[1];
    if (st.depth < -108800 && st.depthNext >= -108800) {
        st.depthNext = -121600;
        Sound_PlayOnce(NE2_LANDER, 0x37e);
        if (CockVox(1))
            Sound_PlayChain(SND_COCKPIT_VOICE, 2, 8, 0x41c);
    }
    if (st.depth < -121600 && st.depthNext >= -121600) {
        st.depthNext = -134400;
        st.depthLast = -121600;
        NE2_PressureWarn();
        NE2_PressureDamage(0x20);
    }
    if (st.depth < -134400 && st.depthNext >= -134400) {
        st.depthNext = -147200;
        st.depthLast = -134400;
        NE2_PressureWarn();
        NE2_PressureDamage(0x40);
    }
    if (st.depth < -147200 && st.depthNext >= -147200) {
        st.depthNext = -160000;
        st.depthLast = -147200;
        NE2_PressureWarn();
        NE2_PressureDamage(0x80);
    }
    if (st.depth < -160000 && st.depthNext >= -160000) {
        st.depthNext = -180000;
        st.depthLast = -160000;
        NE2_PressureWarn();
        Object_SetProperty(NE2_LANDER, PROP_HITPOINTS, 0);
    }
    // 0x10005130: climbing above the last threshold moves both back up one step
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

// 0x100051c0  EV_TRIGGER_ENTER 0x40053: the AI defence's arms wake (once)
static void NE2_OnBossArms(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != NE2_TRIGGER(0x53))
        return;
    MobObj_Wake(0x18);
    MobObj_Wake(0x28);
    MobObj_Wake(0x27);
    MobObj_Wake(0x17);
    MobObj_Wake(0x26);
    MobObj_Wake(0x16);
    MobObj_Wake(2);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, NE2_OnBossArms);
}

// 0x100052b0  common set-up
static void NE2_Setup(void)
{
    st.text = ne2_text[gLanguage];
    for (int32_t id = 0xab; id <= 0xb8; id++)                  // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = 0x188; id <= 0x19c; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = 0x1c; id <= 0x1d; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x2d);
    for (int32_t id = 0x1e; id <= 0x1f; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x2d);
    for (int32_t id = 0x56; id <= 0x64; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    for (int32_t id = 0x9e; id <= 0x9f; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = (int32_t)OBJ_ID(OT_FUELDUMP, 0xa4); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0xaa); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = (int32_t)NE2_DESTR(0x2d); id <= (int32_t)NE2_DESTR(0x51); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(NE2_BOSS, PROP_ARMOUR, 0x78);
    Object_SetProperty(NE2_BOSS, PROP_EXPLOSION, 0x8c);
    Object_SetProperty(NE2_TURRET(0x123), PROP_ARMOUR, 0x1e);
    Object_SetProperty(NE2_ANIMTURRET(0x21), PROP_ARMOUR, 0x23);
    Object_SetProperty(NE2_ANIMTURRET(0x22), PROP_ARMOUR, 0x23);
    Object_SetProperty(NE2_TURRET(0x19d), PROP_ARMOUR, 0x28);
    Object_SetProperty(NE2_TURRET(0x19e), PROP_ARMOUR, 0x28);
    MobObj_Wake(3);
    MobObj_Wake(4);
    MobObj_Wake(5);
    MobObj_Wake(6);
    MobObj_Wake(7);
    MobObj_Wake(8);
    MobObj_Wake(9);
    Sound_PlayLoop(MOB_ID(OT_AMBIENTFX, 3), 0x381, -1);
    SetGravity(0x16);
    gAirResistance = 0x1e;
    gMainThrottlePower = 0x26;
    gNbWaypoints = ne2_nbWaypoints;
    gWaypoints = ne2_waypoints;
    Media_AudioPlay(0);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);           // the entrance objective
    Object_SetProperty(NE2_TRIGGER(0x27), PROP_TARGET, NE2_ANIM(0x26));
    gTurretTypes[21] = &ne2_turretType21;
    gTurretTypes[22] = &ne2_turretType22;
    gTurretTypes[13] = &ne2_turretType13;
    gTurretTypes[15] = &ne2_turretType15;
    EventType_AddHandler(EV_DESTROYED, NULL, NE2_OnGateGuardDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10005220
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnStationEntered);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchDoor15B);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTrigger14E);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTrigger14D);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoor104);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoor104);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoors17F);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchDoor121);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchDoor54);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchLift);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnLiftDone);
    AnimSetUpdateInterval(NE2_LIFT, 0xf);
    AnimSetUpdateInterval(NE2_ANIM(0x26), 0xf);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoors10F);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoors10F);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitch145);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchGenerator);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchDoorD7);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchGate20);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnCoreDoorOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnCoreDoorClose);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnCoreApproach);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnBossLevel);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoors174);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoors174);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTrigger17E);
    EventType_AddHandler(EV_DESTROYED, NULL, NE2_OnPistonBlockDestroyed);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitch149);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoorsE1);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoorsE1);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTransporterStart);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitches);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnPodInBay);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, NE2_OnPodModelDone);
    EventType_AddHandler(EV_POD_HOOKED, NULL, NE2_OnPodHooked);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchPodLift);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnPodLiftCall);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnPodAtSocket);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnDoors14F);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnDoors14F);
    EventType_AddHandler(EV_PICKUP, NULL, NE2_OnPickup);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnLightZones);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, NE2_OnLightZones);
    EventType_AddHandler(EV_SWITCH, NULL, NE2_OnSwitchDoorD9);
    EventType_AddHandler(EV_COLLISION, NULL, NE2_OnTouch);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnTriggerD8);
    EventType_AddHandler(EV_TICK_1S, NULL, NE2_DepthTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, NE2_OnBossArms);
    EventType_AddHandler(EV_TICK_100MS, NULL, NE2_DepthAmbient);
    for (int32_t id = (int32_t)NE2_ANIM(0xba); id <= (int32_t)NE2_ANIM(0xc1); id++)
        Sound_PlayLoop((ObjId)id, 0xb4, -1);
    AnimPlay(NE2_ANIM(0xba), 0x50, 0x81);
    AnimPlay(NE2_ANIM(0xbb), 0x50, 0x81);
    AnimPlay(NE2_ANIM(0xbc), 0x50, 0x81);
    AnimPlay(NE2_ANIM(0xbd), 0x50, 0x81);
    AnimPlay(NE2_BARRIER, 0x50, 0x81);
    AnimPlay(NE2_ANIM(0xbf), 0x50, 0x81);
    AnimPlay(NE2_ANIM(0xc0), 0x50, 0x81);
    AnimPlay(NE2_ANIM(0xc1), 0x50, 0x81);
    AnimPlay(NE2_ANIM(0x107), 0x2c, 0x82);
    AnimPlay(NE2_ANIMDESTR(0x108), 0x2e, 0x82);
    AnimPlay(NE2_ANIMDESTR(0x109), 0x2e, 0x82);
    AnimPlay(NE2_ANIMDESTR(0x10a), 0x2f, 0x82);
    AnimPlay(NE2_ANIMDESTR(0xb9), 0x6c, 0x82);
    Object_SetDead(NE2_TURRET(0x18a));
    Object_SetDead(NE2_TURRET(0x18b));
    Object_SetProperty(NE2_TRIGGER(0x12c), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x12d), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x12e), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x12f), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x128), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x129), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x12a), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x12b), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x124), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x125), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x126), PROP_TARGET, NE2_POD(0x2b));
    Object_SetProperty(NE2_TRIGGER(0x127), PROP_TARGET, NE2_POD(0x2b));
    Sound_PlayLoop(NE2_TURRET(0x123), 0x90, -1);
    Sound_PlayLoop(NE2_TURRET(0x9e), 0x90, -1);
    Sound_PlayLoop(NE2_TURRET(0x9f), 0x90, -1);
    Sound_PlayLoop(NE2_TURRET(0x19d), 0x90, -1);
    Sound_PlayLoop(NE2_TURRET(0x19e), 0x90, -1);
    Sound_PlayLoop(NE2_TURRET(0x1c), 0x90, -1);
    Sound_PlayLoop(NE2_TURRET(0x1d), 0x90, -1);
    for (int32_t id = 0x56; id <= 0x64; id++)
        Sound_PlayLoop((ObjId)id, 0x90, -1);
    Object_SetDead(NE2_LIGHT(0x8c));                // emergency lights off
    Object_SetDead(NE2_LIGHT(0x8e));
    Object_SetDead(NE2_LIGHT(0x8f));
    Object_SetDead(NE2_LIGHT(0x90));
    Object_SetDead(NE2_LIGHT(0x91));
    Object_SetDead(NE2_LIGHT(0x92));
    Object_SetDead(NE2_LIGHT(0x93));
    Object_SetDead(NE2_LIGHT(0x94));
    Object_SetDead(NE2_LIGHT(0x95));
    Object_SetDead(NE2_LIGHT(0x8d));
    Object_SetDead(NE2_CHARGE_LIGHT);
    Object_SetProperty(NE2_FORCEBOX(0x13e), PROP_STATE, 0x4b);
    Object_SetProperty(NE2_FORCEBOX(0x99), PROP_STATE, 0x4b);
    Object_SetProperty(NE2_FORCEBOX(0x9a), PROP_STATE, 0x4b);
    Object_SetDead(NE2_DESTR(0x78));                // the AI defence's wreckage
    Object_SetDead(NE2_DESTR(0xde));
    Object_SetDead(NE2_DESTR(0x141));
    Object_SetDead(NE2_DESTR(0x140));
    Object_SetDead(NE2_DESTR(0xdd));
    Object_SetDead(NE2_DESTR(0x13f));
    Object_SetDead(NE2_DESTR(0xdc));
    MobObj_Wake(0x29);
    TexAnimPlay(0x59, 1, 100);
    TexAnimPlay(0x5a, 1, 100);
    TexAnimPlay(0x5c, 1, 100);
    TexAnimPlay(0x49, 1, 100);
    TexAnimPlay(0x5b, 1, 100);
    TexAnimPlay(0x1a, 1, 100);
    Object_SetProperty(NE2_BOSS, PROP_HITPOINTS, 0xfa);
    Object_SetProperty(NE2_BOSS, PROP_ARMOUR, 0xaa);
    Object_SetProperty(NE2_BOSS, PROP_EXPLOSION, 0xc8);
    MissionScore_Init(NE2_LANDER, 600000, 8);                  // 0x10001530
}

// 0x10005bf0  LevelStartLevel_Lander (export 0x10001010)
static void NE2_StartLander(void)
{
    MobObj_Wake(NE2_MOB_LANDER);
    MobObj_Wake(NE2_MOB_CAMERA);
    NE2_Setup();
    Object_SetProperty(NE2_CAMERA, PROP_TARGET, NE2_LANDER);
}

// 0x10005c30  LevelStartLevel_Flycam (export 0x10001020)
static void NE2_StartFlycam(void)
{
    NE2_Setup();
    MobObj_Wake(NE2_MOB_FLYCAM);
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void NE2_Stop(void)
{
}

// 0x10001040
static uint32_t NE2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001050, return 0. Port: fresh DLL data.
static int32_t NE2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.depthNext = ne2_depthInit[0];
    st.depthLast = ne2_depthInit[1];
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText ne2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000a160, .count = 3 * 6, .out = ne2_text },
    { 0 },
};

const mission_script_t mission_neptune_2 = {
    .name          = "NEPTUNE\\2",
    .get_version   = NE2_GetVersion,
    .load          = NE2_Load,
    .start_lander  = NE2_StartLander,
    .start_flycam  = NE2_StartFlycam,
    .start_animcam = NULL,
    .stop          = NE2_Stop,
    .text          = ne2_gametext,
};
