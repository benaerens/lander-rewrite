// MISSIONS\TITAN\3\MISSIONR.DLL - faithful C port.
//
// Mission: get into the base (waypoint 1: an airlock of two door pairs, triggers 0x40004..06),
// avoid detection (the objective line blinks between two lines; bumping a
// crate 0x30012..15, entering the hatch 0x40150/51 or hooking the pod raises the base alarm), find
// the objective - pod 0x10c000f, next to the lander mob 0xc - and get it to the convoy bay: every
// 50 s a convoy (anim objects mob 0x12 / 0x10 / 0x11, mob 7) comes in; entering trigger 0x4003f
// between seconds 7 and 17 of the cycle starts the escape cut-scene: the player lander and the
// pod are put to sleep, cameras mob 2..6 follow vehicle 0x1030012 along triggers 0x40027..2a, and
// when its anim 0x1b ends the mission ends with result 1. Energy fields (destructibles
// 0x800f4/f5 with destroy levels, switches 0xa0132..34, triggers 0x40016..19 / 0x40025/26: -10
// shield per 100 ms) and guard patrols (AnimSeq) on the way. Par time 300 s, 7 pickups.
//
// Addresses are the DLL's (image base 0x10000000). This DLL starts with its AnimSeq copy
// (0x10001000-0x100011f1: mission_lib.c's AnimSeq with AnimSeq_Stop 0x10001190, Init at
// 0x100011c0, slots at 0x1000c3c0), then the exports: LevelLoadLevel 0x10001200, LevelStartLevel_Lander 0x10001210
// (-> 0x10004650), LevelStartLevel_Flycam and LevelStopLevel 0x10001220 (-> the empty
// 0x10004690: the fly-cam start does no set-up), LevelGetVersion 0x10001230. The score library
// (0x10001240-0x100017e4, Init 0x10001710, End 0x10001590) is in mission_lib.c; the
// destroyed-sound handler 0x100037f0 is MissionLib_OnDestroyedSound.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define T3_LANDER        MOB_ID(OT_LANDER, 0xb)          // 0x0101000b
#define T3_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define T3_CUTCAM(n)     MOB_ID(OT_CAMERA, (n))          // 0x0106000n escape cameras 2..6
#define T3_POD           MOB_ID(OT_POD, 0xf)             // 0x010c000f the objective
#define T3_VEHICLE       MOB_ID(OT_ANIMOBJECT, 0x12)     // 0x01030012 convoy vehicle
#define T3_CONVOY_10     MOB_ID(OT_ANIMOBJECT, 0x10)     // 0x01030010
#define T3_CONVOY_11     MOB_ID(OT_ANIMOBJECT, 0x11)     // 0x01030011
#define T3_CONVOY_07     MOB_ID(OT_ANIMOBJECT, 0x07)     // 0x01030007
#define T3_GUARD(n)      MOB_ID(OT_ANIMTURRET, (n))      // 0x010e00nn
#define T3_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define T3_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define T3_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define T3_TURRET(n)     OBJ_ID(OT_TURRET, (n))          // 0x000000nn
#define T3_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))    // 0x000800nn
#define T3_ANIMTUR(n)    OBJ_ID(OT_ANIMTURRET, (n))      // 0x000e00nn
#define T3_ANIMDESTR(n)  OBJ_ID(OT_ANIMDESTRUCTIBLE, (n))// 0x000f00nn
#define T3_FUELDUMP(n)   OBJ_ID(OT_FUELDUMP, (n))        // 0x001100nn
#define T3_LIGHT(n)      OBJ_ID(OT_LIGHTOBJECT, (n))     // 0x001600nn
#define T3_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define T3_FIELD_A       T3_DESTR(0xf4)                  // 0x000800f4 energy field (destroy levels)
#define T3_FIELD_B       T3_DESTR(0xf5)                  // 0x000800f5
#define T3_MOB_LANDER    0xb
#define T3_MOB_CAMERA    1

// ---- static data (.data) --------------------------------------------------------------------------
// 0x10009170: [language][8] string pointers (entries 2 and 7 are never shown)
static const char *t3_text[3][8];
static const int32_t  t3_nbWaypoints = 1;                // 0x10009160
static const Waypoint t3_waypoints[1] = {                // 0x10009168
    { 70400, -36608 },
};
static const TurretType t3_turretType22 = {              // 0x100091e8 -> gTurretTypes[22]
    { 0, 0, 512, 0 }, 0x18, 0, 3, 5, 60, 0x30, 1024, 12000, 0
};
static const TurretType t3_turretType21 = {              // 0x10009208 -> gTurretTypes[21]
    { 1731, 338, 1442, 0 }, 0x02, 1, 2, 50, 100, 0x20, 800, 60000, 0
};
static const TurretType t3_turretType0 = {               // 0x10009228 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 3, 10, 80, 0x20, 1024, 12000, 0
};
static const TurretType t3_turretType2 = {               // 0x10009248 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 3, 5, 80, 0x20, 1024, 12000, 0
};
static const TurretType t3_turretType4 = {               // 0x10009268 -> gTurretTypes[4]
    { 0, 24, 240, 0 }, 0x00, 0, 3, 25, 100, 0x18, 1024, 12000, 0
};
static const TurretType t3_turretType5 = {               // 0x10009288 -> gTurretTypes[5]
    { 85, 434, 461, 0 }, 0x02, 2, 2, 25, 100, 0x10, 512, 12000, 0
};
static const TurretType t3_turretType10 = {              // 0x100092a8 -> gTurretTypes[10]
    { 112, 0, 601, 0 }, 0x00, 1, 2, 10, 80, 0x10, 1024, 8192, 0
};
// AnimSeq sequences {anim, repeat, AnimPlay flags} ... 0xff (.data; never patched by this mission).
// Unreferenced sequences at 0x100092c8, 0x100092e0, 0x10009300, 0x100093a0..0x10009400 and
// 0x10009480 are not ported.
static uint8_t t3_seq2f0[] = { 0x39,0x05,0x60, 0x2a,0x02,0x60, 0xff,0,0 };
static uint8_t t3_seq310[] = { 0x39,0x03,0x60, 0xff,0,0 };
static uint8_t t3_seq318[] = { 0x39,0x01,0x60, 0x34,0x02,0x60, 0x39,0x01,0x60, 0xff,0,0 };
static uint8_t t3_seq328[] = { 0x2c,0x04,0x60, 0x39,0x01,0x60, 0x34,0x02,0x60, 0x39,0x03,0x60, 0xff,0,0 };
static uint8_t t3_seq338[] = { 0x39,0x02,0x60, 0x2a,0x02,0x60, 0x39,0x01,0x60, 0xff,0,0 };
static uint8_t t3_seq348[] = { 0x39,0x02,0x60, 0x2a,0x02,0x60, 0xff,0,0 };
static uint8_t t3_seq358[] = { 0x39,0x02,0x60, 0xff,0,0 };
static uint8_t t3_seq360[] = { 0x39,0x0b,0x60, 0x2c,0x01,0x60, 0x39,0x09,0x60, 0x2c,0x01,0x60,
                               0x39,0x0b,0x60, 0x2c,0x01,0x60, 0x39,0x09,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t t3_seq380[] = { 0x39,0x09,0x60, 0x2c,0x01,0x60, 0x39,0x0b,0x60, 0x2c,0x01,0x60,
                               0x39,0x09,0x60, 0x2c,0x01,0x60, 0x39,0x0b,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t t3_seq418[] = { 0x39,0x07,0x60, 0x2a,0x02,0x60, 0x39,0x04,0x60, 0xff,0,0 };
static uint8_t t3_seq428[] = { 0x39,0x06,0x60, 0x2a,0x02,0x60, 0x39,0x04,0x60, 0xff,0,0 };
static uint8_t t3_seq438[] = { 0x39,0x04,0x60, 0x2a,0x02,0x60, 0xff,0,0 };
static uint8_t t3_seq448[] = { 0x2a,0x02,0x60, 0x39,0x01,0x60, 0xff,0,0 };
static uint8_t t3_seq458[] = { 0x2a,0x02,0x60, 0xff,0,0 };
static uint8_t t3_seq460[] = { 0x39,0x02,0x60, 0x2a,0x02,0x60, 0xff,0,0 };
static uint8_t t3_seq470[] = { 0x39,0x02,0x60, 0x2a,0x02,0x60, 0x39,0x01,0x60, 0xff,0,0 };
static const uint8_t t3_blinkPeriod = 3;                 // 0x1000948c

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      airlockSide;     // 0x100091d0 (.data 1) 1 = came in by 0x40004, 2 = by 0x40005
    int32_t      hatchReady;      // 0x100091d4 (.data 1)
    int32_t      convoyTicks;     // 0x100091d8 (.data 666, zeroed by the set-up)
    int32_t      door7cShut;      // 0x100091dc (.data 1)
    int32_t      fieldSetA;       // 0x100091e0 (.data 1) 1: fields 0x40016..18 live, 0: 0x40019/25/26
    Window      *win;             // 0x1000be60 objective line
    int32_t      fanTicks;        // 0x1000be70
    int32_t      hatchAlarmDone;  // 0x1000be7c
    int32_t      hatchMoving;     // 0x1000be80
    int32_t      hatchOpen;       // 0x1000be84
    int32_t      gateSeen;        // 0x1000be88 written, never read
    int32_t      lift;            // 0x1000be8c lift 0x300d2: 0 down, 1 rising, 3 up, 2 sinking
    int32_t      alarm;           // 0x1000be90
    int32_t      inLobby;         // 0x1000be98 lander in trigger 0x40046
    int32_t      barracksHits;    // 0x1000be9c
    int32_t      guard1fTicks;    // 0x1000bea0
    int32_t      guard20Ticks;    // 0x1000bea4
    int32_t      gateTicks;       // 0x1000bea8
    int32_t      fieldTimerOn;    // 0x1000beac
    int32_t      fieldAlt;        // 0x1000beb0 toggled by switch 0xa0134
    int32_t      fieldDamageOn;   // 0x1000beb4 the 100-ms damage handler is registered
    int32_t      inField16;       // 0x1000beb8
    int32_t      inField17;       // 0x1000bebc
    int32_t      inField18;       // 0x1000bec0
    int32_t      inField19;       // 0x1000bec4
    int32_t      inField26;       // 0x1000bec8 (cleared by *leaving 0x40025*)
    int32_t      inField25;       // 0x1000becc (cleared by *leaving 0x40026*)
    uint8_t      blink;           // 0x1000bed8
    uint8_t      blinkDown;       // 0x1000bedc
    int32_t      fieldTimer;      // 0x1000bee0
    int32_t      fieldLevel;      // 0x1000bee4 destroy level of the field objects
    int32_t      fieldLevelSaved; // 0x1000bee8
    int32_t      airlockTicks;    // 0x1000c32c
    const char *const *text;      // 0x1000c330 = t3_text[gLanguage]
    int32_t      escaping;        // 0x1000c334
    int32_t      hatchTicks;      // 0x1000c338
} st;

// ================================================================================================
// Mission
// ================================================================================================

static void T3_AirlockOpenB(const LanderEvent *ev);
static void T3_AirlockOpenA(const LanderEvent *ev);

// 0x100017f0  EV_TRIGGER_ENTER: airlock (outer pair 0x3014c/0x3014e on 0x40004, inner pair
// 0x3014d/0x3014f on 0x40005; the middle 0x40006 opens the far pair 4 s later). Sets the door
// frame times on every trigger entry; the 0x40005 case plays its sounds on the outer pair.
static void T3_OnTriggerAirlockIn(const LanderEvent *ev)
{
    AnimSetUpdateInterval(T3_ANIM(0x14c), 0x14);
    AnimSetUpdateInterval(T3_ANIM(0x14e), 0x14);
    AnimSetUpdateInterval(T3_ANIM(0x14d), 0x14);
    AnimSetUpdateInterval(T3_ANIM(0x14f), 0x14);
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x04)) {
        AnimPlay(T3_ANIM(0x14c), 1, 0);
        AnimPlay(T3_ANIM(0x14e), 2, 0);
        Sound_PlayOnce(T3_ANIM(0x14c), 0xab);
        Sound_PlayOnce(T3_ANIM(0x14e), 0xab);
        st.airlockSide = 1;
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x05)) {
        AnimPlay(T3_ANIM(0x14d), 1, 0);
        AnimPlay(T3_ANIM(0x14f), 2, 0);
        Sound_PlayOnce(T3_ANIM(0x14c), 0xab);
        Sound_PlayOnce(T3_ANIM(0x14e), 0xab);
        st.airlockSide = 2;
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x06)) {
        if (st.airlockSide == 1) {
            st.airlockTicks = 0;
            EventType_AddHandler(EV_TICK_1S, NULL, T3_AirlockOpenB);
        }
        if (st.airlockSide == 2) {
            st.airlockTicks = 0;
            EventType_AddHandler(EV_TICK_1S, NULL, T3_AirlockOpenA);
        }
    }
}

// 0x10001930  EV_TRIGGER_LEAVE: the pair of the left trigger closes
static void T3_OnTriggerAirlockOut(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x04)) {
        AnimPlay(T3_ANIM(0x14c), 1, 4);
        AnimPlay(T3_ANIM(0x14e), 2, 4);
        Sound_PlayOnce(T3_ANIM(0x14c), 0xab);
        Sound_PlayOnce(T3_ANIM(0x14e), 0xab);
        st.airlockSide = 1;
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x05)) {
        AnimPlay(T3_ANIM(0x14d), 1, 4);
        AnimPlay(T3_ANIM(0x14f), 2, 4);
        Sound_PlayOnce(T3_ANIM(0x14d), 0xab);
        Sound_PlayOnce(T3_ANIM(0x14f), 0xab);
        st.airlockSide = 2;
    }
}

// 0x100019f0  EV_TICK_1S: 4 s after the middle -> inner pair opens
static void T3_AirlockOpenB(const LanderEvent *ev)
{
    (void)ev;
    if (++st.airlockTicks <= 3)
        return;
    AnimPlay(T3_ANIM(0x14d), 1, 0);
    AnimPlay(T3_ANIM(0x14f), 2, 0);
    Sound_PlayOnce(T3_ANIM(0x14d), 0xab);
    Sound_PlayOnce(T3_ANIM(0x14f), 0xab);
    EventType_RemoveHandler(EV_TICK_1S, T3_AirlockOpenB);
}

// 0x10001a60  EV_TICK_1S: 4 s after the middle -> outer pair opens
static void T3_AirlockOpenA(const LanderEvent *ev)
{
    (void)ev;
    if (++st.airlockTicks <= 3)
        return;
    AnimPlay(T3_ANIM(0x14c), 1, 0);
    AnimPlay(T3_ANIM(0x14e), 2, 0);
    Sound_PlayOnce(T3_ANIM(0x14c), 0xab);
    Sound_PlayOnce(T3_ANIM(0x14e), 0xab);
    EventType_RemoveHandler(EV_TICK_1S, T3_AirlockOpenA);
}

// 0x10001ad0  EV_ANIM_FINISHED (after the escape started): the vehicle has driven off -> success
static void T3_OnVehicleGone(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x1b && ev->arg[0] == (intptr_t)T3_VEHICLE)
        MissionScore_End(1);                             // 0x10001590
}

// 0x10001af0  point an escape camera at a target
static void T3_CamFollow(ObjId cam, ObjId target)
{
    Object_SetProperty(cam, PROP_FLAGS, 0xa4);
    Object_SetProperty(cam, PROP_FLAGS, (intptr_t)PROPFLAGS_CLEAR);
    Object_SetProperty(cam, PROP_TARGET, target);
}

// 0x10001b30  EV_TRIGGER_ENTER (escape): the vehicle passes 0x40027..2a -> next camera
static void T3_OnTriggerCutCam(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x27)) {
        MobObj_Sleep(2);
        MobObj_Wake(3);
        Object_SetProperty(T3_CUTCAM(3), PROP_FLAGS, 0x40);
        T3_CamFollow(T3_CUTCAM(3), T3_VEHICLE);
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x28)) {
        MobObj_Sleep(3);
        MobObj_Wake(4);
        T3_CamFollow(T3_CUTCAM(4), T3_VEHICLE);
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x29)) {
        MobObj_Sleep(4);
        MobObj_Wake(5);
        Object_SetProperty(T3_CUTCAM(5), PROP_FLAGS, 1);
        Object_SetProperty(T3_CUTCAM(5), PROP_TARGET, T3_VEHICLE);
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x2a)) {
        MobObj_Sleep(5);
        MobObj_Wake(6);
        T3_CamFollow(T3_CUTCAM(6), T3_VEHICLE);
    }
}

// 0x10001c10  EV_TRIGGER_ENTER 0x4003f (armed between convoy seconds 7 and 17): escape - does not
// check that the pod was brought
static void T3_OnTriggerEscape(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T3_TRIGGER(0x3f))
        return;
    WindowSetText(st.win, st.text[6]);
    Object_SetProperty(T3_POD, PROP_TARGET, -1);
    MobObj_Sleep(0xf);
    Object_SetProperty(T3_LANDER, PROP_FLAGS, (intptr_t)(PROPFLAGS_CLEAR | LF_LOCAL_PLAYER));
    MobObj_Sleep(T3_MOB_LANDER);
    MobObj_Sleep(T3_MOB_CAMERA);
    MobObj_Wake(2);
    Object_SetProperty(T3_CUTCAM(2), PROP_FLAGS, 0x40);
    T3_CamFollow(T3_CUTCAM(2), T3_VEHICLE);
    Object_SetProperty(T3_TRIGGER(0x27), PROP_TARGET, T3_VEHICLE);
    Object_SetProperty(T3_TRIGGER(0x28), PROP_TARGET, T3_VEHICLE);
    Object_SetProperty(T3_TRIGGER(0x29), PROP_TARGET, T3_VEHICLE);
    Object_SetProperty(T3_TRIGGER(0x2a), PROP_TARGET, T3_VEHICLE);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerCutCam);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T3_OnVehicleGone);
    st.escaping = 1;
}

// 0x10001d20  EV_TICK_1S: the convoy cycle (50 s; stops repeating once the escape started)
static void T3_ConvoyTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.convoyTicks == 1)
        for (int32_t n = 0x95; n <= 0xaa; n++)            // (unrolled) bay lights off
            Object_SetDead(T3_LIGHT(n));
    if (st.convoyTicks == 2) {                           // bay doors open
        AnimPlay(T3_ANIM(0xb8), 0x3e, 0);
        AnimPlay(T3_ANIM(0xb9), 0x3f, 0);
        Sound_PlayOnce(T3_ANIM(0xb8), 0xab);
    }
    if (st.convoyTicks == 3) {                           // the convoy comes in
        AnimPlay(T3_CONVOY_07, 0x18, 0);
        AnimPlay(T3_ANIM(0xc4), 0x19, 0);
        Sound_PlayOnce(T3_ANIM(0xc4), 0xc8);
        AnimPlay(T3_ANIM(0xba), 0x1a, 0);
        AnimPlay(T3_ANIM(0xd8), 0x17, 0);
        Sound_PlayOnce(T3_ANIM(0xd8), 0xce);
        AnimPlay(T3_VEHICLE, 0x1c, 0);
        AnimPlay(T3_CONVOY_10, 0x1d, 0);
        AnimPlay(T3_CONVOY_11, 0x1e, 0);
        AnimPlay(T3_ANIM(0xd7), 0x16, 0);
    }
    if (st.convoyTicks == 7)
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerEscape);
    if (st.convoyTicks == 0x11) {
        EventType_RemoveHandler(EV_TRIGGER_ENTER, T3_OnTriggerEscape);
        AnimStop(T3_CONVOY_07, 0);
        InTime1(100, MobObj_Sleep, 7);
    }
    if (st.convoyTicks == 0x13) {                        // bay doors close
        AnimPlay(T3_ANIM(0xb8), 0x3e, 4);
        AnimPlay(T3_ANIM(0xb9), 0x3f, 4);
        Sound_PlayOnce(T3_ANIM(0xb8), 0xab);
        AnimStop(T3_VEHICLE, 0);
        AnimStop(T3_CONVOY_10, 0);
        AnimStop(T3_CONVOY_11, 0);
    }
    if (st.convoyTicks == 0x14) {
        AnimSetUpdateInterval(T3_ANIM(0xcd), 2);
        AnimSetUpdateInterval(T3_ANIM(0xce), 2);
        AnimPlay(T3_ANIM(0xcd), 0x3d, 0);
        AnimPlay(T3_ANIM(0xce), 0x3d, 0);
        Sound_PlayOnce(T3_ANIM(0xcd), 0x3f8);
    }
    if (st.convoyTicks == 0x15) {                        // lights on, the convoy drives on
        for (int32_t n = 0x95; n <= 0xaa; n++)            // (unrolled)
            Object_SetAlive(T3_LIGHT(n));
        AnimPlay(T3_VEHICLE, 0x1b, 0);
        AnimPlay(T3_CONVOY_10, 0x1b, 0);
        AnimPlay(T3_CONVOY_11, 0x1b, 0);
        AnimSetUpdateInterval(T3_VEHICLE, 0x28);
        AnimSetUpdateInterval(T3_CONVOY_10, 0x28);
        AnimSetUpdateInterval(T3_CONVOY_11, 0x28);
        InTime3(700, AnimPlay, T3_ANIM(0xcd), 0x3d, 4);
        InTime3(700, AnimPlay, T3_ANIM(0xce), 0x3d, 4);
    }
    if (st.escaping != 0)
        return;
    if (st.convoyTicks == 0x1e) {
        AnimStop(T3_VEHICLE, 0);
        AnimStop(T3_CONVOY_10, 0);
        AnimStop(T3_CONVOY_11, 0);
    }
    if (st.convoyTicks == 0x1f) {                        // reset for the next convoy
        AnimPlay(T3_ANIM(0xba), 0x1a, 4);
        AnimPlay(T3_ANIM(0xc4), 0x19, 4);
        Sound_PlayOnce(T3_ANIM(0xc4), 0xc8);
        AnimPlay(T3_ANIM(0xd7), 0x16, 4);
        AnimPlay(T3_ANIM(0xd8), 0x17, 4);
        Sound_PlayOnce(T3_ANIM(0xd8), 0xce);
        MobObj_Sleep(0x12);
        MobObj_Wake(0x12);
        MobObj_Wake(7);
        MobObj_Sleep(0x10);
        MobObj_Wake(0x10);
        MobObj_Sleep(0x11);
        MobObj_Wake(0x11);
    }
    if (st.convoyTicks == 0x32) {
        st.convoyTicks = 0;
        AnimSetUpdateInterval(T3_VEHICLE, 10);
        AnimSetUpdateInterval(T3_CONVOY_10, 10);
        AnimSetUpdateInterval(T3_CONVOY_11, 10);
        AnimSetUpdateInterval(T3_CONVOY_07, 10);
    }
}

static void T3_Guard1fTick(const LanderEvent *ev);
static void T3_Guard20Tick(const LanderEvent *ev);

// 0x100022b0  EV_DESTROYED: generators 0x80093 / 0x80094 (and 0x800bb, 0x80010) vanish; their
// guards start their (one-shot) sequences
static void T3_OnDestroyedGenerators(const LanderEvent *ev)
{
    intptr_t id = ev->arg[0];
    if (id == (intptr_t)T3_DESTR(0x93) || id == (intptr_t)T3_DESTR(0x94) || id == (intptr_t)T3_DESTR(0xbb) ||
        id == (intptr_t)T3_DESTR(0x10))
        Object_SetDead((ObjId)id);
    if (ev->arg[0] == (intptr_t)T3_DESTR(0x94)) {
        AnimSeq_Start(T3_GUARD(0x21), t3_seq310, 0);    // 0x10001120
        AnimSeq_Start(T3_GUARD(0x22), t3_seq310, 0);
    }
    if (ev->arg[0] != (intptr_t)T3_DESTR(0x93))
        return;
    AnimSeq_Start(T3_GUARD(0x1e), t3_seq338, 0);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_Guard1fTick);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_Guard20Tick);
    AnimSeq_Start(T3_GUARD(0x25), t3_seq460, 0);
    AnimSeq_Start(T3_GUARD(0x26), t3_seq438, 0);
    AnimSeq_Start(T3_GUARD(0x27), t3_seq470, 0);
    AnimSeq_Start(T3_GUARD(0x23), t3_seq448, 0);
    AnimSeq_Start(T3_GUARD(0x24), t3_seq458, 0);
}

// 0x100023c0  EV_TICK_1S: guard 0x1f follows 6 s later
static void T3_Guard1fTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.guard1fTicks != 6)
        return;
    EventType_RemoveHandler(EV_TICK_1S, T3_Guard1fTick);
    AnimSeq_Start(T3_GUARD(0x1f), t3_seq348, 0);
}

// 0x10002400  EV_TICK_1S: guard 0x20 follows 9 s later
static void T3_Guard20Tick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.guard20Ticks != 9)
        return;
    EventType_RemoveHandler(EV_TICK_1S, T3_Guard20Tick);
    AnimSeq_Start(T3_GUARD(0x20), t3_seq358, 0);
}

static void T3_Alarm(void);

// 0x10002440  EV_COLLISION: something bumps a crate 0x30012..15 -> alarm (once)
static void T3_OnCrateBumped(const LanderEvent *ev)
{
    intptr_t other = ev->arg[1];
    if (other == (intptr_t)T3_ANIM(0x12) || other == (intptr_t)T3_ANIM(0x13) || other == (intptr_t)T3_ANIM(0x14) ||
        other == (intptr_t)T3_ANIM(0x15)) {
        T3_Alarm();                                      // 0x10002550
        EventType_RemoveHandler(EV_COLLISION, T3_OnCrateBumped);
    }
}

// 0x10002480  EV_DESTROYED: the first of the barracks 0x80054..0x80087 -> guards 0x18/0x19 come out
static void T3_OnDestroyedBarracks(const LanderEvent *ev)
{
    intptr_t id = ev->arg[0];
    if (id < (intptr_t)T3_DESTR(0x54) || id > (intptr_t)T3_DESTR(0x87))
        return;
    if (++st.barracksHits != 1)
        return;
    Object_SetProperty(T3_GUARD(0x18), PROP_STATE, st.barracksHits);
    Object_SetProperty(T3_GUARD(0x19), PROP_STATE, 1);
    AnimPlay(T3_ANIM(0x143), 0xb, 0);
    AnimPlay(T3_ANIM(0x144), 0xc, 0);
    Sound_PlayOnce(T3_ANIM(0x143), 0xab);
    AnimPlay(T3_ANIM(0x7c), 0xa, 0);
    AnimSeq_Start(T3_GUARD(0x18), t3_seq2f0, 0);
    AnimSeq_Start(T3_GUARD(0x19), t3_seq2f0, 0);
    st.door7cShut = 0;
}

static void T3_GuardsRise(void);

// 0x10002550  the base alarm: siren on the lander (every call); the first time: gun doors 0x300af..b4
// open, turrets 0x1f..24, 0xfc..0x101, 0xac, 0xae activate, the hangar guards rise
static void T3_Alarm(void)
{
    Sound_PlayLoop(T3_LANDER, 0x320, 4);
    if (st.alarm == 1)
        return;
    st.alarm = 1;
    AnimPlay(T3_ANIM(0xb7), 0xf, 0);
    Object_SetProperty(T3_TURRET(0xae), PROP_STATE, 1);
    T3_GuardsRise();                                     // 0x10002f30
    for (int32_t i = 0; i < 6; i++) {                    // (loop variable 0x1000c328)
        AnimPlay(T3_ANIM(0xaf) + (ObjId)i, 0x3b, 0);
        Sound_PlayOnce(T3_ANIM(0xaf) + (ObjId)i, 0x3f7);
        Object_SetProperty(T3_TURRET(0x1f) + (ObjId)i, PROP_STATE, 1);
        Object_SetProperty(T3_TURRET(0xfc) + (ObjId)i, PROP_STATE, 1);
    }
    AnimPlay(T3_ANIM(0xad), 0x3c, 0);
    Sound_PlayOnce(T3_ANIM(0xad), 0x3f7);
    Object_SetProperty(T3_TURRET(0xac), PROP_STATE, 1);
}

// 0x10002650  EV_TRIGGER_ENTER: door 0x3002e/2f closes behind the lander (0x40145); the hatch
// 0x40150 / 0x40151 opens the hatch doors, wakes two anim turrets and sounds the alarm
static void T3_OnTriggerHatch(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x145)) {
        AnimSetUpdateInterval(T3_ANIM(0x2e), 2);
        AnimSetUpdateInterval(T3_ANIM(0x2f), 2);
        AnimPlay(T3_ANIM(0x2e), 7, 4);
        AnimPlay(T3_ANIM(0x2f), 8, 4);
        Sound_PlayOnce(T3_ANIM(0x2e), 0xab);
        Object_SetProperty(T3_SWITCH(0x131), PROP_STATE, 0);
    }
    if (ev->arg[1] != (intptr_t)T3_TRIGGER(0x150) && ev->arg[1] != (intptr_t)T3_TRIGGER(0x151))
        return;
    if (st.hatchAlarmDone == 0) {
        AnimPlay(T3_ANIM(0xb5), 5, 0);
        AnimPlay(T3_ANIM(0xb6), 6, 0);
        Object_SetProperty(T3_ANIMTUR(0xdb), PROP_STATE, 1);
        AnimPlay(T3_ANIMTUR(0xdb), 3, 0x60);
        Object_SetProperty(T3_ANIMTUR(0xda), PROP_STATE, 1);
        AnimPlay(T3_ANIMTUR(0xda), 3, 0x60);
        Object_SetProperty(T3_TURRET(0xab), PROP_STATE, 1);
        st.hatchAlarmDone = 1;
    }
    T3_Alarm();
}

// 0x10002760  EV_ANIM_FINISHED (added on every lift-switch use, never removed): plays the lift stop
// sound for any finished animation; when the lift 0x300d2 itself stops: switch back off, state
// 1 (rising) -> 3 (up), anything else -> 0
static void T3_OnLiftStopped(const LanderEvent *ev)
{
    Sound_StopObject(T3_ANIM(0xd2));
    Sound_PlayOnce(T3_ANIM(0xd2), 0xd0);
    if (ev->arg[0] != (intptr_t)T3_ANIM(0xd2))
        return;
    Object_SetProperty(T3_SWITCH(0xbc), PROP_STATE, 0);
    st.lift = (st.lift == 1) ? 3 : 0;
}

// 0x100027c0  EV_SWITCH (any state): door 0x3002e/2f (0xa0131), guard pair 0x1c/0x1d (0xa00dc), lift
// 0x300d2 (0xa00bc: up from 0, down from 3)
static void T3_OnSwitchMisc(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T3_SWITCH(0x131)) {
        AnimPlay(T3_ANIM(0x2e), 7, 0);
        AnimPlay(T3_ANIM(0x2f), 8, 0);
        Sound_PlayOnce(T3_ANIM(0x2f), 0xab);
    }
    if (ev->arg[0] == (intptr_t)T3_SWITCH(0xdc)) {
        AnimPlay(T3_ANIM(0xdf), 0x14, 0);
        Sound_PlayOnce(T3_ANIM(0xdf), 0xab);
        AnimSeq_Start(T3_GUARD(0x1c), t3_seq318, 0);
        AnimSeq_Start(T3_GUARD(0x1d), t3_seq328, 0);
    }
    if (ev->arg[0] != (intptr_t)T3_SWITCH(0xbc))
        return;
    if (st.lift == 0) {
        AnimPlay(T3_ANIM(0xd2), 0x15, 0);
        Sound_PlayOnce(T3_ANIM(0xd2), 0xce);
        st.lift = 1;
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, T3_OnLiftStopped);
    }
    if (st.lift == 3) {
        AnimPlay(T3_ANIM(0xd2), 0x15, 4);
        Sound_PlayOnce(T3_ANIM(0xd2), 0xce);
        st.lift = 2;
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, T3_OnLiftStopped);
    }
}

// 0x100028f0  EV_TRIGGER_ENTER: proximity doors
static void T3_OnTriggerDoorsIn(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x11)) {
        AnimPlay(T3_ANIM(0x1e), 0x13, 0);
        Sound_PlayOnce(T3_ANIM(0x1e), 0xab);
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0xea)) {
        AnimSetUpdateInterval(T3_ANIM(0xeb), 7);
        AnimPlay(T3_ANIM(0xeb), 0x10, 0);
        Sound_PlayOnce(T3_ANIM(0xeb), 0xa9);
        if (st.gateSeen == 0)
            st.gateSeen = 1;
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x7b)) {
        if (st.door7cShut == 1)
            AnimPlay(T3_ANIM(0x7c), 0xa, 0);
        Sound_PlayOnce(T3_ANIM(0x7c), 0xab);
        st.door7cShut = 0;
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0xdd)) {
        AnimPlay(T3_ANIM(0xde), 0x14, 0);
        Sound_PlayOnce(T3_ANIM(0xde), 0xab);
    }
    for (int32_t n = 0; n < 6; n++)                      // (unrolled) 0x400ed..f2 -> 0x300f6..fb
        if (ev->arg[1] == (intptr_t)T3_TRIGGER(0xed + n))
            AnimPlay(T3_ANIM(0xf6 + n), 0xd, 0);
}

// 0x10002a60  EV_TRIGGER_LEAVE: proximity doors close
static void T3_OnTriggerDoorsOut(const LanderEvent *ev)
{
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x154)) {
        AnimPlay(T3_ANIM(0x2e), 7, 4);
        AnimPlay(T3_ANIM(0x2f), 8, 4);
        Sound_PlayOnce(T3_ANIM(0x2e), 0xab);
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0xea)) {
        AnimSetUpdateInterval(T3_ANIM(0xeb), 0x14);
        AnimPlay(T3_ANIM(0xeb), 0x10, 4);
        Sound_PlayOnce(T3_ANIM(0xeb), 0xa9);
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x7b) && st.door7cShut == 1) {
        AnimPlay(T3_ANIM(0x7c), 0xa, 4);
        Sound_PlayOnce(T3_ANIM(0x7c), 0xab);
    }
    for (int32_t n = 0; n < 6; n++)                      // (unrolled)
        if (ev->arg[1] == (intptr_t)T3_TRIGGER(0xed + n))
            AnimPlay(T3_ANIM(0xf6 + n), 0xd, 4);
}

static void T3_GateClose(const LanderEvent *ev);

// 0x10002ba0  EV_TICK_1S: the entrance doors open by themselves every 7 s ...
static void T3_GateOpen(const LanderEvent *ev)
{
    (void)ev;
    if (++st.gateTicks <= 6)
        return;
    st.gateTicks = 0;
    EventType_RemoveHandler(EV_TICK_1S, T3_GateOpen);
    AnimPlay(T3_ANIM(0x14c), 1, 0);
    Sound_PlayOnce(T3_ANIM(0x14c), 0xab);
    Sound_PlayOnce(T3_ANIM(0x14e), 0xab);
    AnimPlay(T3_ANIM(0x14d), 1, 0);
    AnimPlay(T3_ANIM(0x14e), 2, 0);
    AnimPlay(T3_ANIM(0x14f), 2, 0);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_GateClose);
}

// 0x10002c50  ... and close 6 s later
static void T3_GateClose(const LanderEvent *ev)
{
    (void)ev;
    if (++st.gateTicks <= 5)
        return;
    st.gateTicks = 0;
    EventType_RemoveHandler(EV_TICK_1S, T3_GateClose);
    AnimPlay(T3_ANIM(0x14c), 1, 4);
    Sound_PlayOnce(T3_ANIM(0x14c), 0xab);
    Sound_PlayOnce(T3_ANIM(0x14e), 0xab);
    AnimPlay(T3_ANIM(0x14d), 1, 4);
    AnimPlay(T3_ANIM(0x14e), 2, 4);
    AnimPlay(T3_ANIM(0x14f), 2, 4);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_GateOpen);
}

static void T3_HatchTick(const LanderEvent *ev);
static void T3_HatchClosed(const LanderEvent *ev);

// 0x10002d00  EV_TRIGGER_ENTER: compares the *entering object* with destructible 0x8014a (never
// true for the lander): hatch 0x300b5/b6 opens for 10 s
static void T3_OnTriggerHatchObject(const LanderEvent *ev)
{
    if (st.hatchReady == 0 || ev->arg[0] != (intptr_t)T3_DESTR(0x14a))
        return;
    AnimPlay(T3_ANIM(0xb5), 5, 0);
    AnimPlay(T3_ANIM(0xb6), 6, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T3_OnTriggerHatchObject);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_HatchTick);
    st.hatchTicks = 10;
    st.hatchReady = 0;
}

// 0x10002d80  EV_TICK_1S
static void T3_HatchTick(const LanderEvent *ev)
{
    (void)ev;
    if (--st.hatchTicks != 0)
        return;
    EventType_RemoveHandler(EV_TICK_1S, T3_HatchTick);
    AnimPlay(T3_ANIM(0xb5), 5, 4);
    AnimPlay(T3_ANIM(0xb6), 6, 4);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T3_HatchClosed);
}

// 0x10002de0  EV_ANIM_FINISHED (any)
static void T3_HatchClosed(const LanderEvent *ev)
{
    (void)ev;
    EventType_RemoveHandler(EV_ANIM_FINISHED, T3_HatchClosed);
    st.hatchReady = 1;
}

static void T3_OnRoofHatchStopped(const LanderEvent *ev);

// 0x10002e00  EV_SWITCH (any switch first forces 0xa0130 / 0xa012f on): 0xa012f / 0xa0130 toggle the
// roof hatch 0x300bd unless it is moving
static void T3_OnSwitchRoofHatch(const LanderEvent *ev)
{
    Object_SetProperty(T3_SWITCH(0x130), PROP_STATE, 1);
    Object_SetProperty(T3_SWITCH(0x12f), PROP_STATE, 1);
    if (st.hatchMoving == 1)
        return;
    if (ev->arg[0] != (intptr_t)T3_SWITCH(0x12f) && ev->arg[0] != (intptr_t)T3_SWITCH(0x130))
        return;
    if (st.hatchOpen == 1) {
        AnimPlay(T3_ANIM(0xbd), 4, 0);
        Sound_PlayOnce(T3_ANIM(0xbd), 0xce);
        st.hatchOpen = 0;
    } else {
        AnimPlay(T3_ANIM(0xbd), 4, 4);
        Sound_PlayOnce(T3_ANIM(0xbd), 0xce);
        st.hatchOpen = 1;
    }
    st.hatchMoving = 1;
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T3_OnRoofHatchStopped);
}

// 0x10002ed0  EV_ANIM_FINISHED of 0x300bd: both switches back off
static void T3_OnRoofHatchStopped(const LanderEvent *ev)
{
    if (ev->arg[0] != (intptr_t)T3_ANIM(0xbd))
        return;
    EventType_RemoveHandler(EV_ANIM_FINISHED, T3_OnRoofHatchStopped);
    Sound_PlayOnce(T3_ANIM(0xbd), 0xd0);
    st.hatchMoving = 0;
    Object_SetProperty(T3_SWITCH(0x12f), PROP_STATE, 0);
    Object_SetProperty(T3_SWITCH(0x130), PROP_STATE, 0);
}

// 0x10002f30  (alarm) hangar doors 0x300fa/fb open, guards 0x1a / 0x1b rise and activate
static void T3_GuardsRise(void)
{
    AnimPlay(T3_ANIM(0xfb), 0xd, 0);
    AnimPlay(T3_ANIM(0xfa), 0xd, 0);
    AnimSeq_Start(T3_GUARD(0x1a), t3_seq418, 0);
    AnimSeq_Start(T3_GUARD(0x1b), t3_seq428, 0);
    Object_SetProperty(T3_GUARD(0x1a), PROP_STATE, 1);
    Object_SetProperty(T3_GUARD(0x1b), PROP_STATE, 1);
}

// 0x10002fa0  EV_TICK_1S: fans 0x30090 / 0x30091 run backwards from second 26 / 33
static void T3_FanTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.fanTicks > 100)
        EventType_RemoveHandler(EV_TICK_1S, T3_FanTick);
    if (st.fanTicks == 0x1a)
        AnimPlay(T3_ANIM(0x90), 9, 4);
    if (st.fanTicks == 0x21)
        AnimPlay(T3_ANIM(0x91), 9, 4);
}

// 0x10003000  EV_POD_HOOKED (any pod): turrets 0x135..0x139 appear, the exit objective, alarm
static void T3_OnPodHooked(const LanderEvent *ev)
{
    (void)ev;
    Pod_Script_SetupMass(T3_POD, 1000);
    Pod_Script_SetupDrag(T3_POD, 0x1333);
    EventType_RemoveHandler(EV_POD_HOOKED, T3_OnPodHooked);
    for (int32_t n = 0x135; n <= 0x139; n++) {           // (loop variable 0x1000c328)
        Object_SetAlive(T3_TURRET(n));
        WindowSetText(st.win, st.text[5]);
    }
    T3_Alarm();
    Object_SetDead(T3_DESTR(0x47));
    Object_SetProperty(T3_TURRET(0xd1), PROP_STATE, 1);
}

// 0x100030b0  EV_TICK_1S (until trigger 0x4001d): objective line blinks entries 0 / 1
static void T3_BlinkTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.blink == 0) {
        WindowSetText(st.win, st.text[0]);
        st.blinkDown = 0;
        st.blink++;
        return;
    }
    if (st.blink == t3_blinkPeriod) {
        WindowSetText(st.win, st.text[1]);
        st.blinkDown = 1;
        st.blink--;
        return;
    }
    if (st.blinkDown == 0)
        st.blink++;
    else
        st.blink--;
}

// 0x10003140  EV_TRIGGER_ENTER / LEAVE 0x4001d: the entrance objective inside, blinking outside
static void T3_OnTriggerSubEntrance(const LanderEvent *ev)
{
    if (ev->arg[1] != (intptr_t)T3_TRIGGER(0x1d))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        EventType_RemoveHandler(EV_TICK_1S, T3_BlinkTick);
        WindowSetText(st.win, st.text[3]);
        return;
    }
    EventType_AddHandler(EV_TICK_1S, NULL, T3_BlinkTick);
}

// 0x10003190  EV_TRIGGER_ENTER / LEAVE: lobby 0x40045 the objective line (back to entry 3 on leaving,
// unless still in 0x40046)
static void T3_OnTriggerLobby(const LanderEvent *ev)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x45))
            WindowSetText(st.win, st.text[4]);
        if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x46))
            st.inLobby = 1;
        return;
    }
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x45) && st.inLobby == 0)
        WindowSetText(st.win, st.text[3]);
    if (ev->arg[1] == (intptr_t)T3_TRIGGER(0x46))
        st.inLobby = 0;
}

// 0x10003220  EV_TICK_100MS (inside a live field): -10 shield
static void T3_FieldDamage(const LanderEvent *ev)
{
    (void)ev;
    Object_SetProperty(T3_LANDER, PROP_HITPOINTS, (int32_t)Object_GetProperty(T3_LANDER, PROP_HITPOINTS) - 10);
}

// 0x10003250  (re)evaluate the field damage. Being in a live field while the damage handler is
// already registered *removes* it (as in the original).
static void T3_UpdateFieldDamage(void)
{
    int32_t hurt = 0;
    if (st.inField16 && st.fieldSetA)
        hurt = 1;
    if (st.inField17 && st.fieldSetA)
        hurt = 1;
    if (st.inField18 && st.fieldSetA)
        hurt = 1;
    if (st.inField19 && !st.fieldSetA && st.fieldAlt)
        hurt = 1;
    if (st.inField26 && !st.fieldSetA && !st.fieldAlt)
        hurt = 1;
    if (st.inField25 && !st.fieldSetA)
        hurt = 1;
    if (hurt && st.fieldDamageOn == 0) {
        EventType_AddHandler(EV_TICK_100MS, NULL, T3_FieldDamage);
        st.fieldDamageOn = 1;
        return;
    }
    if (st.fieldDamageOn != 0) {
        EventType_RemoveHandler(EV_TICK_100MS, T3_FieldDamage);
        st.fieldDamageOn = 0;
    }
}

// 0x10003330  EV_TRIGGER_ENTER / LEAVE: field triggers (leaving 0x40025 clears the 0x40026 flag
// and vice versa, as in the original)
static void T3_OnTriggerField(const LanderEvent *ev)
{
    intptr_t trig = ev->arg[1];
    if (ev->type == EV_TRIGGER_ENTER) {
        if (trig == (intptr_t)T3_TRIGGER(0x16))
            st.inField16 = 1;
        else if (trig == (intptr_t)T3_TRIGGER(0x17))
            st.inField17 = 1;
        else if (trig == (intptr_t)T3_TRIGGER(0x18))
            st.inField18 = 1;
        else if (trig == (intptr_t)T3_TRIGGER(0x19))
            st.inField19 = 1;
        else if (trig == (intptr_t)T3_TRIGGER(0x26))
            st.inField26 = 1;
        else if (trig == (intptr_t)T3_TRIGGER(0x25))
            st.inField25 = 1;
    } else {
        if (trig == (intptr_t)T3_TRIGGER(0x16))
            st.inField16 = 0;
        else if (trig == (intptr_t)T3_TRIGGER(0x17))
            st.inField17 = 0;
        else if (trig == (intptr_t)T3_TRIGGER(0x18))
            st.inField18 = 0;
        else if (trig == (intptr_t)T3_TRIGGER(0x19))
            st.inField19 = 0;
        else if (trig == (intptr_t)T3_TRIGGER(0x25))
            st.inField26 = 0;
        else if (trig == (intptr_t)T3_TRIGGER(0x26))
            st.inField25 = 0;
    }
    T3_UpdateFieldDamage();                              // (tail jump)
}

static void T3_OnSwitchFields(const LanderEvent *ev);

// 0x10003430  EV_TICK_1S (after switch 0xa0133): 50 s later switch 0xa0132 turns itself back on
static void T3_FieldTimerTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.fieldTimer != 0x32)
        return;
    st.fieldTimerOn = 0;
    Object_SetProperty(T3_SWITCH(0x132), PROP_STATE, 1);
    LanderEvent fake = { EV_SWITCH, { (intptr_t)T3_SWITCH(0x132), 1 } };   // (type left unset)
    T3_OnSwitchFields(&fake);                            // 0x100034a0 called directly
    st.fieldTimer = 0;
    T3_UpdateFieldDamage();
    EventType_RemoveHandler(EV_TICK_1S, T3_FieldTimerTick);
}

// 0x100034a0  EV_SWITCH: field switches. 0xa0132 on: field set A (destroy level 0, barriers
// 0x80040..43 up); 0xa0133 on: set B for 50 s (level 1, or 2 with the alternative); 0xa0134
// toggles the alternative (level 3 while on). 0xa0132 / 0xa0133 cannot be switched off.
static void T3_OnSwitchFields(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T3_SWITCH(0x132)) {
        if (ev->arg[1] != 1) {
            Object_SetProperty(T3_SWITCH(0x132), PROP_STATE, 1);
            return;
        }
        if (st.fieldTimerOn) {
            st.fieldTimerOn = 0;
            EventType_RemoveHandler(EV_TICK_1S, T3_FieldTimerTick);
        }
        Object_AdvanceDestroyLevel(T3_FIELD_A, -st.fieldLevel);
        Object_AdvanceDestroyLevel(T3_FIELD_B, -st.fieldLevel);
        st.fieldLevel = 0;
        Object_SetAlive(T3_DESTR(0x40));
        Object_SetAlive(T3_DESTR(0x41));
        Object_SetAlive(T3_DESTR(0x42));
        Object_SetAlive(T3_DESTR(0x43));
        Object_SetDead(st.fieldAlt ? T3_DESTR(0x44) : T3_DESTR(0x1b));
        Object_SetDead(T3_DESTR(0x1a));
        Object_SetProperty(T3_SWITCH(0x133), PROP_STATE, 0);
        st.fieldSetA = 1;
        return;
    }
    if (ev->arg[0] == (intptr_t)T3_SWITCH(0x133)) {
        if (ev->arg[1] != 1) {
            Object_SetProperty(T3_SWITCH(0x133), PROP_STATE, 1);
            return;
        }
        if (st.fieldAlt) {
            Object_AdvanceDestroyLevel(T3_FIELD_A, 2 - st.fieldLevel);
            Object_AdvanceDestroyLevel(T3_FIELD_B, 2 - st.fieldLevel);
            st.fieldLevel = 2;
        } else {
            Object_AdvanceDestroyLevel(T3_FIELD_A, 1 - st.fieldLevel);
            Object_AdvanceDestroyLevel(T3_FIELD_B, 1 - st.fieldLevel);
            st.fieldLevel = 1;
        }
        Object_SetDead(T3_DESTR(0x40));
        Object_SetDead(T3_DESTR(0x41));
        Object_SetDead(T3_DESTR(0x42));
        Object_SetDead(T3_DESTR(0x43));
        Object_SetAlive(st.fieldAlt ? T3_DESTR(0x44) : T3_DESTR(0x1b));
        Object_SetAlive(T3_DESTR(0x1a));
        Object_SetProperty(T3_SWITCH(0x132), PROP_STATE, 0);
        st.fieldTimerOn = 1;
        EventType_AddHandler(EV_TICK_1S, NULL, T3_FieldTimerTick);
        st.fieldSetA = 0;
        return;
    }
    if (ev->arg[0] != (intptr_t)T3_SWITCH(0x134))
        return;
    st.fieldAlt = 1 - st.fieldAlt;
    if (ev->arg[1] == 1) {
        Object_AdvanceDestroyLevel(T3_FIELD_A, 3 - st.fieldLevel);
        Object_AdvanceDestroyLevel(T3_FIELD_B, 3 - st.fieldLevel);
        st.fieldLevelSaved = st.fieldLevel;
        st.fieldLevel = 3;
        if (st.fieldSetA != 0)
            return;
        Object_SetDead(T3_DESTR(0x1b));
        Object_SetAlive(T3_DESTR(0x44));
        return;
    }
    if (st.fieldLevel == 3) {
        Object_AdvanceDestroyLevel(T3_FIELD_A, st.fieldLevelSaved - 3);
        Object_AdvanceDestroyLevel(T3_FIELD_B, st.fieldLevelSaved - st.fieldLevel);
        st.fieldLevel = st.fieldLevelSaved;
    }
    if (st.fieldSetA != 0)
        return;
    Object_SetDead(T3_DESTR(0x44));
    Object_SetAlive(T3_DESTR(0x1b));
}

// 0x10003880  EV_PICKUP: shield 0x130002/03, cash 0x13001c (large) / 0x1300cb, 0x1300cc (medium),
// missiles 0x130000/01 (the cash voice plays before the cash is added)
static void T3_OnPickup(const LanderEvent *ev)
{
    if (ev->arg[0] == (intptr_t)T3_PICKUP(0x02) || ev->arg[0] == (intptr_t)T3_PICKUP(0x03)) {
        int32_t shield = (int32_t)Object_GetProperty(T3_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(T3_LANDER, 50);
        Object_SetProperty(T3_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if (ev->arg[0] == (intptr_t)T3_PICKUP(0x1c)) {
        Voice_CashAcquired(T3_LANDER);
        Lander_SetCash(T3_LANDER, Lander_GetCash(T3_LANDER) + gLargeCash);
    }
    if (ev->arg[0] == (intptr_t)T3_PICKUP(0xcb) || ev->arg[0] == (intptr_t)T3_PICKUP(0xcc)) {
        Voice_CashAcquired(T3_LANDER);
        Lander_SetCash(T3_LANDER, Lander_GetCash(T3_LANDER) + gMediumCash);
    }
    if (ev->arg[0] == (intptr_t)T3_PICKUP(0x00) || ev->arg[0] == (intptr_t)T3_PICKUP(0x01)) {
        if (Lander_AddMissiles(T3_LANDER, 2) == 0) {
            Voice_MissilesFull(T3_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(T3_LANDER);
    }
}

// 0x100039b0  armour / explosion sizes / idle sounds of the base defences
static void T3_SetupDefences(void)
{
    for (int32_t n = 0x1f; n <= 0x24; n++) {
        Object_SetProperty(T3_TURRET(n), PROP_ARMOUR, 0x2d);
        Object_SetProperty(T3_TURRET(n), PROP_EXPLOSION, 0x46);
    }
    for (int32_t n = 0xab; n <= 0xac; n++)
        Object_SetProperty(T3_TURRET(n), PROP_ARMOUR, 0x23);
    Object_SetProperty(T3_TURRET(0xae), PROP_ARMOUR, 0x23);
    for (int32_t n = 0xfc; n <= 0x127; n++)
        Object_SetProperty(T3_TURRET(n), PROP_ARMOUR, 0x23);
    for (int32_t n = 0xd3; n <= 0xd6; n++) {
        Object_SetProperty(T3_TURRET(n), PROP_ARMOUR, 0x3c);
        Object_SetProperty(T3_TURRET(n), PROP_EXPLOSION, 0x50);
        Sound_PlayLoop(T3_TURRET(n), 0x83, -1);
    }
    Object_SetProperty(T3_TURRET(0xd1), PROP_ARMOUR, 0x3c);
    Object_SetProperty(T3_TURRET(0xd1), PROP_EXPLOSION, 0x50);
    for (int32_t id = T3_ANIMTUR(0xda); id <= (int32_t)T3_ANIMTUR(0xdb); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = T3_GUARD(0x14); id <= (int32_t)T3_GUARD(0x27); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x2d);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
        Sound_PlayLoop((ObjId)id, 0x83, -1);
    }
    for (int32_t n = 0xc5; n <= 0xca; n++) {
        Object_SetProperty(T3_TURRET(n), PROP_ARMOUR, 0x64);
        Object_SetProperty(T3_TURRET(n), PROP_EXPLOSION, 0x8c);
    }
    for (int32_t id = T3_FUELDUMP(0x88); id <= (int32_t)T3_FUELDUMP(0x8d); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = T3_ANIMDESTR(0xe0); id <= (int32_t)T3_ANIMDESTR(0xe4); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x28);
        Sound_PlayLoop((ObjId)id, 0xd3, -1);
    }
    for (int32_t id = T3_DESTR(0xe5); id <= (int32_t)T3_DESTR(0xe9); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = T3_DESTR(0x54); id <= (int32_t)T3_DESTR(0x7a); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = T3_DESTR(0x7d); id <= (int32_t)T3_DESTR(0x87); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(T3_DESTR(0x10), PROP_ARMOUR, 0x1e);
    Object_SetProperty(T3_DESTR(0x1a), PROP_ARMOUR, 0xff);
    Object_SetProperty(T3_DESTR(0x1b), PROP_ARMOUR, 0xff);
    Object_SetProperty(T3_DESTR(0x44), PROP_ARMOUR, 0xff);
    Object_SetProperty(T3_DESTR(0x47), PROP_ARMOUR, 0xff);
}

// 0x10003be0  EV_TRIGGER_ENTER / LEAVE: guard posts - wake the post's guards (and start the
// looping patrols of 0x10e0014..17) on entering, sleep them (and stop the patrols) on leaving
static void T3_OnTriggerGuardPost(const LanderEvent *ev)
{
    intptr_t trig = ev->arg[1];
    if (ev->type == EV_TRIGGER_ENTER) {
        if (trig == (intptr_t)T3_TRIGGER(0x07)) {
            for (int32_t i = 0; i < 4; i++)              // (loop variable 0x1000c328)
                MobObj_Wake((i + 0x14) & 0xffff);
            AnimSeq_Start(T3_GUARD(0x14), t3_seq380, 1);
            AnimSeq_Start(T3_GUARD(0x15), t3_seq360, 1);
            AnimSeq_Start(T3_GUARD(0x16), t3_seq360, 1);
            AnimSeq_Start(T3_GUARD(0x17), t3_seq380, 1);
        } else if (trig == (intptr_t)T3_TRIGGER(0x08)) {
            for (int32_t i = 0; i < 4; i++)
                MobObj_Wake((i + 0x18) & 0xffff);
        } else if (trig == (intptr_t)T3_TRIGGER(0x09)) {
            for (int32_t i = 0; i < 7; i++)
                MobObj_Wake((i + 0x21) & 0xffff);
        } else if (trig == (intptr_t)T3_TRIGGER(0x0a)) {
            for (int32_t i = 0; i < 3; i++)
                MobObj_Wake((i + 0x1e) & 0xffff);
        } else if (trig == (intptr_t)T3_TRIGGER(0x0b)) {
            MobObj_Wake(0x1c);
            MobObj_Wake(0x1d);
        }
        return;
    }
    if (trig == (intptr_t)T3_TRIGGER(0x07)) {
        for (int32_t i = 0; i < 4; i++)
            MobObj_Sleep((i + 0x14) & 0xffff);
        AnimSeq_Stop(T3_GUARD(0x14));                  // 0x10001190
        AnimSeq_Stop(T3_GUARD(0x15));
        AnimSeq_Stop(T3_GUARD(0x16));
        AnimSeq_Stop(T3_GUARD(0x17));
    } else if (trig == (intptr_t)T3_TRIGGER(0x08)) {
        for (int32_t i = 0; i < 4; i++)
            MobObj_Sleep((i + 0x18) & 0xffff);
    } else if (trig == (intptr_t)T3_TRIGGER(0x09)) {
        for (int32_t i = 0; i < 7; i++)
            MobObj_Sleep((i + 0x21) & 0xffff);
    } else if (trig == (intptr_t)T3_TRIGGER(0x0a)) {
        for (int32_t i = 0; i < 3; i++)
            MobObj_Sleep((i + 0x1e) & 0xffff);
    } else if (trig == (intptr_t)T3_TRIGGER(0x0b)) {
        MobObj_Sleep(0x1c);
        MobObj_Sleep(0x1d);
    }
}

// 0x10003e60  set-up (lander start only)
static void T3_Setup(void)
{
    st.text = t3_text[gLanguage];
    SetGravity(13);
    gAirResistance = 0xc;
    gMainThrottlePower = 0x1a;
    gNbWaypoints = t3_nbWaypoints;
    gWaypoints = t3_waypoints;
    Media_AudioPlay(0);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    T3_SetupDefences();                                  // 0x100039b0
    gTurretTypes[22] = &t3_turretType22;
    gTurretTypes[21] = &t3_turretType21;
    gTurretTypes[0] = &t3_turretType0;
    gTurretTypes[2] = &t3_turretType2;
    gTurretTypes[5] = &t3_turretType5;
    gTurretTypes[4] = &t3_turretType4;
    gTurretTypes[10] = &t3_turretType10;
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x100037f0
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerAirlockIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T3_OnTriggerAirlockOut);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_BlinkTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerHatchObject);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_ConvoyTick);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_GateOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerLobby);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T3_OnTriggerLobby);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerField);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T3_OnTriggerField);
    EventType_AddHandler(EV_SWITCH, NULL, T3_OnSwitchRoofHatch);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerHatch);
    EventType_AddHandler(EV_SWITCH, NULL, T3_OnSwitchMisc);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerDoorsIn);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T3_OnTriggerDoorsOut);
    EventType_AddHandler(EV_DESTROYED, NULL, T3_OnDestroyedBarracks);
    EventType_AddHandler(EV_DESTROYED, NULL, T3_OnDestroyedGenerators);
    EventType_AddHandler(EV_COLLISION, NULL, T3_OnCrateBumped);
    EventType_AddHandler(EV_TICK_1S, NULL, T3_FanTick);
    EventType_AddHandler(EV_POD_HOOKED, NULL, T3_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerSubEntrance);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T3_OnTriggerSubEntrance);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T3_OnTriggerGuardPost);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T3_OnTriggerGuardPost);
    EventType_AddHandler(EV_SWITCH, NULL, T3_OnSwitchFields);
    EventType_AddHandler(EV_PICKUP, NULL, T3_OnPickup);
    for (int32_t id = T3_GUARD(0x14); id <= (int32_t)T3_GUARD(0x27); id++)   // (loop variable 0x1000c328)
        Object_SetProperty((ObjId)id, PROP_TARGET, T3_LANDER);
    for (int32_t n = 0x135; n <= 0x13c; n++)
        Object_SetDead(T3_TURRET(n));
    Sound_PlayLoop(T3_VEHICLE, 0x88, -1);
    MobObj_Wake(0xf);
    MobObj_Wake(0x28);
    MobObj_Wake(0x29);
    MobObj_Wake(0x12);
    MobObj_Wake(7);
    MobObj_Wake(0x10);
    MobObj_Wake(0x11);
    MobObj_Wake(8);
    MobObj_Wake(9);
    MobObj_Wake(0xe);
    MobObj_Wake(0x13);
    Object_SetProperty(T3_ANIMTUR(0xda), PROP_STATE, 0);
    Object_SetProperty(T3_ANIMTUR(0xdb), PROP_STATE, 0);
    for (int32_t n = 0xfc; n <= 0x101; n++)               // (unrolled)
        Object_SetProperty(T3_TURRET(n), PROP_STATE, 0);
    Object_SetProperty(T3_TURRET(0xab), PROP_STATE, 0);
    Object_SetProperty(T3_TURRET(0xac), PROP_STATE, 0);
    Object_SetProperty(T3_TURRET(0xae), PROP_STATE, 0);
    Object_SetProperty(T3_TURRET(0xd1), PROP_STATE, 0);
    Object_SetProperty(T3_SWITCH(0x132), PROP_STATE, 1);
    Sound_PlayLoop(T3_DESTR(0x40), 0xc6, -1);
    Sound_PlayLoop(T3_DESTR(0x41), 0xc6, -1);
    Sound_PlayLoop(T3_DESTR(0x42), 0xc6, -1);
    Sound_PlayLoop(T3_DESTR(0x43), 0xc6, -1);
    Sound_PlayLoop(T3_DESTR(0x1b), 0xc6, -1);
    Sound_PlayLoop(T3_DESTR(0x44), 0xc6, -1);
    Sound_PlayLoop(T3_DESTR(0x1a), 0xc6, -1);
    for (int32_t n = 0x1f; n <= 0x24; n++)
        Object_SetProperty(T3_TURRET(n), PROP_STATE, 0);
    for (int32_t n = 0x48; n <= 0x4d; n++) {              // (unrolled) generators
        AnimPlay(T3_ANIM(n), 0xe, 0x81);
        Sound_PlayLoop(T3_ANIM(n), 0xb6, -1);
    }
    AnimPlay(T3_ANIM(0x2b), 0x40, 0x81);
    AnimPlay(T3_ANIM(0x2c), 0x40, 0x81);
    AnimPlay(T3_ANIM(0x2d), 0x40, 0x81);
    for (int32_t n = 0xe0; n <= 0xe4; n++)                // (unrolled)
        AnimPlay(T3_ANIMDESTR(n), 0x40, 0x81);
    AnimPlay(T3_GUARD(0x28), 0x41, 0x60);
    AnimPlay(T3_GUARD(0x29), 0x42, 0x60);
    AnimPlay(T3_ANIM(0x143), 0xb, 0);
    AnimPlay(T3_ANIM(0x144), 0xc, 0);
    AnimPlay(T3_ANIM(0x7c), 0xa, 0);
    AnimPlay(T3_ANIM(0x12), 0x11, 0x82);
    AnimPlay(T3_ANIM(0x14), 0x11, 0x82);
    AnimPlay(T3_ANIM(0x13), 0x12, 0x82);
    AnimPlay(T3_ANIM(0x15), 0x12, 0x82);
    AnimPlay(T3_ANIM(0xbd), 4, 0);
    AnimPlay(T3_ANIM(0x90), 9, 0);
    AnimPlay(T3_ANIM(0x91), 9, 0);
    AnimSetUpdateInterval(T3_ANIM(0x12), 0x14);
    AnimSetUpdateInterval(T3_ANIM(0x14), 0x12);
    AnimSetUpdateInterval(T3_ANIM(0x13), 0x16);
    AnimSetUpdateInterval(T3_ANIM(0x15), 0x18);
    for (int32_t n = 0x48; n <= 0x4d; n++)                // (unrolled)
        AnimSetUpdateInterval(T3_ANIM(n), 2);
    for (int32_t n = 0x14; n <= 0x17; n++)                // (unrolled)
        AnimSetUpdateInterval(T3_GUARD(n), 8);
    TexAnimPlay(0xc7, 1, 100);
    TexAnimPlay(0xc3, 1, 100);
    for (int32_t n = 0xfc; n <= 0x10b; n++)               // (loop variable 0x1000c328)
        Object_SetProperty(T3_TURRET(n), PROP_TARGET, T3_LANDER);
    for (int32_t id = T3_DESTR(0x54); id <= (int32_t)T3_DESTR(0x87); id++)
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x26);
    Object_SetDead(T3_DESTR(0x44));
    Object_SetDead(T3_DESTR(0x1a));
    Object_SetDead(T3_DESTR(0x1b));
    st.escaping = 0;
    st.convoyTicks = 0;
    AnimSeq_Init();                                      // 0x100011c0
    MissionScore_Init(T3_LANDER, 300000, 7);             // 0x10001710
}

// 0x10004650  LevelStartLevel_Lander (export 0x10001210)
static void T3_StartLander(void)
{
    MobObj_Wake(T3_MOB_LANDER);
    MobObj_Wake(T3_MOB_CAMERA);
    Object_SetProperty(T3_CAMERA, PROP_TARGET, T3_LANDER);
    T3_Setup();
}

// 0x10004690  LevelStartLevel_Flycam and LevelStopLevel (export 0x10001220): empty
static void T3_Nothing(void)
{
}

// 0x10001230
static uint32_t T3_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001200  LevelLoadLevel: two calls of the empty 0x10004690, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t T3_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.airlockSide = 1;                                  // .data initial values
    st.hatchReady = 1;
    st.convoyTicks = 666;
    st.door7cShut = 1;
    st.fieldSetA = 1;
    MissionScore_Reset();
    AnimSeq_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t3_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10009170, .count = 3 * 8, .out = t3_text },
    { 0 },
};

const mission_script_t mission_titan_3 = {
    .name          = "TITAN\\3",
    .get_version   = T3_GetVersion,
    .load          = T3_Load,
    .start_lander  = T3_StartLander,
    .start_flycam  = T3_Nothing,
    .start_animcam = NULL,
    .stop          = T3_Nothing,
    .text          = t3_gametext,
};
