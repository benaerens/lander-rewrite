// MISSIONS\RHEA\1\MISSIONR.DLL - faithful C port.
//
// Mission: descend the main shaft to the garrison (trigger 0x400b0 shows the data core objective),
// find the data core (pod: hooking it, event 0xC, switches to the return objective) and fly back
// to the start area (trigger 0x400a9): success
// 1 and +350000 cash. Par time 300 s, 10 pickups. On the way: switch-operated doors, a two-switch
// gate (0xa000e + 0xa0011) whose opening starts a chain of explosions, three crusher doors that
// close on a lander standing in their trigger whenever machine 0x30037 completes a cycle (switch
// 0xa0009 reopens them), proximity doors, pop-up turrets, animation sequences (AnimSeq) that
// raise anim turrets, mines (anim destructibles 0xf00b2..bd blow up on contact).
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x100011d0,
// LevelStartLevel_Lander 0x100011e0 (-> 0x100035e0), LevelStartLevel_Flycam 0x100011f0
// (-> 0x10003630), LevelStopLevel 0x10001200 (-> empty 0x10003650), LevelGetVersion 0x10001210.
// Library code in mission_lib.c: AnimSeq 0x10001000-0x100011c1 (slots 0x1000b040), the score
// library 0x10001220-0x100017c9 (Init 0x100016f0, End 0x10001570), the destroyed-sound handler
// 0x10002820.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define RH1_LANDER        MOB_ID(OT_LANDER, 0xd)            // 0x0101000d
#define RH1_CAMERA        MOB_ID(OT_CAMERA, 1)              // 0x01060001
#define RH1_MOB_LANDER    0xd
#define RH1_MOB_CAMERA    1
#define RH1_MOB_FLYCAM    0xc
#define RH1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))        // 0x000300nn
#define RH1_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))        // 0x000400nn
#define RH1_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))            // 0x000a00nn
#define RH1_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))      // 0x000800nn
#define RH1_TURRET(n)     OBJ_ID(OT_TURRET, (n))            // 0x000000nn
#define RH1_ANIMTURRET(n) MOB_ID(OT_ANIMTURRET, (n))        // 0x010e00nn (mobs)
#define RH1_FORCEBOX(n)   OBJ_ID(OT_FORCEBOX, (n))          // 0x001000nn
#define RH1_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))            // 0x001300nn
#define RH1_MINE_FIRST    OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xb2) // 0x000f00b2..bd
#define RH1_MINE_LAST     OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xbd)
#define RH1_SWITCH_CRUSHERS RH1_SWITCH(0x09)                // 0xa0009 reopens the crusher doors

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  rh1_nbWaypoints = 2;            // 0x10008160
static const Waypoint rh1_waypoints[2] = {            // 0x10008168
    { -14720, -57600 }, { -18944, -70528 },
};
// 0x10008178: [language][3]
static const char *rh1_text[3][3];
static const TurretType rh1_turretType21 = {          // 0x100081a0 -> gTurretTypes[21]
    { 2, 29, 288, 0 }, 0x08, 1, 6, 8, 80, 0x30, 1024, 10000, 0
};
static const TurretType rh1_turretType0 = {           // 0x100081c0 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 2, 10, 80, 0x18, 1024, 12000, 0
};
static const TurretType rh1_turretType7 = {           // 0x100081e0 -> gTurretTypes[7]
    { -19, 0, 793, 0 }, 0x00, 0, 3, 7, 60, 0x18, 1024, 12000, 0
};
// 0x10008200-0x10008257: AnimSeq sequences {anim, repeat, flags} ... 0xff (writable .data)
static const uint8_t rh1_seqImage[0x58] = {
    0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0,      // 0x10008200 anim turret 0x11 rises
    0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0,      // 0x10008208 anim turret 0x16
    0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0,      // 0x10008210 anim turret 0x15
    0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0,      // 0x10008218 anim turret 0x17
    0x20, 0x01, 0x60, 0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 0x10008220 (unused)
    0x20, 0x01, 0x60, 0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 0x10008230 (unused)
    0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0,      // 0x10008240 (unused)
    0x08, 0x01, 0x60, 0xff, 0, 0, 0, 0,      // 0x10008248 (unused)
    0x08, 0x02, 0x60, 0xff, 0, 0, 0, 0,      // 0x10008250 anim turret 0x1a
};
#define RH1_SEQ(va)       (&st.seq[(va) - 0x10008200u])

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    uint8_t      seq[0x58];     // 0x10008200 the sequences (writable copy)
    Window      *win;           // 0x1000ab5c objective line
    int32_t      gateWakes;     // 0x1000ab60 the gate's guard mobs were woken (once)
    int32_t      crusherShut[3];// 0x1000ab64/68/6c crusher door k closed
    int32_t      inCrusher[3];  // 0x1000ab70/74/78 lander inside trigger 0x4002d + k
    int32_t      crusherBusy;   // 0x1000ab7c (written, never read)
    int32_t      crusherTicks;  // 0x1000ab80
    int32_t      inGarrison;    // 0x1000ab84 inside trigger 0x400b1
    int32_t      gateSwitches;  // 0x1000ab88 switches 0xa000e / 0xa0011 on
    int32_t      gateOpen;      // 0x1000ab8c (written, never read)
    const char *const *text;    // 0x1000afc8 = rh1_text[gLanguage]
} st;

// 0x10001120 called through InTime (3 arguments): AnimSeq_Start(obj, seq, loop)
static void RH1_AnimSeqStartLater(intptr_t obj, intptr_t seq, intptr_t loop)
{
    AnimSeq_Start((ObjId)obj, (uint8_t *)seq, (int32_t)loop);
}

// 0x100017d0  EV_SWITCH 0xa0009: reopen the three crusher doors, lower their turrets
static void RH1_OnSwitchCrushers(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_SWITCH_CRUSHERS)
        return;
    st.crusherShut[0] = 0;
    st.crusherShut[1] = 0;
    st.crusherShut[2] = 0;
    AnimPlay(RH1_ANIM(0x1f), 4, 0);
    Sound_PlayOnce(RH1_ANIM(0x1f), 0x3f4);
    AnimPlay(RH1_ANIM(0x20), 4, 0);
    Sound_PlayOnce(RH1_ANIM(0x20), 0x3f4);
    AnimPlay(RH1_ANIM(0x21), 4, 0);
    Sound_PlayOnce(RH1_ANIM(0x21), 0x3f4);
    AnimPlay(RH1_ANIM(0x74), 6, 4);
    Sound_PlayOnce(RH1_ANIM(0x74), 0xda);
    AnimPlay(RH1_ANIM(0x75), 6, 4);
    Sound_PlayOnce(RH1_ANIM(0x75), 0xda);
    AnimPlay(RH1_ANIM(0x76), 6, 4);
    Sound_PlayOnce(RH1_ANIM(0x76), 0xda);
    Object_SetProperty(RH1_TURRET(0x67), PROP_STATE, 0);
    Object_SetProperty(RH1_TURRET(0x68), PROP_STATE, 0);
    Object_SetProperty(RH1_TURRET(0x71), PROP_STATE, 0);
    Object_SetProperty(RH1_SWITCH_CRUSHERS, PROP_STATE, 1);
}

// 0x10001900  EV_TRIGGER_ENTER 0x4002d..2f
static void RH1_OnCrusherEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2d))
        st.inCrusher[0] = 1;
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2e))
        st.inCrusher[1] = 1;
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2f))
        st.inCrusher[2] = 1;
}

// 0x10001940  EV_TRIGGER_LEAVE 0x4002d..2f
static void RH1_OnCrusherLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2d))
        st.inCrusher[0] = 0;
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2e))
        st.inCrusher[1] = 0;
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2f))
        st.inCrusher[2] = 0;
}

// 0x100019d0  EV_TICK_100MS for 7 ticks after a machine cycle: shut the door of an occupied
// crusher, raise and arm its turret
static void RH1_CrusherTick(const LanderEvent *ev)
{
    (void)ev;
    st.crusherTicks++;
    if (st.inCrusher[0] != 0) {
        Sound_PlayOnce(RH1_ANIM(0x36), 0xfc);
        AnimPlay(RH1_ANIM(0x1f), 4, 4);
        Sound_PlayOnce(RH1_ANIM(0x1f), 0x3f4);
        InTime3(500, AnimPlay, RH1_ANIM(0x74), 6, 0);
        InTime2(500, Sound_PlayOnce, RH1_ANIM(0x74), 0xda);
        InTime3(500, Object_SetProperty, RH1_TURRET(0x67), PROP_STATE, 1);
        Object_SetProperty(RH1_SWITCH_CRUSHERS, PROP_STATE, 0);
        st.crusherShut[0] = 1;
        st.inCrusher[0] = 0;
    }
    if (st.inCrusher[1] != 0) {
        Sound_PlayOnce(RH1_ANIM(0x37), 0xfc);
        AnimPlay(RH1_ANIM(0x20), 4, 4);
        Sound_PlayOnce(RH1_ANIM(0x20), 0x3f4);
        InTime3(500, AnimPlay, RH1_ANIM(0x75), 6, 0);
        InTime2(500, Sound_PlayOnce, RH1_ANIM(0x75), 0xda);
        InTime3(500, Object_SetProperty, RH1_TURRET(0x68), PROP_STATE, 1);
        Object_SetProperty(RH1_SWITCH_CRUSHERS, PROP_STATE, 0);
        st.crusherShut[1] = 1;
        st.inCrusher[1] = 0;
    }
    if (st.inCrusher[2] != 0) {
        Sound_PlayOnce(RH1_ANIM(0x38), 0xfc);
        AnimPlay(RH1_ANIM(0x21), 4, 4);
        Sound_PlayOnce(RH1_ANIM(0x21), 0x3f4);
        InTime3(500, AnimPlay, RH1_ANIM(0x76), 6, 0);
        InTime2(500, Sound_PlayOnce, RH1_ANIM(0x76), 0xda);
        InTime3(500, Object_SetProperty, RH1_TURRET(0x71), PROP_STATE, 1);
        Object_SetProperty(RH1_SWITCH_CRUSHERS, PROP_STATE, 0);
        st.crusherShut[2] = 1;
        st.inCrusher[2] = 0;
    }
    if (st.crusherTicks > 6) {
        st.crusherTicks = 0;
        st.crusherBusy = 0;
        EventType_RemoveHandler(EV_TICK_100MS, RH1_CrusherTick);
    }
}

// 0x10001980  EV_ANIM_WRAPPED: machine 0x30037 completed a cycle while all crusher doors are open:
// watch the triggers for 7 ticks (every cycle adds another tick handler)
static void RH1_OnMachineCycle(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_ANIM(0x37))
        return;
    if (st.crusherShut[0] == 1 || st.crusherShut[1] == 1 || st.crusherShut[2] == 1)
        return;
    st.crusherBusy = 1;
    EventType_AddHandler(EV_TICK_100MS, NULL, RH1_CrusherTick);
}

// 0x10001c00  EV_SWITCH 0xa0000: door 0x3003f follows
static void RH1_OnSwitchDoor3F(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_SWITCH(0x00))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(RH1_ANIM(0x3f), 3, 0);
        Sound_PlayOnce(RH1_ANIM(0x3f), 0x3f7);
    }
    if (ev->arg[1] == 0) {
        AnimPlay(RH1_ANIM(0x3f), 3, 4);
        Sound_PlayOnce(RH1_ANIM(0x3f), 0x3f7);
    }
}

// 0x10001c70  EV_DESTROYED 0x800f2: hatch 0x30040 opens, a chain of explosions 0x800f3..fa
static void RH1_OnDestroyedF2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_DESTR(0xf2))
        return;
    AnimPlay(RH1_ANIM(0x40), 3, 0);
    InTime3(1100, Object_SetProperty, RH1_DESTR(0xf3), PROP_HITPOINTS, 0);
    InTime3(1100, Object_SetProperty, RH1_DESTR(0xf4), PROP_HITPOINTS, 0);
    InTime3(1200, Object_SetProperty, RH1_DESTR(0xf5), PROP_HITPOINTS, 0);
    InTime3(1300, Object_SetProperty, RH1_DESTR(0xf6), PROP_HITPOINTS, 0);
    InTime3(1400, Object_SetProperty, RH1_DESTR(0xf7), PROP_HITPOINTS, 0);
    InTime3(1400, Object_SetProperty, RH1_DESTR(0xf8), PROP_HITPOINTS, 0);
    InTime3(1500, Object_SetProperty, RH1_DESTR(0xf9), PROP_HITPOINTS, 0);
    InTime3(1600, Object_SetProperty, RH1_DESTR(0xfa), PROP_HITPOINTS, 0);
    MobObj_Wake(0x13);
    MobObj_Wake(0x14);
}

// 0x10001d70  EV_ANIMSEQ_DONE: anim turret 0x17 is up: arm it, open 0x300f1, raise 0x15 and 0x16
static void RH1_OnSeq17Done(const LanderEvent *ev)
{
    if ((uint8_t *)ev->arg[1] != RH1_SEQ(0x10008218))
        return;
    Object_SetProperty(RH1_ANIMTURRET(0x17), PROP_STATE, 1);
    InTime3(2000, AnimPlay, RH1_ANIM(0xf1), 0x23, 0);
    InTime2(2000, Sound_PlayOnce, RH1_ANIM(0xf1), 0xae);
    MobObj_Wake(0x15);
    InTime3(3000, RH1_AnimSeqStartLater, RH1_ANIMTURRET(0x15), RH1_SEQ(0x10008210), 0);
    MobObj_Wake(0x16);
    InTime3(3000, RH1_AnimSeqStartLater, RH1_ANIMTURRET(0x16), RH1_SEQ(0x10008208), 0);
    EventType_RemoveHandler(EV_ANIMSEQ_DONE, RH1_OnSeq17Done);
}

// 0x10001e40  EV_TRIGGER_ENTER 0x40029: anim turret 0x17 rises (once)
static void RH1_OnTrigger29(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0x29) || ev->type != EV_TRIGGER_ENTER)
        return;
    AnimSeq_Start(RH1_ANIMTURRET(0x17), RH1_SEQ(0x10008218), 0);
    EventType_AddHandler(EV_ANIMSEQ_DONE, NULL, RH1_OnSeq17Done);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH1_OnTrigger29);
}

// 0x10001e90 / 0x10001ef0 / 0x10001f50 / 0x10001fb0  EV_TRIGGER_ENTER/LEAVE: proximity doors
static void RH1_ProximityDoor(const LanderEvent *ev, ObjId door)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(door, 0x24, 0);
        Sound_PlayOnce(door, 0xae);
        return;
    }
    AnimPlay(door, 0x24, 4);
    Sound_PlayOnce(door, 0xa8);
}

// 0x10001e90  trigger 0x4002a: door 0x30039
static void RH1_OnDoor39(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2a))
        RH1_ProximityDoor(ev, RH1_ANIM(0x39));
}

// 0x10001ef0  trigger 0x4002b: door 0x3003b
static void RH1_OnDoor3B(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x2b))
        RH1_ProximityDoor(ev, RH1_ANIM(0x3b));
}

// 0x10001f50  trigger 0x40024: door 0x3003d
static void RH1_OnDoor3D(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x24))
        RH1_ProximityDoor(ev, RH1_ANIM(0x3d));
}

// 0x10001fb0  trigger 0x40025: door 0x3003c (removed when the gate's chain reaction starts)
static void RH1_OnDoor3C(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0x25))
        RH1_ProximityDoor(ev, RH1_ANIM(0x3c));
}

// 0x10002010  EV_SWITCH 0xa000c: door 0x3003a follows
static void RH1_OnSwitchDoor3A(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_SWITCH(0x0c))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(RH1_ANIM(0x3a), 0x24, 0);
        Sound_PlayOnce(RH1_ANIM(0x3a), 0xae);
    }
    if (ev->arg[1] == 0) {
        AnimPlay(RH1_ANIM(0x3a), 0x24, 4);
        Sound_PlayOnce(RH1_ANIM(0x3a), 0xa8);
    }
}

// 0x10002080  EV_SWITCH 0xa0001: door 0x3003e follows
static void RH1_OnSwitchDoor3E(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_SWITCH(0x01))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(RH1_ANIM(0x3e), 0x24, 0);
        Sound_PlayOnce(RH1_ANIM(0x3e), 0xae);
    }
    if (ev->arg[1] == 0) {
        AnimPlay(RH1_ANIM(0x3e), 0x24, 4);
        Sound_PlayOnce(RH1_ANIM(0x3e), 0xa8);
    }
}

// 0x100020f0  EV_SWITCH 0xa0012: doors 0x30045/46; the first opening wakes mobs 0xe and 0xf
static void RH1_OnSwitchDoors45(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_SWITCH(0x12))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(RH1_ANIM(0x45), 0x27, 0);
        Sound_PlayOnce(RH1_ANIM(0x45), 0x3f7);
        AnimPlay(RH1_ANIM(0x46), 0x27, 0);
        Sound_PlayOnce(RH1_ANIM(0x46), 0x3f7);
        if (st.gateWakes == 0) {
            MobObj_Wake(0xe);
            MobObj_Wake(0xf);
            st.gateWakes++;
        }
    }
    if (ev->arg[1] == 0) {
        AnimPlay(RH1_ANIM(0x45), 0x27, 4);
        Sound_PlayOnce(RH1_ANIM(0x45), 0x3f7);
        AnimPlay(RH1_ANIM(0x46), 0x27, 4);
        Sound_PlayOnce(RH1_ANIM(0x46), 0x3f7);
    }
}

// 0x100021d0  EV_ANIM_TURNED: gate 0x300cf fully open: the chain reaction behind it
static void RH1_OnGateOpened(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_ANIM(0xcf))
        return;
    InTime3(3500, AnimPlay, RH1_ANIM(0x13), 0x29, 0);
    AnimSetUpdateInterval(RH1_ANIM(0x13), 0x1e);
    Sound_PlayOnce(RH1_ANIM(0x13), 0x2e5);
    InTime3(3500, AnimPlay, RH1_ANIM(0x14), 0x29, 0);
    AnimSetUpdateInterval(RH1_ANIM(0x14), 0x1e);
    InTime3(1500, Object_SetProperty, RH1_DESTR(0x1b), PROP_HITPOINTS, 0);
    InTime3(2000, Object_SetProperty, RH1_DESTR(0x1c), PROP_HITPOINTS, 0);
    InTime3(4000, AnimPlay, RH1_ANIM(0x79), 0x2f, 0);
    InTime2(4000, Sound_PlayOnce, RH1_ANIM(0x79), 0x2e6);
    InTime3(3500, AnimPlay, RH1_ANIM(0x15), 0x29, 0);
    AnimSetUpdateInterval(RH1_ANIM(0x15), 0x1e);
    InTime3(3500, AnimPlay, RH1_ANIM(0x16), 0x29, 0);
    AnimSetUpdateInterval(RH1_ANIM(0x16), 0x1e);
    InTime3(500, Object_SetProperty, RH1_DESTR(0x1d), PROP_HITPOINTS, 0);
    InTime3(1000, Object_SetProperty, RH1_DESTR(0x1e), PROP_HITPOINTS, 0);
    if (st.gateWakes == 0) {
        MobObj_Wake(0x18);
        MobObj_Wake(0x19);
        st.gateWakes++;
    }
    AnimPlay(RH1_ANIM(0x47), 0x27, 0);
    MobObj_Wake(6);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH1_OnDoor3C);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, RH1_OnDoor3C);
    InTime3(4000, AnimPlay, RH1_ANIM(0xd1), 0x28, 0);
    InTime2(4000, Sound_PlayOnce, RH1_ANIM(0xd1), 0x3f5);
    InTime3(4000, AnimPlay, RH1_ANIM(0xd2), 0x28, 0);
    InTime2(4000, Sound_PlayOnce, RH1_ANIM(0xd2), 0x3f5);
    EventType_RemoveHandler(EV_ANIM_TURNED, RH1_OnGateOpened);
}

// 0x100023f0  EV_SWITCH 0xa000e / 0xa0011: gate 0x300cf/d0 opens while both are on (each opening
// adds another RH1_OnGateOpened); any other change plays the closing
static void RH1_OnSwitchGate(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_SWITCH(0x0e) && (ObjId)ev->arg[0] != RH1_SWITCH(0x11))
        return;
    if (ev->arg[1] == 1)
        st.gateSwitches++;
    if (ev->arg[1] == 0)
        st.gateSwitches--;
    if (st.gateSwitches == 2) {
        AnimPlay(RH1_ANIM(0xcf), 0x28, 0);
        Sound_PlayOnce(RH1_ANIM(0xcf), 0x3f5);
        AnimPlay(RH1_ANIM(0xd0), 0x28, 0);
        Sound_PlayOnce(RH1_ANIM(0xd0), 0x3f5);
        EventType_AddHandler(EV_ANIM_TURNED, NULL, RH1_OnGateOpened);
    }
    st.gateOpen = 1;
    if (st.gateSwitches != 2) {
        AnimPlay(RH1_ANIM(0xcf), 0x28, 4);
        Sound_PlayOnce(RH1_ANIM(0xcf), 0x3f5);
        AnimPlay(RH1_ANIM(0xd0), 0x28, 4);
        Sound_PlayOnce(RH1_ANIM(0xd0), 0x3f5);
        st.gateOpen = 0;
    }
}

// 0x100024f0  EV_ANIMSEQ_DONE: anim turret 0x11 is up: arm it
static void RH1_OnSeq11Done(const LanderEvent *ev)
{
    if ((uint8_t *)ev->arg[1] != RH1_SEQ(0x10008200))
        return;
    Object_SetProperty(RH1_ANIMTURRET(0x11), PROP_STATE, 1);
    EventType_RemoveHandler(EV_ANIMSEQ_DONE, RH1_OnSeq11Done);
}

// 0x10002520  EV_TRIGGER_ENTER 0x4002c: anim turret 0x11 rises (once)
static void RH1_OnTrigger2C(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0x2c) || ev->type != EV_TRIGGER_ENTER)
        return;
    AnimSeq_Start(RH1_ANIMTURRET(0x11), RH1_SEQ(0x10008200), 0);
    EventType_AddHandler(EV_ANIMSEQ_DONE, NULL, RH1_OnSeq11Done);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH1_OnTrigger2C);
}

// 0x10002570  EV_TRIGGER_ENTER 0x40026: anim turret 0x1a rises (once; never armed by script)
static void RH1_OnTrigger26(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0x26) || ev->type != EV_TRIGGER_ENTER)
        return;
    AnimSeq_Start(RH1_ANIMTURRET(0x1a), RH1_SEQ(0x10008250), 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH1_OnTrigger26);
}

// 0x100025b0  EV_TRIGGER_ENTER/LEAVE 0x40027: pop-up turret 0x72 (anim 0x30077)
static void RH1_OnPopup77(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0x27))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(RH1_ANIM(0x77), 6, 0);
        InTime3(1500, Object_SetProperty, RH1_TURRET(0x72), PROP_STATE, 1);
        return;
    }
    InTime3(1000, AnimPlay, RH1_ANIM(0x77), 6, 4);
    Object_SetProperty(RH1_TURRET(0x72), PROP_STATE, 0);
}

// 0x10002630  EV_TRIGGER_ENTER/LEAVE 0x40028: pop-up turret 0x73 (anim 0x30078)
static void RH1_OnPopup78(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0x28))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(RH1_ANIM(0x78), 6, 0);
        InTime3(1500, Object_SetProperty, RH1_TURRET(0x73), PROP_STATE, 1);
        return;
    }
    InTime3(1000, AnimPlay, RH1_ANIM(0x78), 6, 4);
    Object_SetProperty(RH1_TURRET(0x73), PROP_STATE, 0);
}

// 0x100026b0  EV_TRIGGER_ENTER 0x400d3: 0x300da and 0x3004c move (once)
static void RH1_OnTriggerD3(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0xd3))
        return;
    AnimPlay(RH1_ANIM(0xda), 0x2e, 0);
    AnimSetUpdateInterval(RH1_ANIM(0xda), 0x32);
    Sound_PlayOnce(RH1_ANIM(0xda), 0xe9);
    AnimPlay(RH1_ANIM(0x4c), 0x2e, 0);
    AnimSetUpdateInterval(RH1_ANIM(0x4c), 0x32);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH1_OnTriggerD3);
}

// 0x10002730  EV_TRIGGER_ENTER 0x400d4: 0x300d7 / 0x300d8 move (once)
static void RH1_OnTriggerD4(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0xd4))
        return;
    AnimPlay(RH1_ANIM(0xd7), 0x2b, 0);
    InTime2(200, Sound_PlayOnce, RH1_ANIM(0xd7), 0xf8);
    InTime2(1300, Sound_PlayOnce, RH1_ANIM(0xd7), 0xf8);
    AnimPlay(RH1_ANIM(0xd8), 0x2c, 0);
    InTime2(300, Sound_PlayOnce, RH1_ANIM(0xd8), 0xf8);
    InTime2(1400, Sound_PlayOnce, RH1_ANIM(0xd8), 0xf8);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH1_OnTriggerD4);
}

// 0x100027f0  EV_COLLISION {body, other, impulse}: touching a mine blows it up
static void RH1_OnMineTouched(const LanderEvent *ev)
{
    int32_t other = (int32_t)ev->arg[1];
    if (other >= (int32_t)RH1_MINE_FIRST && other <= (int32_t)RH1_MINE_LAST)
        Object_SetProperty((ObjId)other, PROP_HITPOINTS, 0);
}

// 0x100028b0  EV_TRIGGER_ENTER 0x400a9 (added once the core is hooked): evacuation
static void RH1_OnTriggerEvac(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH1_TRIGGER(0xa9))
        return;
    Lander_SetCash(RH1_LANDER, Lander_GetCash(RH1_LANDER) + 350000);
    MissionScore_End(1);                                      // 0x10001570
}

// 0x100028f0  EV_TRIGGER_ENTER/LEAVE 0x400b0 / 0x400b1: objective text in the garrison
static void RH1_OnTriggerGarrison(const LanderEvent *ev)
{
    if (ev->type == EV_TRIGGER_ENTER) {
        if ((ObjId)ev->arg[1] == RH1_TRIGGER(0xb0))
            WindowSetText(st.win, st.text[1]);   // the data core objective
        if ((ObjId)ev->arg[1] == RH1_TRIGGER(0xb1))
            st.inGarrison = 1;
        return;
    }
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0xb0) && st.inGarrison == 0)
        WindowSetText(st.win, st.text[0]);
    if ((ObjId)ev->arg[1] == RH1_TRIGGER(0xb1))
        st.inGarrison = 0;
}

// 0x10002980  EV_POD_HOOKED (any pod): the core is ours
static void RH1_OnPodHooked(const LanderEvent *ev)
{
    (void)ev;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH1_OnTriggerGarrison);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, RH1_OnTriggerGarrison);
    WindowSetText(st.win, st.text[2]);           // the return objective
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnTriggerEvac);
    EventType_RemoveHandler(EV_POD_HOOKED, RH1_OnPodHooked);
}

// 0x100029e0  EV_PICKUP: shield 0x13000f (+50 %), cash, missiles
static void RH1_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == RH1_PICKUP(0x0f)) {
        int32_t shield = (int32_t)Object_GetProperty(RH1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(RH1_LANDER, 50);
        Object_SetProperty(RH1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == RH1_PICKUP(0x03) || (ObjId)ev->arg[0] == RH1_PICKUP(0x04)) {
        Lander_SetCash(RH1_LANDER, Lander_GetCash(RH1_LANDER) + gMediumCash);
        Voice_CashAcquired(RH1_LANDER);
    }
    if ((ObjId)ev->arg[0] == RH1_PICKUP(0x06)) {
        Lander_SetCash(RH1_LANDER, Lander_GetCash(RH1_LANDER) + 2500);
        Voice_CashAcquired(RH1_LANDER);
    }
    if ((ObjId)ev->arg[0] == RH1_PICKUP(0x0a) || (ObjId)ev->arg[0] == RH1_PICKUP(0x0b)) {
        Lander_SetCash(RH1_LANDER, Lander_GetCash(RH1_LANDER) + gLargeCash);
        Voice_CashAcquired(RH1_LANDER);
    }
    if ((ObjId)ev->arg[0] == RH1_PICKUP(0x07) || (ObjId)ev->arg[0] == RH1_PICKUP(0x08)) {
        if (Lander_AddMissiles(RH1_LANDER, 2) == 0) {
            Voice_MissilesFull(RH1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
        } else {
            Voice_MissilesAcquired(RH1_LANDER);
        }
    }
    if ((ObjId)ev->arg[0] == RH1_PICKUP(0x0d)) {
        if (Lander_AddMissiles(RH1_LANDER, 4) == 0) {
            Voice_MissilesFull(RH1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(RH1_LANDER);
    }
}

// 0x10002b80  EV_PICKUP 0x130010: +50 % shield
static void RH1_OnPickupShield10(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_PICKUP(0x10))
        return;
    int32_t shield = (int32_t)Object_GetProperty(RH1_LANDER, PROP_HITPOINTS) + 0x80;
    Voice_ShieldAcquired(RH1_LANDER, 50);
    Object_SetProperty(RH1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10002bf0  EV_PICKUP 0x130005: +25 % shield, and mob 9 wakes 1 s later
static void RH1_OnPickupShield05(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_PICKUP(0x05))
        return;
    int32_t shield = (int32_t)Object_GetProperty(RH1_LANDER, PROP_HITPOINTS) + 0x40;
    Voice_ShieldAcquired(RH1_LANDER, 25);
    Object_SetProperty(RH1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    InTime1(1000, MobObj_Wake, 9);
}

// 0x10002c60  EV_PICKUP 0x130002: full shield
static void RH1_OnPickupShield02(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH1_PICKUP(0x02))
        return;
    int32_t shield = (int32_t)Object_GetProperty(RH1_LANDER, PROP_HITPOINTS) + 0x100;
    Voice_ShieldAcquired(RH1_LANDER, 100);
    Object_SetProperty(RH1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10002cd0  common set-up
static void RH1_Setup(void)
{
    st.text = rh1_text[gLanguage];
    for (int32_t id = (int32_t)RH1_DESTR(0x1b); id <= (int32_t)RH1_DESTR(0x1e); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xff);
    for (int32_t id = 0x67; id <= 0x68; id++)                 // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = 0x71; id <= 0x73; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    for (int32_t id = 0x69; id <= 0x70; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = (int32_t)RH1_ANIMTURRET(0x0e); id <= (int32_t)RH1_ANIMTURRET(0x1a); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x32);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = (int32_t)RH1_DESTR(0x32); id <= (int32_t)RH1_DESTR(0x35); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = (int32_t)RH1_DESTR(0xf2); id <= (int32_t)RH1_DESTR(0xfa); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
    for (int32_t id = (int32_t)OBJ_ID(OT_FUELDUMP, 0x5b); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0x5d); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = (int32_t)RH1_MINE_FIRST; id <= (int32_t)RH1_MINE_LAST; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 5);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    Object_SetProperty(RH1_DESTR(0xd9), PROP_ARMOUR, 0x19);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    gNbWaypoints = rh1_nbWaypoints;
    gWaypoints = rh1_waypoints;
    gTurretTypes[21] = &rh1_turretType21;
    gTurretTypes[0] = &rh1_turretType0;
    gTurretTypes[7] = &rh1_turretType7;
    AnimSeq_Init();                                            // 0x10001190
    EventType_AddHandler(EV_PICKUP, NULL, RH1_OnPickup);
    EventType_AddHandler(EV_PICKUP, NULL, RH1_OnPickupShield10);
    EventType_AddHandler(EV_PICKUP, NULL, RH1_OnPickupShield05);
    EventType_AddHandler(EV_PICKUP, NULL, RH1_OnPickupShield02);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002820
    EventType_AddHandler(EV_COLLISION, NULL, RH1_OnMineTouched);
    EventType_AddHandler(EV_SWITCH, NULL, RH1_OnSwitchDoor3F);
    EventType_AddHandler(EV_DESTROYED, NULL, RH1_OnDestroyedF2);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnTrigger29);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnDoor39);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnDoor39);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnDoor3B);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnDoor3B);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnDoor3D);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnDoor3D);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnDoor3C);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnDoor3C);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnTrigger2C);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnTrigger26);
    EventType_AddHandler(EV_SWITCH, NULL, RH1_OnSwitchDoor3A);
    EventType_AddHandler(EV_SWITCH, NULL, RH1_OnSwitchDoor3E);
    EventType_AddHandler(EV_SWITCH, NULL, RH1_OnSwitchDoors45);
    EventType_AddHandler(EV_SWITCH, NULL, RH1_OnSwitchGate);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnPopup77);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnPopup77);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnPopup78);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnPopup78);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnTriggerD3);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnTriggerD4);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnTriggerGarrison);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnTriggerGarrison);
    EventType_AddHandler(EV_POD_HOOKED, NULL, RH1_OnPodHooked);
    SetGravity(10);
    gAirResistance = 10;
    gMainThrottlePower = 0x17;
    gInOutOverride = 1;
    Environment_OverrideInteriorFog(0, 0, 0);
    static const uint8_t machines[15] = {
        0xdc, 0xdd, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xde, 0xdf, 0xe0, 0xe1, 0xe2
    };
    for (int i = 0; i < 15; i++) {
        AnimPlay(RH1_ANIM(machines[i]), 0x25, 0x81);
        AnimSetUpdateInterval(RH1_ANIM(machines[i]), 0x32);
    }
    Sound_PlayLoop(RH1_ANIM(0xdc), 0xb3, -1);
    Sound_PlayLoop(RH1_ANIM(0xe4), 0xb3, -1);
    Sound_PlayLoop(RH1_ANIM(0xe7), 0xb3, -1);
    Sound_PlayLoop(RH1_ANIM(0xea), 0xb3, -1);
    Sound_PlayLoop(RH1_ANIM(0xe0), 0xb3, -1);
    AnimPlay(RH1_ANIM(0xfb), 0x25, 0x81);
    AnimPlay(RH1_ANIM(0xfc), 0x25, 0x81);
    for (int32_t id = (int32_t)RH1_ANIM(0xfb); id <= (int32_t)RH1_ANIM(0xfc); id++)
        Sound_PlayLoop((ObjId)id, 0xb4, -1);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xb2), 0x26, 0x81);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xb3), 0x26, 0x81);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xb7), 0x26, 0x81);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xb8), 0x26, 0x81);
    for (int32_t id = (int32_t)RH1_MINE_FIRST; id <= (int32_t)OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xb8); id++)
        Sound_PlayLoop((ObjId)id, 0x101, -1);
    AnimPlay(RH1_ANIM(0x36), 5, 0x81);
    AnimSetUpdateInterval(RH1_ANIM(0x36), 0x28);
    AnimPlay(RH1_ANIM(0x37), 5, 0x81);
    AnimSetUpdateInterval(RH1_ANIM(0x37), 0x28);
    AnimPlay(RH1_ANIM(0x38), 5, 0x81);
    AnimSetUpdateInterval(RH1_ANIM(0x38), 0x28);
    for (int32_t id = (int32_t)RH1_ANIM(0x36); id <= (int32_t)RH1_ANIM(0x38); id++)
        Sound_PlayLoop((ObjId)id, 0xd4, -1);
    AnimPlay(RH1_ANIM(0x1f), 4, 0);
    AnimPlay(RH1_ANIM(0x20), 4, 0);
    AnimPlay(RH1_ANIM(0x21), 4, 0);
    MobObj_Wake(0x10);
    MobObj_Wake(0x11);
    for (int32_t id = (int32_t)RH1_MINE_FIRST; id <= (int32_t)RH1_MINE_LAST; id++)
        Sound_PlayLoop((ObjId)id, 0x101, -1);      // the first seven get a second loop (sic)
    Object_SetProperty(RH1_ANIMTURRET(0x11), PROP_STATE, 0);
    MobObj_Wake(0x17);
    Object_SetProperty(RH1_ANIMTURRET(0x17), PROP_STATE, 0);
    Object_SetProperty(RH1_TURRET(0x67), PROP_STATE, 0);
    Object_SetProperty(RH1_TURRET(0x68), PROP_STATE, 0);
    Object_SetProperty(RH1_TURRET(0x71), PROP_STATE, 0);
    Object_SetProperty(RH1_TURRET(0x72), PROP_STATE, 0);
    Object_SetProperty(RH1_TURRET(0x73), PROP_STATE, 0);
    Object_SetDead(RH1_FORCEBOX(0x4d));
    Object_SetDead(RH1_FORCEBOX(0x52));
    Object_SetDead(RH1_FORCEBOX(0x54));
    Object_SetDead(RH1_FORCEBOX(0x55));
    Object_SetDead(RH1_FORCEBOX(0x56));
    Object_SetDead(RH1_FORCEBOX(0x57));
    Object_SetDead(RH1_FORCEBOX(0x58));
    Object_SetDead(RH1_FORCEBOX(0x59));
    Object_SetDead(RH1_FORCEBOX(0x4e));
    Object_SetDead(RH1_FORCEBOX(0x4f));
    Object_SetDead(RH1_FORCEBOX(0x50));
    Object_SetDead(RH1_FORCEBOX(0x51));
    Object_SetProperty(RH1_FORCEBOX(0x53), PROP_STATE, 0x3c);
    Object_SetProperty(RH1_FORCEBOX(0x53), PROP_FLAGS, 2);
    MobObj_Wake(0x12);
    MobObj_Wake(0x1a);
    EventType_AddHandler(EV_SWITCH, NULL, RH1_OnSwitchCrushers);
    EventType_AddHandler(EV_ANIM_WRAPPED, NULL, RH1_OnMachineCycle);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH1_OnCrusherEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH1_OnCrusherLeave);
    Object_SetProperty(RH1_SWITCH_CRUSHERS, PROP_STATE, 1);
    MissionScore_Init(RH1_LANDER, 300000, 10);                 // 0x100016f0
    Media_AudioPlay(0);
    TexAnimPlay(0x55, 1, 100);
    TexAnimPlay(0x5a, 1, 100);
    TexAnimPlay(0x54, 1, 100);
    TexAnimPlay(0x4b, 1, 100);
    TexAnimPlay(0x49, 1, 100);
}

// 0x100035e0  LevelStartLevel_Lander (export 0x100011e0)
static void RH1_StartLander(void)
{
    MobObj_Wake(RH1_MOB_LANDER);
    MobObj_Wake(RH1_MOB_CAMERA);
    RH1_Setup();
    Lander_SetHeadLight(RH1_LANDER, 0x271, 0x1000);
    Object_SetProperty(RH1_CAMERA, PROP_TARGET, RH1_LANDER);
}

// 0x10003630  LevelStartLevel_Flycam (export 0x100011f0)
static void RH1_StartFlycam(void)
{
    RH1_Setup();
    MobObj_Wake(RH1_MOB_FLYCAM);
}

// 0x10003650  LevelStopLevel (export 0x10001200): empty
static void RH1_Stop(void)
{
}

// 0x10001210
static uint32_t RH1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x100011d0  two calls of the empty 0x10003650, return 0. Port: fresh DLL data.
static int32_t RH1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    memcpy(st.seq, rh1_seqImage, sizeof st.seq);
    MissionScore_Reset();
    AnimSeq_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText rh1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10008178, .count = 3 * 3, .out = rh1_text },
    { 0 },
};

const mission_script_t mission_rhea_1 = {
    .name          = "RHEA\\1",
    .get_version   = RH1_GetVersion,
    .load          = RH1_Load,
    .start_lander  = RH1_StartLander,
    .start_flycam  = RH1_StartFlycam,
    .start_animcam = NULL,
    .stop          = RH1_Stop,
    .text          = rh1_gametext,
};
