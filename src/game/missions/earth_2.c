// MISSIONS\EARTH\2\MISSIONR.DLL - faithful C port.
//
// Mission: reach the Renaissance dig site (waypoint 1), find the cache (pod mob 5, awake from the
// start), hook it and climb above y = 10000: +21500 cash, result 2 - or result 1 if J Marshall's
// tank (AnimTurret mob 0xf) was destroyed. Entering the big box 0x40027 plays an MCGA scene (anim
// objects, lights, gun ships 0x10e0000..2, then the 0x10e0014..18 turrets). After that the 1-s
// counter shows Marshall's mayday lines (G-text records 3..6) and adds waypoint 2: Marshall is
// pinned down by three turrets (0x2f..0x31). Destroy them and he fights on your side; shoot him
// and he turns on you. The objective line is a G-text window (records 0..12, GTEXT*.DAT) - the
// DLL's own string table (13 per language) is never read. Par time 15 s, 2 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040 (no Animcam export). The score library
// (0x10001050-0x100015f4) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define E2_LANDER       MOB_ID(OT_LANDER, 0xc)        // 0x0101000c
#define E2_CAMERA       MOB_ID(OT_CAMERA, 4)          // 0x01060004
#define E2_MOB_CAMERA   4
#define E2_MOB_POD      5                             // 0x010c0005 the cache
#define E2_MOB_FLYCAM   0xa
#define E2_MOB_LANDER   0xc
#define E2_MOB_MARSHALL 0xf
#define E2_MARSHALL     MOB_ID(OT_ANIMTURRET, 0xf)    // 0x010e000f J Marshall's tank
#define E2_ANIMTURRET(n) MOB_ID(OT_ANIMTURRET, (n))   // 0x010e00nn
#define E2_ANIMMOB(n)   MOB_ID(OT_ANIMOBJECT, (n))    // 0x010300nn
#define E2_LIGHTMOB(n)  MOB_ID(OT_LIGHTOBJECT, (n))   // 0x011600nn
#define E2_TURRET(n)    OBJ_ID(OT_TURRET, (n))        // 0x000000nn
#define E2_MCGA_FIRST   E2_TURRET(0x2f)               // 0x2f..0x31 the turrets attacking Marshall
#define E2_MCGA_LAST    E2_TURRET(0x31)
#define E2_TRIGGER(n)   OBJ_ID(OT_TRIGGERBOX, (n))    // 0x000400nn
#define E2_TARGET(n)    OBJ_ID(OT_DESTRUCTIBLE, (n))  // 0x000800nn
#define E2_EMITTER(n)   OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn
#define E2_ANIMDEST(n)  OBJ_ID(OT_ANIMDESTRUCTIBLE, (n)) // 0x000f00nn
#define E2_FUELDUMP     OBJ_ID(OT_FUELDUMP, 0x19)     // 0x00110019
#define E2_LIGHT(n)     OBJ_ID(OT_LIGHTOBJECT, (n))   // 0x001600nn
#define E2_PICKUP_CASH   OBJ_ID(OT_PICKUP, 0x13)      // 0x00130013 medium cash
#define E2_PICKUP_SHIELD MOB_ID(OT_PICKUP, 0x13)      // 0x01130013 +50 % shield

// G-text records (GTEXT*.DAT of the mission) shown in the objective window
enum {
    E2_REC_WAYPOINT1 = 0, E2_REC_FIND_CACHE = 1, E2_REC_CLIMB = 2,
    E2_REC_MAYDAY = 3, E2_REC_ATTACKED = 4, E2_REC_HEAR_ME = 5, E2_REC_WAYPOINT2 = 6,
    E2_REC_WOOHOO = 7, E2_REC_THANKS = 8, E2_REC_VARMINT = 9, E2_REC_GREAT_SHOT = 10,
    E2_REC_KICK_ASS = 11, E2_REC_DEATH = 12
};

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  e2_nbWaypoints1 = 1;            // 0x10007160
static const Waypoint e2_waypoints1[1] = {            // 0x10007168
    { -23168, -7936 },
};
static const int32_t  e2_nbWaypoints2 = 2;            // 0x10007170 (after the mayday)
static const Waypoint e2_waypoints2[2] = {            // 0x10007178
    { -23168, -7936 },
    { -7296, 30720 },
};
// 0x10007188: [language][13] string pointers - stored in 0x1000a550 by the set-up, never read
// (the objective window shows the same lines from GTEXT*.DAT). Entry 11 runs into the text of
// record 12 in the DLL (no terminator between them); entry 12 is NULL.
static const char *e2_text[3][13];
static const TurretType e2_turretType22 = {           // 0x10007228 -> gTurretTypes[22], [25]
    { 88, -9, 185, 0 }, 0x01, 1, 1, 0, 50, 0x40, 700, 14000, 0
};
static const TurretType e2_turretTypeWeak = {         // 0x10007248 -> [25] / [22] when 0xf0024 / 0xf0025 die
    { 88, -9, 185, 0 }, 0x01, 1, 1, 0, 100, 0x10, 700, 8192, 0
};
static const TurretType e2_turretType21 = {           // 0x10007268 -> gTurretTypes[21]
    { 2, 29, 288, 0 }, 0x05, 1, 4, 5, 100, 0x10, 800, 14000, 0
};
static const TurretType e2_turretType23 = {           // 0x10007288 -> gTurretTypes[23]
    { -60, 4, 258, 0 }, 0x01, 0, 1, 0, 80, 0x20, 800, 12000, 0
};
static const TurretType e2_turretType24 = {           // 0x100072a8 -> gTurretTypes[24]
    { 0, 0, 1, 0 }, 0x05, 0, 6, 2, 50, 0x30, 800, 12000, 0
};
static const TurretType e2_turretType0 = {            // 0x100072c8 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 1, 0, 80, 0x18, 1024, 12000, 0
};

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    Window      *win;           // 0x1000a0e0 objective window (G-text record)
    int32_t      marshallDead;  // 0x1000a0e4 -> result 1 instead of 2
    int32_t      mcgaKilled;    // 0x1000a0e8 turrets 0x2f..0x31 destroyed
    int32_t      record;        // 0x1000a0ec objective record restored after a radio line
    int32_t      seconds;       // 0x1000a0f0 shared by the mayday sequence and the flare timer
    int32_t      marshallHit;   // 0x1000a0f4 the player shot Marshall: he turns hostile
    int32_t      mcgaDone;      // 0x1000a0f8 all three turrets destroyed while he was friendly
    int32_t      deadA[2];      // 0x1000a100 turrets 0x21 / 0x22 (see E2_OnAnimTurnedRetarget)
    int32_t      deadB[3];      // 0x1000a108 turrets 0x2f..0x31
    const char *const *text;    // 0x1000a550 = e2_text[gLanguage] (never read)
} st;

// The original masks arg0 of some events in place (and $3ffffff), which later handlers of the
// same dispatch see. The dispatcher passes a writable copy, so the port does the same.
static ObjId E2_MaskArg0(const LanderEvent *ev)
{
    LanderEvent *w = (LanderEvent *)ev;
    w->arg[0] = (intptr_t)((uint32_t)w->arg[0] & 0x3ffffffu);
    return (ObjId)w->arg[0];
}

// 0x10001600  EV_TRIGGER_ENTER trigger 0x40027: the MCGA scene
static void E2_OnTriggerScene(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x27))
        return;
    MobObj_Wake(9);                 // AnimObject 0x01030009
    MobObj_Wake(0x11);              // AnimObject 0x01030011
    MobObj_Wake(0);                 // AnimTurret 0x010e0000
    MobObj_Wake(1);                 // AnimTurret 0x010e0001
    MobObj_Wake(2);                 // AnimTurret 0x010e0002
    MobObj_Wake(0x19);              // LightObject 0x01160019
    MobObj_Wake(0x12);              // LightObject 0x01160012
    MobObj_Wake(0xb);               // LightObject 0x0116000b
    AnimPlay(E2_ANIMMOB(9), 0, 0x80);
    AnimSetUpdateInterval(E2_ANIMMOB(9), 4);
    AnimSetUpdateInterval(E2_LIGHTMOB(0x19), 4);
    AnimSetUpdateInterval(E2_LIGHTMOB(0x12), 4);
    AnimSetUpdateInterval(E2_LIGHTMOB(0x0b), 4);
    AnimPlay(E2_LIGHTMOB(0x19), 0x2c, 0xe0);
    AnimPlay(E2_LIGHTMOB(0x12), 0x2a, 0xe0);
    AnimPlay(E2_LIGHTMOB(0x0b), 0x2b, 0xe0);
    InTime2(2500, Sound_PlayOnce, E2_ANIMMOB(9), 0x8b);
    AnimPlay(E2_ANIMMOB(0x11), 4, 0x80);
    AnimSetUpdateInterval(E2_ANIMMOB(0x11), 4);
    AnimPlay(E2_ANIMTURRET(0), 1, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(0), 4);
    AnimPlay(E2_ANIMTURRET(1), 2, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(1), 4);
    AnimPlay(E2_ANIMTURRET(2), 3, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(2), 4);
    InTime1(8000, MobObj_Wake, 0x14);
    InTime1(8000, MobObj_Wake, 0x15);
    InTime1(8000, MobObj_Wake, 0x16);
    InTime1(8000, MobObj_Wake, 0x17);
    InTime1(8000, MobObj_Wake, 0x18);
    InTime2(8000, Sound_PlayOnce, E2_ANIMMOB(9), 0x17d);
    InTime1(8200, MobObj_Wake, 0xd);
    InTime2(8200, Sound_PlayOnce, E2_ANIMMOB(0x11), 0x17c);
    InTime3(8200, Sound_PlayLoop, E2_ANIMMOB(9), 0x88, -1);
    InTime3(10000, Sound_PlayLoop, E2_ANIMMOB(9), 0xe3, -1);
    InTime1(15000, MobObj_Sleep, 0x19);
    InTime1(15000, MobObj_Sleep, 0x12);
    InTime1(15000, MobObj_Sleep, 0xb);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTriggerScene);
}

// 0x10001880  EV_ANIM_FINISHED with anim 4 (any object; never removed): the 0x10e0014..18
// turrets rise
static void E2_OnAnim4Finished(const LanderEvent *ev)
{
    if (ev->arg[1] != 4)
        return;
    for (int32_t id = (int32_t)E2_ANIMTURRET(0x14); id <= (int32_t)E2_ANIMTURRET(0x18); id++)
        Sound_PlayLoop((ObjId)id, 0x8d, -1);
    InTime3(2000, AnimPlay, E2_ANIMTURRET(0x14), 0x27, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(0x14), 0xf);
    InTime3(2500, AnimPlay, E2_ANIMTURRET(0x15), 0x28, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(0x15), 0xf);
    InTime3(168, AnimPlay, E2_ANIMTURRET(0x16), 9, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(0x16), 0xf);
    InTime3(1000, AnimPlay, E2_ANIMTURRET(0x17), 8, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(0x17), 0xf);
    InTime3(0, AnimPlay, E2_ANIMTURRET(0x18), 0xa, 0xe0);
    AnimSetUpdateInterval(E2_ANIMTURRET(0x18), 0xf);
}

// 0x10001980  EV_TICK_1S after the cache is hooked; never removed (LevelEnd stops the scheduler)
static void E2_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(E2_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    if (st.marshallDead == 1) {
        MissionScore_End(1);                                          // 0x100013a0
        Lander_SetCash(E2_LANDER, Lander_GetCash(E2_LANDER) + 21500);
    }
    if (st.marshallDead == 0) {
        MissionScore_End(2);
        Lander_SetCash(E2_LANDER, Lander_GetCash(E2_LANDER) + 21500);
    }
}

// 0x10001a10  EV_POD_HOOKED {pod, lander}; never removed: every re-hook adds another EvacTick
static void E2_OnPodHooked(const LanderEvent *ev)
{
    WindowSetTextFile(st.win, E2_REC_CLIMB);
    st.record = E2_REC_CLIMB;
    Pod_Script_SetupDynamicsCuboid((ObjId)ev->arg[0], 0xe6, 0xe6, 0xe6, 0x3e8);
    Pod_Script_SetupHookPoint((ObjId)ev->arg[0], 0, 0, 0);
    Pod_Script_SetupDrag((ObjId)ev->arg[0], 0x1333);
    EventType_AddHandler(EV_TICK_1S, NULL, E2_EvacTick);
}

// 0x10001a90  EV_TRIGGER_ENTER trigger 0x4001d / 0x4001e: the cache objective
static void E2_OnTriggerDigSite(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[1];
    if (id != E2_TRIGGER(0x1d) && id != E2_TRIGGER(0x1e))
        return;
    WindowSetTextFile(st.win, E2_REC_FIND_CACHE);
    st.record = E2_REC_FIND_CACHE;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTriggerDigSite);
}

// 0x10001ae0  EV_TRIGGER_ENTER trigger 0x40033: the digger 0xf0032 starts
static void E2_OnTriggerDigger(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x33))
        return;
    AnimPlay(E2_ANIMDEST(0x32), 7, 0xe0);
    Sound_PlayLoop(E2_ANIMDEST(0x32), 0x87, -1);
    AnimSetUpdateInterval(E2_ANIMDEST(0x32), 0x28);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTriggerDigger);
}

// 0x10001b40  EV_DESTROYED: the digger stops when destroyed
static void E2_OnDiggerDestroyed(const LanderEvent *ev)
{
    ObjId id = E2_MaskArg0(ev);
    if (id != E2_ANIMDEST(0x32))
        return;
    AnimStop(id, 0);
    EventType_RemoveHandler(EV_DESTROYED, E2_OnDiggerDestroyed);
}

// 0x10001b80  EV_DESTROYED 0xf0024 / 0xf0025: their turret slot gets the weaker record (never removed)
static void E2_OnRadarDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id == E2_ANIMDEST(0x24))
        gTurretTypes[25] = &e2_turretTypeWeak;
    else if (id == E2_ANIMDEST(0x25))
        gTurretTypes[22] = &e2_turretTypeWeak;
}

// 0x10001bc0  EV_ANIM_TURNED (added when the three turrets are down): retarget Marshall to the
// first of the turrets 0x21 / 0x22 not seen "destroyed". The original registers it for event 7
// although it is written like the EV_DESTROYED handler E2_OnMcgaDestroyedRetarget (its
// RemoveHandler uses event 8 and the object ids are turrets), so the flags are never set.
static void E2_OnAnimTurnedRetarget(const LanderEvent *ev)
{
    ObjId id = E2_MaskArg0(ev);
    if (st.marshallHit == 1)
        EventType_RemoveHandler(EV_ANIM_TURNED, E2_OnAnimTurnedRetarget);
    if (id == E2_TURRET(0x21))
        st.deadA[0] = 1;
    if ((ObjId)ev->arg[0] == E2_TURRET(0x22))
        st.deadA[1] = 1;
    (void)Object_GetProperty((ObjId)Object_GetProperty(E2_MARSHALL, PROP_TARGET), PROP_HITPOINTS);  // result unused
    for (int32_t i = 0; i < 2; i++) {
        if (st.deadA[i] == 0) {
            Object_SetProperty(E2_MARSHALL, PROP_TARGET, 0x21 + i);
            return;
        }
    }
    EventType_RemoveHandler(EV_DESTROYED, E2_OnAnimTurnedRetarget);
}

// 0x10001c70  InTime target: restore the objective record after a radio line
static void E2_RestoreObjective(void)
{
    WindowSetTextFile(st.win, st.record);
}

// 0x10001c90  EV_DESTROYED: count the turrets attacking Marshall
static void E2_OnMcgaDestroyed(const LanderEvent *ev)
{
    ObjId id = E2_MaskArg0(ev);
    if (id == E2_TURRET(0x2f))
        st.mcgaKilled++;
    if ((ObjId)ev->arg[0] == E2_TURRET(0x30))
        st.mcgaKilled++;
    if ((ObjId)ev->arg[0] == E2_TURRET(0x31))
        st.mcgaKilled++;
    if ((ObjId)ev->arg[1] == E2_LANDER && st.marshallHit == 0) {
        int32_t t = (int32_t)ev->arg[0];
        if (t >= (int32_t)E2_MCGA_FIRST && t <= (int32_t)E2_MCGA_LAST) {
            WindowSetTextFile(st.win, st.mcgaKilled == 1 ? E2_REC_THANKS : E2_REC_GREAT_SHOT);
            InTime0(3000, E2_RestoreObjective);
        }
    }
    if (st.mcgaKilled == 3 && st.marshallHit == 0) {
        Object_SetProperty(E2_MARSHALL, PROP_STATE, 0);
        AnimPlay(E2_MARSHALL, 0x29, 0xe0);
        EventType_AddHandler(EV_ANIM_TURNED, NULL, E2_OnAnimTurnedRetarget);
        AnimSetUpdateInterval(E2_MARSHALL, 8);
        InTime3(800, Object_SetProperty, E2_MARSHALL, PROP_STATE, 1);
        Object_SetProperty(E2_MARSHALL, PROP_TARGET, E2_TURRET(0x30));
        WindowSetTextFile(st.win, E2_REC_KICK_ASS);
        InTime0(3000, E2_RestoreObjective);
        EventType_RemoveHandler(EV_DESTROYED, E2_OnMcgaDestroyed);
        st.mcgaDone = 1;
    }
    if (st.mcgaKilled == 3 && st.marshallHit == 1)
        Object_SetProperty(E2_MARSHALL, PROP_TARGET, E2_LANDER);
}

// 0x10001e20  EV_PROJECTILE_HIT {target, damage, shooter, kind}: the player shot Marshall
static void E2_OnMarshallHit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_MARSHALL || (ObjId)ev->arg[2] != E2_LANDER || st.marshallHit != 0)
        return;
    if (st.mcgaDone == 0) {
        WindowSetTextFile(st.win, E2_REC_VARMINT);
        st.marshallHit = 1;
    } else {
        WindowSetTextFile(st.win, E2_REC_VARMINT);
        st.marshallHit = 1;
        Object_SetProperty(E2_MARSHALL, PROP_TARGET, E2_LANDER);
    }
    InTime0(3000, E2_RestoreObjective);
    EventType_RemoveHandler(EV_PROJECTILE_HIT, E2_OnMarshallHit);
}

// 0x10001ed0  EV_PROJECTILE_HIT: the player hits one of the turrets (never removed)
static void E2_OnMcgaHit(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[0];
    if (t < (int32_t)E2_MCGA_FIRST || t > (int32_t)E2_MCGA_LAST)
        return;
    if ((ObjId)ev->arg[2] != E2_LANDER || st.marshallHit != 0)
        return;
    WindowSetTextFile(st.win, E2_REC_WOOHOO);
    InTime0(3000, E2_RestoreObjective);
}

// 0x10001f20  EV_TICK_1S: Marshall's mayday at 10, 13, 16, 19 s; at 22 s back to the objective,
// arm the hit handlers and add waypoint 2
static void E2_MaydayTick(const LanderEvent *ev)
{
    (void)ev;
    switch (st.seconds) {
    case 10: WindowSetTextFile(st.win, E2_REC_MAYDAY); break;
    case 13: WindowSetTextFile(st.win, E2_REC_ATTACKED); break;
    case 16: WindowSetTextFile(st.win, E2_REC_HEAR_ME); break;
    case 19: WindowSetTextFile(st.win, E2_REC_WAYPOINT2); break;
    case 22:
        WindowSetTextFile(st.win, st.record);
        EventType_RemoveHandler(EV_TICK_1S, E2_MaydayTick);
        EventType_AddHandler(EV_PROJECTILE_HIT, NULL, E2_OnMarshallHit);
        EventType_AddHandler(EV_PROJECTILE_HIT, NULL, E2_OnMcgaHit);
        gNbWaypoints = e2_nbWaypoints2;
        gWaypoints = e2_waypoints2;
        break;
    default:
        break;
    }
    st.seconds++;
}

// 0x10002040  EV_TRIGGER_ENTER trigger 0x40023: Marshall and the turrets wake up
static void E2_OnTriggerBattle(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E2_TRIGGER(0x23))
        return;
    Object_SetProperty(E2_MARSHALL, PROP_STATE, 1);
    InTime3(600, Object_SetProperty, E2_TURRET(0x2f), PROP_STATE, 1);
    InTime3(700, Object_SetProperty, E2_TURRET(0x30), PROP_STATE, 1);
    InTime3(900, Object_SetProperty, E2_TURRET(0x31), PROP_STATE, 1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E2_OnTriggerBattle);
}

// 0x100020c0  EV_PROJECTILE_HIT on one of the turrets (any shooter): they all target the player
static void E2_OnMcgaProvoked(const LanderEvent *ev)
{
    ObjId t = (ObjId)ev->arg[0];
    if (t != E2_TURRET(0x2f) && t != E2_TURRET(0x30) && t != E2_TURRET(0x31))
        return;
    Object_SetProperty(E2_TURRET(0x2f), PROP_TARGET, E2_LANDER);
    Object_SetProperty(E2_TURRET(0x30), PROP_TARGET, E2_LANDER);
    Object_SetProperty(E2_TURRET(0x31), PROP_TARGET, E2_LANDER);
    EventType_RemoveHandler(EV_PROJECTILE_HIT, E2_OnMcgaProvoked);
}

// 0x10002120  EV_DESTROYED: Marshall targets the first turret still standing
static void E2_OnMcgaDestroyedRetarget(const LanderEvent *ev)
{
    ObjId id = E2_MaskArg0(ev);
    if (id == E2_TURRET(0x2f))
        st.deadB[0] = 1;
    if ((ObjId)ev->arg[0] == E2_TURRET(0x30))
        st.deadB[1] = 1;
    if ((ObjId)ev->arg[0] == E2_TURRET(0x31))
        st.deadB[2] = 1;
    (void)Object_GetProperty((ObjId)Object_GetProperty(E2_MARSHALL, PROP_TARGET), PROP_HITPOINTS);  // result unused
    for (int32_t i = 0; i < 3; i++) {
        if (st.deadB[i] == 0) {
            Object_SetProperty(E2_MARSHALL, PROP_TARGET, 0x2f + i);
            return;
        }
    }
    EventType_RemoveHandler(EV_DESTROYED, E2_OnMcgaDestroyedRetarget);
}

// 0x10002250  EV_DESTROYED Marshall (never removed): the turrets go for the player
static void E2_OnMarshallDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_MARSHALL)
        return;
    Object_SetProperty(E2_TURRET(0x2f), PROP_TARGET, E2_LANDER);
    Object_SetProperty(E2_TURRET(0x30), PROP_TARGET, E2_LANDER);
    Object_SetProperty(E2_TURRET(0x31), PROP_TARGET, E2_LANDER);
    WindowSetTextFile(st.win, E2_REC_DEATH);
    st.marshallDead = 1;
    InTime0(3000, E2_RestoreObjective);
}

// 0x100022d0  EV_TICK_1S (added by E2_OnTarget0dDestroyed, which resets the shared counter):
// the fires of 0x8000d die down one by one
static void E2_FlareTick(const LanderEvent *ev)
{
    (void)ev;
    st.seconds++;
    if (st.seconds == 60)
        Object_SetDead(E2_EMITTER(0x15));
    if (st.seconds == 80)
        Sound_StopObject(E2_EMITTER(0x15));
    if (st.seconds == 120)
        Object_SetDead(E2_EMITTER(0x28));
    if (st.seconds == 150)
        Object_SetDead(E2_EMITTER(0x29));
    if (st.seconds == 180)
        Object_SetDead(E2_EMITTER(0x2a));
    if (st.seconds == 220) {
        Object_SetDead(E2_EMITTER(0x2b));
        Sound_StopObject(E2_EMITTER(0x14));
        EventType_RemoveHandler(EV_TICK_1S, E2_FlareTick);
    }
}

// 0x10002380  EV_DESTROYED 0x8000d: fires and smoke
static void E2_OnTarget0dDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_TARGET(0x0d))
        return;
    InTime1(1000, Object_SetAlive, E2_EMITTER(0x14));
    InTime1(2000, Object_SetAlive, E2_EMITTER(0x15));
    InTime1(168, Object_SetAlive, E2_EMITTER(0x28));
    InTime1(2000, Object_SetAlive, E2_EMITTER(0x29));
    InTime1(3500, Object_SetAlive, E2_EMITTER(0x2a));
    InTime1(5000, Object_SetAlive, E2_EMITTER(0x2b));
    InTime3(5000, Sound_PlayLoop, E2_EMITTER(0x14), 0xf0, -1);
    InTime3(320, Sound_PlayLoop, E2_EMITTER(0x15), 0xf1, -1);
    st.seconds = 0;
    EventType_AddHandler(EV_TICK_1S, NULL, E2_FlareTick);
    EventType_RemoveHandler(EV_DESTROYED, E2_OnTarget0dDestroyed);
}

// 0x10002480  EV_DESTROYED 0x8000c: fire 0xd0016, smoke 0xd002c
static void E2_OnTarget0cDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_TARGET(0x0c))
        return;
    InTime1(1000, Object_SetAlive, E2_EMITTER(0x16));
    InTime1(2000, Object_SetAlive, E2_EMITTER(0x2c));
    InTime3(5000, Sound_PlayLoop, E2_EMITTER(0x16), 0xf0, -1);
    EventType_RemoveHandler(EV_DESTROYED, E2_OnTarget0cDestroyed);
}

// 0x10002500  EV_DESTROYED 0x8000f: smoke 0xd002e
static void E2_OnTarget0fDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_TARGET(0x0f))
        return;
    InTime1(1500, Object_SetAlive, E2_EMITTER(0x2e));
    EventType_RemoveHandler(EV_DESTROYED, E2_OnTarget0fDestroyed);
}

// 0x10002540  EV_DESTROYED 0x80010: smoke 0xd002d
static void E2_OnTarget10Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_TARGET(0x10))
        return;
    InTime1(1500, Object_SetAlive, E2_EMITTER(0x2d));
    EventType_RemoveHandler(EV_DESTROYED, E2_OnTarget10Destroyed);
}

// 0x10002580  EV_DESTROYED 0x80011: fire 0xd0017
static void E2_OnTarget11Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_TARGET(0x11))
        return;
    InTime1(1000, Object_SetAlive, E2_EMITTER(0x17));
    InTime3(5000, Sound_PlayLoop, E2_EMITTER(0x17), 0xf0, -1);
    EventType_RemoveHandler(EV_DESTROYED, E2_OnTarget11Destroyed);
}

// 0x100025e0  EV_DESTROYED 0x80012: fire 0xd0018
static void E2_OnTarget12Destroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_TARGET(0x12))
        return;
    InTime1(1000, Object_SetAlive, E2_EMITTER(0x18));
    InTime3(5000, Sound_PlayLoop, E2_EMITTER(0x18), 0xf0, -1);
    EventType_RemoveHandler(EV_DESTROYED, E2_OnTarget12Destroyed);
}

// 0x10002640  EV_DESTROYED fuel dump 0x110019: its light goes out
static void E2_OnFuelDumpDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E2_FUELDUMP)
        return;
    Object_SetDead(E2_LIGHT(0x1a));
    EventType_RemoveHandler(EV_DESTROYED, E2_OnFuelDumpDestroyed);
}

// 0x10002670  EV_PICKUP: +50 % shield, medium cash
static void E2_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == E2_PICKUP_SHIELD) {
        int32_t shield = (int32_t)Object_GetProperty(E2_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(E2_LANDER, 50);
        Object_SetProperty(E2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == E2_PICKUP_CASH) {
        Voice_CashAcquired(E2_LANDER);
        Lander_SetCash(E2_LANDER, Lander_GetCash(E2_LANDER) + gMediumCash);
    }
}

// 0x10002710  common set-up
static void E2_Setup(void)
{
    st.text = e2_text[gLanguage];
    for (int32_t id = (int32_t)E2_TURRET(0x2f); id <= (int32_t)E2_TURRET(0x31); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = (int32_t)E2_ANIMTURRET(0x14); id <= (int32_t)E2_ANIMTURRET(0x18); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = (int32_t)E2_ANIMTURRET(0); id <= (int32_t)E2_ANIMTURRET(2); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)E2_TURRET(0x1f); id <= (int32_t)E2_TURRET(0x22); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = (int32_t)E2_TURRET(0x1b); id <= (int32_t)E2_TURRET(0x1c); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)E2_TARGET(0x0e); id <= (int32_t)E2_TARGET(0x12); id++) {
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    }
    for (int32_t id = (int32_t)E2_TARGET(0x00); id <= (int32_t)E2_TARGET(0x0b); id++) {
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xf);
    }
    for (int32_t id = (int32_t)E2_ANIMDEST(0x24); id <= (int32_t)E2_ANIMDEST(0x26); id++)
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    Object_SetProperty(E2_ANIMDEST(0x32), PROP_EXPLOSION, 0x46);
    Object_SetProperty(E2_TARGET(0x0c), PROP_EXPLOSION, 0x50);
    Object_SetProperty(E2_TARGET(0x0c), PROP_ARMOUR, 0x1e);
    Object_SetProperty(E2_TARGET(0x0d), PROP_EXPLOSION, 0x50);
    Object_SetProperty(E2_TARGET(0x0d), PROP_ARMOUR, 0x1e);
    Object_SetProperty(E2_MARSHALL, PROP_ARMOUR, 0x3c);
    Object_SetProperty(E2_MARSHALL, PROP_EXPLOSION, 0x64);
    Object_SetProperty(E2_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(E2_FUELDUMP, PROP_EXPLOSION, 0x50);
    st.win = WindowCreate(0x408, NULL);
    WindowSetTextFile(st.win, E2_REC_WAYPOINT1);
    WindowSetSize(st.win, 20, 20, 500, 15);
    gNbWaypoints = e2_nbWaypoints1;
    gWaypoints = e2_waypoints1;
    MissionScore_Init(E2_LANDER, 15000, 2);                    // 0x10001520 (before the handlers)
    gTurretTypes[22] = &e2_turretType22;
    gTurretTypes[21] = &e2_turretType21;
    gTurretTypes[23] = &e2_turretType23;
    gTurretTypes[24] = &e2_turretType24;
    gTurretTypes[0] = &e2_turretType0;
    gTurretTypes[25] = &e2_turretType22;
    EventType_AddHandler(EV_TICK_1S, NULL, E2_MaydayTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerScene);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, E2_OnAnim4Finished);
    EventType_AddHandler(EV_POD_HOOKED, NULL, E2_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerDigSite);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerDigger);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnDiggerDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnRadarDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnMcgaDestroyed);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E2_OnTriggerBattle);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, E2_OnMcgaProvoked);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnMcgaDestroyedRetarget);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x100021c0
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnMarshallDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnTarget0dDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnFuelDumpDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnTarget0cDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnTarget0fDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnTarget10Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnTarget11Destroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, E2_OnTarget12Destroyed);
    EventType_AddHandler(EV_PICKUP, NULL, E2_OnPickup);
    SetGravity(0x13);
    gAirResistance = 0x23;
    gMainThrottlePower = 0x25;
    AnimPlay(E2_ANIMDEST(0x24), 6, 0x81);
    AnimPlay(E2_ANIMDEST(0x25), 6, 0x81);
    AnimPlay(E2_ANIMDEST(0x26), 6, 0x81);
    for (int32_t id = (int32_t)E2_ANIMDEST(0x24); id <= (int32_t)E2_ANIMDEST(0x26); id++)
        Sound_PlayLoop((ObjId)id, 0xd3, -1);
    for (int32_t id = (int32_t)E2_TURRET(0x2f); id <= (int32_t)E2_TURRET(0x31); id++)
        Sound_PlayLoop((ObjId)id, 0x8d, -1);
    MobObj_Wake(E2_MOB_MARSHALL);
    Sound_PlayLoop(E2_MARSHALL, 0x8a, -1);
    MobObj_Wake(E2_MOB_POD);
    Object_SetProperty(E2_TURRET(0x2f), PROP_TARGET, E2_MARSHALL);
    Object_SetProperty(E2_TURRET(0x30), PROP_TARGET, E2_MARSHALL);
    Object_SetProperty(E2_TURRET(0x31), PROP_TARGET, E2_MARSHALL);
    Object_SetProperty(E2_TURRET(0x2f), PROP_STATE, 0);
    Object_SetProperty(E2_TURRET(0x30), PROP_STATE, 0);
    Object_SetProperty(E2_TURRET(0x31), PROP_STATE, 0);
    Object_SetProperty(E2_MARSHALL, PROP_STATE, 0);
    Object_SetProperty(E2_MARSHALL, PROP_TARGET, E2_TURRET(0x2f));
    MobObj_Wake(0x13);              // Pickup 0x01130013
    Object_SetDead(E2_EMITTER(0x14));
    Object_SetDead(E2_EMITTER(0x15));
    Object_SetDead(E2_EMITTER(0x16));
    Object_SetDead(E2_EMITTER(0x17));
    Object_SetDead(E2_EMITTER(0x18));
    Object_SetDead(E2_EMITTER(0x28));
    Object_SetDead(E2_EMITTER(0x29));
    Object_SetDead(E2_EMITTER(0x2a));
    Object_SetDead(E2_EMITTER(0x2b));
    Object_SetDead(E2_EMITTER(0x2c));
    Object_SetDead(E2_EMITTER(0x2d));
    Object_SetDead(E2_EMITTER(0x2e));
    st.mcgaKilled = 0;
    Media_AudioPlay(0);
}

// 0x10002c30  LevelStartLevel_Lander (export 0x10001010)
static void E2_StartLander(void)
{
    MobObj_Wake(E2_MOB_LANDER);
    MobObj_Wake(E2_MOB_CAMERA);
    MobObj_Wake(0xe);               // AmbientFX
    MobObj_Wake(0x10);              // AmbientFX
    MobObj_Wake(6);                 // AmbientFX
    MobObj_Wake(7);                 // AmbientFX
    E2_Setup();
    Lander_SetHeadLight(E2_LANDER, 0x31c, 0xc00);
    Object_SetProperty(E2_CAMERA, PROP_TARGET, E2_LANDER);
}

// 0x10002ca0  LevelStartLevel_Flycam (export 0x10001020)
static void E2_StartFlycam(void)
{
    E2_Setup();
    MobObj_Wake(E2_MOB_FLYCAM);
}

// 0x10002cc0  LevelStopLevel (export 0x10001030): empty
static void E2_Stop(void)
{
}

// 0x10001040
static uint32_t E2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10002cc0, return 0. Port: fresh DLL data.
static int32_t E2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10007188, .count = 3 * 13, .out = e2_text },
    { 0 },
};

const mission_script_t mission_earth_2 = {
    .name          = "EARTH\\2",
    .get_version   = E2_GetVersion,
    .load          = E2_Load,
    .start_lander  = E2_StartLander,
    .start_flycam  = E2_StartFlycam,
    .start_animcam = NULL,
    .stop          = E2_Stop,
    .text          = e2_gametext,
};
