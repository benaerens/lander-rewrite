// MISSIONS\MARS\4\MISSIONR.DLL - faithful C port.
//
// Mission: find the subterranean entrance (waypoint 1) and the repair depot underground. The fuel
// lines (Destructibles 0x80079..86) are armoured: after ten hits the objective line says more
// firepower is needed. The micro-nuke (pod mob 0x13) waits in the depot; hooking it makes the
// trigger 0x4000a (the base centre) watch the pod. Dropping it there starts a 15 s countdown
// (a countdown format); at zero the fuel lines and the base blow up (explosion mobs 7..17)
// and a lander still inside the blast zone (trigger 0x4001b) is destroyed (white flash);
// otherwise the mission is accomplished: climb above y = 15000 for success 1 and +30000 cash.
// Par 240 s, 6 pickups. Also: proximity doors, a headlight zone (triggers 0x4006e/6d), mines
// that blow when bumped, pop-up turret pairs, missile pickups that respawn when the rack is full.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040. The score library (0x10001060-0x10001604, mercury_1's
// code) is in mission_lib.c; the DLL's sprintf (0x10002f10) is the C library's.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define M4_LANDER        MOB_ID(OT_LANDER, 5)            // 0x01010005
#define M4_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define M4_MOB_CAMERA    1
#define M4_MOB_FLYCAM    4
#define M4_MOB_LANDER    5
#define M4_NUKE          MOB_ID(OT_POD, 0x13)            // 0x010c0013 the micro-nuke
#define M4_LIFT          MOB_ID(OT_ANIMOBJECT, 6)        // 0x01030006
#define M4_ATURRET(n)    MOB_ID(OT_ANIMTURRET, (n))      // 0x010e00nn
#define M4_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define M4_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define M4_FUELLINE_FIRST OBJ_ID(OT_DESTRUCTIBLE, 0x79)  // 0x00080079..86
#define M4_FUELLINE_LAST  OBJ_ID(OT_DESTRUCTIBLE, 0x86)
#define M4_MINE_FIRST    OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x89) // 0x000f0089..90
#define M4_MINE_LAST     OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x90)

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  m4_countdownInit = 15;            // 0x1000b160 seconds to detonation
static const int32_t  m4_nbWaypoints = 1;               // 0x1000b164
static const Waypoint m4_waypoints[1] = {               // 0x1000b168
    { -10112, 512 },
};
static const TurretType m4_turretType2 = {              // 0x1000b170 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 2, 5, 85, 0x18, 1024, 12000, 0
};
static const TurretType m4_turretType10 = {             // 0x1000b190 -> gTurretTypes[10]
    { 112, 0, 601, 0 }, 0x00, 1, 1, 0, 100, 0x10, 1024, 8192, 0
};
static const TurretType m4_turretType5 = {              // 0x1000b1b0 -> gTurretTypes[5]
    { 85, 434, 461, 0 }, 0x02, 2, 1, 0, 60, 0x20, 800, 12000, 0
};
// 0x1000b1d0: [language][11] string pointers (strings at 0x1000b254-0x1000b6d3); [4..7] unused.
// French [4] points at the English "Missiles".
static const char *m4_text[3][11];

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      countdown;     // 0x1000b160 (.data, 15)
    int32_t      door1COpen;    // 0x1000eb54 door 0x3001c/1d
    int32_t      doorA0Open;    // 0x1000eb58 door 0x300a0
    int32_t      fuelHits;      // 0x1000eb5c hits on the fuel lines
    int32_t      depotSeen;     // 0x1000eb60 trigger 0x40000 entered once
    int32_t      lineIndex;     // 0x1000eb64 objective to show again (0..2)
    int32_t      hintShown;     // 0x1000eb68 (only ever cleared)
    int32_t      hintTicks;     // 0x1000eb6c never reset: the hint is taken down only once
    int32_t      inBlastZone;   // 0x1000eb70 lander inside trigger 0x4001b
    int32_t      headlightOn;   // 0x1000eb74
    int32_t      turretPairHit; // 0x1000eb78 a pop-up turret of a pair was destroyed
    Window      *win;           // 0x1000eb7c objective line
    char         buf[256];      // 0x1000f120 sprintf buffer of the countdown
    const char *const *text;    // 0x1000f220 = m4_text[gLanguage]
} st;

// 0x10001610  EV_TRIGGER_ENTER trigger 0x400c0 (after the bomb is placed): doors 0x300c1/c2 open
static void M4_OnTriggerTopEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0xc0))
        return;
    AnimPlay(M4_ANIM(0xc1), 2, 0);
    AnimSetUpdateInterval(M4_ANIM(0xc1), 0xf);
    Sound_PlayChain(M4_ANIM(0xc1), 5, 0xb0, 0xb1, 0xb1, 0xb1, 0xb2);
    AnimPlay(M4_ANIM(0xc2), 3, 0);
    AnimSetUpdateInterval(M4_ANIM(0xc2), 7);
}

// 0x10001690  EV_TRIGGER_LEAVE trigger 0x400c0: close them
static void M4_OnTriggerTopLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0xc0))
        return;
    AnimPlay(M4_ANIM(0xc1), 2, 4);
    AnimSetUpdateInterval(M4_ANIM(0xc1), 0xf);
    Sound_PlayChain(M4_ANIM(0xc1), 5, 0xb0, 0xb1, 0xb1, 0xb1, 0xb2);
    AnimPlay(M4_ANIM(0xc2), 3, 4);
    AnimSetUpdateInterval(M4_ANIM(0xc2), 0xf);
}

// 0x10001710  EV_TRIGGER_ENTER/LEAVE triggers 0x40000 (door 0x300a0) and 0x40001 (door 0x3001c/1d)
static void M4_OnTriggerDepotDoors(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x00)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(M4_ANIM(0xa0), 0x15, 0);
            AnimSetUpdateInterval(M4_ANIM(0xa0), 7);
            Sound_PlayOnce(M4_ANIM(0xa0), 0xae);
            st.doorA0Open = 1;
            if (st.depotSeen == 0)
                st.depotSeen = 1;
        } else {
            AnimPlay(M4_ANIM(0xa0), 0x15, 4);
            AnimSetUpdateInterval(M4_ANIM(0xa0), 7);
            Sound_PlayOnce(M4_ANIM(0xa0), 0xa8);
            st.doorA0Open = 0;
        }
    } else if ((ObjId)ev->arg[1] == M4_TRIGGER(0x01)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(M4_ANIM(0x1d), 0xb, 0);
            Sound_PlayOnce(M4_ANIM(0x1d), 0xa9);
            AnimPlay(M4_ANIM(0x1c), 7, 0);
            st.door1COpen = 1;
        } else {
            AnimPlay(M4_ANIM(0x1d), 0xb, 4);
            Sound_PlayOnce(M4_ANIM(0x1d), 0xa9);
            AnimPlay(M4_ANIM(0x1c), 7, 4);
            st.door1COpen = 0;
        }
    }
}

// 0x10001860  EV_COLLISION {body owner, other, impulse}: bumping a mine 0xf0089..90 sets it off
static void M4_OnCollisionMine(const LanderEvent *ev)
{
    int32_t other = (int32_t)ev->arg[1];
    if (other < (int32_t)M4_MINE_FIRST || other > (int32_t)M4_MINE_LAST)
        return;
    Sound_PlayOnce((ObjId)ev->arg[0], 0x102);
    Object_SetProperty((ObjId)ev->arg[1], PROP_HITPOINTS, 0);
}

// 0x100018a0  EV_TRIGGER_ENTER triggers 0x400c8/c9: hatch 0x300a7 + 0x300af..b5 open, turret 0x5b on
static void M4_OnTriggerHatchA7(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0xc8) && (ObjId)ev->arg[1] != M4_TRIGGER(0xc9))
        return;
    Sound_PlayOnce(M4_ANIM(0xa7), 0xc0);
    AnimPlay(M4_ANIM(0xa7), 0xd, 0);
    for (int32_t i = 0xaf; i <= 0xb5; i++)
        AnimPlay(M4_ANIM(i), 0xd, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5b), PROP_STATE, 1);
}

// 0x10001960  EV_DESTROYED one of 0x8006f..71: all three go, the depot wall opens (0x3009c..9f,
// 0x300b7..b9 appear and animate)
static void M4_OnDestroyedWall(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)OBJ_ID(OT_DESTRUCTIBLE, 0x6f) || id > (int32_t)OBJ_ID(OT_DESTRUCTIBLE, 0x71))
        return;
    EventType_RemoveHandler(EV_DESTROYED, M4_OnDestroyedWall);
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0xa6));
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x6f), PROP_HITPOINTS, 0);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x70), PROP_HITPOINTS, 0);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x71), PROP_HITPOINTS, 0);
    Object_SetAlive(M4_ANIM(0x9c));
    Object_SetAlive(M4_ANIM(0x9d));
    Object_SetAlive(M4_ANIM(0x9e));
    Object_SetAlive(M4_ANIM(0x9f));
    Object_SetAlive(M4_ANIM(0xb7));
    Object_SetAlive(M4_ANIM(0xb8));
    Object_SetAlive(M4_ANIM(0xb9));
    InTime3(200, AnimPlay, M4_ANIM(0x9c), 0xe, 0);
    InTime3(0x4c, AnimPlay, M4_ANIM(0x9d), 0x10, 0);
    AnimPlay(M4_ANIM(0x9e), 0x11, 0);
    InTime3(0x8e, AnimPlay, M4_ANIM(0x9f), 0x12, 0);
    InTime3(0x11f, AnimPlay, M4_ANIM(0xb7), 0xf, 0);
    InTime3(0xb1, AnimPlay, M4_ANIM(0xb8), 0x13, 0);
    InTime3(0x146, AnimPlay, M4_ANIM(0xb9), 0x14, 0);
    InTime2(0x146, Sound_PlayOnce, M4_ANIM(0xb9), 0xf8);
    Object_SetAlive(OBJ_ID(OT_PARTICLEEMITTER, 0x0d));
    Object_SetAlive(OBJ_ID(OT_PARTICLEEMITTER, 0x0e));
    Object_SetAlive(OBJ_ID(OT_PARTICLEEMITTER, 0x0f));
}

// 0x10001b00  EV_TRIGGER_ENTER triggers 0x400c3..c6 (the depot floor): force door 0x3001c open and
// door 0x300a0 shut, open the hatch 0x300b6 + 0x300a8..ae, turret 0x58 on
static void M4_OnTriggerDepotFloor(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)M4_TRIGGER(0xc3) || trig > (int32_t)M4_TRIGGER(0xc6))
        return;
    if (st.door1COpen == 0) {
        AnimPlay(M4_ANIM(0x1d), 0xb, 0);
        Sound_PlayOnce(M4_ANIM(0x1d), 0xa9);
        AnimPlay(M4_ANIM(0x1c), 7, 0);
        st.door1COpen = 1;
    }
    if (st.doorA0Open == 1) {
        AnimPlay(M4_ANIM(0xa0), 0x15, 4);
        AnimSetUpdateInterval(M4_ANIM(0xa0), 7);
        Sound_PlayOnce(M4_ANIM(0xa0), 0xa8);
        st.doorA0Open = 0;
    }
    Sound_PlayOnce(M4_ANIM(0xb6), 0xc0);
    AnimPlay(M4_ANIM(0xb6), 0xd, 0);
    for (int32_t i = 0xa8; i <= 0xae; i++)
        AnimPlay(M4_ANIM(i), 0xd, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x58), PROP_STATE, 1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M4_OnTriggerDepotFloor);
}

// 0x10001c50  EV_TICK_1S after the bomb is placed; never removed
static void M4_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(M4_LANDER, PROP_POSITION);
    if (pos[1] > 15000) {
        MissionScore_End(1);                                         // 0x100013b0
        Lander_SetCash(M4_LANDER, Lander_GetCash(M4_LANDER) + 30000);
    }
}

// 0x10001ca0  InTime: interior colours back to normal after the flash
static void M4_FlashEnd(void)
{
    Environment_SetInteriorAmbient(0x11, 0xa, 0x18);
    Environment_OverrideInteriorFog(0xa, 0xa, 0x1b);
    Environment_RestoreViewDistance();
}

// 0x10001cd0  InTime: white-out
static void M4_FlashStart(void)
{
    Environment_SetInteriorAmbient(0xff, 0xff, 0xff);
    Environment_OverrideInteriorFog(0xff, 0xff, 0xff);
    Environment_OverrideViewDistance(0, 1000);
}

static void M4_OnProjectileHitFuel(const LanderEvent *ev);
static void M4_OnTriggerObjective(const LanderEvent *ev);

// 0x10001d20  EV_TICK_1S after the bomb is placed: countdown, then the base blows
static void M4_CountdownTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.countdown == 0) {
        EventType_RemoveHandler(EV_PROJECTILE_HIT, M4_OnProjectileHitFuel);
        for (int32_t id = (int32_t)M4_FUELLINE_FIRST; id <= (int32_t)M4_FUELLINE_LAST; id++)
            Object_SetDead((ObjId)id);
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x2b));
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x9b));
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x55));
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x56));
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0xd1), PROP_HITPOINTS, 0);
        for (int32_t i = 0x16; i <= 0x1a; i++)
            Object_SetAlive(OBJ_ID(OT_PARTICLEEMITTER, i));
        MobObj_Sleep(0x13);
        InTime1(0xfa, MobObj_Wake, 7);       // explosion mobs 0x01090007..11
        InTime1(0x1c2, MobObj_Wake, 0xa);
        InTime1(0xc8, MobObj_Wake, 0xb);
        InTime1(0x33e, MobObj_Wake, 0xc);
        InTime1(0x20d, MobObj_Wake, 0xd);
        InTime1(0x2ee, MobObj_Wake, 0xe);
        InTime1(0x2bc, MobObj_Wake, 0xf);
        InTime1(0x96, MobObj_Wake, 0x10);
        InTime1(0x12c, MobObj_Wake, 0x11);
        InTime1(0x226, MobObj_Wake, 8);
        InTime1(0x316, MobObj_Wake, 9);
        if (st.inBlastZone == 1) {
            Object_SetProperty(M4_LANDER, PROP_HITPOINTS, 0);
            InTime0(0x12c, M4_FlashStart);
            InTime0(0x258, M4_FlashEnd);
        } else {
            WindowSetText(st.win, st.text[10]);   // the accomplished line
            EventType_RemoveHandler(EV_TICK_1S, M4_CountdownTick);
        }
    }
    if (st.countdown == 15)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, M4_OnTriggerObjective);
    if (st.countdown <= 15 && st.countdown > 0) {
        snprintf(st.buf, sizeof st.buf, st.text[9], st.countdown);   // sprintf 0x10002f10
        WindowSetText(st.win, st.buf);
    }
    st.countdown--;
}

// 0x10001fd0  EV_TRIGGER_ENTER trigger 0x4000a (target = the nuke): release it, start the clock.
// Never removed.
static void M4_OnTriggerBombSite(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0x0a))
        return;
    Object_SetProperty(M4_NUKE, PROP_TARGET, -1);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerTopEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M4_OnTriggerTopLeave);
    EventType_AddHandler(EV_TICK_1S, NULL, M4_CountdownTick);
    EventType_AddHandler(EV_TICK_1S, NULL, M4_EvacTick);
}

// 0x10002030  EV_POD_HOOKED of the nuke: the bomb site trigger now watches it
static void M4_OnPodHooked(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != M4_NUKE)
        return;
    Object_SetProperty(M4_TRIGGER(0x0a), PROP_TARGET, M4_NUKE);
    WindowSetText(st.win, st.text[8]);       // the nuke collected line
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerBombSite);
    EventType_RemoveHandler(EV_POD_HOOKED, M4_OnPodHooked);
    Pod_Script_SetupMass(M4_NUKE, 0x3e8);
    Pod_Script_SetupDrag(M4_NUKE, 0x1333);
}

// 0x100020c0  EV_TICK_1S: put the objective back 5 s after the armour hint
static void M4_HintTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.hintTicks != 5)
        return;
    WindowSetText(st.win, st.text[st.lineIndex]);
    st.hintShown = 0;
    EventType_RemoveHandler(EV_TICK_1S, M4_HintTick);
}

// 0x10002110  EV_PROJECTILE_HIT {target, ...}: the tenth hit on a fuel line shows the armour hint
static void M4_OnProjectileHitFuel(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)M4_FUELLINE_FIRST || id > (int32_t)M4_FUELLINE_LAST)
        return;
    if (++st.fuelHits != 10)
        return;
    if (st.hintShown == 1)
        EventType_RemoveHandler(EV_TICK_1S, M4_HintTick);
    WindowSetText(st.win, st.text[3]);       // the armoured fuel lines hint
    EventType_AddHandler(EV_TICK_1S, NULL, M4_HintTick);
}

// 0x10002180  EV_TRIGGER_ENTER/LEAVE trigger 0x400bf: door 0x300a1
static void M4_OnTriggerDoorA1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0xbf))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(M4_ANIM(0xa1), 0x15, 0);
        AnimSetUpdateInterval(M4_ANIM(0xa1), 7);
        Sound_PlayOnce(M4_ANIM(0xa1), 0xae);
    } else if (ev->type == EV_TRIGGER_LEAVE) {
        AnimPlay(M4_ANIM(0xa1), 0x15, 4);
        AnimSetUpdateInterval(M4_ANIM(0xa1), 7);
        Sound_PlayOnce(M4_ANIM(0xa1), 0xa8);
    }
}

// 0x10002210  EV_TRIGGER_ENTER: objectives and the weapon-shop signs (until the countdown runs)
static void M4_OnTriggerObjective(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x5a)) {
        WindowSetText(st.win, st.text[1]);   // the depot objective
        st.lineIndex = 1;
    }
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x0b)) {
        WindowSetText(st.win, st.text[2]);   // the fuel lines objective
        st.lineIndex = 2;
    }
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x88))
        WindowSetText(st.win, st.text[5]);   // the mines warning
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x0c))
        WindowSetText(st.win, st.text[6]);   // "Micro nukes"
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x77))
        WindowSetText(st.win, st.text[4]);   // "Missiles"
}

// 0x100022d0  EV_TRIGGER_LEAVE of a sign trigger: back to the current objective
static void M4_OnTriggerSignLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x88))
        WindowSetText(st.win, st.text[st.lineIndex]);
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x0c))
        WindowSetText(st.win, st.text[st.lineIndex]);
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x77))
        WindowSetText(st.win, st.text[st.lineIndex]);
}

// 0x10002350  EV_ANIM_FINISHED with anim 0xa: the lift 0x01030006 has arrived
static void M4_OnLiftArrived(const LanderEvent *ev)
{
    if (ev->arg[1] != 0xa)
        return;
    Sound_StopObject(M4_LIFT);
    Sound_PlayOnce(M4_ANIM(0x5f), 0xd0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, M4_OnLiftArrived);
}

// 0x10002390  EV_TRIGGER_ENTER trigger 0x40002 once the depot was entered: the lift and the depot
// turrets
static void M4_OnTriggerDepotLift(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0x02) || st.depotSeen != 1)
        return;
    AnimPlay(M4_ANIM(0x5f), 9, 0);
    AnimPlay(M4_ATURRET(0x1d), 9, 0x60);
    AnimPlay(M4_ATURRET(0x1e), 9, 0x60);
    AnimPlay(M4_LIFT, 0xa, 0);
    Sound_PlayOnce(M4_LIFT, 0xce);
    InTime3(0x348, Sound_PlayLoop, M4_LIFT, 0xcf, -1);
    AnimPlay(M4_ATURRET(0x1f), 0xa, 0x60);
    AnimPlay(M4_ATURRET(0x20), 0xa, 0x60);
    if (st.turretPairHit == 0) {
        AnimPlay(M4_ATURRET(0x19), 0x17, 0x60);
        InTime3(1000, AnimPlay, M4_ATURRET(0x17), 0x16, 0x60);
    }
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M4_OnLiftArrived);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M4_OnTriggerDepotLift);
}

// 0x100024a0  EV_TRIGGER_ENTER/LEAVE trigger 0x4001b: lander inside the blast zone
static void M4_OnTriggerBlastZone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0x1b))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        st.inBlastZone = 1;
    else if (ev->type == EV_TRIGGER_LEAVE)
        st.inBlastZone = 0;
}

// 0x100024d0 = MissionLib_OnDestroyedSound (same thresholds)

// 0x10002560  EV_TRIGGER_ENTER trigger 0x400be: the turret pair 0x18/0x1a rises
static void M4_OnTriggerTurretPair(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M4_TRIGGER(0xbe))
        return;
    AnimSetUpdateInterval(M4_ATURRET(0x18), 0x14);
    AnimSetUpdateInterval(M4_ATURRET(0x1a), 0x14);
    AnimPlay(M4_ATURRET(0x18), 0xc, 0x60);
    InTime3(2000, AnimPlay, M4_ATURRET(0x1a), 0x18, 0x60);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M4_OnTriggerTurretPair);
}

// 0x100025d0  EV_TRIGGER_ENTER: headlight on in trigger 0x4006e, off in 0x4006d
static void M4_OnTriggerHeadlight(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x6e) && st.headlightOn == 0) {
        Lander_SetHeadLight(M4_LANDER, 0x31c, 0xc00);
        st.headlightOn = 1;
    }
    if ((ObjId)ev->arg[1] == M4_TRIGGER(0x6d) && st.headlightOn == 1) {
        Lander_SetHeadLight(M4_LANDER, 0x31c, 0);
        st.headlightOn = 0;
    }
}

// 0x10002640  EV_DESTROYED of a pop-up turret stops its partner
static void M4_OnDestroyedTurretPair(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == M4_ATURRET(0x18)) {
        st.turretPairHit = 1;
        AnimStop(M4_ATURRET(0x1a), 0);
    }
    if ((ObjId)ev->arg[0] == M4_ATURRET(0x19)) {
        st.turretPairHit = 1;
        AnimStop(M4_ATURRET(0x17), 0);
    }
}

// 0x10002690  EV_PICKUP: +75 % shield, small cash, missiles (respawned when the rack is full)
static void M4_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 0xa2)) {
        int32_t shield = (int32_t)Object_GetProperty(M4_LANDER, PROP_HITPOINTS) + 0xc0;
        Voice_ShieldAcquired(M4_LANDER, 75);
        Object_SetProperty(M4_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 0x10) || (ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 0x11)) {
        Lander_SetCash(M4_LANDER, Lander_GetCash(M4_LANDER) + gSmallCash);
        Voice_CashAcquired(M4_LANDER);
    }
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)OBJ_ID(OT_PICKUP, 0x74) && id <= (int32_t)OBJ_ID(OT_PICKUP, 0x76)) {
        if (Lander_AddMissiles(M4_LANDER, 1) == 0) {
            Voice_MissilesFull(M4_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
        } else {
            Voice_MissilesAcquired(M4_LANDER);
        }
    }
}

// 0x10002780  EV_DESTROYED fuel dump 0x11002c: its light 0x16006b goes out
static void M4_OnDestroyedDump2C(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_FUELDUMP, 0x2c))
        return;
    Object_SetDead(OBJ_ID(OT_LIGHTOBJECT, 0x6b));
    EventType_RemoveHandler(EV_DESTROYED, M4_OnDestroyedDump2C);
}

// 0x100027b0  EV_DESTROYED fuel dump 0x11002d: light 0x16006c
static void M4_OnDestroyedDump2D(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_FUELDUMP, 0x2d))
        return;
    Object_SetDead(OBJ_ID(OT_LIGHTOBJECT, 0x6c));
    EventType_RemoveHandler(EV_DESTROYED, M4_OnDestroyedDump2D);
}

// 0x100027e0  common set-up
static void M4_Setup(void)
{
    static const struct { int32_t delay; uint8_t mine; } mineStart[7] = {   // mines start rocking
        { 0x7b, 0x89 }, { 0xd5, 0x8a }, { 0x142, 0x8b }, { 0x20b, 0x8c },
        { 0x5b, 0x8d }, { 0x115, 0x8e }, { 0x22, 0x8f },
    };
    st.text = m4_text[gLanguage];
    gTurretTypes[2] = &m4_turretType2;
    gTurretTypes[10] = &m4_turretType10;
    gTurretTypes[5] = &m4_turretType5;
    for (int32_t i = 0x6f; i <= 0x71; i++)
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x96);
    for (int32_t id = (int32_t)M4_MINE_FIRST; id <= (int32_t)M4_MINE_LAST; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 5);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    for (int32_t i = 0x03; i <= 0x09; i++) {
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 5);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x64);
    }
    for (int32_t i = 0x2c; i <= 0x2d; i++) {
        Object_SetProperty(OBJ_ID(OT_FUELDUMP, i), PROP_ARMOUR, 0xa);
        Object_SetProperty(OBJ_ID(OT_FUELDUMP, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x55; i <= 0x57; i++) {
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0xa);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x1e; i <= 0x22; i++)
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x64);
    for (int32_t i = 0x2e; i <= 0x54; i++) {
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0xa);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x1e);
    }
    for (int32_t i = 7; i <= 0x11; i++)                          // explosion mobs
        Object_SetProperty(MOB_ID(OT_EXPLOSION, i), PROP_EXPLOSION, 0x64);
    for (int32_t id = 0x5b; id <= 0x5c; id++)                    // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x19);
    for (int32_t id = 0xba; id <= 0xbd; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t i = 0x16; i <= 0x21; i++) {
        Object_SetProperty(M4_ATURRET(i), PROP_ARMOUR, 0x1e);
        Object_SetProperty(M4_ATURRET(i), PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = 0x72; id <= 0x73; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x58), PROP_ARMOUR, 0x19);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 15);
    WindowSetText(st.win, st.text[0]);
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x23;
    gNbWaypoints = m4_nbWaypoints;
    gWaypoints = m4_waypoints;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerDepotDoors);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M4_OnTriggerDepotDoors);
    EventType_AddHandler(EV_COLLISION, NULL, M4_OnCollisionMine);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerHatchA7);
    EventType_AddHandler(EV_DESTROYED, NULL, M4_OnDestroyedWall);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerDepotFloor);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, M4_OnProjectileHitFuel);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerDoorA1);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M4_OnTriggerDoorA1);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerObjective);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M4_OnTriggerSignLeave);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerDepotLift);
    EventType_AddHandler(EV_POD_HOOKED, NULL, M4_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerBlastZone);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M4_OnTriggerBlastZone);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x100024d0
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerTurretPair);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M4_OnTriggerHeadlight);
    EventType_AddHandler(EV_DESTROYED, NULL, M4_OnDestroyedTurretPair);
    EventType_AddHandler(EV_PICKUP, NULL, M4_OnPickup);
    EventType_AddHandler(EV_DESTROYED, NULL, M4_OnDestroyedDump2C);
    EventType_AddHandler(EV_DESTROYED, NULL, M4_OnDestroyedDump2D);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x9a), 4, 0x81);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x99), 4, 0x81);
    AnimPlay(M4_ATURRET(0x21), 0, 0x61);
    AnimSetUpdateInterval(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x9a), 0x32);
    AnimSetUpdateInterval(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x99), 0x32);
    AnimPlay(OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x90), 6, 1);
    for (int i = 0; i < 7; i++)
        InTime3(mineStart[i].delay, AnimPlay, OBJ_ID(OT_ANIMDESTRUCTIBLE, mineStart[i].mine), 6, 1);
    AnimPlay(M4_ANIM(0x24), 8, 1);
    for (int32_t i = 0x25; i <= 0x2a; i++)
        AnimPlay(M4_ANIM(i), 8, 0x81);
    for (int32_t i = 0x16; i <= 0x21; i++)
        Sound_PlayLoop(M4_ATURRET(i), 0x91, -1);
    for (int32_t i = 0x99; i <= 0x9a; i++)
        Sound_PlayLoop(OBJ_ID(OT_ANIMDESTRUCTIBLE, i), 0xd3, -1);
    for (int32_t id = (int32_t)M4_MINE_FIRST; id <= (int32_t)M4_MINE_LAST; id++)
        Sound_PlayLoop((ObjId)id, 0x101, -1);
    for (int32_t i = 0x24; i <= 0x2a; i++)
        Sound_PlayLoop(M4_ANIM(i), 0xb6, -1);
    for (int32_t i = 0x17; i <= 0x21; i++)                       // anim turrets 0x17..0x21
        MobObj_Wake(i);
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x23));
    MobObj_Wake(0x13);              // the nuke
    MobObj_Wake(6);                 // the lift
    Object_SetDead(OBJ_ID(OT_PARTICLEEMITTER, 0x0d));
    Object_SetDead(OBJ_ID(OT_PARTICLEEMITTER, 0x0e));
    Object_SetDead(OBJ_ID(OT_PARTICLEEMITTER, 0x0f));
    for (int32_t i = 0x16; i <= 0x1a; i++)
        Object_SetDead(OBJ_ID(OT_PARTICLEEMITTER, i));
    for (int32_t i = 0x9c; i <= 0x9f; i++)
        Object_SetDead(M4_ANIM(i));
    for (int32_t i = 0xb7; i <= 0xb9; i++)
        Object_SetDead(M4_ANIM(i));
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x5b), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x58), PROP_STATE, 0);
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0x87), PROP_STATE, 0x32);
    Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0x5d), PROP_STATE, 0x96);
    MissionScore_Init(M4_LANDER, 240000, 6);                      // 0x10001530
    MobObj_Wake(2);                 // AmbientFX 2, 3, 0x15
    MobObj_Wake(3);
    MobObj_Wake(0x15);
    Media_AudioPlay(0);
}

// 0x10002eb0  LevelStartLevel_Lander (export 0x10001010)
static void M4_StartLander(void)
{
    MobObj_Wake(M4_MOB_LANDER);
    MobObj_Wake(M4_MOB_CAMERA);
    Object_SetProperty(M4_CAMERA, PROP_TARGET, M4_LANDER);
    M4_Setup();
}

// 0x10002ef0  LevelStartLevel_Flycam (export 0x10001020)
static void M4_StartFlycam(void)
{
    M4_Setup();
    MobObj_Wake(M4_MOB_FLYCAM);
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void M4_Stop(void)
{
}

// 0x10001040
static uint32_t M4_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001050, return 0. Port: fresh DLL data.
static int32_t M4_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.countdown = m4_countdownInit;
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m4_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000b1d0, .count = 3 * 11, .out = m4_text },
    { 0 },
};

const mission_script_t mission_mars_4 = {
    .name          = "MARS\\4",
    .get_version   = M4_GetVersion,
    .load          = M4_Load,
    .start_lander  = M4_StartLander,
    .start_flycam  = M4_StartFlycam,
    .start_animcam = NULL,
    .stop          = M4_Stop,
    .text          = m4_gametext,
};
