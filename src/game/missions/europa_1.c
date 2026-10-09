// MISSIONS\EUROPA\1\MISSIONR.DLL - faithful C port.
//
// Mission: find the downed craft (waypoint 1) and hook the target container (pod mob 4), find the
// 'Macbeth' colony entrance (waypoint 2), go down into the subterranean colony, find hangar 3
// and the freighter; dropping the container into its cargo bay (any of the triggers
// 0x40055..5d, which watch the pod) stows it; then get out through trigger 0x4000f: 3 s later the
// mission ends, success 2 if done within 900 s of the 1-s ticks, else success 1 (+90000 cash
// either way). Par 900 s, 11 pickups. On the way: a seven-switch combination (0xa004b..51), lift
// platforms, proximity doors, an ambush of anim-turrets that pick explosion mobs as targets
// (trigger 0x40006), headlight zones, bridge and machine switches, interior-light planes.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040. The score library (0x10001060-0x10001604) and AnimSeq
// (0x10004020-0x100041e1, the training_2 code) are in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define E1_LANDER        MOB_ID(OT_LANDER, 0x24)         // 0x01010024
#define E1_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define E1_MOB_CAMERA    1
#define E1_MOB_FLYCAM    0x19
#define E1_MOB_LANDER    0x24
#define E1_CONTAINER     MOB_ID(OT_POD, 4)               // 0x010c0004
#define E1_ATURRET(n)    MOB_ID(OT_ANIMTURRET, (n))      // 0x010e00nn
#define E1_MOBANIM(n)    MOB_ID(OT_ANIMOBJECT, (n))      // 0x010300nn
#define E1_EXPLOSION(n)  MOB_ID(OT_EXPLOSION, (n))       // 0x010900nn
#define E1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define E1_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define E1_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define E1_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn

// ---- static data (.data) --------------------------------------------------------------------------
// 0x10009160 / 0x10009180: AnimSeq sequences {anim, repeat, flags} of the anim-turrets 0x20 / 0x21
static const uint8_t e1_seqAInit[32] = {
    0x19, 0x14, 0x60, 0x1b, 0x01, 0x60, 0x19, 0x05, 0x60, 0x1c, 0x01, 0x60, 0x18, 0x01, 0x60, 0x1b,
    0x01, 0x60, 0x19, 0x02, 0x60, 0x1a, 0x01, 0x60, 0xff,
};
static const uint8_t e1_seqBInit[8] = { 0x19, 0x16, 0x60, 0xff };
static const int32_t e1_containerInit = 3;              // 0x10009188 3 = not yet hooked
// 0x10009190: explosion mob (0x01090005 + n) each ambush turret aims at next
static const int32_t e1_ambushTargets[10] = { 0, 1, 5, 6, 2, 0, 3, 2, 4, 7 };
// 0x100091b8: [language][8] string pointers (strings at 0x10009314-0x10009656). The French [0]
// and [2] are swapped against the English ones; [7] is English in every language.
static const char *e1_text[3][8];
static const int32_t  e1_nbWaypoints = 3;               // 0x10009218
static const Waypoint e1_waypoints[3] = {               // 0x10009220
    { -121600, -57600 },
    { 70400, -83200 },
    { 125440, -121472 },
};
static const TurretType e1_turretType21 = {             // 0x10009238 -> gTurretTypes[21]
    { 1731, 338, 1442, 0 }, 0x02, 1, 2, 50, 160, 0x20, 800, 60000, 0
};
static const TurretType e1_turretType23 = {             // 0x10009258 -> gTurretTypes[23]
    { 50, 29, 279, 0 }, 0x05, 0, 4, 8, 200, 0x20, 512, 12000, 0
};
static const TurretType e1_turretType22 = {             // 0x10009278 -> gTurretTypes[22]
    { 167, -1, 129, 0 }, 0x00, 1, 2, 5, 160, 0x20, 800, 12000, 0
};
static const TurretType e1_turretType27 = {             // 0x10009298 -> gTurretTypes[27]
    { -44, 1, 91, 0 }, 0x05, 0, 4, 3, 100, 0x32, 800, 14000, 0
};
static const TurretType e1_turretType24 = {             // 0x100092b8 -> gTurretTypes[24]
    { 88, -9, 185, 0 }, 0x01, 1, 2, 30, 120, 0x40, 700, 14000, 0
};
static const TurretType e1_turretType25 = {             // 0x100092d8 -> gTurretTypes[25]
    { 2, 29, 288, 0 }, 0x05, 1, 3, 8, 100, 0x20, 800, 14000, 0
};
static const TurretType e1_turretType26 = {             // 0x100092f8 -> gTurretTypes[26]
    { -60, 4, 258, 0 }, 0x01, 0, 2, 10, 100, 0x30, 800, 12000, 0
};

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    uint8_t      seqA[32];      // 0x10009160 (.data, patched by nobody but writable for AnimSeq)
    uint8_t      seqB[8];       // 0x10009180
    int32_t      container;     // 0x10009188 (.data, 3) 1 = hooked, 0 = stowed in the freighter
    int32_t      comboCount;    // 0x1000bd9c switches 0xa004b..51 on
    int32_t      turretKills;   // 0x1000bda0 turrets 0x1d..0x21 destroyed
    int32_t      hangarOpen;    // 0x1000bda4
    int32_t      liftUp;        // 0x1000bda8 platform 0x1030034
    int32_t      lift2Up;       // 0x1000bdac platform 0x1030035
    int32_t      lineIndex;     // 0x1000bdb0 objective shown
    int32_t      inHangar;      // 0x1000bdb4 lander inside trigger 0x400af
    int32_t      targetIndex;   // 0x1000bdb8 into e1_ambushTargets
    int32_t      targetCount2;  // 0x1000bdbc (written, never read)
    int32_t      seconds;       // 0x1000bdc0 1-s ticks since the start
    int32_t      bridgeOn;      // 0x1000bdc4 switch 0xa0022
    int32_t      gate86;        // 0x1000bdc8 switch 0xa0086 used
    int32_t      planeAdded;    // 0x1000bdcc never set: every light-switch event adds a handler
    Window      *win;           // 0x1000bdd0 objective line
    const char *const *text;    // 0x1000c320 = e1_text[gLanguage]
} st;

// 0x10001610  EV_SWITCH 0xa004b..51: each switch turned on moves the indicator 0x1030017/18 one
// step, off moves it back; seven on -> it completes and the switches lock
static void E1_OnSwitchCombo(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)E1_SWITCH(0x4b) || id > (int32_t)E1_SWITCH(0x51))
        return;
    int32_t state = (int32_t)ev->arg[1];
    if (state == 1 && st.comboCount <= 6) {
        AnimPlay(E1_MOBANIM(0x18), 0x1e, 0);
        AnimPlay(E1_MOBANIM(0x17), 0, 0);
        Sound_PlayOnce(E1_MOBANIM(0x17), 0x26d);
        InTime2(500, AnimStop, E1_MOBANIM(0x17), 0);
        InTime2(500, AnimStop, E1_MOBANIM(0x18), 0);
        if (++st.comboCount != 7)
            return;
        InTime3(1000, AnimPlay, E1_MOBANIM(0x18), 0x1d, 0);
        Sound_PlayOnce(E1_MOBANIM(0x18), 0xac);
        for (int32_t i = 0x4b; i <= 0x51; i++)
            Object_SetProperty(E1_SWITCH(i), PROP_STATE, -1);
        EventType_RemoveHandler(EV_SWITCH, E1_OnSwitchCombo);
    } else if (state == 0 && st.comboCount >= 0) {
        AnimPlay(E1_MOBANIM(0x18), 0x1f, 0);
        AnimPlay(E1_MOBANIM(0x17), 0x15, 0);
        Sound_PlayOnce(E1_MOBANIM(0x17), 0x26d);
        InTime2(500, AnimStop, E1_MOBANIM(0x17), 0);
        InTime2(500, AnimStop, E1_MOBANIM(0x18), 0);
        st.comboCount--;
    }
}

// 0x100017f0  EV_TRIGGER_ENTER trigger 0x40006 (the crash site): wake the container
static void E1_OnTriggerCrashEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x06))
        return;
    MobObj_Wake(4);
    WindowSetText(st.win, st.text[1]);       // the container objective
    st.lineIndex = 1;
}

// 0x10001830  EV_ANIM_FINISHED with anim 0x1d: the indicator stops
static void E1_OnComboDone(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x1d)
        AnimStop(E1_MOBANIM(0x18), 0);
}

// 0x10001850  EV_TRIGGER_LEAVE trigger 0x40006 (until the container is hooked)
static void E1_OnTriggerCrashLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x06))
        return;
    MobObj_Sleep(4);
    WindowSetText(st.win, st.text[0]);
    st.lineIndex = 0;
}

// 0x10001890  EV_POD_HOOKED again later: box body, back to the current objective
static void E1_OnContainerRehooked(const LanderEvent *ev)
{
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0x8c, 0x8c, 0x8c, 0x3e8);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    WindowSetText(st.win, st.text[st.lineIndex]);
    st.container = 1;
}

// 0x10001910  EV_POD_HOOKED the first time
static void E1_OnContainerHooked(const LanderEvent *ev)
{
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0x8c, 0x8c, 0x8c, 0x3e8);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    WindowSetText(st.win, st.text[2]);       // the colony entrance objective
    st.lineIndex = 2;
    st.container = 1;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E1_OnTriggerCrashEnter);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, E1_OnTriggerCrashLeave);
    EventType_AddHandler(EV_POD_HOOKED, NULL, E1_OnContainerRehooked);
    EventType_RemoveHandler(EV_POD_HOOKED, E1_OnContainerHooked);
}

// 0x100019d0 / 0x10001a10  EV_TRIGGER_ENTER / LEAVE trigger 0x40000: door 0x30052
static void E1_OnTriggerDoor52Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x00))
        return;
    AnimPlay(E1_ANIM(0x52), 1, 0);
    Sound_PlayOnce(E1_ANIM(0x52), 0xac);
}

static void E1_OnTriggerDoor52Close(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x00))
        return;
    AnimPlay(E1_ANIM(0x52), 1, 4);
    Sound_PlayOnce(E1_ANIM(0x52), 0xac);
}

// 0x10001a50 / 0x10001a80  EV_TRIGGER_ENTER / LEAVE trigger 0x40008: anim-turrets 0x32, 0x33, 0x2a
static void E1_OnTrigger08Enter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x08))
        return;
    MobObj_Wake(0x32);
    MobObj_Wake(0x33);
    MobObj_Wake(0x2a);
}

static void E1_OnTrigger08Leave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x08))
        return;
    MobObj_Sleep(0x32);
    MobObj_Sleep(0x33);
    MobObj_Sleep(0x2a);
}

// 0x10001ab0 / 0x10001af0  trigger 0x40007: door 0x3002d. The closing handler is registered for
// EV_TRIGGER_ENTER too (DLL quirk): on entering it closes, then opens (newest handler first).
static void E1_OnTriggerDoor2DOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x07))
        return;
    AnimPlay(E1_ANIM(0x2d), 2, 0);
    Sound_PlayOnce(E1_ANIM(0x2d), 0xac);
}

static void E1_OnTriggerDoor2DClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x07))
        return;
    AnimPlay(E1_ANIM(0x2d), 2, 4);
    Sound_PlayOnce(E1_ANIM(0x2d), 0xac);
}

// 0x10001b30  EV_SWITCH 0xa00d6..d9: stop the fan 0x30019..1c (and force the event's state to 1
// for the handlers after this one: the DLL writes into the event); 0xa00da toggles the fan 0x30053
static void E1_OnSwitchFans(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)E1_SWITCH(0xd6) && id <= (int32_t)E1_SWITCH(0xd9)) {
        ObjId fan = (ObjId)(id - 0x700bd);                  // 0xa00d6 -> 0x30019
        ((LanderEvent *)ev)->arg[1] = 1;   // the engine's dispatch copy is writable, as in the DLL
        AnimStop(fan, 0);
        Sound_StopObject(fan);
    } else if (id == (int32_t)E1_SWITCH(0xda)) {
        if (ev->arg[1] == 1) {
            AnimStop(E1_ANIM(0x53), 1);
            Sound_StopObject(E1_ANIM(0x53));
        } else if (ev->arg[1] == 0) {
            AnimPlay(E1_ANIM(0x53), 3, 0x81);
            Sound_PlayLoop(E1_ANIM(0x53), 0x26f, -1);
        }
    }
}

// 0x10001bd0  EV_SWITCH 0xa0022: bridge 0x300e2 (and the model of 0x8002b) follow the switch
static void E1_OnSwitchBridge(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E1_SWITCH(0x22))
        return;
    if (ev->arg[1] == 1) {
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x2b), 1 - st.bridgeOn);
        AnimSetUpdateInterval(E1_ANIM(0xe2), 0x14);
        AnimPlay(E1_ANIM(0xe2), 5, 0);
        Sound_PlayOnce(E1_ANIM(0xe2), 0xac);
        Sound_PlayOnce(E1_SWITCH(0x22), 0xf2);
        st.bridgeOn = 1;
    } else if (ev->arg[1] == 0) {
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x2b), -st.bridgeOn);
        AnimPlay(E1_ANIM(0xe2), 5, 4);
        Sound_PlayOnce(E1_SWITCH(0x22), 0xf2);
        Sound_PlayOnce(E1_ANIM(0xe2), 0xac);
        st.bridgeOn = 0;
    }
}

// 0x10001cb0 / 0x10001cf0  trigger 0x40010: door 0x3002f
static void E1_OnTriggerDoor2FOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x10))
        return;
    AnimPlay(E1_ANIM(0x2f), 2, 0);
    Sound_PlayOnce(E1_ANIM(0x2f), 0xac);
}

static void E1_OnTriggerDoor2FClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x10))
        return;
    AnimPlay(E1_ANIM(0x2f), 2, 4);
    Sound_PlayOnce(E1_ANIM(0x2f), 0xac);
}

// 0x10001d30 / 0x10001d70  trigger 0x40011: door 0x3006c (closing also on EV_TRIGGER_ENTER)
static void E1_OnTriggerDoor6COpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x11))
        return;
    AnimPlay(E1_ANIM(0x6c), 5, 0);
    Sound_PlayOnce(E1_ANIM(0x6c), 0xac);
}

static void E1_OnTriggerDoor6CClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x11))
        return;
    AnimPlay(E1_ANIM(0x6c), 5, 4);
    Sound_PlayOnce(E1_ANIM(0x6c), 0xac);
}

// 0x10001db0 / 0x10001df0  trigger 0x40009: door 0x3006d (closing also on EV_TRIGGER_ENTER)
static void E1_OnTriggerDoor6DOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x09))
        return;
    AnimPlay(E1_ANIM(0x6d), 5, 0);
    Sound_PlayOnce(E1_ANIM(0x6d), 0xac);
}

static void E1_OnTriggerDoor6DClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x09))
        return;
    AnimPlay(E1_ANIM(0x6d), 5, 4);
    Sound_PlayOnce(E1_ANIM(0x6d), 0xac);
}

// 0x10001e30  EV_SWITCH 0xa0086 on: gate 0x300db, once
static void E1_OnSwitchGate86(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E1_SWITCH(0x86) || ev->arg[1] != 1)
        return;
    AnimPlay(E1_ANIM(0xdb), 6, 0);
    Sound_PlayLoop(E1_ANIM(0xdb), 0xc2, 3);
    Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0xdc), st.gate86 + 1);
    Object_SetProperty(E1_SWITCH(0x86), PROP_STATE, -1);
    Sound_PlayOnce(E1_SWITCH(0x86), 0xf2);
    st.gate86 = 1;
    EventType_RemoveHandler(EV_SWITCH, E1_OnSwitchGate86);
}

// 0x10001ed0  EV_SWITCH 0xa0087 on: doors 0x300e0/e1, once
static void E1_OnSwitchDoorsE0(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E1_SWITCH(0x87) || ev->arg[1] != 1)
        return;
    AnimSetUpdateInterval(E1_ANIM(0xe0), 0x64);
    AnimPlay(E1_ANIM(0xe0), 7, 0);
    Sound_PlayLoop(E1_ANIM(0xe0), 0xc3, 2);
    AnimSetUpdateInterval(E1_ANIM(0xe1), 0x64);
    AnimPlay(E1_ANIM(0xe1), 8, 0);
    Sound_PlayLoop(E1_ANIM(0xe1), 0xc3, 2);
    EventType_RemoveHandler(EV_SWITCH, E1_OnSwitchDoorsE0);
}

// 0x10001f70  EV_TRIGGER_ENTER trigger 0x40012: shutter 0x30029 opens, the turrets 0x1d..21 hum
static void E1_OnTriggerShutterOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x12))
        return;
    AnimPlay(E1_ANIM(0x29), 9, 0);
    AnimSetUpdateInterval(E1_ANIM(0x29), 0x1e);
    Sound_PlayOnce(E1_ANIM(0x29), 0xac);
    for (int32_t id = 0x1d; id <= 0x21; id++)                   // turrets 0x1d..0x21
        Sound_PlayLoop((ObjId)id, 0x85, -1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E1_OnTriggerShutterOpen);
}

// 0x10001ff0  EV_TRIGGER_ENTER trigger 0x4000c: shutter 0x30029 closes
static void E1_OnTriggerShutterClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x0c))
        return;
    AnimPlay(E1_ANIM(0x29), 9, 4);
    AnimSetUpdateInterval(E1_ANIM(0x29), 0x1e);
    Sound_PlayOnce(E1_ANIM(0x29), 0xac);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E1_OnTriggerShutterClose);
}

// 0x10002050  EV_DESTROYED turrets 0x1d..21: all five -> door 0x3002a opens
static void E1_OnDestroyedTurrets(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < 0x1d || id > 0x21)
        return;
    if (++st.turretKills != 5)
        return;
    AnimPlay(E1_ANIM(0x2a), 0x17, 0);
    Sound_PlayOnce(E1_ANIM(0x2a), 0xac);
    EventType_RemoveHandler(EV_DESTROYED, E1_OnDestroyedTurrets);
}

// 0x100020b0  EV_TRIGGER_ENTER trigger 0x4000a: anim-turrets 2 and 3 come alive
static void E1_OnTrigger0A(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x0a))
        return;
    MobObj_Wake(2);
    MobObj_Wake(3);
    AnimSetUpdateInterval(E1_ATURRET(2), 0x28);
    AnimPlay(E1_ATURRET(2), 0xb, 0xe1);
    Sound_PlayLoop(E1_ATURRET(2), 0x89, -1);
    AnimSetUpdateInterval(E1_ATURRET(3), 0x28);
    AnimPlay(E1_ATURRET(3), 0xc, 0xe1);
    Sound_PlayLoop(E1_ATURRET(3), 0x89, -1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E1_OnTrigger0A);
}

// 0x10002160 / 0x100021b0  trigger 0x4000d: anim-turrets 0x2c..2e
static void E1_OnTrigger0DEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x0d))
        return;
    MobObj_Wake(0x2c);
    MobObj_Wake(0x2d);
    MobObj_Wake(0x2e);
    for (int32_t i = 0x2c; i <= 0x2e; i++)
        Sound_PlayLoop(E1_ATURRET(i), 0x8d, -1);
}

static void E1_OnTrigger0DLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x0d))
        return;
    MobObj_Sleep(0x2c);
    MobObj_Sleep(0x2d);
    MobObj_Sleep(0x2e);
}

// 0x100021e0 / 0x10002220  trigger 0x40003: door 0x3002e
static void E1_OnTriggerDoor2EOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x03))
        return;
    AnimPlay(E1_ANIM(0x2e), 0xf, 0);
    Sound_PlayOnce(E1_ANIM(0x2e), 0xac);
}

static void E1_OnTriggerDoor2EClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x03))
        return;
    AnimPlay(E1_ANIM(0x2e), 0xf, 4);
    Sound_PlayOnce(E1_ANIM(0x2e), 0xac);
}

// 0x10002260  EV_TRIGGER_ENTER trigger 0x40013: hangar doors; the first time also the turrets
// 0x2f..31 and the second door 0x300cf
static void E1_OnTriggerHangarEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x13))
        return;
    if (st.hangarOpen == 1) {
        AnimPlay(E1_ANIM(0xce), 0xf, 0);
        Sound_PlayOnce(E1_ANIM(0xce), 0xac);
    } else if (st.hangarOpen == 0) {
        MobObj_Wake(0x30);
        MobObj_Wake(0x2f);
        MobObj_Wake(0x31);
        for (int32_t i = 0x2f; i <= 0x31; i++)
            Sound_PlayLoop(E1_ATURRET(i), 0x8d, -1);
        AnimPlay(E1_ANIM(0xce), 0xf, 0);
        Sound_PlayOnce(E1_ANIM(0xce), 0xac);
        AnimPlay(E1_ANIM(0xcf), 0xf, 0);
        Sound_PlayOnce(E1_ANIM(0xcf), 0xac);
        st.hangarOpen = 1;
    }
}

// 0x10002340  EV_TRIGGER_LEAVE trigger 0x40013
static void E1_OnTriggerHangarLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x13))
        return;
    AnimPlay(E1_ANIM(0xce), 0xf, 4);
    Sound_PlayOnce(E1_ANIM(0xce), 0xac);
}

// 0x10002380  platform 0x1030034 goes up; its doors 0x300d1 / 0x300d0 open and shut on the way
static void E1_LiftUp(void)
{
    MobObj_Wake(0x34);
    AnimPlay(E1_MOBANIM(0x34), 0x10, 0);
    Sound_PlayLoop(E1_MOBANIM(0x34), 0x270, -1);
    Sound_PlayOnce(E1_MOBANIM(0x34), 0x271);
    InTime3(1000, AnimPlay, E1_ANIM(0xd1), 0xf, 0);
    InTime2(1000, Sound_PlayOnce, E1_ANIM(0xcf), 0xac);
    InTime3(2000, AnimPlay, E1_ANIM(0xd1), 0xf, 4);
    InTime2(2000, Sound_PlayOnce, E1_ANIM(0xcf), 0xac);
    InTime3(0xc1c, AnimPlay, E1_ANIM(0xd0), 0xf, 0);
    InTime2(0xc1c, Sound_PlayOnce, E1_ANIM(0xce), 0xac);
    InTime3(0x10cc, AnimPlay, E1_ANIM(0xd0), 0xf, 4);
    InTime2(0x10cc, Sound_PlayOnce, E1_ANIM(0xce), 0xac);
    st.liftUp = 1;
}

// 0x100024a0  platform 0x1030034 goes down, the call switch resets
static void E1_LiftDown(void)
{
    AnimPlay(E1_MOBANIM(0x34), 0x10, 4);
    Sound_PlayLoop(E1_MOBANIM(0x34), 0x270, -1);
    Sound_PlayOnce(E1_MOBANIM(0x34), 0x271);
    InTime3(0x2bc, AnimPlay, E1_ANIM(0xd0), 0xf, 0);
    InTime2(0x2bc, Sound_PlayOnce, E1_ANIM(0xce), 0xac);
    InTime3(0x1388, AnimPlay, E1_ANIM(0xd0), 0xf, 4);
    InTime2(0x1388, Sound_PlayOnce, E1_ANIM(0xce), 0xac);
    InTime3(0xbb8, AnimPlay, E1_ANIM(0xd1), 0xf, 0);
    InTime2(0xbb8, Sound_PlayOnce, E1_ANIM(0xcf), 0xac);
    InTime3(0xfa0, AnimPlay, E1_ANIM(0xd1), 0xf, 4);
    InTime2(0xfa0, Sound_PlayOnce, E1_ANIM(0xcf), 0xac);
    st.liftUp = 0;
    Object_SetProperty(E1_SWITCH(0xd5), PROP_STATE, 0);
    InTime1(0x1388, MobObj_Sleep, 0x34);
}

// 0x100025d0  platform 0x1030035 up, doors 0x300d4, d3, d2
static void E1_Lift2Up(void)
{
    AnimPlay(E1_MOBANIM(0x35), 0x16, 0);
    Sound_PlayLoop(E1_MOBANIM(0x35), 0x270, -1);
    Sound_PlayOnce(E1_MOBANIM(0x35), 0x271);
    InTime3(0x384, AnimPlay, E1_ANIM(0xd4), 0xf, 0);
    InTime2(0x384, Sound_PlayOnce, E1_ANIM(0xd4), 0xac);
    InTime3(0x6a4, AnimPlay, E1_ANIM(0xd4), 0xf, 4);
    InTime2(0x6a4, Sound_PlayOnce, E1_ANIM(0xd4), 0xac);
    InTime3(0x8fc, AnimPlay, E1_ANIM(0xd3), 0xf, 0);
    InTime2(0x8fc, Sound_PlayOnce, E1_ANIM(0xd3), 0xac);
    InTime3(0xc80, AnimPlay, E1_ANIM(0xd3), 0xf, 4);
    InTime2(0xc80, Sound_PlayOnce, E1_ANIM(0xd3), 0xac);
    InTime3(0x1004, AnimPlay, E1_ANIM(0xd2), 0xf, 0);
    InTime2(0x1004, Sound_PlayOnce, E1_ANIM(0xd2), 0xac);
    InTime3(0x1518, AnimPlay, E1_ANIM(0xd2), 0xf, 4);
    InTime2(0x1518, Sound_PlayOnce, E1_ANIM(0xd2), 0xac);
    st.lift2Up = 1;
}

// 0x10002740  platform 0x1030035 down (the DLL opens 0x300d3 twice instead of closing it)
static void E1_Lift2Down(void)
{
    AnimPlay(E1_MOBANIM(0x35), 0x16, 4);
    Sound_PlayLoop(E1_MOBANIM(0x35), 0x270, -1);
    Sound_PlayOnce(E1_MOBANIM(0x35), 0x271);
    InTime3(0x258, AnimPlay, E1_ANIM(0xd2), 0xf, 0);
    InTime2(0x258, Sound_PlayOnce, E1_ANIM(0xd2), 0xac);
    InTime3(0x5dc, AnimPlay, E1_ANIM(0xd2), 0xf, 4);
    InTime2(0x5dc, Sound_PlayOnce, E1_ANIM(0xd2), 0xac);
    InTime3(0xaf0, AnimPlay, E1_ANIM(0xd3), 0xf, 0);
    InTime2(0xaf0, Sound_PlayOnce, E1_ANIM(0xd3), 0xac);
    InTime3(0xce4, AnimPlay, E1_ANIM(0xd3), 0xf, 0);
    InTime2(0xce4, Sound_PlayOnce, E1_ANIM(0xd3), 0xac);
    InTime3(0x10cc, AnimPlay, E1_ANIM(0xd4), 0xf, 0);
    InTime2(0x10cc, Sound_PlayOnce, E1_ANIM(0xd4), 0xac);
    InTime3(0x1450, AnimPlay, E1_ANIM(0xd4), 0xf, 4);
    InTime2(0x1450, Sound_PlayOnce, E1_ANIM(0xd4), 0xac);
    st.lift2Up = 0;
}

// 0x100028b0  EV_SWITCH 0xa00d5: on -> platform up; turning it off is refused (forced back on)
static void E1_OnSwitchLift(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E1_SWITCH(0xd5))
        return;
    if (ev->arg[1] == 1 && st.liftUp == 0)
        E1_LiftUp();
    if (ev->arg[1] == 0) {
        st.liftUp = 1;
        Object_SetProperty(E1_SWITCH(0xd5), PROP_STATE, 1);
    }
}

// 0x10002900  EV_TRIGGER_ENTER trigger 0x40015 while the platform is up: bring it down
static void E1_OnTriggerLiftTop(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E1_TRIGGER(0x15) && st.liftUp == 1)
        E1_LiftDown();                       // a tail jump in the DLL
}

// 0x10002920  EV_TRIGGER_ENTER trigger 0x40016: the second platform starts its cycle
static void E1_OnTriggerLift2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x16))
        return;
    MobObj_Wake(0x35);
    E1_Lift2Up();
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E1_OnTriggerLift2);
}

// 0x10002950  EV_ANIM_FINISHED of platform 0x1030035: turn round after 30 s
static void E1_OnLift2Anim(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E1_MOBANIM(0x35))
        return;
    if (st.lift2Up == 1)
        InTime0(30000, E1_Lift2Down);
    if (st.lift2Up == 0)
        InTime0(30000, E1_Lift2Up);
}

// 0x100029a0  EV_SWITCH 0xa008a / 8b (a pair): gate 0x3008d follows, the partner switch is synced
static void E1_OnSwitchGate8D(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != E1_SWITCH(0x8a) && id != E1_SWITCH(0x8b))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(E1_ANIM(0x8d), 0x12, 0);
        Sound_PlayChain(E1_ANIM(0x8d), 10, 0xcb, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcd);
        AnimSetUpdateInterval(E1_ANIM(0x8d), 0x32);
    } else {
        AnimPlay(E1_ANIM(0x8d), 0x12, 4);
        AnimSetUpdateInterval(E1_ANIM(0x8d), 0x32);
        Sound_PlayChain(E1_ANIM(0x8d), 10, 0xcb, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcd);
    }
    ObjId other = (ObjId)(0x140115 - (int32_t)ev->arg[0]);      // the other one of the pair
    int32_t state = (int32_t)ev->arg[1];
    if ((int32_t)Object_GetProperty(other, PROP_STATE) != state)
        Object_SetProperty(other, PROP_STATE, state);
}

// 0x10002ac0 / 0x10002b10  trigger 0x4000e: anim-turrets 0x28/0x29
static void E1_OnTrigger0EEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x0e))
        return;
    MobObj_Wake(0x28);
    Sound_PlayLoop(E1_ATURRET(0x28), 0x8d, -1);
    MobObj_Wake(0x29);
    Sound_PlayLoop(E1_ATURRET(0x29), 0x8d, -1);
}

static void E1_OnTrigger0ELeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x0e))
        return;
    MobObj_Sleep(0x28);
    MobObj_Sleep(0x29);
}

// 0x10002b40  EV_TRIGGER_ENTER and LEAVE trigger 0x40004: door 0x3006e opens (both ways)
static void E1_OnTriggerDoor6E(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x04))
        return;
    AnimPlay(E1_ANIM(0x6e), 0x13, 0);
    Sound_PlayOnce(E1_ANIM(0x6e), 0xaf);
}

// 0x10002b80 / 0x10002bd0  trigger 0x40005: door 0x30017
static void E1_OnTriggerDoor17Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x05))
        return;
    AnimPlay(E1_ANIM(0x17), 0x14, 0);
    AnimSetUpdateInterval(E1_ANIM(0x17), 0x1e);
    Sound_PlayOnce(E1_ANIM(0x17), 0xab);
}

static void E1_OnTriggerDoor17Close(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x05))
        return;
    AnimPlay(E1_ANIM(0x17), 0x14, 4);
    AnimSetUpdateInterval(E1_ANIM(0x17), 0x1e);
    Sound_PlayOnce(E1_ANIM(0x17), 0xab);
}

// 0x10002c20  EV_TRIGGER_ENTER/LEAVE: the objective line along the way
static void E1_OnTriggerObjectives(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E1_TRIGGER(0xae) && ev->type == EV_TRIGGER_ENTER) {
        WindowSetText(st.win, st.text[3]);   // the colony objective
        st.lineIndex = 3;
    }
    if ((ObjId)ev->arg[1] == E1_TRIGGER(0xae) && ev->type == EV_TRIGGER_LEAVE && st.inHangar == 0) {
        WindowSetText(st.win, st.text[2]);
        st.lineIndex = 2;
    }
    if ((ObjId)ev->arg[1] == E1_TRIGGER(0xaf)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            WindowSetText(st.win, st.text[4]);   // the hangar objective
            st.lineIndex = 4;
            st.inHangar = 1;
        }
        if ((ObjId)ev->arg[1] == E1_TRIGGER(0xaf) && ev->type == EV_TRIGGER_LEAVE)
            st.inHangar = 0;
    }
    if ((ObjId)ev->arg[1] == E1_TRIGGER(0x0e) && ev->type == EV_TRIGGER_ENTER) {
        WindowSetText(st.win, st.text[5]);   // the freighter objective
        st.lineIndex = 5;
    }
    if ((ObjId)ev->arg[1] == E1_TRIGGER(0x0f)) {
        if (ev->type == EV_TRIGGER_ENTER && st.container == 1) {
            WindowSetText(st.win, st.text[6]);   // the leave line
            st.lineIndex = 6;
        }
        if ((ObjId)ev->arg[1] == E1_TRIGGER(0x0f) && ev->type == EV_TRIGGER_ENTER && st.container == 0) {
            WindowSetText(st.win, st.text[7]);   // the missing container line
            st.lineIndex = 7;
        }
    }
}

// 0x10002d80  InTime 3 s after leaving with the container: +90000, success 1 if it took more
// than 900 s, else 2
static void E1_Finish(void)
{
    if (st.seconds > 900) {
        Lander_SetCash(E1_LANDER, Lander_GetCash(E1_LANDER) + 90000);
        MissionScore_End(1);                                         // 0x100013b0
        return;
    }
    Lander_SetCash(E1_LANDER, Lander_GetCash(E1_LANDER) + 90000);
    MissionScore_End(2);
}

// 0x10002df0  EV_TRIGGER_ENTER trigger 0x4000f with the container hooked: the end in 3 s
static void E1_OnTriggerExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E1_TRIGGER(0x0f) && st.container == 1)
        InTime0(3000, E1_Finish);
}

// 0x10002e20 / 0x10002e50  InTime: the ambush anim-turrets 0x1a / 0x1f take the next explosion
// mob as their target (both index the table with the first counter)
static void E1_RetargetA(void)
{
    st.targetIndex++;
    Object_SetProperty(E1_ATURRET(0x1a), PROP_TARGET, (intptr_t)(e1_ambushTargets[st.targetIndex] + 0x1090005));
}

static void E1_RetargetB(void)
{
    st.targetCount2++;
    Object_SetProperty(E1_ATURRET(0x1f), PROP_TARGET, (intptr_t)(e1_ambushTargets[st.targetIndex] + 0x1090005));
}

// 0x10002e80  EV_TRIGGER_ENTER trigger 0x40006 (the crash site): the ambush - explosions at
// scripted times, turrets shooting at them, two turrets running AnimSeq sequences
static void E1_OnTriggerAmbush(const LanderEvent *ev)
{
    static const struct { uint8_t mob; int32_t size; } sizes[18] = {
        { 0x05, 0x64 }, { 0x06, 0x32 }, { 0x07, 0x46 }, { 0x08, 0x64 }, { 0x09, 0x5a }, { 0x0a, 0x28 },
        { 0x0b, 0xa0 }, { 0x0c, 0x46 }, { 0x0d, 0x5a }, { 0x0e, 0x8c }, { 0x14, 0x6e }, { 0x0f, 0x28 },
        { 0x10, 0x82 }, { 0x11, 0xa0 }, { 0x12, 0x14 }, { 0x13, 0x64 }, { 0x16, 0x64 }, { 0x15, 0x8c },
    };
    if ((ObjId)ev->arg[1] != E1_TRIGGER(0x06))
        return;
    for (int i = 0; i < 18; i++)
        Object_SetProperty(E1_EXPLOSION(sizes[i].mob), PROP_EXPLOSION, sizes[i].size);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa5), PROP_TARGET, E1_ATURRET(0x1a));
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xa4), PROP_TARGET, E1_ATURRET(0x20));
    Sound_PlayLoop(E1_EMITTER(0x32), 0xf1, -1);
    Sound_PlayLoop(E1_EMITTER(0x32), 0xf0, -1);
    Sound_PlayLoop(E1_EMITTER(0x30), 0xf1, -1);
    Sound_PlayLoop(E1_EMITTER(0x30), 0xf0, -1);
    Object_SetProperty(E1_ATURRET(0x1a), PROP_STATE, 1);
    Object_SetProperty(E1_ATURRET(0x1f), PROP_STATE, 1);
    Object_SetProperty(E1_ATURRET(0x1a), PROP_TARGET, E1_EXPLOSION(5));
    InTime1(2000, MobObj_Wake, 5);
    InTime0(2000, E1_RetargetA);
    InTime1(0xc80, MobObj_Wake, 6);
    InTime0(0xc80, E1_RetargetA);
    InTime1(0x1900, MobObj_Wake, 0xa);
    InTime0(0x1900, E1_RetargetA);
    InTime1(0x2454, MobObj_Wake, 0xb);
    InTime0(0x2454, E1_RetargetA);
    Object_SetProperty(E1_ATURRET(0x1f), PROP_TARGET, E1_EXPLOSION(8));
    InTime1(0xe74, MobObj_Wake, 8);
    InTime0(0xe74, E1_RetargetB);
    InTime1(0x1388, MobObj_Wake, 7);
    InTime0(0xe74, E1_RetargetB);
    InTime1(0x1f40, MobObj_Wake, 9);
    InTime0(0xe74, E1_RetargetB);
    InTime1(0xfa0, Object_SetAlive, E1_EMITTER(0x36));
    InTime1(0x2328, Object_SetAlive, E1_EMITTER(0x37));
    InTime1(0xdac, MobObj_Wake, 0xc);
    InTime1(0x2710, MobObj_Wake, 0xd);
    InTime1(0x251c, MobObj_Wake, 0xe);
    InTime1(0x5dc, MobObj_Wake, 0x14);
    InTime1(0x3a98, Object_SetAlive, E1_EMITTER(0x38));
    InTime1(0x3a98, MobObj_Wake, 0x14);      // a second wake of mob 0x14 (the engine ignores it)
    InTime1(0x2710, MobObj_Wake, 0xf);
    InTime1(0x2328, MobObj_Wake, 0x10);
    InTime1(0x30d4, MobObj_Wake, 0x11);
    InTime1(0x7d0, MobObj_Wake, 0x12);
    InTime1(0x4650, MobObj_Wake, 0x13);
    InTime1(0x1ce8, MobObj_Wake, 0x15);
    InTime1(0x1964, MobObj_Wake, 0x16);
    AnimSeq_Start(E1_ATURRET(0x20), st.seqA, 0);                // 0x10004140
    AnimSetUpdateInterval(E1_ATURRET(0x20), 5);
    Sound_PlayLoop(E1_ATURRET(0x20), 0x26c, -1);
    AnimSeq_Start(E1_ATURRET(0x21), st.seqB, 0);
    AnimSetUpdateInterval(E1_ATURRET(0x21), 5);
    Sound_PlayLoop(E1_ATURRET(0x21), 0x26c, -1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E1_OnTriggerAmbush);
}

// 0x100032b0  InTime 2 s after the container was stowed
static void E1_ContainerStowed(void)
{
    Object_SetProperty(E1_CONTAINER, PROP_TARGET, -2);
}

// 0x100032d0  EV_TRIGGER_ENTER of the cargo-bay triggers 0x40055..5d (they watch the container):
// release it, set it upright where it is. EV_TRIGGER_LEAVE of 0x400bb..c3 without the container:
// the container objective
static void E1_OnTriggerCargoBay(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (ev->type == EV_TRIGGER_ENTER && st.container != 0 &&
        trig >= (int32_t)E1_TRIGGER(0x55) && trig <= (int32_t)E1_TRIGGER(0x5d)) {
        Object_SetProperty(E1_CONTAINER, PROP_TARGET, -1);
        st.container = 0;
        // the DLL copies a whole VECTOR {x, y, z, pad}; the pad is not part of the position
        const int32_t *p = (const int32_t *)Object_GetProperty(E1_CONTAINER, PROP_POSITION);
        int32_t pos[4] = { p[0], p[1], p[2], 0 };
        Object_SetProperty(E1_CONTAINER, PROP_MATRIX, Object_GetProperty(MOB_ID(OT_MARKER, 0x26), PROP_MATRIX));
        Object_SetProperty(E1_CONTAINER, PROP_POSITION, (intptr_t)pos);
        InTime0(2000, E1_ContainerStowed);
    }
    if (ev->type == EV_TRIGGER_LEAVE && st.container == 0 &&
        trig >= (int32_t)E1_TRIGGER(0xbb) && trig <= (int32_t)E1_TRIGGER(0xc3))
        WindowSetText(st.win, st.text[1]);
}

// 0x100033e0 = MissionLib_OnDestroyedSound (same thresholds)

// 0x10003470  EV_TICK_1S
static void E1_SecondTick(const LanderEvent *ev)
{
    (void)ev;
    st.seconds++;
}

// 0x10003480  EV_TRIGGER_ENTER/LEAVE triggers 0x40060..6b: headlight in the dark tunnels
static void E1_OnTriggerHeadlight(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)E1_TRIGGER(0x60) || trig > (int32_t)E1_TRIGGER(0x6b))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        Lander_SetHeadLight(E1_LANDER, 0x31c, 0xc00);
    else
        Lander_SetHeadLight(E1_LANDER, 0, 0);
}

// 0x100034d0  EV_PICKUP: +50 % shield (0x1300c4..c8), medium cash (0x130023..28)
static void E1_OnPickup(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)OBJ_ID(OT_PICKUP, 0xc4) && id <= (int32_t)OBJ_ID(OT_PICKUP, 0xc8)) {
        int32_t shield = (int32_t)Object_GetProperty(E1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(E1_LANDER, 50);
        Object_SetProperty(E1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    id = (int32_t)ev->arg[0];
    if (id >= (int32_t)OBJ_ID(OT_PICKUP, 0x23) && id <= (int32_t)OBJ_ID(OT_PICKUP, 0x28)) {
        Lander_SetCash(E1_LANDER, Lander_GetCash(E1_LANDER) + gMediumCash);
        Voice_CashAcquired(E1_LANDER);
    }
}

// 0x10003580  EV_TRIGGER_PLANE 0x1a00b9 / ba (added by the light switches): side 0 forces the
// interior lighting, the other side gives control back
static void E1_OnPlaneLights(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != OBJ_ID(OT_TRIGGERPLANE, 0xb9) && (ObjId)ev->arg[1] != OBJ_ID(OT_TRIGGERPLANE, 0xba))
        return;
    gInOutOverride = (ev->arg[2] == 0) ? 1 : 0;
}

// 0x100035b0  EV_SWITCH 0xa0088 / 89 (a pair): gate 0x3008c follows, the partner is synced. Every
// event adds another plane handler (the guard flag is never set).
static void E1_OnSwitchGate8C(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != E1_SWITCH(0x88) && id != E1_SWITCH(0x89))
        return;
    if (st.planeAdded == 0)
        EventType_AddHandler(EV_TRIGGER_PLANE, NULL, E1_OnPlaneLights);
    AnimPlay(E1_ANIM(0x8c), 0xa, ev->arg[1] == 1 ? 0 : 4);
    AnimSetUpdateInterval(E1_ANIM(0x8c), 0x32);
    Sound_PlayChain(E1_ANIM(0x8c), 10, 0xcb, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcd);
    ObjId other = (ObjId)(0x140111 - (int32_t)ev->arg[0]);
    int32_t state = (int32_t)ev->arg[1];
    if ((int32_t)Object_GetProperty(other, PROP_STATE) != state)
        Object_SetProperty(other, PROP_STATE, state);
}

// 0x10003690  common set-up
static void E1_Setup(void)
{
    st.text = e1_text[gLanguage];
    SetGravity(13);
    gAirResistance = 0xe;
    gMainThrottlePower = 0x1a;
    gNbWaypoints = e1_nbWaypoints;
    gWaypoints = e1_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 550, 12);
    WindowSetText(st.win, st.text[0]);
    st.lineIndex = 0;
    AnimSeq_Init();                                             // 0x100041b0
    Object_SetDead(E1_EMITTER(0x36));
    Object_SetDead(E1_EMITTER(0x37));
    Object_SetDead(E1_EMITTER(0x38));
    for (int32_t id = 0x1d; id <= 0x21; id++)                    // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x19);
    for (int32_t i = 2; i <= 3; i++) {
        Object_SetProperty(E1_ATURRET(i), PROP_ARMOUR, 0x19);
        Object_SetProperty(E1_ATURRET(i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x28; i <= 0x33; i++)
        Object_SetProperty(E1_ATURRET(i), PROP_ARMOUR, 0x28);
    for (int32_t id = 0xb0; id <= 0xb5; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x50);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x8c);
    }
    for (int32_t id = 0x96; id <= 0xad; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = 0x7a; id <= 0x85; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t i = 0x42; i <= 0x4a; i++) {
        Object_SetProperty(OBJ_ID(OT_FUELDUMP, i), PROP_ARMOUR, 0xa);
        Object_SetProperty(OBJ_ID(OT_FUELDUMP, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x39; i <= 0x41; i++) {
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0xa);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x50);
    }
    TexAnimPlay(0x8e, 1, 100);
    TexAnimPlay(0x8f, 1, 100);
    gTurretTypes[21] = &e1_turretType21;
    gTurretTypes[22] = &e1_turretType22;
    gTurretTypes[23] = &e1_turretType23;
    gTurretTypes[24] = &e1_turretType24;
    gTurretTypes[25] = &e1_turretType25;
    gTurretTypes[26] = &e1_turretType26;
    gTurretTypes[27] = &e1_turretType27;
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x100033e0
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerCrashEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerCrashLeave);
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchCombo);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, E1_OnComboDone);
    EventType_AddHandler(EV_POD_HOOKED, NULL, E1_OnContainerHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor52Open);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerDoor52Close);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTrigger08Enter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTrigger08Leave);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor2DOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor2DClose);   // sic: ENTER
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchFans);
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchBridge);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor2FOpen);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerDoor2FClose);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor6COpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor6CClose);   // sic: ENTER
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor6DOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor6DClose);   // sic: ENTER
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchGate86);
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchDoorsE0);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerShutterOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerShutterClose);
    EventType_AddHandler(EV_DESTROYED, NULL, E1_OnDestroyedTurrets);
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchGate8C);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTrigger0A);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTrigger0DEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTrigger0DLeave);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor2EOpen);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerDoor2EClose);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerHangarEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerHangarLeave);
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchLift);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerLiftTop);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerLift2);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, E1_OnLift2Anim);
    EventType_AddHandler(EV_SWITCH, NULL, E1_OnSwitchGate8D);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTrigger0EEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTrigger0ELeave);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor6E);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerDoor6E);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerDoor17Open);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerDoor17Close);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerObjectives);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerObjectives);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerExit);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerAmbush);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerCargoBay);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerCargoBay);
    EventType_AddHandler(EV_TICK_1S, NULL, E1_SecondTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E1_OnTriggerHeadlight);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E1_OnTriggerHeadlight);
    EventType_AddHandler(EV_PICKUP, NULL, E1_OnPickup);
    AnimPlay(E1_ANIM(0x2a), 0x17, 0);
    AnimPlay(E1_ANIM(0x19), 0xe, 0x81);
    AnimSetUpdateInterval(E1_ANIM(0x19), 0x64);
    AnimPlay(E1_ANIM(0x1a), 0xe, 0x81);
    AnimPlay(E1_ANIM(0x1b), 0xe, 0x81);
    AnimPlay(E1_ANIM(0x1c), 0xe, 0x81);
    AnimSetUpdateInterval(E1_ANIM(0x1c), 0x64);
    AnimPlay(E1_ANIM(0x53), 3, 0x81);
    for (int32_t i = 0x77; i <= 0x79; i++) {
        AnimPlay(E1_ANIM(i), 0xd, 0x81);
        AnimSetUpdateInterval(E1_ANIM(i), 0x64);
    }
    AnimPlay(E1_ANIM(0x18), 0x11, 0x81);
    AnimSetUpdateInterval(E1_ANIM(0x18), 0x14);
    Sound_PlayLoop(E1_ANIM(0x18), 0xb8, -1);
    for (int32_t i = 0x77; i <= 0x79; i++)
        Sound_PlayLoop(E1_ANIM(i), 0x26e, -1);
    MobObj_Wake(0x1a);
    MobObj_Wake(0x1f);
    Object_SetProperty(E1_ATURRET(0x1a), PROP_STATE, 0);
    Object_SetProperty(E1_ATURRET(0x1f), PROP_STATE, 0);
    MobObj_Wake(0x20);
    MobObj_Wake(0x21);
    MobObj_Wake(0x22);
    MobObj_Wake(0x1b);
    MobObj_Wake(0x1c);
    MobObj_Wake(0x1d);
    MobObj_Wake(0x1e);
    MobObj_Wake(0x17);
    MobObj_Wake(0x18);
    MobObj_Wake(0x27);
    MobObj_Wake(0x23);
    Object_SetDead(OBJ_ID(OT_LIGHTOBJECT, 0xc9));
    Object_SetDead(OBJ_ID(OT_LIGHTOBJECT, 0xca));
    AnimSetUpdateInterval(E1_ATURRET(0x22), 0x64);
    for (int32_t i = 0x1b; i <= 0x1e; i++)
        AnimSetUpdateInterval(E1_ATURRET(i), 0x64);
    AnimPlay(E1_ATURRET(0x22), 0x20, 0xe1);
    AnimPlay(E1_ATURRET(0x1b), 0x22, 0xe1);
    AnimPlay(E1_ATURRET(0x1c), 0x22, 0xe1);
    AnimPlay(E1_ATURRET(0x1d), 0x23, 0xe1);
    AnimPlay(E1_ATURRET(0x1e), 0x21, 0xe1);
    Sound_PlayLoop(E1_ATURRET(0x22), 0x26c, -1);
    for (int32_t i = 0x1b; i <= 0x1e; i++)
        Sound_PlayLoop(E1_ATURRET(i), 0x26c, -1);
    for (int32_t i = 0x42; i <= 0x4a; i++)
        FuelDump_SetFuel(OBJ_ID(OT_FUELDUMP, i), 1000000);
    for (int32_t i = 0x55; i <= 0x5d; i++)                       // the cargo bay watches the pod
        Object_SetProperty(E1_TRIGGER(i), PROP_TARGET, E1_CONTAINER);
    MissionScore_Init(E1_LANDER, 900000, 11);                     // 0x10001530
    Media_AudioPlay(0);
}

// 0x10003fc0  LevelStartLevel_Lander (export 0x10001010)
static void E1_StartLander(void)
{
    MobObj_Wake(E1_MOB_LANDER);
    MobObj_Wake(E1_MOB_CAMERA);
    Object_SetProperty(E1_CAMERA, PROP_TARGET, E1_LANDER);
    E1_Setup();
}

// 0x10004000  LevelStartLevel_Flycam (export 0x10001020)
static void E1_StartFlycam(void)
{
    E1_Setup();
    MobObj_Wake(E1_MOB_FLYCAM);
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void E1_Stop(void)
{
}

// 0x10001040
static uint32_t E1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001050, return 0. Port: fresh DLL data.
static int32_t E1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    memcpy(st.seqA, e1_seqAInit, sizeof st.seqA);
    memcpy(st.seqB, e1_seqBInit, sizeof st.seqB);
    st.container = e1_containerInit;
    MissionScore_Reset();
    AnimSeq_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x100091b8, .count = 3 * 8, .out = e1_text },
    { 0 },
};

const mission_script_t mission_europa_1 = {
    .name          = "EUROPA\\1",
    .get_version   = E1_GetVersion,
    .load          = E1_Load,
    .start_lander  = E1_StartLander,
    .start_flycam  = E1_StartFlycam,
    .start_animcam = NULL,
    .stop          = E1_Stop,
    .text          = e1_gametext,
};
