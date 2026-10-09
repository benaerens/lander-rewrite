// MISSIONS\TRAINING\1\MISSIONR.DLL - faithful C port (flight training).
//
// Five timed stages, each introduced by an in-game HTML page (text records 0..4 of GTEXT*.DAT)
// shown for a few seconds while the lander controls are locked through the lander's PROP_FLAGS
// (LF_LOCK_*; gTrainingMission = 1 enables the position locks):
//   1  land on three pads in turn (hold 5 s on each), then hover between two markers for 5 s
//   2  touch nine pads (two of them move; you must be level with them)        records 1
//   3  the same with another nine pads                                         records 2
//   4  fly through eight pairs of gates                                        records 3
//   5  fly through the ring course to the final pad                            records 4
// Stage times (seconds) go to gGame_ScoreData.training[0..4] for the brief.dll score sheet; the
// mission also shows its own temporary score sheet for 0.9 s and ends with LevelEnd(1).
//
// Addresses are the DLL's (image base 0x10000000). Exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010, LevelStartLevel_Flycam = LevelStopLevel 0x10001020 (empty),
// LevelGetVersion 0x10001030.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

#define T1_LANDER      MOB_ID(OT_LANDER, 3)       // 0x01010003
#define T1_CAMERA      MOB_ID(OT_CAMERA, 1)       // 0x01060001
#define T1_ANIM(n)     OBJ_ID(OT_ANIMOBJECT, (n))       // 0x000300nn
#define T1_TRIGGER(n)  OBJ_ID(OT_TRIGGERBOX, (n))       // 0x000400nn
#define T1_PAD(n)      OBJ_ID(OT_ANIMDESTRUCTIBLE, (n)) // 0x000f00nn
#define T1_LOCK_ALL    0x2fcu   // LF_LOCK_CTRL0..3 | LF_LOCK_BUTTONS | LF_LOCK_X | LF_LOCK_Z

// ---- static data (.data) --------------------------------------------------------------------------
// 0x1000b040: stage-4 gate pairs {pair index, which of the two is the entry}
static const int32_t t1_gates[8][2] = {
    { 0, 1 }, { 2, 1 }, { 4, 0 }, { 3, 0 }, { 1, 1 }, { 0, 0 }, { 3, 1 }, { 5, 1 },
};
static const WinRect t1_rectInit = { 20, 100, 512, 300 };   // 0x1000b030 (centred at run time)
// the score sheet's text (English only): title, rule, the stage lines' formats, the average's format
static const char *t1_sheetTitle;        // 0x1000b1b8
static const char *t1_sheetRule;         // 0x1000b1a0
static const char *t1_sheetStage[5];     // 0x1000b16c, b138, b104, b0d0, b09c
static const char *t1_sheetAverage;      // 0x1000b080

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t  introTicks1;     // 0x1000e620
    int32_t  padStage;        // 0x1000e624 stage-1 pads completed (1..3)
    int32_t  holdSeconds;     // 0x1000e628
    int32_t  padCooldown;     // 0x1000e62c
    int32_t  hovering;        // 0x1000e630
    ObjId    door;            // 0x1000e634
    int32_t  padDone[10];     // 0x1000e638 stages 2/3
    int32_t  introTicks5;     // 0x1000e660
    int32_t  gateAnimsDone;   // 0x1000e664
    int32_t  gateIndex;       // 0x1000e668
    int32_t  inEntry;         // 0x1000e66c
    int32_t  introTicks4;     // 0x1000e670
    int32_t  padCount3;       // 0x1000e674
    int32_t  introTicks3;     // 0x1000e678
    int32_t  padCount2;       // 0x1000e67c
    int32_t  introTicks2;     // 0x1000e680
    Window  *textWin;         // 0x1000ec14
    ObjId    entryTrigger;    // 0x1000ec18
    ObjId    ringObj;         // 0x1000ec1c
    int32_t  stageSeconds;    // 0x1000ec20
    ObjId    exitTrigger;     // 0x1000ec24
    int32_t *timesCopy;       // 0x1000ec28 (written, never read)
    int32_t *times;           // 0x1000ec2c = gGame_ScoreData.training
    ObjId    gateA;           // 0x1000ec30
    ObjId    ringTrigger;     // 0x1000ec34
    ObjId    gateB;           // 0x1000ec38 (-1 = pair already passed)
    char     sheet[8][256];   // 0x1000ec40..0x1000f43f score-sheet lines
    char     extra[2][256];   // 0x1000f640, 0x1000f740: printed but never written (empty)
    WinRect  rect;            // 0x1000b030 (.data)
} st;

static void T1_NextGatePair(void);
static void T1_OnAnimFinishedGatesOpen(const LanderEvent *ev);

// 0x10001070  InTime target
static void T1_EndLevel(void)
{
    LevelEnd(1);
}

// 0x10001080  g2dRenderCallback while the score sheet is up
static void T1_RenderScoreSheet(void *otLayer)
{
    (void)otLayer;
    Print(200, 120, 0xff00, st.extra[0]);
    Print(200, 130, 0xff00, st.extra[1]);
    for (int i = 0; i < 8; i++)
        Print(200, 160 + 15 * i, 0xff00, st.sheet[i]);
}

// 0x100010f0  build the sheet (par 60 s per stage; % = 6000 / time, capped at 100 for display,
// uncapped in the average), show it, LevelEnd(1) after 0.9 s
static void T1_ShowScoreSheet(void)
{
    int32_t sum = 0;
    sprintf(st.sheet[0], "%s", t1_sheetTitle);      // (the DLL passes these two as the format: no '%' in them)
    sprintf(st.sheet[1], "%s", t1_sheetRule);
    for (int i = 0; i < 5; i++) {
        int32_t t = st.times[i];
        int32_t pct = 6000 / t;      // idiv: a zero time (never happens) faults in the original
        sum += pct;
        sprintf(st.sheet[2 + i], t1_sheetStage[i], t, 60, pct > 100 ? 100 : pct);
    }
    sprintf(st.sheet[7], t1_sheetAverage, sum / 5);
    g2dRenderCallback = T1_RenderScoreSheet;
    InTime0(900, T1_EndLevel);
}

// 0x10001260  EV_TICK_1S: stage clock
static void T1_SecondTick(const LanderEvent *ev)
{
    (void)ev;
    st.stageSeconds++;
}

// 0x10001270  EV_TRIGGER_ENTER: final pad of stage 5
static void T1_OnTriggerFinalPad(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == T1_TRIGGER(0x37)) {
        st.times[4] = st.stageSeconds;
        T1_ShowScoreSheet();
    }
}

// 0x100012a0  EV_TRIGGER_ENTER: ring course (rings 0xf0026..0xf0036, triggers 0x40015..)
static void T1_OnTriggerRing(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != st.ringTrigger)
        return;
    Sound_PlayOnce(T1_LANDER, 0xeb);
    Object_SetDead(st.ringObj);
    st.ringObj++;
    st.ringTrigger++;
    if ((int32_t)st.ringObj > (int32_t)T1_PAD(0x36)) {
        Sound_PlayLoop(T1_LANDER, 0xeb, 2);
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerFinalPad);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerRing);
        return;
    }
    Object_AdvanceDestroyLevel(st.ringObj, 1);   // highlight the next ring
}

// 0x10001340  EV_TICK_1S: stage-5 page shown for 10 s
static void T1_Stage5IntroTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.introTicks5 != 10)
        return;
    st.stageSeconds = 0;
    WindowDestroy(st.textWin);
    st.textWin = NULL;
    Object_SetProperty(T1_LANDER, PROP_FLAGS, PROPFLAGS_CLEAR | T1_LOCK_ALL);
    st.ringObj = T1_PAD(0x26);
    st.ringTrigger = T1_TRIGGER(0x15);
    Object_AdvanceDestroyLevel(T1_PAD(0x26), 1);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerRing);
    EventType_RemoveHandler(EV_TICK_1S, T1_Stage5IntroTick);
}

static void T1_OpenPage(int32_t record)   // the WindowCreate/centre/SetRect/SetTextFile idiom
{
    st.textWin = WindowCreate(WIN_HTML, NULL);
    MissionLib_CenterRect(&st.rect);                         // 0x10001040
    WindowSetRect(st.textWin, &st.rect);
    WindowSetTextFile(st.textWin, record);
}

// 0x100013d0  EV_ANIM_TURNED: stage-5 door 0x30073 fully open
static void T1_OnDoor5Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != T1_ANIM(0x73))
        return;
    T1_OpenPage(4);
    EventType_AddHandler(EV_TICK_1S, NULL, T1_Stage5IntroTick);
    EventType_RemoveHandler(EV_ANIM_TURNED, T1_OnDoor5Open);
}

// 0x10001450  EV_ANIM_FINISHED: door 0x30073 animation 2 done -> door sounds
static void T1_OnDoor5AnimDone(const LanderEvent *ev)
{
    if (ev->arg[1] != 2)
        return;
    Sound_StopObject(T1_ANIM(0x73));
    Sound_PlayOnce(T1_ANIM(0x73), 0xd0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnDoor5AnimDone);
}

// 0x10001490  InTime target: open the stage-5 door (ping-pong animation 2)
static void T1_OpenDoor5(void)
{
    AnimPlay(T1_ANIM(0x73), 2, 0);
    AnimSetUpdateInterval(T1_ANIM(0x73), 0x14);
    Sound_PlayOnce(T1_ANIM(0x73), 0xce);
    InTime3(840, Sound_PlayLoop, T1_ANIM(0x73), 0xcf, -1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnDoor5AnimDone);
}

// 0x10001500  EV_TRIGGER_ENTER: end of stage 4
static void T1_OnTriggerStage4End(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != T1_TRIGGER(0x7a))
        return;
    st.times[3] = st.stageSeconds;
    Object_SetProperty(T1_LANDER, PROP_FLAGS, T1_LOCK_ALL);
    InTime0(600, T1_OpenDoor5);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, T1_OnDoor5Open);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerStage4End);
}

// 0x10001740  play both gates of the pair again (closing), mark the pair as passed
static void T1_ReplayGates(void)
{
    AnimPlay(st.gateA, 5, 0);
    Sound_PlayOnce(st.gateA, 0x3ad);
    AnimPlay(st.gateB, 5, 0);
    Sound_PlayOnce(st.gateB, 0x3ad);
    st.gateB = OBJ_NONE;
    st.gateAnimsDone = 0;
    st.inEntry = 0;
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnAnimFinishedGatesOpen);
}

// 0x10001570
static void T1_SwapGatesAndReplay(void)
{
    ObjId t = st.gateB;
    st.gateB = st.gateA;
    st.gateA = t;
    T1_ReplayGates();
}

// 0x10001590  EV_TRIGGER_LEAVE: left the entry gate's trigger
static void T1_OnLeaveEntry(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == st.entryTrigger) {
        st.inEntry = 0;
        EventType_RemoveHandler(EV_TRIGGER_LEAVE, T1_OnLeaveEntry);
    }
}

// 0x100015c0  EV_TRIGGER_ENTER: entry trigger, then exit trigger while still inside the entry
static void T1_OnEnterGate(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig == st.entryTrigger) {
        st.inEntry = 1;
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnLeaveEntry);
        return;
    }
    if (trig == st.exitTrigger && st.inEntry != 0) {
        T1_SwapGatesAndReplay();
        EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnEnterGate);
        EventType_RemoveHandler(EV_TRIGGER_LEAVE, T1_OnLeaveEntry);
    }
}

// 0x10001630
static void T1_NextGate(void)
{
    if (st.gateB != OBJ_NONE) {
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnEnterGate);
        return;
    }
    if (++st.gateIndex == 8) {
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerStage4End);
        return;
    }
    T1_NextGatePair();
}

// 0x10001680  EV_ANIM_FINISHED: second gate animation (anim 5) done
static void T1_OnAnimFinishedGateClosed(const LanderEvent *ev)
{
    if (ev->arg[1] != 5)
        return;
    AnimStop((ObjId)ev->arg[0], 0);
    T1_NextGate();
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnAnimFinishedGateClosed);
}

// 0x100016b0  EV_ANIM_FINISHED: both gates of a pair finished anim 5; gate A plays it again
static void T1_OnAnimFinishedGatesOpen(const LanderEvent *ev)
{
    if (ev->arg[1] != 5)
        return;
    st.gateAnimsDone++;
    AnimStop((ObjId)ev->arg[0], 0);
    if ((ObjId)ev->arg[0] == st.gateA) {
        AnimPlay(st.gateA, 5, 0);
        Sound_PlayOnce(st.gateA, 0x3ad);
    }
    if (st.gateAnimsDone == 2) {
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnAnimFinishedGateClosed);
        EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnAnimFinishedGatesOpen);
    }
}

// 0x100017d0  open gate pair t1_gates[gateIndex]: anims 0x30066+2k / 0x30067+2k, their trigger
// boxes are the anim index - 12 (0x4005a+2k ...)
static void T1_NextGatePair(void)
{
    ObjId base = (ObjId)(t1_gates[st.gateIndex][0] * 2) + T1_ANIM(0x66);
    st.gateA = base;
    if (t1_gates[st.gateIndex][1] != 0) {
        st.gateB = base + 1;
    } else {
        st.gateB = base;
        st.gateA = base + 1;
    }
    AnimPlay(st.gateA, 5, 0);
    Sound_PlayOnce(st.gateA, 0x3ad);
    AnimPlay(st.gateB, 5, 0);
    Sound_PlayOnce(st.gateB, 0x3ad);
    st.entryTrigger = st.gateB + 0xfff4;
    st.exitTrigger = st.gateA + 0xfff4;
    st.gateAnimsDone = 0;
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnAnimFinishedGatesOpen);
}

// 0x100018a0  EV_TICK_1S: stage-4 page shown for 10 s
static void T1_Stage4IntroTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.introTicks4 != 10)
        return;
    st.stageSeconds = 0;
    WindowDestroy(st.textWin);
    st.textWin = NULL;
    Object_SetProperty(T1_LANDER, PROP_FLAGS, PROPFLAGS_CLEAR | T1_LOCK_ALL);
    T1_NextGatePair();
    EventType_RemoveHandler(EV_TICK_1S, T1_Stage4IntroTick);
}

// 0x10001900  EV_ANIM_TURNED: stage-4 door 0x3004f open
static void T1_OnDoor4Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != T1_ANIM(0x4f))
        return;
    T1_OpenPage(3);
    EventType_AddHandler(EV_TICK_1S, NULL, T1_Stage4IntroTick);
    EventType_RemoveHandler(EV_ANIM_TURNED, T1_OnDoor4Open);
}

static void T1_OnDoorAnimDone(const LanderEvent *ev);

// 0x10001980  InTime target (1 argument): open a door with animation 1
static void T1_OpenDoor(intptr_t door)
{
    AnimPlay((ObjId)door, 1, 0);
    AnimSetUpdateInterval((ObjId)door, 0xc);
    st.door = (ObjId)door;
    Sound_PlayOnce((ObjId)door, 0xce);
    InTime3(840, Sound_PlayLoop, door, 0xcf, -1);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnDoorAnimDone);
}

// 0x100019f0  EV_ANIM_FINISHED: door animation 1 done
static void T1_OnDoorAnimDone(const LanderEvent *ev)
{
    if (ev->arg[1] != 1)
        return;
    Sound_StopObject(st.door);
    Sound_PlayOnce(st.door, 0xd0);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnDoorAnimDone);
}

// 0x10001a30  EV_TRIGGER_ENTER: end of stage 3
static void T1_OnTriggerStage3End(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != T1_TRIGGER(0x59))
        return;
    st.times[2] = st.stageSeconds;
    Object_SetProperty(T1_LANDER, PROP_FLAGS, 0xb0);
    InTime1(600, T1_OpenDoor, T1_ANIM(0x4f));
    EventType_AddHandler(EV_ANIM_TURNED, NULL, T1_OnDoor4Open);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerStage3End);
}

// 0x10001cf0  1 when the lander's height is within 0x400 of the object's
static int32_t T1_IsLevelWith(ObjId obj)
{
    int32_t landerY = ((const int32_t *)Object_GetProperty(T1_LANDER, PROP_POSITION))[1];
    int32_t top = ((const int32_t *)Object_GetProperty(obj, PROP_POSITION))[1] + 0x400;
    return (landerY > top - 0x800 && landerY < top) ? 1 : 0;
}

// 0x10001aa0  EV_TRIGGER_ENTER: stage-3 pads, triggers 0x40045..0x4004d -> pads 0xf0050..58
static void T1_OnTriggerStage3Pad(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)T1_TRIGGER(0x45) || trig > (int32_t)T1_TRIGGER(0x4d) || st.padDone[trig - (int32_t)T1_TRIGGER(0x45)] != 0)
        return;
    if (trig == (int32_t)T1_TRIGGER(0x48) || trig == (int32_t)T1_TRIGGER(0x49)) {   // the two moving pads
        ObjId pad = (ObjId)trig + 0xb000b;
        if (!T1_IsLevelWith(pad))
            return;
        AnimStop(pad, 0);
    }
    st.padDone[trig - (int32_t)T1_TRIGGER(0x45)] = 1;
    Object_AdvanceDestroyLevel((ObjId)trig + 0xb000b, 1);
    if (++st.padCount3 == 9) {
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerStage3End);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerStage3Pad);
    }
}

// 0x10001b50  EV_TICK_1S: stage-3 page shown for 10 s
static void T1_Stage3IntroTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.introTicks3 != 10)
        return;
    st.stageSeconds = 0;
    WindowDestroy(st.textWin);
    st.textWin = NULL;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerStage3Pad);
    Object_SetProperty(T1_LANDER, PROP_FLAGS, PROPFLAGS_CLEAR | T1_LOCK_ALL);
    Object_SetProperty(T1_LANDER, PROP_FLAGS, 0x20c);
    EventType_RemoveHandler(EV_TICK_1S, T1_Stage3IntroTick);
}

// 0x10001bd0  EV_ANIM_TURNED: stage-3 door 0x3000a open
static void T1_OnDoor3Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != T1_ANIM(0x0a))
        return;
    memset(st.padDone, 0, sizeof st.padDone);
    T1_OpenPage(2);
    AnimPlay(T1_PAD(0x53), 4, 2);
    AnimPlay(T1_PAD(0x54), 4, 2);
    EventType_AddHandler(EV_TICK_1S, NULL, T1_Stage3IntroTick);
    EventType_RemoveHandler(EV_ANIM_TURNED, T1_OnDoor3Open);
}

// 0x10001c80  EV_TRIGGER_ENTER: end of stage 2
static void T1_OnTriggerStage2End(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != T1_TRIGGER(0x14))
        return;
    st.times[1] = st.stageSeconds;
    Object_SetProperty(T1_LANDER, PROP_FLAGS, 0x224);
    InTime1(600, T1_OpenDoor, T1_ANIM(0x0a));
    EventType_AddHandler(EV_ANIM_TURNED, NULL, T1_OnDoor3Open);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerStage2End);
}

// 0x10001d40  EV_TRIGGER_ENTER: stage-2 pads, triggers 0x40000..0x40008 -> pads 0xf000b..13
static void T1_OnTriggerStage2Pad(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)T1_TRIGGER(0x00) || trig > (int32_t)T1_TRIGGER(0x08) || st.padDone[trig - (int32_t)T1_TRIGGER(0x00)] != 0)
        return;
    if (trig == (int32_t)T1_TRIGGER(0x03) || trig == (int32_t)T1_TRIGGER(0x04)) {   // moving pads 0xf000e/0xf000f
        ObjId pad = (ObjId)trig + 0xb000b;
        if (!T1_IsLevelWith(pad))
            return;
        AnimStop(pad, 0);
    }
    st.padDone[trig - (int32_t)T1_TRIGGER(0x00)] = 1;
    Object_AdvanceDestroyLevel((ObjId)trig + 0xb000b, 1);
    if (++st.padCount2 == 9) {
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerStage2End);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnTriggerStage2Pad);
    }
}

// 0x10001df0  EV_TICK_1S: stage-2 page shown for 10 s
static void T1_Stage2IntroTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.introTicks2 != 10)
        return;
    st.stageSeconds = 0;
    WindowDestroy(st.textWin);
    st.textWin = NULL;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerStage2Pad);
    Object_SetProperty(T1_LANDER, PROP_FLAGS, PROPFLAGS_CLEAR | 0x224);
    EventType_RemoveHandler(EV_TICK_1S, T1_Stage2IntroTick);
}

// 0x10001e60  EV_ANIM_FINISHED: stage-2 door 0x3008e (anim 6) done
static void T1_OnDoor2Done(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != T1_ANIM(0x8e))
        return;
    memset(st.padDone, 0, sizeof st.padDone);
    T1_OpenPage(1);
    EventType_AddHandler(EV_TICK_1S, NULL, T1_Stage2IntroTick);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnDoor2Done);
}

// 0x10001ef0  EV_ANIM_FINISHED: hatch 0x3008f open -> stage 1 time, open the stage-2 doors
static void T1_OnHatchOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != T1_ANIM(0x8f))
        return;
    st.times[0] = st.stageSeconds;
    AnimPlay(T1_ANIM(0x8e), 6, 0);
    Sound_PlayOnce(T1_ANIM(0x0a), 0xce);
    InTime3(840, Sound_PlayLoop, T1_ANIM(0x0a), 0xcf, 0x17);
    InTime2(3830, Sound_PlayOnce, T1_ANIM(0x0a), 0xd0);
    AnimPlay(T1_PAD(0x0e), 4, 2);
    AnimPlay(T1_PAD(0x0f), 4, 2);
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnHatchOpen);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnDoor2Done);
}

// 0x10001fc0  EV_TRIGGER_ENTER: end of stage 1 (this handler is never removed)
static void T1_OnTriggerStage1End(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != T1_TRIGGER(0x98))
        return;
    Object_SetProperty(T1_LANDER, PROP_FLAGS, 0x20);
    InTime1(600, T1_OpenDoor, T1_ANIM(0x8f));
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnHatchOpen);
}

// 0x10002010  EV_ANIM_FINISHED: light 0x3007f done -> all fifteen lights 0x3008d..0x3007f flash
static void T1_OnLightsDone(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != T1_ANIM(0x7f))
        return;
    for (ObjId id = T1_ANIM(0x8d); (int32_t)(id - T1_ANIM(0x7f)) >= 0; id--) {
        AnimStop(id, 0);
        AnimPlay(id, 5, 0);
        Sound_PlayOnce(id, 0x3ad);
    }
    EventType_RemoveHandler(EV_ANIM_FINISHED, T1_OnLightsDone);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnTriggerStage1End);
}

static void T1_HoverCheck(const LanderEvent *ev);

// 0x10002090  EV_TICK_1S while hovering between the markers: 5 s -> hover done
static void T1_HoverHoldTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.holdSeconds != 5)
        return;
    EventType_RemoveHandler(EV_TICK_100MS, T1_HoverCheck);
    EventType_RemoveHandler(EV_TICK_1S, T1_HoverHoldTick);
    Object_AdvanceDestroyLevel(T1_PAD(0x96), -1);
    Object_AdvanceDestroyLevel(T1_PAD(0x97), -1);
    AnimStop(T1_ANIM(0x8b), 0);
    AnimStop(T1_ANIM(0x8c), 0);
    AnimStop(T1_ANIM(0x8d), 0);
    AnimStop(T1_ANIM(0x7f), 0);
    AnimPlay(T1_ANIM(0x8b), 5, 0);
    Sound_PlayOnce(T1_ANIM(0x8b), 0x3ad);
    AnimPlay(T1_ANIM(0x8c), 5, 0);
    Sound_PlayOnce(T1_ANIM(0x8c), 0x3ad);
    AnimPlay(T1_ANIM(0x8d), 5, 0);
    Sound_PlayOnce(T1_ANIM(0x8d), 0x3ad);
    AnimPlay(T1_ANIM(0x7f), 5, 0);
    Sound_PlayOnce(T1_ANIM(0x7f), 0x3ad);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, T1_OnLightsDone);
}

// 0x100021b0  EV_TICK_100MS: is the lander between the two hover markers 0xf0096 / 0xf0097?
static void T1_HoverCheck(const LanderEvent *ev)
{
    (void)ev;
    int32_t top = ((const int32_t *)Object_GetProperty(T1_PAD(0x97), PROP_POSITION))[1];
    int32_t bottom = ((const int32_t *)Object_GetProperty(T1_PAD(0x96), PROP_POSITION))[1];
    int32_t y = ((const int32_t *)Object_GetProperty(T1_LANDER, PROP_POSITION))[1];
    if (y < top && y > bottom) {
        if (st.hovering == 0) {
            st.hovering = 1;
            EventType_AddHandler(EV_TICK_1S, NULL, T1_HoverHoldTick);
        }
        return;
    }
    if (st.hovering == 1) {
        EventType_RemoveHandler(EV_TICK_1S, T1_HoverHoldTick);
        st.hovering = 0;
        st.holdSeconds = 0;
    }
}

// 0x10002250  InTime target
static void T1_ClearPadCooldown(void)
{
    st.padCooldown = 0;
}

static void T1_OnLeavePad(const LanderEvent *ev);
static void T1_OnEnterPad(const LanderEvent *ev);

// 0x10002260  EV_TICK_1S while on the current landing pad: 5 s -> next pad (3 pads, then hover)
static void T1_PadHoldTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.holdSeconds != 5)
        return;
    EventType_RemoveHandler(EV_TICK_1S, T1_PadHoldTick);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, T1_OnLeavePad);
    st.padStage++;
    st.padCooldown = 1;
    st.holdSeconds = 0;
    if (st.padStage == 1) {
        Object_AdvanceDestroyLevel(T1_PAD(0x90), -1);
        Object_AdvanceDestroyLevel(T1_PAD(0x91), -1);
        InTime3(300, AnimPlay, T1_ANIM(0x82), 5, 0);
        InTime3(600, AnimPlay, T1_ANIM(0x83), 5, 0);
        InTime3(900, AnimPlay, T1_ANIM(0x84), 5, 0);
        InTime2(300, Sound_PlayOnce, T1_ANIM(0x82), 0x3ad);
        InTime2(600, Sound_PlayOnce, T1_ANIM(0x83), 0x3ad);
        InTime2(900, Sound_PlayOnce, T1_ANIM(0x84), 0x3ad);
        InTime2(1200, Object_AdvanceDestroyLevel, T1_PAD(0x92), 1);
        InTime2(1200, Object_AdvanceDestroyLevel, T1_PAD(0x93), 1);
        Sound_PlayOnce(T1_PAD(0x92), 0x3ac);
        InTime0(1200, T1_ClearPadCooldown);
    }
    if (st.padStage == 2) {
        Object_AdvanceDestroyLevel(T1_PAD(0x92), -1);
        Object_AdvanceDestroyLevel(T1_PAD(0x93), -1);
        InTime3(300, AnimPlay, T1_ANIM(0x85), 5, 0);
        InTime3(600, AnimPlay, T1_ANIM(0x86), 5, 0);
        InTime3(900, AnimPlay, T1_ANIM(0x87), 5, 0);
        InTime2(300, Sound_PlayOnce, T1_ANIM(0x85), 0x3ad);
        InTime2(600, Sound_PlayOnce, T1_ANIM(0x85), 0x3ad);   // sic: 0x30085 three times
        InTime2(900, Sound_PlayOnce, T1_ANIM(0x85), 0x3ad);
        InTime2(1200, Object_AdvanceDestroyLevel, T1_PAD(0x94), 1);
        InTime2(1200, Object_AdvanceDestroyLevel, T1_PAD(0x95), 1);
        Sound_PlayOnce(T1_PAD(0x94), 0x3ac);
        InTime0(1200, T1_ClearPadCooldown);
    }
    if (st.padStage == 3) {
        Object_AdvanceDestroyLevel(T1_PAD(0x94), -1);
        Object_AdvanceDestroyLevel(T1_PAD(0x95), -1);
        InTime3(300, AnimPlay, T1_ANIM(0x88), 5, 0);
        InTime3(600, AnimPlay, T1_ANIM(0x89), 5, 0);
        InTime3(900, AnimPlay, T1_ANIM(0x8a), 5, 0);
        InTime2(300, Sound_PlayOnce, T1_ANIM(0x88), 0x3ad);
        InTime2(600, Sound_PlayOnce, T1_ANIM(0x89), 0x3ad);
        InTime2(900, Sound_PlayOnce, T1_ANIM(0x8a), 0x3ad);
        InTime2(1200, Object_AdvanceDestroyLevel, T1_PAD(0x96), 1);
        InTime2(1200, Object_AdvanceDestroyLevel, T1_PAD(0x97), 1);
        InTime3(1200, AnimPlay, T1_PAD(0x96), 3, 2);
        InTime3(1200, AnimPlay, T1_PAD(0x97), 3, 2);
        Sound_PlayOnce(T1_PAD(0x96), 0x3ac);
        EventType_RemoveHandler(EV_TRIGGER_ENTER, T1_OnEnterPad);
        st.holdSeconds = 0;
        EventType_AddHandler(EV_TICK_100MS, NULL, T1_HoverCheck);
    }
}

// 0x10002600  EV_TRIGGER_LEAVE: left the current pad's trigger (0x4007c + padStage)
static void T1_OnLeavePad(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != (ObjId)(st.padStage + (int32_t)T1_TRIGGER(0x7b)))
        return;
    EventType_RemoveHandler(EV_TICK_1S, T1_PadHoldTick);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, T1_OnLeavePad);
    st.holdSeconds = 0;
}

// 0x10002640  EV_TRIGGER_ENTER: on the current pad
static void T1_OnEnterPad(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != (ObjId)(st.padStage + (int32_t)T1_TRIGGER(0x7b)) || st.padCooldown != 0)
        return;
    EventType_AddHandler(EV_TICK_1S, NULL, T1_PadHoldTick);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, T1_OnLeavePad);
}

// 0x10002690  EV_TICK_1S: first page shown for 8 s, then stage 1 starts
static void T1_Stage1IntroTick(const LanderEvent *ev)
{
    (void)ev;
    if (++st.introTicks1 != 8)
        return;
    st.stageSeconds = 0;
    WindowDestroy(st.textWin);
    st.textWin = NULL;
    AnimPlay(T1_ANIM(0x7f), 5, 0);
    InTime3(300, AnimPlay, T1_ANIM(0x80), 5, 0);
    InTime3(600, AnimPlay, T1_ANIM(0x81), 5, 0);
    InTime2(300, Sound_PlayOnce, T1_ANIM(0x80), 0x3ad);
    InTime2(600, Sound_PlayOnce, T1_ANIM(0x81), 0x3ad);
    InTime2(900, Object_AdvanceDestroyLevel, T1_PAD(0x90), 1);
    InTime2(900, Object_AdvanceDestroyLevel, T1_PAD(0x91), 1);
    Sound_PlayOnce(T1_PAD(0x90), 0x3ac);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, T1_OnEnterPad);
    Object_SetProperty(T1_LANDER, PROP_FLAGS, PROPFLAGS_CLEAR | 0x20);
    EventType_RemoveHandler(EV_TICK_1S, T1_Stage1IntroTick);
}

// 0x100027c0  set-up
static void T1_Setup(void)
{
    SetGravity(10);
    gAirResistance = 0x1c;
    gMainThrottlePower = 0x19;
    gTrainingMission = 1;
    st.timesCopy = gGame_ScoreData.training;
    st.times = gGame_ScoreData.training;
    EventType_AddHandler(EV_TICK_1S, NULL, T1_Stage1IntroTick);
    EventType_AddHandler(EV_TICK_1S, NULL, T1_SecondTick);
    for (int32_t id = (int32_t)T1_ANIM(0x7f); id <= (int32_t)T1_ANIM(0x8d); id++)
        AnimSetUpdateInterval((ObjId)id, 2);
    for (int32_t id = (int32_t)T1_ANIM(0x66); id <= (int32_t)T1_ANIM(0x71); id++)
        AnimSetUpdateInterval((ObjId)id, 2);
    Object_SetProperty(T1_LANDER, PROP_FLAGS, T1_LOCK_ALL);
    T1_OpenPage(0);
    Media_AudioPlay(0);
}

// 0x100028d0  LevelStartLevel_Lander (export 0x10001010)
static void T1_StartLander(void)
{
    MobObj_Wake(3);                 // the lander
    MobObj_Wake(1);                 // the camera
    Object_SetProperty(T1_CAMERA, PROP_TARGET, T1_LANDER);
    T1_Setup();
}

// 0x10002910  LevelStartLevel_Flycam and LevelStopLevel (export 0x10001020): empty
static void T1_Nothing(void)
{
}

// 0x10001030
static uint32_t T1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x10002910, return 0. Port: fresh DLL data.
static int32_t T1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.rect = t1_rectInit;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t1_gametext[] = {
    { .kind = GT_STRINGS, .va = 0x1000b1b8, .count = 1, .out = &t1_sheetTitle },
    { .kind = GT_STRINGS, .va = 0x1000b1a0, .count = 1, .out = &t1_sheetRule },
    { .kind = GT_STRINGS, .va = 0x1000b16c, .count = 1, .out = &t1_sheetStage[0] },
    { .kind = GT_STRINGS, .va = 0x1000b138, .count = 1, .out = &t1_sheetStage[1] },
    { .kind = GT_STRINGS, .va = 0x1000b104, .count = 1, .out = &t1_sheetStage[2] },
    { .kind = GT_STRINGS, .va = 0x1000b0d0, .count = 1, .out = &t1_sheetStage[3] },
    { .kind = GT_STRINGS, .va = 0x1000b09c, .count = 1, .out = &t1_sheetStage[4] },
    { .kind = GT_STRINGS, .va = 0x1000b080, .count = 1, .out = &t1_sheetAverage },
    { 0 },
};

const mission_script_t mission_training_1 = {
    .name          = "TRAINING\\1",
    .get_version   = T1_GetVersion,
    .load          = T1_Load,
    .start_lander  = T1_StartLander,
    .start_flycam  = T1_Nothing,
    .start_animcam = NULL,
    .stop          = T1_Nothing,
    .text          = t1_gametext,
};
