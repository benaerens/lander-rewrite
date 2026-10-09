// MISSIONS\MARS\1A\MISSIONR.DLL - faithful C port.
//
// Mission: find the installation at waypoint 1, get inside (switch-operated doors, lifts, a
// bridge, a 4-switch puzzle, pop-up turrets), hook the cargo pod (mob 7) and climb above
// y = 10000 with it: success 1 and +25000 cash. Par time 600 s, 3 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040 (no Animcam export). The score library
// (0x10001050-0x100015f4) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define MA_LANDER       MOB_ID(OT_LANDER, 3)          // 0x01010003
#define MA_CAMERA       MOB_ID(OT_CAMERA, 1)          // 0x01060001
#define MA_POD          MOB_ID(OT_POD, 7)             // 0x010c0007 the cargo
#define MA_MOB_CAMERA   1
#define MA_MOB_FLYCAM   2
#define MA_MOB_LANDER   3
#define MA_MOB_POD      7
#define MA_ANIMTURRET_A MOB_ID(OT_ANIMTURRET, 8)      // 0x010e0008
#define MA_ANIMTURRET_B MOB_ID(OT_ANIMTURRET, 9)      // 0x010e0009
#define MA_POPUP_FIRST  OBJ_ID(OT_ANIMTURRET, 0x3a)   // 0x000e003a..3c pop-up turrets
#define MA_POPUP_LAST   OBJ_ID(OT_ANIMTURRET, 0x3c)
#define MA_FUELDUMP     OBJ_ID(OT_FUELDUMP, 0x34)     // 0x00110034
#define MA_SWITCH(n)    OBJ_ID(OT_SWITCH, (n))        // 0x000a00nn
#define MA_ANIM(n)      OBJ_ID(OT_ANIMOBJECT, (n))    // 0x000300nn
#define MA_TRIGGER(n)   OBJ_ID(OT_TRIGGERBOX, (n))    // 0x000400nn
#define MA_PICKUP_SHIELD100 OBJ_ID(OT_PICKUP, 0x40)   // 0x00130040
#define MA_PICKUP_SHIELD25  OBJ_ID(OT_PICKUP, 0x41)   // 0x00130041
#define MA_PICKUP_CASH      OBJ_ID(OT_PICKUP, 0x52)   // 0x00130052 large cash, appears with the bridge

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  ma_nbWaypoints = 1;             // 0x10007160
static const Waypoint ma_waypoints[1] = {             // 0x10007168
    { -19200, 32000 },
};
// 0x10007170: [language][4] string pointers (strings at 0x100071f0-0x100073b3)
static const char *ma_text[3][4];
static const TurretType ma_turretType0 = {            // 0x100071a0 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 80, 0x18, 1024, 12000, 0
};
static const TurretType ma_turretType8 = {            // 0x100071c0 -> gTurretTypes[8]
    { 0, 0, 544, 0 }, 0x00, 0, 1, 0, 85, 0x18, 800, 12000, 0
};
static const int32_t ma_unused_100071e0[4] = {        // 0x100071e0: never referenced
    0x3000f, 3, 0x3000e, 3
};

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    uint8_t      podHooked;     // 0x10009afc
    Window      *win;           // 0x10009b00 objective line
    int32_t      gateCount;     // 0x10009b08 switches 0x4c/0x4d on
    int32_t      gateOpen;      // 0x10009b0c (written, never read)
    int32_t      fourCount;     // 0x10009b10 switches 0x4e..0x51 on
    int32_t      fourDone;      // 0x10009b14
    const char *const *text;    // 0x10009f50 = ma_text[gLanguage]
} st;

// 0x10001600  doors A/B (0x3001f/0x30020) open (flags 0) or close (flags 4); turret 0 on in 1.25 s
static void MA_DoorsAB(int32_t flags)
{
    InTime3(1250, Object_SetProperty, OBJ_ID(OT_TURRET, 0), PROP_STATE, 1);
    AnimPlay(MA_ANIM(0x1f), 9, flags);
    Sound_PlayOnce(MA_ANIM(0x1f), 0xab);
    AnimPlay(MA_ANIM(0x20), 9, flags);
}

// 0x10001660  doors C/D (0x30021/0x30022); turret 0 on in 1 s
static void MA_DoorsCD(int32_t flags)
{
    InTime3(1000, Object_SetProperty, OBJ_ID(OT_TURRET, 0), PROP_STATE, 1);
    AnimPlay(MA_ANIM(0x21), 9, flags);
    Sound_PlayOnce(MA_ANIM(0x21), 0xab);
    AnimPlay(MA_ANIM(0x22), 9, flags);
}

// 0x100016c0  EV_SWITCH {switch, state, shooter}: switches 0x30/0x31 work as a pair
static void MA_OnSwitchAB(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != MA_SWITCH(0x30) && id != MA_SWITCH(0x31))
        return;
    ObjId other = (id == MA_SWITCH(0x30)) ? MA_SWITCH(0x31) : MA_SWITCH(0x30);
    if (ev->arg[1] == 0) {
        MA_DoorsAB(4);
        Object_SetProperty(other, PROP_STATE, 0);
    } else {
        MA_DoorsAB(0);
        Object_SetProperty(other, PROP_STATE, 1);
    }
}

// 0x10001740  EV_SWITCH: switches 0x32/0x33
static void MA_OnSwitchCD(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != MA_SWITCH(0x32) && id != MA_SWITCH(0x33))
        return;
    ObjId other = (id == MA_SWITCH(0x32)) ? MA_SWITCH(0x33) : MA_SWITCH(0x32);
    if (ev->arg[1] == 0) {
        MA_DoorsCD(4);
        Object_SetProperty(other, PROP_STATE, 0);
    } else {
        MA_DoorsCD(0);
        Object_SetProperty(other, PROP_STATE, 1);
    }
}

// 0x100017c0  EV_POD_HOOKED: close any open door pair and reset its switches
static void MA_OnPodHookedCloseDoors(const LanderEvent *ev)
{
    (void)ev;
    if (Object_GetProperty(MA_SWITCH(0x32), PROP_STATE) == 1 || Object_GetProperty(MA_SWITCH(0x33), PROP_STATE) == 1) {
        MA_DoorsCD(4);
        Object_SetProperty(MA_SWITCH(0x33), PROP_STATE, 0);
        Object_SetProperty(MA_SWITCH(0x32), PROP_STATE, 0);
    }
    if (Object_GetProperty(MA_SWITCH(0x30), PROP_STATE) == 1 || Object_GetProperty(MA_SWITCH(0x31), PROP_STATE) == 1) {
        MA_DoorsAB(4);
        Object_SetProperty(MA_SWITCH(0x30), PROP_STATE, 0);
        Object_SetProperty(MA_SWITCH(0x31), PROP_STATE, 0);
    }
}

// 0x10001870  EV_TRIGGER_ENTER trigger 0x40010: three lift platforms, staggered by 0.5 s
static void MA_OnTriggerLifts(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MA_TRIGGER(0x10))
        return;
    AnimPlay(MA_ANIM(0x0f), 3, 0);
    Sound_PlayOnce(MA_ANIM(0x0f), 0xbc);
    InTime2(2000, Sound_PlayOnce, MA_ANIM(0x0f), 0xd8);
    AnimSetUpdateInterval(MA_ANIM(0x0f), 8);
    InTime3(500, AnimPlay, MA_ANIM(0x0e), 3, 0);
    InTime2(500, Sound_PlayOnce, MA_ANIM(0x0e), 0xbc);
    InTime2(2500, Sound_PlayOnce, MA_ANIM(0x0e), 0xd8);
    InTime3(1000, AnimPlay, MA_ANIM(0x0d), 3, 0);
    InTime2(1000, Sound_PlayOnce, MA_ANIM(0x0d), 0xbc);
    InTime2(3000, Sound_PlayOnce, MA_ANIM(0x0d), 0xd8);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, MA_OnTriggerLifts);
}

// 0x10001980  EV_TRIGGER_ENTER trigger 0x4003d: the three pop-up turrets rise
static void MA_OnTriggerPopups(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MA_TRIGGER(0x3d))
        return;
    InTime3(0, AnimPlay, OBJ_ID(OT_ANIMTURRET, 0x3a), 6, 0x60);
    AnimSetUpdateInterval(OBJ_ID(OT_ANIMTURRET, 0x3a), 8);
    Sound_PlayOnce(OBJ_ID(OT_ANIMTURRET, 0x3a), 0xbf);
    InTime3(192, AnimPlay, OBJ_ID(OT_ANIMTURRET, 0x3b), 6, 0x60);
    AnimSetUpdateInterval(OBJ_ID(OT_ANIMTURRET, 0x3b), 8);
    InTime2(192, Sound_PlayOnce, OBJ_ID(OT_ANIMTURRET, 0x3b), 0xbf);
    InTime3(384, AnimPlay, OBJ_ID(OT_ANIMTURRET, 0x3c), 6, 0x60);
    AnimSetUpdateInterval(OBJ_ID(OT_ANIMTURRET, 0x3c), 8);
    InTime2(384, Sound_PlayOnce, OBJ_ID(OT_ANIMTURRET, 0x3c), 0xbf);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, MA_OnTriggerPopups);
}

// 0x10001a70  EV_ANIM_FINISHED: a pop-up turret is up -> enable it
static void MA_OnPopupUp(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)MA_POPUP_FIRST && id <= (int32_t)MA_POPUP_LAST)
        Object_SetProperty((ObjId)id, PROP_STATE, 1);
}

// 0x10001aa0  EV_TRIGGER_ENTER and EV_TRIGGER_LEAVE, trigger 0x40025: proximity door 0x30011/12
static void MA_OnTriggerDoor11(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MA_TRIGGER(0x25))
        return;
    int32_t flags = (ev->type == EV_TRIGGER_ENTER) ? 0 : 4;
    AnimPlay(MA_ANIM(0x11), 7, flags);
    Sound_PlayOnce(MA_ANIM(0x11), 0x3f7);
    AnimPlay(MA_ANIM(0x12), 7, flags);
}

// 0x10001b30  EV_TRIGGER_ENTER/LEAVE, trigger 0x40026: 8-part door 0x30013..0x3001a
static void MA_OnTriggerDoor13(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MA_TRIGGER(0x26))
        return;
    int32_t flags = (ev->type == EV_TRIGGER_ENTER) ? 0 : 4;
    AnimPlay(MA_ANIM(0x13), 8, flags);
    AnimPlay(MA_ANIM(0x14), 8, flags);
    AnimPlay(MA_ANIM(0x15), 8, flags);
    AnimPlay(MA_ANIM(0x16), 8, flags);
    Sound_PlayOnce(MA_ANIM(0x16), 0x3f6);
    AnimPlay(MA_ANIM(0x17), 8, flags);
    AnimPlay(MA_ANIM(0x18), 8, flags);
    AnimPlay(MA_ANIM(0x19), 8, flags);
    AnimPlay(MA_ANIM(0x1a), 8, flags);
}

// 0x10001c70  EV_SWITCH 0x4a: door 0x3001d/1e follows the switch
static void MA_OnSwitchDoor1D(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MA_SWITCH(0x4a))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(MA_ANIM(0x1d), 9, 0);
        Sound_PlayOnce(MA_ANIM(0x1d), 0xab);
        AnimPlay(MA_ANIM(0x1e), 9, 0);
    }
    if (ev->arg[1] == 0) {
        AnimPlay(MA_ANIM(0x1d), 9, 4);
        Sound_PlayOnce(MA_ANIM(0x1d), 0xab);
        AnimPlay(MA_ANIM(0x1e), 9, 4);
    }
}

// 0x10001d00  EV_SWITCH 0x4c/0x4d: gate 0x30023/24 opens while both are on; every other change
// of either switch plays the closing animation (silently)
static void MA_OnSwitchGate(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != MA_SWITCH(0x4c) && id != MA_SWITCH(0x4d))
        return;
    if (ev->arg[1] == 1)
        st.gateCount++;
    if (ev->arg[1] == 0)
        st.gateCount--;
    if (st.gateCount == 2) {
        AnimPlay(MA_ANIM(0x23), 7, 0);
        Sound_PlayOnce(MA_ANIM(0x23), 0x3f7);
        AnimPlay(MA_ANIM(0x24), 7, 0);
    }
    st.gateOpen = 1;
    if (st.gateCount != 2) {
        AnimPlay(MA_ANIM(0x23), 7, 4);
        AnimPlay(MA_ANIM(0x24), 7, 4);
        st.gateOpen = 0;
    }
}

// 0x10001db0  EV_SWITCH 0x4b on: extend the bridge (0x30042..49), reveal the cash pickup
static void MA_OnSwitchBridge(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != MA_SWITCH(0x4b) || ev->arg[1] != 1)
        return;
    InTime3(128, AnimPlay, MA_ANIM(0x42), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x43), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x44), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x45), 0xa, 0);
    InTime2(128, Sound_PlayOnce, MA_ANIM(0x45), 0xc0);
    InTime3(128, AnimPlay, MA_ANIM(0x46), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x47), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x48), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x49), 0xa, 0);
    Object_SetAlive(MA_PICKUP_CASH);
    EventType_RemoveHandler(EV_SWITCH, MA_OnSwitchBridge);
}

// 0x10001ed0  EV_SWITCH 0x4e..0x51: all four on -> second bridge (0x30028..2f); afterwards the
// switches are forced back on whenever they are shot
static void MA_OnSwitchFour(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)MA_SWITCH(0x4e) || id > (int32_t)MA_SWITCH(0x51))
        return;
    if (st.fourDone == 1)
        Object_SetProperty((ObjId)id, PROP_STATE, 1);
    if (ev->arg[1] == 1)
        st.fourCount++;
    if (ev->arg[1] == 0)
        st.fourCount--;
    if (st.fourCount != 4)
        return;
    InTime3(128, AnimPlay, MA_ANIM(0x28), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x29), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x2a), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x2b), 0xa, 0);
    InTime2(128, Sound_PlayOnce, MA_ANIM(0x2b), 0xc0);
    InTime3(128, AnimPlay, MA_ANIM(0x2c), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x2d), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x2e), 0xa, 0);
    InTime3(128, AnimPlay, MA_ANIM(0x2f), 0xa, 0);
    st.fourDone = 1;
}

// 0x10002020  EV_SWITCH 0x36/0x37/0x38: stop (on) or restart (off) a moving machine
static void MA_OnSwitchMachines(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    ObjId anim;
    int32_t index;
    if (id == MA_SWITCH(0x36)) {
        anim = MA_ANIM(0x35); index = 2;
    } else if (id == MA_SWITCH(0x37)) {
        anim = MA_ANIM(0x3e); index = 5;
    } else if (id == MA_SWITCH(0x38)) {
        anim = MA_ANIM(0x3f); index = 5;
    } else {
        return;
    }
    if (ev->arg[1] == 1) {
        AnimPlay(anim, index, 4);
        Sound_StopObject(anim);
    } else if (ev->arg[1] == 0) {
        AnimPlay(anim, index, 2);
        Sound_PlayLoop(anim, 0xbc, -1);
    }
}

// 0x10002150  EV_TRIGGER_ENTER trigger 0x40053 (installation exit)
static void MA_OnTriggerExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MA_TRIGGER(0x53))
        return;
    if (st.podHooked == 1)
        WindowSetText(st.win, st.text[3]);   // the evacuation objective
    else
        WindowSetText(st.win, st.text[1]);   // the cargo objective
}

// 0x100021a0  EV_TICK_1S after the pod is hooked; never removed (LevelEnd stops the scheduler)
static void MA_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(MA_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        MissionScore_End(1);                                          // 0x100013a0
        Lander_SetCash(MA_LANDER, Lander_GetCash(MA_LANDER) + 25000);
    }
}

// 0x100021f0  EV_POD_HOOKED (hard-wired to the cargo pod)
static void MA_OnPodHooked(const LanderEvent *ev)
{
    (void)ev;
    st.podHooked = 1;
    Pod_Script_SetupDrag(MA_POD, 0x1333);
    Pod_Script_SetupMass(MA_POD, 0x4b0);
    WindowSetText(st.win, st.text[2]);       // the exit objective
    EventType_AddHandler(EV_TICK_1S, NULL, MA_EvacTick);
}

// 0x10002250  EV_TRIGGER_ENTER trigger 0x40039: wake the cargo pod
static void MA_OnTriggerWakePod(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != MA_TRIGGER(0x39))
        return;
    MobObj_Wake(MA_MOB_POD);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, MA_OnTriggerWakePod);
}

// 0x10002310  EV_PICKUP: full shield
static void MA_OnPickupShield100(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == MA_PICKUP_SHIELD100) {
        Object_SetProperty(MA_LANDER, PROP_HITPOINTS, 0x100);
        Voice_ShieldAcquired(MA_LANDER, 100);
    }
}

// 0x10002350  EV_PICKUP: +25 % shield, large cash
static void MA_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == MA_PICKUP_SHIELD25) {
        int32_t shield = (int32_t)Object_GetProperty(MA_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(MA_LANDER, 25);
        Object_SetProperty(MA_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == MA_PICKUP_CASH) {
        Lander_SetCash(MA_LANDER, Lander_GetCash(MA_LANDER) + gLargeCash);
        Voice_CashAcquired(MA_LANDER);
    }
}

// 0x100023f0  common set-up
static void MA_Setup(void)
{
    st.text = ma_text[gLanguage];
    for (int32_t id = (int32_t)MA_POPUP_FIRST; id <= (int32_t)MA_POPUP_LAST; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = 0; id <= 6; id++)                        // turrets 0..6
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)MA_ANIMTURRET_A; id <= (int32_t)MA_ANIMTURRET_B; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    Object_SetProperty(MA_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(MA_FUELDUMP, PROP_EXPLOSION, 0x50);
    gTurretTypes[0] = &ma_turretType0;
    gTurretTypes[8] = &ma_turretType8;
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x23;
    gNbWaypoints = ma_nbWaypoints;
    gWaypoints = ma_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_SWITCH, NULL, MA_OnSwitchAB);
    EventType_AddHandler(EV_SWITCH, NULL, MA_OnSwitchCD);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, MA_OnPopupUp);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MA_OnTriggerLifts);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MA_OnTriggerPopups);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MA_OnTriggerDoor11);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, MA_OnTriggerDoor11);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MA_OnTriggerDoor13);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, MA_OnTriggerDoor13);
    EventType_AddHandler(EV_POD_HOOKED, NULL, MA_OnPodHookedCloseDoors);
    EventType_AddHandler(EV_POD_HOOKED, NULL, MA_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MA_OnTriggerWakePod);
    EventType_AddHandler(EV_SWITCH, NULL, MA_OnSwitchDoor1D);
    EventType_AddHandler(EV_SWITCH, NULL, MA_OnSwitchBridge);
    EventType_AddHandler(EV_SWITCH, NULL, MA_OnSwitchGate);
    EventType_AddHandler(EV_SWITCH, NULL, MA_OnSwitchFour);
    EventType_AddHandler(EV_SWITCH, NULL, MA_OnSwitchMachines);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002280
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, MA_OnTriggerExit);
    EventType_AddHandler(EV_PICKUP, NULL, MA_OnPickupShield100);
    EventType_AddHandler(EV_PICKUP, NULL, MA_OnPickup);
    AnimPlay(MA_ANIM(0x27), 1, 0x81);
    AnimSetUpdateInterval(MA_ANIM(0x27), 0x14);
    Sound_PlayLoop(MA_ANIM(0x27), 0xb1, -1);
    AnimPlay(MA_ANIM(0x35), 2, 2);
    Sound_PlayLoop(MA_ANIM(0x35), 0xbc, -1);
    AnimPlay(MA_ANIM(0x07), 4, 1);
    Sound_PlayLoop(MA_ANIM(0x07), 0xbc, -1);
    AnimPlay(MA_ANIM(0x08), 4, 1);
    Sound_PlayLoop(MA_ANIM(0x08), 0xbc, -1);
    AnimPlay(MA_ANIM(0x3e), 5, 2);
    Sound_PlayLoop(MA_ANIM(0x3e), 0xbc, -1);
    AnimPlay(MA_ANIM(0x3f), 5, 2);
    Sound_PlayLoop(MA_ANIM(0x3f), 0xbc, -1);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x0c), 0xb, 1);
    Sound_PlayLoop(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x0c), 0xd3, -1);
    AnimPlay(MA_ANIMTURRET_A, 0xc, 0xe1);
    AnimPlay(MA_ANIMTURRET_B, 0xd, 0xe1);
    AnimSetUpdateInterval(MA_ANIMTURRET_A, 0x28);
    AnimSetUpdateInterval(MA_ANIMTURRET_B, 0x28);
    Sound_PlayLoop(MA_ANIMTURRET_A, 0x8d, -1);
    Sound_PlayLoop(MA_ANIMTURRET_B, 0x8d, -1);
    AnimSetUpdateInterval(MA_ANIM(0x07), 0x14);
    AnimSetUpdateInterval(MA_ANIM(0x08), 0x14);
    Media_AudioPlay(0);
    Object_SetProperty(OBJ_ID(OT_ANIMTURRET, 0x3a), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_ANIMTURRET, 0x3b), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_ANIMTURRET, 0x3c), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0), PROP_STATE, 0);
    TexAnimPlay(0x38, 1, 100);
    Object_SetDead(MA_PICKUP_CASH);
    MissionScore_Init(MA_LANDER, 600000, 3);                      // 0x10001520
}

// 0x10002820  LevelStartLevel_Lander (export 0x10001010)
static void MA_StartLander(void)
{
    MobObj_Wake(MA_MOB_LANDER);
    MobObj_Wake(MA_MOB_CAMERA);
    MobObj_Wake(4);                 // AmbientFX
    MobObj_Wake(10);                // AmbientFX
    MobObj_Wake(5);                 // AmbientFX
    MobObj_Wake(6);                 // AmbientFX
    MobObj_Wake(8);                 // AnimTurret 0x010e0008
    MobObj_Wake(9);                 // AnimTurret 0x010e0009
    Object_SetProperty(MA_CAMERA, PROP_TARGET, MA_LANDER);
    MA_Setup();
}

// 0x10002880  LevelStartLevel_Flycam (export 0x10001020)
static void MA_StartFlycam(void)
{
    MA_Setup();
    MobObj_Wake(MA_MOB_FLYCAM);
}

// 0x100028a0  LevelStopLevel (export 0x10001030): empty
static void MA_Stop(void)
{
}

// 0x10001040
static uint32_t MA_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x100028a0, return 0. Port: fresh DLL data.
static int32_t MA_Load(const char *dllPath)
{
    (void)dllPath;
    (void)ma_unused_100071e0;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText ma_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10007170, .count = 3 * 4, .out = ma_text },
    { 0 },
};

const mission_script_t mission_mars_1a = {
    .name          = "MARS\\1A",
    .get_version   = MA_GetVersion,
    .load          = MA_Load,
    .start_lander  = MA_StartLander,
    .start_flycam  = MA_StartFlycam,
    .start_animcam = NULL,
    .stop          = MA_Stop,
    .text          = ma_gametext,
};
