// MISSIONS\GANYMEDE\1\MISSIONR.DLL - faithful C port.
//
// Mission: find the installation at waypoint 1 and kill "Big Vern". Destroy the external comms rig
// (the lift generator 0xf00dc: the lift 0x300db drops), enter the complex, destroy the comms
// centre (0x8005c), the main control room (0x800fc, chain reaction 0x800fd..0x80104) and the
// computer core (0x80105, chain 0x80106..0x8010d; opens the way, turrets 0x57/0x58/0xe8/0xe9 wake).
// In Vern's reactor room (trigger 0x4003f) Vern talks; 10 s later the reactor core rises with his
// gun (AnimTurret mob 0xa) and the exit opens. Nine hits on the reactor 0x30056 damage it ("Vern:
// AAARGH"). Climbing above y = 10000 after that: success 2 if the reactor was damaged, else 1;
// +100000 cash either way. Par time 30 s, 8 pickups.
//
// Plenty of machinery on the way: pop-up guns that duck when hit (0xa4..0xa8), guard posts where a
// gun rises while both the lander and the patrol mob 0x1030010 are at the post, laser barriers
// (0x30072..77 loops; while a barrier object is inside its trigger the barrier switches 0xa000b /
// 0xa0010 reset), hatches with anim turrets (0xe00ce..d3) that open/close in a loop, a heat zone
// (-2 shield per 100 ms in triggers 0x40094..96), a door complex driven by a switch table, and
// camera tweaks in a tunnel (AutoCam lag / angle).
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10005190), LevelStartLevel_Flycam 0x10001020
// (-> 0x10005220), LevelStopLevel 0x10001030 (-> the empty 0x10001050), LevelGetVersion
// 0x10001040. The score library (0x10001060-0x10001604) is in mission_lib.c; the destroyed-sound
// handler 0x10003c60 is MissionLib_OnDestroyedSound.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define G1_LANDER        MOB_ID(OT_LANDER, 0x0f)         // 0x0101000f
#define G1_CAMERA        MOB_ID(OT_CAMERA, 7)            // 0x01060007
#define G1_PATROL        MOB_ID(OT_ANIMOBJECT, 0x10)     // 0x01030010 patrol with light mob 0x11
#define G1_PATROL_LIGHT  MOB_ID(OT_LIGHTOBJECT, 0x11)    // 0x01160011
#define G1_CORE_GUN      MOB_ID(OT_ANIMTURRET, 0x0a)     // 0x010e000a Vern's gun
#define G1_GUN_12        MOB_ID(OT_ANIMTURRET, 0x12)     // 0x010e0012
#define G1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define G1_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define G1_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define G1_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn
#define G1_ATURRET(n)    OBJ_ID(OT_ANIMTURRET, (n))      // 0x000e00nn
#define G1_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define G1_MOB_LANDER    0x0f
#define G1_MOB_CAMERA    7
#define G1_MOB_FLYCAM    0x0b

// ---- static data (.data) --------------------------------------------------------------------------
// 0x1000a160: [language][12] string pointers
static const char *g1_text[3][12];
static const int32_t  g1_nbWaypoints = 1;                // 0x1000a1f0
static const Waypoint g1_waypoints[1] = {                // 0x1000a1f8
    { -2304, 8704 },
};
// 0x1000a200: pop-up gun state, .data = 4 (all guns rise at the first second)
static const TurretType g1_turretType21 = {              // 0x1000a208 -> gTurretTypes[21]
    { 2, 29, 288, 0 }, 0x05, 1, 5, 5, 80, 0x30, 1024, 10000, 0
};
static const TurretType g1_turretType23 = {              // 0x1000a228 -> gTurretTypes[23]
    { 0, 0, 256, 0 }, 0x08, 1, 5, 2, 30, 0xb4, 1024, 10000, 0
};
static const TurretType g1_turretType24 = {              // 0x1000a248 -> gTurretTypes[24]
    { 0, 0, 256, 0 }, 0x01, 0, 2, 5, 80, 0x40, 1024, 10000, 0
};
static const TurretType g1_turretType22 = {              // 0x1000a268 -> gTurretTypes[22]
    { 2, 29, 288, 0 }, 0x09, 0, 3, 4, 30, 0x5a, 1024, 10000, 0
};
static const TurretType g1_turretType15 = {              // 0x1000a288 -> gTurretTypes[15]
    { 61, 0, 198, 0 }, 0x05, 2, 4, 5, 80, 0x20, 800, 10000, 0
};
static const TurretType g1_turretType13 = {              // 0x1000a2a8 -> gTurretTypes[13]
    { 61, 0, 198, 0 }, 0x05, 2, 4, 5, 80, 0x20, 1024, 10000, 0
};
static const TurretType g1_turretType17 = {              // 0x1000a2c8 -> gTurretTypes[17]
    { 61, 0, 198, 0 }, 0x00, 2, 4, 5, 80, 0x20, 1024, 10000, 0
};

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      popupHit;        // 0x1000a200 (.data = 4) 1..3 = which pop-up group was hit, 4 = all
    Window      *win;             // 0x1000cf4c objective line
    int32_t      scannerSeen;     // 0x1000cf50 trigger 0x400c3 entered
    int32_t      powerCut;        // 0x1000cf54 0..3: objective = entry 3 + powerCut
    int32_t      barrierOn[6];    // 0x1000cf58..cf6c barrier 0x3002a..f is closed (2 groups of 3)
    int32_t      barrierIn[6];    // 0x1000cf70..cf84 something is in trigger 0x40047/4e/4f/50/51/52
    int32_t      coreDown;        // 0x1000cf88 computer core destroyed
    int32_t      reactorDamaged;  // 0x1000cf8c
    int32_t      reactorTicks;    // 0x1000cf90
    int32_t      barrierActive;   // 0x1000cf94 written, never read
    int32_t      barrierTicks;    // 0x1000cf98
    int32_t      doorTicks;       // 0x1000cf9c
    int32_t      postLander[4];   // 0x1000cfa0..cfac lander at guard post 0x40045/48/4a/4c
    int32_t      postPatrol[4];   // 0x1000cfb0..cfbc patrol at guard post 0x40046/49/4b/4d
    int32_t      reactorHits;     // 0x1000cfc4
    int32_t      startShield;     // 0x1000d400 written, never read
    const char *const *text;      // 0x1000d404 = g1_text[gLanguage]
    int32_t      savedCamAngle;   // 0x1000d408
} st;

static void G1_OnPopupHit(const LanderEvent *ev);

// 0x10001610  EV_TICK_1S (once, after a hidden pop-up gun was hit): 3.5 s later the hit gun(s)
// rise again and are re-enabled; the hit watch is re-armed
static void G1_PopupReturnTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.popupHit == 1 || st.popupHit == 4) {
        InTime3(3500, AnimPlay, G1_ANIM(0xbe), 0x16, 0);
        InTime2(3500, Sound_PlayOnce, G1_ANIM(0xbe), 0xab);
        InTime3(3500, Object_SetProperty, OBJ_ID(OT_TURRET, 0xa4), PROP_STATE, 1);
        InTime3(3500, Object_SetProperty, OBJ_ID(OT_TURRET, 0xa5), PROP_STATE, 1);
    }
    if (st.popupHit == 2 || st.popupHit == 4) {
        InTime3(3500, AnimPlay, G1_ANIM(0xbf), 0x17, 0);
        InTime2(3500, Sound_PlayOnce, G1_ANIM(0xbf), 0xab);
        InTime3(3500, Object_SetProperty, OBJ_ID(OT_TURRET, 0xa8), PROP_STATE, 1);
    }
    if (st.popupHit == 3 || st.popupHit == 4) {
        InTime3(3500, AnimPlay, G1_ANIM(0xc0), 0x17, 0);
        InTime2(3500, Sound_PlayOnce, G1_ANIM(0xc0), 0xab);
        InTime3(3500, AnimPlay, G1_ANIM(0xc1), 0x17, 0);
        InTime2(3500, Sound_PlayOnce, G1_ANIM(0xc1), 0xab);
        InTime3(3500, Object_SetProperty, OBJ_ID(OT_TURRET, 0xa7), PROP_STATE, 1);
        InTime3(3500, Object_SetProperty, OBJ_ID(OT_TURRET, 0xa6), PROP_STATE, 1);
    }
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, G1_OnPopupHit);
    st.popupHit = 0;
    EventType_RemoveHandler(EV_TICK_1S, G1_PopupReturnTick);
}

// 0x100017b0  EV_PROJECTILE_HIT {target, ...}: a hit pop-up gun ducks into its housing
static void G1_OnPopupHit(const LanderEvent *ev)
{
    // the original masks the part bits off the target id in the event itself (later handlers of
    // this dispatch see the masked id)
    ObjId id = (ObjId)ev->arg[0] & 0x3ffffffu;
    ((LanderEvent *)ev)->arg[0] = (intptr_t)id;
    if (id == OBJ_ID(OT_TURRET, 0xa4) || id == OBJ_ID(OT_TURRET, 0xa5)) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa4), PROP_STATE, 0);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa5), PROP_STATE, 0);
        AnimPlay(G1_ANIM(0xbe), 0x16, 4);
        Sound_PlayOnce(G1_ANIM(0xbe), 0xab);
        st.popupHit = 1;
        EventType_AddHandler(EV_TICK_1S, NULL, G1_PopupReturnTick);
        EventType_RemoveHandler(EV_PROJECTILE_HIT, G1_OnPopupHit);
    }
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_TURRET, 0xa8)) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa8), PROP_STATE, 0);
        AnimPlay(G1_ANIM(0xbf), 0x17, 4);
        Sound_PlayOnce(G1_ANIM(0xbf), 0xab);
        st.popupHit = 2;
        EventType_AddHandler(EV_TICK_1S, NULL, G1_PopupReturnTick);
        EventType_RemoveHandler(EV_PROJECTILE_HIT, G1_OnPopupHit);
    }
    id = (ObjId)ev->arg[0];
    if (id == OBJ_ID(OT_TURRET, 0xa6) || id == OBJ_ID(OT_TURRET, 0xa7)) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa7), PROP_STATE, 0);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa6), PROP_STATE, 0);
        AnimPlay(G1_ANIM(0xc0), 0x17, 4);
        Sound_PlayOnce(G1_ANIM(0xc0), 0xab);
        AnimPlay(G1_ANIM(0xc1), 0x17, 4);
        Sound_PlayOnce(G1_ANIM(0xc1), 0xab);
        st.popupHit = 3;
        EventType_AddHandler(EV_TICK_1S, NULL, G1_PopupReturnTick);
        EventType_RemoveHandler(EV_PROJECTILE_HIT, G1_OnPopupHit);
    }
}

// 0x10001950  EV_TRIGGER_ENTER 0x40043 / 0x40044: guns 0xa9 / 0xaa rise
static void G1_OnTriggerGunsUp(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x43)) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa9), PROP_STATE, 1);
        AnimPlay(G1_ANIM(0xb7), 7, 0);
        Sound_PlayOnce(G1_ANIM(0xb7), 0xab);
    }
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x44)) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xaa), PROP_STATE, 1);
        AnimPlay(G1_ANIM(0xb8), 7, 0);
        Sound_PlayOnce(G1_ANIM(0xb8), 0xab);
    }
}

// 0x100019e0  EV_TRIGGER_LEAVE 0x40043 / 0x40044: guns 0xa9 / 0xaa go down
static void G1_OnTriggerGunsDown(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x43)) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa9), PROP_STATE, 0);
        AnimPlay(G1_ANIM(0xb7), 7, 4);
        Sound_PlayOnce(G1_ANIM(0xb7), 0xab);
    }
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x44)) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xaa), PROP_STATE, 0);
        AnimPlay(G1_ANIM(0xb8), 7, 4);
        Sound_PlayOnce(G1_ANIM(0xb8), 0xab);
    }
}

static void G1_DoorTimerTick(const LanderEvent *ev);

// 0x10001a70  EV_SWITCH 0xa0011: door 0x30079 opens for 20 s
static void G1_OnSwitchDoor79(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_SWITCH(0x11))
        return;
    EventType_AddHandler(EV_TICK_1S, NULL, G1_DoorTimerTick);
    AnimPlay(G1_ANIM(0x79), 0xf, 0);
    Sound_PlayOnce(G1_ANIM(0x79), 0x3f7);
    EventType_RemoveHandler(EV_SWITCH, G1_OnSwitchDoor79);
    st.doorTicks = 0;
}

// 0x10001ad0  EV_TICK_1S: after 20 s the door closes and the switch is reset
static void G1_DoorTimerTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.doorTicks <= 20)
        return;
    EventType_RemoveHandler(EV_TICK_1S, G1_DoorTimerTick);
    AnimPlay(G1_ANIM(0x79), 0xf, 4);
    Sound_PlayOnce(G1_ANIM(0x79), 0x3f7);
    Object_SetProperty(G1_SWITCH(0x11), PROP_STATE, 0);
    EventType_AddHandler(EV_SWITCH, NULL, G1_OnSwitchDoor79);
}

// 0x10001b40  a pad gun goes down (turret id 0xab..0xae, its housing is id + 0x3000e)
static void G1_PadGunOff(intptr_t id)
{
    Object_SetProperty((ObjId)id, PROP_STATE, 0);
    AnimPlay((ObjId)(id + 0x3000e), 7, 4);
}

// 0x10001b70  a pad gun rises
static void G1_PadGunOn(intptr_t id)
{
    Object_SetProperty((ObjId)id, PROP_STATE, 1);
    AnimPlay((ObjId)(id + 0x3000e), 7, 0);
    Sound_PlayOnce((ObjId)(id + 0x3000e), 0xda);
}

// 0x10001bb0  EV_TRIGGER_ENTER: four guard posts, each a trigger pair (one for the lander, one for
// the patrol mob 0x1030010): a gun rises while both are inside
static void G1_OnTriggerPostEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == G1_LANDER) {
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x45))
            st.postLander[0] = 1;
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x48))
            st.postLander[1] = 1;
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4a))
            st.postLander[2] = 1;
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4c))
            st.postLander[3] = 1;
    } else {
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x46))
            st.postPatrol[0] = 1;
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x49))
            st.postPatrol[1] = 1;
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4b))
            st.postPatrol[2] = 1;
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4d))
            st.postPatrol[3] = 1;
    }
    if (st.postLander[0] == 1 && st.postPatrol[0] == 1)
        G1_PadGunOn(OBJ_ID(OT_TURRET, 0xae));
    if (st.postLander[1] == 1 && st.postPatrol[1] == 1)
        G1_PadGunOn(OBJ_ID(OT_TURRET, 0xab));
    if (st.postLander[2] == 1 && st.postPatrol[2] == 1)
        G1_PadGunOn(OBJ_ID(OT_TURRET, 0xac));
    if (st.postLander[3] == 1 && st.postPatrol[3] == 1)
        G1_PadGunOn(OBJ_ID(OT_TURRET, 0xad));
}

// 0x10001cc0  EV_TRIGGER_LEAVE (lander): its gun goes down
static void G1_OnTriggerPostLeaveLander(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_LANDER)
        return;
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x48)) {
        st.postLander[1] = 0;
        G1_PadGunOff(OBJ_ID(OT_TURRET, 0xab));
    }
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4a)) {
        st.postLander[2] = 0;
        G1_PadGunOff(OBJ_ID(OT_TURRET, 0xac));
    }
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4c)) {
        st.postLander[3] = 0;
        G1_PadGunOff(OBJ_ID(OT_TURRET, 0xad));
    }
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x45)) {
        st.postLander[0] = 0;
        G1_PadGunOff(OBJ_ID(OT_TURRET, 0xae));
    }
}

// 0x10001d50  EV_TRIGGER_LEAVE (patrol mob 0x1030010)
static void G1_OnTriggerPostLeavePatrol(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_PATROL)
        return;
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x49))
        st.postPatrol[1] = 0;
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4b))
        st.postPatrol[2] = 0;
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x4d))
        st.postPatrol[3] = 0;
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0x46))
        st.postPatrol[0] = 0;
}

// 0x10001da0 / 0x10001dc0  InTime: objective lines 9 and 10
static void G1_Say9(void)
{
    WindowSetText(st.win, st.text[9]);
}

static void G1_Say10(void)
{
    WindowSetText(st.win, st.text[10]);
}

static void G1_ReactorTick(const LanderEvent *ev);
static void G1_OnReactorHit(const LanderEvent *ev);

// 0x10001de0  EV_TRIGGER_ENTER 0x4003f: the reactor room
static void G1_OnTriggerReactorRoom(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0x3f))
        return;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, G1_OnTriggerReactorRoom);
    EventType_AddHandler(EV_TICK_1S, NULL, G1_ReactorTick);
    AnimSetUpdateInterval(G1_ANIM(0x56), 0x96);
    AnimSetUpdateInterval(G1_ANIM(0x9b), 0xdc);
    AnimSetUpdateInterval(G1_ANIM(0xe0), 0xfa);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, G1_OnReactorHit);
    InTime0(2000, G1_Say9);
    InTime0(4000, G1_Say10);
}

// 0x10001e90  EV_PROJECTILE_HIT: the 9th hit on reactor 0x30056 damages it
static void G1_OnReactorHit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_ANIM(0x56))
        return;
    if (++st.reactorHits <= 8)
        return;
    EventType_RemoveHandler(EV_PROJECTILE_HIT, G1_OnReactorHit);
    Object_AdvanceDestroyLevel(G1_ANIM(0x56), 1);
    MobObj_Wake(0x13);
    WindowSetText(st.win, st.text[8]);
    st.reactorDamaged = 1;
}

static void G1_OnReactorStep1(const LanderEvent *ev);
static void G1_OnReactorStep2(const LanderEvent *ev);
static void G1_OnReactorStep3(const LanderEvent *ev);
static void G1_EscapeTick(const LanderEvent *ev);

// 0x10001f00  EV_TICK_1S in the reactor room: after 10 s the reactor sequence starts
static void G1_ReactorTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.reactorTicks != 10)
        return;
    EventType_RemoveHandler(EV_TICK_1S, G1_ReactorTick);
    AnimPlay(G1_ANIM(0x56), 0x1b, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_OnReactorStep1);
}

// 0x10001f50  EV_ANIM_FINISHED 0x30056
static void G1_OnReactorStep1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_ANIM(0x56))
        return;
    AnimPlay(G1_ANIM(0x9b), 0x1a, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_OnReactorStep2);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_OnReactorStep1);
    AnimStop(G1_ANIM(0x56), 0);
}

// 0x10001fb0  EV_ANIM_FINISHED 0x3009b
static void G1_OnReactorStep2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_ANIM(0x9b))
        return;
    AnimPlay(G1_ANIM(0xe0), 0x1c, 4);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_OnReactorStep3);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_OnReactorStep2);
}

// 0x10002000  EV_ANIM_FINISHED 0x300e0: the reactor core rises with its gun (mob 0x10e000a);
// the exit doors open and the escape check starts
static void G1_OnReactorStep3(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_ANIM(0xe0))
        return;
    AnimSetUpdateInterval(G1_ANIM(0xdd), 0x32);
    AnimSetUpdateInterval(G1_ANIM(0xe0), 0x32);
    AnimSetUpdateInterval(G1_ANIM(0x56), 0x32);
    AnimSetUpdateInterval(G1_CORE_GUN, 0x32);
    AnimStop(G1_ANIM(0xe0), 0);
    AnimPlay(G1_ANIM(0xdd), 0x1d, 0);
    AnimPlay(G1_ANIM(0xe0), 0x1d, 0);
    AnimPlay(G1_ANIM(0x56), 0x1d, 0);
    AnimPlay(G1_CORE_GUN, 0x1d, 0xe0);
    MobObj_Wake(5);
    Sound_StopObject(G1_CORE_GUN);
    InTime2(500, Sound_PlayOnce, G1_ANIM(0xdd), 0x2aa);
    Sound_PlayOnce(G1_ANIM(0xdd), 0x8c);
    AnimPlay(G1_ANIM(0x83), 0xe, 0);
    Sound_PlayOnce(G1_ANIM(0x83), 0x3f7);
    AnimPlay(G1_ANIM(0x82), 0xd, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_OnReactorStep3);
    WindowSetText(st.win, st.text[11]);
    EventType_AddHandler(EV_TICK_1S, NULL, G1_EscapeTick);
}

// 0x10002150  EV_DESTROYED (anything): repeat objective 11 once the reactor is damaged
static void G1_OnDestroyedSayEscape(const LanderEvent *ev)
{
    (void)ev;
    if (st.reactorDamaged == 1)
        WindowSetText(st.win, st.text[11]);
}

static void G1_OnTriggerLift(const LanderEvent *ev);

// 0x10002180  EV_DESTROYED 0xf00dc: the lift's generator is gone -> generator 0x8005d explodes too,
// the lift 0x300db drops and stops working
static void G1_OnLiftGeneratorDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xdc))
        return;
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x5d), PROP_HITPOINTS, 0);
    AnimPlay(G1_ANIM(0xdb), 0, 4);
    Sound_PlayChain(G1_ANIM(0xdb), 6, 0xce, 0xcf, 0xcf, 0xcf, 0xcf, 0xd0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, G1_OnTriggerLift);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, G1_OnTriggerLift);
    WindowSetText(st.win, st.text[2]);
}

// 0x10002220  EV_DESTROYED 0x8005c: first power line cut -> door 0x30085 opens, sparks
static void G1_OnPower1Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_DESTRUCTIBLE, 0x5c))
        return;
    AnimPlay(G1_ANIM(0x85), 2, 0);
    InTime1(500, Object_SetAlive, G1_EMITTER(1));
    InTime1(500, Object_SetAlive, G1_EMITTER(2));
    InTime1(500, Object_SetAlive, G1_EMITTER(3));
    InTime1(500, Object_SetAlive, G1_EMITTER(7));
    InTime1(500, Object_SetAlive, G1_EMITTER(8));
    InTime1(500, Object_SetAlive, G1_EMITTER(4));
    InTime1(500, Object_SetAlive, G1_EMITTER(5));
    InTime1(500, Object_SetAlive, G1_EMITTER(6));
    InTime3(500, Sound_PlayLoop, G1_EMITTER(1), 0xf1, -1);
    InTime3(500, Sound_PlayLoop, G1_EMITTER(2), 0xf1, -1);
    InTime3(500, Sound_PlayLoop, G1_EMITTER(7), 0x106, -1);
    InTime3(500, Sound_PlayLoop, G1_EMITTER(8), 0x106, -1);
    WindowSetText(st.win, st.text[4]);
    st.powerCut = 1;
}

// 0x10002380  EV_SWITCH 0xa0000: door 0x30078 and guns 0xa2 / 0xa3 follow the switch
static void G1_OnSwitchDoor78(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_SWITCH(0x00))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(G1_ANIM(0x78), 1, 0);
        Sound_PlayOnce(G1_ANIM(0x78), 0x3f7);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa2), PROP_STATE, 1);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa3), PROP_STATE, 1);
        return;
    }
    AnimPlay(G1_ANIM(0x78), 1, 4);
    Sound_PlayOnce(G1_ANIM(0x78), 0x3f7);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa2), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa3), PROP_STATE, 0);
}

// 0x10002430  EV_SWITCH 0xa0024: door 0x30086 follows the switch
static void G1_OnSwitchDoor86(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_SWITCH(0x24))
        return;
    AnimPlay(G1_ANIM(0x86), 1, ev->arg[1] == 1 ? 0 : 4);
    Sound_PlayOnce(G1_ANIM(0x86), 0x3f7);
}

// 0x10002470  EV_TRIGGER_ENTER 0x40034 (after switch 0xa0027 opened it): door 0x30090/0x3008f
// closes behind the lander
static void G1_OnTriggerDoor8F(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0x34))
        return;
    AnimPlay(G1_ANIM(0x90), 0xe, 4);
    AnimPlay(G1_ANIM(0x8f), 0xd, 4);
    Sound_PlayOnce(G1_ANIM(0x8f), 0x3f7);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, G1_OnTriggerDoor8F);
}

// 0x100024d0  EV_SWITCH 0xa0027 on: door 0x30090/0x3008f opens
static void G1_OnSwitchDoor8F(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_SWITCH(0x27) || ev->arg[1] != 1)
        return;
    AnimPlay(G1_ANIM(0x90), 0xe, 0);
    AnimPlay(G1_ANIM(0x8f), 0xd, 0);
    Sound_PlayOnce(G1_ANIM(0x8f), 0x3f7);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerDoor8F);
}

// 0x10002530  EV_DESTROYED 0x800fc: second power line -> the chain 0x800fd..0x80104 blows up,
// door 0x30084 opens, more sparks
static void G1_OnPower2Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_DESTRUCTIBLE, 0xfc))
        return;
    AnimPlay(G1_ANIM(0x84), 3, 0);
    Sound_PlayOnce(G1_ANIM(0x84), 0x3f7);
    InTime3(1000, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0xfd), PROP_HITPOINTS, 0);
    InTime3(1100, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0xfe), PROP_HITPOINTS, 0);
    InTime3(1100, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0xff), PROP_HITPOINTS, 0);
    InTime3(1400, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x100), PROP_HITPOINTS, 0);
    InTime3(1600, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x101), PROP_HITPOINTS, 0);
    InTime3(1800, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x102), PROP_HITPOINTS, 0);
    InTime3(1800, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x103), PROP_HITPOINTS, 0);
    InTime3(1900, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x104), PROP_HITPOINTS, 0);
    MobObj_Wake(8);
    InTime1(1900, Object_SetAlive, G1_EMITTER(0x68));
    InTime1(1600, Object_SetAlive, G1_EMITTER(0x69));
    InTime1(1800, Object_SetAlive, G1_EMITTER(0x6a));
    InTime1(100, Object_SetAlive, G1_EMITTER(0x6b));
    InTime1(1100, Object_SetAlive, G1_EMITTER(0x6c));
    InTime1(1800, Object_SetAlive, G1_EMITTER(0x70));
    InTime1(1600, Object_SetAlive, G1_EMITTER(0x71));
    InTime1(1900, Object_SetAlive, G1_EMITTER(0x6d));
    InTime1(1100, Object_SetAlive, G1_EMITTER(0x6e));
    InTime1(100, Object_SetAlive, G1_EMITTER(0x6f));
    InTime3(1900, Sound_PlayLoop, G1_EMITTER(0x68), 0xf1, -1);
    InTime3(1600, Sound_PlayLoop, G1_EMITTER(0x69), 0xf1, -1);
    InTime3(1800, Sound_PlayLoop, G1_EMITTER(0x6a), 0xf1, -1);
    InTime3(100, Sound_PlayLoop, G1_EMITTER(0x6b), 0xf1, -1);
    InTime3(1800, Sound_PlayLoop, G1_EMITTER(0x70), 0x106, -1);
    InTime3(1600, Sound_PlayLoop, G1_EMITTER(0x71), 0x106, -1);
    WindowSetText(st.win, st.text[5]);
    st.powerCut = 2;
}

// 0x100027b0  EV_DESTROYED 0x80105: the computer core -> chain 0x80106..0x8010d, the guns of the
// last section wake, the force-field generators 0x80091..93 go
static void G1_OnCoreDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_DESTRUCTIBLE, 0x105))
        return;
    MobObj_Wake(9);
    st.coreDown = 1;
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x57), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x58), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xe8), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xe9), PROP_STATE, 1);
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x91));
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x92));
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x93));
    Sound_StopObject(OBJ_ID(OT_DESTRUCTIBLE, 0x91));
    Sound_StopObject(OBJ_ID(OT_DESTRUCTIBLE, 0x92));
    Sound_StopObject(OBJ_ID(OT_DESTRUCTIBLE, 0x93));
    Object_SetAlive(OBJ_ID(OT_TURRET, 0xe8));
    Object_SetAlive(OBJ_ID(OT_TURRET, 0xe9));
    InTime3(1000, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x106), PROP_HITPOINTS, 0);
    InTime3(1200, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x107), PROP_HITPOINTS, 0);
    InTime3(1300, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x108), PROP_HITPOINTS, 0);
    InTime3(1300, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x109), PROP_HITPOINTS, 0);
    InTime3(1500, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x10a), PROP_HITPOINTS, 0);
    InTime3(1700, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x10b), PROP_HITPOINTS, 0);
    InTime3(1700, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x10c), PROP_HITPOINTS, 0);
    InTime3(1900, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x10d), PROP_HITPOINTS, 0);
    InTime1(1000, Object_SetAlive, G1_EMITTER(0x5e));
    InTime1(1900, Object_SetAlive, G1_EMITTER(0x5f));
    InTime1(1500, Object_SetAlive, G1_EMITTER(0x60));
    InTime1(1700, Object_SetAlive, G1_EMITTER(0x65));
    InTime1(1200, Object_SetAlive, G1_EMITTER(0x66));
    InTime1(1700, Object_SetAlive, G1_EMITTER(0x67));
    InTime1(1000, Object_SetAlive, G1_EMITTER(0x61));
    InTime1(1900, Object_SetAlive, G1_EMITTER(0x62));
    InTime1(1300, Object_SetAlive, G1_EMITTER(0x63));
    InTime1(1500, Object_SetAlive, G1_EMITTER(0x64));
    InTime3(1000, Sound_PlayLoop, G1_EMITTER(0x5e), 0xf1, -1);
    InTime3(1900, Sound_PlayLoop, G1_EMITTER(0x5f), 0xf1, -1);
    InTime3(1500, Sound_PlayLoop, G1_EMITTER(0x60), 0xf1, -1);
    InTime3(1700, Sound_PlayLoop, G1_EMITTER(0x65), 0x107, -1);
    InTime3(1200, Sound_PlayLoop, G1_EMITTER(0x66), 0x107, -1);
    InTime3(1700, Sound_PlayLoop, G1_EMITTER(0x67), 0x107, -1);
    WindowSetText(st.win, st.text[6]);
    st.powerCut = 3;
}

// 0x10002aa0  open (trigger entered) or close an animated door
static void G1_DoorAnim(int32_t type, ObjId obj, int32_t anim)
{
    if (type == EV_TRIGGER_ENTER) {
        AnimPlay(obj, anim, 0);
        return;
    }
    AnimPlay(obj, anim, 4);
}

// 0x10002ae0 .. 0x10002c20  EV_ANIM_FINISHED (any object, once): the second half of a two-stage
// door (added by G1_OnTriggerDoors)
static void G1_Door7COpen(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(G1_ANIM(0x7c), 0x11, 0);
    Sound_PlayOnce(G1_ANIM(0x7c), 0x3f7);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_Door7COpen);
}

static void G1_Door7CClose(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(G1_ANIM(0x7c), 0x11, 4);
    Sound_PlayOnce(G1_ANIM(0x7c), 0x3f7);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_Door7CClose);
}

static void G1_Door7EOpen(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(G1_ANIM(0x7e), 0x13, 0);
    Sound_PlayOnce(G1_ANIM(0x7e), 0x3f7);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_Door7EOpen);
}

static void G1_Door7EClose(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(G1_ANIM(0x7e), 0x13, 4);
    Sound_PlayOnce(G1_ANIM(0x7e), 0x3f7);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_Door7EClose);
}

static void G1_Door81Open(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(G1_ANIM(0x81), 0x15, 0);
    Sound_PlayOnce(G1_ANIM(0x81), 0x3f7);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_Door81Open);
}

static void G1_Door81Close(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(G1_ANIM(0x81), 0x15, 4);
    Sound_PlayOnce(G1_ANIM(0x81), 0x3f7);
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_Door81Close);
}

// 0x10002c60  EV_TRIGGER_ENTER / LEAVE 0x40031..0x40053: the proximity doors. The original is a
// switch (byte table 0x10002fb8 -> jump table 0x10002f84) whose cases enter one long code block
// at different points; the C cases fall through the same way.
static void G1_OnTriggerDoors(const LanderEvent *ev)
{
    int32_t t = ev->type;
    switch ((ObjId)ev->arg[1]) {
    case G1_TRIGGER(0x53):
        G1_DoorAnim(t, G1_ANIM(0x87), 8);
        G1_DoorAnim(t, G1_ANIM(0x88), 9);
        Sound_PlayOnce(G1_ANIM(0x87), 0x3f7);
        // fall through
    case G1_TRIGGER(0x31):
        G1_DoorAnim(t, G1_ANIM(0x8b), 0xa);
        Sound_PlayOnce(G1_ANIM(0x8b), 0x3f7);
        break;
    case G1_TRIGGER(0x32):
        G1_DoorAnim(t, G1_ANIM(0x8c), 0xa);
        Sound_PlayOnce(G1_ANIM(0x8c), 0x3f7);
        break;
    case G1_TRIGGER(0x33):
        G1_DoorAnim(t, G1_ANIM(0x8d), 0xb);
        G1_DoorAnim(t, G1_ANIM(0x8e), 0xc);
        Sound_PlayOnce(G1_ANIM(0x8d), 0x3f7);
        break;
    case G1_TRIGGER(0x36):
        G1_DoorAnim(t, G1_ANIM(0xcc), 5);
        Sound_PlayOnce(G1_ANIM(0xcc), 0x3f9);
        // fall through
    case G1_TRIGGER(0x37):
        G1_DoorAnim(t, G1_ANIM(0x7a), 0x10);
        Sound_PlayOnce(G1_ANIM(0x7a), 0x3f7);
        break;
    case G1_TRIGGER(0x38):
        if (t == EV_TRIGGER_ENTER) {
            AnimPlay(G1_ANIM(0x7b), 0x11, 0);
            Sound_PlayOnce(G1_ANIM(0x7b), 0x3f7);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_Door7COpen);
        }
        if (t == EV_TRIGGER_LEAVE) {
            AnimPlay(G1_ANIM(0x7b), 0x11, 4);
            Sound_PlayOnce(G1_ANIM(0x7b), 0x3f7);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_Door7CClose);
        }
        // fall through
    case G1_TRIGGER(0x35):
        G1_DoorAnim(t, G1_ANIM(0x89), 8);
        Sound_PlayOnce(G1_ANIM(0x89), 0x3f7);
        G1_DoorAnim(t, G1_ANIM(0x8a), 9);
        // fall through
    case G1_TRIGGER(0x39):
        if (t == EV_TRIGGER_ENTER) {
            AnimPlay(G1_ANIM(0x7d), 0x12, 4);              // (sic: 4 on entering)
            Sound_PlayOnce(G1_ANIM(0x7d), 0x3f7);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_Door7EOpen);
        }
        if (t == EV_TRIGGER_LEAVE) {
            AnimPlay(G1_ANIM(0x7d), 0x12, 0);
            Sound_PlayOnce(G1_ANIM(0x7d), 0x3f7);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_Door7EClose);
        }
        // fall through
    case G1_TRIGGER(0x3a):
        G1_DoorAnim(t, G1_ANIM(0x7f), 0x14);
        Sound_PlayOnce(G1_ANIM(0x7f), 0x3f7);
        // fall through
    case G1_TRIGGER(0x3c):
        if (t == EV_TRIGGER_ENTER) {
            AnimSetUpdateInterval(G1_ANIM(0x80), 0x14);
            AnimSetUpdateInterval(G1_ANIM(0x81), 0x14);
            AnimPlay(G1_ANIM(0x80), 0x15, 0);
            Sound_PlayOnce(G1_ANIM(0x80), 0x3f7);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_Door81Open);
        }
        if (t == EV_TRIGGER_LEAVE) {
            AnimPlay(G1_ANIM(0x80), 0x15, 4);
            Sound_PlayOnce(G1_ANIM(0x80), 0x3f7);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_Door81Close);
        }
        // fall through
    case G1_TRIGGER(0x40):
        if (st.coreDown == 1) {
            G1_DoorAnim(t, G1_ANIM(0xcb), 5);
            Sound_PlayOnce(G1_ANIM(0xcb), 0x3f9);
        }
        break;
    default:
        break;
    }
}

// 0x10002fe0  EV_TRIGGER_ENTER / LEAVE 0x4003b: hatch 0x300ca
static void G1_OnTriggerHatchCA(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0x3b))
        return;
    AnimPlay(G1_ANIM(0xca), 5, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
    Sound_PlayOnce(G1_ANIM(0xca), 0x3f9);
}

// 0x10003020  EV_COLLISION {body, other, impulse}: bumping a hatch cover wakes its two guns
static void G1_OnHatchBumped(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == G1_ANIM(0xd4)) {
        Object_SetProperty(G1_ATURRET(0xd0), PROP_STATE, 1);
        Object_SetProperty(G1_ATURRET(0xd1), PROP_STATE, 1);
        Sound_PlayOnce(G1_ANIM(0xd4), 0xfa);
    }
    if ((ObjId)ev->arg[1] == G1_ANIM(0xcd)) {
        Object_SetProperty(G1_ATURRET(0xce), PROP_STATE, 1);
        Object_SetProperty(G1_ATURRET(0xcf), PROP_STATE, 1);
        Sound_PlayOnce(G1_ANIM(0xcd), 0xfa);
    }
    if ((ObjId)ev->arg[1] == G1_ANIM(0xd5)) {
        Object_SetProperty(G1_ATURRET(0xd2), PROP_STATE, 1);
        Object_SetProperty(G1_ATURRET(0xd3), PROP_STATE, 1);
        Sound_PlayOnce(G1_ANIM(0xd5), 0xfa);
    }
}

// 0x100030e0  the three hatches open (covers 0x300d4 / 0x300cd / 0x300d5 appear, guns rise, off)
static void G1_HatchesOpen(void)
{
    Object_SetAlive(G1_ANIM(0xd4));
    InTime3(500, AnimPlay, G1_ANIM(0xd7), 0x18, 0);
    InTime3(500, AnimPlay, G1_ANIM(0xd4), 0x18, 0);
    InTime3(500, AnimPlay, G1_ATURRET(0xd0), 0x1f, 0xe0);
    InTime3(500, AnimPlay, G1_ATURRET(0xd1), 0x18, 0xe0);
    AnimSetUpdateInterval(G1_ANIM(0xd4), 0x19);
    AnimSetUpdateInterval(G1_ANIM(0xd7), 0x19);
    AnimSetUpdateInterval(G1_ATURRET(0xd0), 0x19);
    AnimSetUpdateInterval(G1_ATURRET(0xd1), 0x19);
    Object_SetAlive(G1_ANIM(0xcd));
    InTime3(500, AnimPlay, G1_ANIM(0xd6), 0x18, 0);
    InTime3(500, AnimPlay, G1_ANIM(0xcd), 0x18, 0);
    InTime3(500, AnimPlay, G1_ATURRET(0xce), 0x18, 0xe0);
    InTime3(500, AnimPlay, G1_ATURRET(0xcf), 0x1f, 0xe0);
    AnimSetUpdateInterval(G1_ANIM(0xcd), 0x19);
    AnimSetUpdateInterval(G1_ANIM(0xd6), 0x19);
    AnimSetUpdateInterval(G1_ATURRET(0xce), 0x19);
    AnimSetUpdateInterval(G1_ATURRET(0xcf), 0x19);
    Object_SetAlive(G1_ANIM(0xd5));
    InTime3(500, AnimPlay, G1_ANIM(0xd8), 0x18, 0);
    InTime3(500, AnimPlay, G1_ANIM(0xd5), 0x18, 0);
    InTime3(500, AnimPlay, G1_ATURRET(0xd2), 0x18, 0xe0);
    InTime3(500, AnimPlay, G1_ATURRET(0xd3), 0x1f, 0xe0);
    AnimSetUpdateInterval(G1_ANIM(0xd5), 0x19);
    AnimSetUpdateInterval(G1_ANIM(0xd8), 0x19);
    AnimSetUpdateInterval(G1_ATURRET(0xd2), 0x19);
    AnimSetUpdateInterval(G1_ATURRET(0xd3), 0x19);
    for (int32_t n = 0xce; n <= 0xd3; n++)               // (unrolled)
        Object_SetProperty(G1_ATURRET(n), PROP_STATE, 0);
}

// 0x10003330  the three hatches close
static void G1_HatchesClose(void)
{
    Object_SetDead(G1_ANIM(0xd4));
    InTime3(500, AnimPlay, G1_ANIM(0xd7), 0x18, 4);
    InTime3(500, AnimPlay, G1_ANIM(0xd4), 0x18, 4);
    InTime3(500, AnimPlay, G1_ATURRET(0xd0), 0x1f, 0xe4);
    InTime3(500, AnimPlay, G1_ATURRET(0xd1), 0x18, 0xe4);
    AnimSetUpdateInterval(G1_ANIM(0xd4), 0xa);
    AnimSetUpdateInterval(G1_ANIM(0xd7), 0xa);
    AnimSetUpdateInterval(G1_ATURRET(0xd0), 0xa);
    AnimSetUpdateInterval(G1_ATURRET(0xd1), 0xa);
    Object_SetDead(G1_ANIM(0xcd));
    InTime3(500, AnimPlay, G1_ANIM(0xd6), 0x18, 4);
    InTime3(500, AnimPlay, G1_ANIM(0xcd), 0x18, 4);
    InTime3(500, AnimPlay, G1_ATURRET(0xce), 0x18, 0xe4);
    InTime3(500, AnimPlay, G1_ATURRET(0xcf), 0x1f, 0xe4);
    AnimSetUpdateInterval(G1_ANIM(0xcd), 0xa);
    AnimSetUpdateInterval(G1_ANIM(0xd6), 0xa);
    AnimSetUpdateInterval(G1_ATURRET(0xce), 0xa);
    AnimSetUpdateInterval(G1_ATURRET(0xcf), 0xa);
    Object_SetDead(G1_ANIM(0xd5));
    InTime3(500, AnimPlay, G1_ANIM(0xd8), 0x18, 4);
    InTime3(500, AnimPlay, G1_ANIM(0xd5), 0x18, 4);
    InTime3(500, AnimPlay, G1_ATURRET(0xd2), 0x18, 0xe4);
    InTime3(500, AnimPlay, G1_ATURRET(0xd3), 0x1f, 0xe4);
    AnimSetUpdateInterval(G1_ANIM(0xd5), 0xa);
    AnimSetUpdateInterval(G1_ANIM(0xd8), 0xa);
    AnimSetUpdateInterval(G1_ATURRET(0xd2), 0xa);
    AnimSetUpdateInterval(G1_ATURRET(0xd3), 0xa);
}

static void G1_OnHatchesClosed(const LanderEvent *ev);

// 0x10003520  EV_ANIM_FINISHED 0x300d7 anim 0x18 (closed): open again
static void G1_OnHatchesOpened(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_ANIM(0xd7) || ev->arg[1] != 0x18)
        return;
    G1_HatchesOpen();
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_OnHatchesOpened);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_OnHatchesClosed);
}

// 0x10003560  EV_ANIM_FINISHED 0x300d7 anim 0x18 (open): close again
static void G1_OnHatchesClosed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_ANIM(0xd7) || ev->arg[1] != 0x18)
        return;
    G1_HatchesClose();
    EventType_RemoveHandler(EV_ANIM_FINISHED, G1_OnHatchesClosed);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_OnHatchesOpened);
}

// 0x100035a0  EV_TRIGGER_ENTER 0x4003b (first time): the hatch room starts
static void G1_OnTriggerHatchRoom(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0x3b))
        return;
    AnimPlay(G1_ANIM(0xca), 5, 0);
    Sound_PlayOnce(G1_ANIM(0xca), 0x3f9);
    G1_HatchesOpen();
    EventType_AddHandler(EV_COLLISION, NULL, G1_OnHatchBumped);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, G1_OnHatchesClosed);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, G1_OnTriggerHatchRoom);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerHatchCA);   // (a second node: setup has one)
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerHatchCA);
}

// 0x10003630  EV_TRIGGER_ENTER 0x400c4..0x400c9: gun 0xaf rises
static void G1_OnTriggerGunAF(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig != (int32_t)G1_TRIGGER(0xc4) && trig != (int32_t)G1_TRIGGER(0xc5) && trig != (int32_t)G1_TRIGGER(0xc6) &&
        trig != (int32_t)G1_TRIGGER(0xc7) && trig != (int32_t)G1_TRIGGER(0xc8) && trig != (int32_t)G1_TRIGGER(0xc9))
        return;
    AnimPlay(G1_ANIM(0xbd), 7, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xaf), PROP_STATE, 1);
    Sound_PlayOnce(G1_ANIM(0xbd), 0xda);
}

// 0x100036a0  EV_TRIGGER_ENTER 0x40055: guns 0xe00ec / 0xe00ed rise
static void G1_OnTriggerGunsEC(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0x55))
        return;
    AnimPlay(G1_ATURRET(0xec), 0x1e, 0x60);
    AnimPlay(G1_ATURRET(0xed), 0x1e, 0x60);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, G1_OnTriggerGunsEC);
}

// 0x100036f0  EV_TRIGGER_ENTER 0x40054: gun mob 0x10e0012 rises
static void G1_OnTriggerGun12(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0x54))
        return;
    AnimPlay(G1_GUN_12, 0x1e, 0x60);
    Sound_PlayLoop(G1_GUN_12, 0x91, -1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, G1_OnTriggerGun12);
}

// 0x10003740  EV_TICK_100MS in the heat zone: -2 shield
static void G1_HeatTick(const LanderEvent *ev)
{
    (void)ev;
    Object_SetProperty(G1_LANDER, PROP_HITPOINTS, Object_GetProperty(G1_LANDER, PROP_HITPOINTS) - 2);
}

// 0x10003770  EV_TRIGGER_ENTER / LEAVE 0x40094..0x40096: the heat zone
static void G1_OnTriggerHeat(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig != (int32_t)G1_TRIGGER(0x94) && trig != (int32_t)G1_TRIGGER(0x95) && trig != (int32_t)G1_TRIGGER(0x96))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerHeat);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, G1_OnTriggerHeat);
        EventType_AddHandler(EV_TICK_100MS, NULL, G1_HeatTick);
    }
    if (ev->type == EV_TRIGGER_LEAVE) {
        EventType_RemoveHandler(EV_TICK_100MS, G1_HeatTick);
        EventType_RemoveHandler(EV_TRIGGER_LEAVE, G1_OnTriggerHeat);
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerHeat);
    }
}

// 0x10003800  EV_TICK_1S after the reactor sequence: above 10000 -> success (2 = Vern dead)
static void G1_EscapeTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(G1_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    if (st.reactorDamaged == 0) {
        Lander_SetCash(G1_LANDER, Lander_GetCash(G1_LANDER) + 100000);
        MissionScore_End(1);                             // 0x100013b0
    } else {
        Lander_SetCash(G1_LANDER, Lander_GetCash(G1_LANDER) + 100000);
        MissionScore_End(2);
    }
    EventType_RemoveHandler(EV_TICK_1S, G1_EscapeTick);
}

// 0x10003890  EV_TRIGGER_ENTER / LEAVE 0x400da (around the comms rig, until the lift is down)
static void G1_OnTriggerLift(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xda))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        WindowSetText(st.win, st.text[1]);
        return;
    }
    WindowSetText(st.win, st.text[0]);
}

// 0x100038e0  EV_TRIGGER_ENTER / LEAVE 0x400c2 / 0x400c3: the current target, or back to the rig
static void G1_OnTriggerObjective(const LanderEvent *ev)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0xc2))
            WindowSetText(st.win, st.text[3 + st.powerCut]);
        if ((ObjId)ev->arg[1] == G1_TRIGGER(0xc3))
            st.scannerSeen = 1;
        return;
    }
    if ((ObjId)ev->arg[1] == G1_TRIGGER(0xc2) && st.scannerSeen == 0)
        WindowSetText(st.win, st.text[1]);
}

// 0x10003960  EV_TRIGGER_ENTER / LEAVE 0x400df: security scanner warning
static void G1_OnTriggerScanners(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xdf))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        WindowSetText(st.win, st.text[7]);
        return;
    }
    WindowSetText(st.win, st.text[3 + st.powerCut]);
}

// 0x100039b0  EV_TRIGGER_ENTER 0x400e1: tunnel camera
static void G1_OnTriggerTunnelIn(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xe1))
        return;
    AutoCam_SetLagY(G1_CAMERA, 0xe);
    st.savedCamAngle = AutoCam_GetUserAngle(G1_CAMERA);
    AutoCam_SetUserAngle(G1_CAMERA, 2);
}

// 0x10003a00  EV_TRIGGER_LEAVE 0x400e1
static void G1_OnTriggerTunnelOut(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xe1))
        return;
    AutoCam_SetLagY(G1_CAMERA, 8);
    if (AutoCam_GetUserAngle(G1_CAMERA) == 2)
        AutoCam_SetUserAngle(G1_CAMERA, st.savedCamAngle);
}

// 0x10003a50  EV_PICKUP {pickup, player}
static void G1_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == G1_PICKUP(0x25)) {
        int32_t shield = (int32_t)Object_GetProperty(G1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(G1_LANDER, 50);
        Object_SetProperty(G1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == G1_PICKUP(0x0d) || (ObjId)ev->arg[0] == G1_PICKUP(0x0e)) {
        Lander_SetCash(G1_LANDER, Lander_GetCash(G1_LANDER) + gMediumCash);
        Voice_CashAcquired(G1_LANDER);
    }
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)G1_PICKUP(9) && id <= (int32_t)G1_PICKUP(0x0a)) {
        if (Lander_AddMissiles(G1_LANDER, 1) == 0) {
            Voice_MissilesFull(G1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
        } else {
            Voice_MissilesAcquired(G1_LANDER);
        }
    }
    if ((ObjId)ev->arg[0] == G1_PICKUP(0x0f)) {
        if (Lander_AddMissiles(G1_LANDER, 2) == 0) {
            Voice_MissilesFull(G1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(G1_LANDER);
    }
}

// 0x10003b80  EV_PICKUP: +50 % shield
static void G1_OnPickupShield26(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_PICKUP(0x26))
        return;
    int32_t shield = (int32_t)Object_GetProperty(G1_LANDER, PROP_HITPOINTS) + 0x80;
    Voice_ShieldAcquired(G1_LANDER, 50);
    Object_SetProperty(G1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10003bf0  EV_PICKUP: +25 % shield
static void G1_OnPickupShield0C(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != G1_PICKUP(0x0c))
        return;
    int32_t shield = (int32_t)Object_GetProperty(G1_LANDER, PROP_HITPOINTS) + 0x40;
    Voice_ShieldAcquired(G1_LANDER, 25);
    Object_SetProperty(G1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10003cf0  EV_TRIGGER_PLANE {player, plane, side}: plane 0x1a00d9 switches the head light
static void G1_OnPlaneHeadLight(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != OBJ_ID(OT_TRIGGERPLANE, 0xd9))
        return;
    if (ev->arg[2] == 0) {
        Lander_SetHeadLight(G1_LANDER, 0x2aa, 0xccc);
        return;
    }
    Lander_SetHeadLight(G1_LANDER, 0, 0);
}

// 0x10003d30  EV_DESTROYED: a hatch gun is removed when destroyed (part bits masked in the event)
static void G1_OnHatchGunDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0] & 0x3ffffffu;
    ((LanderEvent *)ev)->arg[0] = (intptr_t)id;
    if ((int32_t)id >= (int32_t)G1_ATURRET(0xce) && (int32_t)id <= (int32_t)G1_ATURRET(0xd3))
        Object_SetDead(id);
}

// 0x10003d60  EV_TRIGGER_ENTER 0x400de: the patrol (mob 0x10 + light mob 0x11) starts
static void G1_OnTriggerPatrolOn(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xde))
        return;
    MobObj_Wake(0x10);
    AnimPlay(G1_PATROL, 0x19, 1);
    Sound_PlayLoop(G1_PATROL, 0x2a8, -1);
    MobObj_Wake(0x11);
    AnimPlay(G1_PATROL_LIGHT, 0x19, 0x61);
}

// 0x10003dc0  EV_TRIGGER_LEAVE 0x400de: the patrol stops
static void G1_OnTriggerPatrolOff(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xde))
        return;
    AnimStop(G1_PATROL, 1);
    AnimStop(G1_PATROL_LIGHT, 1);
    MobObj_Sleep(0x10);
    MobObj_Sleep(0x11);
    Sound_StopObject(G1_PATROL);
}

// 0x10003e10 / 0x10003e80  EV_TRIGGER_ENTER / LEAVE 0x400fb: turrets 0x59..0x5b, 0xef..0xf1
static void G1_OnTriggerTurretsOn(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xfb))
        return;
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x59), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5a), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5b), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xef), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xf0), PROP_STATE, 1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xf1), PROP_STATE, 1);
}

static void G1_OnTriggerTurretsOff(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != G1_TRIGGER(0xfb))
        return;
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x59), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5a), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5b), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xef), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xf0), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xf1), PROP_STATE, 0);
}

static void G1_BarrierTick(const LanderEvent *ev);
static void G1_OnTriggerBarrierIn(const LanderEvent *ev);
static void G1_OnTriggerBarrierOut(const LanderEvent *ev);
static void G1_OnSwitchBarriers(const LanderEvent *ev);
static void G1_OnBarrierWrapped(const LanderEvent *ev);

// 0x10003ef0  common set-up
static void G1_Setup(void)
{
    st.text = g1_text[gLanguage];
    for (int32_t id = OBJ_ID(OT_TURRET, 0x9c); id <= (int32_t)OBJ_ID(OT_TURRET, 0xb0); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t n = 0xce; n <= 0xd3; n++)
        Object_SetProperty(G1_ATURRET(n), PROP_ARMOUR, 0x28);
    for (int32_t id = OBJ_ID(OT_TURRET, 0xe8); id <= (int32_t)OBJ_ID(OT_TURRET, 0xfa); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    for (int32_t id = OBJ_ID(OT_TURRET, 0xe2); id <= (int32_t)OBJ_ID(OT_TURRET, 0xe7); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    for (int32_t id = OBJ_ID(OT_FUELDUMP, 0x97); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0x9a); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = OBJ_ID(OT_DESTRUCTIBLE, 0xfc); id <= (int32_t)OBJ_ID(OT_DESTRUCTIBLE, 0x10d); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
    Object_SetProperty(G1_GUN_12, PROP_ARMOUR, 0x3c);
    Object_SetProperty(G1_GUN_12, PROP_EXPLOSION, 0x50);
    Object_SetProperty(G1_CORE_GUN, PROP_ARMOUR, 0x64);
    Object_SetProperty(G1_CORE_GUN, PROP_EXPLOSION, 0xa0);
    Object_SetProperty(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xdc), PROP_EXPLOSION, 0x50);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x5c), PROP_ARMOUR, 0x32);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x5c), PROP_EXPLOSION, 0x64);
    gTurretTypes[21] = &g1_turretType21;
    gTurretTypes[22] = &g1_turretType22;
    gTurretTypes[23] = &g1_turretType23;
    gTurretTypes[24] = &g1_turretType24;
    gTurretTypes[13] = &g1_turretType13;
    gTurretTypes[15] = &g1_turretType15;
    gTurretTypes[17] = &g1_turretType17;
    MobObj_Wake(0x12);
    Object_SetDead(OBJ_ID(OT_TURRET, 0xe8));
    Object_SetDead(OBJ_ID(OT_TURRET, 0xe9));
    AnimPlay(G1_ATURRET(0xea), 0x1e, 0x62);
    AnimPlay(G1_ATURRET(0xf2), 0x1e, 0x62);
    AnimPlay(G1_ATURRET(0xf7), 0x1e, 0x62);
    AnimPlay(G1_ATURRET(0xf9), 0x1e, 0x62);
    AnimSetUpdateInterval(G1_ATURRET(0xea), 0x1e);
    AnimSetUpdateInterval(G1_ATURRET(0xec), 0x19);
    AnimSetUpdateInterval(G1_ATURRET(0xf2), 0x14);
    AnimSetUpdateInterval(G1_ATURRET(0xf7), 0x1e);
    AnimSetUpdateInterval(G1_ATURRET(0xf9), 0x14);
    Sound_PlayLoop(G1_ATURRET(0xea), 0x88, -1);
    Sound_PlayLoop(G1_ATURRET(0xec), 0x88, -1);
    Sound_PlayLoop(G1_ATURRET(0xed), 0x88, -1);
    Sound_PlayLoop(G1_ATURRET(0xf2), 0x88, -1);
    Sound_PlayLoop(G1_ATURRET(0xf7), 0x88, -1);
    Sound_PlayLoop(G1_ATURRET(0xf9), 0x88, -1);
    for (int32_t n = 0xce; n <= 0xd3; n++)               // (unrolled)
        Object_SetProperty(G1_ATURRET(n), PROP_STATE, 0);
    MobObj_Wake(0);
    MobObj_Wake(1);
    MobObj_Wake(2);
    MobObj_Wake(3);
    MobObj_Wake(4);
    static const uint16_t dead[] = {                     // (unrolled) sparks + hatch covers
        0x01, 0x02, 0x03, 0x07, 0x08, 0x04, 0x05, 0x06, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x70, 0x71,
        0x6d, 0x6e, 0x6f, 0x5e, 0x5f, 0x60, 0x65, 0x66, 0x67, 0x61, 0x62, 0x63, 0x64,
    };
    for (size_t i = 0; i < sizeof dead / sizeof dead[0]; i++)
        Object_SetDead(G1_EMITTER(dead[i]));
    Object_SetDead(G1_ANIM(0xcd));
    Object_SetDead(G1_ANIM(0xd4));
    Object_SetDead(G1_ANIM(0xd5));
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10003c60
    EventType_AddHandler(EV_DESTROYED, NULL, G1_OnLiftGeneratorDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, G1_OnDestroyedSayEscape);
    EventType_AddHandler(EV_SWITCH, NULL, G1_OnSwitchDoor78);
    EventType_AddHandler(EV_DESTROYED, NULL, G1_OnPower1Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, G1_OnPower2Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, G1_OnCoreDestroyed);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerDoors);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerDoors);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerHatchRoom);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerHatchCA);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerPostEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerPostLeaveLander);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerPostLeavePatrol);
    EventType_AddHandler(EV_SWITCH, NULL, G1_OnSwitchDoor79);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerGunsUp);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerGunsDown);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerBarrierIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerBarrierOut);
    EventType_AddHandler(EV_SWITCH, NULL, G1_OnSwitchBarriers);
    EventType_AddHandler(EV_ANIM_WRAPPED, NULL, G1_OnBarrierWrapped);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerHeat);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerGunsEC);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerGun12);
    EventType_AddHandler(EV_SWITCH, NULL, G1_OnSwitchDoor86);
    EventType_AddHandler(EV_SWITCH, NULL, G1_OnSwitchDoor8F);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerGunAF);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerLift);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerLift);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerObjective);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerObjective);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerScanners);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerScanners);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerTunnelIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerTunnelOut);
    EventType_AddHandler(EV_PICKUP, NULL, G1_OnPickup);
    EventType_AddHandler(EV_PICKUP, NULL, G1_OnPickupShield26);
    EventType_AddHandler(EV_PICKUP, NULL, G1_OnPickupShield0C);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, G1_OnPlaneHeadLight);
    EventType_AddHandler(EV_TICK_1S, NULL, G1_PopupReturnTick);
    EventType_AddHandler(EV_DESTROYED, NULL, G1_OnHatchGunDestroyed);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerPatrolOn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerPatrolOff);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerTurretsOn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, G1_OnTriggerTurretsOff);
    SetGravity(12);
    gAirResistance = 0xe;
    gMainThrottlePower = 0x18;
    Environment_OverrideExteriorFog(0, 0, 0);
    AnimSetUpdateInterval(G1_ANIM(0x86), 0x19);
    AnimSetUpdateInterval(G1_ANIM(0x85), 0x19);
    AnimSetUpdateInterval(G1_ANIM(0x78), 0x19);
    AnimSetUpdateInterval(G1_ANIM(0x7b), 0x12);
    AnimSetUpdateInterval(G1_ANIM(0x7c), 0x12);
    AnimSetUpdateInterval(G1_ANIM(0x7d), 0x14);
    AnimSetUpdateInterval(G1_ANIM(0x7e), 0x14);
    AnimSetUpdateInterval(G1_ANIM(0x8d), 0x19);
    AnimSetUpdateInterval(G1_ANIM(0x8e), 0x19);
    AnimPlay(G1_ANIM(0xdb), 0, 0);                       // the lift
    for (int32_t n = 0x2a; n <= 0x2f; n++)               // (unrolled) barriers open
        AnimPlay(G1_ANIM(n), 4, 0);
    for (int32_t n = 0x72; n <= 0x77; n++)               // (unrolled) barrier emitters loop
        AnimPlay(G1_ANIM(n), 6, 0x81);
    for (int32_t n = 0x72; n <= 0x74; n++)
        Sound_PlayLoop(G1_ANIM(n), 0xd4, -1);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xdc), 0x20, 0x81);
    Sound_PlayLoop(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xdc), 0xd3, -1);
    for (int32_t n = 0x91; n <= 0x93; n++)
        Sound_PlayLoop(OBJ_ID(OT_DESTRUCTIBLE, n), 0xfd, -1);
    for (int32_t id = OBJ_ID(OT_TURRET, 0x9c); id <= (int32_t)OBJ_ID(OT_TURRET, 0xaf); id++)   // (unrolled)
        Object_SetProperty((ObjId)id, PROP_STATE, 0);
    for (int32_t id = OBJ_ID(OT_TURRET, 0x9c); id <= (int32_t)OBJ_ID(OT_TURRET, 0xaf); id++) { // (unrolled)
        if (id == (int32_t)OBJ_ID(OT_TURRET, 0xa2) || id == (int32_t)OBJ_ID(OT_TURRET, 0xa3))
            continue;                                    // (not these two)
        Object_SetProperty((ObjId)id, PROP_HITPOINTS, 0xff);
    }
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x57), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x58), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xe8), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xe9), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x59), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5a), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5b), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xef), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xf0), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xf1), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x5d), PROP_EXPLOSION, 0x64);
    TexAnimPlay(0x26, 1, 100);
    TexAnimPlay(0x28, 1, 100);
    TexAnimPlay(0x4b, 1, 100);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, G1_OnTriggerReactorRoom);
    MobObj_Wake(0x0a);                                   // Vern's gun
    Sound_PlayLoop(G1_CORE_GUN, 0x2a9, -1);
    AnimPlay(G1_ANIM(0xe0), 0x1c, 0);
    AnimSetUpdateInterval(G1_PATROL, 0x32);
    AnimSetUpdateInterval(G1_PATROL_LIGHT, 0x32);
    Object_SetProperty(G1_TRIGGER(0x46), PROP_TARGET, G1_PATROL);
    Object_SetProperty(G1_TRIGGER(0x49), PROP_TARGET, G1_PATROL);
    Object_SetProperty(G1_TRIGGER(0x4b), PROP_TARGET, G1_PATROL);
    Object_SetProperty(G1_TRIGGER(0x4d), PROP_TARGET, G1_PATROL);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xab), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xac), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xad), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xae), PROP_STATE, 0);
    MissionScore_Init(G1_LANDER, 30000, 8);              // 0x10001530
    st.startShield = (int32_t)Object_GetProperty(G1_LANDER, PROP_HITPOINTS);
}

// 0x10004c00  EV_ANIM_WRAPPED: barrier emitter 0x30073 / 0x30076 completed a loop -> check the
// barriers for the next 8 x 100 ms
static void G1_OnBarrierWrapped(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != G1_ANIM(0x73) && id != G1_ANIM(0x76))
        return;
    st.barrierActive = 1;
    EventType_AddHandler(EV_TICK_100MS, NULL, G1_BarrierTick);
}

// 0x10004c40  EV_TICK_100MS: something in a barrier trigger closes that barrier (gun up, the
// group's reset switch off) - each group of three only while none of it is closed yet
static void G1_BarrierTick(const LanderEvent *ev)
{
    (void)ev;
    st.barrierTicks++;
    for (int g = 0; g < 2; g++) {
        int32_t *on = &st.barrierOn[g * 3];
        if (on[0] != 0 || on[1] != 0 || on[2] != 0)
            continue;
        for (int i = 0; i < 3; i++) {
            int k = g * 3 + i;
            if (st.barrierIn[k] == 0)
                continue;
            AnimPlay(G1_ANIM(0x2a + k), 4, 4);
            AnimPlay(G1_ANIM(0xb1 + k), 7, 0);
            Object_SetProperty(OBJ_ID(OT_TURRET, 0x9c + k), PROP_STATE, 1);
            Object_SetProperty(g == 0 ? G1_SWITCH(0x0b) : G1_SWITCH(0x10), PROP_STATE, 0);
            st.barrierIn[k] = 0;
            on[i] = 1;
            Sound_PlayOnce(G1_ANIM(0x72 + k), 0xfc);   // the barrier's emitter
            Sound_PlayOnce(G1_ANIM(0xb1 + k), 0xda);
        }
    }
    if (st.barrierTicks > 7) {
        st.barrierTicks = 0;
        st.barrierActive = 0;
        EventType_RemoveHandler(EV_TICK_100MS, G1_BarrierTick);
    }
}

// 0x10004f60 / 0x10004fd0  EV_TRIGGER_ENTER / LEAVE 0x40047, 0x4004e..0x40052: barrier triggers
static void G1_SetBarrierIn(const LanderEvent *ev, int32_t v)
{
    static const ObjId trig[6] = { G1_TRIGGER(0x47), G1_TRIGGER(0x4e), G1_TRIGGER(0x4f),
                                   G1_TRIGGER(0x50), G1_TRIGGER(0x51), G1_TRIGGER(0x52) };
    for (int k = 0; k < 6; k++)
        if ((ObjId)ev->arg[1] == trig[k])
            st.barrierIn[k] = v;
}

static void G1_OnTriggerBarrierIn(const LanderEvent *ev)
{
    G1_SetBarrierIn(ev, 1);
}

static void G1_OnTriggerBarrierOut(const LanderEvent *ev)
{
    G1_SetBarrierIn(ev, 0);
}

// 0x10005040  EV_SWITCH 0xa000b / 0xa0010 (any change): open that group's barriers again
static void G1_OnSwitchBarriers(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == G1_SWITCH(0x0b)) {
        st.barrierOn[0] = st.barrierOn[1] = st.barrierOn[2] = 0;
        AnimPlay(G1_ANIM(0x2a), 4, 0);
        AnimPlay(G1_ANIM(0x2b), 4, 0);
        AnimPlay(G1_ANIM(0x2c), 4, 0);
        AnimPlay(G1_ANIM(0xb1), 7, 4);
        AnimPlay(G1_ANIM(0xb2), 7, 4);
        AnimPlay(G1_ANIM(0xb3), 7, 4);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0x9c), PROP_STATE, 0);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0x9d), PROP_STATE, 0);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0x9e), PROP_STATE, 0);
    }
    if ((ObjId)ev->arg[0] == G1_SWITCH(0x10)) {
        st.barrierOn[3] = st.barrierOn[4] = st.barrierOn[5] = 0;
        AnimPlay(G1_ANIM(0x2d), 4, 0);
        AnimPlay(G1_ANIM(0x2e), 4, 0);
        AnimPlay(G1_ANIM(0x2f), 4, 0);
        AnimPlay(G1_ANIM(0xb4), 7, 4);
        AnimPlay(G1_ANIM(0xb5), 7, 4);
        AnimPlay(G1_ANIM(0xb6), 7, 4);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0x9f), PROP_STATE, 0);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa0), PROP_STATE, 0);
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xa1), PROP_STATE, 0);
    }
}

// 0x10005190  LevelStartLevel_Lander (export 0x10001010)
static void G1_StartLander(void)
{
    gNbWaypoints = g1_nbWaypoints;
    gWaypoints = g1_waypoints;
    MobObj_Wake(G1_MOB_LANDER);
    MobObj_Wake(G1_MOB_CAMERA);
    MobObj_Wake(0x0d);
    MobObj_Wake(0x0e);
    MobObj_Wake(0x0c);
    Media_AudioPlay(0);
    Object_SetProperty(G1_CAMERA, PROP_TARGET, G1_LANDER);
    G1_Setup();
    Lander_SetHeadLight(G1_LANDER, 0x1ff, 0xc00);
}

// 0x10005220  LevelStartLevel_Flycam (export 0x10001020)
static void G1_StartFlycam(void)
{
    G1_Setup();
    MobObj_Wake(G1_MOB_FLYCAM);
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void G1_Stop(void)
{
}

// 0x10001040
static uint32_t G1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel: two calls of the empty 0x10001050, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t G1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.popupHit = 4;
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText g1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000a160, .count = 3 * 12, .out = g1_text },
    { 0 },
};

const mission_script_t mission_ganymede_1 = {
    .name          = "GANYMEDE\\1",
    .get_version   = G1_GetVersion,
    .load          = G1_Load,
    .start_lander  = G1_StartLander,
    .start_flycam  = G1_StartFlycam,
    .start_animcam = NULL,
    .stop          = G1_Stop,
    .text          = g1_gametext,
};
