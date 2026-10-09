
// MISSIONS\DEATHMAT\1\MISSIONR.DLL - faithful C port.
//
// Multiplayer arena built from the same source as Deathmatch 3 (see deathmat_3.c for the shared
// logic: game types 0 deathmatch / 1 capture the flag / 2 bomb, spawn points, frag panel, kill
// messages, network snapshot registration), with this level's own machinery:
//   * four crushers: shooting switch 0xa00c4+k lowers anim object 0x30011+k (anim 3), switches its
//     two lights 0x1600d4+2k/d5+2k off and wakes warning light mob 0x52+k; when the anim ends it
//     rises again, then the lights come back and the switch flips back. A player killed by a
//     crusher is credited to whoever shot the switch (the crusher kill messages).
//   * a death without a killer within 2 s of being hit / bumped by another player is credited to
//     that player (the last-attacker table, write-only in Deathmatch 3, is read here).
//   * four door groups (6 parts each, 0x30015 + 6k) opened while a player is inside trigger
//     0x400cc + 2k (or, outside deathmatch, 0x400cd + 2k).
//   * four teleports 0x400c8..cb (two of them removed in capture the flag), fuel dumps
//     0x11002d..30 refilled to "endless" every second, pickups that re-appear.
// Spawn points: 2 shared + 4 (deathmatch) / 8 (capture the flag) / 6 (bomb).
//
// Single player (Mission_Run mode 0): set-up, lander mob 0xb and camera mob 1 woken (camera target
// 0x101000b); game type 0; no message line or frag panel (made by Animcam); the engine raises no
// event 0x12 in single player, so the lander starts at its mob record position.
//
// Addresses are the DLL's (image base 0x10000000). Exports: LevelLoadLevel 0x10002720,
// LevelStartLevel_Lander 0x10002730 (-> 0x10003d10), LevelStartLevel_Animcam 0x10002740
// (-> 0x10003e50), LevelStartLevel_Flycam and LevelStopLevel 0x10002750 (-> empty 0x10003e80:
// this DLL's Flycam start does nothing), LevelGetVersion 0x10002760. CRT used by the mission
// code: _ftol 0x10003ef0, sprintf 0x10003f20, strncpy 0x10003f90, qsort 0x10004090, rand
// 0x100042d0 (MSVC, per-thread seed 1 at DLL load). No score library, no music.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define DM1_LANDER        MOB_ID(OT_LANDER, 0xb)      // 0x0101000b single player's lander
#define DM1_CAMERA        MOB_ID(OT_CAMERA, 1)        // 0x01060001
#define DM1_MOB_LANDER    0xb
#define DM1_MOB_CAMERA    1
#define DM1_PLAYER_FIRST  MOB_ID(OT_LANDER, 0xb)      // 0x0101000b..0x0101001a: the 16 player landers
#define DM1_PLAYER_LAST   MOB_ID(OT_LANDER, 0x1a)
#define DM1_BOMB          MOB_ID(OT_POD, 0x1b)        // 0x010c001b game type 2
#define DM1_MOB_BOMB      0x1b
#define DM1_FLAG_A_BASE   MOB_ID(OT_POD, 0x30)        // 0x010c0030 + team A colour
#define DM1_FLAG_B_BASE   MOB_ID(OT_POD, 0x41)        // 0x010c0041 + team B colour
#define DM1_TRIG_HOME_A   OBJ_ID(OT_TRIGGERBOX, 0x09) // flag A home
#define DM1_TRIG_CAPT_B   OBJ_ID(OT_TRIGGERBOX, 0x0a) // flag B captured here
#define DM1_TRIG_CAPT_A   OBJ_ID(OT_TRIGGERBOX, 0x0d) // flag A captured here
#define DM1_TRIG_HOME_B   OBJ_ID(OT_TRIGGERBOX, 0x0e) // flag B home
#define DM1_TRIG_TELE_FIRST OBJ_ID(OT_TRIGGERBOX, 0xc8) // 0x400c8..cb teleports
#define DM1_TRIG_DOOR_FIRST OBJ_ID(OT_TRIGGERBOX, 0xcc) // 0x400cc..d3 door trigger pairs
#define DM1_BASE_A        OBJ_ID(OT_DESTRUCTIBLE, 0x0b)
#define DM1_BASE_B        OBJ_ID(OT_DESTRUCTIBLE, 0x0c)
#define DM1_CRUSHER_FIRST OBJ_ID(OT_ANIMOBJECT, 0x11) // 0x30011..14
#define DM1_CRUSHER_LAST  OBJ_ID(OT_ANIMOBJECT, 0x14)
#define DM1_SWITCH_FIRST  OBJ_ID(OT_SWITCH, 0xc4)     // 0xa00c4..c7
#define DM1_SWITCH_LAST   OBJ_ID(OT_SWITCH, 0xc7)
#define DM1_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))
#define DM1_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))      // 0x00130000..08
#define DM1_FUELDUMP(n)   OBJ_ID(OT_FUELDUMP, (n))

#define DM1_IS_PLAYER(id) ((int32_t)(id) >= (int32_t)DM1_PLAYER_FIRST && (int32_t)(id) <= (int32_t)DM1_PLAYER_LAST)
#define DM1_FN(f)         ((void (*)(void))(f))      // GameState_RegisterFunction takes void(*)(void)

// ---- static data (.data / .rdata) ---------------------------------------------------------------
static const double dm1_hookScale = 128.0;           // 0x1000c1c8
static const char   dm1_fmtInt[] = "%d";             // 0x1000e030
static const char   dm1_fmtStr[] = "%s";             // 0x1000e034
static const int32_t dm1_doorStateInit[4] = { 1, 1, 1, 1 };   // 0x1000e168 (.data)

// 0x1000e178: [language][11] death without a killer (victim name)
static const char *dm1_textAccident[3][11];

// 0x1000e200: [language][8] frags (two names; order per dm1_fragVictimFirst)
static const char *dm1_textFrag[3][8];
static const int32_t dm1_fragVictimFirst[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };   // 0x1000e260

// 0x1000e280: [language][22] interface strings
enum {
    DM1_T_PLAYER, DM1_T_TITLE, DM1_T_TEAM_A, DM1_T_TEAM_B, DM1_T_JOINED, DM1_T_EXCHANGE_FAILED,
    DM1_T_QUIT, DM1_T_BOMB_COUNT, DM1_T_EXCHANGE_OK, DM1_T_YOU_KILLED, DM1_T_HAS_FLAG,
    DM1_T_A_RETRIEVED, DM1_T_B_RETRIEVED, DM1_T_A_RESPAWNED, DM1_T_B_RESPAWNED, DM1_T_CRUSHED,
    DM1_T_CRUSHED_SELF, DM1_T_A_STOLEN, DM1_T_B_STOLEN, DM1_T_KILLED_YOU, DM1_T_A_BEATEN,
    DM1_T_B_BEATEN, DM1_T_COUNT
};
static const char *dm1_text[3][DM1_T_COUNT];

// 0x1000e388: [language][7] the local player died without a killer
static const char *dm1_textYouDied[3][7];

// ---- DLL globals (.bss) -------------------------------------------------------------------------
typedef struct DM1Player {      // 0x10012fa0: 16 x 0x18
    int32_t id;                 // +0x00 player id
    char    name[16];           // +0x04
    int32_t frags;              // +0x14
} DM1Player;

static struct {
    int32_t   hookPoint[4];     // 0x10012c80 pod tow point / 128 (always 0: default point)
    ObjId     flagA;            // 0x10012c90
    int32_t   homeB;            // 0x10012c94 flag B is at home
    int32_t   returnB;          // 0x10012c98 100-ms ticks until a dropped flag B returns
    int32_t   homeA;            // 0x10012c9c
    int32_t   returnA;          // 0x10012ca0
    ObjId     flagB;            // 0x10012ca4
    struct { ObjId who; int32_t time; } lastHit[16];   // 0x10012ca8 last player who hit / bumped
    char      rowName[16][16];  // 0x10012d28 frag panel rows (sorted)
    Window   *panel;            // 0x10012e28 frag panel window (NULL = hidden)
    char      rowFrags[16][16]; // 0x10012e30
    int32_t   panelX, panelY, panelW, panelH;  // 0x10012f30
    DM1Player *sorted[16];      // 0x10012f40
    char      teamText[2][16];  // 0x10012f80
    DM1Player players[16];      // 0x10012fa0
    Window   *msgWin;           // 0x10013120 message line
    char      empty[4];         // 0x10013124 "" (clears the message line)
    int32_t   grace;            // 0x10013128 bomb: seconds of grace after a pass
    char      bombText[20];     // 0x10013130
    int32_t   bombFast;         // 0x10013144 holder died: count down every tick
    int32_t   bombCount;        // 0x10013148
    int32_t   bombTicks;        // 0x1001314c
    Window   *bombWin;          // 0x10013150
    int32_t   spawnFx[10];      // 0x10013158 100-ms ticks of spawn effects left per point
    int32_t   nSpawn;           // 0x10013180
    ObjId     crusherBy[4];     // 0x100131b0 who shot crusher k's switch
    int32_t   doorInA[6];       // 0x100131c0 objects inside door trigger 0x400cc + 2k
    int32_t   doorInB[6];       // 0x100131d8 objects inside door trigger 0x400cd + 2k
    int32_t   crusher[4];       // 0x100131f0 crusher k: 0 up, 1 going down, 2 going up
    int32_t   doorState[4];     // 0x1000e168 (.data, initially 1) last AnimPlay flags per door group
    ObjId     carried[32];      // 0x100137c0 per player id: pod it carries (or -1)
    ObjId     carrierA;         // 0x100138e0 lander carrying flag A (-1)
    ObjId     carrierB;         // 0x100138e4
    int32_t   capturedB;        // 0x100138e8 times flag B was captured (the frag panel's team B score)
    int32_t   capturedA;        // 0x100138ec times flag A was captured (the frag panel's team A score)
    ObjId     bombHolder;       // 0x10013840
    char      msg[128];         // 0x10013860 message line text
    uint32_t  randSeed;         // CRT _holdrand (per-thread data, 1 at DLL load)
} st;

// 0x100042d0  MSVC rand()
static int32_t DM1_Rand(void)
{
    st.randSeed = st.randSeed * 214013u + 2531011u;
    return (int32_t)((st.randSeed >> 16) & 0x7fff);
}

// The original indexes carried[] with Player_GetId() unchecked (-1 would write just before it)
static void DM1_SetCarried(int32_t pid, ObjId v)
{
    if (pid >= 0 && pid < 32)
        st.carried[pid] = v;
}

// 0x10001000  (InTime target) wake a pod and set its tow point
static void DM1_WakePod(intptr_t pod)
{
    MobObj_Wake((int32_t)((uint32_t)pod & 0xffff));
    Pod_Script_SetupHookPoint((ObjId)pod, (int32_t)(st.hookPoint[0] * dm1_hookScale),
                              (int32_t)(st.hookPoint[1] * dm1_hookScale),
                              (int32_t)(st.hookPoint[2] * dm1_hookScale));
}

// 0x10001060
static void DM1_RegisterHookPoint(void)
{
    GameState_RegisterDataBlock(st.hookPoint, 0x10);
}

static void DM1_Message(void)
{
    WindowSetText(st.msgWin, st.msg);
}

// sprintf(msg, text) without arguments in the original; none of these texts contains a '%'
static void DM1_SetMsg(const char *text)
{
    snprintf(st.msg, sizeof st.msg, "%s", text);
}

static const char *DM1_PlayerName(int32_t pid);

// ================================================================================================
// Capture the flag (game type 1)
// ================================================================================================

static void DM1_CtfReturnTickA(const LanderEvent *ev);
static void DM1_CtfReturnTickB(const LanderEvent *ev);

// 0x10001080  EV_POD_HOOKED {pod, lander}: a flag is picked up (any pod other than flag A
// counts as flag B); a returning flag stops its countdown; message if it was not at home
static void DM1_CtfOnPodHooked(const LanderEvent *ev)
{
    int32_t pid = -1;
    if ((ObjId)ev->arg[0] == st.flagA) {
        st.carrierA = (ObjId)ev->arg[1];
        if (st.returnA != 0) {
            EventType_RemoveHandler(EV_TICK_100MS, DM1_CtfReturnTickA);
            st.returnA = 0;
        }
        if (st.homeA == 0)
            pid = Player_GetId((ObjId)ev->arg[1]);
    } else {
        st.carrierB = (ObjId)ev->arg[1];
        if (st.returnB != 0) {
            EventType_RemoveHandler(EV_TICK_100MS, DM1_CtfReturnTickB);
            st.returnB = 0;
        }
        if (st.homeB == 0)
            pid = Player_GetId((ObjId)ev->arg[1]);
    }
    if (pid != -1) {
        const char *name = DM1_PlayerName(pid);
        sprintf(st.msg, dm1_text[gLanguage][DM1_T_HAS_FLAG], name);
        DM1_Message();
    }
}

// 0x10001160  EV_TRIGGER_LEAVE: a flag left its home trigger
static void DM1_CtfOnTriggerLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == DM1_TRIG_HOME_A && (ObjId)ev->arg[0] == st.flagA) {
        st.homeA = 0;
        DM1_SetMsg(dm1_text[gLanguage][DM1_T_A_STOLEN]);
        DM1_Message();
    }
    if ((ObjId)ev->arg[1] == DM1_TRIG_HOME_B && (ObjId)ev->arg[0] == st.flagB) {
        st.homeB = 0;
        DM1_SetMsg(dm1_text[gLanguage][DM1_T_B_STOLEN]);
        DM1_Message();
    }
}

// 0x10001220  EV_TRIGGER_ENTER: a flag reached its capture trigger: count it, release it, put it
// back home in 10 s
static void DM1_CtfOnTriggerEnter(const LanderEvent *ev)
{
    ObjId flag = (ObjId)ev->arg[0];
    if ((ObjId)ev->arg[1] == DM1_TRIG_CAPT_B && flag == st.flagB) {
        st.capturedB++;
        if (st.carrierB != OBJ_NONE) {
            Object_SetProperty(flag, PROP_TARGET, -1);
            Object_SetProperty(flag, PROP_TARGET, -2);
        }
        MobObj_Sleep((int32_t)(flag & 0xffff));
        InTime1(10000, DM1_WakePod, flag);
        st.homeB = 1;
        if (st.carrierB != OBJ_NONE) {
            int32_t pid = Player_GetId(st.carrierB);
            const char *name = DM1_PlayerName(pid);
            sprintf(st.msg, dm1_text[gLanguage][DM1_T_B_BEATEN], name);
            DM1_Message();
            st.carrierB = OBJ_NONE;
            DM1_SetCarried(pid, OBJ_NONE);
        }
    } else if ((ObjId)ev->arg[1] == DM1_TRIG_CAPT_A && flag == st.flagA) {
        st.capturedA++;
        if (st.carrierA != OBJ_NONE) {
            Object_SetProperty(flag, PROP_TARGET, -1);
            Object_SetProperty(flag, PROP_TARGET, -2);
        }
        MobObj_Sleep((int32_t)(flag & 0xffff));
        InTime1(10000, DM1_WakePod, flag);
        st.homeA = 1;
        if (st.carrierA != OBJ_NONE) {
            int32_t pid = Player_GetId(st.carrierA);
            const char *name = DM1_PlayerName(pid);
            sprintf(st.msg, dm1_text[gLanguage][DM1_T_A_BEATEN], name);
            DM1_Message();
            st.carrierA = OBJ_NONE;
            DM1_SetCarried(pid, OBJ_NONE);
        }
    }
}

// 0x10001410  EV_TICK_100MS while dropped flag A waits: back home at 0
static void DM1_CtfReturnTickA(const LanderEvent *ev)
{
    (void)ev;
    if (--st.returnA != 0)
        return;
    MobObj_Sleep((int32_t)(st.flagA & 0xffff));
    DM1_WakePod(st.flagA);
    DM1_SetMsg(dm1_text[gLanguage][DM1_T_A_RESPAWNED]);
    DM1_Message();
    st.homeA = 1;
    EventType_RemoveHandler(EV_TICK_100MS, DM1_CtfReturnTickA);
}

// 0x100014a0  same for flag B
static void DM1_CtfReturnTickB(const LanderEvent *ev)
{
    (void)ev;
    if (--st.returnB != 0)
        return;
    MobObj_Sleep((int32_t)(st.flagB & 0xffff));
    DM1_WakePod(st.flagB);
    DM1_SetMsg(dm1_text[gLanguage][DM1_T_B_RESPAWNED]);
    DM1_Message();
    st.homeB = 1;
    EventType_RemoveHandler(EV_TICK_100MS, DM1_CtfReturnTickB);
}

// 0x10001530  start a dropped flag's return countdown (100-ms ticks)
static void DM1_CtfStartReturn(ObjId flag, int32_t ticks)
{
    if (flag == st.flagA) {
        st.returnA = ticks;
        EventType_AddHandler(EV_TICK_100MS, NULL, DM1_CtfReturnTickA);
        return;
    }
    st.returnB = ticks;
    EventType_AddHandler(EV_TICK_100MS, NULL, DM1_CtfReturnTickB);
}

// 0x10001580  EV_DESTROYED: a flag carrier died: drop the flag, it returns in 100 s
static void DM1_CtfOnDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (!DM1_IS_PLAYER(id))
        return;
    int32_t pid = Player_GetId(id);
    if (st.carrierA == id) {
        Object_SetProperty(st.flagA, PROP_TARGET, -3);
        DM1_SetCarried(pid, OBJ_NONE);
        st.carrierA = OBJ_NONE;
        DM1_CtfStartReturn(st.flagA, 1000);
    }
    if (st.carrierB == id) {
        Object_SetProperty(st.flagB, PROP_TARGET, -3);
        DM1_SetCarried(pid, OBJ_NONE);
        st.carrierB = OBJ_NONE;
        DM1_CtfStartReturn(st.flagB, 1000);
    }
}

// 0x10001640  the flags are the pods of the teams' colours; the bases show the colour
static void DM1_CtfChooseFlags(void)
{
    if (gTeamAShipColour != 0)
        Object_AdvanceDestroyLevel(DM1_BASE_A, (int32_t)gTeamAShipColour);
    if (gTeamBShipColour != 0)
        Object_AdvanceDestroyLevel(DM1_BASE_B, (int32_t)gTeamBShipColour);
    st.flagA = DM1_FLAG_A_BASE + gTeamAShipColour;
    st.flagB = DM1_FLAG_B_BASE + gTeamBShipColour;
}

// 0x100016a0
static void DM1_CtfSetup(void)
{
    DM1_CtfChooseFlags();
    Object_SetProperty(DM1_TRIG_HOME_A, PROP_TARGET, st.flagA);
    Object_SetProperty(DM1_TRIG_CAPT_B, PROP_TARGET, st.flagB);
    Object_SetProperty(DM1_TRIG_HOME_B, PROP_TARGET, st.flagB);
    Object_SetProperty(DM1_TRIG_CAPT_A, PROP_TARGET, st.flagA);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM1_CtfOnTriggerEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, DM1_CtfOnTriggerLeave);
    EventType_AddHandler(EV_DESTROYED, NULL, DM1_CtfOnDestroyed);
    st.carrierB = OBJ_NONE;
    st.carrierA = OBJ_NONE;
    EventType_AddHandler(EV_POD_HOOKED, NULL, DM1_CtfOnPodHooked);
    st.returnB = 0;
    st.returnA = 0;
    st.capturedA = 0;
    st.capturedB = 0;
    st.homeB = 1;
    st.homeA = 1;
    DM1_WakePod(st.flagA);
    DM1_WakePod(st.flagB);
    Object_SetProperty(st.flagA, PROP_FLAGS, 0x80);
    Object_SetProperty(st.flagB, PROP_FLAGS, 0x40);
}

// 0x100017c0  network snapshot registration (game type 1)
static void DM1_CtfRegisterState(void)
{
    GameState_RegisterDataBlock(&st.carrierA, 4);
    GameState_RegisterDataBlock(&st.carrierB, 4);
    GameState_RegisterDataBlock(&st.homeA, 4);
    GameState_RegisterDataBlock(&st.homeB, 4);
    GameState_RegisterDataBlock(&st.returnA, 4);
    GameState_RegisterDataBlock(&st.returnB, 4);
    GameState_RegisterDataBlock(&st.capturedB, 4);
    GameState_RegisterDataBlock(&st.capturedA, 4);
    GameState_RegisterDataBlock(&st.flagA, 4);
    GameState_RegisterDataBlock(&st.flagB, 4);
    GameState_RegisterFunction(DM1_FN(DM1_CtfReturnTickA));
    GameState_RegisterFunction(DM1_FN(DM1_CtfReturnTickB));
    GameState_RegisterFunction(DM1_FN(DM1_CtfOnTriggerEnter));
    GameState_RegisterFunction(DM1_FN(DM1_CtfOnTriggerLeave));
    GameState_RegisterFunction(DM1_FN(DM1_CtfOnPodHooked));
    GameState_RegisterFunction(DM1_FN(DM1_CtfOnDestroyed));
}

// ================================================================================================
// Deathmatch (game type 0)
// ================================================================================================

// 0x10001890  EV_DESTROYED: a player died: drop the pod it carried
static void DM1_DmOnDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (!DM1_IS_PLAYER(id))
        return;
    int32_t pid = Player_GetId(id);
    ObjId pod = (pid >= 0 && pid < 32) ? st.carried[pid] : OBJ_NONE;
    if (pod != OBJ_NONE)
        Object_SetProperty(pod, PROP_TARGET, -3);
}

// 0x100018d0  EV_COLLISION {body, other, impulse}: two players bumped: remember each other.
// The handler masks the event's "other" id in place (later handlers see the masked value).
static void DM1_DmOnCollision(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;   // the original writes into the event record
    int32_t a = Player_GetId((ObjId)e->arg[0]);
    if (a == -1)
        return;
    e->arg[1] = (intptr_t)((uint32_t)e->arg[1] & 0x3ffffffu);
    if (!DM1_IS_PLAYER(e->arg[1]))
        return;
    int32_t b = Player_GetId((ObjId)e->arg[1]);
    if (b == -1 || a > 15 || b > 15)
        return;
    st.lastHit[a].who = (ObjId)e->arg[1];
    st.lastHit[b].who = (ObjId)e->arg[0];
    st.lastHit[b].time = gGameUpdateTime;
    st.lastHit[a].time = gGameUpdateTime;
}

// 0x10001950  EV_PROJECTILE_HIT {target, damage, shooter, kind}: remember the shooter
static void DM1_DmOnHit(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if (!DM1_IS_PLAYER(e->arg[0]) || !DM1_IS_PLAYER(e->arg[2]))
        return;
    int32_t a = Player_GetId((ObjId)e->arg[0]);
    if (a < 0 || a > 15)
        return;
    st.lastHit[a].who = (ObjId)e->arg[2];
    st.lastHit[a].time = gGameUpdateTime;
}

// 0x100019b0  the player who hit / bumped player pid less than 2 s ago (unsigned compare), or -1
static ObjId DM1_LastAttacker(int32_t pid, int32_t now)
{
    ObjId who = st.lastHit[pid & 15].who;
    if (who != OBJ_NONE && (uint32_t)(now - st.lastHit[pid & 15].time) < 2000u)
        return who;
    return OBJ_NONE;
}

// 0x100019e0
static void DM1_DmSetup(void)
{
    EventType_AddHandler(EV_DESTROYED, NULL, DM1_DmOnDestroyed);
    EventType_AddHandler(EV_COLLISION, NULL, DM1_DmOnCollision);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, DM1_DmOnHit);
    for (int i = 0; i < 16; i++)
        st.lastHit[i].who = OBJ_NONE;
}

// 0x10001a30  network snapshot registration (game type 0)
static void DM1_DmRegisterState(void)
{
    GameState_RegisterFunction(DM1_FN(DM1_DmOnDestroyed));
    GameState_RegisterFunction(DM1_FN(DM1_DmOnCollision));
    GameState_RegisterFunction(DM1_FN(DM1_DmOnHit));
    GameState_RegisterDataBlock(st.lastHit, 0x80);
}

// ================================================================================================
// Players, messages, frag panel
// ================================================================================================

// 0x10001a70  the player's name (kept in its record; "PLAYER" when the engine has none)
static const char *DM1_PlayerName(int32_t pid)
{
    char *dst = st.players[pid & 15].name;
    const char *name = Message_GetNameFromId(pid);
    if (name != NULL) {
        strncpy(dst, name, 15);
        return dst;
    }
    strcpy(dst, dm1_text[gLanguage][DM1_T_PLAYER]);
    return dst;
}

// 0x10001ae0  qsort: most frags first, then lower player id
static int DM1_CompareFrags(const void *pa, const void *pb)
{
    const DM1Player *a = *(DM1Player *const *)pa;
    const DM1Player *b = *(DM1Player *const *)pb;
    if (a->frags > b->frags)
        return -1;
    if (a->frags < b->frags)
        return 1;
    if (a->id < b->id)
        return -1;
    return a->id > b->id;
}

// 0x10001b20  EV_PLAYER_JOINED {lander}
static void DM1_OnPlayerJoined(const LanderEvent *ev)
{
    int32_t pid = Lander_GetPlayerId((ObjId)ev->arg[0]);
    if (pid == -1)
        return;
    if ((int32_t)ev->arg[0] != gThisPlayer) {
        const char *name = DM1_PlayerName(pid);
        sprintf(st.msg, dm1_text[gLanguage][DM1_T_JOINED], name);
        DM1_Message();
    }
    st.players[pid & 15].frags = 0;
}

// 0x10001ba0  EV_PLAYER_LEFT {lander, name}
static void DM1_OnPlayerLeft(const LanderEvent *ev)
{
    if ((int32_t)ev->arg[0] == gThisPlayer)
        return;
    sprintf(st.msg, dm1_text[gLanguage][DM1_T_QUIT], (const char *)ev->arg[1]);
    DM1_Message();
}

static void DM1_HideFrags(void);
static void DM1_ShowFrags(void);

// 0x10001c00  EV_LANDER_SPAWNED {lander}: the local player is back: hide the panel and message
static void DM1_OnLocalRespawn(const LanderEvent *ev)
{
    if ((int32_t)ev->arg[0] != gThisPlayer)
        return;
    DM1_HideFrags();
    WindowSetText(st.msgWin, st.empty);
}

// 0x10001c30  EV_FRAG_KEY {0}: toggle the frag panel
static void DM1_OnFragKey(const LanderEvent *ev)
{
    if (ev->arg[0] != 0)
        return;
    if (st.panel != NULL)
        DM1_HideFrags();
    else
        DM1_ShowFrags();
}

// 0x10001c50
static void DM1_AddFrag(int32_t pid)
{
    st.players[pid & 15].frags++;
}

// 0x10001c60
static void DM1_SubFrag(int32_t pid)
{
    st.players[pid & 15].frags--;
}

static void DM1_UpdateFrags(void);

// 0x10001c70  g2dRenderCallback while the frag panel is shown
static void DM1_RenderFrags(void *ot)
{
    int32_t x = st.panelX, y = st.panelY, w, h;
    const char *const *text = dm1_text[gLanguage];
    Win_SetTransform(st.panel);
    Font_GetStringLen(text[DM1_T_TITLE], &w, &h, 1);
    Font_Print(text[DM1_T_TITLE], ((st.panelW - w) >> 1) + x, y, 0xf7ff, 1, ot);
    y += 0x18;
    if (gNetworkGameType == 1) {
        for (int t = 0; t < 2; t++) {
            Font_Print(text[DM1_T_TEAM_A + t], x, y, 0xb3ad00, 1, ot);
            Font_GetStringLen(st.teamText[t], &w, &h, 1);
            Font_Print(st.teamText[t], x - w + st.panelW, y, 0xf7ff, 1, ot);
            y += 0xc;
        }
        y += 0x18;
    }
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < gNbPlayers; j++) {
            if (gPlayerIds[j] != st.sorted[i]->id)
                continue;
            Font_Print(st.rowName[i], x, y, 0xb3ad00, 1, ot);
            Font_GetStringLen(st.rowFrags[i], &w, &h, 1);
            Font_Print(st.rowFrags[i], x - w + st.panelW, y, 0xf7ff, 1, ot);
            y += 0xc;
        }
    }
    Win_SetTransform(NULL);
    DM1_UpdateFrags();
}

// 0x10001e90  sort the players by frags, make the rows, size and centre the panel
static void DM1_UpdateFrags(void)
{
    int32_t h = (gNbPlayers + 2) * 12;
    qsort(st.sorted, 16, sizeof st.sorted[0], DM1_CompareFrags);
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < gNbPlayers; j++) {
            if (gPlayerIds[j] != st.sorted[i]->id)
                continue;
            sprintf(st.rowName[i], dm1_fmtStr, DM1_PlayerName(st.sorted[i]->id));
            sprintf(st.rowFrags[i], dm1_fmtInt, st.sorted[i]->frags);
        }
    }
    if (gNetworkGameType == 1) {
        sprintf(st.teamText[0], dm1_fmtInt, st.capturedA);
        sprintf(st.teamText[1], dm1_fmtInt, st.capturedB);
        h += 0x30;
    }
    if (st.panel == NULL)
        return;
    st.panelW = 0xdc;
    st.panelH = h;
    st.panelX = (gWindowWidth - 0xdc) >> 1;
    st.panelY = (gWindowHeight - (h < 0x60 ? 0x60 : h)) >> 1;
    WindowSetSize(st.panel, st.panelX - 6, st.panelY - 6, 0xe8, h + 12);
}

// 0x10001fe0  (also an InTime target)
static void DM1_ShowFrags(void)
{
    if (st.panel != NULL)
        return;
    g2dRenderCallback = DM1_RenderFrags;
    st.panel = WindowCreate(9, NULL);
    WindowSetBorderInfo(st.panel, &BorderInfo);
    DM1_UpdateFrags();
    Window_CreateTransform(st.panel);
}

// 0x10002030
static void DM1_HideFrags(void)
{
    if (st.panel != NULL) {
        WindowDestroy(st.panel);
        st.panel = NULL;
    }
    g2dRenderCallback = NULL;
}

// 0x10002060  (LevelStartLevel_Animcam only)
static void DM1_FragsSetup(void)
{
    st.panel = NULL;
    for (int i = 0; i < 16; i++) {
        st.players[i].id = i;
        st.players[i].frags = 0;
        st.sorted[i] = &st.players[i];
    }
    st.msgWin = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.msgWin, 20, 20, 500, 12);
    EventType_AddHandler(EV_PLAYER_JOINED, NULL, DM1_OnPlayerJoined);
    EventType_AddHandler(EV_PLAYER_LEFT, NULL, DM1_OnPlayerLeft);
    EventType_AddHandler(EV_LANDER_SPAWNED, NULL, DM1_OnLocalRespawn);
    EventType_AddHandler(EV_FRAG_KEY, NULL, DM1_OnFragKey);
}

// 0x100020f0
static void DM1_FragsRegisterState(void)
{
    GameState_RegisterDataBlock(st.players, 0x180);
    GameState_RegisterFunction(DM1_FN(DM1_OnPlayerJoined));
    GameState_RegisterFunction(DM1_FN(DM1_OnPlayerLeft));
    GameState_RegisterFunction(DM1_FN(DM1_OnLocalRespawn));
    GameState_RegisterFunction(DM1_FN(DM1_OnFragKey));
    GameState_UnRegisterFunction(DM1_FN(DM1_ShowFrags));
}

// ================================================================================================
// Bomb (game type 2)
// ================================================================================================

// 0x10002150
static ObjId DM1_RandomPlayer(void)
{
    return gPlayerObjects[LndrRand() % (uint32_t)gNbPlayers];
}

static void DM1_BombTick(const LanderEvent *ev);
static void DM1_BombOnCollision(const LanderEvent *ev);

// 0x10002170  (InTime target) give the bomb to a random living player, 120 steps
static void DM1_BombStart(void)
{
    ObjId lander = DM1_RandomPlayer();
    if (Object_GetProperty(lander, PROP_HITPOINTS) == 0) {
        InTime0(500, DM1_BombStart);
        return;
    }
    int32_t pos[3];
    pos[0] = ((const int32_t *)Object_GetProperty(lander, PROP_POSITION))[0];
    pos[1] = ((const int32_t *)Object_GetProperty(lander, PROP_POSITION))[1];
    pos[2] = ((const int32_t *)Object_GetProperty(lander, PROP_POSITION))[2];
    pos[1] += 0x180;
    Object_SetProperty(DM1_BOMB, PROP_POSITION, (intptr_t)pos);
    DM1_WakePod(DM1_BOMB);
    Pod_Script_SetupDrag(DM1_BOMB, 0x400);
    Object_SetProperty(DM1_BOMB, PROP_TARGET, lander);
    DM1_SetCarried(Player_GetId(lander), lander);   // sic: the lander id, not the pod
    st.bombHolder = lander;
    st.bombCount = 0x78;
    st.bombFast = 0;
    st.bombTicks = 0;
    EventType_AddHandler(EV_TICK_100MS, NULL, DM1_BombTick);
}

// 0x10002280  after a pass: no passing, no countdown for 6 s
static void DM1_BombDisarm(void)
{
    EventType_RemoveHandler(EV_COLLISION, DM1_BombOnCollision);
    EventType_RemoveHandler(EV_TICK_100MS, DM1_BombTick);
    st.grace = 6;
}

// 0x100022b0
static void DM1_BombArm(void)
{
    EventType_AddHandler(EV_COLLISION, NULL, DM1_BombOnCollision);
    EventType_AddHandler(EV_TICK_100MS, NULL, DM1_BombTick);
}

// 0x100022e0  EV_TICK_1S during the grace time
static void DM1_BombGraceTick(const LanderEvent *ev)
{
    (void)ev;
    if (--st.grace != 0)
        return;
    DM1_BombArm();
    EventType_RemoveHandler(EV_TICK_1S, DM1_BombGraceTick);
}

// 0x10002310  attach the bomb to newHolder; if it does not take, give it back to oldHolder
static int32_t DM1_BombPass(ObjId oldHolder, ObjId newHolder, ObjId bomb)
{
    Object_SetProperty(bomb, PROP_TARGET, newHolder);
    if ((ObjId)Object_GetProperty(bomb, PROP_TARGET) != newHolder) {
        if (oldHolder != OBJ_NONE)
            Object_SetProperty(bomb, PROP_TARGET, oldHolder);
        return 0;
    }
    DM1_SetCarried(Player_GetId(newHolder), newHolder);
    st.bombHolder = newHolder;
    return 1;
}

static void DM1_BombExplode(void);

// 0x10002380  EV_TICK_100MS: one count per 11 ticks (every tick once the holder died)
static void DM1_BombTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.bombFast == 0) {
        if (st.bombTicks++ != 10)
            return;
        st.bombTicks = 0;
    }
    if (--st.bombCount == 0)
        DM1_BombExplode();
    sprintf(st.bombText, dm1_text[gLanguage][DM1_T_BOMB_COUNT], st.bombCount);
    WindowSetText(st.bombWin, st.bombText);
}

// 0x10002400  EV_COLLISION {body, other, impulse}: a living player touched the holder (or the
// bomb): pass it. Masks both ids in the event record in place.
static void DM1_BombOnCollision(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if (!DM1_IS_PLAYER(e->arg[0]))
        return;
    if (Object_GetProperty((ObjId)e->arg[0], PROP_HITPOINTS) == 0)
        return;
    e->arg[1] = (intptr_t)((uint32_t)e->arg[1] & 0x3ffffffu);
    ObjId holder = st.bombHolder;
    if (!(DM1_IS_PLAYER(e->arg[1]) && holder == (ObjId)e->arg[1])) {
        if ((ObjId)e->arg[1] != DM1_BOMB || holder == (ObjId)e->arg[0])
            return;
    }
    if (DM1_BombPass(holder, (ObjId)e->arg[0], DM1_BOMB)) {
        DM1_SetMsg(dm1_text[gLanguage][DM1_T_EXCHANGE_OK]);
        DM1_Message();
        DM1_BombDisarm();
        EventType_AddHandler(EV_TICK_1S, NULL, DM1_BombGraceTick);
        return;
    }
    DM1_SetMsg(dm1_text[gLanguage][DM1_T_EXCHANGE_FAILED]);
    DM1_Message();
}

// 0x10002520  EV_DESTROYED: the holder died: drop the bomb, count down fast
static void DM1_BombOnDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (!DM1_IS_PLAYER(id) || st.bombHolder != id)
        return;
    st.bombFast = 1;
    Object_SetProperty(DM1_BOMB, PROP_TARGET, -3);
}

// 0x10002560  count reached 0: explosion at the bomb, a new bomb in 5 s
static void DM1_BombExplode(void)
{
    if (st.grace != 0) {
        st.grace = 0;
        EventType_RemoveHandler(EV_TICK_1S, DM1_BombGraceTick);
        EventType_AddHandler(EV_COLLISION, NULL, DM1_BombOnCollision);
    } else {
        EventType_RemoveHandler(EV_TICK_100MS, DM1_BombTick);
    }
    if (st.bombHolder != OBJ_NONE) {
        int32_t pid = Lander_GetPlayerId(st.bombHolder);
        st.bombHolder = OBJ_NONE;
        DM1_SetCarried(pid, OBJ_NONE);
    }
    Explosion_CreateDynamic(1, 0x78, (const int32_t *)Object_GetProperty(DM1_BOMB, PROP_POSITION));
    MobObj_Sleep(DM1_MOB_BOMB);
    InTime0(5000, DM1_BombStart);
}

// 0x10002620  the bomb count line
static void DM1_BombCreateWindow(void)
{
    st.bombWin = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.bombWin, 20, 40, 128, 12);
}

// 0x10002650
static void DM1_BombSetup(void)
{
    st.bombHolder = OBJ_NONE;
    EventType_AddHandler(EV_COLLISION, NULL, DM1_BombOnCollision);
    EventType_AddHandler(EV_DESTROYED, NULL, DM1_BombOnDestroyed);
    InTime0(5000, DM1_BombStart);
    DM1_BombCreateWindow();
}

// 0x100026a0  network snapshot registration (game type 2)
static void DM1_BombRegisterState(void)
{
    GameState_RegisterDataBlock(&st.bombHolder, 4);
    GameState_RegisterDataBlock(&st.bombCount, 4);
    GameState_RegisterDataBlock(&st.bombTicks, 4);
    GameState_RegisterDataBlock(&st.bombFast, 4);
    GameState_RegisterDataBlock(&st.grace, 4);
    GameState_RegisterFunction(DM1_FN(DM1_BombTick));
    GameState_RegisterFunction(DM1_FN(DM1_BombGraceTick));
    GameState_RegisterFunction(DM1_FN(DM1_BombStart));
    GameState_RegisterFunction(DM1_FN(DM1_BombOnCollision));
    GameState_RegisterFunction(DM1_FN(DM1_BombOnDestroyed));
}


// 0x10003e80  LevelStopLevel, LevelStartLevel_Flycam and LevelLoadLevel's helper: empty
static void DM1_Nothing(void)
{
}

// ================================================================================================
// Spawn points (2 shared + 4 / 8 / 6 per game type)
// ================================================================================================

// 0x10002770  object of spawn point i: kind 0 anim (0x30038), 1/2 lights (0x160039/3a), 3 first
// of four smoke emitters (0xd003b), 4 marker mob; 7 objects per point; points 0 and 1 are shared
static ObjId DM1_SpawnObject(int32_t i, int32_t kind, int32_t type)
{
    if (kind == 4) {
        if (type == 0 || i < 2)
            return (ObjId)(i + 0x119001c);
        if (type == 1)
            return (ObjId)(i + 0x1190020);
        return (ObjId)(i + 0x1190028);
    }
    int32_t base;
    switch (kind) {
    case 0: base = 0x30038; break;
    case 1: base = 0x160039; break;
    case 2: base = 0x16003a; break;
    case 3: base = 0xd003b; break;
    default: base = i; break;
    }
    if (type == 0 || i < 2)
        return (ObjId)(i * 7 + base);
    if (type == 1)
        return (ObjId)((i + 4) * 7 + base);
    return (ObjId)((i + 12) * 7 + base);
}

// 0x10002820
static void DM1_SpawnFxOn(int32_t i)
{
    Object_SetAlive(DM1_SpawnObject(i, 1, gNetworkGameType));
    ObjId e = DM1_SpawnObject(i, 3, gNetworkGameType);
    for (int32_t k = 0; k < 4; k++)
        Object_SetAlive(e + k);
}

// 0x10002880
static void DM1_SpawnFxOff(int32_t i)
{
    Object_SetDead(DM1_SpawnObject(i, 1, gNetworkGameType));
    ObjId e = DM1_SpawnObject(i, 3, gNetworkGameType);
    for (int32_t k = 0; k < 4; k++)
        Object_SetDead(e + k);
}

// 0x100028e0  move one of the point's emitters by a random vector of length 1536
static void DM1_SpawnFxJitter(int32_t i)
{
    int32_t k = DM1_Rand() & 3;
    int32_t v[3];
    v[0] = (DM1_Rand() & 0xfff) - 0x800;
    v[1] = (DM1_Rand() & 0xfff) - 0x800;
    v[2] = (DM1_Rand() & 0xfff) - 0x800;
    FwVectorNormalise(v);
    v[0] = (v[0] * 3 << 9) >> 12;
    v[1] = (v[1] * 3 << 9) >> 12;
    v[2] = (v[2] * 3 << 9) >> 12;
    ObjId e = DM1_SpawnObject(i, 3, gNetworkGameType) + (ObjId)k;
    FwVectorAdd(v, (const int32_t *)Object_GetProperty(e, PROP_POSITION), v);
    Object_SetProperty(e, PROP_POSITION, (intptr_t)v);
}

// 0x100029c0
static void DM1_SpawnFxStart(int32_t i)
{
    if (st.spawnFx[i] == 0)
        DM1_SpawnFxOn(i);
    st.spawnFx[i] += 0x10;
}

// 0x100029f0  EV_TICK_100MS
static void DM1_SpawnFxTick(const LanderEvent *ev)
{
    (void)ev;
    for (int32_t i = 0; i < st.nSpawn; i++) {
        if (st.spawnFx[i] == 0)
            continue;
        if (--st.spawnFx[i] == 0)
            DM1_SpawnFxOff(i);
        else
            DM1_SpawnFxJitter(i);
    }
}

// 0x10002a30  EV_LANDER_SPAWNED {lander}: put it on a random spawn point (team parity in CTF)
static void DM1_OnLanderSpawned(const LanderEvent *ev)
{
    ObjId lander = (ObjId)ev->arg[0];
    if (!DM1_IS_PLAYER(lander) || Player_GetId(lander) == -1)
        return;
    int32_t i = (int32_t)(LndrRand() % (uint32_t)st.nSpawn);
    if (gNetworkGameType == 1) {
        if (Object_GetProperty(lander, PROP_FLAGS) & 0x800)
            i |= 1;
        else
            i &= ~1;
    }
    SnapToTarget(lander, DM1_SpawnObject(i, 4, gNetworkGameType));
    if ((int32_t)lander == gThisPlayer)
        AutoCam_SnapToTarget(DM1_CAMERA);
    DM1_SpawnFxStart(i);
    Object_GetProperty(lander, PROP_POSITION);   // result unused
}

// 0x10002af0  the n own spawn points (after the 2 shared ones) of this game type: idle animation
static void DM1_SpawnSetStart(int32_t type, int32_t n)
{
    st.nSpawn = n + 2;
    for (int32_t i = 0; i < n; i++)
        AnimPlay(DM1_SpawnObject(i + 2, 0, type), 4, 0x81);
}

// 0x10002b40  the own spawn points of another game type: removed
static void DM1_SpawnSetKill(int32_t type, int32_t n)
{
    for (int32_t i = 2; i - 2 < n; i++) {
        Object_SetDead(DM1_SpawnObject(i, 0, type));
        Object_SetDead(DM1_SpawnObject(i, 2, type));
        Object_SetDead(DM1_SpawnObject(i, 1, type));
        ObjId e = DM1_SpawnObject(i, 3, type);
        for (int32_t k = 0; k < 4; k++)
            Object_SetDead(e + k);
    }
}

// 0x10002bd0
static void DM1_SpawnSetup(void)
{
    for (int32_t i = 0; i < 2; i++)
        AnimPlay(DM1_SpawnObject(i, 0, gNetworkGameType), 4, 0x81);
    switch (gNetworkGameType) {
    case 0:
        DM1_SpawnSetStart(0, 4);
        DM1_SpawnSetKill(1, 8);
        DM1_SpawnSetKill(2, 6);
        break;
    case 1:
        DM1_SpawnSetStart(1, 8);
        DM1_SpawnSetKill(0, 4);
        DM1_SpawnSetKill(2, 6);
        break;
    case 2:
        DM1_SpawnSetStart(2, 6);
        DM1_SpawnSetKill(1, 8);
        DM1_SpawnSetKill(0, 4);
        break;
    }
    for (int32_t i = 0; i < st.nSpawn; i++)
        DM1_SpawnFxOff(i);
    EventType_AddHandler(EV_LANDER_SPAWNED, NULL, DM1_OnLanderSpawned);
    EventType_AddHandler(EV_TICK_100MS, NULL, DM1_SpawnFxTick);
}

// 0x10002cb0
static void DM1_SpawnRegisterState(void)
{
    GameState_RegisterDataBlock(st.spawnFx, 0x28);
    GameState_RegisterDataBlock(&st.nSpawn, 4);
    GameState_RegisterFunction(DM1_FN(DM1_OnLanderSpawned));
    GameState_RegisterFunction(DM1_FN(DM1_SpawnFxTick));
}

// ================================================================================================
// Level machinery, frags
// ================================================================================================

// 0x10002cf0  EV_POD_HOOKED {pod, lander}: remember which pod the player carries
static void DM1_OnPodHooked(const LanderEvent *ev)
{
    DM1_SetCarried(Lander_GetPlayerId((ObjId)ev->arg[1]), (ObjId)ev->arg[0]);
}

// 0x10002d10  move the lander, the pod it carries and (local player) the camera by to - from
static void DM1_Teleport(ObjId lander, ObjId pod, ObjId camera, const int32_t from[3], const int32_t to[3])
{
    int32_t dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    int32_t p[3];
    const int32_t *q = (const int32_t *)Object_GetProperty(lander, PROP_POSITION);
    p[0] = q[0] + dx; p[1] = q[1] + dy; p[2] = q[2] + dz;
    Object_SetProperty(lander, PROP_POSITION, (intptr_t)p);
    Player_GetId(lander);                         // result unused
    if (pod != OBJ_NONE) {
        q = (const int32_t *)Object_GetProperty(pod, PROP_POSITION);
        p[0] = q[0] + dx; p[1] = q[1] + dy; p[2] = q[2] + dz;
        Object_SetProperty(pod, PROP_POSITION, (intptr_t)p);
    }
    if ((int32_t)lander == gThisPlayer) {
        q = (const int32_t *)Object_GetProperty(camera, PROP_POSITION);
        p[0] = q[0] + dx; p[1] = q[1] + dy; p[2] = q[2] + dz;
        Object_SetProperty(camera, PROP_POSITION, (intptr_t)p);
    }
}

// 0x10002e10  door group k (6 parts 0x30015 + 6k): play anim 1 with flags (0 open, 4 close)
// unless that was the last command
static void DM1_DoorGroup(int32_t k, int32_t flags)
{
    if (k < 0 || k >= 4)          // the original indexes its 4-entry table with up to k = 5
        return;
    if (st.doorState[k] == flags)
        return;
    st.doorState[k] = flags;
    ObjId part = DM1_ANIM(0x15 + 6 * k);
    int32_t f = flags | 0x80;
    AnimPlay(part, 1, f);
    AnimSetUpdateInterval(part, 7);
    Sound_PlayOnce(part, 0x3c6);
    for (int32_t n = 1; n < 6; n++) {
        AnimPlay(part + (ObjId)n, 1, f);
        AnimSetUpdateInterval(part + (ObjId)n, 7);
    }
}

// 0x10002f10  EV_TRIGGER_ENTER 0x400cc..0x400d7: door k = (t - 0x400cc) / 2 opens when the first
// object enters its first trigger (outside deathmatch: either trigger)
static void DM1_OnDoorEnter(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[1] - (int32_t)DM1_TRIG_DOOR_FIRST;
    int32_t side = t & 1, k = t >> 1;
    if (k < 0 || k > 5)
        return;
    if (side)
        st.doorInB[k]++;
    else
        st.doorInA[k]++;
    if (gNetworkGameType == 0) {
        if (st.doorInA[k] != 1)
            return;
    } else if (st.doorInA[k] != 1 && st.doorInB[k] != 1) {
        return;
    }
    DM1_DoorGroup(k, 0);
}

// 0x10002f80  EV_TRIGGER_LEAVE: the door closes when both of its triggers are empty
static void DM1_OnDoorLeave(const LanderEvent *ev)
{
    int32_t t = (int32_t)ev->arg[1] - (int32_t)DM1_TRIG_DOOR_FIRST;
    int32_t side = t & 1, k = t >> 1;
    if (k < 0 || k > 5)
        return;
    if (side)
        st.doorInB[k]--;
    else
        st.doorInA[k]--;
    if (st.doorInA[k] == 0 && st.doorInB[k] == 0)
        DM1_DoorGroup(k, 4);
}

// 0x10002fd0  EV_ANIM_FINISHED {object, anim}: crusher anim 3 done: down -> back up; up -> lights
// on, warning light off, switch flipped back. (The original reads the state of any object
// finishing an anim 3 from its table without a range check; the port treats others as idle.)
static void DM1_OnCrusherAnimDone(const LanderEvent *ev)
{
    if (ev->arg[1] != 3)
        return;
    ObjId obj = (ObjId)ev->arg[0];
    int32_t k = (int32_t)obj - (int32_t)DM1_CRUSHER_FIRST;
    int32_t s = (k >= 0 && k < 4) ? st.crusher[k] : 0;
    if (s == 1) {
        AnimPlay(obj, 3, 0x84);
        InTime2(0, Sound_PlayOnce, obj, 0x3c3);
        InTime3(1240, Sound_PlayLoop, obj, 0x3c4, 5);
        InTime2(5640, Sound_PlayOnce, obj, 0x3c5);
        st.crusher[k] = 2;
    } else if (s == 2) {
        MobObj_Sleep((int32_t)((obj - 0x2ffbf) & 0xffff));        // warning light mob 0x52 + k
        Object_SetAlive(2 * obj + 0x1000b2);                        // lights 0x1600d4 + 2k
        Object_SetAlive(2 * obj + 0x1000b3);
        st.crusher[k] = 0;
        ObjId sw = obj + 0x700b3;                                   // switch 0xa00c4 + k
        Object_SetProperty(sw, PROP_STATE, 1 - (int32_t)Object_GetProperty(sw, PROP_STATE));
    }
}

// 0x100030e0  EV_SWITCH {switch, state, shooter} 0xa00c4..c7: start crusher k (or undo the switch
// while it is busy)
static void DM1_OnSwitch(const LanderEvent *ev)
{
    ObjId sw = (ObjId)ev->arg[0];
    if ((int32_t)sw < (int32_t)DM1_SWITCH_FIRST || (int32_t)sw > (int32_t)DM1_SWITCH_LAST)
        return;
    int32_t k = (int32_t)(sw - DM1_SWITCH_FIRST);
    if (st.crusher[k] != 0) {
        Object_SetProperty(sw, PROP_STATE, 1 - (int32_t)ev->arg[1]);
        return;
    }
    ObjId crusher = sw - 0x700b3;                                   // 0x30011 + k
    AnimPlay(crusher, 3, 0x80);
    InTime2(0, Sound_PlayOnce, crusher, 0x3c0);
    InTime3(1170, Sound_PlayLoop, crusher, 0x3c1, 5);
    InTime2(5570, Sound_PlayOnce, crusher, 0x3c2);
    Object_SetDead(2 * sw + 0x1ff4c);                               // lights 0x1600d4 + 2k
    Object_SetDead(2 * sw + 0x1ff4d);
    MobObj_Wake((int32_t)((sw - 0xa0072) & 0xffff));                // warning light mob 0x52 + k
    st.crusher[k] = 1;
    st.crusherBy[k] = (ObjId)ev->arg[2];
}

// 0x100031e0  EV_TRIGGER_ENTER 0x400c8..cb: teleports (fixed from / to points)
static void DM1_OnTeleportTrigger(const LanderEvent *ev)
{
    int32_t pid = Player_GetId((ObjId)ev->arg[0]);
    ObjId pod = (pid >= 0 && pid < 32) ? st.carried[pid] : OBJ_NONE;
    int32_t from[4] = { 0, 0, 0, 0 }, to[4] = { 0, 0, 0, 0 };
    switch ((uint32_t)ev->arg[1] - DM1_TRIG_TELE_FIRST) {
    case 0:                                                         // 0x10003214
        from[1] = 0x1b80; from[2] = -0x5f00;
        to[1] = 0xc80;    to[2] = -0x780;
        break;
    case 1:                                                         // 0x10003254
        from[0] = 0x5f00; from[1] = 0x1b80;
        to[0] = 0x780;    to[1] = 0xc80;
        break;
    case 2:                                                         // 0x10003291
        from[1] = 0x1b80; from[2] = 0x5f00;
        to[1] = 0xc80;    to[2] = 0x780;
        break;
    case 3:                                                         // 0x100032ce
        from[0] = -0x5f00; from[1] = 0x1b80;
        to[0] = -0x780;    to[1] = 0xc80;
        break;
    default:
        return;
    }
    DM1_Teleport((ObjId)ev->arg[0], pod, DM1_CAMERA, from, to);
}

// 0x10003340  EV_DESTROYED {victim, source, size} (multiplayer): frags and the kill message.
// A crusher kill counts for whoever shot its switch; a death without a killer within 2 s of a hit
// or bump counts for that player.
static void DM1_OnFrag(const LanderEvent *ev)
{
    ObjId victim = (ObjId)ev->arg[0], source = (ObjId)ev->arg[1];
    if (!DM1_IS_PLAYER(victim))
        return;
    int32_t vp = Player_GetId(victim), kp;
    if (DM1_IS_PLAYER(source)) {
        kp = Player_GetId(source);
        if (kp == -1)
            goto accident;
    } else if ((int32_t)source >= (int32_t)DM1_CRUSHER_FIRST && (int32_t)source <= (int32_t)DM1_CRUSHER_LAST) {
        int32_t k = (int32_t)(source - DM1_CRUSHER_FIRST);
        if (st.crusher[k] == 0)
            goto accident;
        ObjId by = st.crusherBy[k];
        if (!DM1_IS_PLAYER(by))
            goto tail;
        int32_t bp = Player_GetId(by);
        if (victim == by) {
            const char *name = DM1_PlayerName(bp);
            sprintf(st.msg, dm1_text[gLanguage][DM1_T_CRUSHED_SELF], name);
        } else {
            DM1_AddFrag(bp);
            const char *vname = DM1_PlayerName(vp);
            const char *bname = DM1_PlayerName(bp);
            sprintf(st.msg, dm1_text[gLanguage][DM1_T_CRUSHED], bname, vname);
        }
        DM1_Message();
        goto tail;
    } else {
        ObjId att = DM1_LastAttacker(vp, gGameUpdateTime);
        if (att == OBJ_NONE)
            goto accident;
        kp = Player_GetId(att);
        if (kp == -1)
            goto accident;
    }
    DM1_AddFrag(kp);
    if ((int32_t)source == gThisPlayer) {
        const char *name = DM1_PlayerName(vp);
        sprintf(st.msg, dm1_text[gLanguage][DM1_T_YOU_KILLED], name);
    } else if ((int32_t)victim == gThisPlayer) {
        const char *name = DM1_PlayerName(kp);
        sprintf(st.msg, dm1_text[gLanguage][DM1_T_KILLED_YOU], name);
    } else {
        int32_t r = DM1_Rand() % 8;
        if (dm1_fragVictimFirst[r]) {
            const char *killer = DM1_PlayerName(kp);
            const char *vname = DM1_PlayerName(vp);
            sprintf(st.msg, dm1_textFrag[gLanguage][r], vname, killer);
        } else {
            const char *vname = DM1_PlayerName(vp);
            const char *killer = DM1_PlayerName(kp);
            sprintf(st.msg, dm1_textFrag[gLanguage][r], killer, vname);
        }
    }
    DM1_Message();
    goto tail;
accident:
    if ((int32_t)victim == gThisPlayer) {
        DM1_SetMsg(dm1_textYouDied[gLanguage][DM1_Rand() % 7]);
    } else {
        const char *name = DM1_PlayerName(vp);
        sprintf(st.msg, dm1_textAccident[gLanguage][DM1_Rand() % 11], name);
    }
    DM1_Message();
    DM1_SubFrag(vp);
tail:
    if ((int32_t)victim == gThisPlayer)
        InTime0(1400, DM1_ShowFrags);
}

// 0x10003770  EV_PICKUP {pickup, lander}: 0x130000..03 +4 missiles (40 s), 0x130004..08 +50 %
// shield (20 s). The switch also has cases -6..-1 (+1 / +2 missiles, +25 % / full shield) that no
// pickup id can match.
static void DM1_OnPickup(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    ObjId lander = (ObjId)ev->arg[1];
    int32_t hp;
    if (id > (int32_t)DM1_PICKUP(3)) {
        if (id < (int32_t)DM1_PICKUP(4) || id > (int32_t)DM1_PICKUP(8))
            return;
        hp = (int32_t)Object_GetProperty(lander, PROP_HITPOINTS);
        hp = hp < 0x80 ? hp + 0x80 : 0x100;
        Object_SetProperty(lander, PROP_HITPOINTS, hp);
        Voice_ShieldAcquired(lander, 50);
        InTime1(20000, Object_SetAlive, id);
        return;
    }
    if (id >= (int32_t)DM1_PICKUP(0)) {                            // 0x1000389b
        if (Lander_AddMissiles(lander, 4) == 0)
            goto full;
        Voice_MissilesAcquired(lander);
        InTime1(40000, Object_SetAlive, id);
        return;
    }
    switch ((uint32_t)(id + 6)) {
    case 0:                                                         // 0x1000385e +1 missile
        if (Lander_AddMissiles(lander, 1) == 0)
            goto full;
        Voice_MissilesAcquired(lander);
        InTime1(10000, Object_SetAlive, id);
        return;
    case 1:                                                         // 0x10003821 +2 missiles
        if (Lander_AddMissiles(lander, 2) == 0)
            goto full;
        Voice_MissilesAcquired(lander);
        InTime1(20000, Object_SetAlive, id);
        return;
    case 3:                                                         // 0x100037e0 +25 % shield
        hp = (int32_t)Object_GetProperty(lander, PROP_HITPOINTS);
        hp = hp < 0xc0 ? hp + 0x40 : 0x100;
        Object_SetProperty(lander, PROP_HITPOINTS, hp);
        Voice_ShieldAcquired(lander, 25);
        InTime1(10000, Object_SetAlive, id);
        return;
    case 5:                                                         // 0x100037a1 full shield
        Object_SetProperty(lander, PROP_HITPOINTS, 0x100);
        Voice_ShieldAcquired(lander, 100);
        InTime1(40000, Object_SetAlive, id);
        return;
    default:
        return;
    }
full:                                                               // 0x100038ae
    Voice_MissilesFull(lander);
    Object_SetAlive((ObjId)id);
}

// 0x10003980  EV_TICK_1S: the four fuel dumps never run dry
static void DM1_FuelTick(const LanderEvent *ev)
{
    (void)ev;
    FuelDump_SetFuel(DM1_FUELDUMP(0x2d), 0x7fffffff);
    FuelDump_SetFuel(DM1_FUELDUMP(0x2e), 0x7fffffff);
    FuelDump_SetFuel(DM1_FUELDUMP(0x2f), 0x7fffffff);
    FuelDump_SetFuel(DM1_FUELDUMP(0x30), 0x7fffffff);
}

// 0x100039d0  set-up (host / single player)
static void DM1_Setup(void)
{
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x1e;
    Object_SetDead(OBJ_ID(OT_TURRET, 0x35));      // the three turrets at the centre
    Object_SetDead(OBJ_ID(OT_TURRET, 0x36));
    Object_SetDead(OBJ_ID(OT_TURRET, 0x37));
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM1_OnDoorEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, DM1_OnDoorLeave);
    EventType_AddHandler(EV_SWITCH, NULL, DM1_OnSwitch);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM1_OnTeleportTrigger);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, DM1_OnCrusherAnimDone);
    EventType_AddHandler(EV_POD_HOOKED, NULL, DM1_OnPodHooked);
    EventType_AddHandler(EV_PICKUP, NULL, DM1_OnPickup);
    EventType_AddHandler(EV_TICK_1S, NULL, DM1_FuelTick);
    st.hookPoint[2] = 0;
    st.hookPoint[1] = 0;
    st.hookPoint[0] = 0;
    switch (gNetworkGameType) {
    case 0:
    case 2:
        AnimPlay(DM1_ANIM(0x31), 2, 0x81);
        AnimPlay(DM1_ANIM(0x32), 2, 0x81);
        AnimPlay(DM1_ANIM(0x33), 2, 0x81);
        AnimPlay(DM1_ANIM(0x34), 2, 0x81);
        AnimSetUpdateInterval(DM1_ANIM(0x31), 2);
        AnimSetUpdateInterval(DM1_ANIM(0x32), 2);
        AnimSetUpdateInterval(DM1_ANIM(0x33), 2);
        AnimSetUpdateInterval(DM1_ANIM(0x34), 2);
        for (int32_t id = (int32_t)DM1_ANIM(0x31); id <= (int32_t)DM1_ANIM(0x34); id++)
            Sound_PlayLoop((ObjId)id, 0xb6, -1);
        Object_SetDead(DM1_BASE_A);
        Object_SetDead(DM1_BASE_B);
        if (gNetworkGameType == 2)
            DM1_BombSetup();
        else
            DM1_DmSetup();
        break;
    case 1:
        Object_SetDead(OBJ_ID(OT_TRIGGERBOX, 0xc8));   // two of the teleports
        Object_SetDead(OBJ_ID(OT_TRIGGERBOX, 0xca));
        Object_SetDead(OBJ_ID(OT_FORCEBOX, 0xdc));
        Object_SetDead(OBJ_ID(OT_FORCEBOX, 0xde));
        AnimPlay(DM1_ANIM(0x32), 2, 0x81);
        AnimPlay(DM1_ANIM(0x34), 2, 0x81);
        AnimSetUpdateInterval(DM1_ANIM(0x32), 2);
        AnimSetUpdateInterval(DM1_ANIM(0x34), 2);
        Sound_PlayLoop(DM1_ANIM(0x32), 0xb6, -1);
        Sound_PlayLoop(DM1_ANIM(0x34), 0xb6, -1);
        DM1_CtfSetup();
        break;
    }
    DM1_SpawnSetup();
    for (int i = 0; i < 32; i++)
        st.carried[i] = OBJ_NONE;
    if (gMultiPlayerGame != 0)
        EventType_AddHandler(EV_DESTROYED, NULL, DM1_OnFrag);
}

// 0x10003d10  LevelStartLevel_Lander (export 0x10002730)
static void DM1_StartLander(void)
{
    if (gMultiPlayerGame == 0) {
        DM1_Setup();
        MobObj_Wake(DM1_MOB_LANDER);
        MobObj_Wake(DM1_MOB_CAMERA);
        Object_SetProperty(DM1_CAMERA, PROP_TARGET, DM1_LANDER);
        return;
    }
    if (gJoinedGame == 0)
        DM1_Setup();
}

// 0x10003d60  network snapshot registration of everything
static void DM1_RegisterAll(void)
{
    GameState_RegisterDataBlock(st.doorInA, 0x18);
    GameState_RegisterDataBlock(st.doorInB, 0x18);
    GameState_RegisterDataBlock(st.crusher, 0x10);
    GameState_RegisterDataBlock(st.carried, 0x80);
    GameState_RegisterDataBlock(st.crusherBy, 0x10);
    GameState_RegisterDataBlock(st.doorState, 0x10);
    GameState_RegisterFunction(DM1_FN(DM1_OnDoorEnter));
    GameState_RegisterFunction(DM1_FN(DM1_OnDoorLeave));
    GameState_RegisterFunction(DM1_FN(DM1_OnSwitch));
    GameState_RegisterFunction(DM1_FN(DM1_OnTeleportTrigger));
    GameState_RegisterFunction(DM1_FN(DM1_OnCrusherAnimDone));
    GameState_RegisterFunction(DM1_FN(DM1_OnPodHooked));
    GameState_RegisterFunction(DM1_FN(DM1_OnFrag));
    GameState_RegisterFunction(DM1_FN(DM1_OnPickup));
    GameState_RegisterFunction(DM1_FN(DM1_FuelTick));
    DM1_RegisterHookPoint();
    DM1_SpawnRegisterState();
    DM1_FragsRegisterState();
    switch (gNetworkGameType) {
    case 0: DM1_DmRegisterState(); break;
    case 1: DM1_CtfRegisterState(); break;
    case 2: DM1_BombRegisterState(); break;
    }
}

// 0x10003e50  LevelStartLevel_Animcam (export 0x10002740): multiplayer, before the players spawn
static void DM1_StartAnimcam(void)
{
    DM1_RegisterAll();
    DM1_FragsSetup();
    if (gNetworkGameType == 2)
        DM1_BombCreateWindow();
}

// 0x10002760
static uint32_t DM1_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10002720  two calls of the empty 0x10003e80, return 0. Port: fresh DLL data.
static int32_t DM1_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    memcpy(st.doorState, dm1_doorStateInit, sizeof st.doorState);
    st.randSeed = 1;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText dm1_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000e178, .count = 3 * 11, .out = dm1_textAccident },
    { .kind = GT_POINTERS, .va = 0x1000e200, .count = 3 * 8, .out = dm1_textFrag },
    { .kind = GT_POINTERS, .va = 0x1000e280, .count = 3 * DM1_T_COUNT, .out = dm1_text },
    { .kind = GT_POINTERS, .va = 0x1000e388, .count = 3 * 7, .out = dm1_textYouDied },
    { 0 },
};

const mission_script_t mission_deathmat_1 = {
    .name          = "DEATHMAT\\1",
    .get_version   = DM1_GetVersion,
    .load          = DM1_Load,
    .start_lander  = DM1_StartLander,
    .start_flycam  = DM1_Nothing,      // export 0x10002750 = the empty 0x10003e80
    .start_animcam = DM1_StartAnimcam,
    .stop          = DM1_Nothing,
    .text          = dm1_gametext,
};