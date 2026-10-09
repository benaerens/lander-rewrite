// MISSIONS\TRAINING\2\MISSIONR.DLL - faithful C port (the shooting range).
//
// 200 seconds to score 100 points: five ducks (mob AnimDestructibles 3..7, 5 points each),
// six pop-up targets (0xf0014..19, 10 each, shown one at a time for 10 s), five barrels
// (Destructibles 0x80003..07, 1 each) and a tank (mob 0x14, 10 points) that appears when all five
// switches 0xa000f..13 are shot on within 5 s of each other. The score is shown on three digit
// drums (0x30000 hundreds, 0x30001 tens, 0x30002 units) turned by the AnimSeq helper. Exactly 100
// shown -> page 1 and LevelEnd(1) five seconds later; time out -> page 2 and LevelEnd(0).
// Hit counts go to gGame_ScoreData.training[0..3] (totals in [4..7]) for brief.dll.
//
// Addresses are the DLL's (image base 0x10000000). Exports: LevelLoadLevel 0x100011d0,
// LevelStartLevel_Lander 0x100011e0, LevelStartLevel_Flycam 0x100011f0, LevelStopLevel
// 0x10001200, LevelGetVersion 0x10001210. AnimSeq (0x10001000-0x100011c1) is in mission_lib.c.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

#define T2_LANDER       MOB_ID(OT_LANDER, 14)            // 0x0101000e
#define T2_CAMERA       MOB_ID(OT_CAMERA, 1)             // 0x01060001
#define T2_CAMERA2      MOB_ID(OT_CAMERA, 2)             // 0x01060002 end-of-mission view
#define T2_DUCK_FIRST   MOB_ID(OT_ANIMDESTRUCTIBLE, 3)   // 0x010f0003..0x010f0007
#define T2_DUCK_LAST    MOB_ID(OT_ANIMDESTRUCTIBLE, 7)
#define T2_TANK         MOB_ID(OT_ANIMDESTRUCTIBLE, 0x14) // 0x010f0014
#define T2_BARREL_FIRST OBJ_ID(OT_DESTRUCTIBLE, 3)       // 0x00080003..0x00080007
#define T2_BARREL_LAST  OBJ_ID(OT_DESTRUCTIBLE, 7)
#define T2_TARGET_FIRST OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x14) // 0x000f0014..0x000f0019
#define T2_TARGET_LAST  OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x19)
#define T2_SWITCH_FIRST OBJ_ID(OT_SWITCH, 0x0f)          // 0x000a000f..0x000a0013
#define T2_SWITCH_LAST  OBJ_ID(OT_SWITCH, 0x13)
#define T2_DRUM_HUNDREDS OBJ_ID(OT_ANIMOBJECT, 0)        // 0x00030000
#define T2_DRUM_TENS     OBJ_ID(OT_ANIMOBJECT, 1)        // 0x00030001
#define T2_DRUM_UNITS    OBJ_ID(OT_ANIMOBJECT, 2)        // 0x00030002
// switch k (0..4) on: lamp mob 0xf+k goes to sleep, lamp mob 9+k wakes

// ---- initial .data image (the DLL patches some of it at run time) ---------------------------------
typedef struct T2Data {
    uint8_t  seqUnits[8];     // 0x1000a030 {anim 5, repeat 1, flags 0x80}, end; repeat patched
    uint8_t  seqTens[8];      // 0x1000a038
    int32_t  timeLeft;        // 0x1000a040 200 s; reused as the outro counter
    int32_t  introSeconds;    // 0x1000a044 10
    WinRect  rect;            // 0x1000a048 page rect, centred at run time
    char     getReady[128];   // 0x1000a058 never used
    char     timerText[128];  // 0x1000a0d8 sprintf buffer (with an initial text)
} T2Data;
static T2Data t2_dataInit = {
    { 5, 1, 0x80, 0xff, 0, 0, 0, 0 },
    { 5, 1, 0x80, 0xff, 0, 0, 0, 0 },
    200, 10,
    { 20, 100, 512, 300 },
    "", "",                   // the two texts are read from the DLL (t2_gametext)
};
// 0x1000a158: the countdown line's format per language, 0x1c bytes apart
static const char *t2_timeFormat[3];

static struct {
    T2Data   d;
    int32_t  score;          // 0x1000d600
    int32_t  shownScore;     // 0x1000d604
    int32_t  drumsBusy;      // 0x1000d608
    int32_t  targetTicks;    // 0x1000d60c
    int32_t  targetIndex;    // 0x1000d610
    int32_t  switchesOn;     // 0x1000d614
    int32_t  switchesMax;    // 0x1000d618
    int32_t  switchIdle;     // 0x1000d61c seconds since the last switch went on
    int32_t  won;            // 0x1000d620
    int32_t  targetHit[6];   // 0x1000d628
    Window  *timerWin;       // 0x1000d644
    Window  *textWin;        // 0x1000dbd4
    int32_t *counts;         // 0x1000dbd8 = gGame_ScoreData.training
} st;

static void T2_OutroTick(const LanderEvent *ev);

static void T2_OpenPage(int32_t record)
{
    st.textWin = WindowCreate(WIN_HTML, NULL);
    MissionLib_CenterRect(&st.d.rect);                      // 0x10001220
    WindowSetRect(st.textWin, &st.d.rect);
    WindowSetTextFile(st.textWin, record);
}

// 0x10001250
static void T2_EndLevel(void)
{
    if (st.won == 1)
        LevelEnd(1);
    else
        LevelEnd(0);
}

// 0x10001280  EV_TICK_1S after the result: at 0 switch to the second camera, at 5 end
static void T2_OutroTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.d.timeLeft == 0) {
        MobObj_Sleep(1);                                                   // camera 1
        Object_SetProperty(T2_LANDER, PROP_FLAGS, PROPFLAGS_CLEAR | LF_LOCAL_PLAYER);
        MobObj_Sleep(14);                                                  // the lander
        MobObj_Wake(2);                                                    // camera 2
        Object_SetProperty(T2_CAMERA2, PROP_FLAGS, PROPFLAGS_CLEAR);
        Object_SetProperty(T2_CAMERA2, PROP_FLAGS, 0xe4);
    }
    if (st.d.timeLeft == 5)
        T2_EndLevel();
    st.d.timeLeft++;
}

// 0x10001300  EV_TICK_1S: win check and the countdown line
static void T2_CountdownTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.shownScore == 100) {
        EventType_RemoveHandler(EV_TICK_1S, T2_CountdownTick);
        WindowDestroy(st.timerWin);
        Object_SetProperty(T2_LANDER, PROP_FLAGS, LF_LOCK_BUTTONS);
        T2_OpenPage(1);
        st.won = 1;
        st.d.timeLeft = 0;
        EventType_AddHandler(EV_TICK_1S, NULL, T2_OutroTick);
        return;
    }
    int32_t t = st.d.timeLeft;
    if (t == 0) {
        WindowDestroy(st.timerWin);
        st.won = 0;
        T2_OpenPage(2);
        EventType_AddHandler(EV_TICK_1S, NULL, T2_OutroTick);
        EventType_RemoveHandler(EV_TICK_1S, T2_CountdownTick);
        t = st.d.timeLeft;
    }
    if (t <= 200) {
        if (gLanguage >= 0 && gLanguage <= 2)               // (a switch on gLanguage)
            sprintf(st.d.timerText, t2_timeFormat[gLanguage], t);
        // after a time-out the timer window has just been destroyed: the original writes into
        // the freed window anyway (the rewrite's WindowSetText must tolerate a closed window)
        WindowSetText(st.timerWin, st.d.timerText);
        t = st.d.timeLeft;
    }
    st.d.timeLeft = t - 1;
}

// 0x100014b0  EV_DESTROYED: a shot duck keeps turning (anim 2)
static void T2_OnDuckDestroyed(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)T2_DUCK_FIRST && id <= (int32_t)T2_DUCK_LAST) {
        AnimStop((ObjId)id, 0);
        AnimPlay((ObjId)id, 2, 0x80);
    }
}

// 0x100014f0  EV_DESTROYED: points and hit counts
static void T2_OnDestroyedScore(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)T2_DUCK_FIRST && id <= (int32_t)T2_DUCK_LAST) {
        st.score += 5;
        st.counts[0]++;
        return;
    }
    if (id == (int32_t)T2_TANK) {
        st.score += 10;
        st.counts[3]++;
        return;
    }
    if (id >= (int32_t)T2_BARREL_FIRST && id <= (int32_t)T2_BARREL_LAST) {
        st.score += 1;
        st.counts[2]++;
        return;
    }
    if (id >= (int32_t)T2_TARGET_FIRST && id <= (int32_t)T2_TARGET_LAST) {
        st.score += 10;
        st.counts[1]++;
        st.targetHit[id - (int32_t)T2_TARGET_FIRST] = 1;
    }
}

// 0x10001590  EV_ANIMSEQ_DONE {object, sequence}: a digit drum stopped
static void T2_OnDrumDone(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id == T2_DRUM_HUNDREDS || id == T2_DRUM_TENS || id == T2_DRUM_UNITS)
        st.drumsBusy--;
}

// 0x100015c0  EV_TICK_1S: turn the drums to the new score once they are idle. The hundreds drum
// is turned with a plain AnimPlay, so it never reports done and the display freezes afterwards.
static void T2_ScoreDisplayTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.drumsBusy != 0 || st.score <= st.shownScore)
        return;
    int32_t newRest = st.score % 100, oldRest = st.shownScore % 100;
    int32_t dHundreds = st.score / 100 - st.shownScore / 100;
    int32_t dTens = newRest / 10 - oldRest / 10;
    int32_t dUnits = newRest % 10 - oldRest % 10;
    if (dTens < 0)
        dTens += 10;
    if (dUnits < 0)
        dUnits += 10;
    if (dHundreds == 1) {
        AnimPlay(T2_DRUM_HUNDREDS, 5, 0x80);
        Sound_PlayChain(T2_DRUM_HUNDREDS, 2, 0x3ae, 0x3b0);
        st.drumsBusy++;
    }
    if (dTens > 0) {
        st.d.seqTens[1] = (uint8_t)dTens;
        AnimSeq_Start(T2_DRUM_TENS, st.d.seqTens, 0);
        Sound_PlayOnce(T2_DRUM_TENS, 0x3ae);
        st.drumsBusy++;
    }
    if (dUnits > 0) {
        st.d.seqUnits[1] = (uint8_t)dUnits;
        AnimSeq_Start(T2_DRUM_UNITS, st.d.seqUnits, 0);
        Sound_PlayOnce(T2_DRUM_UNITS, 0x3ae);
        st.drumsBusy++;
    }
    st.shownScore = st.score;
}

// 0x10001760  EV_TICK_1S: show the pop-up targets one at a time, 10 s each, skipping hit ones
static void T2_TargetCycleTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.targetTicks == 0) {
        AnimPlay(T2_TARGET_FIRST + (ObjId)st.targetIndex, 3, 0);
        Sound_PlayOnce(T2_TARGET_FIRST + (ObjId)st.targetIndex, 0x3b2);
    }
    if (++st.targetTicks != 10)
        return;
    AnimStop(T2_TARGET_FIRST + (ObjId)st.targetIndex, 0);
    AnimPlay(T2_TARGET_FIRST + (ObjId)st.targetIndex, 4, 0);
    Sound_PlayOnce(T2_TARGET_FIRST + (ObjId)st.targetIndex, 0x3b2);
    int32_t from = st.targetIndex + 1;
    st.targetTicks = 0;
    st.targetIndex = from;
    int32_t i = from;
    if (i < 6) {
        for (; i < 6; i++) {
            if (st.targetHit[i] == 0) {
                st.targetIndex = i;
                break;
            }
        }
        if (i != 6)
            return;
    } else if (i != 6) {
        return;
    }
    // past the last target: wrap around to the first unhit one below 'from'
    for (i = 0; i < from; i++) {
        if (st.targetHit[i] == 0) {
            st.targetIndex = i;
            return;
        }
    }
    EventType_RemoveHandler(EV_TICK_1S, T2_TargetCycleTick);
}

// 0x10001890  EV_SWITCH, after the tank appeared: switches turned off are turned on again
static void T2_OnSwitchKeepOn(const LanderEvent *ev)
{
    if (ev->arg[1] == 0)
        Object_SetProperty((ObjId)ev->arg[0], PROP_STATE, 1);
}

// 0x100018b0  EV_TICK_1S: 5 s after the last switch went on, reset the switches that are on
static void T2_SwitchTimeoutTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.switchIdle != 5 || st.switchesMax < st.switchesOn)
        return;
    for (int32_t id = (int32_t)T2_SWITCH_LAST; id - (int32_t)T2_SWITCH_FIRST >= 0; id--) {
        if (Object_GetProperty((ObjId)id, PROP_STATE) == 1) {
            int32_t k = id - (int32_t)OBJ_ID(OT_SWITCH, 0);   // 0x0f..0x13
            MobObj_Sleep((k - 6) & 0xffff);                    // "on" lamp 9..0xd
            MobObj_Wake(k & 0xffff);                           // "off" lamp 0xf..0x13
            Object_SetProperty((ObjId)id, PROP_STATE, 0);
        }
    }
    st.switchIdle = 0;
    st.switchesOn = 0;
    st.switchesMax = 0;
    EventType_RemoveHandler(EV_TICK_1S, T2_SwitchTimeoutTick);
}

// 0x10001960  EV_DESTROYED: explosion sound (ducks quack instead)
static void T2_OnDestroyedSound(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    int32_t size = (int32_t)ev->arg[2];
    Sound_StopObject((ObjId)id);
    if (id >= (int32_t)T2_DUCK_FIRST && id <= (int32_t)T2_DUCK_LAST) {
        Sound_PlayOnce((ObjId)id, 0x3af);
        return;
    }
    if (size > 99)
        Sound_PlayOnce((ObjId)id, 0x72);
    else if (size > 79)
        Sound_PlayOnce((ObjId)id, 0x71);
    else if (size > 59)
        Sound_PlayOnce((ObjId)id, 0x70);
    else if (size > 39)
        Sound_PlayOnce((ObjId)id, 0x6f);
    else if (size > 0)
        Sound_PlayOnce((ObjId)id, 0x6e);
}

// 0x10001a00  EV_SWITCH {switch, state, shooter}: lamps; five on -> the tank appears
static void T2_OnSwitch(const LanderEvent *ev)
{
    int32_t k = (int32_t)ev->arg[0] - (int32_t)T2_SWITCH_FIRST;     // 0..4
    if (ev->arg[1] == 1) {
        MobObj_Sleep((k + 0xf) & 0xffff);
        MobObj_Wake((k + 9) & 0xffff);
        st.switchesOn++;
        st.switchesMax = st.switchesOn;
        if (st.switchesOn == 5) {
            Sound_PlayOnce(T2_TANK, 0x3b1);
            MobObj_Wake(0x14);
            AnimPlay(T2_TANK, 6, 0xe1);
            Sound_PlayLoop(T2_TANK, 0x8d, -1);
            AnimSetUpdateInterval(T2_TANK, 0x14);
            EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitchKeepOn);
            EventType_RemoveHandler(EV_SWITCH, T2_OnSwitch);
        }
        // (re)start the 5-s timeout; when it has not ticked yet (switchIdle == 0) a second copy
        // of the handler is registered, as in the original
        if (st.switchIdle > 0)
            EventType_RemoveHandler(EV_TICK_1S, T2_SwitchTimeoutTick);
        st.switchIdle = 0;
        EventType_AddHandler(EV_TICK_1S, NULL, T2_SwitchTimeoutTick);
        return;
    }
    if (ev->arg[1] == 0) {
        MobObj_Sleep((k + 9) & 0xffff);
        MobObj_Wake((k + 0xf) & 0xffff);
        st.switchesOn--;
    }
}

// 0x10001b40  EV_TICK_1S: intro page for 10 s, then the countdown line
static void T2_IntroTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.d.introSeconds == 0) {
        EventType_RemoveHandler(EV_TICK_1S, T2_IntroTick);
        WindowDestroy(st.textWin);
        st.timerWin = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
        WindowSetSize(st.timerWin, 20, 20, 0x15e, 12);
        EventType_AddHandler(EV_TICK_1S, NULL, T2_CountdownTick);
    }
    st.d.introSeconds--;
}

// 0x10001bb0  set-up
static void T2_Setup(void)
{
    SetGravity(10);
    gAirResistance = 0x1c;
    gMainThrottlePower = 0x19;
    st.counts = gGame_ScoreData.training;
    st.counts[4] = 5;           // ducks
    st.counts[5] = 6;           // targets
    st.counts[6] = 5;           // barrels
    st.counts[7] = 1;           // tank
    AnimSeq_Init();                                             // 0x10001190
    EventType_AddHandler(EV_DESTROYED, NULL, T2_OnDestroyedSound);
    EventType_AddHandler(EV_DESTROYED, NULL, T2_OnDuckDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, T2_OnDestroyedScore);
    EventType_AddHandler(EV_TICK_1S, NULL, T2_ScoreDisplayTick);
    EventType_AddHandler(EV_ANIMSEQ_DONE, NULL, T2_OnDrumDone);
    EventType_AddHandler(EV_TICK_1S, NULL, T2_TargetCycleTick);
    EventType_AddHandler(EV_SWITCH, NULL, T2_OnSwitch);
    EventType_AddHandler(EV_TICK_1S, NULL, T2_IntroTick);
    for (int32_t id = (int32_t)T2_DUCK_FIRST; id <= (int32_t)T2_DUCK_LAST; id++)
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0);
    MobObj_Wake(3);
    MobObj_Wake(4);
    MobObj_Wake(5);
    MobObj_Wake(6);
    MobObj_Wake(7);
    MobObj_Wake(0xf);
    MobObj_Wake(0x10);
    MobObj_Wake(0x11);
    MobObj_Wake(0x12);
    MobObj_Wake(0x13);
    // switch PROP_FLAGS is stored inverted (0x458fe0): this clears the reported 0x8000 flag
    for (int32_t id = (int32_t)T2_SWITCH_FIRST; id <= (int32_t)T2_SWITCH_LAST; id++)
        Object_SetProperty((ObjId)id, PROP_FLAGS, 0x80008000);
    for (int32_t k = 0; k < 5; k++) {           // ducks start every 3 s
        InTime3(3000 * (k + 1), AnimPlay, T2_DUCK_FIRST + (ObjId)k, 1, 1);
        AnimSetUpdateInterval(T2_DUCK_FIRST + (ObjId)k, 0x32);
    }
    for (int32_t k = 0; k < 5; k++)
        InTime3(3000 * (k + 1), Sound_PlayLoop, T2_DUCK_FIRST + (ObjId)k, 0x3b3, -1);
    T2_OpenPage(0);
    Media_AudioPlay(0);
}

// 0x10001f00  LevelStartLevel_Lander (export 0x100011e0)
static void T2_StartLander(void)
{
    T2_Setup();
    MobObj_Wake(14);            // the lander
    MobObj_Wake(1);             // camera 1
    Object_SetProperty(T2_CAMERA, PROP_TARGET, T2_LANDER);
}

// 0x10001f40  LevelStartLevel_Flycam (export 0x100011f0)
static void T2_StartFlycam(void)
{
    T2_Setup();
    MobObj_Wake(8);
}

// 0x10001f60  LevelStopLevel (export 0x10001200): empty
static void T2_Stop(void)
{
}

// 0x10001210
static uint32_t T2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x100011d0  two calls of 0x10001f60, return 0. Port: fresh DLL data.
static int32_t T2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.d = t2_dataInit;
    AnimSeq_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t2_gametext[] = {
    { .kind = GT_BYTES, .va = 0x1000a058, .count = sizeof t2_dataInit.getReady, .out = t2_dataInit.getReady },
    { .kind = GT_BYTES, .va = 0x1000a0d8, .count = sizeof t2_dataInit.timerText, .out = t2_dataInit.timerText },
    { .kind = GT_STRINGS, .va = 0x1000a158, .count = 3, .out = t2_timeFormat, .stride = 0x1c },
    { 0 },
};

const mission_script_t mission_training_2 = {
    .name          = "TRAINING\\2",
    .get_version   = T2_GetVersion,
    .load          = T2_Load,
    .start_lander  = T2_StartLander,
    .start_flycam  = T2_StartFlycam,
    .start_animcam = NULL,
    .stop          = T2_Stop,
    .text          = t2_gametext,
};
