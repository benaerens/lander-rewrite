// MISSIONS\MARS\3\MISSIONR.DLL - faithful C port.
//
// Mission: carry the virus pod (mob 0x1b, hooked to the lander from the start) down to the AI
// core and insert it. The installation has two airlocks (only one door of each open at a time),
// a spinning fan 0x30033 above the core whose wind (force box 0x5b) stops once the switch 0xa00b3
// is shot, mines that blow when approached, pop-up turrets that rise when the lander enters the
// top hall (trigger 0x40041). Delivering the pod into trigger 0x40034 (only after the switch) wakes
// the installed virus (mob 0x1c), starts the alarm and the machines; then climb above y = 30000:
// success 1 and +30000 cash. Par 200 s, 2 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040. The score library (0x10001050-0x100015f4, same code as
// mars_1a) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define M3_LANDER        MOB_ID(OT_LANDER, 8)            // 0x01010008
#define M3_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define M3_MOB_CAMERA    1
#define M3_MOB_FLYCAM    2
#define M3_MOB_LANDER    8
#define M3_MOB_POD       0x1b
#define M3_MOB_VIRUS     0x1c
#define M3_POD           MOB_ID(OT_POD, 0x1b)            // 0x010c001b the virus, carried
#define M3_FAN           OBJ_ID(OT_ANIMOBJECT, 0x33)     // 0x00030033 above the core
#define M3_FAN_SWITCH    OBJ_ID(OT_SWITCH, 0xb3)         // 0x000a00b3
#define M3_FAN_WIND      OBJ_ID(OT_FORCEBOX, 0x5b)       // 0x0010005b
#define M3_CORE_TRIGGER  OBJ_ID(OT_TRIGGERBOX, 0x34)     // 0x00040034
#define M3_ATURRET(n)    MOB_ID(OT_ANIMTURRET, (n))      // 0x010e00nn
#define M3_MOBANIM(n)    MOB_ID(OT_ANIMOBJECT, (n))      // 0x010300nn
#define M3_EMITTER(n)    MOB_ID(OT_PARTICLEEMITTER, (n)) // 0x010d00nn
#define M3_LIGHT(n)      OBJ_ID(OT_LIGHTOBJECT, (n))     // 0x001600nn
#define M3_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define M3_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn

// ---- static data (.data) --------------------------------------------------------------------------
static const uint8_t m3_virusLockedInit = 1;            // 0x10008160 cleared by the fan switch
static const int32_t  m3_nbWaypoints = 2;               // 0x10008164
static const Waypoint m3_waypoints[2] = {               // 0x10008168
    { 6400, -134400 },
    { 6400, -44800 },
};
static const TurretType m3_turretType2 = {              // 0x10008178 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 1, 0, 85, 0x10, 800, 12000, 0
};
static const TurretType m3_turretType5 = {              // 0x10008198 -> gTurretTypes[5]
    { 85, 434, 461, 0 }, 0x02, 2, 1, 0, 60, 0x20, 800, 12000, 0
};
// 0x100081b8: [language][5] string pointers (strings at 0x10008220-0x10008462); [4] is unused
static const char *m3_text[3][5];
// 0x100081f8: next animation (+ 0xe) of the tubes 0x3005e..69, used round robin
static const int32_t m3_tubeAnims[10] = { 0, 2, 1, 1, 2, 0, 2, 0, 1, 2 };

// mobs woken (and their emitter sound started) when the virus is inserted, in the DLL's order
static const uint8_t m3_alarmMobs[31] = {
    0x23, 0x2e, 0x39, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
    0x2b, 0x2c, 0x2d, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x3a, 0x3b,
};

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    uint8_t      virusLocked;   // 0x10008160 (.data, 1) 0 once the fan switch was shot
    Window      *win;           // 0x1000abb0 objective line
    uint8_t      virusIn;       // 0x1000abb4 virus inserted
    int32_t      fanTicks;      // 0x1000abb8 seconds of fan spin-down
    int32_t      lockAOpen[2];      // 0x1000abc0 airlock A (doors 0x30013/15 and 0x30016/19)
    int32_t      lockAClosing[2];   // 0x1000abc8
    int32_t      lockAInZone[2];    // 0x1000abd0 triggers 0x4001e/1f
    int32_t      lockBOpen[2];      // 0x1000abd8 airlock B (doors 0x30017/1a and 0x30018/1b)
    int32_t      lockBClosing[2];   // 0x1000abe0
    int32_t      lockBInZone[2];    // 0x1000abe8 triggers 0x40020/21
    int32_t      alarmTicks;    // 0x1000abf0
    int32_t      alarmCount;    // 0x1000abf4
    int32_t      tubeIndex;     // 0x1000abf8
    const char *const *text;    // 0x1000b038 = m3_text[gLanguage]
} st;

// 0x10001600  airlock A, door `side` opens (flags 0) or closes (4)
static void M3_LockADoor(int32_t side, int32_t flags)
{
    if (side == 0) {
        AnimPlay(M3_ANIM(0x13), 4, flags);
        AnimPlay(M3_ANIM(0x15), 5, flags);
        AnimSetUpdateInterval(M3_ANIM(0x13), 5);
        AnimSetUpdateInterval(M3_ANIM(0x15), 5);
        Sound_PlayOnce(M3_ANIM(0x13), 0x3f7);
    } else {
        AnimPlay(M3_ANIM(0x16), 4, flags);
        AnimPlay(M3_ANIM(0x19), 5, flags);
        AnimSetUpdateInterval(M3_ANIM(0x16), 5);
        AnimSetUpdateInterval(M3_ANIM(0x19), 5);
        Sound_PlayOnce(M3_ANIM(0x16), 0x3f7);
    }
    if (flags == 0)
        st.lockAOpen[side] = 1;
    else
        st.lockAClosing[side] = 1;
}

// 0x100016d0  EV_ANIM_FINISHED: an airlock-A door finished closing -> open the other one if the
// lander waits there
static void M3_OnLockAAnim(const LanderEvent *ev)
{
    int32_t i;
    if ((ObjId)ev->arg[0] == M3_ANIM(0x13))
        i = 0;
    else if ((ObjId)ev->arg[0] == M3_ANIM(0x16))
        i = 1;
    else
        return;
    if (st.lockAClosing[i] == 0)
        return;
    st.lockAClosing[i] = 0;
    st.lockAOpen[i] = 0;
    if (st.lockAInZone[1 - i] != 0)
        M3_LockADoor(1 - i, 0);
}

// 0x10001730  EV_TRIGGER_ENTER/LEAVE triggers 0x4001e/1f: airlock A
static void M3_OnLockATrigger(const LanderEvent *ev)
{
    int32_t i;
    if ((ObjId)ev->arg[1] == M3_TRIGGER(0x1e))
        i = 0;
    else if ((ObjId)ev->arg[1] == M3_TRIGGER(0x1f))
        i = 1;
    else
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        st.lockAInZone[i] = 1;
        if (st.lockAOpen[1 - i] == 0)
            M3_LockADoor(i, 0);
    } else {
        int32_t open = st.lockAOpen[i];
        st.lockAInZone[i] = 0;
        if (open)
            M3_LockADoor(i, 4);
    }
}

// 0x100017a0  airlock B, door `side`
static void M3_LockBDoor(int32_t side, int32_t flags)
{
    if (side == 0) {
        AnimPlay(M3_ANIM(0x17), 3, flags);
        AnimPlay(M3_ANIM(0x1a), 2, flags);
        AnimSetUpdateInterval(M3_ANIM(0x17), 5);
        AnimSetUpdateInterval(M3_ANIM(0x1a), 5);
        Sound_PlayOnce(M3_ANIM(0x17), 0x3f7);
    } else {
        AnimPlay(M3_ANIM(0x18), 3, flags);
        AnimPlay(M3_ANIM(0x1b), 2, flags);
        AnimSetUpdateInterval(M3_ANIM(0x18), 5);
        AnimSetUpdateInterval(M3_ANIM(0x1b), 5);
        Sound_PlayOnce(M3_ANIM(0x18), 0x3f7);
    }
    if (flags == 0)
        st.lockBOpen[side] = 1;
    else
        st.lockBClosing[side] = 1;
}

// 0x10001870  EV_ANIM_FINISHED: airlock B door closed
static void M3_OnLockBAnim(const LanderEvent *ev)
{
    int32_t i;
    if ((ObjId)ev->arg[0] == M3_ANIM(0x17))
        i = 0;
    else if ((ObjId)ev->arg[0] == M3_ANIM(0x18))
        i = 1;
    else
        return;
    if (st.lockBClosing[i] == 0)
        return;
    st.lockBClosing[i] = 0;
    st.lockBOpen[i] = 0;
    if (st.lockBInZone[1 - i] != 0)
        M3_LockBDoor(1 - i, 0);
}

// 0x100018d0  EV_TRIGGER_ENTER/LEAVE triggers 0x40020/21: airlock B
static void M3_OnLockBTrigger(const LanderEvent *ev)
{
    int32_t i;
    if ((ObjId)ev->arg[1] == M3_TRIGGER(0x20))
        i = 0;
    else if ((ObjId)ev->arg[1] == M3_TRIGGER(0x21))
        i = 1;
    else
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        st.lockBInZone[i] = 1;
        if (st.lockBOpen[1 - i] == 0)
            M3_LockBDoor(i, 0);
    } else {
        int32_t open = st.lockBOpen[i];
        st.lockBInZone[i] = 0;
        if (open)
            M3_LockBDoor(i, 4);
    }
}

// 0x10001940  EV_DESTROYED 0x80025 (after trigger 0x40087): the rock 0xf0084 falls 2 s later
static void M3_OnDestroyed25(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_DESTRUCTIBLE, 0x25))
        return;
    InTime3(2000, Object_SetProperty, OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x84), PROP_HITPOINTS, 0);
    EventType_RemoveHandler(EV_DESTROYED, M3_OnDestroyed25);
}

// 0x10001980  EV_DESTROYED 0x80029 (after trigger 0x40088): rocks 0xf0086 and 0xf0085
static void M3_OnDestroyed29(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_DESTRUCTIBLE, 0x29))
        return;
    InTime3(2000, Object_SetProperty, OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x86), PROP_HITPOINTS, 0);
    InTime3(3000, Object_SetProperty, OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x85), PROP_HITPOINTS, 0);
    EventType_RemoveHandler(EV_DESTROYED, M3_OnDestroyed29);
}

// 0x100019e0  EV_TRIGGER_ENTER trigger 0x40087: rock 0xf0084 wobbles
static void M3_OnTrigger87(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0x87))
        return;
    InTime3(500, AnimPlay, OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x84), 6, 0);
    InTime2(500, Sound_PlayOnce, OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x84), 0x3f6);
    EventType_AddHandler(EV_DESTROYED, NULL, M3_OnDestroyed25);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M3_OnTrigger87);
}

// 0x10001a60  EV_TRIGGER_ENTER trigger 0x40088: rock 0xf0085 wobbles
static void M3_OnTrigger88(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0x88))
        return;
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x85), 6, 0);
    Sound_PlayOnce(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x85), 0x3f6);
    EventType_AddHandler(EV_DESTROYED, NULL, M3_OnDestroyed29);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M3_OnTrigger88);
}

// 0x10001ac0  EV_TRIGGER_ENTER trigger 0x40089: rock 0xf0086 wobbles
static void M3_OnTrigger89(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0x89))
        return;
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x86), 6, 0);
    Sound_PlayOnce(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x86), 0x3f6);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M3_OnTrigger89);
}

// 0x10001b10  EV_TRIGGER_ENTER/LEAVE trigger 0x4001d: proximity door 0x3001c/14
static void M3_OnTriggerDoor1C(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0x1d))
        return;
    int32_t flags = (ev->type == EV_TRIGGER_ENTER) ? 0 : 4;
    AnimPlay(M3_ANIM(0x1c), 5, flags);
    AnimPlay(M3_ANIM(0x14), 4, flags);
    AnimSetUpdateInterval(M3_ANIM(0x1c), 5);
    AnimSetUpdateInterval(M3_ANIM(0x14), 5);
    Sound_PlayOnce(M3_ANIM(0x1c), 0x3f7);
}

// 0x10001b90  EV_TRIGGER_ENTER/LEAVE trigger 0x400b8: the 4-part hatch 0x300b4..b7
static void M3_OnTriggerHatch(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0xb8))
        return;
    Sound_StopObject(M3_ANIM(0xb4));
    if (ev->type == EV_TRIGGER_ENTER) {
        for (int32_t i = 0xb4; i <= 0xb7; i++)
            AnimPlay(M3_ANIM(i), 0xd, 0);
        for (int32_t i = 0xb4; i <= 0xb7; i++)
            AnimSetUpdateInterval(M3_ANIM(i), 2);
        Sound_PlayChain(M3_ANIM(0xb4), 9, 0xb0, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb2);
    } else {
        for (int32_t i = 0xb4; i <= 0xb7; i++)
            AnimPlay(M3_ANIM(i), 0xd, 4);
        for (int32_t i = 0xb4; i <= 0xb7; i++)
            AnimSetUpdateInterval(M3_ANIM(i), 4);
        Sound_PlayChain(M3_ANIM(0xb4), 0x12, 0xb0, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1,
                        0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb1, 0xb2);
    }
}

// 0x10001d60  EV_ANIM_FINISHED with anim 0xb (added by the turret hall trigger): hide the object
static void M3_OnAnimHide(const LanderEvent *ev)
{
    if (ev->arg[1] == 0xb)
        Object_SetDead((ObjId)ev->arg[0]);
}

// 0x10001d80  EV_TRIGGER_ENTER trigger 0x40041 (top hall): lights, covers open, turrets rise
static void M3_OnTriggerHall(const LanderEvent *ev)
{
    static const uint8_t covers[20] = {
        0x6a, 0x79, 0x6d, 0x71, 0x76, 0x75, 0x7a, 0x6c, 0x70, 0x74,
        0x77, 0x7b, 0x6b, 0x6f, 0x73, 0x78, 0x7c, 0x7d, 0x6e, 0x72,
    };
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0x41))
        return;
    for (int32_t i = 0x4c; i <= 0x55; i++)
        Object_SetAlive(M3_LIGHT(i));
    MobObj_Wake(3);
    MobObj_Wake(4);
    MobObj_Wake(5);
    MobObj_Wake(6);
    MobObj_Wake(7);
    MobObj_Wake(0x16);
    MobObj_Wake(0x17);
    MobObj_Wake(0x18);
    MobObj_Wake(0x19);
    MobObj_Wake(0x1a);
    for (int i = 0; i < 20; i++)
        AnimPlay(M3_ANIM(covers[i]), 0xb, 0);
    for (int32_t i = 0x16; i <= 0x1a; i++)
        AnimPlay(M3_MOBANIM(i), 0xc, 0);
    for (int32_t id = (int32_t)M3_MOBANIM(0x16); id <= (int32_t)M3_MOBANIM(0x1a); id++)
        Sound_PlayOnce((ObjId)id, 0xda);
    for (int32_t i = 3; i <= 7; i++)
        AnimPlay(M3_ATURRET(i), 0xc, 0x60);
    for (int32_t i = 3; i <= 7; i++)
        Object_SetProperty(M3_ATURRET(i), PROP_STATE, 1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M3_OnTriggerHall);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M3_OnAnimHide);
}

// 0x10002090  EV_TRIGGER_PLANE 0x1a0023/24 (the two entrances): side 1 = going in
static void M3_OnTriggerPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != OBJ_ID(OT_TRIGGERPLANE, 0x23) && (ObjId)ev->arg[1] != OBJ_ID(OT_TRIGGERPLANE, 0x24))
        return;
    if (ev->arg[2] == 1)
        WindowSetText(st.win, st.text[1]);   // the AI core objective
    else if (st.virusIn == 1)
        WindowSetText(st.win, st.text[2]);   // the evacuation objective
    else
        WindowSetText(st.win, st.text[0]);
}

// 0x10002110  EV_TRIGGER_ENTER triggers 0x4007e..83: the mine (Destructible 0x8008a + k) blows
// a second later and is removed after two
static void M3_OnTriggerMine(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)M3_TRIGGER(0x7e) || trig > (int32_t)M3_TRIGGER(0x83))
        return;
    for (int32_t k = 0; k < 6; k++) {
        ObjId mine = OBJ_ID(OT_DESTRUCTIBLE, 0x8a + k);
        if ((int32_t)ev->arg[1] != (int32_t)M3_TRIGGER(0x7e + k) || (int32_t)Object_GetProperty(mine, PROP_HITPOINTS) <= 0)
            continue;
        InTime3(1000, Object_SetProperty, mine, PROP_HITPOINTS, 0);
        InTime1(2000, Object_SetDead, mine);
        Sound_PlayOnce(mine, 0x102);
    }
}

// 0x10002370  EV_TICK_1S: the fan slows down over five seconds and stops
static void M3_FanSpinDown(const LanderEvent *ev)
{
    (void)ev;
    if (st.fanTicks == 0) {
        AnimPlay(M3_FAN, 0xa, 0x81);
        AnimSetUpdateInterval(M3_FAN, 0x32);
    }
    st.fanTicks++;
    if (st.fanTicks > 0)
        AnimSetUpdateInterval(M3_FAN, (st.fanTicks * 200 + 50) / 10);
    if (st.fanTicks == 5) {
        AnimStop(M3_FAN, 0);
        EventType_RemoveHandler(EV_TICK_1S, M3_FanSpinDown);
    }
}

// 0x10002410  EV_ANIM_FINISHED with anim 0xa (any object): start the spin-down
static void M3_OnFanAnimDone(const LanderEvent *ev)
{
    if (ev->arg[1] != 0xa)
        return;
    AnimStop(M3_FAN, 0);
    st.fanTicks = 0;
    EventType_AddHandler(EV_TICK_1S, NULL, M3_FanSpinDown);
    EventType_RemoveHandler(EV_ANIM_FINISHED, M3_OnFanAnimDone);
}

// 0x10002460  EV_SWITCH 0xa00b3: stop the fan and its wind; the core accepts the virus
static void M3_OnSwitchFan(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != M3_FAN_SWITCH)
        return;
    st.virusLocked = 0;
    AnimStop(M3_FAN, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M3_OnFanAnimDone);
    Sound_StopObject(M3_FAN);
    Sound_PlayOnce(M3_FAN, 0x20b);
    Object_SetDead(M3_FAN_WIND);
    EventType_RemoveHandler(EV_SWITCH, M3_OnSwitchFan);
}

// 0x100024e0  EV_TICK_1S after the virus is in; never removed
static void M3_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(M3_LANDER, PROP_POSITION);
    if (pos[1] > 30000) {
        MissionScore_End(1);                                         // 0x100013a0
        Lander_SetCash(M3_LANDER, Lander_GetCash(M3_LANDER) + 30000);
    }
}

// 0x10002530  EV_TICK_1S: alarm on the lander every 5 s, ten times
static void M3_AlarmTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.alarmTicks != 5)
        return;
    Sound_PlayLoop(M3_LANDER, 0x209, st.alarmTicks);   // loops = 5 (the counter's value)
    st.alarmTicks = 0;
    if (++st.alarmCount == 10)
        EventType_RemoveHandler(EV_TICK_1S, M3_AlarmTick);
}

// 0x10002a90  EV_TRIGGER_ENTER trigger 0x40008 (registered for enter only, so the leave branch
// never runs): the virus objective
static void M3_OnTriggerCoreHint(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0x08))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        WindowSetText(st.win, st.text[3]);
    else
        WindowSetText(st.win, st.text[1]);
}

// 0x10002580  EV_TRIGGER_ENTER trigger 0x40034 (target = the virus pod) once the fan is off
static void M3_OnTriggerCore(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_CORE_TRIGGER || st.virusLocked != 0)
        return;
    Object_SetProperty(M3_POD, PROP_TARGET, -1);
    Object_SetProperty(M3_CORE_TRIGGER, PROP_TARGET, -1);
    Object_SetDead(M3_CORE_TRIGGER);
    MobObj_Sleep(M3_MOB_POD);
    MobObj_Wake(M3_MOB_VIRUS);
    TexAnimPlay(0xbe, 1, 100);
    WindowSetText(st.win, st.text[2]);       // the evacuation objective
    st.virusIn = 1;
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5c), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5d), PROP_STATE, 0);
    Object_SetDead(M3_LIGHT(0xb9));
    Object_SetAlive(M3_LIGHT(0xba));
    for (int32_t i = 0x0e; i <= 0x12; i++)
        AnimPlay(M3_ANIM(i), 7, 1);
    for (int32_t i = 0x09; i <= 0x0d; i++)
        AnimPlay(M3_ANIM(i), 8, 1);
    for (int32_t id = (int32_t)M3_ANIM(0x0a); id <= (int32_t)M3_ANIM(0x12); id++)
        Sound_PlayLoop((ObjId)id, 0xbc, -1);
    for (int i = 0; i < 31; i++)
        MobObj_Wake(m3_alarmMobs[i]);
    for (int i = 0; i < 31; i++)
        Sound_PlayLoop(M3_EMITTER(m3_alarmMobs[i]), 0xf3, -1);
    MobObj_Wake(0x20);
    MobObj_Wake(0x21);
    MobObj_Wake(0x22);
    EventType_AddHandler(EV_TICK_1S, NULL, M3_EvacTick);
    EventType_AddHandler(EV_TICK_1S, NULL, M3_AlarmTick);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M3_OnTriggerCore);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M3_OnTriggerCoreHint);
}

// 0x10002a40  EV_POD_HOOKED (re-hooking the virus): box body and hook point, once
static void M3_OnPodHooked(const LanderEvent *ev)
{
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0xd9, 0x200, 0xd9, 0x3e8);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0x100, 0);
    EventType_RemoveHandler(EV_POD_HOOKED, M3_OnPodHooked);
}

// 0x10002ae0  EV_TRIGGER_ENTER/LEAVE trigger 0x40041: the core turrets 0x5c/5d are on while the
// lander is in the top hall
static void M3_OnTriggerHallTurrets(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M3_TRIGGER(0x41))
        return;
    int32_t on = (ev->type == EV_TRIGGER_ENTER) ? 1 : 0;
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5c), PROP_STATE, on);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5d), PROP_STATE, on);
}

// 0x10002b30  EV_PICKUP: full shield (with its own voice chain), small cash
static void M3_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 0)) {
        Object_SetProperty(M3_LANDER, PROP_HITPOINTS, 0x100);
        Voice_ShieldAcquired(M3_LANDER, 100);
        Sound_PlayChain(OBJ_ID(OT_PICKUP, 0), 3, 0x40b, 0x419, 0x402);
    }
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 1)) {
        Lander_SetCash(M3_LANDER, Lander_GetCash(M3_LANDER) + gSmallCash);
        Voice_CashAcquired(M3_LANDER);
    }
}

// 0x10002bc0 = MissionLib_OnDestroyedSound (same thresholds)

// 0x10002c50  EV_ANIM_TURNED of a tube 0x3005e..69: next animation from the round-robin table
static void M3_OnTubeAnim(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)M3_ANIM(0x5e) || id > (int32_t)M3_ANIM(0x69))
        return;
    AnimStop((ObjId)id, 1);
    AnimPlay((ObjId)ev->arg[0], m3_tubeAnims[st.tubeIndex] + 0xe, 0);
    if (++st.tubeIndex == 10)
        st.tubeIndex = 0;
}

// 0x10002cb0  start the tubes, staggered
static void M3_StartTubes(void)
{
    InTime3(200, AnimPlay, M3_ANIM(0x5e), 0xe, 0);
    InTime3(400, AnimPlay, M3_ANIM(0x5f), 0x10, 0);
    InTime3(500, AnimPlay, M3_ANIM(0x60), 0xe, 0);
    InTime3(800, AnimPlay, M3_ANIM(0x61), 0xf, 0);
    InTime3(0x44c, AnimPlay, M3_ANIM(0x62), 0x10, 0);
    InTime3(0x578, AnimPlay, M3_ANIM(0x63), 0xe, 0);
    InTime3(0x5dc, AnimPlay, M3_ANIM(0x64), 0xf, 0);
    InTime3(0x640, AnimPlay, M3_ANIM(0x65), 0xe, 0);
    InTime3(0x708, AnimPlay, M3_ANIM(0x66), 0x10, 0);
    InTime3(0x7d0, AnimPlay, M3_ANIM(0x67), 0xf, 0);
    InTime3(0x8fc, AnimPlay, M3_ANIM(0x68), 0xe, 0);
    InTime3(0x834, AnimPlay, M3_ANIM(0x69), 0x10, 0);
}

// 0x10002dd0  common set-up
static void M3_Setup(void)
{
    static const uint8_t emitters[16] = {   // particle emitters 0xd00a1..b0, in the DLL's order
        0xa3, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xa2, 0xa1,
    };
    st.text = m3_text[gLanguage];
    for (int32_t id = 0x5c; id <= 0x5d; id++) {                  // turrets 0x5c/5d
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 3; i <= 7; i++)
        Object_SetProperty(M3_ATURRET(i), PROP_ARMOUR, 0x14);
    for (int32_t i = 0x25; i <= 0x28; i++) {
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0xa);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x8a; i <= 0x8f; i++) {                     // mines
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 5);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x64);
    }
    for (int32_t i = 0x37; i <= 0x39; i++) {
        Object_SetProperty(OBJ_ID(OT_FUELDUMP, i), PROP_ARMOUR, 0xa);
        Object_SetProperty(OBJ_ID(OT_FUELDUMP, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x84; i <= 0x86; i++) {                     // rocks
        Object_SetProperty(OBJ_ID(OT_ANIMDESTRUCTIBLE, i), PROP_ARMOUR, 0x14);
        Object_SetProperty(OBJ_ID(OT_ANIMDESTRUCTIBLE, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x1d; i <= 0x1f; i++) {
        Object_SetProperty(M3_ATURRET(i), PROP_ARMOUR, 0x1e);
        Object_SetProperty(M3_ATURRET(i), PROP_EXPLOSION, 0x46);
    }
    for (int32_t i = 0x8a; i <= 0x8f; i++)
        Sound_PlayLoop(OBJ_ID(OT_DESTRUCTIBLE, i), 0x101, -1);
    gTurretTypes[2] = &m3_turretType2;
    gTurretTypes[5] = &m3_turretType5;
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x23;
    gNbWaypoints = m3_nbWaypoints;
    gWaypoints = m3_waypoints;
    MissionScore_Init(M3_LANDER, 200000, 2);                      // 0x10001520, before the
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);       // mission's own handlers
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002bc0
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTriggerHallTurrets);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M3_OnTriggerHallTurrets);
    EventType_AddHandler(EV_PICKUP, NULL, M3_OnPickup);
    EventType_AddHandler(EV_SWITCH, NULL, M3_OnSwitchFan);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTriggerHall);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, M3_OnTriggerPlane);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnLockATrigger);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M3_OnLockATrigger);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M3_OnLockAAnim);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnLockBTrigger);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M3_OnLockBTrigger);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M3_OnLockBAnim);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTriggerDoor1C);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M3_OnTriggerDoor1C);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTrigger87);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTrigger88);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTrigger89);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTriggerHatch);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M3_OnTriggerHatch);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTriggerMine);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTriggerCore);
    EventType_AddHandler(EV_POD_HOOKED, NULL, M3_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M3_OnTriggerCoreHint);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, M3_OnTubeAnim);
    AnimPlay(M3_FAN, 0xa, 0x81);
    Sound_PlayLoop(M3_FAN, 0x20a, -1);
    AnimSetUpdateInterval(M3_FAN, 5);
    AnimPlay(M3_ANIM(0x2d), 1, 0x81);
    Sound_PlayLoop(M3_ANIM(0x2d), 0x20c, -1);
    for (int32_t i = 0x2e; i <= 0x32; i++)
        AnimPlay(M3_ANIM(i), 1, 0x81);
    Sound_PlayLoop(M3_ANIM(0x32), 0x20c, -1);
    AnimPlay(M3_ANIM(0x90), 9, 0x81);
    AnimPlay(M3_ANIM(0x91), 9, 0x81);
    Sound_PlayLoop(M3_ANIM(0x90), 0xd3, -1);
    Sound_PlayLoop(M3_ANIM(0x91), 0xd3, -1);
    for (int32_t i = 0x0e; i <= 0x12; i++)
        AnimSetUpdateInterval(M3_ANIM(i), 0x32);
    for (int32_t i = 0x09; i <= 0x0d; i++)
        AnimSetUpdateInterval(M3_ANIM(i), 0x32);
    MobObj_Wake(0x1d);              // anim turrets 0x1d..1f
    MobObj_Wake(0x1e);
    MobObj_Wake(0x1f);
    Object_SetProperty(M3_CORE_TRIGGER, PROP_TARGET, M3_POD);
    MobObj_Wake(0xc);               // particle emitters 0xc..0xf
    MobObj_Wake(0xd);
    MobObj_Wake(0xe);
    MobObj_Wake(0xf);
    for (int32_t i = 0xc; i <= 0xf; i++)
        Sound_PlayLoop(M3_EMITTER(i), 0xf3, -1);
    MobObj_Wake(9);                 // Explosion 0x01090009 + emitters 0xa, 0xb, 0x10, 0x11
    MobObj_Wake(0xa);
    MobObj_Wake(0xb);
    MobObj_Wake(0x10);
    MobObj_Wake(0x11);
    Sound_PlayLoop(M3_EMITTER(0x10), 0xf3, -1);
    Sound_PlayLoop(M3_EMITTER(0x11), 0xf3, -1);
    for (int i = 0; i < 16; i++)
        Sound_PlayLoop(OBJ_ID(OT_PARTICLEEMITTER, emitters[i]), 0xf3, -1);
    Object_SetDead(M3_LIGHT(0xba));
    for (int32_t i = 0x4c; i <= 0x55; i++)
        Object_SetDead(M3_LIGHT(i));
    for (int32_t i = 3; i <= 7; i++)
        Object_SetProperty(M3_ATURRET(i), PROP_STATE, 0);
    for (int32_t i = 0x1d; i <= 0x1f; i++)
        Object_SetProperty(M3_ATURRET(i), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0x36), PROP_STATE, 0x64);
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0x35), PROP_STATE, 0x64);
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0xb1), PROP_STATE, 0x96);
    M3_StartTubes();
    Media_AudioPlay(0);
}

// 0x10003590  LevelStartLevel_Lander (export 0x10001010)
static void M3_StartLander(void)
{
    MobObj_Wake(M3_MOB_LANDER);
    MobObj_Wake(M3_MOB_CAMERA);
    MobObj_Wake(M3_MOB_POD);
    MobObj_Wake(0x15);              // AmbientFX 0x15, 0x12, 0x13, 0x14
    MobObj_Wake(0x12);
    MobObj_Wake(0x13);
    MobObj_Wake(0x14);
    Lander_SetHeadLight(M3_LANDER, 0x1ff, 0x999);
    Object_SetProperty(M3_POD, PROP_TARGET, M3_LANDER);         // the virus starts hooked
    Pod_Script_SetupMass(M3_POD, 0x5dc);
    Pod_Script_SetupDrag(M3_POD, 0x1333);
    Object_SetProperty(M3_CAMERA, PROP_TARGET, M3_LANDER);
    M3_Setup();
}

// 0x10003640  LevelStartLevel_Flycam (export 0x10001020)
static void M3_StartFlycam(void)
{
    M3_Setup();
    MobObj_Wake(M3_MOB_FLYCAM);
}

// 0x10003660  LevelStopLevel (export 0x10001030): empty
static void M3_Stop(void)
{
}

// 0x10001040
static uint32_t M3_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10003660, return 0. Port: fresh DLL data.
static int32_t M3_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.virusLocked = m3_virusLockedInit;
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m3_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x100081b8, .count = 3 * 5, .out = m3_text },
    { 0 },
};

const mission_script_t mission_mars_3 = {
    .name          = "MARS\\3",
    .get_version   = M3_GetVersion,
    .load          = M3_Load,
    .start_lander  = M3_StartLander,
    .start_flycam  = M3_StartFlycam,
    .start_animcam = NULL,
    .stop          = M3_Stop,
    .text          = m3_gametext,
};
