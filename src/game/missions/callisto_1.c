// MISSIONS\CALLISTO\1\MISSIONR.DLL - faithful C port.
//
// Mission: find the power plant at waypoint 1, get in (proximity doors, a tractor-beam pod that
// keeps a door open on its pad, a shield-draining energy field, a crusher, switch doors), find the
// reactor core and shoot its four switches (0xa00ac..af) to pull the fuel rods. That starts a 360 s
// meltdown countdown with chase lights; climb above y = 10000 for success (+60000 cash). If the
// countdown runs out while the lander is still inside (trigger pair 0x400b0/b1 decides), the lander
// is destroyed and the plant lights flash. Par time 15 s (sic), 8 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10003f10), LevelStartLevel_Flycam and LevelStopLevel
// 0x10001020 (-> the empty 0x10003f60: the fly-cam start does no set-up at all), LevelGetVersion
// 0x10001030. The score library (0x10001040-0x100015e4) is in mission_lib.c; the destroyed-sound
// handler 0x10003000 is MissionLib_OnDestroyedSound.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define C1_LANDER        MOB_ID(OT_LANDER, 0x0b)         // 0x0101000b
#define C1_CAMERA        MOB_ID(OT_CAMERA, 1)            // 0x01060001
#define C1_POD           MOB_ID(OT_POD, 2)               // 0x010c0002 keeps door 0x30086 open on pad 0x40085
#define C1_REACTOR_ARM   MOB_ID(OT_ANIMOBJECT, 0x0c)     // 0x0103000c
#define C1_ROD_A         MOB_ID(OT_ANIMOBJECT, 5)        // 0x01030005 fuel-rod machinery
#define C1_ROD_B         MOB_ID(OT_ANIMOBJECT, 6)        // 0x01030006
#define C1_ROD_C         MOB_ID(OT_ANIMOBJECT, 7)        // 0x01030007
#define C1_ZAP_A         MOB_ID(OT_ANIMOBJECT, 0x11)     // 0x01030011 energy field in shaft 0x40083
#define C1_ZAP_B         MOB_ID(OT_ANIMOBJECT, 0x12)     // 0x01030012
#define C1_ZAP_C         MOB_ID(OT_ANIMOBJECT, 0x13)     // 0x01030013
#define C1_ALARM         MOB_ID(OT_AMBIENTFX, 0x15)      // 0x01150015 countdown siren
#define C1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))      // 0x000300nn
#define C1_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))      // 0x000400nn
#define C1_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))          // 0x000a00nn
#define C1_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))          // 0x001300nn
#define C1_FORCEBOX(n)   OBJ_ID(OT_FORCEBOX, (n))        // 0x001000nn
#define C1_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n)) // 0x000d00nn
#define C1_CRUSHER       OBJ_ID(OT_ANIMDESTRUCTIBLE, 0xc3) // 0x000f00c3
#define C1_GEN_A         OBJ_ID(OT_DESTRUCTIBLE, 0x66)   // 0x00080066 crusher generators
#define C1_GEN_B         OBJ_ID(OT_DESTRUCTIBLE, 0x69)   // 0x00080069
#define C1_MOB_CAMERA    1
#define C1_MOB_POD       2
#define C1_MOB_LANDER    0x0b
#define C1_MOB_REACTOR_ARM 0x0c

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  c1_countdownStart = 360;           // 0x1000c160 (seconds, counts down)
static const int32_t  c1_nbWaypoints = 1;                // 0x1000c164
static const Waypoint c1_waypoints[1] = {                // 0x1000c168
    { 12800, 16000 },
};
static const TurretType c1_turretType5 = {               // 0x1000c170 -> gTurretTypes[5]
    { 0, 0, 145, 0 }, 0x01, 0, 1, 0, 70, 0x20, 1024, 6000, 0
};
static const TurretType c1_turretType3 = {               // 0x1000c190 -> gTurretTypes[3]
    { 0, 18, 158, 0 }, 0x05, 0, 3, 5, 100, 0x18, 800, 6000, 0
};
static const TurretType c1_turretType8 = {               // 0x1000c1b0 -> gTurretTypes[8]
    { 0, 0, 544, 0 }, 0x00, 0, 1, 0, 85, 0x18, 800, 6000, 0
};
// 0x1000c1cc / 0x1000c1d0: chase-light step and row, both start at -1 (.data)
// 0x1000c1d8: delay (s) of each chase-light step
static const int32_t c1_chaseDelay[8] = { 3, 2, 2, 4, 3, 4, 3, 5 };
// 0x1000c1f8: [step][7] particle emitters lit by each chase-light step (-1 = none); emitters
// 0xd0046..0xd0054 have a light object at id + 0x90001 (0x160047..0x160055)
static const int32_t c1_chaseRows[8][7] = {
    { 0xd00c7, 0xd0050, 0xd004a, 0xd00ce, 0xd004e, 0xd00cc, 0xd00cd },  // 0x1000c1f8
    { 0xd0046, 0xd0054, 0xd004c, 0xd00cf, 0xd00c4, 0xd0052, 0xd00c7 },  // 0x1000c214
    { 0xd00c5, 0xd00c6, 0xd00c8, 0xd00c9, -1, -1, -1 },                 // 0x1000c230
    { 0xd0048, 0xd0054, 0xd004e, 0xd00c5, 0xd00cf, 0xd0052, -1 },       // 0x1000c24c
    { 0xd00c9, 0xd0046, 0xd0050, 0xd004a, 0xd00ce, -1, -1 },            // 0x1000c268
    { 0xd00cc, 0xd00cb, 0xd0048, 0xd0054, 0xd00cd, 0xd00c7, 0xd00c6 },  // 0x1000c284
    { 0xd00c6, 0xd00c4, 0xd004e, 0xd00c5, 0xd00cf, 0xd0052, -1 },       // 0x1000c2a0
    { 0xd00c9, 0xd0046, 0xd0050, 0xd004a, 0xd00ce, 0xd00cc, 0xd00cb },  // 0x1000c2bc
};
// 0x1000c2d8: [language][13] string pointers (strings 0x1000c374-0x1000ca1b). Only 0, 4 and 6..11
// are shown by the code. Entry 4 is also the countdown's sprintf buffer (see C1_COUNTDOWN_LEN).
enum {
    C1_T_FIND = 0, C1_T_ENTER, C1_T_CORE, C1_T_CRITICAL, C1_T_SECONDS, C1_T_CLIMB, C1_T_SIX_MIN,
    C1_T_FIVE_MIN, C1_T_FOUR_MIN, C1_T_THREE_MIN, C1_T_TWO_MIN, C1_T_ONE_MIN, C1_T_RODS
};
static const char *c1_text[3][13];
// 0x1000ca1c: the countdown format (English in every language)
static const char *c1_countdownFormat;
// The original sprintf()s into the .data string of entry 4 (40 bytes in English and German, so the
// up to 42 bytes of the formatted line overwrite the start of entry 3, which nothing shows). The
// port gives each language a buffer of its own, initialised with entry 4.
#define C1_COUNTDOWN_LEN 64

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      countdown;     // 0x1000c160 (.data = 360)
    int32_t      chaseStep;     // 0x1000c1cc (.data = -1)
    int32_t      chaseRow;      // 0x1000c1d0 (.data = -1)
    int32_t      crusherTicks;  // 0x1000fec4 100-ms ticks since the crusher started
    int32_t      doorBTicks;    // 0x1000fec8 seconds door 0x30028 has been open
    int32_t      doorBOpen;     // 0x1000fecc
    int32_t      doorCOpen;     // 0x1000fed0 never written: always 0
    int32_t      zapOn;         // 0x1000fed4 energy field in shaft 0x40083 is on
    int32_t      zapStep;       // 0x1000fed8 0..8 field cycle
    int32_t      rodSwitches;   // 0x1000fedc reactor switches 0xa00ac..af on
    int32_t      escaped;       // 0x1000fee0 left the plant through 0x400b1 (not into 0x400b0)
    int32_t      zapDisabled;   // 0x1000fee4 field was on when the rods were pulled
    int32_t      inExitLower;   // 0x1000feec inside trigger 0x400b0
    int32_t      podOnPad;      // 0x1000fef0 the pod reached pad 0x40085
    Window      *win;           // 0x1000fef4 objective line
    int32_t      chaseDelay;    // 0x1000fef8 seconds left on the current chase-light step
    int32_t      rodsCycled;    // 0x1000ff00 written, never read
    const char *const *text;    // 0x10010494 = c1_text[gLanguage]
    int32_t      lang;
    char         seconds[3][C1_COUNTDOWN_LEN];   // entry 4 of each language (sprintf target)
} st;

static const char *C1_Text(int32_t i)
{
    return i == C1_T_SECONDS ? st.seconds[st.lang] : st.text[i];
}

// 0x100015f0  InTime: plant lights out
static void C1_LightsOut(void)
{
    Environment_SetInteriorAmbient(0, 0, 0);
    Environment_OverrideInteriorFog(0, 0, 0);
}

// 0x10001610  InTime: meltdown flash
static void C1_LightsFlash(void)
{
    Environment_SetInteriorAmbient(0xff, 0xff, 0xff);
    Environment_OverrideInteriorFog(0xff, 0xff, 0xff);
    Environment_OverrideViewDistance(0, 1000);
}

// 0x10001660  EV_TICK_1S during the countdown: chase lights. Every c1_chaseDelay[] seconds the
// emitters (and their lights) of the current row go out and the next row lights up.
static void C1_ChaseLightsTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.chaseDelay == 0) {
        int32_t i = 0;
        if (st.chaseRow >= 0) {
            for (i = 0; i <= 6; i++) {
                int32_t id = c1_chaseRows[st.chaseRow][i];
                if (id == -1)
                    continue;
                if (id >= (int32_t)C1_EMITTER(0x46) && id <= (int32_t)C1_EMITTER(0x54)) {
                    Object_SetDead((ObjId)id);
                    id = c1_chaseRows[st.chaseRow][i];
                    Sound_StopObject((ObjId)id);
                    Object_SetDead((ObjId)(id + 0x90001));       // its light object
                } else {
                    Object_SetDead((ObjId)id);
                    Sound_StopObject((ObjId)c1_chaseRows[st.chaseRow][i]);
                }
            }
            i = 0;
        }
        if (++st.chaseStep > 7)
            st.chaseStep = 0;
        if (++st.chaseRow > 7)
            st.chaseRow = 0;
        st.chaseDelay = c1_chaseDelay[st.chaseStep];
        for (; i <= 6; i++) {
            int32_t id = c1_chaseRows[st.chaseRow][i];
            if (id == -1)
                continue;
            if (id >= (int32_t)C1_EMITTER(0x46) && id <= (int32_t)C1_EMITTER(0x54)) {
                Object_SetAlive((ObjId)id);
                id = c1_chaseRows[st.chaseRow][i];
                Sound_PlayLoop((ObjId)id, 0xf1, -1);
                Object_SetAlive((ObjId)(id + 0x90001));
            } else {
                Object_SetAlive((ObjId)id);
                Sound_PlayLoop((ObjId)c1_chaseRows[st.chaseRow][i], 0x106, -1);
            }
        }
    }
    st.chaseDelay--;
}

// 0x100017f0  EV_DESTROYED: turrets 0xb2 / 0xb4 are removed when destroyed
static void C1_OnTurretDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id == OBJ_ID(OT_TURRET, 0xb2) || id == OBJ_ID(OT_TURRET, 0xb4))
        Object_SetDead(id);
}

// 0x10001810  EV_ANIM_FINISHED (added by C1_OnCrusherDestroyed): the reactor arm stopped
static void C1_OnArmStopped(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != C1_REACTOR_ARM)
        return;
    Sound_PlayOnce(C1_REACTOR_ARM, 0x24a);
    EventType_RemoveHandler(EV_ANIM_FINISHED, C1_OnArmStopped);
}

// 0x10001850  EV_DESTROYED: one of the two generators 0x80066 / 0x80069 is shot -> the other one
// goes too and the reactor arm and the crusher start
static void C1_OnGeneratorDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (id != C1_GEN_A && id != C1_GEN_B)
        return;
    if (id == C1_GEN_A)
        Object_SetProperty(C1_GEN_B, PROP_HITPOINTS, 0);
    else
        Object_SetProperty(C1_GEN_A, PROP_HITPOINTS, 0);
    EventType_RemoveHandler(EV_DESTROYED, C1_OnGeneratorDestroyed);
    AnimPlay(C1_REACTOR_ARM, 1, 0);
    Sound_PlayOnce(C1_REACTOR_ARM, 0xce);
    InTime3(840, Sound_PlayLoop, C1_REACTOR_ARM, 0xcf, 0x23);
    InTime2(5390, Sound_PlayOnce, C1_REACTOR_ARM, 0xd0);
    AnimSetUpdateInterval(C1_REACTOR_ARM, 0x32);
    AnimPlay(C1_ANIM(0x65), 3, 0);
    AnimSetUpdateInterval(C1_ANIM(0x65), 0x32);
    AnimSetUpdateInterval(C1_CRUSHER, 0x14);
    AnimPlay(C1_CRUSHER, 5, 0x81);
    Sound_PlayLoop(C1_CRUSHER, 0x24d, -1);
}

// 0x10001970  EV_DESTROYED: the crusher 0xf00c3 is destroyed -> the reactor arm retracts
static void C1_OnCrusherDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != C1_CRUSHER)
        return;
    EventType_RemoveHandler(EV_DESTROYED, C1_OnCrusherDestroyed);
    AnimStop(C1_REACTOR_ARM, 0);
    AnimPlay(C1_REACTOR_ARM, 2, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C1_OnArmStopped);
    Sound_PlayLoop(C1_REACTOR_ARM, 0x24c, 0xa);
    AnimPlay(C1_ANIM(0x65), 4, 0);
    Sound_StopObject(C1_CRUSHER);
    Sound_PlayOnce(C1_ANIM(0x65), 0x244);
    InTime2(2500, Sound_PlayOnce, C1_ANIM(0x65), 0x24b);
    AnimStop(C1_CRUSHER, 0);
}

// 0x10001a40  EV_TRIGGER_ENTER / LEAVE, pad 0x40085: doors 0x30086/0x30027/0x3001c open while
// something is on the pad; once the pod is put there they stay open
static void C1_OnPodPad(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x85))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        if ((ObjId)ev->arg[0] == C1_POD) {
            Object_SetProperty(C1_POD, PROP_TARGET, -1);       // drop it
            st.podOnPad = 1;
            EventType_RemoveHandler(EV_TRIGGER_ENTER, C1_OnPodPad);
        }
        AnimPlay(C1_ANIM(0x86), 0x17, 0);
        Sound_PlayOnce(C1_ANIM(0x86), 0x245);
        AnimSetUpdateInterval(C1_ANIM(0x27), 2);
        AnimPlay(C1_ANIM(0x27), 0xd, 0);
        Sound_PlayOnce(C1_ANIM(0x27), 0xab);
        AnimSetUpdateInterval(C1_ANIM(0x1c), 2);
        AnimPlay(C1_ANIM(0x1c), 0xc, 0);
        return;
    }
    if (st.podOnPad != 0)
        return;
    AnimPlay(C1_ANIM(0x86), 0x17, 4);
    Sound_PlayOnce(C1_ANIM(0x86), 0x245);
    AnimPlay(C1_ANIM(0x27), 0xd, 4);
    Sound_PlayOnce(C1_ANIM(0x27), 0xab);
    AnimPlay(C1_ANIM(0x1c), 0xc, 4);
}

// 0x10001b70  EV_TRIGGER_ENTER 0x400df: the crusher-room doors open (once)
static void C1_OnTriggerCrusherDoorsOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0xdf))
        return;
    AnimPlay(C1_ANIM(0xe0), 7, 0);
    AnimPlay(C1_ANIM(0xe1), 7, 0);
    AnimPlay(C1_ANIM(0xde), 6, 0);
    Sound_PlayOnce(C1_ANIM(0xe0), 0xaa);
    Sound_PlayOnce(C1_ANIM(0xe1), 0xaa);
    Sound_PlayOnce(C1_ANIM(0xde), 0xa9);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C1_OnTriggerCrusherDoorsOpen);
}

// 0x10001c00  EV_TICK_100MS (added by C1_OnTriggerCrusherDoorsClose): the fan / suction trap
static void C1_FanTick(const LanderEvent *ev)
{
    (void)ev;
    st.crusherTicks++;
    if (st.crusherTicks == 10) {
        AnimPlay(C1_ANIM(0x3b), 9, 0x81);
        AnimSetUpdateInterval(C1_ANIM(0x3b), 100);
        Sound_PlayLoop(C1_ANIM(0x3b), 0xb5, -1);
        AnimPlay(C1_ANIM(0xe5), 8, 0);
        Sound_PlayOnce(C1_ANIM(0xe5), 0x250);
        AnimPlay(C1_ANIM(0xe6), 8, 0);
        AnimPlay(C1_ANIM(0xb7), 0xb, 1);
        Sound_PlayLoop(C1_ANIM(0xb7), 0x251, -1);
        AnimPlay(C1_ANIM(0x5a), 0xa, 0);
        Sound_PlayOnce(C1_ANIM(0x5a), 0x252);
    }
    if (st.crusherTicks > 10)
        AnimSetUpdateInterval(C1_ANIM(0x3b), ((100 - st.crusherTicks) * 10) / 10);   // spins up
    if (st.crusherTicks > 30)
        Object_SetProperty(C1_FORCEBOX(0x45), PROP_STATE, -200);   // suction
    if (st.crusherTicks == 95)
        EventType_RemoveHandler(EV_TICK_100MS, C1_FanTick);
}

// 0x10001d40  EV_TRIGGER_LEAVE 0x400dd: the doors close behind the lander and the fan starts
static void C1_OnTriggerCrusherDoorsClose(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0xdd))
        return;
    AnimPlay(C1_ANIM(0xe0), 7, 4);
    AnimPlay(C1_ANIM(0xe1), 7, 4);
    AnimPlay(C1_ANIM(0xde), 6, 4);
    MobObj_Wake(0x17);                                   // LightObject 0x01160017 (red)
    Sound_PlayOnce(C1_ANIM(0xe0), 0xaa);
    Sound_PlayOnce(C1_ANIM(0xe1), 0xaa);
    Sound_PlayOnce(C1_ANIM(0xde), 0xa9);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, C1_OnTriggerCrusherDoorsClose);
    EventType_AddHandler(EV_TICK_100MS, NULL, C1_FanTick);
}

// 0x10001df0  EV_POD_HOOKED: the pod is the only thing pad 0x40085 reacts to from now on
static void C1_OnPodHooked(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != C1_POD)
        return;
    Pod_Script_SetupMass(C1_POD, 0x5dc);
    Object_SetProperty(C1_TRIGGER(0x85), PROP_TARGET, ev->arg[0]);
    Pod_Script_SetupDrag(C1_POD, 0x1333);
}

// 0x10001e40  EV_ANIM_FINISHED: pistons 0x3005a / 0x3005b alternate
static void C1_OnPistonFinished(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == C1_ANIM(0x5a)) {
        AnimStop(C1_ANIM(0x5a), 0);
        AnimPlay(C1_ANIM(0x5b), 0xa, 0);
        Sound_PlayOnce(C1_ANIM(0x5b), 0x252);
        AnimSetUpdateInterval(C1_ANIM(0x5b), 0x1e);
    }
    if ((ObjId)ev->arg[0] == C1_ANIM(0x5b)) {
        AnimStop(C1_ANIM(0x5b), 0);
        AnimPlay(C1_ANIM(0x5a), 0xa, 0);
        Sound_PlayOnce(C1_ANIM(0x5a), 0x252);
        AnimSetUpdateInterval(C1_ANIM(0x5a), 0x1e);
    }
}

// 0x10001ef0  EV_TRIGGER_ENTER / LEAVE: the proximity doors
static void C1_OnTriggerDoors(const LanderEvent *ev)
{
    ObjId trig = (ObjId)ev->arg[1];
    if (trig == C1_TRIGGER(0x73)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x2f), 0xd, 0);
            Sound_PlayOnce(C1_ANIM(0x2f), 0xab);
            AnimPlay(C1_ANIM(0x22), 0xc, 0);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            AnimPlay(C1_ANIM(0x2f), 0xd, 4);
            AnimPlay(C1_ANIM(0x22), 0xc, 4);
            Sound_PlayOnce(C1_ANIM(0x2f), 0xab);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x77)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x7d), 0xf, 0);
            AnimPlay(C1_ANIM(0x7e), 0xf, 0);
            Sound_PlayOnce(C1_ANIM(0x7e), 0xaa);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            AnimPlay(C1_ANIM(0x7d), 0xf, 4);
            AnimPlay(C1_ANIM(0x7e), 0xf, 4);
            Sound_PlayOnce(C1_ANIM(0x7e), 0xaa);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x75)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x24), 0xc, 0);
            AnimPlay(C1_ANIM(0x31), 0xd, 0);
            Sound_PlayOnce(C1_ANIM(0x31), 0xab);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            AnimPlay(C1_ANIM(0x24), 0xc, 4);
            AnimPlay(C1_ANIM(0x31), 0xd, 4);
            Sound_PlayOnce(C1_ANIM(0x31), 0xab);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x76)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x1b), 0xc, 0);
            AnimPlay(C1_ANIM(0x2a), 0xd, 0);
            Sound_PlayOnce(C1_ANIM(0x2a), 0xab);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            AnimPlay(C1_ANIM(0x1b), 0xc, 4);
            AnimPlay(C1_ANIM(0x2a), 0xd, 4);
            Sound_PlayOnce(C1_ANIM(0x2a), 0xab);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x8e)) {       // shaft door: dead once the field is disabled
        if (ev->type == EV_TRIGGER_ENTER && st.zapDisabled == 0) {
            AnimPlay(C1_ANIM(0x2b), 0xd, 0);
            AnimPlay(C1_ANIM(0x1d), 0xc, 0);
            Sound_PlayOnce(C1_ANIM(0x2b), 0xab);
        }
        if (ev->type == EV_TRIGGER_LEAVE && st.zapDisabled == 0) {
            AnimPlay(C1_ANIM(0x2b), 0xd, 4);
            AnimPlay(C1_ANIM(0x1d), 0xc, 4);
            Sound_PlayOnce(C1_ANIM(0x2b), 0xab);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x35)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x2c), 0xd, 0);
            AnimPlay(C1_ANIM(0x1e), 0xc, 0);
            Sound_PlayOnce(C1_ANIM(0x2c), 0xab);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            AnimPlay(C1_ANIM(0x2c), 0xd, 4);
            AnimPlay(C1_ANIM(0x1e), 0xc, 4);
            Sound_PlayOnce(C1_ANIM(0x2c), 0xab);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x5e)) {       // closes 30 s after leaving
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x5c), 0xe, 0);
            Sound_PlayOnce(C1_ANIM(0x5c), 0xa9);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            InTime3(30000, AnimPlay, C1_ANIM(0x5c), 0xe, 4);
            Sound_PlayOnce(C1_ANIM(0x5c), 0xa9);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x5f)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x5d), 0xe, 0);
            Sound_PlayOnce(C1_ANIM(0x5d), 0xa9);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            InTime3(30000, AnimPlay, C1_ANIM(0x5d), 0xe, 4);
            InTime2(30000, Sound_PlayOnce, C1_ANIM(0x5d), 0xa9);
        }
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0x74)) {
        if (ev->type == EV_TRIGGER_ENTER) {
            AnimPlay(C1_ANIM(0x23), 0xc, 0);
            AnimPlay(C1_ANIM(0x30), 0xd, 0);
            Sound_PlayOnce(C1_ANIM(0x30), 0xab);
        }
        if (ev->type == EV_TRIGGER_LEAVE) {
            AnimPlay(C1_ANIM(0x23), 0xc, 4);
            AnimPlay(C1_ANIM(0x30), 0xd, 4);
            Sound_PlayOnce(C1_ANIM(0x30), 0xab);
        }
    }
}

// 0x100022d0  EV_TICK_1S (added by C1_OnSwitch when 0xa0025 is shot): door 0x30028 closes after
// 30 s and its switch is reset
static void C1_DoorBTimer(const LanderEvent *ev)
{
    (void)ev;
    if (++st.doorBTicks != 30)
        return;
    EventType_RemoveHandler(EV_TICK_1S, C1_DoorBTimer);
    st.doorBTicks = 0;
    if (st.doorBOpen != 1)
        return;
    AnimPlay(C1_ANIM(0x28), 0xd, 4);
    AnimPlay(C1_ANIM(0x1a), 0xc, 4);
    Object_SetProperty(C1_SWITCH(0x25), PROP_STATE, 0);
    Sound_PlayOnce(C1_ANIM(0x28), 0xab);
    st.doorBOpen = 0;
}

static void C1_OnDoorABumped(const LanderEvent *ev);

// 0x10002360  EV_ANIM_FINISHED (while door 0x30029 closes): closed -> stop watching for bumps
static void C1_OnDoorAClosed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != C1_ANIM(0x29))
        return;
    EventType_RemoveHandler(EV_COLLISION, C1_OnDoorABumped);
    EventType_RemoveHandler(EV_ANIM_FINISHED, C1_OnDoorAClosed);
}

// 0x10002390  EV_COLLISION (while door 0x30029 closes): something hit the door -> it reopens
static void C1_OnDoorABumped(const LanderEvent *ev)
{
    ObjId other = (ObjId)ev->arg[1];
    if (other != C1_ANIM(0x29) && other != C1_ANIM(0x20))
        return;
    AnimPlay(C1_ANIM(0x29), 0xd, 0);
    AnimPlay(C1_ANIM(0x20), 0xc, 0);
    Object_SetProperty(C1_SWITCH(0x26), PROP_STATE, 1);
    EventType_RemoveHandler(EV_COLLISION, C1_OnDoorABumped);
    EventType_RemoveHandler(EV_ANIM_FINISHED, C1_OnDoorAClosed);
}

// 0x10002400  EV_SWITCH 0xa0026 (door 0x30029/0x30020, airlock with 0x3002e/0x30021) and 0xa0025
// (door 0x30028/0x3001a for 30 s)
static void C1_OnSwitch(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == C1_SWITCH(0x26)) {
        if (ev->arg[1] == 1) {
            AnimPlay(C1_ANIM(0x29), 0xd, 0);
            AnimPlay(C1_ANIM(0x20), 0xc, 0);
            Sound_PlayOnce(C1_ANIM(0x29), 0xab);
            if (st.doorCOpen == 1) {                   // never true (0x1000fed0 is never set)
                AnimPlay(C1_ANIM(0x2e), 0xd, 4);
                AnimPlay(C1_ANIM(0x21), 0xc, 4);
                Sound_PlayOnce(C1_ANIM(0x2e), 0xab);
            }
        }
        if (ev->arg[1] == 0) {
            AnimPlay(C1_ANIM(0x29), 0xd, 4);
            AnimPlay(C1_ANIM(0x20), 0xc, 4);
            EventType_AddHandler(EV_COLLISION, NULL, C1_OnDoorABumped);
            EventType_AddHandler(EV_ANIM_FINISHED, NULL, C1_OnDoorAClosed);
            Sound_PlayOnce(C1_ANIM(0x29), 0xab);
            AnimPlay(C1_ANIM(0x2e), 0xd, 0);
            AnimPlay(C1_ANIM(0x21), 0xc, 0);
            Sound_PlayOnce(C1_ANIM(0x2e), 0xab);
        }
    }
    if ((ObjId)ev->arg[0] == C1_SWITCH(0x25)) {
        AnimPlay(C1_ANIM(0x28), 0xd, 0);
        AnimPlay(C1_ANIM(0x1a), 0xc, 0);
        Sound_PlayOnce(C1_ANIM(0x28), 0xab);
        st.doorBOpen = 1;
        EventType_AddHandler(EV_TICK_1S, NULL, C1_DoorBTimer);
    }
}

// 0x10002560  EV_TRIGGER_ENTER / LEAVE 0x40084: doors 0x30087 / 0x300ba (the second closes 2 s late)
static void C1_OnTriggerDoor87(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x84))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(C1_ANIM(0x87), 0x17, 0);
        Sound_PlayOnce(C1_ANIM(0x87), 0x245);
        AnimPlay(C1_ANIM(0xba), 0x18, 0);
        Sound_PlayOnce(C1_ANIM(0xba), 0xab);
    }
    if (ev->type == EV_TRIGGER_LEAVE) {
        AnimPlay(C1_ANIM(0x87), 0x17, 4);
        Sound_PlayOnce(C1_ANIM(0x87), 0x245);
        InTime3(2000, AnimPlay, C1_ANIM(0xba), 0x18, 4);
        Sound_PlayOnce(C1_ANIM(0xba), 0xab);
    }
}

// 0x10002610  EV_SWITCH 0xa00d1 (door 0x300b9) and 0xa00d0 (8-part door 0x300bb..0x300c2)
static void C1_OnSwitchDoors(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == C1_SWITCH(0xd1)) {
        if (ev->arg[1] == 1) {
            AnimPlay(C1_ANIM(0xb9), 0x18, 0);
            Sound_PlayOnce(C1_ANIM(0xb9), 0xab);
        }
        if (ev->arg[1] == 0) {
            AnimPlay(C1_ANIM(0xb9), 0x18, 4);
            Sound_PlayOnce(C1_ANIM(0xb9), 0xab);
        }
    }
    if ((ObjId)ev->arg[0] == C1_SWITCH(0xd0)) {
        if (ev->arg[1] == 1) {
            AnimPlay(C1_ANIM(0xbb), 0x10, 0);
            Sound_PlayOnce(C1_ANIM(0xbb), 0xc0);
            AnimPlay(C1_ANIM(0xbc), 0x10, 0);
            AnimPlay(C1_ANIM(0xbd), 0x10, 0);
            AnimPlay(C1_ANIM(0xbe), 0x10, 0);
            AnimPlay(C1_ANIM(0xbf), 0x10, 0);
            AnimPlay(C1_ANIM(0xc0), 0x10, 0);
            AnimPlay(C1_ANIM(0xc1), 0x10, 0);
            AnimPlay(C1_ANIM(0xc2), 0x10, 0);
        }
        if (ev->arg[1] == 0) {
            AnimPlay(C1_ANIM(0xbb), 0x10, 4);
            Sound_PlayOnce(C1_ANIM(0xbb), 0xc0);
            AnimPlay(C1_ANIM(0xbc), 0x10, 4);
            AnimPlay(C1_ANIM(0xbd), 0x10, 4);
            AnimPlay(C1_ANIM(0xbe), 0x10, 4);
            AnimPlay(C1_ANIM(0xbf), 0x10, 4);
            AnimPlay(C1_ANIM(0xc0), 0x10, 4);
            AnimPlay(C1_ANIM(0xc1), 0x10, 4);
            AnimPlay(C1_ANIM(0xc2), 0x10, 4);
        }
    }
}

// 0x10002790 / 0x100027f0 / 0x10002850  EV_TRIGGER_ENTER 0x40078 / 79 / 7a: one-way doors
static void C1_OnTriggerDoor7F(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x78))
        return;
    AnimPlay(C1_ANIM(0x7f), 0xf, 0);
    AnimPlay(C1_ANIM(0x80), 0xf, 0);
    Sound_PlayOnce(C1_ANIM(0x7f), 0xaa);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C1_OnTriggerDoor7F);
}

static void C1_OnTriggerDoor81(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x79))
        return;
    AnimPlay(C1_ANIM(0x81), 0xf, 0);
    AnimPlay(C1_ANIM(0x82), 0xf, 0);
    Sound_PlayOnce(C1_ANIM(0x81), 0xaa);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C1_OnTriggerDoor81);
}

static void C1_OnTriggerDoor7B(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x7a))
        return;
    AnimPlay(C1_ANIM(0x7b), 0xf, 0);
    AnimPlay(C1_ANIM(0x7c), 0xf, 0);
    Sound_PlayOnce(C1_ANIM(0x7b), 0xaa);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C1_OnTriggerDoor7B);
}

// 0x100028b0  EV_TICK_1S (from C1_OnTriggerReactorLevel until the rods are pulled): the energy
// field in shaft 0x40083 runs a 9-s cycle: on at 0 and 2, off at 1 and 4
static void C1_ZapCycleTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.zapStep == 9)
        st.zapStep = 0;
    if (st.zapStep == 0 || st.zapStep == 2) {
        MobObj_Wake(0x13);
        MobObj_Wake(0x11);
        MobObj_Wake(0x12);
        Sound_PlayLoop(C1_ZAP_C, 0x246, -1);
        AnimPlay(C1_ZAP_C, 9, 0x81);
        AnimPlay(C1_ZAP_A, 9, 0x81);
        AnimSetUpdateInterval(C1_ZAP_A, 0x32);
        st.zapOn = 1;
    }
    if (st.zapStep == 1 || st.zapStep == 4) {
        MobObj_Sleep(0x13);
        Sound_StopObject(C1_ZAP_C);
        MobObj_Sleep(0x11);
        MobObj_Sleep(0x12);
        AnimStop(C1_ZAP_C, 1);
        AnimStop(C1_ZAP_A, 1);
        st.zapOn = 0;
    }
    st.zapStep++;
}

// 0x100029b0  EV_TICK_100MS while the lander is in the shaft: the field drains 5 shield per tick
static void C1_ZapDamageTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.zapOn == 1)
        Object_SetProperty(C1_LANDER, PROP_HITPOINTS, Object_GetProperty(C1_LANDER, PROP_HITPOINTS) - 5);
}

// 0x100029e0  EV_TRIGGER_ENTER / LEAVE 0x40083 (the shaft)
static void C1_OnTriggerShaft(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x83))
        return;
    if (ev->type == EV_TRIGGER_ENTER)
        EventType_AddHandler(EV_TICK_100MS, NULL, C1_ZapDamageTick);
    else
        EventType_RemoveHandler(EV_TICK_100MS, C1_ZapDamageTick);
}

static void C1_OnRodMachineFinished(const LanderEvent *ev);

// 0x10002a20  EV_TICK_1S after the rods are pulled: the meltdown countdown (counter starts at 360)
static void C1_CountdownTick(const LanderEvent *ev)
{
    (void)ev;
    int32_t c = st.countdown;
    if (c > 60) {
        int32_t text, loops;
        switch (c) {                                    // byte table 0x10002c58 -> 0x10002c40
        case 120: text = C1_T_TWO_MIN;   loops = 6; break;
        case 180: text = C1_T_THREE_MIN; loops = 4; break;
        case 240: text = C1_T_FOUR_MIN;  loops = 4; break;
        case 300: text = C1_T_FIVE_MIN;  loops = 4; break;
        case 360: text = C1_T_SIX_MIN;   loops = 4; break;
        default:  text = -1; loops = 0; break;
        }
        if (text >= 0) {
            WindowSetText(st.win, C1_Text(text));
            Sound_PlayLoop(C1_ALARM, 0x253, loops);
        }
    } else if (c == 60) {
        WindowSetText(st.win, C1_Text(C1_T_ONE_MIN));
        Sound_PlayLoop(C1_ALARM, 0x253, -1);
    } else if (c == 0) {
        if (st.escaped == 0) {                          // still inside: meltdown kills the lander
            Object_SetProperty(C1_LANDER, PROP_HITPOINTS, 0);
            InTime0(300, C1_LightsFlash);
            InTime0(600, C1_LightsOut);
            Sound_StopObject(C1_ALARM);
            EventType_RemoveHandler(EV_TICK_1S, C1_ChaseLightsTick);
            EventType_RemoveHandler(EV_ANIM_FINISHED, C1_OnRodMachineFinished);
            Sound_PlayOnce(C1_LANDER, 0x6f);
            InTime2(100, Sound_PlayOnce, C1_LANDER, 0x70);
            EventType_RemoveHandler(EV_TICK_1S, C1_CountdownTick);
        } else {                                        // outside: just the blast
            Sound_PlayOnce(C1_LANDER, 0x6f);
            InTime2(100, Sound_PlayOnce, C1_LANDER, 0x70);
            EventType_RemoveHandler(EV_TICK_1S, C1_CountdownTick);
        }
    }
    if (st.countdown <= 50) {
        snprintf(st.seconds[st.lang], C1_COUNTDOWN_LEN, c1_countdownFormat, st.countdown);
        WindowSetText(st.win, C1_Text(C1_T_SECONDS));
    }
    st.countdown--;
}

static void C1_EvacTick(const LanderEvent *ev);

// 0x10002d50  EV_SWITCH 0xa00ac..af (added by C1_OnTriggerReactorLevel): the four rod switches;
// each one moves its rod 0x30095..0x30098. All four on: the countdown starts.
static void C1_OnRodSwitch(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id < (int32_t)C1_SWITCH(0xac) || id > (int32_t)C1_SWITCH(0xaf))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay((ObjId)(id - 0x70017), 0x35, 0);
        st.rodSwitches++;
    }
    if (ev->arg[1] == 0) {
        AnimPlay((ObjId)(id - 0x70017), 0x35, 4);
        st.rodSwitches--;
    }
    if (st.rodSwitches != 4)
        return;
    EventType_AddHandler(EV_TICK_1S, NULL, C1_ChaseLightsTick);
    EventType_AddHandler(EV_TICK_1S, NULL, C1_CountdownTick);
    AnimPlay(C1_ANIM(0x29), 0xd, 0);
    Sound_PlayOnce(C1_ANIM(0x29), 0xab);
    AnimPlay(C1_ANIM(0x20), 0xc, 0);
    AnimPlay(C1_ANIM(0x2d), 0xd, 0);
    Sound_PlayOnce(C1_ANIM(0x2d), 0xab);
    AnimPlay(C1_ANIM(0x1f), 0xc, 0);
    AnimPlay(C1_ANIM(0x2d), 0xd, 0);                    // (the original does this pair twice)
    Sound_PlayOnce(C1_ANIM(0x2d), 0xab);
    AnimPlay(C1_ANIM(0x1f), 0xc, 0);
    EventType_RemoveHandler(EV_SWITCH, C1_OnRodSwitch);
    EventType_RemoveHandler(EV_TICK_1S, C1_ZapCycleTick);
    AnimStop(C1_ANIM(0x3e), 0);
    AnimStop(C1_ANIM(0x3f), 0);
    AnimStop(C1_ANIM(0x40), 0);
    AnimStop(C1_ANIM(0x41), 0);
    AnimStop(C1_ANIM(0x42), 0);
    MobObj_Wake(5);
    MobObj_Wake(6);
    MobObj_Wake(7);
    Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x33));
    InTime3(60000, AnimPlay, C1_ROD_A, 0x12, 0);
    InTime3(60000, AnimPlay, C1_ROD_B, 0x37, 0);
    InTime3(60000, AnimPlay, C1_ROD_C, 0x36, 0);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C1_OnRodMachineFinished);
    EventType_AddHandler(EV_TICK_100MS, NULL, C1_EvacTick);
    if (st.zapOn == 1)
        st.zapDisabled = 1;
    Object_SetDead(C1_ZAP_C);
    Object_SetDead(C1_ZAP_A);
    Object_SetDead(C1_ZAP_B);
    st.zapOn = 0;
}

// 0x10002fb0  EV_TRIGGER_ENTER 0x40076: the lander reached the reactor level
static void C1_OnTriggerReactorLevel(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x76))
        return;
    EventType_AddHandler(EV_TICK_1S, NULL, C1_ZapCycleTick);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C1_OnTriggerShaft);
    EventType_AddHandler(EV_SWITCH, NULL, C1_OnRodSwitch);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C1_OnTriggerReactorLevel);
}

// 0x10003090  EV_TRIGGER_ENTER 0x4008e: turrets 0xd8..0xdc and 0xd4 start humming
static void C1_OnTriggerTurretHum(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x8e))
        return;
    for (int32_t id = OBJ_ID(OT_TURRET, 0xd8); id <= (int32_t)OBJ_ID(OT_TURRET, 0xdc); id++)
        Sound_PlayLoop((ObjId)id, 0x8d, -1);
    Sound_PlayLoop(OBJ_ID(OT_TURRET, 0xd4), 0x8d, -1);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, C1_OnTriggerTurretHum);
}

// 0x100030f0  EV_ANIM_FINISHED (from the rod switches until the meltdown): the fuel-rod
// machinery (mobs 5/6/7 with the hatches 0x30034 / 0x30064) cycles
static void C1_OnRodMachineFinished(const LanderEvent *ev)
{
    if (ev->arg[1] == 0x12) {                           // anim index 0x12: rod A went down
        AnimStop(C1_ROD_A, 0);
        AnimStop(C1_ROD_B, 0);
        AnimStop(C1_ROD_C, 0);
        AnimPlay(C1_ANIM(0x34), 0x16, 0x80);
        Sound_PlayOnce(C1_ANIM(0x34), 0xab);
        InTime3(3000, AnimPlay, C1_ANIM(0x64), 0x13, 0x80);
        InTime2(3000, Sound_PlayOnce, C1_ANIM(0x64), 0x24f);
        InTime3(3000, AnimPlay, C1_ROD_A, 0x14, 0x80);
        InTime2(3000, Sound_PlayOnce, C1_ROD_A, 0x8c);
        InTime3(3000, AnimPlay, C1_ROD_B, 0x38, 0x80);
        InTime2(3000, Sound_PlayOnce, C1_ROD_B, 0x244);
        InTime3(3000, AnimPlay, C1_ROD_C, 0x15, 0x80);
        InTime2(3000, Sound_PlayOnce, C1_ROD_C, 0x244);
        st.rodsCycled = 1;
    }
    if (ev->arg[1] == 0x14 && (ObjId)ev->arg[0] == C1_ROD_A) {
        AnimPlay(C1_ANIM(0x34), 0x16, 0x84);
        Sound_PlayOnce(C1_ANIM(0x34), 0xab);
        AnimPlay(C1_ANIM(0x64), 0x13, 0x84);
        Sound_PlayOnce(C1_ANIM(0x64), 0x24f);
        AnimStop(C1_ROD_A, 1);
        AnimStop(C1_ROD_B, 1);
        AnimStop(C1_ROD_C, 1);
        MobObj_Sleep(5);
        MobObj_Sleep(6);
        MobObj_Sleep(7);
        InTime1(2000, MobObj_Wake, 5);
        InTime1(2000, MobObj_Wake, 6);
        InTime1(2000, MobObj_Wake, 7);
        InTime3(2100, AnimPlay, C1_ROD_A, 0x12, 0x80);
        InTime2(2100, Sound_PlayOnce, C1_ROD_A, 0xc8);
        InTime3(2100, AnimPlay, C1_ROD_B, 0x37, 0x80);
        InTime3(2100, AnimPlay, C1_ROD_C, 0x36, 0x80);
    }
}

// 0x10003360  EV_TICK_100MS after the rods are pulled: above 10000 -> success; never removed
// (LevelEnd stops the scheduler)
static void C1_EvacTick(const LanderEvent *ev)
{
    (void)ev;
    const int32_t *pos = (const int32_t *)Object_GetProperty(C1_LANDER, PROP_POSITION);
    if (pos[1] > 10000) {
        Lander_SetCash(C1_LANDER, Lander_GetCash(C1_LANDER) + 60000);
        MissionScore_End(1);                            // 0x10001390
    }
}

// 0x100033b0  EV_TRIGGER_ENTER / LEAVE 0x400b0 / 0x400b1 (stacked boxes at the plant's exit):
// leaving the upper one while not in the lower one = out of the plant
static void C1_OnTriggerExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0xb0)) {
        if (ev->type == EV_TRIGGER_ENTER)
            st.inExitLower = 1;
        if (ev->type == EV_TRIGGER_LEAVE)
            st.inExitLower = 0;
    }
    if ((ObjId)ev->arg[1] == C1_TRIGGER(0xb1)) {
        if (ev->type == EV_TRIGGER_LEAVE && st.inExitLower == 0)
            st.escaped = 1;
        if (ev->type == EV_TRIGGER_ENTER)
            st.escaped = 0;
    }
}

// 0x10003410  EV_PICKUP {pickup, player}
static void C1_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == C1_PICKUP(2)) {
        int32_t shield = (int32_t)Object_GetProperty(C1_LANDER, PROP_HITPOINTS) + 0x40;
        Voice_ShieldAcquired(C1_LANDER, 25);
        Object_SetProperty(C1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == C1_PICKUP(3)) {
        int32_t shield = (int32_t)Object_GetProperty(C1_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(C1_LANDER, 50);
        Object_SetProperty(C1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == C1_PICKUP(4)) {
        int32_t shield = (int32_t)Object_GetProperty(C1_LANDER, PROP_HITPOINTS) + 0xc0;
        Voice_ShieldAcquired(C1_LANDER, 75);
        Object_SetProperty(C1_LANDER, PROP_HITPOINTS, shield > 0x100 ? 0x100 : shield);
    }
    if ((ObjId)ev->arg[0] == C1_PICKUP(0xb8)) {
        Lander_SetCash(C1_LANDER, Lander_GetCash(C1_LANDER) + gSmallCash);
        Voice_CashAcquired(C1_LANDER);
    }
    if ((ObjId)ev->arg[0] == C1_PICKUP(5)) {
        Lander_SetCash(C1_LANDER, Lander_GetCash(C1_LANDER) + gLargeCash);
        Voice_CashAcquired(C1_LANDER);
    }
    if ((ObjId)ev->arg[0] == C1_PICKUP(0)) {            // missile: stays if the rack is full
        if (Lander_AddMissiles(C1_LANDER, 1) == 0) {
            Voice_MissilesFull(C1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
        } else {
            Voice_MissilesAcquired(C1_LANDER);
        }
    }
    if ((ObjId)ev->arg[0] == C1_PICKUP(1)) {
        if (Lander_AddMissiles(C1_LANDER, 1) == 0) {
            Voice_MissilesFull(C1_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(C1_LANDER);
    }
}

// 0x10003600  EV_TRIGGER_ENTER / LEAVE 0x40035: turret 0xd8 is active while the lander is inside
static void C1_OnTriggerTurretD8(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != C1_TRIGGER(0x35))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        Object_SetProperty(OBJ_ID(OT_TURRET, 0xd8), PROP_STATE, 1);
        return;
    }
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xd8), PROP_STATE, 0);
}

// 0x10003640  EV_TRIGGER_ENTER 0x40088..0x4008d: the matching mine 0xf008f..0xf0094 beeps and
// blows up a second later
static void C1_OnTriggerMine(const LanderEvent *ev)
{
    int32_t trig = (int32_t)ev->arg[1];
    if (trig < (int32_t)C1_TRIGGER(0x88) || trig > (int32_t)C1_TRIGGER(0x8d))
        return;
    ObjId mine = (ObjId)(trig + 0xb0007);
    Sound_PlayOnce(mine, 0x102);
    InTime3(1000, Object_SetProperty, mine, PROP_HITPOINTS, 0);
}

// 0x10003690  common set-up (only the lander start calls it)
static void C1_Setup(void)
{
    st.lang = gLanguage;
    st.text = c1_text[gLanguage];
    gTurretTypes[5] = &c1_turretType5;
    gTurretTypes[3] = &c1_turretType3;
    gTurretTypes[8] = &c1_turretType8;
    SetGravity(12);
    gAirResistance = 0x14;
    gMainThrottlePower = 0x18;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, C1_Text(C1_T_FIND));
    MissionScore_Init(C1_LANDER, 15000, 8);                   // 0x10001510 (first, so it runs last)
    for (int32_t id = OBJ_ID(OT_TURRET, 0x9e); id <= (int32_t)OBJ_ID(OT_TURRET, 0xab); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = OBJ_ID(OT_TURRET, 0xb2); id <= (int32_t)OBJ_ID(OT_TURRET, 0xb5); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = OBJ_ID(OT_TURRET, 0xd2); id <= (int32_t)OBJ_ID(OT_TURRET, 0xdc); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = OBJ_ID(OT_FUELDUMP, 0x56); id <= (int32_t)OBJ_ID(OT_FUELDUMP, 0x57); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    for (int32_t id = OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x8f); id <= (int32_t)OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x94); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 5);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 100);
    }
    for (int32_t id = C1_GEN_A; id <= (int32_t)OBJ_ID(OT_DESTRUCTIBLE, 0x68); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 5);
    for (int32_t id = C1_GEN_B; id <= (int32_t)OBJ_ID(OT_DESTRUCTIBLE, 0x6b); id++)
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 5);
    Object_SetProperty(OBJ_ID(OT_DESTRUCTIBLE, 0x32), PROP_EXPLOSION, 0x78);
    gNbWaypoints = c1_nbWaypoints;
    gWaypoints = c1_waypoints;
    EventType_AddHandler(EV_DESTROYED, NULL, C1_OnTurretDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, C1_OnGeneratorDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, C1_OnCrusherDestroyed);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10003000
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnPodPad);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerCrusherDoorsOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerDoors);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerDoor87);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerDoor7F);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerDoor81);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerDoor7B);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerShaft);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerReactorLevel);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerTurretHum);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerExit);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerTurretD8);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, C1_OnTriggerMine);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C1_OnTriggerDoors);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C1_OnTriggerCrusherDoorsClose);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C1_OnPodPad);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C1_OnTriggerDoor87);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C1_OnTriggerExit);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, C1_OnTriggerTurretD8);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, C1_OnPistonFinished);
    EventType_AddHandler(EV_POD_HOOKED, NULL, C1_OnPodHooked);
    EventType_AddHandler(EV_SWITCH, NULL, C1_OnSwitch);
    EventType_AddHandler(EV_SWITCH, NULL, C1_OnSwitchDoors);
    EventType_AddHandler(EV_PICKUP, NULL, C1_OnPickup);
    for (int32_t id = OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x8f); id <= (int32_t)OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x94); id++)
        AnimPlay((ObjId)id, 0, 1);                          // (six separate calls)
    for (int32_t id = OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x8f); id <= (int32_t)OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x94); id++)
        Sound_PlayLoop((ObjId)id, 0x101, -1);
    for (int32_t id = C1_ANIM(0x3d); id <= (int32_t)C1_ANIM(0x42); id++) {   // (unrolled)
        AnimPlay((ObjId)id, 0x11, 0x81);
        AnimSetUpdateInterval((ObjId)id, 0x32);
    }
    for (int32_t id = C1_ANIM(0x3d); id <= (int32_t)C1_ANIM(0x42); id++)
        Sound_PlayLoop((ObjId)id, 0xb5, -1);
    Sound_PlayLoop(OBJ_ID(OT_TURRET, 0xd3), 0x8d, -1);
    Sound_PlayLoop(OBJ_ID(OT_TURRET, 0xd6), 0x8d, -1);
    Sound_PlayLoop(OBJ_ID(OT_TURRET, 0xd7), 0x8d, -1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xd5), PROP_TARGET, -1);
    Object_SetProperty(OBJ_ID(OT_TURRET, 0xd8), PROP_STATE, 0);
    MobObj_Wake(0x15);                                      // AmbientFX (siren)
    MobObj_Wake(0x16);                                      // AmbientFX
    MobObj_Wake(9);                                         // AmbientFX
    for (int32_t i = 0x47; i <= 0x55; i += 2)
        Object_SetDead(OBJ_ID(OT_LIGHTOBJECT, i));          // chase lights 0x160047..0x160055
    MobObj_Wake(C1_MOB_POD);
    MobObj_Wake(C1_MOB_REACTOR_ARM);
    Object_SetProperty(C1_FORCEBOX(0x45), PROP_STATE, 0);
    Object_SetProperty(C1_FORCEBOX(0x45), PROP_FLAGS, 2);
    Object_SetProperty(C1_FORCEBOX(0x43), PROP_STATE, 0);
    Object_SetProperty(C1_FORCEBOX(0x43), PROP_FLAGS, 2);
    Object_SetProperty(C1_FORCEBOX(0x44), PROP_STATE, 0);
    Object_SetProperty(C1_FORCEBOX(0x44), PROP_FLAGS, 2);
    Object_SetProperty(C1_FORCEBOX(0x3c), PROP_STATE, 0);
    Object_SetProperty(C1_FORCEBOX(0x3c), PROP_FLAGS, 2);
    for (int32_t i = 0x09; i <= 0x0e; i++) {                // (unrolled)
        Object_SetProperty(C1_FORCEBOX(i), PROP_STATE, 0x23);
        Object_SetProperty(C1_FORCEBOX(i), PROP_FLAGS, 2);
    }
    for (int32_t i = 0x0f; i <= 0x11; i++) {                // (unrolled)
        Object_SetProperty(C1_FORCEBOX(i), PROP_STATE, -0x23);
        Object_SetProperty(C1_FORCEBOX(i), PROP_FLAGS, 2);
    }
    for (int32_t id = OBJ_ID(OT_TURRET, 0xd2); id <= (int32_t)OBJ_ID(OT_TURRET, 0xdc); id++)   // (unrolled)
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x3c);
    TexAnimPlay(0x8c, 1, 100);
    TexAnimPlay(0x8d, 1, 100);
    AnimSetUpdateInterval(C1_ANIM(0x20), 0x19);
    AnimSetUpdateInterval(C1_ANIM(0x29), 0x19);
    Media_AudioPlay(0);
    for (int32_t i = 0x46; i <= 0x54; i += 2)               // (unrolled) chase-light emitters
        Object_SetDead(C1_EMITTER(i));
    for (int32_t i = 0xc4; i <= 0xcf; i++)
        Object_SetDead(C1_EMITTER(i));
    Object_SetDead(C1_EMITTER(0x07));
    Object_SetDead(C1_EMITTER(0x08));
}

// 0x10003f10  LevelStartLevel_Lander (export 0x10001010)
static void C1_StartLander(void)
{
    MobObj_Wake(C1_MOB_LANDER);
    MobObj_Wake(C1_MOB_CAMERA);
    C1_Setup();
    Lander_SetHeadLight(C1_LANDER, 0x271, 0xa66);
    Object_SetProperty(C1_CAMERA, PROP_TARGET, C1_LANDER);
}

// 0x10003f60  LevelStartLevel_Flycam and LevelStopLevel (exports 0x10001020): empty
static void C1_Nothing(void)
{
}

// 0x10001030
static uint32_t C1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel: two calls of the empty 0x10003f60, returns 0.
// Port: re-initialise the DLL data (the engine reloads the DLL for every mission start).
static int32_t C1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.countdown = c1_countdownStart;
    st.chaseStep = -1;
    st.chaseRow = -1;
    for (int l = 0; l < 3; l++)
        snprintf(st.seconds[l], C1_COUNTDOWN_LEN, "%s", c1_text[l][C1_T_SECONDS]);
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText c1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000c2d8, .count = 3 * 13, .out = c1_text },
    { .kind = GT_STRINGS, .va = 0x1000ca1c, .count = 1, .out = &c1_countdownFormat },
    { 0 },
};

const mission_script_t mission_callisto_1 = {
    .name          = "CALLISTO\\1",
    .get_version   = C1_GetVersion,
    .load          = C1_Load,
    .start_lander  = C1_StartLander,
    .start_flycam  = C1_Nothing,
    .start_animcam = NULL,
    .stop          = C1_Nothing,
    .text          = c1_gametext,
};
