// MISSIONS\EUROPA\2\MISSIONR.DLL - faithful C port.
//
// Mission: find and destroy the ten target artefacts (Destructibles 0x8002a..31 and the anim-
// destructibles 0x10f002f / 0x10f0034, the last two part of a segmented creature); the objective
// line counts them down (a format with the count); after the last one climb above
// y = 10000: success 1, +300000 cash and the CUT2 film. Par 30 s (sic), 3 pickups.
// The level is a set of chambers joined by teleport planes (0x1a0038..3f, in pairs): crossing one
// moves the lander (and a hooked pod) to its partner; the lander must leave the arrival box
// before the next jump. Four power cells (pods 0x10c0012..15) fit into slots (triggers
// 0x40008..10): slot pairs (i, i+3) power doors, turrets and teleporters (the model of 0x80001
// shows the power level). A segmented creature (AnimDestructible / anim-turret mobs, chain
// 0x20..0x43) crawls along marker paths once trigger 0x4008a is entered.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030 (= the empty 0x10002460), LevelGetVersion 0x10001040. The score library
// (0x10001050-0x100015f4, mars_1a's code) is in mission_lib.c; the DLL's sprintf (0x100038c0)
// is the C library's.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define E2_LANDER        MOB_ID(OT_LANDER, 0x1e)         // 0x0101001e
#define E2_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define E2_MOB_CAMERA    1
#define E2_MOB_FLYCAM    0x19
#define E2_MOB_LANDER    0x1e
#define E2_POD_FIRST     MOB_ID(OT_POD, 0x12)            // 0x010c0012..15 power cells
#define E2_POD_LAST      MOB_ID(OT_POD, 0x15)
#define E2_POWER         OBJ_ID(OT_DESTRUCTIBLE, 0x01)   // 0x00080001 power display (model)
#define E2_ATURRET(n)    MOB_ID(OT_ANIMTURRET, (n))      // 0x010e00nn
#define E2_SEGMENT(n)    MOB_ID(OT_ANIMDESTRUCTIBLE, (n)) // 0x010f00nn
#define E2_EXPLOSION(n)  MOB_ID(OT_EXPLOSION, (n))       // 0x010900nn
#define E2_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define E2_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define E2_PLANE(n)      OBJ_ID(OT_TRIGGERPLANE, (n))    // 0x001a00nn
#define E2_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t e2_artefactsInit = 10;             // 0x1000c160
static const int32_t e2_link03Init = 1;                 // 0x1000c164 slots 0 and 3 start filled
static const int32_t e2_chainLast = 7;                  // 0x1000c168 last index of the chain
static const ObjId   e2_carriedInit = OBJ_NONE;         // 0x1000c16c pod carried through teleports
static const ObjId   e2_chain[8] = {                    // 0x1000c170 the creature's segments
    0x10f0020, 0x10e0025, 0x10f002a, 0x10f002f, 0x10f0034, 0x10f0039, 0x10f003e, 0x10e0043,
};
// 0x1000c190: [language][3] string pointers (strings at 0x1000c2c4-0x1000c457)
static const char *e2_text[3][3];
static const int32_t e2_slotsInit[9] = { 1, 0, 0, 1, 0, 0, 0, 0, 0 };   // 0x1000c1b8
static const int32_t  e2_nbWaypoints = 1;               // 0x1000c1dc
static const Waypoint e2_waypoints[1] = {               // 0x1000c1e0
    { -12800, -6400 },
};
static const TurretType e2_turretType15 = {             // 0x1000c1e8 -> gTurretTypes[15]
    { 60, 0, 198, 0 }, 0x00, 0, 4, 2, 60, 0x20, 800, 50000, 0
};
static const TurretType e2_turretType21 = {             // 0x1000c208 -> gTurretTypes[21]
    { 384, 4736, 0, 0 }, 0x00, 0, 4, 2, 32, 0x20, 800, 12000, 0
};
static const TurretType e2_turretType0 = {              // 0x1000c228 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 2, 10, 100, 0x10, 1024, 12000, 0
};
static const TurretType e2_turretType3 = {              // 0x1000c248 -> gTurretTypes[3]
    { 0, 18, 158, 0 }, 0x05, 0, 6, 5, 100, 0x10, 1024, 12000, 0
};
static const TurretType e2_turretType5 = {              // 0x1000c268 -> gTurretTypes[5]
    { 85, 434, 461, 0 }, 0x02, 2, 1, 0, 60, 0x20, 800, 12000, 0
};
static const TurretType e2_turretType6 = {              // 0x1000c288 -> gTurretTypes[6]
    { 166, 243, 576, 0 }, 0x02, 1, 1, 0, 60, 0x18, 800, 12000, 0
};
static const TurretType e2_turretType7 = {              // 0x1000c2a8 -> gTurretTypes[7]
    { -19, 0, 793, 0 }, 0x00, 0, 3, 7, 60, 0x18, 1024, 12000, 0
};
static const char e2_endMovie[] = "CUT2";               // 0x1000c458

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      artefacts;     // 0x1000c160 (.data, 10) left to destroy
    int32_t      link03;        // 0x1000c164 (.data, 1) slots 0/3 linked
    ObjId        carried;       // 0x1000c16c (.data, -1) the hooked pod
    int32_t      slots[9];      // 0x1000c1b8 (.data) a cell sits in slot i (trigger 0x40008 + i)
    int32_t      link14;        // 0x1000f8d8 slots 1/4 linked
    int32_t      power;         // 0x1000f8dc power level shown by 0x80001
    int32_t      link25;        // 0x1000f8e0 slots 2/5 linked (written, never read)
    Window      *win;           // 0x1000f8e4 objective line
    int32_t      segSteps[8];   // 0x1000f8e8 marker steps done by each segment
    int32_t      turretA;       // 0x1000f914 anim-turret 0x1a raised
    int32_t      turretB;       // 0x1000f918 anim-turret 0x1b raised
    int32_t      turretC;       // 0x1000f91c anim-turret 0x1c raised
    int32_t      ticks;         // 0x1000f920
    char         buf[256];      // 0x1000fec0 sprintf buffer
    ObjId        lastHooked;    // 0x1000ffc0 (written, never read)
    const char *const *text;    // 0x1000ffc4 = e2_text[gLanguage]
} st;

// 0x10001600  move the lander (a), a hooked pod (b) and the camera (c, position only) by the
// transform from one teleport plane to its partner; -1 = skip
static void E2_Teleport(ObjId a, ObjId b, ObjId c, ObjId from, ObjId to)
{
    SetupOrientationTeleport(from, to);
    if (a != OBJ_NONE)
        OrientationTeleport(a);
    if (b != OBJ_NONE)
        OrientationTeleport(b);
    if (c != OBJ_NONE)
        PositionTeleport(c);
}

// 0x10001650  index of a segment in the chain (0 when absent)
static int32_t E2_ChainIndex(ObjId id)
{
    for (int32_t i = 0; i < 8; i++)
        if (e2_chain[i] == id)
            return i;
    return 0;
}

// 0x10001670  EV_DESTROYED (after trigger 0x40089): a hit on a joint stops the rest of the chain;
// the DLL writes the masked id back into the event
static void E2_OnSegmentDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0] & 0x3ffffffu;
    ((LanderEvent *)ev)->arg[0] = (intptr_t)id;   // the engine's dispatch copy is writable
    if (id == 0x10f0020 || id == 0x10f002a || id == 0x10f0039 || id == 0x10f003e) {
        if (E2_ChainIndex(id) <= e2_chainLast) {
            for (int32_t i = E2_ChainIndex(id); i <= e2_chainLast; i++)
                AnimStop(e2_chain[i], 0);
        }
        if (id == 0x10f002a) {
            if ((int32_t)Object_GetProperty(E2_SEGMENT(0x2f), PROP_HITPOINTS) > 0)
                Object_SetProperty(E2_SEGMENT(0x2f), PROP_HITPOINTS, 0);
            if ((int32_t)Object_GetProperty(E2_SEGMENT(0x34), PROP_HITPOINTS) > 0)
                Object_SetProperty(E2_SEGMENT(0x34), PROP_HITPOINTS, 0);
        }
    }
    if (id == 0x10e0025 || id == 0x10f002f || id == 0x10f0034)
        MobObj_Sleep((int32_t)(id & 0xffff));
}

// 0x10001780  EV_ANIM_FINISHED of a segment (mob index 0x20..0x43): snap it to its next marker
// and play the next leg (k + 0x12, the fourth one k + 0x1a), stop after that
static void E2_OnSegmentLeg(const LanderEvent *ev)
{
    ObjId obj = (ObjId)ev->arg[0];
    int32_t idx = (int32_t)(obj & 0xffff);
    if (idx < 0x20 || idx > 0x43)
        return;
    int32_t k = (idx - 0x20) / 5;
    int32_t steps = st.segSteps[k];
    ObjId marker = (ObjId)((steps + idx + 1) | 0x1190000);     // MOB_ID(OT_MARKER, ...)
    if (steps == 4) {
        AnimStop(obj, 0);
        return;
    }
    AnimStop(obj, 0);
    Object_SetProperty((ObjId)ev->arg[0], PROP_MATRIX, Object_GetProperty(marker, PROP_MATRIX));
    if (steps == 3)
        AnimPlay((ObjId)ev->arg[0], k + 0x1a, 0xe0);
    else
        AnimPlay((ObjId)ev->arg[0], k + 0x12, 0xe0);
    st.segSteps[k]++;
}

static void E2_OnTriggerHatchOpen(const LanderEvent *ev);
static void E2_OnTriggerHatchClose(const LanderEvent *ev);

// 0x10001870  EV_TRIGGER_ENTER trigger 0x4008a (added by 0x40089): the creature comes out
static void E2_OnTriggerCreature(const LanderEvent *ev)
{
    static const struct { uint32_t id; int32_t anim; } legs[8] = {
        { 0x10f0020, 0xa }, { 0x10e0025, 0xb }, { 0x10f002a, 0xc }, { 0x10f002f, 0xd },
        { 0x10f0034, 0xe }, { 0x10f0039, 0xf }, { 0x10f003e, 0x10 }, { 0x10e0043, 0x11 },
    };
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x8a))
        return;
    for (int32_t i = 0x6f; i <= 0x76; i++)
        Object_SetDead(E2_ANIM(i));
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTriggerHatchOpen);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, E2_OnTriggerHatchClose);
    MobObj_Wake(0x20);
    MobObj_Wake(0x25);
    Object_SetProperty(E2_ATURRET(0x25), PROP_STATE, 0);
    MobObj_Wake(0x2a);
    MobObj_Wake(0x2f);
    MobObj_Wake(0x34);
    MobObj_Wake(0x39);
    MobObj_Wake(0x3e);
    MobObj_Wake(0x43);
    Object_SetProperty(E2_ATURRET(0x43), PROP_STATE, 0);
    Object_SetProperty(E2_TRIGGER(0x87), PROP_TARGET, E2_SEGMENT(0x20));
    Object_SetProperty(E2_TRIGGER(0x88), PROP_TARGET, E2_SEGMENT(0x20));
    for (int i = 0; i < 8; i++) {
        InTime3(2000, AnimPlay, legs[i].id, legs[i].anim, 0xe0);
        AnimSetUpdateInterval(legs[i].id, 0x1e);
    }
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, E2_OnSegmentLeg);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTriggerCreature);
}

// 0x10001ae0  EV_TRIGGER_ENTER of a slot (the slots watch the hooked cell; re-armed 2 s after each
// hook): the cell drops into the slot; a filled opposite slot (i, i+3) powers things up
static void E2_OnCellInSlot(const LanderEvent *ev)
{
    int32_t pod = (int32_t)ev->arg[0];
    if (pod < (int32_t)E2_POD_FIRST || pod > (int32_t)E2_POD_LAST)
        return;
    int32_t trig = (int32_t)ev->arg[1];
    int32_t slot = trig - (int32_t)E2_TRIGGER(0x08);
    if ((uint32_t)slot >= 9)     // port guard: only the slot triggers watch cells
        return;
    if (st.slots[slot] == 0) {
        Object_SetProperty((ObjId)pod, PROP_TARGET, -1);
        SnapToTarget((ObjId)ev->arg[0], (ObjId)ev->arg[1]);
        st.carried = OBJ_NONE;
        st.slots[slot] = 1;
        if (trig == (int32_t)E2_TRIGGER(0x0e)) {
            for (int32_t i = 0x4b; i <= 0x50; i++)
                AnimPlay(E2_ANIM(i), 4, 0x81);
        } else if (trig == (int32_t)E2_TRIGGER(0x0f)) {
            Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x83), -1);
            Object_SetAlive(E2_PLANE(0x3c));
        } else if (trig == (int32_t)E2_TRIGGER(0x10)) {
            Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x7f), -1);
            Object_SetAlive(E2_PLANE(0x38));
        } else if (st.slots[(slot + 3) % 6] == 1) {
            int32_t delta = 100;
            if (slot == 0 || slot == 3) {
                delta = -st.power;
                st.power = 0;
                Object_SetAlive(E2_PLANE(0x39));
                Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x80), -1);
                Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x81), -1);
                Object_SetAlive(E2_PLANE(0x3a));
                st.link03 = 1;
            }
            if (slot == 1 || slot == 4) {
                delta = 2 - st.power;
                st.power = 2;
                if (!st.turretA) {
                    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x51), PROP_HITPOINTS, 0);
                    MobObj_Wake(0x1a);
                    Sound_PlayLoop(E2_ATURRET(0x1a), 0x2d0, -1);
                    AnimSetUpdateInterval(E2_ATURRET(0x1a), 0x32);
                    AnimPlay(E2_ATURRET(0x1a), 5, 0x61);
                    st.turretA = 1;
                }
                Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x85), -1);
                Object_SetAlive(E2_PLANE(0x3e));
                st.link14 = 1;
                AnimPlay(E2_ANIM(0x49), 7, 0);
                AnimPlay(E2_ANIM(0x4a), 7, 0);
                Sound_PlayOnce(E2_ANIM(0x49), 0x3f7);
            }
            if (slot == 2 || slot == 5) {
                delta = 3 - st.power;
                st.link25 = 1;
                st.power = 3;
                if (!st.turretB) {
                    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x52), PROP_HITPOINTS, 0);
                    MobObj_Wake(0x1b);
                    Sound_PlayLoop(E2_ATURRET(0x1b), 0x2d0, -1);
                    AnimSetUpdateInterval(E2_ATURRET(0x1b), 0x32);
                    AnimPlay(E2_ATURRET(0x1b), 6, 0x61);
                    st.turretB = 1;
                }
                if (!st.turretC) {
                    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x53), PROP_HITPOINTS, 0);
                    MobObj_Wake(0x1c);
                    Sound_PlayLoop(E2_ATURRET(0x1c), 0x2d0, -1);
                    AnimSetUpdateInterval(E2_ATURRET(0x1c), 0x32);
                    InTime3(0x3a98, AnimPlay, E2_ATURRET(0x1c), 6, 0x61);
                    st.turretC = 1;
                }
            }
            if (delta != 0)
                Object_AdvanceDestroyLevel(E2_POWER, delta);
        }
    }
    Object_SetProperty((ObjId)ev->arg[0], PROP_TARGET, -2);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnCellInSlot);
}

// 0x10001ec0  EV_TRIGGER_LEAVE of a slot by a cell: power down what it powered
static void E2_OnCellOutOfSlot(const LanderEvent *ev)
{
    int32_t pod = (int32_t)ev->arg[0];
    if (pod < (int32_t)E2_POD_FIRST || pod > (int32_t)E2_POD_LAST)
        return;
    int32_t trig = (int32_t)ev->arg[1];
    int32_t slot = trig - (int32_t)E2_TRIGGER(0x08);
    if ((uint32_t)slot >= 9)     // port guard: only the slot triggers watch cells
        return;
    st.slots[slot] = 0;
    if (trig == (int32_t)E2_TRIGGER(0x0e)) {
        for (int32_t i = 0x4b; i <= 0x50; i++)
            AnimStop(E2_ANIM(i), 1);
        return;
    }
    if (trig == (int32_t)E2_TRIGGER(0x0f)) {
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x83), 1);
        Object_SetDead(E2_PLANE(0x3c));
        return;
    }
    if (trig == (int32_t)E2_TRIGGER(0x10)) {
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x7f), 1);
        Object_SetDead(E2_PLANE(0x38));
        return;
    }
    if ((trig == (int32_t)E2_TRIGGER(0x08) || trig == (int32_t)E2_TRIGGER(0x0b)) && st.link03 == 1) {
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x80), 1);
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x81), 1);
        Object_SetDead(E2_PLANE(0x39));
        Object_SetDead(E2_PLANE(0x3a));
        st.link03 = 0;
    }
    if ((ev->arg[1] == (intptr_t)E2_TRIGGER(0x09) || ev->arg[1] == (intptr_t)E2_TRIGGER(0x0c)) && st.link14 == 1) {
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x85), 1);
        Object_SetDead(E2_PLANE(0x3e));
        st.link14 = 0;
        AnimPlay(E2_ANIM(0x49), 7, 4);
        AnimPlay(E2_ANIM(0x4a), 7, 4);
        Sound_PlayOnce(E2_ANIM(0x49), 0x3f7);
    }
    if (ev->arg[1] == (intptr_t)E2_TRIGGER(0x0d) || ev->arg[1] == (intptr_t)E2_TRIGGER(0x0a))
        st.link25 = 0;
    int32_t delta = 1 - st.power;
    if (delta != 0) {
        Object_AdvanceDestroyLevel(E2_POWER, delta);
        st.power = 1;
    }
}

// 0x100020b0  EV_POD_HOOKED: every slot now watches this cell; the slot handler is armed 2 s later
static void E2_OnCellHooked(const LanderEvent *ev)
{
    st.lastHooked = (ObjId)ev->arg[0];
    st.carried = (ObjId)ev->arg[0];
    for (int32_t i = 0x08; i <= 0x10; i++)
        Object_SetProperty(E2_TRIGGER(i), PROP_TARGET, ev->arg[0]);
    InTime3(2000, EventType_AddHandler, EV_TRIGGER_ENTER, (intptr_t)NULL, E2_OnCellInSlot);
}

// 0x10002180  EV_TICK_1S: 13 s into the mission the guards 2 and 8 blow up and are replaced by
// the anim-turrets 0x16 / 0x17
static void E2_OpeningTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.ticks != 13)
        return;
    MobObj_Sleep(2);
    Sound_StopObject(E2_ATURRET(2));
    MobObj_Wake(0x16);
    InTime2(0x5a, Sound_PlayOnce, E2_ATURRET(0x16), 0x8b);
    MobObj_Wake(4);
    Sound_PlayOnce(E2_EXPLOSION(4), 0x72);
    InTime1(0x32, MobObj_Wake, 5);
    InTime2(0x32, Sound_PlayOnce, E2_EXPLOSION(5), 0x72);
    InTime1(0x64, MobObj_Wake, 6);
    InTime2(0x64, Sound_PlayOnce, E2_EXPLOSION(6), 0x72);
    Object_SetProperty(E2_ATURRET(0x16), PROP_STATE, 0);
    AnimPlay(E2_ATURRET(0x16), 0x26, 0xe0);
    MobObj_Sleep(8);
    Sound_StopObject(E2_ATURRET(8));
    MobObj_Wake(0x17);
    InTime2(0x5a, Sound_PlayOnce, E2_ATURRET(0x17), 0x8b);
    MobObj_Wake(0xa);
    Sound_PlayOnce(E2_EXPLOSION(0xa), 0x72);
    InTime1(0x32, MobObj_Wake, 0xb);
    InTime2(0x32, Sound_PlayOnce, E2_EXPLOSION(0xb), 0x72);
    InTime1(0x64, MobObj_Wake, 0xc);
    InTime2(0x64, Sound_PlayOnce, E2_EXPLOSION(0xc), 0x72);
    Object_SetProperty(E2_ATURRET(0x17), PROP_STATE, 0);
    AnimPlay(E2_ATURRET(0x17), 0x29, 0xe0);
    EventType_RemoveHandler(EV_TICK_1S, E2_OpeningTick);
}

// 0x10002310  EV_ANIM_TURNED: the replacement turrets reached their place (anims 0x29 / 0x26)
static void E2_OnGuardsArrived(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x29) {
        Sound_StopObject(E2_ATURRET(0x17));
        Object_SetDead(E2_ATURRET(0x17));
        Object_SetProperty(E2_EXPLOSION(9), PROP_EXPLOSION, 0xc8);
        MobObj_Wake(9);
        Object_SetAlive(E2_EMITTER(0x62));
        Object_SetAlive(E2_EMITTER(0x15));
        Sound_PlayLoop(E2_EMITTER(0x62), 0xf1, -1);
        Sound_PlayLoop(E2_EMITTER(0x15), 0xf0, -1);
    } else if (ev->arg[1] == 0x26) {
        Sound_StopObject(E2_ATURRET(0x16));
        Object_SetProperty(E2_EXPLOSION(3), PROP_EXPLOSION, 0xc8);
        MobObj_Wake(3);
        Object_SetDead(E2_ATURRET(0x16));
        Object_SetAlive(E2_EMITTER(0x61));
        Object_SetAlive(E2_EMITTER(0x14));
        Sound_PlayLoop(E2_EMITTER(0x61), 0xf1, -1);
        Sound_PlayLoop(E2_EMITTER(0x14), 0xf0, -1);
    }
}

// 0x10002430  EV_DESTROYED 0x80002: the guard turret 2 picks its next target
static void E2_OnDestroyedTarget2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_DESTRUCTIBLE, 0x02))
        Object_SetProperty(E2_ATURRET(2), PROP_TARGET, OBJ_ID(OT_DESTRUCTIBLE, 0x03));
}

// 0x10002460  empty; also registered as an EV_DESTROYED handler
static void E2_Nothing(void)
{
}

static void E2_OnDestroyedNothing(const LanderEvent *ev)
{
    (void)ev;
    E2_Nothing();
}

// 0x10002470  EV_ANIM_FINISHED with anim 0x27: the hatch 0x3006f..72 closes, mobs 0x48..4a wake,
// doors 0x3006d/6e open
static void E2_OnHatchAnim(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x27)
        return;
    AnimPlay(E2_ANIM(0x6f), 2, 0x84);
    Sound_PlayOnce(E2_ANIM(0x6f), 0x3f8);
    AnimPlay(E2_ANIM(0x70), 2, 0x84);
    AnimPlay(E2_ANIM(0x71), 2, 0x84);
    AnimPlay(E2_ANIM(0x72), 2, 0x84);
    MobObj_Wake(0x48);
    MobObj_Wake(0x49);
    MobObj_Wake(0x4a);
    AnimPlay(E2_ANIM(0x6d), 1, 0);
    Sound_PlayOnce(E2_ANIM(0x6d), 0xa9);
    AnimPlay(E2_ANIM(0x6e), 1, 0);
}

// 0x10002530 / 0x100025e0  EV_TRIGGER_ENTER / LEAVE trigger 0x4005b: hatch 0x30073..76
static void E2_OnTriggerHatchOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x5b))
        return;
    for (int32_t i = 0x73; i <= 0x76; i++) {
        AnimPlay(E2_ANIM(i), 2, 0x80);
        AnimSetUpdateInterval(E2_ANIM(i), 3);
        if (i == 0x73)
            Sound_PlayOnce(E2_ANIM(0x73), 0x3f8);
    }
}

static void E2_OnTriggerHatchClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x5b))
        return;
    AnimPlay(E2_ANIM(0x73), 2, 0x84);
    Sound_PlayOnce(E2_ANIM(0x73), 0x3f8);
    AnimPlay(E2_ANIM(0x74), 2, 0x84);
    AnimPlay(E2_ANIM(0x75), 2, 0x84);
    AnimPlay(E2_ANIM(0x76), 2, 0x84);
}

// 0x10002650 / 0x100026a0  trigger 0x40056: door 0x30054/55
static void E2_OnTriggerDoor54Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x56))
        return;
    AnimPlay(E2_ANIM(0x54), 3, 0x80);
    Sound_PlayOnce(E2_ANIM(0x54), 0xab);
    AnimPlay(E2_ANIM(0x55), 3, 0x80);
}

static void E2_OnTriggerDoor54Close(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x56))
        return;
    AnimPlay(E2_ANIM(0x54), 3, 0x84);
    Sound_PlayOnce(E2_ANIM(0x54), 0xab);
    AnimPlay(E2_ANIM(0x55), 3, 0x84);
}

// 0x100026f0  EV_DESTROYED of one of the three parts 0x10f0050 / 18 / 0d: all of them go
static void E2_OnDestroyedTrio(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id == E2_SEGMENT(0x50)) {
        AnimStop(E2_SEGMENT(0x18), 0);
        AnimStop(E2_SEGMENT(0x0d), 0);
        Object_SetProperty(E2_SEGMENT(0x18), PROP_HITPOINTS, 0);
    } else if (id == E2_SEGMENT(0x18)) {
        AnimStop(E2_SEGMENT(0x0d), 0);
        AnimStop(E2_SEGMENT(0x50), 0);
        Object_SetDead(E2_SEGMENT(0x0d));
        Object_SetProperty(E2_SEGMENT(0x50), PROP_HITPOINTS, 0);
        EventType_RemoveHandler(EV_DESTROYED, E2_OnDestroyedTrio);
        return;
    } else if (id == E2_SEGMENT(0x0d)) {
        AnimStop(E2_SEGMENT(0x18), 0);
        AnimStop(E2_SEGMENT(0x50), 0);
        Object_SetProperty(E2_SEGMENT(0x18), PROP_HITPOINTS, 0);
        Object_SetProperty(E2_SEGMENT(0x50), PROP_HITPOINTS, 0);
    } else {
        return;
    }
    Object_SetDead(E2_SEGMENT(0x0d));
    EventType_RemoveHandler(EV_DESTROYED, E2_OnDestroyedTrio);
}

// 0x100027f0  EV_TRIGGER_ENTER trigger 0x4008b: the trio 0x10f0050 / 18 / 0d sets off
static void E2_OnTriggerTrio(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x8b))
        return;
    MobObj_Wake(0x50);
    MobObj_Wake(0x18);
    MobObj_Wake(0xd);
    InTime3(2000, AnimPlay, E2_SEGMENT(0x50), 0x22, 0);
    InTime3(2000, AnimPlay, E2_SEGMENT(0x18), 0x23, 0);
    InTime3(2000, AnimPlay, E2_SEGMENT(0x0d), 0x24, 0);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnDestroyedTrio);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTriggerTrio);
}

// 0x100028a0  EV_TRIGGER_ENTER trigger 0x40089: anim-turrets 0x4b..4e rise; arms the creature
static void E2_OnTrigger89(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x89))
        return;
    AnimPlay(E2_ANIM(0x49), 7, 4);
    AnimPlay(E2_ANIM(0x4a), 7, 4);
    MobObj_Wake(0x4b);
    MobObj_Wake(0x4c);
    MobObj_Wake(0x4d);
    MobObj_Wake(0x4e);
    for (int32_t i = 0x4b; i <= 0x4e; i++)
        Sound_PlayLoop(E2_ATURRET(i), 0x91, -1);
    AnimPlay(E2_ATURRET(0x4b), 8, 0x60);
    AnimSetUpdateInterval(E2_ATURRET(0x4b), 0x32);
    InTime3(2000, AnimPlay, E2_ATURRET(0x4c), 0x2a, 0x60);
    AnimSetUpdateInterval(E2_ATURRET(0x4c), 0x32);
    InTime3(4000, AnimPlay, E2_ATURRET(0x4d), 0x2b, 0x60);
    AnimSetUpdateInterval(E2_ATURRET(0x4d), 0x32);
    InTime3(6000, AnimPlay, E2_ATURRET(0x4e), 0x2c, 0x60);
    AnimSetUpdateInterval(E2_ATURRET(0x4e), 0x32);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTrigger89);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerCreature);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnSegmentDestroyed);
}

static void E2_OnPlane(const LanderEvent *ev);

// 0x100029e0  EV_TRIGGER_LEAVE of an arrival box 0x40077..7e: the teleporters work again
static void E2_OnLeaveArrival(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)E2_TRIGGER(0x77) || trig > (int32_t)E2_TRIGGER(0x7e))
        return;
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, E2_OnPlane);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, E2_OnLeaveArrival);
}

// the end of every teleport: once in the arrival box, wait until the lander has left it
static int E2_Arrived(ObjId box)
{
    if (TrigBox_Test(box, E2_LANDER) != 1)
        return 0;
    EventType_RemoveHandler(EV_TRIGGER_PLANE, E2_OnPlane);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E2_OnLeaveArrival);
    return 1;
}

// 0x10002a20  EV_TRIGGER_PLANE {player, plane, side}: fuel topped up; planes 0x1a0038..3f teleport
// to their partner (jump table 0x10002cd4), some also change the lighting
static void E2_OnPlane(const LanderEvent *ev)
{
    Lander_SetFuel(E2_LANDER, 20000000);
    switch ((uint32_t)ev->arg[1] - E2_PLANE(0x38)) {
    case 0:
        E2_Teleport(E2_LANDER, st.carried, E2_CAMERA, E2_PLANE(0x38), E2_PLANE(0x39));
        E2_Arrived(E2_TRIGGER(0x78));
        Environment_SetInteriorAmbient(0x99, 0x99, 0x99);
        break;
    case 1:
        E2_Teleport(E2_LANDER, st.carried, E2_CAMERA, E2_PLANE(0x39), E2_PLANE(0x38));
        E2_Arrived(E2_TRIGGER(0x77));
        Environment_SetInteriorAmbient(0xf, 0xc, 0x18);
        break;
    case 2:
        E2_Teleport(E2_LANDER, st.carried, E2_CAMERA, E2_PLANE(0x3a), E2_PLANE(0x3b));
        E2_Arrived(E2_TRIGGER(0x7a));
        break;
    case 3:
        E2_Teleport(E2_LANDER, st.carried, E2_CAMERA, E2_PLANE(0x3b), E2_PLANE(0x3a));
        E2_Arrived(E2_TRIGGER(0x79));
        break;
    case 4:
        E2_Teleport(E2_LANDER, st.carried, E2_CAMERA, E2_PLANE(0x3c), E2_PLANE(0x3d));
        E2_Arrived(E2_TRIGGER(0x7c));
        gInOutOverride = 1;
        break;
    case 5:
        E2_Teleport(E2_LANDER, st.carried, E2_CAMERA, E2_PLANE(0x3d), E2_PLANE(0x3c));
        E2_Arrived(E2_TRIGGER(0x7b));
        break;
    case 6:
        E2_Teleport(E2_LANDER, st.carried, E2_CAMERA, E2_PLANE(0x3e), E2_PLANE(0x3f));
        E2_Arrived(E2_TRIGGER(0x7e));
        Environment_SetInteriorAmbient(0xf, 0xc, 0x18);
        break;
    case 7:  // no pod comes along on this one
        E2_Teleport(E2_LANDER, OBJ_NONE, E2_CAMERA, E2_PLANE(0x3f), E2_PLANE(0x3e));
        E2_Arrived(E2_TRIGGER(0x7d));
        Environment_SetInteriorAmbient(0x99, 0x99, 0x99);
        break;
    default:
        break;
    }
}

// 0x10002d00  EV_TICK_1S once all artefacts are gone; never removed
static void E2_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(E2_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        SetLevelEndMovie(e2_endMovie);
        Lander_SetCash(E2_LANDER, Lander_GetCash(E2_LANDER) + 300000);
        MissionScore_End(1);                                         // 0x100013a0
    }
}

// 0x10002d60  EV_DESTROYED of an artefact: count down
static void E2_OnDestroyedArtefact(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (!(id >= (int32_t)OBJ_ID(OT_DESTRUCTIBLE, 0x2a) && id <= (int32_t)OBJ_ID(OT_DESTRUCTIBLE, 0x31)) &&
        id != (int32_t)E2_SEGMENT(0x2f) && id != (int32_t)E2_SEGMENT(0x34))
        return;
    if (--st.artefacts > 0) {
        snprintf(st.buf, sizeof st.buf, st.text[1], st.artefacts);   // sprintf 0x100038c0
        WindowSetText(st.win, st.buf);
        return;
    }
    WindowSetText(st.win, st.text[2]);
    EventType_AddHandler(EV_TICK_1S, NULL, E2_EvacTick);
}

// 0x10002df0  EV_PICKUP: +25 % / +50 % shield, two missiles (respawned when the rack is full)
static void E2_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 0x5c)) {
        int32_t shield = (int32_t)Object_GetProperty(E2_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(E2_LANDER, 25);
        Object_SetProperty(E2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 0x5d)) {
        int32_t shield = (int32_t)Object_GetProperty(E2_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(E2_LANDER, 50);
        Object_SetProperty(E2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == OBJ_ID(OT_PICKUP, 0x8c)) {
        if (Lander_AddMissiles(E2_LANDER, 2) == 0) {
            Voice_MissilesFull(E2_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
        } else {
            Voice_MissilesAcquired(E2_LANDER);
        }
    }
}

// 0x10002ef0 = MissionLib_OnDestroyedSound (same thresholds)

// 0x10002f80  common set-up
static void E2_Setup(void)
{
    st.text = e2_text[gLanguage];
    for (int32_t id = 0x1b; id <= 0x21; id++)                    // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x19);
    for (int32_t id = 0x32; id <= 0x37; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x67; i <= 0x6c; i++) {
        Object_SetProperty(OBJ_ID(OT_ANIMTURRET, i), PROP_ARMOUR, 0x32);
        Object_SetProperty(OBJ_ID(OT_ANIMTURRET, i), PROP_EXPLOSION, 0x46);
    }
    for (int32_t i = 0x48; i <= 0x4f; i++) {
        Object_SetProperty(E2_ATURRET(i), PROP_ARMOUR, 0x32);
        Object_SetProperty(E2_ATURRET(i), PROP_EXPLOSION, 0x46);
    }
    for (int32_t i = 0x22; i <= 0x25; i++) {
        Object_SetProperty(OBJ_ID(OT_ANIMTURRET, i), PROP_ARMOUR, 0x32);
        Object_SetProperty(OBJ_ID(OT_ANIMTURRET, i), PROP_EXPLOSION, 0x46);
    }
    for (int32_t i = 0x1a; i <= 0x1c; i++) {
        Object_SetProperty(E2_ATURRET(i), PROP_ARMOUR, 0x3c);
        Object_SetProperty(E2_ATURRET(i), PROP_EXPLOSION, 0x64);
    }
    for (int32_t id = 0x57; id <= 0x5a; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x19);
    Object_SetProperty(E2_ATURRET(0x25), PROP_ARMOUR, 0x3c);
    Object_SetProperty(E2_ATURRET(0x25), PROP_EXPLOSION, 0x50);
    static const uint8_t segs[6] = { 0x20, 0x2a, 0x2f, 0x34, 0x39, 0x3e };
    for (int i = 0; i < 6; i++) {
        Object_SetProperty(E2_SEGMENT(segs[i]), PROP_ARMOUR, 0x32);
        Object_SetProperty(E2_SEGMENT(segs[i]), PROP_EXPLOSION, 0x64);
    }
    Object_SetProperty(E2_ATURRET(0x43), PROP_ARMOUR, 0x32);
    Object_SetProperty(E2_ATURRET(0x43), PROP_EXPLOSION, 0x64);
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x1a), PROP_ARMOUR, 0xa);
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x1a), PROP_EXPLOSION, 0x50);
    for (int32_t i = 0x02; i <= 0x04; i++)
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0xff);
    for (int32_t i = 0x26; i <= 0x29; i++) {
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0x1e);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x2a; i <= 0x31; i++) {                     // artefacts
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0x19);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x64);
    }
    for (int32_t i = 0x40; i <= 0x48; i++)
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0xff);
    for (int32_t i = 0x51; i <= 0x53; i++) {
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0x1e);
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_EXPLOSION, 0x50);
    }
    for (int32_t i = 0x7f; i <= 0x86; i++)
        Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, i), PROP_ARMOUR, 0xff);
    Object_SetProperty(E2_SEGMENT(0x50), PROP_ARMOUR, 0x19);
    Object_SetProperty(E2_SEGMENT(0x50), PROP_EXPLOSION, 0x46);
    Object_SetProperty(E2_POWER, PROP_ARMOUR, 0xff);
    SetGravity(13);
    gAirResistance = 0xe;
    gMainThrottlePower = 0x1a;
    gNbWaypoints = e2_nbWaypoints;
    gWaypoints = e2_waypoints;
    MissionScore_Init(E2_LANDER, 30000, 3);                       // 0x10001520, before the
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);       // mission's own handlers
    WindowSetSize(st.win, 20, 20, 350, 12);
    WindowSetText(st.win, st.text[0]);
    TexAnimPlay(0x14, 3, 100);
    gTurretTypes[15] = &e2_turretType15;
    gTurretTypes[21] = &e2_turretType21;
    gTurretTypes[0] = &e2_turretType0;
    gTurretTypes[3] = &e2_turretType3;
    gTurretTypes[5] = &e2_turretType5;
    gTurretTypes[6] = &e2_turretType6;
    gTurretTypes[7] = &e2_turretType7;
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002ef0
    EventType_AddHandler(EV_POD_HOOKED, NULL, E2_OnCellHooked);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, E2_OnGuardsArrived);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnDestroyedTarget2);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnDestroyedNothing);           // 0x10002460
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, E2_OnHatchAnim);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerHatchOpen);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E2_OnTriggerHatchClose);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerDoor54Open);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E2_OnTriggerDoor54Close);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerTrio);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTrigger89);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, E2_OnPlane);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E2_OnCellOutOfSlot);
    EventType_AddHandler(EV_PICKUP, NULL, E2_OnPickup);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnDestroyedArtefact);
    MobObj_Wake(2);                 // guard anim-turrets 2, 7, 8
    MobObj_Wake(7);
    MobObj_Wake(8);
    Sound_PlayLoop(E2_ATURRET(2), 0x8d, -1);
    Sound_PlayLoop(E2_ATURRET(7), 0x8d, -1);
    Sound_PlayLoop(E2_ATURRET(8), 0x8d, -1);
    for (int32_t i = 0x12; i <= 0x15; i++) {                     // the power cells
        MobObj_Wake(i);
        Pod_Script_SetupHookPoint(MOB_ID(OT_POD, i), 0, 0x126, 0);
        Pod_Script_SetupDrag(MOB_ID(OT_POD, i), 0x1333);
    }
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x1b), PROP_TARGET, E2_ATURRET(8));
    Object_SetProperty(OBJ_ID(OT_TURRET, 0x37), PROP_TARGET, E2_ATURRET(2));
    Object_SetProperty(E2_ATURRET(2), PROP_TARGET, OBJ_ID(OT_TURRET, 0x37));
    Object_SetProperty(E2_ATURRET(2), PROP_TARGET, OBJ_ID(OT_DESTRUCTIBLE, 0x02));
    Object_SetProperty(E2_ATURRET(7), PROP_TARGET, OBJ_ID(OT_DESTRUCTIBLE, 0x04));
    AnimPlay(E2_ATURRET(2), 0x25, 0xe0);
    Object_SetProperty(E2_ATURRET(2), PROP_ARMOUR, 0xff);
    AnimSetUpdateInterval(E2_ATURRET(2), 0x32);
    AnimPlay(E2_ATURRET(7), 0x27, 0xe0);
    Object_SetProperty(E2_ATURRET(7), PROP_ARMOUR, 0xff);
    AnimSetUpdateInterval(E2_ATURRET(7), 0x64);
    AnimPlay(E2_ATURRET(8), 0x28, 0xe0);
    Object_SetProperty(E2_ATURRET(8), PROP_ARMOUR, 0xff);
    AnimSetUpdateInterval(E2_ATURRET(8), 0x32);
    AnimSetUpdateInterval(E2_ATURRET(8), 0x32);
    AnimPlay(E2_ANIM(0x6f), 2, 0x80);
    Sound_PlayOnce(E2_ANIM(0x6f), 0x3f8);
    AnimPlay(E2_ANIM(0x70), 2, 0x80);
    AnimPlay(E2_ANIM(0x71), 2, 0x80);
    AnimPlay(E2_ANIM(0x72), 2, 0x80);
    EventType_AddHandler(EV_TICK_1S, NULL, E2_OpeningTick);
    AnimPlay(E2_ANIM(0x49), 7, 0);
    AnimPlay(E2_ANIM(0x4a), 7, 0);
    for (int32_t i = 0x4b; i <= 0x50; i++)
        AnimPlay(E2_ANIM(i), 4, 0x81);
    for (int32_t i = 0x67; i <= 0x6c; i++)
        Sound_PlayLoop(OBJ_ID(OT_ANIMTURRET, i), 0x88, -1);
    Sound_PlayLoop(E2_ATURRET(0x4f), 0x91, -1);
    static const uint8_t emitters[10] = { 0x5e, 0x11, 0x5f, 0x12, 0x60, 0x13, 0x61, 0x14, 0x62, 0x15 };
    for (int i = 0; i < 10; i++)
        Object_SetDead(E2_EMITTER(emitters[i]));
    for (int32_t i = 0x22; i <= 0x25; i++)
        Object_SetDead(OBJ_ID(OT_ANIMTURRET, i));
    Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x83), 1);
    Object_SetDead(E2_PLANE(0x3c));
    Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x85), 1);
    Object_SetDead(E2_PLANE(0x3e));
    Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x86), 1);
    Object_SetDead(E2_PLANE(0x3f));
    MobObj_Wake(0x1f);
    MobObj_Wake(0x1d);
    MobObj_Wake(0x11);
    Environment_OverrideInteriorFog(0, 0, 0);
    Media_AudioPlay(0);
}

// 0x10003860  LevelStartLevel_Lander (export 0x10001010)
static void E2_StartLander(void)
{
    MobObj_Wake(E2_MOB_LANDER);
    MobObj_Wake(E2_MOB_CAMERA);
    Object_SetProperty(E2_CAMERA, PROP_TARGET, E2_LANDER);
    E2_Setup();
}

// 0x100038a0  LevelStartLevel_Flycam (export 0x10001020)
static void E2_StartFlycam(void)
{
    E2_Setup();
    MobObj_Wake(E2_MOB_FLYCAM);
}

// 0x10001040
static uint32_t E2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10002460, return 0. Port: fresh DLL data.
static int32_t E2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.artefacts = e2_artefactsInit;
    st.link03 = e2_link03Init;
    st.carried = e2_carriedInit;
    memcpy(st.slots, e2_slotsInit, sizeof st.slots);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000c190, .count = 3 * 3, .out = e2_text },
    { 0 },
};

const mission_script_t mission_europa_2 = {
    .name          = "EUROPA\\2",
    .get_version   = E2_GetVersion,
    .load          = E2_Load,
    .start_lander  = E2_StartLander,
    .start_flycam  = E2_StartFlycam,
    .start_animcam = NULL,
    .stop          = E2_Nothing,      // LevelStopLevel 0x10001030 -> 0x10002460
    .text          = e2_gametext,
};
