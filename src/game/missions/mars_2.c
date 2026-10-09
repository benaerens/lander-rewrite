// MISSIONS\MARS\2\MISSIONR.DLL - faithful C port.
//
// Mission: carry the bomb pod (mob 2, hooked to the lander from the start) into the installation
// at waypoint 1 and blow the generator. Entering the building (trigger 0x40082) -> "Find the
// generator."; the first door (trigger 0x4007a) stays shut until the lock mechanism (the four
// Destructibles 0x80033..36, they blow up in a chain) is destroyed: then the shaft lift 0x103000f
// runs and the doors work as proximity doors. The shield objective: destroy both shield
// units 0x80037/38 -> the lift runs again, the shield 0x3002f opens and the trigger 0x4001c now
// watches the pod. Dropping the pod into it releases it, the generator 0xf0015 blows 3 s later and
// the explosion mobs 5..11 go off; then climb above y = 10000: success 1 and +27500 cash.
// Par 165 s, 2 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030 (= the empty 0x10001050), LevelGetVersion 0x10001040. The score library
// (0x10001060-0x10001604, mercury_1's code) is in mission_lib.c.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define M2_LANDER        MOB_ID(OT_LANDER, 0xd)          // 0x0101000d
#define M2_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define M2_MOB_CAMERA    1
#define M2_MOB_POD       2
#define M2_MOB_LANDER    0xd
#define M2_POD           MOB_ID(OT_POD, 2)               // 0x010c0002 the bomb
#define M2_SHAFTLIFT     MOB_ID(OT_ANIMOBJECT, 0xf)      // 0x0103000f
#define M2_GENERATOR     OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x15) // 0x000f0015
#define M2_CRAWLER_A     MOB_ID(OT_ANIMDESTRUCTIBLE, 3)  // 0x010f0003 moving vehicles
#define M2_CRAWLER_B     MOB_ID(OT_ANIMDESTRUCTIBLE, 4)  // 0x010f0004
#define M2_CRAWLER_C     MOB_ID(OT_ANIMDESTRUCTIBLE, 0x12) // 0x010f0012
#define M2_ATURRET       MOB_ID(OT_ANIMTURRET, 0x10)     // 0x010e0010
#define M2_LOCK_FIRST    OBJ_ID(OT_DESTRUCTIBLE, 0x33)   // 0x00080033..36 door lock mechanism
#define M2_LOCK_LAST     OBJ_ID(OT_DESTRUCTIBLE, 0x36)
#define M2_SHIELD_A      OBJ_ID(OT_DESTRUCTIBLE, 0x37)   // 0x00080037 generator shield units
#define M2_SHIELD_B      OBJ_ID(OT_DESTRUCTIBLE, 0x38)   // 0x00080038
#define M2_PLANE         OBJ_ID(OT_TRIGGERPLANE, 0x81)   // 0x001a0081
#define M2_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define M2_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  m2_nbWaypoints = 1;               // 0x10007160
static const Waypoint m2_waypoints[1] = {               // 0x10007168
    { -5632, -6784 },
};
// 0x10007170: [language][6] string pointers (strings at 0x100071f4-0x1000748e)
static const char *m2_text[3][6];
static const TurretType m2_turretType13 = {             // 0x100071b8 -> gTurretTypes[13]
    { 2, 29, 288, 0 }, 0x05, 1, 4, 5, 120, 0x10, 800, 8192, 0
};
static const TurretType m2_turretType16 = {             // 0x100071d8 -> gTurretTypes[16]
    { 0, 0, 134, 0 }, 0x01, 0, 1, 0, 70, 0x20, 1024, 12000, 0
};

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    int32_t      crawlersDone;  // 0x10009bdc crawlers C and B back home
    Window      *win;           // 0x10009be0 objective line
    uint8_t      shieldsDown;   // 0x10009be4 shield units destroyed
    uint8_t      lockOpen;      // 0x10009be8 lock mechanism destroyed
    const char *const *text;    // 0x1000a028 = m2_text[gLanguage]
} st;

// 0x10001610  EV_TICK_1S after the bomb is placed
static void M2_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(M2_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        MissionScore_End(1);                                         // 0x100013b0
        EventType_RemoveHandler(EV_TICK_1S, M2_EvacTick);
        Lander_SetCash(M2_LANDER, Lander_GetCash(M2_LANDER) + 27500);
    }
}

// 0x10001670  EV_TRIGGER_ENTER trigger 0x4001c (target = the pod, set when the shields are down):
// release the bomb, the generator blows 3 s later, explosions, evac
static void M2_OnTriggerBomb(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M2_TRIGGER(0x1c))
        return;
    Object_SetProperty(M2_POD, PROP_TARGET, -1);
    WindowSetText(st.win, st.text[5]);       // the evacuation objective
    InTime2(3000, AnimStop, M2_GENERATOR, 1);
    InTime1(0x92e, Sound_StopObject, M2_GENERATOR);
    InTime3(3000, Object_SetProperty, M2_GENERATOR, PROP_HITPOINTS, 0);
    InTime1(3000, MobObj_Sleep, M2_MOB_POD);
    Sound_PlayOnce(M2_GENERATOR, 0x1f5);
    InTime1(3000, MobObj_Wake, 5);           // Explosion mobs 0x01090005..0b
    InTime1(0xd48, MobObj_Wake, 6);
    InTime1(0xd48, MobObj_Wake, 7);
    InTime1(0xd48, MobObj_Wake, 8);
    InTime1(0xd48, MobObj_Wake, 9);
    InTime1(0x1388, MobObj_Wake, 0xa);
    InTime1(0x1388, MobObj_Wake, 0xb);
    InTime3(3000, AnimPlay, M2_GENERATOR, 0xe, 0);
    InTime2(3000, Sound_PlayOnce, M2_GENERATOR, 0xf5);
    InTime2(0xc1c, Sound_PlayOnce, M2_GENERATOR, 0xf5);
    InTime2(0xdac, Sound_PlayOnce, M2_GENERATOR, 0xf5);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M2_OnTriggerBomb);
    EventType_AddHandler(EV_TICK_1S, NULL, M2_EvacTick);
}

// 0x10001820  EV_DESTROYED shield units 0x80037/38: both gone -> lift, shield 0x3002f opens,
// the trigger 0x4001c watches the pod. Never removed.
static void M2_OnDestroyedShield(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != M2_SHIELD_A && id != M2_SHIELD_B)
        return;
    if (++st.shieldsDown != 2)
        return;
    AnimPlay(M2_SHAFTLIFT, 2, 4);
    Sound_PlayOnce(M2_SHAFTLIFT, 0xce);
    InTime3(0x348, Sound_PlayLoop, M2_SHAFTLIFT, 0xcf, 0x11);
    InTime2(0xbea, Sound_PlayOnce, M2_SHAFTLIFT, 0xd0);
    AnimPlay(M2_ANIM(0x2f), 0xc, 0x80);
    Sound_PlayOnce(M2_ANIM(0x2f), 0x1f6);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M2_OnTriggerBomb);
    Object_SetProperty(M2_TRIGGER(0x1c), PROP_TARGET, M2_POD);
    WindowSetText(st.win, st.text[4]);       // the blow-up objective
}

// 0x10001920  door pair 0x30018 + 2i / 0x30019 + 2i (anim 0xa)
static void M2_DoorPair18(int32_t i, int32_t flags)
{
    ObjId a = (ObjId)(0x30018 + 2 * i);
    AnimPlay(a, 0xa, flags);
    AnimPlay((ObjId)(0x30019 + 2 * i), 0xa, flags);
    if (flags == 0 || flags == 4)
        Sound_PlayOnce(a, 0xab);
}

// 0x10001970  EV_TRIGGER_ENTER/LEAVE triggers 0x40011/12: proximity doors
static void M2_OnTriggerDoor18(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig != (int32_t)M2_TRIGGER(0x11) && trig != (int32_t)M2_TRIGGER(0x12))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        M2_DoorPair18(trig - (int32_t)M2_TRIGGER(0x11), 0);
    else
        M2_DoorPair18(trig - (int32_t)M2_TRIGGER(0x11), 4);
}

// 0x100019b0  door pair 0x3004a + 2i / 0x3004b + 2i (anim 0xb)
static void M2_DoorPair4A(int32_t i, int32_t flags)
{
    ObjId a = (ObjId)(0x3004a + 2 * i);
    AnimPlay(a, 0xb, flags);
    AnimPlay((ObjId)(0x3004b + 2 * i), 0xb, flags);
    if (flags == 0 || flags == 4)
        Sound_PlayOnce(a, 0xaa);
}

// 0x10001a00  EV_TRIGGER_ENTER/LEAVE triggers 0x40013/14: proximity doors
static void M2_OnTriggerDoor4A(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig != (int32_t)M2_TRIGGER(0x13) && trig != (int32_t)M2_TRIGGER(0x14))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        M2_DoorPair4A(trig - (int32_t)M2_TRIGGER(0x13), 0);
    else
        M2_DoorPair4A(trig - (int32_t)M2_TRIGGER(0x13), 4);
}

// 0x10001a40  door i: 0x30021 + 2i / 0x30022 + 2i (anim 6) and 0x30027 + i (anim 5)
static void M2_DoorTriple(int32_t i, int32_t flags)
{
    ObjId a = (ObjId)(0x30021 + 2 * i);
    AnimPlay(a, 6, flags);
    AnimPlay((ObjId)(0x30022 + 2 * i), 6, flags);
    AnimPlay((ObjId)(0x30027 + i), 5, flags);
    Sound_PlayOnce(a, 0xab);
}

// 0x10001aa0  EV_TRIGGER_ENTER/LEAVE triggers 0x4007a..7c: proximity doors; the first one stays
// shut and shows the lock hint until the lock mechanism is destroyed
static void M2_OnTriggerDoors21(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)M2_TRIGGER(0x7a) || trig > (int32_t)M2_TRIGGER(0x7c))
        return;
    int32_t i = trig - (int32_t)M2_TRIGGER(0x7a);
    if (ev->type == EV_TRIGGER_ENTER) {
        if (trig == (int32_t)M2_TRIGGER(0x7a) && !st.lockOpen)
            WindowSetText(st.win, st.text[2]);   // the door lock hint
        else
            M2_DoorTriple(i, 0);
    } else {
        if (trig == (int32_t)M2_TRIGGER(0x7a) && !st.lockOpen)
            WindowSetText(st.win, st.text[1]);   // the generator objective
        else
            M2_DoorTriple(i, 4);
    }
}

// 0x10001b40  EV_DESTROYED any part of the lock 0x80033..36: lift down, "Disable the generator
// shields." Never removed (each later part replays it).
static void M2_OnDestroyedLock(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)M2_LOCK_FIRST || id > (int32_t)M2_LOCK_LAST)
        return;
    AnimPlay(M2_SHAFTLIFT, 2, 0);
    Sound_PlayOnce(M2_SHAFTLIFT, 0xce);
    InTime3(0x348, Sound_PlayLoop, M2_SHAFTLIFT, 0xcf, 0x11);
    InTime2(0xbea, Sound_PlayOnce, M2_SHAFTLIFT, 0xd0);
    st.lockOpen = 1;
    WindowSetText(st.win, st.text[3]);       // the shield objective
}

// 0x10001bf0  EV_TRIGGER_ENTER trigger 0x40082 (building entrance): the generator objective
static void M2_OnTriggerEntrance(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M2_TRIGGER(0x82))
        return;
    WindowSetText(st.win, st.text[1]);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M2_OnTriggerEntrance);
}

// 0x10001c30  EV_DESTROYED one part of the lock: the four blow up one after the other
static void M2_OnDestroyedLockChain(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != OBJ_ID(OT_DESTRUCTIBLE, 0x33) && id != OBJ_ID(OT_DESTRUCTIBLE, 0x34) &&
        id != OBJ_ID(OT_DESTRUCTIBLE, 0x35) && id != OBJ_ID(OT_DESTRUCTIBLE, 0x36))
        return;
    InTime3(1000, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x33), PROP_HITPOINTS, 0);
    InTime3(0x44c, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x34), PROP_HITPOINTS, 0);
    InTime3(0x4b0, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x35), PROP_HITPOINTS, 0);
    InTime3(0x514, Object_SetProperty, OBJ_ID(OT_DESTRUCTIBLE, 0x36), PROP_HITPOINTS, 0);
    EventType_RemoveHandler(EV_DESTROYED, M2_OnDestroyedLockChain);
}

// 0x10001cd0 = MissionLib_OnDestroyedSound (same thresholds)

// 0x10001d60  EV_PICKUP 0x130048/49: +25 % shield
static void M2_OnPickupShield(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)OBJ_ID(OT_PICKUP, 0x48) || id > (int32_t)OBJ_ID(OT_PICKUP, 0x49))
        return;
    int32_t shield = (int32_t)Object_GetProperty(M2_LANDER, PROP_HITPOINTS) + 0x40;
    Voice_ShieldAcquired(M2_LANDER, 25);
    Object_SetProperty(M2_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
}

// 0x10001dd0  EV_ANIM_FINISHED of the crawlers: A restarts 10 s later, B and C together 5 s
// after both are back
static void M2_OnCrawlerDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == M2_CRAWLER_C)
        st.crawlersDone++;
    if ((ObjId)ev->arg[0] == M2_CRAWLER_A)
        InTime3(10000, AnimPlay, M2_CRAWLER_A, 0xf, 0x80);
    if ((ObjId)ev->arg[0] == M2_CRAWLER_B)
        st.crawlersDone++;
    if (st.crawlersDone == 2) {
        st.crawlersDone = 0;
        InTime3(5000, AnimPlay, M2_CRAWLER_C, 0x10, 0x80);
        InTime3(5000, AnimPlay, M2_CRAWLER_B, 0x11, 0x80);
    }
}

// 0x10001e70  EV_TRIGGER_PLANE 0x1a0081 {player, plane, side}: side 1 puts the mobs 0x12, 4, 3
// (crawlers) to sleep, the other side wakes them
static void M2_OnTriggerPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M2_PLANE)
        return;
    void (*fn)(int32_t) = (ev->arg[2] == 1) ? MobObj_Sleep : MobObj_Wake;
    fn(0x12);
    fn(4);
    fn(3);
}

// 0x10001eb0  EV_PICKUP 0x130039/3a: medium cash
static void M2_OnPickupCash(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)OBJ_ID(OT_PICKUP, 0x39) || id > (int32_t)OBJ_ID(OT_PICKUP, 0x3a))
        return;
    Lander_SetCash(M2_LANDER, Lander_GetCash(M2_LANDER) + gMediumCash);
    Voice_CashAcquired(M2_LANDER);
}

// 0x10001f00  common set-up (also the whole of LevelStartLevel_Flycam)
static void M2_Setup(void)
{
    static const struct { uint32_t id; int32_t size; } explosions[7] = {   // Explosion mobs
        { 0x1090005, 0x64 }, { 0x1090006, 0x96 }, { 0x1090007, 0x96 }, { 0x1090008, 0xaa },
        { 0x1090009, 0x64 }, { 0x109000a, 0xc8 }, { 0x109000b, 0x64 },
    };
    st.text = m2_text[gLanguage];
    for (int32_t id = 0; id <= 0xe; id++)                        // turrets 0..14
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)M2_LOCK_FIRST; id <= (int32_t)M2_LOCK_LAST; id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xa);
    for (int32_t id = (int32_t)OBJ_ID(OT_FUELDUMP, 0x2d); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0x2e); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xa);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(M2_ATURRET, PROP_ARMOUR, 0x23);
    Object_SetProperty(M2_CRAWLER_C, PROP_ARMOUR, 0x14);
    Object_SetProperty(M2_CRAWLER_C, PROP_EXPLOSION, 0x46);
    Object_SetProperty(M2_CRAWLER_A, PROP_ARMOUR, 0x19);
    Object_SetProperty(M2_CRAWLER_A, PROP_EXPLOSION, 0x4b);
    Object_SetProperty(M2_CRAWLER_B, PROP_ARMOUR, 0xf);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x1d), PROP_ARMOUR, 0x19);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x1d), PROP_EXPLOSION, 0x4b);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x1e), PROP_ARMOUR, 0x19);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x1e), PROP_EXPLOSION, 0x4b);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x1f), PROP_ARMOUR, 0xf);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x20), PROP_ARMOUR, 0xf);
    for (int i = 0; i < 7; i++)
        Object_SetProperty(explosions[i].id, PROP_EXPLOSION, explosions[i].size);
    for (int32_t id = (int32_t)M2_LOCK_FIRST; id <= (int32_t)M2_LOCK_LAST; id++)   // unrolled
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x3c);
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x23;
    gNbWaypoints = m2_nbWaypoints;
    gWaypoints = m2_waypoints;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    gTurretTypes[13] = &m2_turretType13;
    gTurretTypes[16] = &m2_turretType16;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M2_OnTriggerDoors21);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M2_OnTriggerDoors21);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M2_OnTriggerDoor18);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M2_OnTriggerDoor18);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M2_OnTriggerDoor4A);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, M2_OnTriggerDoor4A);
    EventType_AddHandler(EV_DESTROYED, NULL, M2_OnDestroyedLock);
    EventType_AddHandler(EV_DESTROYED, NULL, M2_OnDestroyedShield);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10001cd0
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M2_OnTriggerEntrance);
    EventType_AddHandler(EV_DESTROYED, NULL, M2_OnDestroyedLockChain);
    EventType_AddHandler(EV_PICKUP, NULL, M2_OnPickupShield);
    EventType_AddHandler(EV_PICKUP, NULL, M2_OnPickupCash);
    for (int32_t i = 0x3b; i <= 0x43; i++)
        AnimPlay(M2_ANIM(i), 4, 0x81);
    AnimPlay(M2_ANIM(0x45), 3, 0x81);
    AnimPlay(M2_ANIM(0x46), 3, 0x81);
    AnimPlay(M2_GENERATOR, 9, 0x81);
    AnimPlay(M2_ANIM(0x2a), 7, 0x81);
    AnimPlay(M2_ANIM(0x2b), 8, 0x81);
    AnimPlay(M2_ANIM(0x2c), 8, 0x81);
    AnimPlay(M2_ATURRET, 0xd, 0x61);
    for (int32_t i = 0x3b; i <= 0x43; i++)
        Sound_PlayLoop(M2_ANIM(i), 0xd3, -1);
    for (int32_t i = 0x44; i <= 0x46; i++)
        Sound_PlayLoop(M2_ANIM(i), 0xd2, -1);
    for (int32_t i = 0x4e; i <= 0x63; i++)                      // particle emitters 0xd004e..63
        Sound_PlayLoop(OBJ_ID(OT_PARTICLEEMITTER, i), 0xd2, -1);
    for (int32_t i = 0x3b; i <= 0x43; i++)
        AnimSetUpdateInterval(M2_ANIM(i), 0x28);
    AnimSetUpdateInterval(M2_ANIM(0x45), 0x28);
    AnimSetUpdateInterval(M2_ANIM(0x46), 0x28);
    AnimSetUpdateInterval(M2_GENERATOR, 0xc);
    Sound_PlayLoop(M2_ANIM(0x2b), 0xd6, -1);
    Sound_PlayLoop(M2_ANIM(0x2c), 0xd6, -1);
    Sound_PlayLoop(M2_ANIM(0x2a), 0xb9, -1);
    Sound_PlayLoop(M2_GENERATOR, 0x1f4, -1);
    Sound_PlayLoop(M2_ATURRET, 0x92, -1);
    MobObj_Wake(0x11);              // AmbientFX
    MobObj_Wake(0xe);               // AnimObject 0x0103000e
    MobObj_Wake(0xf);               // shaft lift 0x0103000f
    MobObj_Wake(0x10);              // AnimTurret 0x010e0010
    MobObj_Wake(M2_MOB_POD);
    MobObj_Wake(3);                 // crawlers
    MobObj_Wake(0x12);
    MobObj_Wake(4);
    AnimSetUpdateInterval(M2_CRAWLER_C, 0x28);
    AnimSetUpdateInterval(M2_CRAWLER_A, 0x28);
    AnimSetUpdateInterval(M2_CRAWLER_B, 0x28);
    Sound_PlayLoop(M2_CRAWLER_C, 0x84, -1);
    Sound_PlayLoop(M2_CRAWLER_A, 0x87, -1);
    Sound_PlayLoop(M2_CRAWLER_B, 0x82, -1);
    // a second loop on most of the machines (0x30043 and 0x30044 differ from the first set)
    for (int32_t i = 0x3b; i <= 0x42; i++)
        Sound_PlayLoop(M2_ANIM(i), 0xd3, -1);
    Sound_PlayLoop(M2_ANIM(0x44), 0xd3, -1);
    Sound_PlayLoop(M2_ANIM(0x45), 0xd3, -1);
    Sound_PlayLoop(M2_ANIM(0x46), 0xd3, -1);
    AnimPlay(M2_CRAWLER_A, 0xf, 0);
    AnimPlay(M2_CRAWLER_C, 0x10, 0);
    AnimPlay(M2_CRAWLER_B, 0x11, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M2_OnCrawlerDone);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, M2_OnTriggerPlane);
    Object_SetProperty(M2_POD, PROP_TARGET, M2_LANDER);         // the bomb starts hooked
    Pod_Script_SetupHookPoint(M2_POD, 0, 0xc0, 0);
    Pod_Script_SetupMass(M2_POD, 0x5dc);
    Pod_Script_SetupDrag(M2_POD, 0x1333);
    Media_AudioPlay(0);
    MissionScore_Init(M2_LANDER, 165000, 2);                      // 0x10001530
}

// 0x100026f0  LevelStartLevel_Lander (export 0x10001010)
static void M2_StartLander(void)
{
    MobObj_Wake(M2_MOB_LANDER);
    MobObj_Wake(M2_MOB_CAMERA);
    Object_SetProperty(M2_CAMERA, PROP_TARGET, M2_LANDER);
    M2_Setup();
}

// 0x10002730  LevelStartLevel_Flycam (export 0x10001020): jmp to the set-up; the flycam mob
// 0xc is never woken
static void M2_StartFlycam(void)
{
    M2_Setup();
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void M2_Stop(void)
{
}

// 0x10001040
static uint32_t M2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001050, return 0. Port: fresh DLL data.
static int32_t M2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m2_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10007170, .count = 3 * 6, .out = m2_text },
    { 0 },
};

const mission_script_t mission_mars_2 = {
    .name          = "MARS\\2",
    .get_version   = M2_GetVersion,
    .load          = M2_Load,
    .start_lander  = M2_StartLander,
    .start_flycam  = M2_StartFlycam,
    .start_animcam = NULL,
    .stop          = M2_Stop,
    .text          = m2_gametext,
};
