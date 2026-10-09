// MISSIONS\MARS\5\MISSIONR.DLL - faithful C port.
//
// Mission: fly into the cave system, find the probe (pod mob 0x1d) and bring it out. The caves
// shake (the inertial frame mob 0x19 plays its quake animation at five trigger boxes and when the
// probe is hooked) and rocks (AnimObject mobs 3..0x17) come down; columns collapse when one part
// is destroyed; flares (moving light mobs) drift through the caves while the lander's headlight
// flickers; two segmented creatures (mobs 0x20/0x21) appear at trigger 0x40046. The trigger
// planes wake and sleep the mobs of each cave section. Leaving the caves with the probe (trigger
// 0x4002c) -> the climb objective: above y = 10000 = success 1, +50000 cash and the CUT1 film.
// Par 400 s, 1 pickup.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam 0x10001020, LevelStopLevel
// 0x10001030, LevelGetVersion 0x10001040. The score library (0x10001060-0x10001604, mercury_1's
// code) is in mission_lib.c; 0x100037f0 is the CRT's _ftol (truncating float -> int).
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define M5_LANDER        MOB_ID(OT_LANDER, 0x1a)         // 0x0101001a
#define M5_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define M5_MOB_CAMERA    1
#define M5_MOB_FLYCAM    0x18
#define M5_MOB_FRAME     0x19
#define M5_MOB_LANDER    0x1a
#define M5_MOB_PROBE     0x1d
#define M5_FRAME         MOB_ID(OT_INERTIALFRAME, 0x19)  // 0x01140019 the cave (quakes)
#define M5_PROBE         MOB_ID(OT_POD, 0x1d)            // 0x010c001d
#define M5_ROCK(n)       MOB_ID(OT_ANIMOBJECT, (n))      // 0x010300nn falling rocks
#define M5_LIGHT(n)      MOB_ID(OT_LIGHTOBJECT, (n))     // 0x011600nn flares / worm lights
#define M5_WORM_A        MOB_ID(OT_SEGMENTED, 0x20)      // 0x01170020
#define M5_WORM_B        MOB_ID(OT_SEGMENTED, 0x21)      // 0x01170021
#define M5_COLUMN(n)     OBJ_ID(OT_ANIMDESTRUCTIBLE, (n)) // 0x000f00nn
#define M5_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define M5_PLANE(n)      OBJ_ID(OT_TRIGGERPLANE, (n))    // 0x001a00nn

// ---- static data (.data) --------------------------------------------------------------------------
static const float    m5_flickerScale[2] = { 1.0f / 4096.0f, 4096.0f };   // 0x1000b170, 0x1000b174
static const int32_t  m5_nbWaypoints = 2;               // 0x1000d160
static const Waypoint m5_waypoints[2] = {               // 0x1000d168
    { -6400, 6400 },
    { -50560, -55424 },
};
// 0x1000d178: [language][4] string pointers (strings at 0x1000d1d0-0x1000d32f)
static const char *m5_text[3][4];
static const int32_t m5_flicker[10] = {                 // 0x1000d1a8 headlight intensities
    409, 1228, 0, 0, 1638, 1228, 819, 0, 409, 819
};
static const char m5_endMovie[] = "CUT1";               // 0x1000d330

// ---- DLL globals (.bss) -------------------------------------------------------------------------
static struct {
    Window      *win;           // 0x10010ac8 objective line
    int32_t      flickerIndex;  // 0x10010ad0
    uint8_t      rockCount[2];  // 0x10010ad4 finished runs of the rocks 0x1030003 / 0x1030008
    uint8_t      rockBusy[2];   // 0x10010ad6
    int32_t      flareWraps;    // 0x10010ad8
    int32_t      flareTurns;    // 0x10010adc
    int32_t      collapsed[4];  // 0x10010ae0 column i has collapsed
    int32_t      wormDone;      // 0x10010af0
    int32_t      wormWraps;     // 0x10010af4
    const char *const *text;    // 0x100110a4 = m5_text[gLanguage]
    ObjId        flareLast;     // 0x100110b0 flare that wrapped third
    ObjId        flareFirst;    // 0x100110b4
    ObjId        flareSecond;   // 0x100110b8
} st;

// 0x10001610  EV_TICK_100MS: headlight flicker while flares or creatures are about
static void M5_FlickerTick(const LanderEvent *ev)
{
    (void)ev;
    int32_t intensity = (int32_t)((float)m5_flicker[st.flickerIndex] * m5_flickerScale[0] * m5_flickerScale[1]);
    Lander_SetHeadLight(M5_LANDER, 0x31c, intensity);
    if (++st.flickerIndex == 10)
        st.flickerIndex = 0;
}

// 0x10001660  EV_TICK_1S once out of the caves with the probe
static void M5_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(M5_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        SetLevelEndMovie(m5_endMovie);
        MissionScore_End(1);                                         // 0x100013b0
        Lander_SetCash(M5_LANDER, Lander_GetCash(M5_LANDER) + 50000);
        EventType_RemoveHandler(EV_TICK_1S, M5_EvacTick);
    }
}

// 0x100016d0  EV_TRIGGER_ENTER trigger 0x4002c (cave mouth, after the probe was hooked)
static void M5_OnTriggerCaveExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x2c))
        return;
    WindowSetText(st.win, st.text[3]);       // the evacuation objective
    EventType_AddHandler(EV_TICK_1S, NULL, M5_EvacTick);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerCaveExit);
}

// 0x10001720  EV_COLLISION {owner, other, impulse}: bumping the rock 0x1030003 sets it moving
// (second and third run only)
static void M5_OnCollisionRock3(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_ROCK(3) || st.rockBusy[0])
        return;
    if (st.rockCount[0] == 1) {
        AnimPlay(M5_ROCK(3), 0x19, 0x80);
        AnimSetUpdateInterval(M5_ROCK(3), 0x28);
        st.rockBusy[0] = 1;
        Sound_PlayOnce(M5_ROCK(3), 0xf8);
    } else if (st.rockCount[0] == 2) {
        AnimPlay(M5_ROCK(3), 0x18, 0x80);
        AnimSetUpdateInterval(M5_ROCK(3), 0x28);
        st.rockBusy[0] = 1;
        Sound_PlayOnce(M5_ROCK(3), 0xf8);
        InTime2(0xfa0, Sound_PlayOnce, M5_ROCK(3), 0xf8);
        InTime2(0xfa0, Sound_PlayOnce, M5_ROCK(3), 0xf8);
    }
}

// 0x10001810  EV_COLLISION: the rock 0x1030008 (second run)
static void M5_OnCollisionRock8(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_ROCK(8) || st.rockBusy[1] || st.rockCount[1] != 1)
        return;
    AnimPlay(M5_ROCK(8), 0x1c, 0x80);
    AnimSetUpdateInterval(M5_ROCK(8), 0x28);
    st.rockBusy[1] = 1;
    Sound_PlayOnce(M5_ROCK(8), 0xf8);
}

// 0x10001870  EV_ANIM_FINISHED of the rock 0x1030003 or 0x1030008
static void M5_OnRockAnimDone(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != M5_ROCK(3) && id != M5_ROCK(8))
        return;
    int i = (id != M5_ROCK(3));
    st.rockCount[i]++;
    st.rockBusy[i] = 0;
}

// 0x100018c0  EV_ANIM_TURNED of the flare 0x116001e/1f: it is gone, light back to normal
static void M5_OnFlareGone(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != M5_LIGHT(0x1e) && id != M5_LIGHT(0x1f))
        return;
    MobObj_Sleep((int32_t)(id & 0xffff));
    Sound_StopObject((ObjId)ev->arg[0]);
    Lander_SetHeadLight(M5_LANDER, 0x31c, 0xccc);
    EventType_RemoveHandler(EV_TICK_100MS, M5_FlickerTick);
}

// 0x10001920  EV_TRIGGER_ENTER triggers 0x40044 / 0x40045: a flare comes by
static void M5_OnTriggerFlares(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == M5_TRIGGER(0x44)) {
        EventType_AddHandler(EV_TICK_100MS, NULL, M5_FlickerTick);
        MobObj_Wake(0x1e);
        AnimPlay(M5_LIGHT(0x1e), 0x2b, 0);
        Sound_PlayOnce(M5_LIGHT(0x1e), 0x230);
        EventType_AddHandler(EV_ANIM_TURNED, NULL, M5_OnFlareGone);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerFlares);
    }
    if ((ObjId)ev->arg[1] == M5_TRIGGER(0x45)) {
        EventType_AddHandler(EV_TICK_100MS, NULL, M5_FlickerTick);
        MobObj_Wake(0x1f);
        AnimPlay(M5_LIGHT(0x1f), 0x2c, 0);
        Sound_PlayOnce(M5_LIGHT(0x1f), 0x230);
        EventType_AddHandler(EV_ANIM_TURNED, NULL, M5_OnFlareGone);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerFlares);
    }
}

// 0x10001a00  EV_ANIM_TURNED of the flare 0x1160025: the three flares are gone
static void M5_OnFlaresOut(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != M5_LIGHT(0x25))
        return;
    MobObj_Sleep(0x25);
    MobObj_Sleep(0x26);
    MobObj_Sleep(0x27);
    Lander_SetHeadLight(M5_LANDER, 0x31c, 0xccc);
    EventType_RemoveHandler(EV_TICK_100MS, M5_FlickerTick);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerFlares);
    EventType_RemoveHandler(EV_ANIM_TURNED, M5_OnFlaresOut);
}

// 0x10001a80  InTime: the three flares fly off
static void M5_FlaresAway(void)
{
    AnimPlay(st.flareLast, 0x28, 0x40);
    InTime3(200, AnimPlay, st.flareFirst, 0x29, 0x40);
    InTime3(400, AnimPlay, st.flareSecond, 0x2a, 0x40);
    Sound_StopObject(M5_LIGHT(0x25));
    Sound_StopObject(M5_LIGHT(0x26));
    Sound_StopObject(M5_LIGHT(0x27));
    Sound_PlayOnce(M5_LIGHT(0x25), 0x230);
    InTime2(200, Sound_PlayOnce, M5_LIGHT(0x26), 0x230);
    InTime2(400, Sound_PlayOnce, M5_LIGHT(0x27), 0x230);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, M5_OnFlaresOut);
}

// 0x10001b50  EV_ANIM_TURNED: two flares finished their approach (anims 0x25/0x26)
static void M5_OnFlaresTurned(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)M5_LIGHT(0x25) && id <= (int32_t)M5_LIGHT(0x27) && (ev->arg[1] == 0x25 || ev->arg[1] == 0x26))
        st.flareTurns++;
    if (st.flareTurns == 2) {
        InTime0(500, M5_FlaresAway);
        EventType_RemoveHandler(EV_ANIM_TURNED, M5_OnFlaresTurned);
    }
}

// 0x10001bb0  EV_ANIM_WRAPPED of a flare's idle loop (anim 0x27): the first two come closer
static void M5_OnFlareWrapped(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)M5_LIGHT(0x25) || id > (int32_t)M5_LIGHT(0x27) || ev->arg[1] != 0x27)
        return;
    AnimStop((ObjId)id, 1);
    st.flareWraps++;
    if (st.flareWraps == 1) {
        AnimPlay((ObjId)ev->arg[0], 0x25, 0x40);
        Sound_PlayOnce((ObjId)ev->arg[0], 0x232);
        AnimSetUpdateInterval((ObjId)ev->arg[0], 5);
        EventType_AddHandler(EV_ANIM_TURNED, NULL, M5_OnFlaresTurned);
        st.flareFirst = (ObjId)ev->arg[0];
    } else if (st.flareWraps == 2) {
        AnimPlay((ObjId)ev->arg[0], 0x26, 0x40);
        Sound_PlayOnce((ObjId)ev->arg[0], 0x232);
        AnimSetUpdateInterval((ObjId)ev->arg[0], 5);
        st.flareSecond = (ObjId)ev->arg[0];
    } else {
        EventType_RemoveHandler(EV_ANIM_WRAPPED, M5_OnFlareWrapped);
        st.flareLast = (ObjId)ev->arg[0];
    }
}

// 0x10001ca0  EV_TRIGGER_ENTER trigger 0x40037: the flares notice the lander
static void M5_OnTriggerFlareStart(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x37))
        return;
    EventType_AddHandler(EV_TICK_100MS, NULL, M5_FlickerTick);
    EventType_AddHandler(EV_ANIM_WRAPPED, NULL, M5_OnFlareWrapped);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerFlareStart);
}

// 0x10001cf0  the three flares 0x1160025..27 idle, staggered
static void M5_StartFlares(void)
{
    MobObj_Wake(0x25);
    MobObj_Wake(0x26);
    MobObj_Wake(0x27);
    Sound_PlayLoop(M5_LIGHT(0x25), 0x231, -1);
    Sound_PlayLoop(M5_LIGHT(0x26), 0x231, -1);
    Sound_PlayLoop(M5_LIGHT(0x27), 0x231, -1);
    AnimPlay(M5_LIGHT(0x25), 0x27, 0x41);
    InTime3(0x28a, AnimPlay, M5_LIGHT(0x26), 0x27, 0x41);
    InTime3(0x5dc, AnimPlay, M5_LIGHT(0x27), 0x27, 0x41);
}

// 0x10001d90  EV_TRIGGER_ENTER trigger 0x40049: first quake, rock 0x1030003, three columns go
static void M5_OnTriggerQuake1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x49))
        return;
    AnimPlay(M5_FRAME, 0x17, 0xc0);
    Sound_PlayOnce(M5_LANDER, 0xf5);
    Sound_PlayOnce(M5_FRAME, 0xe7);
    InTime3(1, AnimPlay, M5_ROCK(3), 0x1a, 0x80);
    InTime2(0xe10, Sound_PlayOnce, M5_ROCK(3), 0xf8);
    AnimSetUpdateInterval(M5_ROCK(3), 0x28);
    st.rockBusy[0] = 1;
    InTime3(0x1770, Object_SetProperty, M5_COLUMN(0x1c), PROP_HITPOINTS, 0);
    InTime3(0x128e, Object_SetProperty, M5_COLUMN(0x1d), PROP_HITPOINTS, 0);
    InTime3(0xdac, Object_SetProperty, M5_COLUMN(0x1e), PROP_HITPOINTS, 0);
    InTime3(0x128e, Object_SetProperty, M5_COLUMN(0x17), PROP_HITPOINTS, 0);
    InTime2(0x1770, Sound_PlayOnce, M5_COLUMN(0x1c), 0xf5);
    InTime2(0x128e, Sound_PlayOnce, M5_COLUMN(0x1d), 0xf5);
    InTime2(0xdac, Sound_PlayOnce, M5_COLUMN(0x1e), 0xf5);
    InTime2(0x2134, Sound_PlayOnce, M5_COLUMN(0x1c), 0xf7);
    InTime2(0x1c52, Sound_PlayOnce, M5_COLUMN(0x1d), 0xf7);
    InTime2(0x1770, Sound_PlayOnce, M5_COLUMN(0x1e), 0xf7);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerFlareStart);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerQuake1);
}

// 0x10001f40  EV_TRIGGER_ENTER trigger 0x4004a: quake, rocks 0x1030004/05
static void M5_OnTriggerQuake2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x4a))
        return;
    AnimStop(M5_FRAME, 0);
    InTime3(100, AnimPlay, M5_FRAME, 0x17, 0xc0);
    Sound_PlayOnce(M5_LANDER, 0xf5);
    InTime2(800, Sound_PlayOnce, M5_FRAME, 0xe7);
    InTime3(0x2ee, AnimPlay, M5_ROCK(4), 2, 0x80);
    InTime2(0x2ee, Sound_PlayOnce, M5_ROCK(4), 0xf6);
    InTime2(0x10fe, Sound_PlayOnce, M5_ROCK(4), 0xf8);
    InTime2(0x15ae, Sound_PlayOnce, M5_ROCK(4), 0xf8);
    InTime3(0x61a8, AnimPlay, M5_ROCK(5), 3, 0x80);
    InTime2(0x6c98, Sound_PlayOnce, M5_ROCK(5), 0xf8);
    AnimSetUpdateInterval(M5_ROCK(4), 0x28);
    AnimSetUpdateInterval(M5_ROCK(5), 0x28);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerQuake2);
}

// 0x10002080  EV_TRIGGER_ENTER trigger 0x4004b: quake, rocks 0x1030006..08
static void M5_OnTriggerQuake3(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x4b))
        return;
    AnimStop(M5_FRAME, 0);
    InTime3(100, AnimPlay, M5_FRAME, 0x17, 0xc0);
    Sound_PlayOnce(M5_LANDER, 0xf5);
    InTime2(800, Sound_PlayOnce, M5_FRAME, 0xe7);
    InTime3(0xfa, AnimPlay, M5_ROCK(6), 4, 0x80);
    InTime2(0x13ba, Sound_PlayOnce, M5_ROCK(6), 0xf8);
    InTime3(0x2ee, AnimPlay, M5_ROCK(7), 5, 0x80);
    InTime2(0x15ae, Sound_PlayOnce, M5_ROCK(7), 0xf8);
    InTime3(0xabe, AnimPlay, M5_ROCK(8), 6, 0x80);
    InTime2(0x18ce, Sound_PlayOnce, M5_ROCK(8), 0xf8);
    InTime2(0x23be, Sound_PlayOnce, M5_ROCK(8), 0xf8);
    InTime2(0x29fe, Sound_PlayOnce, M5_ROCK(8), 0xf8);
    AnimSetUpdateInterval(M5_ROCK(7), 0x28);
    AnimSetUpdateInterval(M5_ROCK(6), 0x28);
    AnimSetUpdateInterval(M5_ROCK(8), 0x28);
    st.rockBusy[0] = 1;              // (the flag of rock 0x1030003, as in the DLL)
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerQuake3);
}

// 0x10002200  EV_TRIGGER_ENTER trigger 0x4004c: quake, rocks 0x1030009/0a
static void M5_OnTriggerQuake4(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x4c))
        return;
    AnimStop(M5_FRAME, 0);
    InTime3(100, AnimPlay, M5_FRAME, 0x17, 0xc0);
    Sound_PlayOnce(M5_LANDER, 0xf5);
    InTime2(800, Sound_PlayOnce, M5_FRAME, 0xe7);
    InTime3(0x1f4, AnimPlay, M5_ROCK(9), 7, 0x80);
    InTime2(0x1194, Sound_PlayOnce, M5_ROCK(9), 0xf8);
    InTime2(0x1af4, Sound_PlayOnce, M5_ROCK(9), 0xf8);
    InTime2(0x2774, Sound_PlayOnce, M5_ROCK(9), 0xf8);
    InTime3(0x2ee, AnimPlay, M5_ROCK(0xa), 8, 0x80);
    InTime2(0x1d7e, Sound_PlayOnce, M5_ROCK(0xa), 0xf8);
    InTime2(0x222e, Sound_PlayOnce, M5_ROCK(0xa), 0xf8);
    AnimSetUpdateInterval(M5_ROCK(9), 0x28);
    AnimSetUpdateInterval(M5_ROCK(0xa), 0x28);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerQuake4);
}

// 0x10002350  EV_TRIGGER_ENTER trigger 0x4004d: quake, rocks 0x103000b/0c
static void M5_OnTriggerQuake5(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x4d))
        return;
    AnimStop(M5_FRAME, 0);
    InTime3(100, AnimPlay, M5_FRAME, 0x17, 0xc0);
    Sound_PlayOnce(M5_LANDER, 0xf5);
    InTime2(800, Sound_PlayOnce, M5_FRAME, 0xe7);
    InTime3(0x3e8, AnimPlay, M5_ROCK(0xb), 9, 0x80);
    InTime2(0x16a8, Sound_PlayOnce, M5_ROCK(0xb), 0xf8);
    InTime2(0x1ce8, Sound_PlayOnce, M5_ROCK(0xb), 0xf8);
    InTime3(0x5dc, AnimPlay, M5_ROCK(0xc), 0xa, 0x80);
    InTime2(0x13ec, Sound_PlayOnce, M5_ROCK(0xc), 0xf8);
    InTime2(0x251c, Sound_PlayOnce, M5_ROCK(0xc), 0xf8);
    AnimSetUpdateInterval(M5_ROCK(0xb), 0x28);
    AnimSetUpdateInterval(M5_ROCK(0xc), 0x28);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerQuake5);
}

// 0x10002490  EV_POD_HOOKED (the probe): a big quake on the way out, rocks 0x103000d..11
static void M5_OnProbeHooked(const LanderEvent *ev)
{
    (void)ev;
    Pod_Script_SetupMass(M5_PROBE, 0x3e8);
    Pod_Script_SetupDrag(M5_PROBE, 0x1333);
    AnimStop(M5_FRAME, 0);
    InTime3(1000, AnimPlay, M5_FRAME, 0x17, 0xc0);
    Sound_PlayOnce(M5_LANDER, 0xf5);
    InTime2(800, Sound_PlayOnce, M5_FRAME, 0xe7);
    InTime3(0x7d0, AnimPlay, M5_ROCK(0xd), 0xb, 0x80);
    InTime2(0x1a90, Sound_PlayOnce, M5_ROCK(0xd), 0xf8);
    InTime2(0x1db0, Sound_PlayOnce, M5_ROCK(0xd), 0xf8);
    InTime3(0x9c4, AnimPlay, M5_ROCK(0xe), 0xc, 0x80);
    InTime2(0x1fa4, Sound_PlayOnce, M5_ROCK(0xe), 0xf8);
    InTime2(0x2904, Sound_PlayOnce, M5_ROCK(0xe), 0xf8);
    InTime3(0xbb8, AnimPlay, M5_ROCK(0xf), 0xd, 0x80);
    InTime2(0x2648, Sound_PlayOnce, M5_ROCK(0xf), 0xf8);
    InTime2(0x3138, Sound_PlayOnce, M5_ROCK(0xf), 0xf8);
    InTime3(0x109a, AnimPlay, M5_ROCK(0x10), 0xe, 0x80);
    InTime2(0x235a, Sound_PlayOnce, M5_ROCK(0x10), 0xf8);
    InTime2(0x267a, Sound_PlayOnce, M5_ROCK(0x10), 0xf8);
    InTime3(0x1964, AnimPlay, M5_ROCK(0x11), 0xf, 0x80);
    InTime2(0x22c4, Sound_PlayOnce, M5_ROCK(0x11), 0xf8);
    for (int32_t i = 0xd; i <= 0x11; i++)
        AnimSetUpdateInterval(M5_ROCK(i), 0x28);
    WindowSetText(st.win, st.text[2]);       // the exit objective
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerCaveExit);
    EventType_RemoveHandler(EV_POD_HOOKED, M5_OnProbeHooked);
}

// 0x100026f0  EV_DESTROYED of a stalactite 0xf0005..12 or its partner 0xf001c..29 (index +/- 0x17):
// it falls and takes the partner with it
static void M5_OnDestroyedStalactite(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)M5_COLUMN(0x05) && id <= (int32_t)M5_COLUMN(0x12)) {
        ObjId other = (ObjId)(id + 0x17);
        AnimPlay((ObjId)id, 1, 0x80);
        if (Object_GetProperty(other, PROP_HITPOINTS) != 0)
            Object_SetProperty(other, PROP_HITPOINTS, 0);
    } else if (id >= (int32_t)M5_COLUMN(0x1c) && id <= (int32_t)M5_COLUMN(0x29)) {
        ObjId other = (ObjId)(id - 0x17);
        AnimPlay((ObjId)id, 0, 0x80);
        Sound_PlayOnce((ObjId)ev->arg[0], 0xf5);
        InTime2(0x9c4, Sound_PlayOnce, ev->arg[0], 0xf7);
        if (Object_GetProperty(other, PROP_HITPOINTS) != 0)
            Object_SetProperty(other, PROP_HITPOINTS, 0);
    }
}

// 0x100027c0  column i (parts 0xf0001 + i, 0xf0013 + i, 0xf0017 + i) collapses
static void M5_Collapse(int32_t i)
{
    ObjId top = (ObjId)(0xf0017 + i);
    AnimPlay(top, 0x1f, 0x80);
    AnimPlay((ObjId)(0xf0013 + i), 0x1e, 0x80);
    AnimPlay((ObjId)(0xf0001 + i), 0x1d, 0x80);
    Sound_PlayOnce(top, 0xf5);
    InTime2(0xdac, Sound_PlayOnce, top, 0xf7);
    st.collapsed[i] = 1;
}

// 0x10002840  EV_DESTROYED of a column part: destroy the other two parts and collapse it
static void M5_OnDestroyedColumn(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    int32_t i;
    if (id >= 0xf0017 && id <= 0xf001a) {
        i = id - 0xf0017;
        if (st.collapsed[i] != 0)
            return;
        Object_SetProperty((ObjId)(0xf0013 + i), PROP_HITPOINTS, 0);
        Object_SetProperty((ObjId)(0xf0001 + i), PROP_HITPOINTS, 0);
        M5_Collapse(i);
    } else if (id >= 0xf0013 && id <= 0xf0016) {
        i = id - 0xf0013;
        if (st.collapsed[i] != 0)
            return;
        Object_SetProperty((ObjId)(0xf0017 + i), PROP_HITPOINTS, 0);
        Object_SetProperty((ObjId)(0xf0001 + i), PROP_HITPOINTS, 0);
        M5_Collapse(i);
    } else if (id >= 0xf0001 && id <= 0xf0004) {
        i = id - 0xf0001;
        if (st.collapsed[i] != 0)
            return;
        Object_SetProperty((ObjId)(0xf0017 + i), PROP_HITPOINTS, 0);
        Object_SetProperty((ObjId)(0xf0013 + i), PROP_HITPOINTS, 0);
        M5_Collapse(i);
    }
}

// 0x10002950  EV_DESTROYED 0xf0043: it falls and the rock 0x3004e with it
static void M5_OnDestroyed43(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != M5_COLUMN(0x43))
        return;
    AnimPlay(M5_COLUMN(0x43), 0x20, 0x80);
    AnimPlay(OBJ_ID(OT_ANIMOBJECT, 0x4e), 0x21, 0x80);
    EventType_RemoveHandler(EV_DESTROYED, M5_OnDestroyed43);
}

// 0x100029a0  InTime 8 s after the creatures appear: stalactites and rocks come down
static void M5_RockFall(void)
{
    InTime3(0x26ac, Object_SetProperty, M5_COLUMN(0x21), PROP_HITPOINTS, 0);
    InTime3(0x2008, Object_SetProperty, M5_COLUMN(0x22), PROP_HITPOINTS, 0);
    InTime3(0x1388, Object_SetProperty, M5_COLUMN(0x23), PROP_HITPOINTS, 0);
    InTime3(0xdac, Object_SetProperty, M5_COLUMN(0x24), PROP_HITPOINTS, 0);
    InTime3(0x4e2, Object_SetProperty, M5_COLUMN(0x25), PROP_HITPOINTS, 0);
    InTime3(0x1f4, Object_SetProperty, M5_COLUMN(0x26), PROP_HITPOINTS, 0);
    InTime3(0x708, Object_SetProperty, M5_COLUMN(0x27), PROP_HITPOINTS, 0);
    InTime3(0x1d4c, Object_SetProperty, M5_COLUMN(0x28), PROP_HITPOINTS, 0);
    InTime3(0x11f8, Object_SetProperty, M5_COLUMN(0x18), PROP_HITPOINTS, 0);
    InTime3(0x708, Object_SetProperty, M5_COLUMN(0x1a), PROP_HITPOINTS, 0);
    InTime3(0x1194, AnimPlay, M5_ROCK(0x13), 0x10, 0x80);
    InTime2(0x2454, Sound_PlayOnce, M5_ROCK(0x13), 0xf8);
    InTime3(0x11f8, AnimPlay, M5_ROCK(0x14), 0x11, 0x80);
    InTime2(0x24b8, Sound_PlayOnce, M5_ROCK(0x14), 0xf8);
    InTime3(0x1964, AnimPlay, M5_ROCK(0x15), 0x12, 0x80);
    InTime2(0x2c24, Sound_PlayOnce, M5_ROCK(0x15), 0xf8);
    InTime3(0x3e8, AnimPlay, M5_ROCK(0x16), 0x13, 0x80);
    InTime2(0x1518, Sound_PlayOnce, M5_ROCK(0x16), 0xf8);
    InTime3(0xbb8, AnimPlay, M5_ROCK(0x17), 0x14, 0x80);
    InTime2(0x1e78, Sound_PlayOnce, M5_ROCK(0x17), 0xf8);
}

// 0x10002b90  EV_TRIGGER_ENTER trigger 0x4001b: stalactites 0xf0029/19 and the rock 0x1030002
static void M5_OnTrigger1B(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x1b))
        return;
    InTime3(1000, Object_SetProperty, M5_COLUMN(0x29), PROP_HITPOINTS, 0);
    InTime3(2000, Object_SetProperty, M5_COLUMN(0x19), PROP_HITPOINTS, 0);
    InTime3(0x1388, AnimPlay, M5_ROCK(2), 0x15, 0x80);
    InTime2(0x1a90, Sound_PlayOnce, M5_ROCK(2), 0xf8);
    AnimSetUpdateInterval(M5_ROCK(2), 0x14);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTrigger1B);
}

// 0x10002c40  EV_ANIM_TURNED: a creature finished its retreat (anim 0x23/0x24); after both, their
// lights go and the flicker stops
static void M5_OnWormGone(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != M5_WORM_A && id != M5_WORM_B)
        return;
    if (ev->arg[1] != 0x24 && ev->arg[1] != 0x23)
        return;
    MobObj_Sleep((int32_t)(id & 0xffff));
    if (st.wormDone != 0) {
        MobObj_Sleep(0x22);
        MobObj_Sleep(0x23);
        Lander_SetHeadLight(M5_LANDER, 0x31c, 0xccc);
        EventType_RemoveHandler(EV_TICK_100MS, M5_FlickerTick);
        EventType_RemoveHandler(EV_ANIM_TURNED, M5_OnWormGone);
    }
    st.wormDone++;
}

// 0x10002cd0  EV_ANIM_WRAPPED of a creature's loop: it retreats; the first one also takes the
// light 0x1b away and shakes the cave
static void M5_OnWormWrapped(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != M5_WORM_A && id != M5_WORM_B)
        return;
    AnimStop(id, 1);
    Object_SetProperty((ObjId)ev->arg[0], PROP_STATE, 0);
    if ((ObjId)ev->arg[0] == M5_WORM_A) {
        AnimPlay(M5_WORM_A, 0x23, 0x40);
        Sound_PlayOnce((ObjId)ev->arg[0], 0x234);
    } else {
        AnimPlay((ObjId)ev->arg[0], 0x24, 0x40);
        Sound_PlayOnce((ObjId)ev->arg[0], 0x234);
    }
    if (st.wormWraps == 0) {
        MobObj_Sleep(0x1b);
        AnimStop(M5_FRAME, 0);
        InTime3(100, AnimPlay, M5_FRAME, 0x17, 0xc0);
        Sound_PlayOnce(M5_LANDER, 0xf5);
        InTime2(800, Sound_PlayOnce, M5_FRAME, 0xe7);
        EventType_AddHandler(EV_ANIM_TURNED, NULL, M5_OnWormGone);
    } else {
        EventType_RemoveHandler(EV_ANIM_WRAPPED, M5_OnWormWrapped);
    }
    st.wormWraps++;
}

// 0x10002e00  EV_ANIM_FINISHED with anim 0x22: the creature's arrival sound stops
static void M5_OnWormArrived(const LanderEvent *ev)
{
    if (ev->arg[1] != 0x22)
        return;
    Sound_StopObject(M5_WORM_A);
    EventType_RemoveHandler(EV_ANIM_FINISHED, M5_OnWormArrived);
}

// 0x10002e30  InTime 8 s after the creatures appear
static void M5_ArmWorms(void)
{
    EventType_AddHandler(EV_ANIM_WRAPPED, NULL, M5_OnWormWrapped);
}

// 0x10002e50  EV_TRIGGER_ENTER trigger 0x40046: the two creatures come out of the rock
static void M5_OnTriggerWorms(const LanderEvent *ev)
{
    int32_t pos[4] = { -41088, -33152, -21248, 0 };   // a local VECTOR in the DLL
    if ((ObjId)ev->arg[1] != M5_TRIGGER(0x46))
        return;
    MobObj_Wake(0x20);
    Object_SetProperty(M5_WORM_A, PROP_POSITION, (intptr_t)pos);
    AnimPlay(M5_WORM_A, 0x22, 0x41);
    Sound_PlayLoop(M5_WORM_A, 0x233, -1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M5_OnWormArrived);
    AnimSetUpdateInterval(M5_WORM_A, 5);
    MobObj_Wake(0x21);
    Object_SetProperty(M5_WORM_B, PROP_POSITION, (intptr_t)pos);
    AnimPlay(M5_WORM_B, 0x22, 0x41);
    AnimSetUpdateInterval(M5_WORM_B, 5);
    MobObj_Wake(0x1b);              // lights 0x1b, 0x22, 0x23
    MobObj_Wake(0x22);
    MobObj_Wake(0x23);
    Object_SetProperty(M5_LIGHT(0x22), PROP_TARGET, M5_WORM_A);
    Object_SetProperty(M5_LIGHT(0x23), PROP_TARGET, M5_WORM_B);
    EventType_AddHandler(EV_TICK_100MS, NULL, M5_FlickerTick);
    InTime0(0x1f40, M5_ArmWorms);
    InTime0(0x1f40, M5_RockFall);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTrigger1B);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, M5_OnTriggerWorms);
}

// 0x10002fd0 = MissionLib_OnDestroyedSound (same thresholds)

// 0x10003060  EV_DESTROYED fuel dump 0x11002d: its light 0x16002e goes out
static void M5_OnDestroyedDump(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_FUELDUMP, 0x2d))
        return;
    Object_SetDead(OBJ_ID(OT_LIGHTOBJECT, 0x2e));
    EventType_RemoveHandler(EV_DESTROYED, M5_OnDestroyedDump);
}

// 0x10003090  EV_TRIGGER_PLANE 0x1a0038 (cave entrance): side 1 -> the probe objective (once)
static void M5_OnPlaneEntrance(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != M5_PLANE(0x38))
        return;
    if (ev->arg[2] == 1) {
        WindowSetText(st.win, st.text[1]);
        EventType_RemoveHandler(EV_TRIGGER_PLANE, M5_OnPlaneEntrance);
    } else {
        WindowSetText(st.win, st.text[0]);
    }
}

// 0x100030f0  EV_TRIGGER_PLANE: each cave section's rocks are woken on the way in and put to
// sleep on the way out (side 1 / other side)
static void M5_OnPlaneSections(const LanderEvent *ev)
{
    static const uint8_t deep[13] = { 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x13, 0x14, 0x15, 0x16, 0x17 };
    int in = (ev->arg[2] == 1);
    if ((ObjId)ev->arg[1] == M5_PLANE(0x38)) {
        if (in) MobObj_Wake(3); else MobObj_Sleep(3);
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x3a)) {
        if (in) MobObj_Sleep(3); else MobObj_Wake(3);
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x3b)) {
        if (in) MobObj_Wake(4); else MobObj_Sleep(4);
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x3d)) {
        if (in) {
            MobObj_Sleep(4);
            for (int32_t m = 5; m <= 8; m++)
                MobObj_Wake(m);
        } else {
            MobObj_Wake(4);
            for (int32_t m = 5; m <= 8; m++)
                MobObj_Sleep(m);
        }
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x3e)) {
        for (int32_t m = 5; m <= 8; m++) {
            if (in) MobObj_Sleep(m); else MobObj_Wake(m);
        }
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x3f)) {
        if (in) MobObj_Wake(9); else MobObj_Sleep(9);
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x40)) {
        if (in) MobObj_Sleep(9); else MobObj_Wake(9);
        for (int i = 0; i < 13; i++) {
            if (in) MobObj_Wake(deep[i]); else MobObj_Sleep(deep[i]);
        }
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x41)) {
        if (in) MobObj_Wake(2); else MobObj_Sleep(2);
        for (int i = 0; i < 13; i++) {
            if (in) MobObj_Sleep(deep[i]); else MobObj_Wake(deep[i]);
        }
    }
    if ((ObjId)ev->arg[1] == M5_PLANE(0x42)) {
        if (in) MobObj_Sleep(2); else MobObj_Wake(2);
    }
}

// 0x100033e0  EV_PICKUP 0x130000: large cash
static void M5_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != OBJ_ID(OT_PICKUP, 0))
        return;
    Lander_SetCash(M5_LANDER, Lander_GetCash(M5_LANDER) + gLargeCash);
    Voice_CashAcquired(M5_LANDER);
}

// 0x10003430  common set-up (runs before the lander exists; the score library is started by
// LevelStartLevel_Lander)
static void M5_Setup(void)
{
    st.text = m5_text[gLanguage];
    for (int32_t id = (int32_t)M5_COLUMN(0x01); id <= (int32_t)M5_COLUMN(0x1a); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    for (int32_t id = (int32_t)M5_COLUMN(0x1c); id <= (int32_t)M5_COLUMN(0x29); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x2d), PROP_ARMOUR, 0xa);
    Object_SetProperty(OBJ_ID(OT_FUELDUMP, 0x2d), PROP_EXPLOSION, 0x50);
    for (int32_t i = 0x2f; i <= 0x33; i++)                       // markers 0x19002f..33
        Sound_PlayLoop(OBJ_ID(OT_MARKER, i), 0xee, -1);
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x23;
    gNbWaypoints = m5_nbWaypoints;
    gWaypoints = m5_waypoints;
    Environment_OverrideInteriorFog(0, 0, 0);
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerQuake1);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerQuake2);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerQuake3);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerQuake4);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerQuake5);
    EventType_AddHandler(EV_POD_HOOKED, NULL, M5_OnProbeHooked);
    EventType_AddHandler(EV_DESTROYED, NULL, M5_OnDestroyedStalactite);
    EventType_AddHandler(EV_DESTROYED, NULL, M5_OnDestroyedColumn);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, M5_OnRockAnimDone);
    EventType_AddHandler(EV_COLLISION, NULL, M5_OnCollisionRock3);
    EventType_AddHandler(EV_COLLISION, NULL, M5_OnCollisionRock8);
    EventType_AddHandler(EV_DESTROYED, NULL, M5_OnDestroyed43);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, M5_OnPlaneEntrance);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002fd0
    EventType_AddHandler(EV_DESTROYED, NULL, M5_OnDestroyedDump);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, M5_OnPlaneSections);
    EventType_AddHandler(EV_PICKUP, NULL, M5_OnPickup);
    TexAnimPlay(3, 2, 100);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, M5_OnTriggerWorms);
    MobObj_Wake(M5_MOB_PROBE);
    MobObj_Wake(M5_MOB_FRAME);
    M5_StartFlares();
    Media_AudioPlay(0);
}

// 0x100036d0  LevelStartLevel_Lander (export 0x10001010)
static void M5_StartLander(void)
{
    M5_Setup();
    MobObj_Wake(M5_MOB_LANDER);
    MobObj_Wake(M5_MOB_CAMERA);
    MobObj_Wake(0x1c);              // AmbientFX
    Sound_PlayLoop(MOB_ID(OT_AMBIENTFX, 0x1c), 0x235, -1);
    Sound_PlayLoop(MOB_ID(OT_AMBIENTFX, 0x1c), 0x236, -1);
    MobObj_Wake(0x24);              // AmbientFX
    Object_SetProperty(M5_CAMERA, PROP_TARGET, M5_LANDER);
    Lander_SetHeadLight(M5_LANDER, 0x31c, 0x999);
    MissionScore_Init(M5_LANDER, 400000, 1);                      // 0x10001530
}

// 0x10003770  LevelStartLevel_Flycam (export 0x10001020)
static void M5_StartFlycam(void)
{
    M5_Setup();
    MobObj_Wake(M5_MOB_FLYCAM);
}

// 0x10001050  LevelStopLevel (export 0x10001030): empty
static void M5_Stop(void)
{
}

// 0x10001040
static uint32_t M5_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10001050, return 0. Port: fresh DLL data.
static int32_t M5_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m5_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000d178, .count = 3 * 4, .out = m5_text },
    { 0 },
};

const mission_script_t mission_mars_5 = {
    .name          = "MARS\\5",
    .get_version   = M5_GetVersion,
    .load          = M5_Load,
    .start_lander  = M5_StartLander,
    .start_flycam  = M5_StartFlycam,
    .start_animcam = NULL,
    .stop          = M5_Stop,
    .text          = m5_gametext,
};
