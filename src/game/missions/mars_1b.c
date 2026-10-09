// MISSIONS\MARS\1B\MISSIONR.DLL - faithful C port.
//
// Mission: the second half of Mars 1. Find the installation at waypoint 1 and destroy the
// generator 0x80025: two pop-up anim-turrets rise, the turret 0x4b blows up, the entrance doors
// open and the underground hatch follows ("Gain entrance..."). Inside: proximity doors, two lifts,
// a cage with security pass 2 (switch 0xa0013 raises it for 30 s), security pass 1 (pickup 0x58)
// which enables the door switches, a three-stop lift (switches 0xa0010..12; the deepest stop needs
// pass 2), rock falls (trigger 0x40032..48: the rock above breaks a second later), mines
// (triggers 0x4001c..24). Both switches 0xa0053/54 open the pod bay; hooking the cargo pod
// (mob 0xc) starts the evac check: above y = 10000 = success 1 and +28000 cash. Par 420 s,
// 3 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040 (no Animcam export). The score library
// (0x10001050-0x100015f4, same code as mars_1a) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define MB_LANDER        MOB_ID(OT_LANDER, 9)            // 0x01010009
#define MB_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define MB_MOB_CAMERA    1
#define MB_MOB_LIFTDOOR  2                               // AnimObject mob 0x01030002
#define MB_MOB_FLYCAM    8
#define MB_MOB_LANDER    9
#define MB_MOB_POD       0xc                             // the cargo, 0x010c000c
#define MB_MOB_PASS2     0xd                             // security pass 2, 0x0113000d
#define MB_LIFTDOOR      MOB_ID(OT_ANIMOBJECT, 2)        // 0x01030002
#define MB_POD           MOB_ID(OT_POD, 0xc)             // 0x010c000c
#define MB_PASS2         MOB_ID(OT_PICKUP, 0xd)          // 0x0113000d
#define MB_PASS1         OBJ_ID(OT_PICKUP, 0x58)         // 0x00130058
#define MB_SHIELD50      OBJ_ID(OT_PICKUP, 0x59)         // 0x00130059
#define MB_CASH_A        OBJ_ID(OT_PICKUP, 0x2b)         // 0x0013002b
#define MB_CASH_B        OBJ_ID(OT_PICKUP, 0x2c)         // 0x0013002c
#define MB_ATURRET(n)    MOB_ID(OT_ANIMTURRET, (n))      // 0x010e00nn
#define MB_TURRET_4B     OBJ_ID(OT_TURRET, 0x4b)         // 0x0000004b
#define MB_GENERATOR     OBJ_ID(OT_DESTRUCTIBLE, 0x25)   // 0x00080025
#define MB_GENERATOR2    OBJ_ID(OT_DESTRUCTIBLE, 0x49)   // 0x00080049 humming machine
#define MB_FUELDUMP      OBJ_ID(OT_FUELDUMP, 0x1a)       // 0x0011001a
#define MB_ROCK_FIRST    OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x31) // 0x000f0031..47 (odd indices)
#define MB_ROCK_LAST     OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x47)
#define MB_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define MB_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define MB_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  mb_nbWaypoints = 1;               // 0x10008160
static const Waypoint mb_waypoints[1] = {               // 0x10008168
    { 19200, -17600 },
};
// 0x10008170: [language][4] string pointers (strings at 0x100081fc-0x100083c2)
static const char *mb_text[3][4];
static const TurretType mb_turretType7 = {              // 0x100081a0 -> gTurretTypes[7]
    { -19, 0, 793, 0 }, 0x00, 0, 2, 7, 80, 0x18, 800, 8192, 0
};
static const TurretType mb_turretType0 = {              // 0x100081c0 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 80, 0x18, 1024, 12000, 0
};
static const TurretType mb_turretType1 = {              // 0x100081e0 -> gTurretTypes[1]
    { 0, 0, 145, 0 }, 0x01, 0, 1, 0, 70, 0x20, 1024, 12000, 0
};

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    uint8_t      hasPass2;      // 0x1000ab0c security pass 2 collected
    Window      *win;           // 0x1000ab10 objective line
    int32_t      bayCount;      // 0x1000ab14 pod-bay switches 0x53/0x54 on
    int32_t      bayOpen;       // 0x1000ab18 (written, never read)
    uint8_t      inLiftZone;    // 0x1000ab1c lander inside trigger 0x40016 (lift 0x30056)
    int32_t      lift2Moving;   // 0x1000ab20 lift 0x30017 on its way out
    int32_t      lift2Stop;     // 0x1000ab24 3 = called from switch 0xa0010, else 0
    uint8_t      inCageZone;    // 0x1000ab28 lander inside trigger 0x40057 (pass-2 cage)
    const char *const *text;    // 0x1000af68 = mb_text[gLanguage]
} st;

// 0x10001600  EV_TICK_1S after the pod is hooked; never removed (LevelEnd stops the scheduler)
static void MB_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(MB_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        MissionScore_End(1);                                         // 0x100013a0
        Lander_SetCash(MB_LANDER, Lander_GetCash(MB_LANDER) + 28000);
    }
}

// 0x10001650  EV_POD_HOOKED {pod, lander}: never removed, so a re-hook repeats all of it
static void MB_OnPodHooked(const LanderEvent *ev)
{
    ObjId pod = (ObjId)ev->arg[0];
    Pod_Script_SetupDynamicsCuboid(pod, 0xe6, 0xe6, 0xe6, 0x578);
    Pod_Script_SetupDrag(pod, 0x1333);
    AnimSetUpdateInterval(pod, 4);
    WindowSetText(st.win, st.text[3]);       // the evacuation objective
    EventType_AddHandler(EV_TICK_1S, NULL, MB_EvacTick);
    AnimPlay(MB_ATURRET(3), 5, 0x60);
    AnimSetUpdateInterval(MB_ATURRET(3), 0x32);
    Sound_PlayOnce(MB_ATURRET(3), 0xbe);
    AnimPlay(MB_ATURRET(4), 5, 0x60);
    AnimSetUpdateInterval(MB_ATURRET(4), 0x32);
    Sound_PlayOnce(MB_ATURRET(4), 0xbe);
    Object_SetProperty(MB_ATURRET(3), PROP_STATE, 1);
    Object_SetProperty(MB_ATURRET(4), PROP_STATE, 1);
    MobObj_Wake(0x11);
    MobObj_Wake(0x12);
    MobObj_Wake(0x13);
    for (int32_t id = (int32_t)MB_ATURRET(0x11); id <= (int32_t)MB_ATURRET(0x13); id++)
        Sound_PlayLoop((ObjId)id, 0x91, -1);
}

// 0x10001790  EV_SWITCH 0x53/0x54: both on -> wake the pod, open the bay (0x3004e..52) and lift it
static void MB_OnSwitchPodBay(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != MB_SWITCH(0x53) && id != MB_SWITCH(0x54))
        return;
    if (ev->arg[1] == 1)
        st.bayCount++;
    if (ev->arg[1] == 0)
        st.bayCount--;
    if (st.bayCount != 2)
        return;
    MobObj_Wake(MB_MOB_POD);
    AnimPlay(MB_ANIM(0x4e), 3, 0x80);
    AnimSetUpdateInterval(MB_ANIM(0x4e), 0x1e);
    Sound_PlayOnce(MB_ANIM(0x4e), 0x1e5);
    AnimPlay(MB_ANIM(0x4f), 3, 0x80);
    AnimSetUpdateInterval(MB_ANIM(0x4f), 0x1e);
    AnimPlay(MB_ANIM(0x50), 3, 0x80);
    AnimSetUpdateInterval(MB_ANIM(0x50), 0x1e);
    AnimPlay(MB_ANIM(0x51), 3, 0x80);
    AnimSetUpdateInterval(MB_ANIM(0x51), 0x1e);
    AnimPlay(MB_ANIM(0x52), 4, 0);
    AnimSetUpdateInterval(MB_ANIM(0x52), 0x1e);
    Sound_PlayOnce(MB_ANIM(0x52), 0xc8);
    InTime3(0x528, Sound_PlayLoop, MB_ANIM(0x52), 0xc9, 5);
    InTime2(0xc94, Sound_PlayOnce, MB_ANIM(0x52), 0xca);
    AnimPlay(MB_POD, 4, 0);
    AnimSetUpdateInterval(MB_POD, 0x1e);
    EventType_AddHandler(EV_POD_HOOKED, NULL, MB_OnPodHooked);
    EventType_RemoveHandler(EV_SWITCH, MB_OnSwitchPodBay);
    st.bayOpen = 1;
}

// 0x10001930  EV_TRIGGER_ENTER/LEAVE trigger 0x40016: lander on the lift 0x30056
static void MB_OnTriggerLiftZone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == MB_TRIGGER(0x16))
        st.inLiftZone = (ev->type == EV_TRIGGER_ENTER);
}

// 0x10001950  EV_ANIM_FINISHED: lift 0x30056 back down -> both call switches off
static void MB_OnLiftDown(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_ANIM(0x56) || ev->arg[1] != 7)
        return;
    Object_SetProperty(MB_SWITCH(0x0e), PROP_STATE, 0);
    Object_SetProperty(MB_SWITCH(0x0f), PROP_STATE, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, MB_OnLiftDown);
}

// 0x100019a0  InTime: send the lift 0x30056 back once the lander is off it (retry every 3 s)
static void MB_LiftReturn(void)
{
    if (st.inLiftZone) {
        InTime0(3000, MB_LiftReturn);
        return;
    }
    AnimPlay(MB_ANIM(0x56), 7, 4);
    AnimSetUpdateInterval(MB_ANIM(0x56), 4);
    Sound_PlayOnce(MB_ANIM(0x56), 0xa8);
    AnimPlay(MB_ANIM(0x55), 6, 4);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, MB_OnLiftDown);
}

// 0x10001a20  EV_ANIM_FINISHED: lift 0x30056 up -> unlock the switches, return in 3 s
static void MB_OnLiftUp(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_ANIM(0x56) || ev->arg[1] != 7)
        return;
    Object_SetProperty(MB_SWITCH(0x0e), PROP_STATE, -2);
    Object_SetProperty(MB_SWITCH(0x0f), PROP_STATE, -2);
    InTime0(3000, MB_LiftReturn);
    EventType_RemoveHandler(EV_ANIM_FINISHED, MB_OnLiftUp);
}

// 0x10001a80  EV_SWITCH 0x0e/0x0f on: lift 0x30056 (+ its shaft door 0x30055) goes up
static void MB_OnSwitchLift(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if ((id != MB_SWITCH(0x0e) && id != MB_SWITCH(0x0f)) || ev->arg[1] != 1)
        return;
    AnimPlay(MB_ANIM(0x56), 7, 0);
    AnimSetUpdateInterval(MB_ANIM(0x56), 4);
    Sound_PlayOnce(MB_ANIM(0x56), 0xae);
    AnimPlay(MB_ANIM(0x55), 6, 0);
    ObjId other = (id == MB_SWITCH(0x0e)) ? MB_SWITCH(0x0f) : MB_SWITCH(0x0e);
    Object_SetProperty(other, PROP_STATE, 1);
    Object_SetProperty(MB_SWITCH(0x0e), PROP_STATE, -1);
    Object_SetProperty(MB_SWITCH(0x0f), PROP_STATE, -1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, MB_OnLiftUp);
}

// 0x10001b40  EV_ANIM_FINISHED of lift 0x30017 (any anim) once it is back: free its switches in 1 s
static void MB_OnLift2Back(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_ANIM(0x17) || st.lift2Moving != 0)
        return;
    InTime3(1000, Object_SetProperty, MB_SWITCH(0x10), PROP_STATE, -2);
    InTime3(1000, Object_SetProperty, MB_SWITCH(0x11), PROP_STATE, -2);
    InTime3(1000, Object_SetProperty, MB_SWITCH(0x12), PROP_STATE, -2);
    InTime3(1000, Object_SetProperty, MB_SWITCH(0x10), PROP_STATE, 0);
    InTime3(1000, Object_SetProperty, MB_SWITCH(0x11), PROP_STATE, 0);
    InTime3(1000, Object_SetProperty, MB_SWITCH(0x12), PROP_STATE, 0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, MB_OnLift2Back);
}

// 0x10001c10  EV_ANIM_FINISHED of lift 0x30017 (any anim) at its stop: travel back after 1 s
static void MB_OnLift2Arrived(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_ANIM(0x17))
        return;
    int32_t loops;
    st.lift2Moving = 0;
    if (st.hasPass2 == 1 && st.lift2Stop != 3) {
        InTime3(1000, AnimPlay, MB_ANIM(0x17), 0xc, 0x84);
        InTime2(0, Sound_PlayOnce, MB_ANIM(0x17), 0xa5);
        loops = 0xc;
    } else {
        InTime3(1000, AnimPlay, MB_ANIM(0x17), 0xb, 0x84);
        InTime2(0, Sound_PlayOnce, MB_ANIM(0x17), 0xa5);
        loops = 8;
    }
    InTime3(0x4d8, Sound_PlayLoop, MB_ANIM(0x17), 0xa6, loops);
    AnimSetUpdateInterval(MB_ANIM(0x17), 0x1e);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, MB_OnLift2Back);
    EventType_RemoveHandler(EV_ANIM_FINISHED, MB_OnLift2Arrived);
}

// 0x10001d20  EV_SWITCH 0x10..0x12 (any state): lift 0x30017 travels (the long run 0xc only with
// pass 2 and not from switch 0x10), the other two switches are shown on and all three locked
static void MB_OnSwitchLift2(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)MB_SWITCH(0x10) || id > (int32_t)MB_SWITCH(0x12))
        return;
    st.lift2Moving = 1;
    st.lift2Stop = (id == (int32_t)MB_SWITCH(0x10)) ? 3 : 0;
    if (st.hasPass2 == 1 && id != (int32_t)MB_SWITCH(0x10)) {
        InTime3(300, AnimPlay, MB_ANIM(0x17), 0xc, 0x80);
        InTime2(0, Sound_PlayOnce, MB_ANIM(0x17), 0xb0);
        InTime3(0x492, Sound_PlayLoop, MB_ANIM(0x17), 0xb1, 5);
        InTime2(0x15c2, Sound_PlayOnce, MB_ANIM(0x17), 0xb2);
    } else {
        InTime3(300, AnimPlay, MB_ANIM(0x17), 0xb, 0x80);
        InTime2(0, Sound_PlayOnce, MB_ANIM(0x17), 0xb0);
        InTime3(0x492, Sound_PlayLoop, MB_ANIM(0x17), 0xb1, 2);
        InTime2(0xb72, Sound_PlayOnce, MB_ANIM(0x17), 0xb2);
    }
    AnimSetUpdateInterval(MB_ANIM(0x17), 0xa);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, MB_OnLift2Arrived);
    if ((ObjId)ev->arg[0] == MB_SWITCH(0x10)) {
        Object_SetProperty(MB_SWITCH(0x11), PROP_STATE, 1);
        Object_SetProperty(MB_SWITCH(0x12), PROP_STATE, 1);
    } else if ((ObjId)ev->arg[0] == MB_SWITCH(0x11)) {
        Object_SetProperty(MB_SWITCH(0x10), PROP_STATE, 1);
        Object_SetProperty(MB_SWITCH(0x12), PROP_STATE, 1);
    } else {
        Object_SetProperty(MB_SWITCH(0x10), PROP_STATE, 1);
        Object_SetProperty(MB_SWITCH(0x11), PROP_STATE, 1);
    }
    Object_SetProperty(MB_SWITCH(0x10), PROP_STATE, -1);
    Object_SetProperty(MB_SWITCH(0x11), PROP_STATE, -1);
    Object_SetProperty(MB_SWITCH(0x12), PROP_STATE, -1);
}

// 0x10001f10  EV_TRIGGER_ENTER/LEAVE trigger 0x4004a: proximity door 0x3004c/4d
static void MB_OnTriggerDoor4C(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MB_TRIGGER(0x4a))
        return;
    int32_t flags = (ev->type == EV_TRIGGER_ENTER) ? 0 : 4;
    AnimPlay(MB_ANIM(0x4c), 0xd, flags);
    Sound_PlayOnce(MB_ANIM(0x4c), 0x1e3);
    AnimPlay(MB_ANIM(0x4d), 0xd, flags);
}

// 0x10001fa0  EV_TRIGGER_ENTER/LEAVE trigger 0x40057: lander inside the pass-2 cage
static void MB_OnTriggerCageZone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == MB_TRIGGER(0x57))
        st.inCageZone = (ev->type == EV_TRIGGER_ENTER);
}

// 0x10001fc0  EV_PICKUP security pass 2: the guard anim-turrets 5..7 go to sleep and fold away
static void MB_OnPickupPass2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_PASS2)
        return;
    st.hasPass2 = 1;
    Voice_SecurityPassAcquired(MB_LANDER, 2);
    MobObj_Sleep(5);
    MobObj_Sleep(6);
    MobObj_Sleep(7);
    AnimPlay(MB_ATURRET(5), 0xa, 0);
    Sound_PlayOnce(MB_ATURRET(5), 0xbf);
    AnimPlay(MB_ATURRET(6), 0xa, 0);
    Sound_PlayOnce(MB_ATURRET(6), 0xbf);
    AnimPlay(MB_ATURRET(7), 0xa, 0);
    Sound_PlayOnce(MB_ATURRET(7), 0xbf);
}

// 0x10002070  InTime / direct: close the cage (0x3002e/2f) unless the lander is inside (retry 2 s)
static void MB_CloseCage(void)
{
    if (st.inCageZone) {
        InTime0(2000, MB_CloseCage);
        return;
    }
    AnimPlay(MB_ANIM(0x2e), 0xf, 4);
    Sound_PlayOnce(MB_ANIM(0x2e), 0xa7);
    AnimPlay(MB_ANIM(0x2f), 0xf, 4);
    Object_SetProperty(MB_SWITCH(0x13), PROP_STATE, -2);
    Object_SetProperty(MB_SWITCH(0x13), PROP_STATE, 0);
}

// 0x100020f0  EV_ANIM_FINISHED: pass platform 0x30030 back down -> close the cage
static void MB_OnPassLowered(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_ANIM(0x30) || ev->arg[1] != 0x10)
        return;
    MB_CloseCage();
    EventType_RemoveHandler(EV_ANIM_FINISHED, MB_OnPassLowered);
}

// 0x10002120  InTime 30 s after the cage switch: lower the pass platform (and the pass) again
static void MB_LowerPass(void)
{
    AnimPlay(MB_ANIM(0x30), 0x10, 4);
    Sound_PlayOnce(MB_ANIM(0x30), 0xc8);
    InTime3(0x528, Sound_PlayLoop, MB_ANIM(0x30), 0xc9, 0x2a);
    InTime2(0x4380, Sound_PlayOnce, MB_ANIM(0x30), 0xca);
    AnimPlay(MB_PASS2, 0x10, 4);
    AnimSetUpdateInterval(MB_ANIM(0x30), 0x64);
    AnimSetUpdateInterval(MB_PASS2, 0x64);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, MB_OnPassLowered);
}

// 0x100021d0  EV_SWITCH 0x13 on (enabled by pass 1): open the cage, raise pass 2 for 30 s
static void MB_OnSwitchCage(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_SWITCH(0x13) || ev->arg[1] != 1)
        return;
    MobObj_Wake(MB_MOB_PASS2);
    AnimPlay(MB_ANIM(0x2e), 0xf, 0);
    AnimPlay(MB_ANIM(0x2f), 0xf, 0);
    AnimPlay(MB_ANIM(0x30), 0x10, 0);
    AnimPlay(MB_PASS2, 0x10, 0);
    InTime0(30000, MB_LowerPass);
    Object_SetProperty(MB_SWITCH(0x13), PROP_STATE, -1);
}

// 0x10002260  EV_SWITCH 0x15 (enabled by pass 1): doors 0x30007/08 open (on) or close
static void MB_OnSwitchDoors07(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_SWITCH(0x15))
        return;
    int32_t flags = (ev->arg[1] == 1) ? 0 : 4;
    AnimPlay(MB_ANIM(0x07), 0x11, flags);
    AnimSetUpdateInterval(MB_ANIM(0x07), 1);
    Sound_PlayOnce(MB_ANIM(0x07), 0x1e1);
    AnimPlay(MB_ANIM(0x08), 0x16, flags);
    AnimSetUpdateInterval(MB_ANIM(0x08), 1);
    Sound_PlayOnce(MB_ANIM(0x08), 0x1e1);
}

// 0x100022f0  EV_SWITCH 0x14 (enabled by pass 1): doors 0x30009/0a
static void MB_OnSwitchDoors09(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_SWITCH(0x14))
        return;
    int32_t flags = (ev->arg[1] == 1) ? 0 : 4;
    AnimPlay(MB_ANIM(0x09), 0x11, flags);
    AnimSetUpdateInterval(MB_ANIM(0x09), 1);
    Sound_PlayOnce(MB_ANIM(0x09), 0x1e1);
    AnimPlay(MB_ANIM(0x0a), 0x16, flags);
    AnimSetUpdateInterval(MB_ANIM(0x0a), 1);
    Sound_PlayOnce(MB_ANIM(0x0a), 0x1e1);
}

// 0x10002380  EV_TRIGGER_ENTER trigger 0x40029 (enabled by pass 1): lift 0x30028 runs once
static void MB_OnTriggerLift28(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MB_TRIGGER(0x29) || ev->type != EV_TRIGGER_ENTER)
        return;
    AnimPlay(MB_ANIM(0x28), 0x13, 0);
    Sound_PlayOnce(MB_ANIM(0x28), 0xce);
    InTime3(0x348, Sound_PlayLoop, MB_ANIM(0x28), 0xcf, 0x31);
    InTime2(0x1c2a, Sound_PlayOnce, MB_ANIM(0x28), 0xd0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, MB_OnTriggerLift28);
}

// 0x10002410  EV_PICKUP security pass 1: enables the lift 0x30028 and the door / cage switches
static void MB_OnPickupPass1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_PASS1)
        return;
    Voice_SecurityPassAcquired(MB_LANDER, 1);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerLift28);
    EventType_AddHandler(EV_SWITCH, NULL, MB_OnSwitchDoors07);
    EventType_AddHandler(EV_SWITCH, NULL, MB_OnSwitchDoors09);
    EventType_AddHandler(EV_SWITCH, NULL, MB_OnSwitchCage);
}

// 0x10002470  EV_TRIGGER_ENTER/LEAVE trigger 0x40062: proximity door 0x30060/61
static void MB_OnTriggerDoor60(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MB_TRIGGER(0x62))
        return;
    int32_t flags = (ev->type == EV_TRIGGER_ENTER) ? 0 : 4;
    AnimPlay(MB_ANIM(0x60), 0x15, flags);
    Sound_PlayOnce(MB_ANIM(0x60), 0xab);
    AnimPlay(MB_ANIM(0x61), 0x15, flags);
}

// 0x10002500  EV_TRIGGER_ENTER trigger 0x40027 (after the hatch opened): lift 0x30026 descends
static void MB_OnTriggerLift26(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MB_TRIGGER(0x27))
        return;
    AnimPlay(MB_ANIM(0x26), 0x12, 0);
    Sound_PlayOnce(MB_ANIM(0x26), 0xce);
    InTime3(0x348, Sound_PlayLoop, MB_ANIM(0x26), 0xcf, 0x23);
    InTime2(0x150e, Sound_PlayOnce, MB_ANIM(0x26), 0xd0);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerDoor60);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, MB_OnTriggerDoor60);
    EventType_AddHandler(EV_PICKUP, NULL, MB_OnPickupPass1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, MB_OnTriggerLift26);
}

// 0x100025c0  EV_ANIM_FINISHED with anim index 2 (the door 0x30019; the object is not checked):
// open the hatch 0x3002a, "Locate cargo."
static void MB_OnEntranceOpen(const LanderEvent *ev)
{
    if (ev->arg[1] != 2)
        return;
    AnimPlay(MB_ANIM(0x2a), 0x14, 0);
    Sound_PlayOnce(MB_ANIM(0x2a), 0x1e0);
    WindowSetText(st.win, st.text[2]);       // "Locate cargo."
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerLift26);
    EventType_RemoveHandler(EV_ANIM_FINISHED, MB_OnEntranceOpen);
}

// 0x10002630  EV_DESTROYED generator 0x80025: pop-up turrets, entrance doors, turret 0x4b blows
static void MB_OnDestroyedGenerator(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MB_GENERATOR)
        return;
    MobObj_Wake(0xf);
    MobObj_Wake(0x10);
    for (int32_t id = (int32_t)MB_ATURRET(0xf); id <= (int32_t)MB_ATURRET(0x10); id++)
        Sound_PlayLoop((ObjId)id, 0x91, -1);
    InTime3(2000, AnimPlay, MB_ATURRET(0xf), 0x17, 0x60);
    InTime3(1000, AnimPlay, MB_ATURRET(0x10), 0x18, 0x60);
    AnimSetUpdateInterval(MB_ATURRET(0xf), 0x1e);
    AnimSetUpdateInterval(MB_ATURRET(0x10), 0x1e);
    InTime3(500, AnimPlay, MB_ANIM(0x18), 1, 0);
    InTime2(500, Sound_PlayOnce, MB_ANIM(0x18), 0x1e4);
    InTime3(500, AnimPlay, MB_ANIM(0x19), 2, 0);
    MobObj_Wake(MB_MOB_LIFTDOOR);
    AnimPlay(MB_LIFTDOOR, 0x19, 0);
    Sound_PlayOnce(MB_LIFTDOOR, 0x1e2);
    InTime3(0x834, Object_SetProperty, MB_TURRET_4B, PROP_HITPOINTS, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, MB_OnEntranceOpen);
    EventType_RemoveHandler(EV_DESTROYED, MB_OnDestroyedGenerator);
}

// 0x10002780  EV_TRIGGER_ENTER trigger 0x40063 (around the generator). The original sets
// text[0] again ("Locate installation..."), which is already shown: kept as is.
static void MB_OnTriggerArrive(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MB_TRIGGER(0x63))
        return;
    WindowSetText(st.win, st.text[0]);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, MB_OnTriggerArrive);
}

// 0x100027c0  EV_DESTROYED rock 0xf0031..47: remove it and its trigger box (index + 1)
static void MB_OnDestroyedRock(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)MB_ROCK_FIRST || id > (int32_t)MB_ROCK_LAST)
        return;
    Object_SetDead((ObjId)id);
    Object_SetDead((ObjId)(id - 0xaffff));               // add 0xfff50001: 0xf00nn -> 0x400nn+1
}

// 0x10002800  EV_TRIGGER_ENTER trigger 0x40032..48: the rock (index - 1) falls a second later
static void MB_OnTriggerRock(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)MB_TRIGGER(0x32) || trig > (int32_t)MB_TRIGGER(0x48))
        return;
    ObjId rock = (ObjId)(trig + 0xaffff);                 // 0x400nn -> 0xf00nn-1
    Sound_PlayOnce(rock, 0x102);
    InTime3(1000, Object_SetProperty, rock, PROP_HITPOINTS, 0);
}

// 0x10002850  EV_TRIGGER_ENTER trigger 0x4001c..24: the mine (Destructible index - 1) explodes
static void MB_OnTriggerMine(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)MB_TRIGGER(0x1c) || trig > (int32_t)MB_TRIGGER(0x24))
        return;
    Object_SetProperty((ObjId)(trig + 0x3ffff), PROP_HITPOINTS, 0);   // 0x400nn -> 0x800nn-1
}

// 0x10002880 = MissionLib_OnDestroyedSound (same thresholds)

// 0x10002910  EV_PICKUP: +50 % shield, small cash x2
static void MB_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == MB_SHIELD50) {
        int32_t shield = (int32_t)Object_GetProperty(MB_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(MB_LANDER, 50);
        Object_SetProperty(MB_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == MB_CASH_A || (ObjId)ev->arg[0] == MB_CASH_B) {
        Lander_SetCash(MB_LANDER, Lander_GetCash(MB_LANDER) + gSmallCash);
        Voice_CashAcquired(MB_LANDER);
    }
}

// 0x100029b0  common set-up
static void MB_Setup(void)
{
    static const struct { uint32_t id; int32_t interval; } rockIntervals[12] = {
        { 0xf0031, 0xa }, { 0xf0033, 0xa }, { 0xf0035, 0xb }, { 0xf0037, 0xb },
        { 0xf0039, 0xc }, { 0xf003b, 0xc }, { 0xf003d, 0xa }, { 0xf003f, 0xa },
        { 0xf0041, 0xb }, { 0xf0043, 0xb }, { 0xf0045, 0xc }, { 0xf0047, 0xc },
    };
    st.text = mb_text[gLanguage];
    for (int32_t id = (int32_t)MB_ATURRET(0xf); id <= (int32_t)MB_ATURRET(0x13); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = (int32_t)MB_ATURRET(3); id <= (int32_t)MB_ATURRET(7); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = 0; id <= 6; id++)                          // turrets 0..6
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    Object_SetProperty(MB_TURRET_4B, PROP_ARMOUR, 0x1e);
    Object_SetProperty(MB_TURRET_4B, PROP_EXPLOSION, 0x46);
    Object_SetProperty(MB_GENERATOR, PROP_ARMOUR, 0x2d);
    Object_SetProperty(MB_GENERATOR, PROP_EXPLOSION, 0x8c);
    Object_SetProperty(MB_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(MB_FUELDUMP, PROP_EXPLOSION, 0x50);
    for (int32_t i = 0x1b; i <= 0x23; i += 2) {                  // mines 0x8001b..23 (odd)
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 10);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x2d), PROP_ARMOUR, 0x19);
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x23;
    gNbWaypoints = mb_nbWaypoints;
    gWaypoints = mb_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 250, 12);
    WindowSetText(st.win, st.text[0]);
    gTurretTypes[0] = &mb_turretType0;
    gTurretTypes[7] = &mb_turretType7;
    gTurretTypes[1] = &mb_turretType1;
    EventType_AddHandler(EV_PICKUP, NULL, MB_OnPickup);
    EventType_AddHandler(EV_DESTROYED, NULL, MB_OnDestroyedGenerator);
    EventType_AddHandler(EV_PICKUP, NULL, MB_OnPickupPass2);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerDoor4C);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, MB_OnTriggerDoor4C);
    EventType_AddHandler(EV_SWITCH, NULL, MB_OnSwitchLift2);
    EventType_AddHandler(EV_SWITCH, NULL, MB_OnSwitchLift);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerLiftZone);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, MB_OnTriggerLiftZone);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerCageZone);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, MB_OnTriggerCageZone);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002880
    EventType_AddHandler(EV_DESTROYED, NULL, MB_OnDestroyedRock);
    EventType_AddHandler(EV_SWITCH, NULL, MB_OnSwitchPodBay);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerArrive);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerRock);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MB_OnTriggerMine);
    AnimPlay(MB_ANIM(0x5a), 8, 1);
    AnimPlay(MB_ANIM(0x5b), 8, 1);
    AnimPlay(MB_ANIM(0x5c), 9, 1);
    AnimPlay(MB_ANIM(0x5d), 9, 1);
    AnimPlay(MB_ANIM(0x5e), 9, 1);
    AnimPlay(MB_ANIM(0x5f), 9, 1);
    for (int32_t i = 0x5a; i <= 0x5f; i++)
        AnimSetUpdateInterval(MB_ANIM(i), 0x14);
    for (int32_t i = 0x5a; i <= 0x5b; i++)
        Sound_PlayLoop(MB_ANIM(i), 0xc1, -1);
    for (int32_t i = 0x5c; i <= 0x5f; i++)
        Sound_PlayLoop(MB_ANIM(i), 0xbc, -1);
    for (int32_t i = 0x31; i <= 0x47; i += 2)                    // rocks wobble
        AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, i), 0xe, 1);
    // every id 0xf0031..0xf0047, including the even ones (their indices are the trigger boxes)
    for (int32_t id = (int32_t)MB_ROCK_FIRST; id <= (int32_t)MB_ROCK_LAST; id++)
        Sound_PlayLoop((ObjId)id, 0x101, -1);
    for (int i = 0; i < 12; i++)
        AnimSetUpdateInterval(rockIntervals[i].id, rockIntervals[i].interval);
    MobObj_Wake(5);
    MobObj_Wake(6);
    MobObj_Wake(7);
    MobObj_Wake(3);
    MobObj_Wake(4);
    Object_SetProperty(MB_ATURRET(3), PROP_STATE, 0);
    Object_SetProperty(MB_ATURRET(4), PROP_STATE, 0);
    Sound_PlayLoop(MB_GENERATOR2, 0xd2, -1);
    MissionScore_Init(MB_LANDER, 420000, 3);                      // 0x10001520
    Media_AudioPlay(0);
}

// 0x10002f60  LevelStartLevel_Lander (export 0x10001010)
static void MB_StartLander(void)
{
    MobObj_Wake(MB_MOB_LANDER);
    MobObj_Wake(MB_MOB_CAMERA);
    MobObj_Wake(0xe);               // AmbientFX
    MobObj_Wake(0xb);               // AmbientFX
    MobObj_Wake(0xa);               // AmbientFX
    MobObj_Wake(0x14);              // AmbientFX
    Object_SetProperty(MB_CAMERA, PROP_TARGET, MB_LANDER);
    MB_Setup();
}

// 0x10002fb0  LevelStartLevel_Flycam (export 0x10001020)
static void MB_StartFlycam(void)
{
    MB_Setup();
    MobObj_Wake(MB_MOB_FLYCAM);
}

// 0x10002fd0  LevelStopLevel (export 0x10001030): empty
static void MB_Stop(void)
{
}

// 0x10001040
static uint32_t MB_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10002fd0, return 0. Port: fresh DLL data.
static int32_t MB_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText mb_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10008170, .count = 3 * 4, .out = mb_text },
    { 0 },
};

const mission_script_t mission_mars_1b = {
    .name          = "MARS\\1B",
    .get_version   = MB_GetVersion,
    .load          = MB_Load,
    .start_lander  = MB_StartLander,
    .start_flycam  = MB_StartFlycam,
    .start_animcam = NULL,
    .stop          = MB_Stop,
    .text          = mb_gametext,
};
