// MISSIONS\MIMAS\1\MISSIONR.DLL - faithful C port.
//
// Mission: enter the complex and find the flight recorder (pod 0x10c000e) at waypoint 1. Opening
// either inner door 0x30035/36 (triggers 0x4003e/3f) or crossing trigger planes 0x1a0075/76 sets off
// the alarm (14 lights, a siren every 10 s; the sealed-area line at door 0x30038) until
// switch 0xa0014 turns it off for good (its line). Switch 0xa0092 arms trigger 0x40091,
// which wakes the guard machinery (mobs 2..7, 0xc, 0xf, 0x1c, anim turrets). Hooking the recorder
// shows the evacuation objective; above y = 10000: +450000 cash, MissionScore_End(1), and the
// end film CUT3 if Titan 2 ended with result 1. Par time 30 s (sic), 7 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001200,
// LevelStartLevel_Lander 0x10001210 (-> 0x10003850), LevelStartLevel_Flycam 0x10001220
// (-> 0x10003890), LevelStopLevel 0x10001230 (-> empty 0x100038b0), LevelGetVersion 0x10001240.
// The score library (0x10001250-0x100017ff, Init 0x10001720, End 0x100015a0) and the
// destroyed-sound handler (0x10002c30) are the common code in mission_lib.c, and so is this DLL's
// AnimSeq (0x10001000-0x100011f1, slots 0x1000b1e0: AnimSeq_Stop 0x10001190, Init 0x100011c0).
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define MI_LANDER        MOB_ID(OT_LANDER, 9)              // 0x01010009
#define MI_CAMERA        MOB_ID(OT_CAMERA, 1)              // 0x01060001
#define MI_MOB_LANDER    9
#define MI_MOB_CAMERA    1
#define MI_MOB_FLYCAM    8
#define MI_RECORDER      MOB_ID(OT_POD, 0x0e)              // 0x010c000e the flight recorder
#define MI_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))        // 0x000300nn
#define MI_MOBANIM(n)    MOB_ID(OT_ANIMOBJECT, (n))        // 0x010300nn
#define MI_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))        // 0x000400nn
#define MI_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))            // 0x000a00nn
#define MI_TURRET(n)     OBJ_ID(OT_TURRET, (n))
#define MI_ANIMTURRET(n) MOB_ID(OT_ANIMTURRET, (n))        // 0x010e00nn (mobs)
#define MI_LIGHT(n)      OBJ_ID(OT_LIGHTOBJECT, (n))       // 0x001600nn
#define MI_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))      // 0x000800nn
#define MI_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))            // 0x001300nn
#define MI_PLANE(n)      OBJ_ID(OT_TRIGGERPLANE, (n))      // 0x001a00nn
#define MI_MINE_FIRST    OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x62) // 0x000f0062..67
#define MI_MINE_LAST     OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x67)
#define MI_WORLD_TITAN   11

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t mi_dataInit[3] = { 1, 1, 1 };   // 0x10008160 / 64 / 68
// 0x10008170: [language][6]
static const char *mi_text[3][6];
static const int32_t  mi_nbWaypoints = 1;             // 0x100081b8
static const Waypoint mi_waypoints[1] = {             // 0x100081c0
    { -7936, 9728 },
};
static const TurretType mi_turretType2 = {            // 0x100081c8 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 3, 5, 80, 0x40, 1024, 8000, 0
};
static const TurretType mi_turretType1 = {            // 0x100081e8 -> gTurretTypes[1]
    { 0, 0, 145, 0 }, 0x01, 0, 3, 10, 80, 0x40, 1024, 8000, 0
};
static const TurretType mi_turretType5 = {            // 0x10008208 -> gTurretTypes[5]
    { 85, 434, 461, 0 }, 0x02, 2, 2, 25, 100, 0x20, 512, 8000, 0
};
static const TurretType mi_turretType11 = {           // 0x10008228 -> gTurretTypes[11]
    { 170, 239, 576, 0 }, 0x02, 0, 1, 0, 60, 0x40, 800, 8000, 0
};
static const TurretType mi_turretType13 = {           // 0x10008248 -> gTurretTypes[13]
    { 61, 0, 198, 0 }, 0x05, 2, 6, 5, 80, 0x40, 1024, 8000, 0
};
static const TurretType mi_turretType15 = {           // 0x10008268 -> gTurretTypes[15]
    { 61, 0, 198, 0 }, 0x05, 2, 3, 5, 80, 0x40, 800, 8000, 0
};
// 0x10008288-0x100082cf: AnimSeq sequences {anim, repeat, flags} ... 0xff (writable .data)
static const uint8_t mi_seqImage[0x48] = {
    0x0f, 0x08, 0x60, 0x10, 0x02, 0xe0, 0xff, 0x00,  0, 0, 0, 0, 0, 0, 0, 0,   // 0x10008288 (unused)
    0x0f, 0x0a, 0x60, 0x10, 0x02, 0xe0, 0x0f, 0x0a, 0x60, 0x10, 0x02, 0xe0, 0xff, 0, 0, 0,  // 0x10008298
    0x0f, 0x05, 0x60, 0x10, 0x02, 0xe0, 0x0f, 0x06, 0x60, 0x10, 0x02, 0xe0, 0x0f, 0x01, 0x60, 0xff, // 0x100082a8
    0, 0, 0, 0, 0, 0, 0, 0,                                                     // 0x100082b8
    0x0f, 0x03, 0x60, 0x10, 0x02, 0xe0, 0x0f, 0x06, 0x60, 0x10, 0x02, 0xe0, 0x0f, 0x03, 0x60, 0xff, // 0x100082c0
};
#define MI_SEQ(va)       (&st.seq[(va) - 0x10008288u])
static const char mi_movie[] = "CUT3";               // 0x100085a8

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      firstVisit1E;  // 0x10008160 (.data 1) cleared on entering trigger 0x4001e
    int32_t      zapPhase;      // 0x10008164 (.data 1) +1 / -1, flipped every 4 s
    int32_t      gate51;        // 0x10008168 (.data 1) gate 0x30051 state (never set to 0)
    uint8_t      seq[0x48];     // 0x10008288 the sequences (writable copy)
    Window      *win;           // 0x1000acec objective line
    int32_t      alarmOffDone;  // 0x1000acf0 the alarm was switched off
    int32_t      sirenTicks;    // 0x1000acf4
    int32_t      machineHall;   // 0x1000acf8 trigger 0x40094 was entered
    int32_t      hallStarted;   // 0x1000acfc (written, never read)
    int32_t      hallSeconds;   // 0x1000ad00
    int32_t      alarm;         // 0x1000ad04 alarm on
    int32_t      inLift68;      // 0x1000ad0c
    int32_t      lift1BUp;      // 0x1000ad10 lift 0x3001b is up
    int32_t      zapTicks;      // 0x1000ad18
    int32_t      restoreIndex;  // 0x1000ad1c text restored after a message (never written: 0)
    int32_t      guardsWoken;   // 0x1000ad28
    int32_t      fan1A;         // 0x1000ad2c fan 0x3001a started
    int32_t      hallLocked;    // 0x1000ad30
    int32_t      guardSwitch;   // 0x1000ad34 switch 0xa0092 was shot
    int32_t      shieldAtStart; // 0x1000b170 (written, never read)
    const char *const *text;    // 0x1000b174 = mi_text[gLanguage]
} st;

// ================================================================================================
// Mission
// ================================================================================================

// 0x10001800  (InTime target) restore the objective line after a message
static void MI_RestoreText(void)
{
    WindowSetText(st.win, st.text[st.restoreIndex]);
}

// 0x10001820  EV_ANIM_TURNED: fan 0x3001a anim 1 reached its end: its sound changes
static void MI_OnFanTurned(const LanderEvent *ev)
{
    if (ev->arg[1] != 1 || (ObjId)ev->arg[0] != MI_ANIM(0x1a))
        return;
    Sound_StopObject(MI_ANIM(0x1a));
    Sound_PlayOnce(MI_ANIM(0x1a), 0xca);
}

static void MI_AlarmOn(void);
static void MI_GuardsWake(void);
static void MI_ZapTick(const LanderEvent *ev);
static void MI_HallTick(const LanderEvent *ev);

// 0x10001860  EV_TRIGGER_ENTER: the level's doors, lifts and events
static void MI_OnTriggerEnter(const LanderEvent *ev)
{
    ObjId t = (ObjId)ev->arg[1];
    if (t == MI_TRIGGER(0x6d)) {
        AnimSetUpdateInterval(MI_ANIM(0x6b), 0x14);
        AnimPlay(MI_ANIM(0x6b), 3, 0);
        Object_SetProperty(MI_TURRET(0x8f), PROP_STATE, 1);
        Object_SetProperty(MI_TURRET(0x90), PROP_STATE, 1);
    }
    if (t == MI_TRIGGER(0x6e)) {
        AnimSetUpdateInterval(MI_ANIM(0x6a), 0x14);
        AnimPlay(MI_ANIM(0x6a), 3, 0);
        Object_SetProperty(MI_TURRET(0x8d), PROP_STATE, 1);
        Object_SetProperty(MI_TURRET(0x8e), PROP_STATE, 1);
    }
    if (t == MI_TRIGGER(0x3e)) {
        AnimPlay(MI_ANIM(0x36), 2, 0);
        Sound_PlayOnce(MI_ANIM(0x36), 0x3f6);
        if (st.alarm == 0 && st.alarmOffDone == 0)
            MI_AlarmOn();
    }
    if (t == MI_TRIGGER(0x3f)) {
        AnimPlay(MI_ANIM(0x35), 2, 0);
        Sound_PlayOnce(MI_ANIM(0x35), 0x3f6);
        if (st.alarm == 0 && st.alarmOffDone == 0)
            MI_AlarmOn();
    }
    if (t == MI_TRIGGER(0x30)) {
        if (st.alarm == 0) {
            AnimPlay(MI_ANIM(0x38), 2, 0);
            Sound_PlayOnce(MI_ANIM(0x38), 0x3f6);
        } else {
            WindowSetText(st.win, st.text[2]);       // the sealed-area line
            Sound_PlayOnce(MI_LANDER, 0xec);
            InTime0(2000, MI_RestoreText);
        }
    }
    if (t == MI_TRIGGER(0x31)) {
        AnimSetUpdateInterval(MI_ANIM(0x37), 0xa);
        AnimPlay(MI_ANIM(0x37), 2, 0);
        Sound_PlayOnce(MI_ANIM(0x37), 0x3f6);
    }
    if (t == MI_TRIGGER(0x60)) {
        AnimSetUpdateInterval(MI_ANIM(0x6c), 0xa);
        AnimPlay(MI_ANIM(0x6c), 3, 0);
        Sound_PlayOnce(MI_ANIM(0x6c), 0x3f6);
    }
    if (t == MI_TRIGGER(0x61)) {
        if (st.hallLocked == 0) {
            AnimSetUpdateInterval(MI_ANIM(0x79), 0xa);
            AnimSetUpdateInterval(MI_ANIM(0x7a), 0xa);
            AnimPlay(MI_ANIM(0x7a), 4, 0);
            AnimPlay(MI_ANIM(0x79), 4, 0);
            Sound_PlayOnce(MI_ANIM(0x79), 0x3f6);
            Sound_PlayOnce(MI_ANIM(0x7a), 0x3f6);
        } else {
            Sound_PlayOnce(MI_ANIM(0x7a), 0xec);
        }
    }
    if (t == MI_TRIGGER(0x94) && st.machineHall == 0) {
        st.machineHall = 1;
        st.hallSeconds = 0;
        st.hallLocked = 1;
        Sound_PlayOnce(MI_LANDER, 0xec);
        EventType_AddHandler(EV_TICK_1S, NULL, MI_HallTick);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x91) && st.guardsWoken == 0 && st.guardSwitch == 1)
        MI_GuardsWake();
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x1e)) {
        if (st.fan1A == 0) {
            AnimSetUpdateInterval(MI_ANIM(0x1a), 0x37);
            AnimPlay(MI_ANIM(0x1a), 1, 0);
            Sound_PlayOnce(MI_ANIM(0x1a), 0xc8);
            Sound_PlayLoop(MI_ANIM(0x1a), 0xc9, -1);
            EventType_AddHandler(EV_ANIM_TURNED, NULL, MI_OnFanTurned);
            st.fan1A = 1;
        }
        if (st.firstVisit1E == 1)
            st.firstVisit1E = 0;
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x68)) {
        AnimSetUpdateInterval(MI_ANIM(0x1b), 0x32);
        AnimPlay(MI_ANIM(0x1b), 3, 0);
        Sound_PlayOnce(MI_ANIM(0x1b), 0xaa);
        st.lift1BUp = 1;
        st.inLift68 = 1;
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x69) && st.inLift68 == 1)
        st.lift1BUp = 0;
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x9f)) {
        AnimSetUpdateInterval(MI_ANIM(0xa0), 0xa);
        AnimSetUpdateInterval(MI_ANIM(0x9e), 0xa);
        AnimPlay(MI_ANIM(0xa0), 8, 0);
        AnimPlay(MI_ANIM(0x9e), 9, 0);
        Sound_PlayOnce(MI_ANIM(0xa0), 0xaa);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x5f)) {
        AnimSetUpdateInterval(MI_ANIM(0x77), 0xa);
        AnimSetUpdateInterval(MI_ANIM(0x78), 0xa);
        AnimPlay(MI_ANIM(0x77), 4, 0);
        AnimPlay(MI_ANIM(0x78), 4, 0);
        Sound_PlayOnce(MI_ANIM(0x77), 0x3f6);
        Sound_PlayOnce(MI_ANIM(0x78), 0x3f6);
    }
    // compares the trigger with two anim object ids: never true (dead code in the original)
    if ((ObjId)ev->arg[1] == MI_ANIM(0x4a) || (ObjId)ev->arg[1] == MI_ANIM(0x4b))
        EventType_AddHandler(EV_TICK_100MS, NULL, MI_ZapTick);
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x5d)) {
        AnimSetUpdateInterval(MI_ANIM(0x7b), 0xa);
        AnimSetUpdateInterval(MI_ANIM(0x7c), 0xa);
        AnimPlay(MI_ANIM(0x7b), 4, 0);
        AnimPlay(MI_ANIM(0x7c), 4, 0);
        Sound_PlayOnce(MI_ANIM(0x7b), 0x3f6);
        Sound_PlayOnce(MI_ANIM(0x7c), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x5e)) {
        AnimSetUpdateInterval(MI_ANIM(0x3a), 0x14);
        AnimPlay(MI_ANIM(0x3a), 2, 0);
        Sound_PlayOnce(MI_ANIM(0x3a), 0x3f6);
    }
}

// 0x10001d80  EV_TRIGGER_LEAVE
static void MI_OnTriggerLeave(const LanderEvent *ev)
{
    ObjId t = (ObjId)ev->arg[1];
    if (t == MI_TRIGGER(0x68) && st.lift1BUp == 1) {
        AnimSetUpdateInterval(MI_ANIM(0x1b), 0x32);
        AnimPlay(MI_ANIM(0x1b), 3, 4);
        Sound_PlayOnce(MI_ANIM(0x1b), 0xaa);
        st.lift1BUp = 0;
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x69) && st.lift1BUp == 1) {
        AnimSetUpdateInterval(MI_ANIM(0x1b), 0x32);
        AnimPlay(MI_ANIM(0x1b), 3, 4);
        Sound_PlayOnce(MI_ANIM(0x1b), 0xaa);
        st.lift1BUp = 0;
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x6d)) {
        AnimSetUpdateInterval(MI_ANIM(0x6b), 0x14);
        AnimPlay(MI_ANIM(0x6b), 3, 4);
        Sound_PlayOnce(MI_ANIM(0x6b), 0x3f6);
        Object_SetProperty(MI_TURRET(0x8f), PROP_STATE, 0);
        Object_SetProperty(MI_TURRET(0x90), PROP_STATE, 0);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x6e)) {
        AnimSetUpdateInterval(MI_ANIM(0x6a), 0x14);
        AnimPlay(MI_ANIM(0x6a), 3, 4);
        Sound_PlayOnce(MI_ANIM(0x6a), 0x3f6);
        Object_SetProperty(MI_TURRET(0x8d), PROP_STATE, 0);
        Object_SetProperty(MI_TURRET(0x8e), PROP_STATE, 0);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x3e)) {
        AnimPlay(MI_ANIM(0x36), 2, 4);
        Sound_PlayOnce(MI_ANIM(0x36), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x3f)) {
        AnimPlay(MI_ANIM(0x35), 2, 4);
        Sound_PlayOnce(MI_ANIM(0x35), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x30) && st.alarm == 0) {
        AnimPlay(MI_ANIM(0x38), 2, 4);
        Sound_PlayOnce(MI_ANIM(0x38), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x60)) {
        AnimSetUpdateInterval(MI_ANIM(0x6c), 0xa);
        AnimPlay(MI_ANIM(0x6c), 3, 4);
        Sound_PlayOnce(MI_ANIM(0x6c), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x61) && st.hallLocked == 0) {
        AnimSetUpdateInterval(MI_ANIM(0x7a), 0xa);
        AnimSetUpdateInterval(MI_ANIM(0x79), 0xa);
        AnimPlay(MI_ANIM(0x7a), 4, 4);
        AnimPlay(MI_ANIM(0x79), 4, 4);
        Sound_PlayOnce(MI_ANIM(0x79), 0x3f6);
        Sound_PlayOnce(MI_ANIM(0x7a), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x5f)) {
        AnimSetUpdateInterval(MI_ANIM(0x77), 0xa);
        AnimSetUpdateInterval(MI_ANIM(0x78), 0xa);
        AnimPlay(MI_ANIM(0x77), 4, 4);
        AnimPlay(MI_ANIM(0x78), 4, 4);
        Sound_PlayOnce(MI_ANIM(0x77), 0x3f6);
        Sound_PlayOnce(MI_ANIM(0x78), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_ANIM(0x4a) || (ObjId)ev->arg[1] == MI_ANIM(0x4b))
        EventType_RemoveHandler(EV_TICK_100MS, MI_ZapTick);
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x5d)) {
        AnimPlay(MI_ANIM(0x7b), 4, 4);
        AnimPlay(MI_ANIM(0x7c), 4, 4);
        Sound_PlayOnce(MI_ANIM(0x7b), 0x3f6);
        Sound_PlayOnce(MI_ANIM(0x7c), 0x3f6);
    }
    if ((ObjId)ev->arg[1] == MI_TRIGGER(0x5e)) {
        AnimPlay(MI_ANIM(0x3a), 2, 4);
        Sound_PlayOnce(MI_ANIM(0x3a), 0x3f6);
    }
}

// 0x100020f0  EV_TICK_100MS (never registered, see above): -10 shield while the zapper is on
static void MI_ZapTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.zapPhase == 1)
        Object_SetProperty(MI_LANDER, PROP_HITPOINTS, Object_GetProperty(MI_LANDER, PROP_HITPOINTS) - 10);
}

static void MI_SirenTick(const LanderEvent *ev);

// 0x10002120  the alarm goes on: 14 warning lights, siren every 10 s
static void MI_AlarmOn(void)
{
    st.alarm = 1;
    for (int32_t n = 6; n <= 0x13; n++)
        Object_SetAlive(MI_LIGHT(n));
    st.sirenTicks = 0;
    EventType_AddHandler(EV_TICK_1S, NULL, MI_SirenTick);
}

// 0x100021e0  switch 0xa0014: the alarm is off for good; six panels change model, lights off
static void MI_AlarmOff(void)
{
    st.alarm = 0;
    st.alarmOffDone = 1;
    EventType_RemoveHandler(EV_TICK_1S, MI_SirenTick);
    Object_AdvanceDestroyLevel(MI_DESTR(0x34), 1);
    Object_AdvanceDestroyLevel(MI_DESTR(0x3d), 1);
    Object_AdvanceDestroyLevel(MI_DESTR(0x3c), 1);
    Object_AdvanceDestroyLevel(MI_DESTR(0x33), 1);
    Object_AdvanceDestroyLevel(MI_DESTR(0x32), 1);
    Object_AdvanceDestroyLevel(MI_DESTR(0x3b), 1);
    for (int32_t n = 7; n <= 0x13; n++)
        Object_SetDead(MI_LIGHT(n));
    Object_SetDead(MI_LIGHT(6));
}

// 0x100022f0  EV_TICK_1S while the alarm is on
static void MI_SirenTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.sirenTicks != 10)
        return;
    Sound_PlayLoop(OBJ_NONE, 0x3a6, 2);
    st.sirenTicks = 0;
}

// 0x10002320  EV_COLLISION {body, other, impulse}: touching a mine arms it: it blows up 1 s later
static void MI_OnMineTouched(const LanderEvent *ev)
{
    int32_t mine = (int32_t)ev->arg[1];
    if (mine < (int32_t)MI_MINE_FIRST || mine > (int32_t)MI_MINE_LAST)
        return;
    InTime3(1000, Object_SetProperty, ev->arg[1], PROP_HITPOINTS, 0);
    Object_SetProperty((ObjId)ev->arg[1], PROP_EXPLOSION, 0x64);
    Sound_StopObject((ObjId)ev->arg[1]);
    Sound_PlayOnce((ObjId)ev->arg[1], 0x102);
}

// 0x10002390  EV_TICK_1S after entering the machine hall (trigger 0x40094): its door 0x30079/7a
// shuts behind the lander and the hall comes to life step by step (never removed)
static void MI_HallTick(const LanderEvent *ev)
{
    (void)ev;
    st.hallSeconds++;
    if (st.hallSeconds == 1) {
        AnimSetUpdateInterval(MI_ANIM(0x79), 0xa);
        AnimSetUpdateInterval(MI_ANIM(0x7a), 0xa);
        AnimPlay(MI_ANIM(0x79), 4, 4);
        AnimPlay(MI_ANIM(0x7a), 4, 4);
        Sound_PlayOnce(MI_ANIM(0x79), 0x3f6);
        Sound_PlayOnce(MI_ANIM(0x7a), 0x3f6);
        AnimSetUpdateInterval(MI_ANIM(0x97), 0x32);
        AnimPlay(MI_ANIM(0x97), 0xe, 0);
        Sound_PlayOnce(MI_ANIM(0x97), 0xa9);
        st.hallStarted = 1;
    }
    if (st.hallSeconds == 2) {
        MobObj_Wake(0x18);
        Sound_PlayLoop(MI_ANIMTURRET(0x18), 0x8f, -1);
        MobObj_Wake(0xd);
        Sound_PlayLoop(MI_ANIMTURRET(0x0d), 0x8f, -1);
        AnimSetUpdateInterval(MI_ANIM(0x9b), 0x14);
        AnimSetUpdateInterval(MI_ANIM(0x98), 0x14);
        AnimPlay(MI_ANIM(0x9b), 3, 0);
        AnimPlay(MI_ANIM(0x98), 3, 0);
        Sound_PlayOnce(MI_ANIM(0x98), 0xaa);
        Sound_PlayOnce(MI_ANIM(0x9b), 0xaa);
        AnimPlay(MI_ANIMTURRET(0x18), 0xf, 0x60);
        AnimPlay(MI_ANIMTURRET(0x0d), 0xf, 0x60);
    }
    if (st.hallSeconds == 3) {
        AnimSetUpdateInterval(MI_ANIM(0x95), 0x96);
        AnimPlay(MI_ANIM(0x95), 0xe, 0);
    }
    if (st.hallSeconds == 4) {
        AnimSetUpdateInterval(MI_ANIM(0x96), 0x96);
        AnimPlay(MI_ANIM(0x96), 0xe, 0);
    }
    if (st.hallSeconds == 7) {
        MobObj_Wake(0x16);
        Sound_PlayLoop(MI_ANIMTURRET(0x16), 0x8f, -1);
        AnimSetUpdateInterval(MI_ANIM(0x9a), 0x14);
        AnimPlay(MI_ANIM(0x9a), 3, 0);
        Sound_PlayOnce(MI_ANIM(0x9a), 0xaa);
        AnimPlay(MI_ANIMTURRET(0x16), 0xf, 0x60);
    }
    if (st.hallSeconds == 0xb) {
        MobObj_Wake(0x17);
        Sound_PlayLoop(MI_ANIMTURRET(0x17), 0x8f, -1);
        AnimSetUpdateInterval(MI_ANIM(0x99), 0x14);
        AnimPlay(MI_ANIM(0x99), 3, 0);
        Sound_PlayOnce(MI_ANIM(0x99), 0xaa);
        AnimPlay(MI_ANIMTURRET(0x17), 0xf, 0x60);
    }
    if (st.hallSeconds == 0x1b) {
        AnimSetUpdateInterval(MI_ANIM(0x95), 0x1c2);
        AnimPlay(MI_ANIM(0x95), 0xe, 4);
    }
    if (st.hallSeconds == 0x1d) {
        AnimSetUpdateInterval(MI_ANIM(0x96), 0x1c2);
        AnimPlay(MI_ANIM(0x96), 0xe, 4);
    }
}

// 0x10002650  EV_SWITCH {switch, state, shooter}
static void MI_OnSwitch(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == MI_SWITCH(0x14) && st.alarmOffDone == 0) {
        WindowSetText(st.win, st.text[3]);       // the alarm-off line
        Sound_PlayOnce(MI_SWITCH(0x14), 0xeb);
        InTime0(5000, MI_RestoreText);
        MI_AlarmOff();
    }
    if ((ObjId)ev->arg[0] == MI_SWITCH(0x92)) {
        Sound_PlayOnce(MI_SWITCH(0x92), 0xeb);
        st.guardSwitch = 1;
        AnimPlay(MI_ANIM(0x7d), 4, 0);
        AnimPlay(MI_ANIM(0x7e), 4, 0);
        Sound_PlayOnce(MI_ANIM(0x7d), 0xa9);
        Sound_PlayOnce(MI_ANIM(0x7e), 0xa9);
    }
    if ((ObjId)ev->arg[0] == MI_SWITCH(0x4f) || (ObjId)ev->arg[0] == MI_SWITCH(0x4e)) {
        AnimSetUpdateInterval(MI_ANIM(0x51), 0x14);
        if (st.gate51 == 1) {
            AnimPlay(MI_ANIM(0x51), 0, 0);
            Sound_PlayOnce(MI_ANIM(0x51), 0xaa);
            Object_SetProperty(MI_SWITCH(0x4e), PROP_STATE, 1);
            Object_SetProperty(MI_SWITCH(0x4f), PROP_STATE, 1);
        } else {
            AnimPlay(MI_ANIM(0x51), 0, 4);
            Sound_PlayOnce(MI_ANIM(0x51), 0xaa);
            Object_SetProperty(MI_SWITCH(0x4e), PROP_STATE, 0);
            Object_SetProperty(MI_SWITCH(0x4f), PROP_STATE, 0);
            st.gate51 = 1;
        }
    }
    if ((ObjId)ev->arg[0] == MI_SWITCH(0x50)) {
        AnimSetUpdateInterval(MI_ANIM(0x39), 0x14);
        AnimPlay(MI_ANIM(0x39), 2, 0);
        Sound_PlayOnce(MI_ANIM(0x39), 0x3f6);
    }
}

// 0x10002810  EV_ANIM_TURNED: machine mob 0x1030003 anim 6 turned
static void MI_OnMachineTurned(const LanderEvent *ev)
{
    if (ev->arg[1] != 6 || (ObjId)ev->arg[0] != MI_MOBANIM(3))
        return;
    Sound_StopObject(MI_MOBANIM(3));
    Sound_StopObject(MI_MOBANIM(2));
    Sound_PlayOnce(MI_MOBANIM(3), 0x8f);
    Sound_PlayOnce(MI_MOBANIM(2), 0x8f);
}

// 0x10002870  trigger 0x40091 after switch 0xa0092: the guards and machines wake up
static void MI_GuardsWake(void)
{
    st.guardsWoken = 1;
    InTime1(3000, MobObj_Wake, 3);
    InTime1(3000, MobObj_Wake, 5);
    InTime1(3000, MobObj_Wake, 0xc);
    InTime1(3000, MobObj_Wake, 4);
    InTime1(3000, MobObj_Wake, 2);
    InTime1(3000, MobObj_Wake, 6);
    InTime1(3000, MobObj_Wake, 7);
    InTime1(3000, MobObj_Wake, 0xf);
    InTime1(3000, MobObj_Wake, 0x1c);
    AnimSetUpdateInterval(MI_MOBANIM(3), 8);
    AnimSetUpdateInterval(MI_MOBANIM(5), 8);
    AnimSetUpdateInterval(MI_MOBANIM(0xc), 8);
    AnimSetUpdateInterval(MI_MOBANIM(4), 8);
    AnimSetUpdateInterval(MI_MOBANIM(2), 8);
    AnimSetUpdateInterval(MOB_ID(OT_LIGHTOBJECT, 0x1c), 8);
    AnimSetUpdateInterval(MI_ANIMTURRET(6), 8);
    AnimSetUpdateInterval(MI_ANIMTURRET(7), 8);
    AnimSetUpdateInterval(MI_ANIMTURRET(0xf), 8);
    InTime3(3200, AnimPlay, MI_MOBANIM(3), 6, 0);
    InTime3(3200, AnimPlay, MI_MOBANIM(5), 0xa, 0);
    InTime3(3200, AnimPlay, MI_MOBANIM(0xc), 0xb, 0);
    InTime3(3200, AnimPlay, MI_MOBANIM(4), 0xc, 0);
    InTime3(3200, AnimPlay, MI_MOBANIM(2), 0xd, 0);
    InTime3(3200, AnimPlay, MOB_ID(OT_LIGHTOBJECT, 0x1c), 6, 0x40);
    InTime3(3200, Sound_PlayLoop, MI_MOBANIM(3), 0x8e, -1);
    InTime3(3200, Sound_PlayLoop, MI_MOBANIM(2), 0x8e, -1);
    InTime3(3200, AnimPlay, MI_ANIMTURRET(6), 0xb, 0xe0);
    InTime3(3200, AnimPlay, MI_ANIMTURRET(7), 0xb, 0xe0);
    InTime3(3200, AnimPlay, MI_ANIMTURRET(0xf), 0xc, 0xe0);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, MI_OnMachineTurned);
    AnimSetUpdateInterval(MI_ANIM(0x9d), 0xa);
    InTime3(3500, AnimPlay, MI_ANIM(0x9d), 7, 0);
    AnimSetUpdateInterval(MI_ANIM(0x9c), 0xa);
    InTime3(3500, AnimPlay, MI_ANIM(0x9c), 7, 0);
    InTime2(3500, Sound_PlayOnce, MI_ANIM(0x9c), 0xaa);
    InTime2(3500, Sound_PlayOnce, MI_ANIM(0x9d), 0xaa);
}

// 0x10002b20  EV_TICK_1S after the recorder is hooked: above y = 10000 the mission is won
static void MI_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(MI_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    if (Briefing_GetExitCode(MI_WORLD_TITAN, 1) == 1)
        SetLevelEndMovie(mi_movie);
    Lander_SetCash(MI_LANDER, Lander_GetCash(MI_LANDER) + 450000);
    MissionScore_End(1);                                         // 0x100015a0
    EventType_RemoveHandler(EV_TICK_1S, MI_EvacTick);
}

// 0x10002ba0  EV_POD_HOOKED (any pod; not removed, so every hook adds an evac check)
static void MI_OnPodHooked(const LanderEvent *ev)
{
    (void)ev;
    EventType_AddHandler(EV_TICK_1S, NULL, MI_EvacTick);
    Object_SetDead(MI_LIGHT(0x5c));
    WindowSetText(st.win, st.text[5]);           // the evacuation objective
    Object_SetProperty(MI_TRIGGER(0x3e), PROP_TARGET, MI_LANDER);
    Object_SetProperty(MI_TRIGGER(0x3f), PROP_TARGET, MI_LANDER);
    Pod_Script_SetupMass(MI_RECORDER, 0x320);
    Pod_Script_SetupDrag(MI_RECORDER, 0x1333);
}

// 0x10002cc0  EV_TRIGGER_PLANE {player, plane, side}: crossing plane 0x1a0075/76 (side 1) sets off
// the alarm
static void MI_OnAlarmPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MI_PLANE(0x75) && (ObjId)ev->arg[1] != MI_PLANE(0x76))
        return;
    if (ev->arg[2] != 1 || st.alarm != 0)
        return;
    MI_AlarmOn();
}

// 0x10002cf0  EV_DESTROYED: anim turret 0x15 or 0x19 destroyed: doors 0x30035/36 open, both
// turrets' sequences stop
static void MI_OnGuardDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MI_ANIMTURRET(0x15) && (ObjId)ev->arg[0] != MI_ANIMTURRET(0x19))
        return;
    AnimPlay(MI_ANIM(0x36), 2, 0);
    AnimPlay(MI_ANIM(0x35), 2, 0);
    Sound_PlayOnce(MI_ANIM(0x35), 0x3f6);
    Sound_PlayOnce(MI_ANIM(0x36), 0x3f6);
    AnimSeq_Stop(MI_ANIMTURRET(0x15));
    AnimSeq_Stop(MI_ANIMTURRET(0x19));
    EventType_RemoveHandler(EV_DESTROYED, MI_OnGuardDestroyed);
}

// 0x10002d80  EV_PICKUP: +50 % shield, cash, missiles
static void MI_OnPickup(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)MI_PICKUP(0x03) && id <= (int32_t)MI_PICKUP(0x05)) {
        int32_t shield = (int32_t)Object_GetProperty(MI_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(MI_LANDER, 50);
        Object_SetProperty(MI_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == MI_PICKUP(0x73) || (ObjId)ev->arg[0] == MI_PICKUP(0x74)) {
        Lander_SetCash(MI_LANDER, Lander_GetCash(MI_LANDER) + gMediumCash);
        Voice_CashAcquired(MI_LANDER);
    }
    if ((ObjId)ev->arg[0] == MI_PICKUP(0x19)) {
        Lander_SetCash(MI_LANDER, Lander_GetCash(MI_LANDER) + gLargeCash);
        Voice_CashAcquired(MI_LANDER);
    }
    id = (int32_t)ev->arg[0];
    if (id >= (int32_t)MI_PICKUP(0x00) && id <= (int32_t)MI_PICKUP(0x02)) {
        if (Lander_AddMissiles(MI_LANDER, 2) == 0) {
            Voice_MissilesFull(MI_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(MI_LANDER);
    }
}

// 0x10002eb0  EV_TICK_1S: every 4 s the zapper 0x3004a/4b and its lights 0x16004c/4d toggle
static void MI_ZapToggle(const LanderEvent *ev)
{
    (void)ev;
    st.zapTicks++;
    if ((st.zapTicks & ~3) != st.zapTicks)
        return;
    st.zapPhase = -st.zapPhase;
    void (*fn)(ObjId) = (st.zapPhase == -1) ? Object_SetDead : Object_SetAlive;
    fn(MI_ANIM(0x4a));
    fn(MI_ANIM(0x4b));
    fn(MI_LIGHT(0x4c));
    fn(MI_LIGHT(0x4d));
}

// 0x10002f10  EV_TRIGGER_PLANE 0x1a0052..57: head light on (side 0) / off (side 1)
static void MI_OnLightPlane(const LanderEvent *ev)
{
    int32_t plane = (int32_t)ev->arg[1];
    if (plane < (int32_t)MI_PLANE(0x52) || plane > (int32_t)MI_PLANE(0x57))
        return;
    if (ev->arg[2] == 0) {
        Lander_SetHeadLight(MI_LANDER, 0x2aa, 0xccc);
        return;
    }
    Lander_SetHeadLight(MI_LANDER, 0x2aa, 0);
}

// 0x100038b0  LevelStopLevel; also registered as an EV_DESTROYED handler (empty)
static void MI_Stop(void)
{
}

static void MI_OnDestroyedNothing(const LanderEvent *ev)
{
    (void)ev;   // 0x100038b0 as an event handler
}

// 0x10002f60  common set-up
static void MI_Setup(void)
{
    st.text = mi_text[gLanguage];
    for (int32_t id = 0x6f; id <= 0x72; id++)                  // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    Object_SetProperty(MI_ANIMTURRET(0x0d), PROP_ARMOUR, 0x3c);
    static const uint32_t armour23[] = {
        0x7f, 0x80, 0x10e0015, 0x10e0019, 0x8d, 0x8e, 0x8f, 0x90, 0x10e001a, 0x10e001b, 0x10e0013,
        0x10e0014, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x10e0016, 0x10e0017, 0x89, 0x8a,
        0x8b, 0x8c, 0x10e0018,
    };
    for (size_t i = 0; i < sizeof armour23 / sizeof armour23[0]; i++)
        Object_SetProperty(armour23[i], PROP_ARMOUR, 0x23);
    Object_SetProperty(MI_TURRET(0x8d), PROP_STATE, 0);
    Object_SetProperty(MI_TURRET(0x8e), PROP_STATE, 0);
    Object_SetProperty(MI_TURRET(0x8f), PROP_STATE, 0);
    Object_SetProperty(MI_TURRET(0x90), PROP_STATE, 0);
    Object_SetProperty(MI_TRIGGER(0x9f), PROP_TARGET, MI_MOBANIM(3));
    for (int32_t id = (int32_t)MI_ANIMTURRET(6); id <= (int32_t)MI_ANIMTURRET(7); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x23);
    for (int32_t id = 0x58; id <= 0x59; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(MI_ANIMTURRET(0xf), PROP_ARMOUR, 0x2d);
    Object_SetProperty(MI_ANIMTURRET(0xf), PROP_EXPLOSION, 0x46);
    for (int32_t id = (int32_t)MI_DESTR(0x29); id <= (int32_t)MI_DESTR(0x2a); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x78);
    }
    for (int32_t id = (int32_t)MI_DESTR(0x16); id <= (int32_t)MI_DESTR(0x18); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x2b), PROP_ARMOUR, 10);
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x2b), PROP_EXPLOSION, 0x50);
    Object_SetProperty(MI_DESTR(0x15), PROP_ARMOUR, 0x32);
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x2b), PROP_EXPLOSION, 0x64);
    Object_SetProperty(MI_DESTR(0x15), PROP_ARMOUR, 0x1e);
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x2b), PROP_EXPLOSION, 0x64);
    Object_SetProperty(MI_DESTR(0xa3), PROP_ARMOUR, 10);
    SetGravity(6);
    gAirResistance = 0xd;
    gMainThrottlePower = 0x14;
    Media_AudioPlay(0);
    gNbWaypoints = mi_nbWaypoints;
    gWaypoints = mi_waypoints;
    MobObj_Wake(0x11);
    MobObj_Wake(0x10);
    MobObj_Wake(0x12);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002c30
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MI_OnTriggerEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, MI_OnTriggerLeave);
    EventType_AddHandler(EV_SWITCH, NULL, MI_OnSwitch);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, MI_OnLightPlane);
    for (int32_t n = 0x21; n <= 0x28; n++)
        AnimPlay(MI_ANIM(n), 5, 0x81);
    for (int32_t id = (int32_t)MI_ANIM(0x21); id <= (int32_t)MI_ANIM(0x27); id++)
        Sound_PlayLoop((ObjId)id, 0xb4, -1);
    for (int32_t n = 6; n <= 0x13; n++)
        Object_SetDead(MI_LIGHT(n));
    Object_SetProperty(MI_DESTR(0x2f), PROP_EXPLOSION, 0x23);
    for (int32_t n = 0x21; n <= 0x27; n++)
        AnimSetUpdateInterval(MI_ANIM(n), 7);
    EventType_AddHandler(EV_COLLISION, NULL, MI_OnMineTouched);
    AnimPlay(MI_ANIM(0x4a), 5, 0x81);
    AnimPlay(MI_ANIM(0x4b), 5, 0x81);
    for (int32_t id = (int32_t)MI_MINE_FIRST; id <= (int32_t)MI_MINE_LAST; id++)
        Sound_PlayLoop((ObjId)id, 0x101, -1);
    for (int32_t id = (int32_t)MI_MINE_FIRST; id <= (int32_t)MI_MINE_LAST; id++)
        AnimPlay((ObjId)id, 0x11, 0x81);
    for (int32_t id = (int32_t)MI_MINE_FIRST; id <= (int32_t)MI_MINE_LAST; id++)
        AnimSetUpdateInterval((ObjId)id, 0x78);
    gTurretTypes[2] = &mi_turretType2;
    gTurretTypes[1] = &mi_turretType1;
    gTurretTypes[5] = &mi_turretType5;
    gTurretTypes[11] = &mi_turretType11;
    gTurretTypes[13] = &mi_turretType13;
    gTurretTypes[15] = &mi_turretType15;
    EventType_AddHandler(EV_TICK_1S, NULL, MI_ZapToggle);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, MI_OnAlarmPlane);
    EventType_AddHandler(EV_PICKUP, NULL, MI_OnPickup);
    MobObj_Wake(0x15);
    MobObj_Wake(0x19);
    MobObj_Wake(0x1a);
    MobObj_Wake(0x1b);
    MobObj_Wake(0x13);
    MobObj_Wake(0x14);
    Sound_PlayLoop(MI_ANIMTURRET(0x15), 0x8f, -1);
    Sound_PlayLoop(MI_ANIMTURRET(0x19), 0x8f, -1);
    Sound_PlayLoop(MI_ANIMTURRET(0x1a), 0x8f, -1);
    Sound_PlayLoop(MI_ANIMTURRET(0x1b), 0x8f, -1);
    Sound_PlayLoop(MI_ANIMTURRET(0x13), 0x8f, -1);
    Sound_PlayLoop(MI_ANIMTURRET(0x14), 0x8f, -1);
    Object_SetDead(MI_TRIGGER(0x3e));
    Object_SetDead(MI_TRIGGER(0x3f));
    Object_SetProperty(MI_TRIGGER(0x3e), PROP_TARGET, MI_ANIMTURRET(0x15));
    Object_SetProperty(MI_TRIGGER(0x3f), PROP_TARGET, MI_ANIMTURRET(0x15));
    AnimPlay(MI_ANIM(0x36), 2, 0);
    AnimPlay(MI_ANIM(0x35), 2, 0);
    MobObj_Wake(0xe);
    EventType_AddHandler(EV_DESTROYED, NULL, MI_OnDestroyedNothing);   // 0x100038b0 (empty)
    EventType_AddHandler(EV_DESTROYED, NULL, MI_OnGuardDestroyed);
    AnimSeq_Init();
    AnimSeq_Start(MI_ANIMTURRET(0x15), MI_SEQ(0x10008298), 1);
    AnimSeq_Start(MI_ANIMTURRET(0x19), MI_SEQ(0x10008298), 1);
    AnimSeq_Start(MI_ANIMTURRET(0x1b), MI_SEQ(0x100082a8), 1);
    AnimSeq_Start(MI_ANIMTURRET(0x13), MI_SEQ(0x100082c0), 1);
    AnimSeq_Start(MI_ANIMTURRET(0x14), MI_SEQ(0x100082c0), 1);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    MissionScore_Init(MI_LANDER, 30000, 7);                    // 0x10001720
    EventType_AddHandler(EV_POD_HOOKED, NULL, MI_OnPodHooked);
    st.shieldAtStart = (int32_t)Object_GetProperty(MI_LANDER, PROP_HITPOINTS);
}

// 0x10003850  LevelStartLevel_Lander (export 0x10001210)
static void MI_StartLander(void)
{
    MobObj_Wake(MI_MOB_LANDER);
    MobObj_Wake(MI_MOB_CAMERA);
    Object_SetProperty(MI_CAMERA, PROP_TARGET, MI_LANDER);
    MI_Setup();
}

// 0x10003890  LevelStartLevel_Flycam (export 0x10001220)
static void MI_StartFlycam(void)
{
    MI_Setup();
    MobObj_Wake(MI_MOB_FLYCAM);
}

// 0x10001240
static uint32_t MI_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001200  two calls of the empty 0x100038b0, return 0. Port: fresh DLL data.
static int32_t MI_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.firstVisit1E = mi_dataInit[0];
    st.zapPhase = mi_dataInit[1];
    st.gate51 = mi_dataInit[2];
    memcpy(st.seq, mi_seqImage, sizeof st.seq);
    MissionScore_Reset();
    AnimSeq_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText mi_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10008170, .count = 3 * 6, .out = mi_text },
    { 0 },
};

const mission_script_t mission_mimas_1 = {
    .name          = "MIMAS\\1",
    .get_version   = MI_GetVersion,
    .load          = MI_Load,
    .start_lander  = MI_StartLander,
    .start_flycam  = MI_StartFlycam,
    .start_animcam = NULL,
    .stop          = MI_Stop,
    .text          = mi_gametext,
};
