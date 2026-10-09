// MISSIONS\EARTH\4\MISSIONR.DLL - faithful C port.
//
// Mission: find the dig site (waypoint 1), get into the underground workings (box 0x4002a:
// Dr. Mishima's capsule, pod mob 0x23, wakes up and fifteen AnimTurret mobs start their AnimSeq
// patrol loops) and hook the capsule. The hook starts a 60-s fuse ("Time remaining" from 55 s).
// Climb above y = 50 (the climb objective), then above y = 10000: +24000 cash, result 1 if
// the bomb 0xf0078 was armed (both 0xf0025 and 0xf0016 standing at the hook), result 2 if it
// was not or got destroyed by them (box 0x4006f). When the fuse runs out the screen whites out
// and the lander dies 3 s later (LevelEnd 0) unless it is inside the safe box 0x40069. Shooting
// one of the 30 support pillars 0xf000a..0xf0027 sets off a chain reaction (E4_Chain).
// Par time 180 s, 4 pickups.
//
// Addresses are the DLL's (image base 0x10000000). This DLL has a different layout: AnimSeq
// (0x10001000-0x100011f1, mission_lib.c's code with AnimSeq_Stop 0x10001190, which nothing here
// calls, and Init 0x100011c0) comes first, then the exports: LevelLoadLevel 0x10001200, LevelStartLevel_Lander
// 0x10001210, LevelStartLevel_Flycam 0x10001230, LevelStopLevel 0x10001240, LevelGetVersion
// 0x10001250; 0x10001220 jumps to an unexported anim-cam start 0x10002e70 (set-up, MobObj_Wake(0),
// AnimPlay(0x1050000, 0, 0xc1)) that the engine never calls. Also unreferenced: an AbortLevel
// thunk 0x10001260 and 0x10001290 (LevelEnd(last score result)). The score library
// (0x100012a0-0x10001844) is in mission_lib.c.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define E4_LANDER       MOB_ID(OT_LANDER, 0x1e)       // 0x0101001e
#define E4_CAMERA       MOB_ID(OT_CAMERA, 1)          // 0x01060001
#define E4_POD          MOB_ID(OT_POD, 0x23)          // 0x010c0023 Dr. Mishima's capsule
#define E4_SHAKE        MOB_ID(OT_INERTIALFRAME, 0x1d) // 0x0114001d the explosion tremor
#define E4_MOB_CAMERA   1
#define E4_MOB_FLYCAM   0x1c
#define E4_MOB_SHAKE    0x1d
#define E4_MOB_LANDER   0x1e
#define E4_MOB_SKY_A    0x20                          // AmbientFX, off underground
#define E4_MOB_POD      0x23
#define E4_MOB_SKY_B    0x24                          // AmbientFX, off underground
#define E4_ANIMTURRET(n) MOB_ID(OT_ANIMTURRET, (n))   // 0x010e00nn
#define E4_TURRET(n)    OBJ_ID(OT_TURRET, (n))        // 0x000000nn
#define E4_ANIM(n)      OBJ_ID(OT_ANIMOBJECT, (n))    // 0x000300nn
#define E4_TRIGGER(n)   OBJ_ID(OT_TRIGGERBOX, (n))    // 0x000400nn
#define E4_TARGET(n)    OBJ_ID(OT_DESTRUCTIBLE, (n))  // 0x000800nn
#define E4_SWITCH(n)    OBJ_ID(OT_SWITCH, (n))        // 0x000a00nn
#define E4_ANIMDEST(n)  OBJ_ID(OT_ANIMDESTRUCTIBLE, (n)) // 0x000f00nn
#define E4_PILLAR_A(i)  E4_ANIMDEST(0x0a + (i))       // 0x000f000a..18 (i = 0..14)
#define E4_PILLAR_B(i)  E4_ANIMDEST(0x19 + (i))       // 0x000f0019..27
#define E4_BOMB         E4_ANIMDEST(0x78)             // 0x000f0078
#define E4_FUELDUMP     OBJ_ID(OT_FUELDUMP, 0x2c)     // 0x0011002c
#define E4_PICKUP(n)    OBJ_ID(OT_PICKUP, (n))        // 0x001300nn
#define E4_PLANE(n)     OBJ_ID(OT_TRIGGERPLANE, (n))  // 0x001a00nn

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  e4_countdownStart = 60;         // 0x10010160 initial value of the fuse
static const int32_t  e4_nbWaypoints = 1;             // 0x10010164
static const Waypoint e4_waypoints[1] = {             // 0x10010168
    { 56320, 6400 },
};
static const TurretType e4_turretType2 = {            // 0x10010170 -> gTurretTypes[2]
    { 0, 0, 396, 0 }, 0x00, 0, 1, 0, 85, 0x10, 800, 12000, 0
};
static const TurretType e4_turretType5 = {            // 0x10010190 -> gTurretTypes[5]
    { 85, 434, 461, 0 }, 0x02, 2, 1, 0, 80, 0x10, 512, 8192, 0
};
// 0x100101b0: [language][11] string pointers (German shares entries 7, 8 with English)
static const char *e4_text[3][11];
static const TurretType e4_turretType8 = {            // 0x10010238 -> gTurretTypes[8]
    { 0, 0, 544, 0 }, 0x00, 0, 1, 0, 100, 0x10, 800, 12000, 0
};
// AnimSeq sequences 0x10010258..0x10010447: 3-byte steps {anim, repeat, AnimPlay flags}, 0xff ends.
// Writable like the DLL's .data (AnimSeq takes uint8_t *); this mission never patches them.
static uint8_t e4_seq258[] = { 0x2b,0x0d,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x0d,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq278[] = { 0x2c,0x01,0x60, 0x2b,0x07,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x07,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq298[] = { 0x2b,0x09,0x60, 0x25,0x01,0x60, 0x24,0x01,0x60, 0x23,0x01,0x60, 0x17,0x01,0x60, 0x2b,0x07,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq2b8[] = { 0x2b,0x08,0x60, 0x1b,0x01,0x60, 0x1a,0x01,0x60, 0x2b,0x03,0x60, 0x20,0x01,0x60, 0x2b,0x04,0x60, 0x29,0x01,0x60, 0x27,0x01,0x60, 0x2b,0x08,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq2e0[] = { 0x2b,0x0b,0x60, 0x26,0x01,0x60, 0x2b,0x03,0x60, 0x20,0x01,0x60, 0x2b,0x04,0x60, 0x1c,0x01,0x60, 0x2b,0x0b,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq308[] = { 0x2b,0x09,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x09,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq328[] = { 0x2b,0x04,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x04,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq348[] = { 0x2b,0x03,0x60, 0x22,0x01,0x60, 0x2b,0x03,0x60, 0x2a,0x01,0x60, 0x1d,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x03,0x60, 0x17,0x01,0x60, 0x2b,0x03,0x60, 0x2a,0x01,0x60, 0x1d,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq370[] = { 0x2b,0x01,0x60, 0x17,0x01,0x60, 0x2b,0x05,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x05,0x60, 0x22,0x01,0x60, 0x2b,0x01,0x60, 0x2a,0x01,0x60, 0x1d,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq398[] = { 0x2b,0x0b,0x60, 0x17,0x01,0x60, 0x2b,0x02,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x02,0x60, 0x22,0x01,0x60, 0x2b,0x0b,0x60, 0x2a,0x01,0x60, 0x1d,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq3c0[] = { 0x2b,0x08,0x60, 0x22,0x01,0x60, 0x2b,0x02,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x02,0x60, 0x17,0x01,0x60, 0x2b,0x08,0x60, 0x2a,0x01,0x60, 0x1d,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq3e8[] = { 0x2b,0x04,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x04,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static uint8_t e4_seq408[] = { 0x2b,0x02,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x06,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0x2b,0x04,0x60, 0xff,0,0 };
static uint8_t e4_seq428[] = { 0x2b,0x07,0x60, 0x25,0x01,0x60, 0x24,0x01,0x60, 0x23,0x01,0x60, 0x17,0x01,0x60, 0x2b,0x05,0x60, 0x2a,0x01,0x60, 0x13,0x01,0x60, 0x2c,0x01,0x60, 0xff,0,0 };
static const char *e4_fmtTime;                    // 0x100108bc the fuse line's format (not translated)

// ---- DLL globals (.data / .bss) -----------------------------------------------------------------
static struct {
    int32_t      countdown;     // 0x10010160 (.data 60) fuse seconds
    int32_t      doorLock;      // 0x10013d58 never written: the door-closing handler never acts
    int32_t      bombArmed;     // 0x10013d5c 1 = result 1, 0 = result 2
    int32_t      inShelter;     // 0x10013d60 inside box 0x40068
    int32_t      safe;          // 0x10013d64 inside box 0x40069 (or still in 0x40068)
    int32_t      deathTicks;    // 0x10013d68 seconds since the white-out
    Window      *win;           // 0x10013d6c objective line
    int32_t      chain[4][3];   // 0x10013d70 pillar groups already set off
    char         timeText[256]; // 0x10014340 sprintf buffer of the countdown line
    const char *const *text;    // 0x10014440 = e4_text[gLanguage]
} st;

// 0x10001850  EV_TRIGGER_ENTER trigger 0x4004d: doors 0x3004b / 0x3004c open
static void E4_OnTriggerDoorsOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E4_TRIGGER(0x4d))
        return;
    AnimPlay(E4_ANIM(0x4b), 3, 0);
    Sound_PlayOnce(E4_ANIM(0x4b), 0xab);
    AnimPlay(E4_ANIM(0x4c), 3, 0);
}

// 0x100018a0  EV_TRIGGER_LEAVE trigger 0x4004d: close the doors for good - only if 0x10013d58 is
// 1, which nothing sets
static void E4_OnTriggerDoorsClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E4_TRIGGER(0x4d) || st.doorLock != 1)
        return;
    AnimPlay(E4_ANIM(0x4b), 3, 4);
    Sound_PlayOnce(E4_ANIM(0x4b), 0xab);
    AnimPlay(E4_ANIM(0x4c), 3, 4);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E4_OnTriggerDoorsOpen);
}

// 0x10001900  EV_TRIGGER_ENTER trigger 0x40003: the lift 0x30001 / 0x30002
static void E4_OnTriggerLift(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E4_TRIGGER(3))
        return;
    AnimPlay(E4_ANIM(1), 4, 0);
    Sound_PlayChain(E4_ANIM(1), 9, 0x1b8, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa5);
    AnimPlay(E4_ANIM(2), 5, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E4_OnTriggerLift);
}

// 0x10001980  EV_SWITCH 0xa0072: doors 0x3006a / 0x3006b follow the switch
static void E4_OnSwitch72(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E4_SWITCH(0x72))
        return;
    int32_t flags = ev->arg[1] == 1 ? 0 : 4;
    AnimPlay(E4_ANIM(0x6a), 6, flags);
    Sound_PlayOnce(E4_ANIM(0x6a), 0xaa);
    AnimPlay(E4_ANIM(0x6b), 6, flags);
}

// 0x10001a10  EV_SWITCH 0xa0073: doors 0x3006c / 0x3006d follow the switch
static void E4_OnSwitch73(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E4_SWITCH(0x73))
        return;
    int32_t flags = ev->arg[1] == 1 ? 0 : 4;
    AnimPlay(E4_ANIM(0x6c), 6, flags);
    Sound_PlayOnce(E4_ANIM(0x6c), 0xaa);
    AnimPlay(E4_ANIM(0x6d), 6, flags);
}

// 0x10001aa0  EV_SWITCH 0xa0074: roof 0x30070 / 0x30071; the sky mobs come back when it opens
static void E4_OnSwitch74(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != E4_SWITCH(0x74))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(E4_ANIM(0x70), 1, 0);
        Sound_PlayOnce(E4_ANIM(0x70), 0x1b9);
        AnimPlay(E4_ANIM(0x71), 1, 0);
        MobObj_Wake(E4_MOB_SKY_B);
        MobObj_Wake(E4_MOB_SKY_A);
    } else {
        AnimPlay(E4_ANIM(0x70), 1, 4);
        Sound_PlayOnce(E4_ANIM(0x70), 0x1b9);
        AnimPlay(E4_ANIM(0x71), 1, 4);
        MobObj_Sleep(E4_MOB_SKY_B);
    }
}

// 0x10001b50  EV_COLLISION {body, other, impulse}: touching a crate 0xf004f..0xf005c destroys it
static void E4_OnCrateTouched(const LanderEvent *ev)
{
    int32_t other = (int32_t)ev->arg[1];
    if (other < (int32_t)E4_ANIMDEST(0x4f) || other > (int32_t)E4_ANIMDEST(0x5c))
        return;
    Sound_PlayOnce((ObjId)ev->arg[1], 0x102);
    Object_SetProperty((ObjId)ev->arg[1], PROP_HITPOINTS, 0);
}

// 0x10001b90  InTime target: pillar pair i collapses
static void E4_Detonate(int32_t i)
{
    ObjId a = E4_PILLAR_A(i), b = E4_PILLAR_B(i);
    AnimPlay(a, 8, 0);
    AnimSetUpdateInterval(a, 5);
    AnimPlay(b, 7, 0);
    AnimSetUpdateInterval(b, 5);
    Sound_PlayOnce(b, 0xf5);
    Object_SetProperty(a, PROP_ARMOUR, 0xff);
    Object_SetProperty(b, PROP_ARMOUR, 0xff);
    InTime2(1275, Sound_PlayOnce, b, 0xf7);
}

// 0x10001c30  chain reaction from pillar pair n (0..14). Pairs 0..9 form rows of three
// (0-2, 3-5, then 6-7 and 8-9 with column = (n & 1) + 1) and fall one per second to the row's end;
// the end of some rows takes turrets and gun mobs with it; pairs 10/11 are single.
static void E4_Chain(int32_t n)
{
    int32_t row, col, delay;
    if (n >= 10) {
        E4_Detonate(n);
        if (n == 10) {
            InTime3(1000, Object_SetProperty, E4_TURRET(0x61), PROP_HITPOINTS, 0);
            InTime3(1000, Object_SetProperty, E4_TURRET(0x62), PROP_HITPOINTS, 0);
            InTime1(1000, Object_SetDead, E4_TURRET(0x61));
            InTime1(1000, Object_SetDead, E4_TURRET(0x62));
            Object_SetProperty(E4_TURRET(0x61), PROP_ARMOUR, 0xff);
            Object_SetProperty(E4_TURRET(0x62), PROP_ARMOUR, 0xff);
        } else if (n == 11) {
            InTime3(800, Object_SetProperty, E4_ANIMDEST(0x5b), PROP_HITPOINTS, 0);
        }
        return;
    }
    if (n >= 6) {
        col = (n & 1) + 1;
        row = n / 2 - 1;
        switch (n - 6) {            // jump table 0x10001e5c
        case 0:
        case 2:                     // 0x10001d1d
            if (st.chain[row][2] == 0) {
                InTime3(1000, Object_SetProperty, (ObjId)(n + 0x5e), PROP_HITPOINTS, 0);
                InTime1(1000, Object_SetDead, (ObjId)(n + 0x5e));
                Object_SetProperty((ObjId)(n + 0x5e), PROP_ARMOUR, 0xff);
            }
            // fall through
        case 1:
        case 3:                     // 0x10001d72
            Object_SetProperty((ObjId)(n + 0x5d), PROP_HITPOINTS, 0);
            Object_SetDead((ObjId)(n + 0x5d));
            break;
        default:
            break;
        }
    } else {
        row = n / 3;
        col = n % 3;
    }
    delay = 0;                                                     // 0x10001dab
    for (; col < 3; col++) {
        if (st.chain[row][col] != 0)
            return;
        st.chain[row][col] = 1;
        InTime1(delay, E4_Detonate, n);
        n++;
        delay += 1000;
    }
    if (n == 3) {                                                  // 0x10001e17
        InTime3(delay, Object_SetProperty, E4_ANIMTURRET(0x15), PROP_HITPOINTS, 0);
    } else if (n == 6) {                                           // 0x10001e38
        InTime3(delay, Object_SetProperty, E4_ANIMTURRET(0x16), PROP_HITPOINTS, 0);
    } else if (n == 8) {
        InTime3(delay, Object_SetProperty, E4_TARGET(9), PROP_HITPOINTS, 0);
        InTime3(delay, Object_SetProperty, E4_ANIMTURRET(0x15), PROP_HITPOINTS, 0);
    }
}

// 0x10001e70  EV_DESTROYED: a pillar sets off its group
static void E4_OnPillarDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)E4_PILLAR_A(0) && id <= (int32_t)E4_PILLAR_A(14))
        E4_Chain(id - (int32_t)E4_PILLAR_A(0));
    else if (id >= (int32_t)E4_PILLAR_B(0) && id <= (int32_t)E4_PILLAR_B(14))
        E4_Chain(id - (int32_t)E4_PILLAR_B(0));
}

// 0x10001ec0  EV_TRIGGER_ENTER trigger 0x4002a: underground - sky off, capsule on, patrols start
static void E4_OnTriggerUnderground(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E4_TRIGGER(0x2a))
        return;
    MobObj_Sleep(E4_MOB_SKY_B);
    MobObj_Sleep(E4_MOB_SKY_A);
    MobObj_Wake(E4_MOB_POD);
    Pod_Script_SetupHookPoint(E4_POD, 0, 0x40, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, E4_OnTriggerUnderground);
    AnimSeq_Start(E4_ANIMTURRET(0x09), e4_seq258, 1);           // 0x10001120
    AnimSetUpdateInterval(E4_ANIMTURRET(0x09), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x07), e4_seq428, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x07), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x08), e4_seq408, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x08), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x05), e4_seq278, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x05), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x06), e4_seq298, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x06), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x04), e4_seq2b8, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x04), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x0a), e4_seq2e0, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x0a), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x0b), e4_seq308, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x0b), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x0c), e4_seq3c0, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x0c), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x11), e4_seq348, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x11), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x0f), e4_seq328, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x0f), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x12), e4_seq370, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x12), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x10), e4_seq370, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x10), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x0d), e4_seq398, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x0d), 5);
    AnimSeq_Start(E4_ANIMTURRET(0x0e), e4_seq3e8, 1);
    AnimSetUpdateInterval(E4_ANIMTURRET(0x0e), 5);
}

// 0x10002100  EV_TRIGGER_ENTER trigger 0x40075 (every entry): gun mobs 0x17..0x19 rise
static void E4_OnTriggerGuns75(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != E4_TRIGGER(0x75))
        return;
    AnimPlay(E4_ANIMTURRET(0x17), 9, 0x62);
    AnimPlay(E4_ANIMTURRET(0x18), 0xa, 0x62);
    AnimPlay(E4_ANIMTURRET(0x19), 0xb, 0x62);
}

// 0x10002140  EV_TRIGGER_ENTER trigger 0x40076: gun mob 0x1a rises
static void E4_OnTriggerGun1a(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x76))
        AnimPlay(E4_ANIMTURRET(0x1a), 0xc, 0x62);
}

// 0x10002160  EV_TRIGGER_ENTER trigger 0x40076: gun mob 0x1b rises
static void E4_OnTriggerGun1b(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x76))
        AnimPlay(E4_ANIMTURRET(0x1b), 0xd, 0x62);
}

// 0x10002180  EV_TICK_1S once above y = 50; never removed (LevelEnd stops the scheduler)
static void E4_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(E4_LANDER, PROP_POSITION);
    if (pos[1] <= 10000)
        return;
    if (st.bombArmed == 1)
        MissionScore_End(1);                                      // 0x100015f0
    else if (st.bombArmed == 0)
        MissionScore_End(2);
    else
        return;
    Lander_SetCash(E4_LANDER, Lander_GetCash(E4_LANDER) + 24000);
}

// 0x100021e0  EV_TICK_1S after the hook: out of the workings (y > 50) -> the climb objective
static void E4_ClimbTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(E4_LANDER, PROP_POSITION);
    if (pos[1] <= 0x32)
        return;
    WindowSetText(st.win, st.text[2]);
    EventType_AddHandler(EV_TICK_1S, NULL, E4_EvacTick);
    EventType_RemoveHandler(EV_TICK_1S, E4_ClimbTick);
}

// 0x10002240  EV_TICK_1S after the white-out: the lander dies on the third tick (never removed)
static void E4_DeathTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.deathTicks == 2) {
        Object_SetProperty(E4_LANDER, PROP_HITPOINTS, 0);
        Environment_RestoreViewDistance();
        LevelEnd(0);
    }
    st.deathTicks++;
}

// 0x10002280  EV_TICK_1S after the hook: the fuse
static void E4_FuseTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.countdown == 0) {
        AnimPlay(E4_ANIM(0x38), 0x10, 0);
        if (st.safe == 0) {
            Environment_SetInteriorAmbient(0xff, 0xff, 0xff);
            Environment_OverrideInteriorFog(0xff, 0xff, 0xff);
            Environment_OverrideViewDistance(0, 1000);
            MobObj_Wake(E4_MOB_SHAKE);
            AnimPlay(E4_SHAKE, 0x2d, 0x40);
            EventType_AddHandler(EV_TICK_1S, NULL, E4_DeathTick);
        }
        if (st.safe == 1) {
            WindowSetText(st.win, st.text[8]);   // the distant explosion note (then overwritten below)
            MobObj_Wake(E4_MOB_SHAKE);
            AnimPlay(E4_SHAKE, 0x2d, 0x40);
        }
        EventType_RemoveHandler(EV_TICK_1S, E4_FuseTick);
    }
    if (st.countdown <= 0x37) {
        snprintf(st.timeText, sizeof st.timeText, e4_fmtTime, st.countdown);   // sprintf 0x10002fb0
        WindowSetText(st.win, st.timeText);
    }
    st.countdown--;
}

// 0x100023a0  EV_TRIGGER_ENTER: the safe boxes
static void E4_OnTriggerSafeEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x69))
        st.safe = 1;
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x68))
        st.inShelter = 1;
}

// 0x100023d0  EV_TRIGGER_LEAVE: the safe boxes
static void E4_OnTriggerSafeLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x69) && st.inShelter == 0)
        st.safe = 0;
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x68))
        st.inShelter = 0;
}

// 0x10002400  InTime target: the armed bomb's ticking
static void E4_BombSound(void)
{
    Sound_PlayChain(E4_BOMB, 10, 0x86, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8f);
}

// 0x10002450  EV_POD_HOOKED: start the fuse; arm the bomb if both detonators 0xf0025 / 0xf0016
// still stand (the handler then stays for 2 s more), else it fizzles
static void E4_OnPodHooked(const LanderEvent *ev)
{
    (void)ev;
    Pod_Script_SetupMass(E4_POD, 0x3e8);
    Pod_Script_SetupDrag(E4_POD, 0x1333);
    EventType_AddHandler(EV_TICK_1S, NULL, E4_ClimbTick);
    WindowSetText(st.win, st.text[0]);       // the fuse warning
    EventType_AddHandler(EV_TICK_1S, NULL, E4_FuseTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerSafeEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E4_OnTriggerSafeLeave);
    if (Object_GetProperty(E4_ANIMDEST(0x25), PROP_HITPOINTS) >= 1
        && Object_GetProperty(E4_ANIMDEST(0x16), PROP_HITPOINTS) >= 1) {
        InTime3(2000, AnimPlay, E4_BOMB, 0xe, 0);
        InTime0(1800, E4_BombSound);
        st.bombArmed = 1;
        InTime2(2000, EventType_RemoveHandler, EV_POD_HOOKED, E4_OnPodHooked);
        return;
    }
    AnimPlay(E4_BOMB, 0xf, 0);
    Sound_PlayChain(E4_BOMB, 6, 0x86, 0x8e, 0x8e, 0x8e, 0x8e, 0x8f);
    EventType_RemoveHandler(EV_POD_HOOKED, E4_OnPodHooked);
}

// 0x100025a0  EV_DESTROYED (while the bomb is in box 0x4006f): destroyed by a detonator -> the
// bomb goes off harmlessly
static void E4_OnDetonatorKill(const LanderEvent *ev)
{
    ObjId source = (ObjId)ev->arg[1];
    if (source != E4_ANIMDEST(0x25) && source != E4_ANIMDEST(0x16))
        return;
    Object_SetProperty(E4_BOMB, PROP_HITPOINTS, 0);
    st.bombArmed = 0;
}

// 0x100025e0  EV_TRIGGER_ENTER box 0x4006f (its target is the bomb); every entry adds a handler
static void E4_OnTriggerBombEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x6f))
        EventType_AddHandler(EV_DESTROYED, NULL, E4_OnDetonatorKill);
}

// 0x10002600  EV_TRIGGER_LEAVE box 0x4006f
static void E4_OnTriggerBombLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x6f))
        EventType_RemoveHandler(EV_DESTROYED, E4_OnDetonatorKill);
}

// 0x10002620  EV_TRIGGER_ENTER trigger 0x4007a: the workings objective
static void E4_OnTriggerDigSite(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x7a))
        WindowSetText(st.win, st.text[5]);
}

// 0x10002650  EV_TRIGGER_ENTER trigger 0x40028: the capsule objective
static void E4_OnTriggerEntrance(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == E4_TRIGGER(0x28))
        WindowSetText(st.win, st.text[6]);
}

// 0x10002680  EV_ANIM_FINISHED: the tremor is over
static void E4_OnShakeFinished(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == E4_SHAKE)
        MobObj_Sleep(E4_MOB_SHAKE);
}

// 0x100026a0  EV_PICKUP: +25 % shield, large / medium / small cash
static void E4_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == E4_PICKUP(0x6e)) {
        int32_t shield = (int32_t)Object_GetProperty(E4_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(E4_LANDER, 25);
        Object_SetProperty(E4_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == E4_PICKUP(0x79)) {
        Voice_CashAcquired(E4_LANDER);
        Lander_SetCash(E4_LANDER, Lander_GetCash(E4_LANDER) + gLargeCash);
    }
    if ((ObjId)ev->arg[0] == E4_PICKUP(0x00)) {
        Voice_CashAcquired(E4_LANDER);
        Lander_SetCash(E4_LANDER, Lander_GetCash(E4_LANDER) + gMediumCash);
    }
    if ((ObjId)ev->arg[0] == E4_PICKUP(0x39)) {
        Voice_CashAcquired(E4_LANDER);
        Lander_SetCash(E4_LANDER, Lander_GetCash(E4_LANDER) + gSmallCash);
    }
}

// 0x10002830  EV_TRIGGER_PLANE {player, plane, side}, planes 0x1a0029 / 0x1a002b: interior
// lighting on one side
static void E4_OnTriggerPlane(const LanderEvent *ev)
{
    ObjId plane = (ObjId)ev->arg[1];
    if (plane == E4_PLANE(0x29) || plane == E4_PLANE(0x2b))
        gInOutOverride = ev->arg[2] == 1;
}

// 0x10002860  EV_DESTROYED: a destroyed gun mob 0x10e0002..0x10e001b freezes its animation
static void E4_OnGunDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)E4_ANIMTURRET(0x02) && id <= (int32_t)E4_ANIMTURRET(0x1b))
        AnimStop((ObjId)id, 0);
}

// 0x10002890  common set-up
static void E4_Setup(void)
{
    st.text = e4_text[gLanguage];
    gTurretTypes[2] = &e4_turretType2;
    gTurretTypes[5] = &e4_turretType5;
    gTurretTypes[8] = &e4_turretType8;
    SetGravity(0x13);
    gAirResistance = 0x23;
    gMainThrottlePower = 0x25;
    for (int32_t i = 0; i <= 14; i++) {
        Object_SetProperty(E4_PILLAR_A(i), PROP_ARMOUR, 0x28);
        Object_SetProperty(E4_PILLAR_A(i), PROP_EXPLOSION, 0x64);
    }
    for (int32_t i = 0; i <= 14; i++) {
        Object_SetProperty(E4_PILLAR_B(i), PROP_ARMOUR, 0x28);
        Object_SetProperty(E4_PILLAR_B(i), PROP_EXPLOSION, 0x64);
    }
    for (int32_t id = (int32_t)E4_TURRET(0x5d); id <= (int32_t)E4_TURRET(0x67); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)E4_ANIMTURRET(0x02); id <= (int32_t)E4_ANIMTURRET(0x1b); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x14);
    for (int32_t id = (int32_t)E4_TARGET(0x2d); id <= (int32_t)E4_TARGET(0x37); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0xa);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = (int32_t)E4_ANIMDEST(0x4f); id <= (int32_t)E4_ANIMDEST(0x5c); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 5);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    Object_SetProperty(E4_TURRET(0x48), PROP_ARMOUR, 0x32);
    Object_SetProperty(E4_TURRET(0x48), PROP_EXPLOSION, 0x50);
    Object_SetProperty(E4_TARGET(4), PROP_ARMOUR, 0x28);
    Object_SetProperty(E4_TARGET(4), PROP_EXPLOSION, 0x64);
    Object_SetProperty(E4_FUELDUMP, PROP_ARMOUR, 10);
    Object_SetProperty(E4_FUELDUMP, PROP_EXPLOSION, 0x50);
    AnimSeq_Init();                                                // 0x100011c0
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);       // 0x48
    WindowSetSize(st.win, 20, 20, 350, 12);
    WindowSetText(st.win, st.text[4]);       // the dig site objective
    gNbWaypoints = e4_nbWaypoints;
    gWaypoints = e4_waypoints;
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x100027a0
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerDoorsOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerLift);
    EventType_AddHandler(EV_SWITCH, NULL, E4_OnSwitch72);
    EventType_AddHandler(EV_SWITCH, NULL, E4_OnSwitch73);
    EventType_AddHandler(EV_SWITCH, NULL, E4_OnSwitch74);
    EventType_AddHandler(EV_COLLISION, NULL, E4_OnCrateTouched);
    EventType_AddHandler(EV_DESTROYED, NULL, E4_OnPillarDestroyed);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerUnderground);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E4_OnTriggerDoorsClose);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerGuns75);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerGun1a);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerGun1b);
    EventType_AddHandler(EV_POD_HOOKED, NULL, E4_OnPodHooked);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerBombEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, E4_OnTriggerBombLeave);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerDigSite);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, E4_OnTriggerEntrance);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, E4_OnShakeFinished);
    EventType_AddHandler(EV_PICKUP, NULL, E4_OnPickup);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, E4_OnTriggerPlane);
    EventType_AddHandler(EV_DESTROYED, NULL, E4_OnGunDestroyed);
    AnimPlay(E4_ANIMDEST(0x4f), 2, 1);                             // the crates bob, staggered
    InTime3(500, AnimPlay, E4_ANIMDEST(0x50), 2, 1);
    InTime3(1800, AnimPlay, E4_ANIMDEST(0x51), 2, 1);
    InTime3(600, AnimPlay, E4_ANIMDEST(0x52), 2, 1);
    InTime3(1700, AnimPlay, E4_ANIMDEST(0x53), 2, 1);
    InTime3(700, AnimPlay, E4_ANIMDEST(0x54), 2, 1);
    InTime3(1600, AnimPlay, E4_ANIMDEST(0x55), 2, 1);
    InTime3(800, AnimPlay, E4_ANIMDEST(0x56), 2, 1);
    InTime3(1500, AnimPlay, E4_ANIMDEST(0x57), 2, 1);
    InTime3(900, AnimPlay, E4_ANIMDEST(0x58), 2, 1);
    InTime3(1400, AnimPlay, E4_ANIMDEST(0x59), 2, 1);
    InTime3(1000, AnimPlay, E4_ANIMDEST(0x5a), 2, 1);
    InTime3(1400, AnimPlay, E4_ANIMDEST(0x5b), 2, 1);
    InTime3(1200, AnimPlay, E4_ANIMDEST(0x5c), 2, 1);
    for (int32_t id = (int32_t)E4_ANIMDEST(0x4f); id <= (int32_t)E4_ANIMDEST(0x5c); id++)
        Sound_PlayLoop((ObjId)id, 0x101, -1);
    for (int32_t i = 2; i <= 0x1b; i++)       // AnimTurret mobs 0x10e0002..0x10e001b
        MobObj_Wake(i);
    for (int32_t id = (int32_t)E4_ANIMTURRET(0x02); id <= (int32_t)E4_ANIMTURRET(0x1b); id++)
        Sound_PlayLoop((ObjId)id, 0x8d, -1);
    MobObj_Wake(E4_MOB_SKY_B);
    MobObj_Wake(E4_MOB_SKY_A);
    Object_SetProperty(E4_TRIGGER(0x6f), PROP_TARGET, E4_BOMB);
    MissionScore_Init(E4_LANDER, 180000, 4);                       // 0x10001770
    Media_AudioPlay(0);
}

// 0x10002e10  LevelStartLevel_Lander (export 0x10001210)
static void E4_StartLander(void)
{
    MobObj_Wake(E4_MOB_LANDER);
    MobObj_Wake(E4_MOB_CAMERA);
    MobObj_Wake(0x21);              // AmbientFX
    MobObj_Wake(0x22);              // AmbientFX
    MobObj_Wake(0x25);              // AmbientFX
    E4_Setup();
    Object_SetProperty(E4_CAMERA, PROP_TARGET, E4_LANDER);
    Lander_SetHeadLight(E4_LANDER, 0x31c, 0x999);
}

// 0x10002ea0  LevelStartLevel_Flycam (export 0x10001230)
static void E4_StartFlycam(void)
{
    E4_Setup();
    MobObj_Wake(E4_MOB_FLYCAM);
}

// 0x10002ec0  LevelStopLevel (export 0x10001240): empty
static void E4_Stop(void)
{
}

// 0x10001250
static uint32_t E4_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001200  calls the empty 0x10001270 and 0x10001280, returns 0. Port: fresh DLL data.
static int32_t E4_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.countdown = e4_countdownStart;
    MissionScore_Reset();
    AnimSeq_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e4_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x100101b0, .count = 3 * 11, .out = e4_text },
    { .kind = GT_STRINGS, .va = 0x100108bc, .count = 1, .out = &e4_fmtTime },
    { 0 },
};

const mission_script_t mission_earth_4 = {
    .name          = "EARTH\\4",
    .get_version   = E4_GetVersion,
    .load          = E4_Load,
    .start_lander  = E4_StartLander,
    .start_flycam  = E4_StartFlycam,
    .start_animcam = NULL,          // 0x10002e70 exists but is not exported
    .stop          = E4_Stop,
    .text          = e4_gametext,
};
