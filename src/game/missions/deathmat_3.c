// MISSIONS\DEATHMAT\3\MISSIONR.DLL - faithful C port.
//
// Multiplayer arena with three game types (gNetworkGameType):
//   0 deathmatch     - frags; the DLL tracks who last hit / bumped whom (data only)
//   1 capture the flag - pod 0x10c0026 + team A colour is team A's flag (home trigger 0x40008,
//                      captured at 0x40014), pod 0x10c0036 + team B colour team B's (0x40015 /
//                      0x40009); a dropped flag returns after 100 s, a captured one respawns in 10 s
//   2 "bomb"         - pod 0x10c0013 is attached to a random player with a 120-step countdown
//                      (one step per 1.1 s, per 0.1 s once the holder died); bumping into the
//                      holder (or the bomb) passes it on, then 6 s grace; at 0 it explodes
// Common to all: six spawn points per game type (marker mobs, a light pair and four smoke
// emitters each, jittered for 1.6 s after a spawn), the frag table / score panel (frag key,
// event 0x14), kill messages (random phrasing, 3 languages), four teleport triggers
// 0x40030..33, two door pairs on a trigger/anim-finished cycle, a door on two triggers, a gate on
// trigger 0x40019, re-spawning pickups.
//
// Single player (Mission_Run mode 0: gMultiPlayerGame = 0, only LevelStartLevel_Lander is called):
// the set-up runs, the lander mob 3 and camera mob 1 are woken; the frag panel / message line are
// not created (they are made by LevelStartLevel_Animcam), so no message is ever shown; game type
// 0 is used; each spawn (event 0x12) moves the lander to a random spawn marker. Multiplayer
// (mode 4): Animcam (state registration for network snapshots, frag panel, message line), the
// engine spawns every player (event 0 each), then LevelStartLevel_Lander runs the set-up on the
// host only (gJoinedGame = 0).
//
// Addresses are the DLL's (image base 0x10000000). Exports: LevelLoadLevel 0x100026f0,
// LevelStartLevel_Lander 0x10002700 (-> 0x100039c0), LevelStartLevel_Animcam 0x10002710
// (-> 0x10003b00), LevelStartLevel_Flycam 0x10002720 (-> 0x10003b30), LevelStopLevel 0x10002730
// (-> empty 0x10002750), LevelGetVersion 0x10002740. CRT used by the mission code: _ftol
// 0x10003bb0, sprintf 0x10003be0, strncpy 0x10003c50, qsort 0x10003d50, rand 0x10003f90 (MSVC,
// per-thread seed 1 at DLL load). No score library, no music.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define DM3_LANDER        MOB_ID(OT_LANDER, 3)        // 0x01010003 single player's lander
#define DM3_CAMERA        MOB_ID(OT_CAMERA, 1)        // 0x01060001
#define DM3_MOB_LANDER    3
#define DM3_MOB_CAMERA    1
#define DM3_MOB_FLYCAM    2
#define DM3_PLAYER_FIRST  MOB_ID(OT_LANDER, 3)        // 0x01010003..0x01010012: the 16 player landers
#define DM3_PLAYER_LAST   MOB_ID(OT_LANDER, 0x12)
#define DM3_BOMB          MOB_ID(OT_POD, 0x13)        // 0x010c0013 game type 2
#define DM3_MOB_BOMB      0x13
#define DM3_FLAG_A_BASE   MOB_ID(OT_POD, 0x26)        // 0x010c0026 + team A colour
#define DM3_FLAG_B_BASE   MOB_ID(OT_POD, 0x36)        // 0x010c0036 + team B colour
#define DM3_TRIG_HOME_A   OBJ_ID(OT_TRIGGERBOX, 0x08) // flag A home
#define DM3_TRIG_CAPT_B   OBJ_ID(OT_TRIGGERBOX, 0x09) // flag B captured here
#define DM3_TRIG_CAPT_A   OBJ_ID(OT_TRIGGERBOX, 0x14) // flag A captured here
#define DM3_TRIG_HOME_B   OBJ_ID(OT_TRIGGERBOX, 0x15) // flag B home
#define DM3_TRIG_TELE_FIRST OBJ_ID(OT_TRIGGERBOX, 0x30) // 0x40030..33 teleports
#define DM3_TRIG_TELE_LAST  OBJ_ID(OT_TRIGGERBOX, 0x33)
#define DM3_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))
#define DM3_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))
#define DM3_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))      // 0x00130000..07

#define DM3_IS_PLAYER(id) ((int32_t)(id) >= (int32_t)DM3_PLAYER_FIRST && (int32_t)(id) <= (int32_t)DM3_PLAYER_LAST)
#define DM3_FN(f)         ((void (*)(void))(f))      // GameState_RegisterFunction takes void(*)(void)

// ---- static data (.data / .rdata) ---------------------------------------------------------------
static const double dm3_hookScale = 128.0;           // 0x1000c1c8
static const double dm3_teleScale = 128.0;           // 0x1000c1d0
static const char   dm3_fmtInt[] = "%d";             // 0x1000e030
static const char   dm3_fmtStr[] = "%s";             // 0x1000e034

// 0x1000e038 / 0x1000e078: teleport k (trigger 0x40030 + k) moves by (to - from) * 128
static const int32_t dm3_teleFrom[4][4] = {
    { -40, -15, -50, 0 }, { -80, -15, -50, 0 }, { -80, -15, -10, 0 }, { -40, -15, -10, 0 },
};
static const int32_t dm3_teleTo[4][4] = {
    { 5, -39, 40, 0 }, { -123, -40, -105, 0 }, { -15, -75, -55, 0 }, { -135, -70, 5, 0 },
};

// 0x1000e0b8: [language][11] death without a killer (victim name)
static const char *dm3_textAccident[3][11];

// 0x1000e140: [language][8] frags (two names; order per dm3_fragVictimFirst)
static const char *dm3_textFrag[3][8];
static const int32_t dm3_fragVictimFirst[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };   // 0x1000e1a0

// 0x1000e1c0: [language][22] interface strings
enum {
    DM3_T_PLAYER, DM3_T_TITLE, DM3_T_TEAM_A, DM3_T_TEAM_B, DM3_T_JOINED, DM3_T_EXCHANGE_FAILED,
    DM3_T_QUIT, DM3_T_BOMB_COUNT, DM3_T_EXCHANGE_OK, DM3_T_YOU_KILLED, DM3_T_HAS_FLAG,
    DM3_T_A_RETRIEVED, DM3_T_B_RETRIEVED, DM3_T_A_RESPAWNED, DM3_T_B_RESPAWNED, DM3_T_CRUSHED,
    DM3_T_CRUSHED_SELF, DM3_T_A_STOLEN, DM3_T_B_STOLEN, DM3_T_KILLED_YOU, DM3_T_A_BEATEN,
    DM3_T_B_BEATEN, DM3_T_COUNT
};
static const char *dm3_text[3][DM3_T_COUNT];

// 0x1000e2c8: [language][7] the local player died without a killer
static const char *dm3_textYouDied[3][7];

// ---- DLL globals (.bss) -------------------------------------------------------------------------
typedef struct DM3Player {      // 0x10012ee0: 16 x 0x18
    int32_t id;                 // +0x00 player id
    char    name[16];           // +0x04
    int32_t frags;              // +0x14
} DM3Player;

static struct {
    int32_t   hookPoint[4];     // 0x10012bc0 pod tow point / 128 (always 0: default point)
    ObjId     flagA;            // 0x10012bd0
    int32_t   homeB;            // 0x10012bd4 flag B is at home
    int32_t   returnB;          // 0x10012bd8 100-ms ticks until a dropped flag B returns
    int32_t   homeA;            // 0x10012bdc
    int32_t   returnA;          // 0x10012be0
    ObjId     flagB;            // 0x10012be4
    struct { ObjId who; int32_t time; } lastHit[16];   // 0x10012be8 (written, never read)
    char      rowName[16][16];  // 0x10012c68 frag panel rows (sorted)
    Window   *panel;            // 0x10012d68 frag panel window (NULL = hidden)
    char      rowFrags[16][16]; // 0x10012d70
    int32_t   panelX, panelY, panelW, panelH;  // 0x10012e70
    DM3Player *sorted[16];      // 0x10012e80
    char      teamText[2][16];  // 0x10012ec0
    DM3Player players[16];      // 0x10012ee0
    Window   *msgWin;           // 0x10013060 message line
    char      empty[4];         // 0x10013064 "" (clears the message line)
    int32_t   grace;            // 0x10013068 bomb: seconds of grace after a pass
    char      bombText[20];     // 0x10013070
    int32_t   bombFast;         // 0x10013084 holder died: count down every tick
    int32_t   bombCount;        // 0x10013088
    int32_t   bombTicks;        // 0x1001308c
    Window   *bombWin;          // 0x10013090
    int32_t   spawnFx[6];       // 0x10013098 100-ms ticks of spawn effects left per point
    int32_t   nSpawn;           // 0x100130b0
    int32_t   doorIn17;         // 0x100130b4 objects inside trigger 0x40017
    int32_t   doorIn18;         // 0x100130b8 objects inside trigger 0x40018
    ObjId     carried[32];      // 0x10013680 per player id: pod it carries (or -1)
    ObjId     carrierA;         // 0x100137a0 lander carrying flag A (-1)
    ObjId     carrierB;         // 0x100137a4
    int32_t   capturedB;        // 0x100137a8 times flag B was captured (the frag panel's team B score)
    int32_t   capturedA;        // 0x100137ac times flag A was captured (the frag panel's team A score)
    ObjId     bombHolder;       // 0x10013700
    char      msg[128];         // 0x10013720 message line text
    uint32_t  randSeed;         // CRT _holdrand (per-thread data, 1 at DLL load)
} st;

// 0x10003f90  MSVC rand()
static int32_t DM3_Rand(void)
{
    st.randSeed = st.randSeed * 214013u + 2531011u;
    return (int32_t)((st.randSeed >> 16) & 0x7fff);
}

// The original indexes carried[] with Player_GetId() unchecked (-1 would write just before it)
static void DM3_SetCarried(int32_t pid, ObjId v)
{
    if (pid >= 0 && pid < 32)
        st.carried[pid] = v;
}

// 0x10001000  (InTime target) wake a pod and set its tow point
static void DM3_WakePod(intptr_t pod)
{
    MobObj_Wake((int32_t)((uint32_t)pod & 0xffff));
    Pod_Script_SetupHookPoint((ObjId)pod, (int32_t)(st.hookPoint[0] * dm3_hookScale),
                              (int32_t)(st.hookPoint[1] * dm3_hookScale),
                              (int32_t)(st.hookPoint[2] * dm3_hookScale));
}

// 0x10001060
static void DM3_RegisterHookPoint(void)
{
    GameState_RegisterDataBlock(st.hookPoint, 0x10);
}

static void DM3_Message(void)
{
    WindowSetText(st.msgWin, st.msg);
}

// sprintf(msg, text) without arguments in the original; none of these texts contains a '%'
static void DM3_SetMsg(const char *text)
{
    snprintf(st.msg, sizeof st.msg, "%s", text);
}

static const char *DM3_PlayerName(int32_t pid);

// ================================================================================================
// Capture the flag (game type 1)
// ================================================================================================

static void DM3_CtfReturnTickA(const LanderEvent *ev);
static void DM3_CtfReturnTickB(const LanderEvent *ev);

// 0x10001080  EV_POD_HOOKED {pod, lander}: a flag is picked up (any pod other than flag A
// counts as flag B); a returning flag stops its countdown; message if it was not at home
static void DM3_CtfOnPodHooked(const LanderEvent *ev)
{
    int32_t pid = -1;
    if ((ObjId)ev->arg[0] == st.flagA) {
        st.carrierA = (ObjId)ev->arg[1];
        if (st.returnA != 0) {
            EventType_RemoveHandler(EV_TICK_100MS, DM3_CtfReturnTickA);
            st.returnA = 0;
        }
        if (st.homeA == 0)
            pid = Player_GetId((ObjId)ev->arg[1]);
    } else {
        st.carrierB = (ObjId)ev->arg[1];
        if (st.returnB != 0) {
            EventType_RemoveHandler(EV_TICK_100MS, DM3_CtfReturnTickB);
            st.returnB = 0;
        }
        if (st.homeB == 0)
            pid = Player_GetId((ObjId)ev->arg[1]);
    }
    if (pid != -1) {
        const char *name = DM3_PlayerName(pid);
        sprintf(st.msg, dm3_text[gLanguage][DM3_T_HAS_FLAG], name);
        DM3_Message();
    }
}

// 0x10001160  EV_TRIGGER_LEAVE: a flag left its home trigger
static void DM3_CtfOnTriggerLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == DM3_TRIG_HOME_A && (ObjId)ev->arg[0] == st.flagA) {
        st.homeA = 0;
        DM3_SetMsg(dm3_text[gLanguage][DM3_T_A_STOLEN]);
        DM3_Message();
    }
    if ((ObjId)ev->arg[1] == DM3_TRIG_HOME_B && (ObjId)ev->arg[0] == st.flagB) {
        st.homeB = 0;
        DM3_SetMsg(dm3_text[gLanguage][DM3_T_B_STOLEN]);
        DM3_Message();
    }
}

// 0x10001220  EV_TRIGGER_ENTER: a flag reached its capture trigger: count it, release it, put it
// back home in 10 s
static void DM3_CtfOnTriggerEnter(const LanderEvent *ev)
{
    ObjId flag = (ObjId)ev->arg[0];
    if ((ObjId)ev->arg[1] == DM3_TRIG_CAPT_B && flag == st.flagB) {
        st.capturedB++;
        if (st.carrierB != OBJ_NONE) {
            Object_SetProperty(flag, PROP_TARGET, -1);
            Object_SetProperty(flag, PROP_TARGET, -2);
        }
        MobObj_Sleep((int32_t)(flag & 0xffff));
        InTime1(10000, DM3_WakePod, flag);
        st.homeB = 1;
        if (st.carrierB != OBJ_NONE) {
            int32_t pid = Player_GetId(st.carrierB);
            const char *name = DM3_PlayerName(pid);
            sprintf(st.msg, dm3_text[gLanguage][DM3_T_B_BEATEN], name);
            DM3_Message();
            st.carrierB = OBJ_NONE;
            DM3_SetCarried(pid, OBJ_NONE);
        }
    } else if ((ObjId)ev->arg[1] == DM3_TRIG_CAPT_A && flag == st.flagA) {
        st.capturedA++;
        if (st.carrierA != OBJ_NONE) {
            Object_SetProperty(flag, PROP_TARGET, -1);
            Object_SetProperty(flag, PROP_TARGET, -2);
        }
        MobObj_Sleep((int32_t)(flag & 0xffff));
        InTime1(10000, DM3_WakePod, flag);
        st.homeA = 1;
        if (st.carrierA != OBJ_NONE) {
            int32_t pid = Player_GetId(st.carrierA);
            const char *name = DM3_PlayerName(pid);
            sprintf(st.msg, dm3_text[gLanguage][DM3_T_A_BEATEN], name);
            DM3_Message();
            st.carrierA = OBJ_NONE;
            DM3_SetCarried(pid, OBJ_NONE);
        }
    }
}

// 0x10001410  EV_TICK_100MS while dropped flag A waits: back home at 0
static void DM3_CtfReturnTickA(const LanderEvent *ev)
{
    (void)ev;
    if (--st.returnA != 0)
        return;
    MobObj_Sleep((int32_t)(st.flagA & 0xffff));
    DM3_WakePod(st.flagA);
    DM3_SetMsg(dm3_text[gLanguage][DM3_T_A_RESPAWNED]);
    DM3_Message();
    st.homeA = 1;
    EventType_RemoveHandler(EV_TICK_100MS, DM3_CtfReturnTickA);
}

// 0x100014a0  same for flag B
static void DM3_CtfReturnTickB(const LanderEvent *ev)
{
    (void)ev;
    if (--st.returnB != 0)
        return;
    MobObj_Sleep((int32_t)(st.flagB & 0xffff));
    DM3_WakePod(st.flagB);
    DM3_SetMsg(dm3_text[gLanguage][DM3_T_B_RESPAWNED]);
    DM3_Message();
    st.homeB = 1;
    EventType_RemoveHandler(EV_TICK_100MS, DM3_CtfReturnTickB);
}

// 0x10001530  start a dropped flag's return countdown (100-ms ticks)
static void DM3_CtfStartReturn(ObjId flag, int32_t ticks)
{
    if (flag == st.flagA) {
        st.returnA = ticks;
        EventType_AddHandler(EV_TICK_100MS, NULL, DM3_CtfReturnTickA);
        return;
    }
    st.returnB = ticks;
    EventType_AddHandler(EV_TICK_100MS, NULL, DM3_CtfReturnTickB);
}

// 0x10001580  EV_DESTROYED: a flag carrier died: drop the flag, it returns in 100 s
static void DM3_CtfOnDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (!DM3_IS_PLAYER(id))
        return;
    int32_t pid = Player_GetId(id);
    if (st.carrierA == id) {
        Object_SetProperty(st.flagA, PROP_TARGET, -3);
        DM3_SetCarried(pid, OBJ_NONE);
        st.carrierA = OBJ_NONE;
        DM3_CtfStartReturn(st.flagA, 1000);
    }
    if (st.carrierB == id) {
        Object_SetProperty(st.flagB, PROP_TARGET, -3);
        DM3_SetCarried(pid, OBJ_NONE);
        st.carrierB = OBJ_NONE;
        DM3_CtfStartReturn(st.flagB, 1000);
    }
}

// 0x10001640  the flags are the pods of the teams' colours; the bases show the colour
static void DM3_CtfChooseFlags(void)
{
    if (gTeamAShipColour != 0)
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x12), (int32_t)gTeamAShipColour);
    if (gTeamBShipColour != 0)
        Object_AdvanceDestroyLevel(OBJ_ID(OT_DESTRUCTIBLE, 0x13), (int32_t)gTeamBShipColour);
    st.flagA = DM3_FLAG_A_BASE + gTeamAShipColour;
    st.flagB = DM3_FLAG_B_BASE + gTeamBShipColour;
}

// 0x100016a0
static void DM3_CtfSetup(void)
{
    DM3_CtfChooseFlags();
    Object_SetProperty(DM3_TRIG_HOME_A, PROP_TARGET, st.flagA);
    Object_SetProperty(DM3_TRIG_CAPT_B, PROP_TARGET, st.flagB);
    Object_SetProperty(DM3_TRIG_HOME_B, PROP_TARGET, st.flagB);
    Object_SetProperty(DM3_TRIG_CAPT_A, PROP_TARGET, st.flagA);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_CtfOnTriggerEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, DM3_CtfOnTriggerLeave);
    EventType_AddHandler(EV_DESTROYED, NULL, DM3_CtfOnDestroyed);
    st.carrierB = OBJ_NONE;
    st.carrierA = OBJ_NONE;
    EventType_AddHandler(EV_POD_HOOKED, NULL, DM3_CtfOnPodHooked);
    st.returnB = 0;
    st.returnA = 0;
    st.capturedA = 0;
    st.capturedB = 0;
    st.homeB = 1;
    st.homeA = 1;
    DM3_WakePod(st.flagA);
    DM3_WakePod(st.flagB);
    Object_SetProperty(st.flagA, PROP_FLAGS, 0x80);
    Object_SetProperty(st.flagB, PROP_FLAGS, 0x40);
}

// 0x100017c0  network snapshot registration (game type 1)
static void DM3_CtfRegisterState(void)
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
    GameState_RegisterFunction(DM3_FN(DM3_CtfReturnTickA));
    GameState_RegisterFunction(DM3_FN(DM3_CtfReturnTickB));
    GameState_RegisterFunction(DM3_FN(DM3_CtfOnTriggerEnter));
    GameState_RegisterFunction(DM3_FN(DM3_CtfOnTriggerLeave));
    GameState_RegisterFunction(DM3_FN(DM3_CtfOnPodHooked));
    GameState_RegisterFunction(DM3_FN(DM3_CtfOnDestroyed));
}

// ================================================================================================
// Deathmatch (game type 0)
// ================================================================================================

// 0x10001890  EV_DESTROYED: a player died: drop the pod it carried
static void DM3_DmOnDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (!DM3_IS_PLAYER(id))
        return;
    int32_t pid = Player_GetId(id);
    ObjId pod = (pid >= 0 && pid < 32) ? st.carried[pid] : OBJ_NONE;
    if (pod != OBJ_NONE)
        Object_SetProperty(pod, PROP_TARGET, -3);
}

// 0x100018d0  EV_COLLISION {body, other, impulse}: two players bumped: remember each other.
// The handler masks the event's "other" id in place (later handlers see the masked value).
static void DM3_DmOnCollision(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;   // the original writes into the event record
    int32_t a = Player_GetId((ObjId)e->arg[0]);
    if (a == -1)
        return;
    e->arg[1] = (intptr_t)((uint32_t)e->arg[1] & 0x3ffffffu);
    if (!DM3_IS_PLAYER(e->arg[1]))
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
static void DM3_DmOnHit(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if (!DM3_IS_PLAYER(e->arg[0]) || !DM3_IS_PLAYER(e->arg[2]))
        return;
    int32_t a = Player_GetId((ObjId)e->arg[0]);
    if (a < 0 || a > 15)
        return;
    st.lastHit[a].who = (ObjId)e->arg[2];
    st.lastHit[a].time = gGameUpdateTime;
}

// 0x100019b0
static void DM3_DmSetup(void)
{
    EventType_AddHandler(EV_DESTROYED, NULL, DM3_DmOnDestroyed);
    EventType_AddHandler(EV_COLLISION, NULL, DM3_DmOnCollision);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, DM3_DmOnHit);
    for (int i = 0; i < 16; i++)
        st.lastHit[i].who = OBJ_NONE;
}

// 0x10001a00  network snapshot registration (game type 0)
static void DM3_DmRegisterState(void)
{
    GameState_RegisterFunction(DM3_FN(DM3_DmOnDestroyed));
    GameState_RegisterFunction(DM3_FN(DM3_DmOnCollision));
    GameState_RegisterFunction(DM3_FN(DM3_DmOnHit));
    GameState_RegisterDataBlock(st.lastHit, 0x80);
}

// ================================================================================================
// Players, messages, frag panel
// ================================================================================================

// 0x10001a40  the player's name (kept in its record; "PLAYER" when the engine has none)
static const char *DM3_PlayerName(int32_t pid)
{
    char *dst = st.players[pid & 15].name;
    const char *name = Message_GetNameFromId(pid);
    if (name != NULL) {
        strncpy(dst, name, 15);
        return dst;
    }
    strcpy(dst, dm3_text[gLanguage][DM3_T_PLAYER]);
    return dst;
}

// 0x10001ab0  qsort: most frags first, then lower player id
static int DM3_CompareFrags(const void *pa, const void *pb)
{
    const DM3Player *a = *(DM3Player *const *)pa;
    const DM3Player *b = *(DM3Player *const *)pb;
    if (a->frags > b->frags)
        return -1;
    if (a->frags < b->frags)
        return 1;
    if (a->id < b->id)
        return -1;
    return a->id > b->id;
}

// 0x10001af0  EV_PLAYER_JOINED {lander}
static void DM3_OnPlayerJoined(const LanderEvent *ev)
{
    int32_t pid = Lander_GetPlayerId((ObjId)ev->arg[0]);
    if (pid == -1)
        return;
    if ((int32_t)ev->arg[0] != gThisPlayer) {
        const char *name = DM3_PlayerName(pid);
        sprintf(st.msg, dm3_text[gLanguage][DM3_T_JOINED], name);
        DM3_Message();
    }
    st.players[pid & 15].frags = 0;
}

// 0x10001b70  EV_PLAYER_LEFT {lander, name}
static void DM3_OnPlayerLeft(const LanderEvent *ev)
{
    if ((int32_t)ev->arg[0] == gThisPlayer)
        return;
    sprintf(st.msg, dm3_text[gLanguage][DM3_T_QUIT], (const char *)ev->arg[1]);
    DM3_Message();
}

static void DM3_HideFrags(void);
static void DM3_ShowFrags(void);

// 0x10001bd0  EV_LANDER_SPAWNED {lander}: the local player is back: hide the panel and message
static void DM3_OnLocalRespawn(const LanderEvent *ev)
{
    if ((int32_t)ev->arg[0] != gThisPlayer)
        return;
    DM3_HideFrags();
    WindowSetText(st.msgWin, st.empty);
}

// 0x10001c00  EV_FRAG_KEY {0}: toggle the frag panel
static void DM3_OnFragKey(const LanderEvent *ev)
{
    if (ev->arg[0] != 0)
        return;
    if (st.panel != NULL)
        DM3_HideFrags();
    else
        DM3_ShowFrags();
}

// 0x10001c20
static void DM3_AddFrag(int32_t pid)
{
    st.players[pid & 15].frags++;
}

// 0x10001c30
static void DM3_SubFrag(int32_t pid)
{
    st.players[pid & 15].frags--;
}

static void DM3_UpdateFrags(void);

// 0x10001c40  g2dRenderCallback while the frag panel is shown
static void DM3_RenderFrags(void *ot)
{
    int32_t x = st.panelX, y = st.panelY, w, h;
    const char *const *text = dm3_text[gLanguage];
    Win_SetTransform(st.panel);
    Font_GetStringLen(text[DM3_T_TITLE], &w, &h, 1);
    Font_Print(text[DM3_T_TITLE], ((st.panelW - w) >> 1) + x, y, 0xf7ff, 1, ot);
    y += 0x18;
    if (gNetworkGameType == 1) {
        for (int t = 0; t < 2; t++) {
            Font_Print(text[DM3_T_TEAM_A + t], x, y, 0xb3ad00, 1, ot);
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
    DM3_UpdateFrags();
}

// 0x10001e60  sort the players by frags, make the rows, size and centre the panel
static void DM3_UpdateFrags(void)
{
    int32_t h = (gNbPlayers + 2) * 12;
    qsort(st.sorted, 16, sizeof st.sorted[0], DM3_CompareFrags);
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < gNbPlayers; j++) {
            if (gPlayerIds[j] != st.sorted[i]->id)
                continue;
            sprintf(st.rowName[i], dm3_fmtStr, DM3_PlayerName(st.sorted[i]->id));
            sprintf(st.rowFrags[i], dm3_fmtInt, st.sorted[i]->frags);
        }
    }
    if (gNetworkGameType == 1) {
        sprintf(st.teamText[0], dm3_fmtInt, st.capturedA);
        sprintf(st.teamText[1], dm3_fmtInt, st.capturedB);
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

// 0x10001fb0  (also an InTime target)
static void DM3_ShowFrags(void)
{
    if (st.panel != NULL)
        return;
    g2dRenderCallback = DM3_RenderFrags;
    st.panel = WindowCreate(9, NULL);
    WindowSetBorderInfo(st.panel, &BorderInfo);
    DM3_UpdateFrags();
    Window_CreateTransform(st.panel);
}

// 0x10002000
static void DM3_HideFrags(void)
{
    if (st.panel != NULL) {
        WindowDestroy(st.panel);
        st.panel = NULL;
    }
    g2dRenderCallback = NULL;
}

// 0x10002030  (LevelStartLevel_Animcam only)
static void DM3_FragsSetup(void)
{
    st.panel = NULL;
    for (int i = 0; i < 16; i++) {
        st.players[i].id = i;
        st.players[i].frags = 0;
        st.sorted[i] = &st.players[i];
    }
    st.msgWin = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.msgWin, 20, 20, 500, 12);
    EventType_AddHandler(EV_PLAYER_JOINED, NULL, DM3_OnPlayerJoined);
    EventType_AddHandler(EV_PLAYER_LEFT, NULL, DM3_OnPlayerLeft);
    EventType_AddHandler(EV_LANDER_SPAWNED, NULL, DM3_OnLocalRespawn);
    EventType_AddHandler(EV_FRAG_KEY, NULL, DM3_OnFragKey);
}

// 0x100020c0
static void DM3_FragsRegisterState(void)
{
    GameState_RegisterDataBlock(st.players, 0x180);
    GameState_RegisterFunction(DM3_FN(DM3_OnPlayerJoined));
    GameState_RegisterFunction(DM3_FN(DM3_OnPlayerLeft));
    GameState_RegisterFunction(DM3_FN(DM3_OnLocalRespawn));
    GameState_RegisterFunction(DM3_FN(DM3_OnFragKey));
    GameState_UnRegisterFunction(DM3_FN(DM3_ShowFrags));
}

// ================================================================================================
// Bomb (game type 2)
// ================================================================================================

// 0x10002120
static ObjId DM3_RandomPlayer(void)
{
    return gPlayerObjects[LndrRand() % (uint32_t)gNbPlayers];
}

static void DM3_BombTick(const LanderEvent *ev);
static void DM3_BombOnCollision(const LanderEvent *ev);

// 0x10002140  (InTime target) give the bomb to a random living player, 120 steps
static void DM3_BombStart(void)
{
    ObjId lander = DM3_RandomPlayer();
    if (Object_GetProperty(lander, PROP_HITPOINTS) == 0) {
        InTime0(500, DM3_BombStart);
        return;
    }
    int32_t pos[3];
    pos[0] = ((const int32_t *)Object_GetProperty(lander, PROP_POSITION))[0];
    pos[1] = ((const int32_t *)Object_GetProperty(lander, PROP_POSITION))[1];
    pos[2] = ((const int32_t *)Object_GetProperty(lander, PROP_POSITION))[2];
    pos[1] += 0x180;
    Object_SetProperty(DM3_BOMB, PROP_POSITION, (intptr_t)pos);
    DM3_WakePod(DM3_BOMB);
    Pod_Script_SetupDrag(DM3_BOMB, 0x400);
    Object_SetProperty(DM3_BOMB, PROP_TARGET, lander);
    DM3_SetCarried(Player_GetId(lander), lander);   // sic: the lander id, not the pod
    st.bombHolder = lander;
    st.bombCount = 0x78;
    st.bombFast = 0;
    st.bombTicks = 0;
    EventType_AddHandler(EV_TICK_100MS, NULL, DM3_BombTick);
}

// 0x10002250  after a pass: no passing, no countdown for 6 s
static void DM3_BombDisarm(void)
{
    EventType_RemoveHandler(EV_COLLISION, DM3_BombOnCollision);
    EventType_RemoveHandler(EV_TICK_100MS, DM3_BombTick);
    st.grace = 6;
}

// 0x10002280
static void DM3_BombArm(void)
{
    EventType_AddHandler(EV_COLLISION, NULL, DM3_BombOnCollision);
    EventType_AddHandler(EV_TICK_100MS, NULL, DM3_BombTick);
}

// 0x100022b0  EV_TICK_1S during the grace time
static void DM3_BombGraceTick(const LanderEvent *ev)
{
    (void)ev;
    if (--st.grace != 0)
        return;
    DM3_BombArm();
    EventType_RemoveHandler(EV_TICK_1S, DM3_BombGraceTick);
}

// 0x100022e0  attach the bomb to newHolder; if it does not take, give it back to oldHolder
static int32_t DM3_BombPass(ObjId oldHolder, ObjId newHolder, ObjId bomb)
{
    Object_SetProperty(bomb, PROP_TARGET, newHolder);
    if ((ObjId)Object_GetProperty(bomb, PROP_TARGET) != newHolder) {
        if (oldHolder != OBJ_NONE)
            Object_SetProperty(bomb, PROP_TARGET, oldHolder);
        return 0;
    }
    DM3_SetCarried(Player_GetId(newHolder), newHolder);
    st.bombHolder = newHolder;
    return 1;
}

static void DM3_BombExplode(void);

// 0x10002350  EV_TICK_100MS: one count per 11 ticks (every tick once the holder died)
static void DM3_BombTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.bombFast == 0) {
        if (st.bombTicks++ != 10)
            return;
        st.bombTicks = 0;
    }
    if (--st.bombCount == 0)
        DM3_BombExplode();
    sprintf(st.bombText, dm3_text[gLanguage][DM3_T_BOMB_COUNT], st.bombCount);
    WindowSetText(st.bombWin, st.bombText);
}

// 0x100023d0  EV_COLLISION {body, other, impulse}: a living player touched the holder (or the
// bomb): pass it. Masks both ids in the event record in place.
static void DM3_BombOnCollision(const LanderEvent *ev)
{
    LanderEvent *e = (LanderEvent *)ev;
    e->arg[0] = (intptr_t)((uint32_t)e->arg[0] & 0x3ffffffu);
    if (!DM3_IS_PLAYER(e->arg[0]))
        return;
    if (Object_GetProperty((ObjId)e->arg[0], PROP_HITPOINTS) == 0)
        return;
    e->arg[1] = (intptr_t)((uint32_t)e->arg[1] & 0x3ffffffu);
    ObjId holder = st.bombHolder;
    if (!(DM3_IS_PLAYER(e->arg[1]) && holder == (ObjId)e->arg[1])) {
        if ((ObjId)e->arg[1] != DM3_BOMB || holder == (ObjId)e->arg[0])
            return;
    }
    if (DM3_BombPass(holder, (ObjId)e->arg[0], DM3_BOMB)) {
        DM3_SetMsg(dm3_text[gLanguage][DM3_T_EXCHANGE_OK]);
        DM3_Message();
        DM3_BombDisarm();
        EventType_AddHandler(EV_TICK_1S, NULL, DM3_BombGraceTick);
        return;
    }
    DM3_SetMsg(dm3_text[gLanguage][DM3_T_EXCHANGE_FAILED]);
    DM3_Message();
}

// 0x100024f0  EV_DESTROYED: the holder died: drop the bomb, count down fast
static void DM3_BombOnDestroyed(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    if (!DM3_IS_PLAYER(id) || st.bombHolder != id)
        return;
    st.bombFast = 1;
    Object_SetProperty(DM3_BOMB, PROP_TARGET, -3);
}

// 0x10002530  count reached 0: explosion at the bomb, a new bomb in 5 s
static void DM3_BombExplode(void)
{
    if (st.grace != 0) {
        st.grace = 0;
        EventType_RemoveHandler(EV_TICK_1S, DM3_BombGraceTick);
        EventType_AddHandler(EV_COLLISION, NULL, DM3_BombOnCollision);
    } else {
        EventType_RemoveHandler(EV_TICK_100MS, DM3_BombTick);
    }
    if (st.bombHolder != OBJ_NONE) {
        int32_t pid = Lander_GetPlayerId(st.bombHolder);
        st.bombHolder = OBJ_NONE;
        DM3_SetCarried(pid, OBJ_NONE);
    }
    Explosion_CreateDynamic(1, 0x78, (const int32_t *)Object_GetProperty(DM3_BOMB, PROP_POSITION));
    MobObj_Sleep(DM3_MOB_BOMB);
    InTime0(5000, DM3_BombStart);
}

// 0x100025f0  the bomb count line
static void DM3_BombCreateWindow(void)
{
    st.bombWin = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.bombWin, 20, 40, 128, 12);
}

// 0x10002620
static void DM3_BombSetup(void)
{
    st.bombHolder = OBJ_NONE;
    EventType_AddHandler(EV_COLLISION, NULL, DM3_BombOnCollision);
    EventType_AddHandler(EV_DESTROYED, NULL, DM3_BombOnDestroyed);
    InTime0(5000, DM3_BombStart);
    DM3_BombCreateWindow();
}

// 0x10002670  network snapshot registration (game type 2)
static void DM3_BombRegisterState(void)
{
    GameState_RegisterDataBlock(&st.bombHolder, 4);
    GameState_RegisterDataBlock(&st.bombCount, 4);
    GameState_RegisterDataBlock(&st.bombTicks, 4);
    GameState_RegisterDataBlock(&st.bombFast, 4);
    GameState_RegisterDataBlock(&st.grace, 4);
    GameState_RegisterFunction(DM3_FN(DM3_BombTick));
    GameState_RegisterFunction(DM3_FN(DM3_BombGraceTick));
    GameState_RegisterFunction(DM3_FN(DM3_BombStart));
    GameState_RegisterFunction(DM3_FN(DM3_BombOnCollision));
    GameState_RegisterFunction(DM3_FN(DM3_BombOnDestroyed));
}

// 0x10002750  LevelStopLevel, LevelLoadLevel's helper and an EV_TICK_1S handler: empty
static void DM3_Nothing(void)
{
}

static void DM3_OnTick1sNothing(const LanderEvent *ev)
{
    (void)ev;   // 0x10002750 registered as an event handler
}

// ================================================================================================
// Spawn points (6 per game type)
// ================================================================================================

// 0x10002760  object of spawn point i: kind 0 anim (0x30039), 1/2 lights (0x16003a/b), 3 first of
// four smoke emitters (0xd003c), 4 marker mob; 7 objects per point, 6 points per game type
static ObjId DM3_SpawnObject(int32_t i, int32_t kind, int32_t type)
{
    if (kind == 4) {
        if (type == 0 || i < 0)
            return (ObjId)(i + 0x1190014);
        if (type == 1)
            return (ObjId)(i + 0x119001a);
        return (ObjId)(i + 0x1190020);
    }
    int32_t base;
    switch (kind) {
    case 0: base = 0x30039; break;
    case 1: base = 0x16003a; break;
    case 2: base = 0x16003b; break;
    case 3: base = 0xd003c; break;
    default: base = i; break;
    }
    if (type == 0 || i < 0)
        return (ObjId)(i * 7 + base);
    if (type == 1)
        return (ObjId)((i + 6) * 7 + base);
    return (ObjId)((i + 12) * 7 + base);
}

// 0x10002810
static void DM3_SpawnFxOn(int32_t i)
{
    Object_SetAlive(DM3_SpawnObject(i, 1, gNetworkGameType));
    ObjId e = DM3_SpawnObject(i, 3, gNetworkGameType);
    for (int32_t k = 0; k < 4; k++)
        Object_SetAlive(e + k);
}

// 0x10002870
static void DM3_SpawnFxOff(int32_t i)
{
    Object_SetDead(DM3_SpawnObject(i, 1, gNetworkGameType));
    ObjId e = DM3_SpawnObject(i, 3, gNetworkGameType);
    for (int32_t k = 0; k < 4; k++)
        Object_SetDead(e + k);
}

// 0x100028d0  move one of the point's emitters by a random vector of length 1536
static void DM3_SpawnFxJitter(int32_t i)
{
    int32_t k = DM3_Rand() & 3;
    int32_t v[3];
    v[0] = (DM3_Rand() & 0xfff) - 0x800;
    v[1] = (DM3_Rand() & 0xfff) - 0x800;
    v[2] = (DM3_Rand() & 0xfff) - 0x800;
    FwVectorNormalise(v);
    v[0] = (v[0] * 3 << 9) >> 12;
    v[1] = (v[1] * 3 << 9) >> 12;
    v[2] = (v[2] * 3 << 9) >> 12;
    ObjId e = DM3_SpawnObject(i, 3, gNetworkGameType) + (ObjId)k;
    FwVectorAdd(v, (const int32_t *)Object_GetProperty(e, PROP_POSITION), v);
    Object_SetProperty(e, PROP_POSITION, (intptr_t)v);
}

// 0x100029b0
static void DM3_SpawnFxStart(int32_t i)
{
    if (st.spawnFx[i] == 0)
        DM3_SpawnFxOn(i);
    st.spawnFx[i] += 0x10;
}

// 0x100029e0  EV_TICK_100MS
static void DM3_SpawnFxTick(const LanderEvent *ev)
{
    (void)ev;
    for (int32_t i = 0; i < st.nSpawn; i++) {
        if (st.spawnFx[i] == 0)
            continue;
        if (--st.spawnFx[i] == 0)
            DM3_SpawnFxOff(i);
        else
            DM3_SpawnFxJitter(i);
    }
}

// 0x10002a20  EV_LANDER_SPAWNED {lander}: put it on a random spawn point (team parity in CTF)
static void DM3_OnLanderSpawned(const LanderEvent *ev)
{
    ObjId lander = (ObjId)ev->arg[0];
    if (!DM3_IS_PLAYER(lander) || Player_GetId(lander) == -1)
        return;
    int32_t i = (int32_t)(LndrRand() % (uint32_t)st.nSpawn);
    if (gNetworkGameType == 1) {
        if (Object_GetProperty(lander, PROP_FLAGS) & 0x800)
            i |= 1;
        else
            i &= ~1;
    }
    SnapToTarget(lander, DM3_SpawnObject(i, 4, gNetworkGameType));
    if ((int32_t)lander == gThisPlayer)
        AutoCam_SnapToTarget(DM3_CAMERA);
    DM3_SpawnFxStart(i);
    Object_GetProperty(lander, PROP_POSITION);   // result unused
}

// 0x10002ae0  the spawn points of this game type: idle animation
static void DM3_SpawnSetStart(int32_t type, int32_t n)
{
    st.nSpawn = n;
    for (int32_t i = 0; i < n; i++)
        AnimPlay(DM3_SpawnObject(i, 0, type), 5, 0x81);
}

// 0x10002b30  the spawn points of another game type: removed
static void DM3_SpawnSetKill(int32_t type, int32_t n)
{
    for (int32_t i = 0; i < n; i++) {
        Object_SetDead(DM3_SpawnObject(i, 0, type));
        Object_SetDead(DM3_SpawnObject(i, 2, type));
        Object_SetDead(DM3_SpawnObject(i, 1, type));
        ObjId e = DM3_SpawnObject(i, 3, type);
        for (int32_t k = 0; k < 4; k++)
            Object_SetDead(e + k);
    }
}

// 0x10002bc0
static void DM3_SpawnSetup(void)
{
    switch (gNetworkGameType) {
    case 0:
        DM3_SpawnSetStart(0, 6);
        DM3_SpawnSetKill(1, 6);
        DM3_SpawnSetKill(2, 6);
        break;
    case 1:
        DM3_SpawnSetStart(1, 6);
        DM3_SpawnSetKill(0, 6);
        DM3_SpawnSetKill(2, 6);
        break;
    case 2:
        DM3_SpawnSetStart(2, 6);
        DM3_SpawnSetKill(1, 6);
        DM3_SpawnSetKill(0, 6);
        break;
    }
    for (int32_t i = 0; i < st.nSpawn; i++)
        DM3_SpawnFxOff(i);
    EventType_AddHandler(EV_LANDER_SPAWNED, NULL, DM3_OnLanderSpawned);
    EventType_AddHandler(EV_TICK_100MS, NULL, DM3_SpawnFxTick);
}

// 0x10002c70
static void DM3_SpawnRegisterState(void)
{
    GameState_RegisterDataBlock(st.spawnFx, 0x18);
    GameState_RegisterDataBlock(&st.nSpawn, 4);
    GameState_RegisterFunction(DM3_FN(DM3_OnLanderSpawned));
    GameState_RegisterFunction(DM3_FN(DM3_SpawnFxTick));
}

// ================================================================================================
// Frags (multiplayer only), level machinery
// ================================================================================================

// 0x10002cb0  EV_DESTROYED {victim, source, size}: frag counting and the kill message
static void DM3_OnFrag(const LanderEvent *ev)
{
    ObjId victim = (ObjId)ev->arg[0], source = (ObjId)ev->arg[1];
    if (!DM3_IS_PLAYER(victim))
        return;
    int32_t vp = Player_GetId(victim);
    if (DM3_IS_PLAYER(source)) {
        int32_t kp = Player_GetId(source);
        DM3_AddFrag(kp);
        if ((int32_t)source == gThisPlayer) {
            const char *name = DM3_PlayerName(vp);
            sprintf(st.msg, dm3_text[gLanguage][DM3_T_YOU_KILLED], name);
        } else if ((int32_t)victim == gThisPlayer) {
            const char *name = DM3_PlayerName(kp);
            sprintf(st.msg, dm3_text[gLanguage][DM3_T_KILLED_YOU], name);
        } else {
            int32_t r = DM3_Rand() % 8;
            if (dm3_fragVictimFirst[r]) {
                const char *killer = DM3_PlayerName(kp);
                const char *vname = DM3_PlayerName(vp);
                sprintf(st.msg, dm3_textFrag[gLanguage][r], vname, killer);
            } else {
                const char *vname = DM3_PlayerName(vp);
                const char *killer = DM3_PlayerName(kp);
                sprintf(st.msg, dm3_textFrag[gLanguage][r], killer, vname);
            }
        }
        DM3_Message();
    } else {
        if ((int32_t)victim == gThisPlayer) {
            DM3_SetMsg(dm3_textYouDied[gLanguage][DM3_Rand() % 7]);
        } else {
            const char *name = DM3_PlayerName(vp);
            sprintf(st.msg, dm3_textAccident[gLanguage][DM3_Rand() % 11], name);
        }
        DM3_Message();
        DM3_SubFrag(vp);
    }
    if ((int32_t)victim == gThisPlayer)
        InTime0(1400, DM3_ShowFrags);
}

// 0x10002ee0  EV_TRIGGER_ENTER: door 0x30016 opens when the first object enters 0x40017.
// (While one object is inside 0x40017, any trigger entry replays the opening.)
static void DM3_OnDoorEnter(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == DM3_TRIGGER(0x17))
        st.doorIn17++;
    if ((ObjId)ev->arg[1] == DM3_TRIGGER(0x18))
        st.doorIn18++;
    if (st.doorIn17 == 1) {
        AnimPlay(DM3_ANIM(0x16), 2, 0);
        Sound_PlayOnce(DM3_ANIM(0x16), 0x3cb);
        AnimSetUpdateInterval(DM3_ANIM(0x16), 6);
    }
}

// 0x10002f50  EV_TRIGGER_LEAVE: closes when 0x40017 and 0x40018 are both empty (any leave)
static void DM3_OnDoorLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == DM3_TRIGGER(0x17))
        st.doorIn17--;
    if ((ObjId)ev->arg[1] == DM3_TRIGGER(0x18))
        st.doorIn18--;
    if (st.doorIn17 == 0 && st.doorIn18 == 0) {
        AnimPlay(DM3_ANIM(0x16), 2, 4);
        Sound_PlayOnce(DM3_ANIM(0x16), 0x3cb);
    }
}

// 0x10002fb0  EV_TRIGGER_ENTER/LEAVE 0x40019: gate 0x30038 follows
static void DM3_OnGate(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != DM3_TRIGGER(0x19))
        return;
    AnimPlay(DM3_ANIM(0x38), 3, ev->type == EV_TRIGGER_ENTER ? 0 : 4);
    Sound_PlayOnce(DM3_ANIM(0x38), 0xa9);
}

static void DM3_DoorsABOpen(const LanderEvent *ev);
static void DM3_DoorsCDOpen(const LanderEvent *ev);

// 0x10002ff0  EV_ANIM_FINISHED: an anim 4 finished (any object): doors 0x3000a/b close again
static void DM3_DoorsABClose(const LanderEvent *ev)
{
    if (ev->arg[1] != 4)
        return;
    AnimPlay(DM3_ANIM(0x0a), 4, 4);
    AnimSetUpdateInterval(DM3_ANIM(0x0a), 7);
    Sound_PlayOnce(DM3_ANIM(0x0a), 0x3ca);
    AnimPlay(DM3_ANIM(0x0b), 4, 4);
    AnimSetUpdateInterval(DM3_ANIM(0x0b), 7);
    Sound_PlayOnce(DM3_ANIM(0x0b), 0x3ca);
    EventType_RemoveHandler(EV_ANIM_FINISHED, DM3_DoorsABClose);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_DoorsABOpen);
}

// 0x10003090  EV_TRIGGER_ENTER 0x4000e/0x4000f: doors 0x3000a/b open
static void DM3_DoorsABOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != DM3_TRIGGER(0x0e) && (ObjId)ev->arg[1] != DM3_TRIGGER(0x0f))
        return;
    AnimPlay(DM3_ANIM(0x0a), 4, 0);
    AnimSetUpdateInterval(DM3_ANIM(0x0a), 7);
    Sound_PlayOnce(DM3_ANIM(0x0a), 0x3ca);
    AnimPlay(DM3_ANIM(0x0b), 4, 0);
    AnimSetUpdateInterval(DM3_ANIM(0x0b), 7);
    Sound_PlayOnce(DM3_ANIM(0x0b), 0x3ca);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, DM3_DoorsABOpen);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, DM3_DoorsABClose);
}

// 0x10003140  EV_ANIM_FINISHED: an anim 4 finished: doors 0x3000c/d close again
static void DM3_DoorsCDClose(const LanderEvent *ev)
{
    if (ev->arg[1] != 4)
        return;
    AnimPlay(DM3_ANIM(0x0c), 4, 4);
    AnimSetUpdateInterval(DM3_ANIM(0x0c), 7);
    Sound_PlayOnce(DM3_ANIM(0x0c), 0x3ca);
    AnimPlay(DM3_ANIM(0x0d), 4, 4);
    AnimSetUpdateInterval(DM3_ANIM(0x0d), 7);
    Sound_PlayOnce(DM3_ANIM(0x0d), 0x3ca);
    EventType_RemoveHandler(EV_ANIM_FINISHED, DM3_DoorsCDClose);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_DoorsCDOpen);
}

// 0x100031e0  EV_TRIGGER_ENTER 0x40010/0x40011: doors 0x3000c/d open
static void DM3_DoorsCDOpen(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != DM3_TRIGGER(0x10) && (ObjId)ev->arg[1] != DM3_TRIGGER(0x11))
        return;
    AnimPlay(DM3_ANIM(0x0c), 4, 0);
    AnimSetUpdateInterval(DM3_ANIM(0x0c), 7);
    Sound_PlayOnce(DM3_ANIM(0x0c), 0x3ca);
    AnimPlay(DM3_ANIM(0x0d), 4, 0);
    AnimSetUpdateInterval(DM3_ANIM(0x0d), 7);
    Sound_PlayOnce(DM3_ANIM(0x0d), 0x3ca);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, DM3_DoorsCDOpen);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, DM3_DoorsCDClose);
}

// 0x10003290  EV_POD_HOOKED {pod, lander}: remember which pod the player carries
static void DM3_OnPodHooked(const LanderEvent *ev)
{
    DM3_SetCarried(Lander_GetPlayerId((ObjId)ev->arg[1]), (ObjId)ev->arg[0]);
}

// 0x100032b0  move the lander, the pod it carries and (local player) the camera by to - from
static void DM3_Teleport(ObjId lander, ObjId pod, ObjId camera, const int32_t from[3], const int32_t to[3])
{
    int32_t dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    int32_t p[3];
    const int32_t *q = (const int32_t *)Object_GetProperty(lander, PROP_POSITION);
    p[0] = q[0] + dx; p[1] = q[1] + dy; p[2] = q[2] + dz;
    Object_SetProperty(lander, PROP_POSITION, (intptr_t)p);
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

// 0x100033b0  EV_TRIGGER_ENTER 0x40030..0x40033: teleport
static void DM3_OnTeleportTrigger(const LanderEvent *ev)
{
    int32_t pid = Player_GetId((ObjId)ev->arg[0]);
    ObjId pod = (pid >= 0 && pid < 32) ? st.carried[pid] : OBJ_NONE;
    int32_t t = (int32_t)ev->arg[1];
    if (t < (int32_t)DM3_TRIG_TELE_FIRST || t > (int32_t)DM3_TRIG_TELE_LAST)
        return;
    int32_t k = t - (int32_t)DM3_TRIG_TELE_FIRST;
    int32_t from[4], to[4];
    from[0] = (int32_t)(dm3_teleFrom[k][0] * dm3_teleScale);
    from[1] = (int32_t)(dm3_teleFrom[k][1] * dm3_teleScale);
    from[2] = (int32_t)(dm3_teleFrom[k][2] * dm3_teleScale);
    from[3] = 0;
    to[0] = (int32_t)(dm3_teleTo[k][0] * dm3_teleScale);
    to[1] = (int32_t)(dm3_teleTo[k][1] * dm3_teleScale);
    to[2] = (int32_t)(dm3_teleTo[k][2] * dm3_teleScale);
    to[3] = 0;
    DM3_Teleport((ObjId)ev->arg[0], pod, DM3_CAMERA, from, to);
}

// 0x100034b0  EV_PLAYER_JOINED: the local player gets a head light
static void DM3_OnPlayerJoinedLight(const LanderEvent *ev)
{
    if ((int32_t)ev->arg[0] == gThisPlayer)
        Lander_SetHeadLight((ObjId)gThisPlayer, 0x31c, 0x999);
}

// 0x100034e0  EV_PICKUP {pickup, lander}: pickups come back after 10..40 s
static void DM3_OnPickup(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0], lander = (ObjId)ev->arg[1];
    int32_t hp, n;
    if ((int32_t)id > (int32_t)DM3_PICKUP(0)) {
        switch (id - DM3_PICKUP(1)) {
        case 0:                                        // 0x10003609 +25 % shield
            hp = (int32_t)Object_GetProperty(lander, PROP_HITPOINTS);
            hp = hp < 0xc0 ? hp + 0x40 : 0x100;
            Object_SetProperty(lander, PROP_HITPOINTS, hp);
            Voice_ShieldAcquired(lander, 25);
            InTime1(10000, Object_SetAlive, id);
            return;
        case 1:
        case 2:                                        // 0x100036a2 +2 missiles
            n = Lander_AddMissiles(lander, 2);
            if (n == 0)
                break;
            Voice_MissilesAcquired(lander);
            InTime1(20000, Object_SetAlive, id);
            return;
        case 3:
        case 4:                                        // 0x10003665 +4 missiles
            n = Lander_AddMissiles(lander, 4);
            if (n == 0)
                break;
            Voice_MissilesAcquired(lander);
            InTime1(40000, Object_SetAlive, id);
            return;
        case 5:
        case 6:                                        // 0x100035ab +50 % shield
            hp = (int32_t)Object_GetProperty(lander, PROP_HITPOINTS);
            hp = hp < 0x80 ? hp + 0x80 : 0x100;
            Object_SetProperty(lander, PROP_HITPOINTS, hp);
            Voice_ShieldAcquired(lander, 50);
            InTime1(20000, Object_SetAlive, id);
            return;
        default:
            return;
        }
        Voice_MissilesFull(lander);                    // 0x100036b5 missiles full: stays
        Object_SetAlive(id);
        return;
    }
    if (id == DM3_PICKUP(0)) {                         // 0x10003557 full shield
        Object_SetProperty(lander, PROP_HITPOINTS, 0x100);
        Voice_ShieldAcquired(lander, 100);
        InTime1(40000, Object_SetAlive, id);
        return;
    }
    if (id == OBJ_NONE) {                              // a case -1 (+1 missile): never a pickup id
        if (Lander_AddMissiles(lander, 1) == 0) {
            Voice_MissilesFull(lander);
            Object_SetAlive(id);
            return;
        }
        Voice_MissilesAcquired(lander);
        InTime1(10000, Object_SetAlive, id);
    }
}

// 0x10003720  set-up (host / single player)
static void DM3_Setup(void)
{
    SetGravity(15);
    gAirResistance = 0x17;
    gMainThrottlePower = 0x1e;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_OnDoorEnter);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, DM3_OnDoorLeave);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_OnGate);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, DM3_OnGate);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_DoorsABOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_DoorsCDOpen);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, DM3_OnTeleportTrigger);
    EventType_AddHandler(EV_POD_HOOKED, NULL, DM3_OnPodHooked);
    EventType_AddHandler(EV_PLAYER_JOINED, NULL, DM3_OnPlayerJoinedLight);
    EventType_AddHandler(EV_PICKUP, NULL, DM3_OnPickup);
    EventType_AddHandler(EV_TICK_1S, NULL, DM3_OnTick1sNothing);
    st.hookPoint[2] = 0;
    st.hookPoint[1] = 0;
    st.hookPoint[0] = 0;
    switch (gNetworkGameType) {
    case 0:
        for (int32_t id = (int32_t)DM3_ANIM(0x1e); id - (int32_t)DM3_ANIM(0x1e) < 4; id++) {
            AnimPlay((ObjId)id, 1, 0x81);
            AnimSetUpdateInterval((ObjId)id, 2);
        }
        for (int32_t id = (int32_t)DM3_ANIM(0x1e); id <= (int32_t)DM3_ANIM(0x21); id++)
            Sound_PlayLoop((ObjId)id, 0xb6, -1);
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x12));
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x13));
        DM3_DmSetup();
        break;
    case 1:
        Object_SetDead(OBJ_ID(OT_FORCEBOX, 0xb8));
        Object_SetProperty(OBJ_ID(OT_FORCEBOX, 0x22), PROP_STATE, -25);
        for (int32_t k = 0; k < 4; k++) {
            if (k == 2)
                continue;
            AnimPlay(DM3_ANIM(0x1e + k), 1, 0x81);
            AnimSetUpdateInterval(DM3_ANIM(0x1e + k), 2);
        }
        for (int32_t id = (int32_t)DM3_ANIM(0x1e); id <= (int32_t)DM3_ANIM(0x1f); id++)
            Sound_PlayLoop((ObjId)id, 0xb6, -1);
        Sound_PlayLoop(DM3_ANIM(0x21), 0xb6, -1);
        DM3_CtfSetup();
        break;
    case 2:
        for (int32_t id = (int32_t)DM3_ANIM(0x1e); id - (int32_t)DM3_ANIM(0x1e) < 4; id++) {
            AnimPlay((ObjId)id, 1, 0x81);
            AnimSetUpdateInterval((ObjId)id, 2);
        }
        for (int32_t id = (int32_t)DM3_ANIM(0x1e); id <= (int32_t)DM3_ANIM(0x21); id++)
            Sound_PlayLoop((ObjId)id, 0xb6, -1);
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x12));
        Object_SetDead(OBJ_ID(OT_DESTRUCTIBLE, 0x13));
        DM3_BombSetup();
        break;
    }
    DM3_SpawnSetup();
    for (int i = 0; i < 32; i++)
        st.carried[i] = OBJ_NONE;
    if (gMultiPlayerGame != 0)
        EventType_AddHandler(EV_DESTROYED, NULL, DM3_OnFrag);
}

// 0x100039c0  LevelStartLevel_Lander (export 0x10002700)
static void DM3_StartLander(void)
{
    if (gMultiPlayerGame == 0) {
        DM3_Setup();
        MobObj_Wake(DM3_MOB_LANDER);
        MobObj_Wake(DM3_MOB_CAMERA);
        Object_SetProperty(DM3_CAMERA, PROP_TARGET, DM3_LANDER);
        return;
    }
    if (gJoinedGame == 0)
        DM3_Setup();
}

// 0x10003a10  network snapshot registration of everything
static void DM3_RegisterAll(void)
{
    GameState_RegisterDataBlock(st.carried, 0x80);
    GameState_RegisterDataBlock(&st.doorIn17, 4);
    GameState_RegisterDataBlock(&st.doorIn18, 4);
    GameState_RegisterFunction(DM3_FN(DM3_DoorsABOpen));
    GameState_RegisterFunction(DM3_FN(DM3_DoorsABClose));
    GameState_RegisterFunction(DM3_FN(DM3_DoorsCDOpen));
    GameState_RegisterFunction(DM3_FN(DM3_DoorsCDClose));
    GameState_RegisterFunction(DM3_FN(DM3_OnDoorEnter));
    GameState_RegisterFunction(DM3_FN(DM3_OnDoorLeave));
    GameState_RegisterFunction(DM3_FN(DM3_OnGate));
    GameState_RegisterFunction(DM3_FN(DM3_OnTeleportTrigger));
    GameState_RegisterFunction(DM3_FN(DM3_OnPodHooked));
    GameState_RegisterFunction(DM3_FN(DM3_OnFrag));
    GameState_RegisterFunction(DM3_FN(DM3_OnPlayerJoinedLight));
    GameState_RegisterFunction(DM3_FN(DM3_OnPickup));
    GameState_RegisterFunction(DM3_FN(DM3_OnTick1sNothing));   // 0x10002750
    DM3_RegisterHookPoint();
    DM3_SpawnRegisterState();
    DM3_FragsRegisterState();
    switch (gNetworkGameType) {
    case 0: DM3_DmRegisterState(); break;
    case 1: DM3_CtfRegisterState(); break;
    case 2: DM3_BombRegisterState(); break;
    }
}

// 0x10003b00  LevelStartLevel_Animcam (export 0x10002710): multiplayer, before the players spawn
static void DM3_StartAnimcam(void)
{
    DM3_RegisterAll();
    DM3_FragsSetup();
    if (gNetworkGameType == 2)
        DM3_BombCreateWindow();
}

// 0x10003b30  LevelStartLevel_Flycam (export 0x10002720)
static void DM3_StartFlycam(void)
{
    DM3_Setup();
    MobObj_Wake(DM3_MOB_FLYCAM);
}

// 0x10002740
static uint32_t DM3_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x100026f0  two calls of the empty 0x10002750, return 0. Port: fresh DLL data.
static int32_t DM3_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.randSeed = 1;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText dm3_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x1000e0b8, .count = 3 * 11, .out = dm3_textAccident },
    { .kind = GT_POINTERS, .va = 0x1000e140, .count = 3 * 8, .out = dm3_textFrag },
    { .kind = GT_POINTERS, .va = 0x1000e1c0, .count = 3 * DM3_T_COUNT, .out = dm3_text },
    { .kind = GT_POINTERS, .va = 0x1000e2c8, .count = 3 * 7, .out = dm3_textYouDied },
    { 0 },
};

const mission_script_t mission_deathmat_3 = {
    .name          = "DEATHMAT\\3",
    .get_version   = DM3_GetVersion,
    .load          = DM3_Load,
    .start_lander  = DM3_StartLander,
    .start_flycam  = DM3_StartFlycam,
    .start_animcam = DM3_StartAnimcam,
    .stop          = DM3_Nothing,
    .text          = dm3_gametext,
};
