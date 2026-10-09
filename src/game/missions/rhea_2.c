// MISSIONS\RHEA\2\MISSIONR.DLL - faithful C port.
//
// Mission: inside the Rhea installation, five warheads (pods 0x10c0039..3d) lie on the floor of the
// main shaft. Hook one, carry it to one of the five bridge trigger boxes (0x4002c..0x40030), where it
// is released and armed: a 15-s countdown (shown from 10 s) blows that bridge up (explosion mobs,
// turrets/lights removed). After five bridges, fly back to the entrance trigger 0x40031: success 1
// and +380000 cash, or +400000 if the secondary objective was achieved (destroy the anim-destructible
// 0xf0028: its wreck animation finishes, the anim object 0x30020's anim 6 finishes -> text[5]).
// Par time 180 s, 8 pickups.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x10001000,
// LevelStartLevel_Lander 0x10001010 (-> 0x10002910), LevelStartLevel_Flycam 0x10001020
// (-> 0x10002950), LevelStopLevel 0x10001030 (-> empty 0x10002970), LevelGetVersion 0x10001040
// (no Animcam export). The score library (0x10001050-0x100015ff, Init 0x10001520, End 0x100013a0)
// and the destroyed-sound handler (0x10002340) are the common code in mission_lib.c. 0x10002980
// is the CRT sprintf.
//
// Quirk kept: the objective strings live in one writable .data block and the countdown sprintf()s
// its format (0x1000b494) INTO the string text[4] itself. In English the 10-second line (25 bytes)
// overruns the 22-byte text[4] and zeroes the first byte of the next string, text[3] (the warhead
// armed line), so later arming messages are empty. In French and German a missing comma in the
// original source table concatenated text[2] and text[3], shifting the rest: the countdown
// overwrites the secondary objective's line with the English format, and text[5] (secondary
// objective) is NULL. The port keeps the block as a byte image, read from the DLL.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define RH2_LANDER        MOB_ID(OT_LANDER, 0x3e)          // 0x0101003e
#define RH2_CAMERA        MOB_ID(OT_CAMERA, 1)             // 0x01060001
#define RH2_MOB_CAMERA    1
#define RH2_MOB_FLYCAM    0x38
#define RH2_MOB_LANDER    0x3e
#define RH2_MOB_POD0      0x39                             // pods (warheads) are mobs 0x39..0x3d
#define RH2_POD_FIRST     MOB_ID(OT_POD, 0x39)             // 0x010c0039
#define RH2_POD_LAST      MOB_ID(OT_POD, 0x3d)             // 0x010c003d
#define RH2_ANIMTURRET_FIRST MOB_ID(OT_ANIMTURRET, 0x40)   // 0x010e0040..4b
#define RH2_ANIMTURRET_LAST  MOB_ID(OT_ANIMTURRET, 0x4b)
#define RH2_FUELDUMP_FIRST OBJ_ID(OT_FUELDUMP, 0x37)       // 0x00110037..3b
#define RH2_FUELDUMP_LAST  OBJ_ID(OT_FUELDUMP, 0x3b)
#define RH2_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))       // 0x000300nn
#define RH2_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))       // 0x000400nn
#define RH2_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))     // 0x000800nn
#define RH2_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n))  // 0x000d00nn
#define RH2_EXPLOSION(n)  MOB_ID(OT_EXPLOSION, (n))        // 0x010900nn (mobs)
#define RH2_ANIMDESTR_SECONDARY OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x28)  // 0x000f0028 secondary objective
#define RH2_ANIMDESTR_21  OBJ_ID(OT_ANIMDESTRUCTIBLE, 0x21)        // 0x000f0021 guards trigger 0x4001e
#define RH2_SWITCH_DOOR   OBJ_ID(OT_SWITCH, 0x75)          // 0x000a0075
#define RH2_TRIG_EXIT     RH2_TRIGGER(0x31)                // 0x00040031 entrance shaft (end)
#define RH2_BRIDGE_A      RH2_TRIGGER(0x2c)                // 0x0004002c..30 bridge trigger boxes
#define RH2_BRIDGE_B      RH2_TRIGGER(0x2d)
#define RH2_BRIDGE_C      RH2_TRIGGER(0x2e)
#define RH2_BRIDGE_D      RH2_TRIGGER(0x2f)
#define RH2_BRIDGE_E      RH2_TRIGGER(0x30)
#define RH2_PICKUP_SHIELD MOB_ID(OT_PICKUP, 0x3f)          // 0x0113003f
#define RH2_PICKUP_CASH_FIRST MOB_ID(OT_PICKUP, 0x2e)      // 0x0113002e..32
#define RH2_PICKUP_CASH_LAST  MOB_ID(OT_PICKUP, 0x32)
#define RH2_PICKUP_MISSILES_A MOB_ID(OT_PICKUP, 0x4c)      // 0x0113004c
#define RH2_PICKUP_MISSILES_B MOB_ID(OT_PICKUP, 0x4d)      // 0x0113004d

// ---- static data (.data) --------------------------------------------------------------------------
static const int32_t  rh2_nbWaypoints = 6;            // 0x1000b160
static const Waypoint rh2_waypoints[6] = {            // 0x1000b168
    { -8704, -9344 }, { -8192, -57600 }, { -384, -19072 },
    { -6400, -32000 }, { -6144, -6400 }, { -4480, 6272 },
};
static const TurretType rh2_turretType0 = {           // 0x1000b1a0 -> gTurretTypes[0]
    { 75, 0, 281, 0 }, 0x00, 1, 2, 10, 80, 0x18, 1024, 12000, 0
};
static const TurretType rh2_turretType7 = {           // 0x1000b1c0 -> gTurretTypes[7]
    { -19, 0, 793, 0 }, 0x00, 0, 3, 7, 60, 0x18, 1024, 12000, 0
};
static const int32_t rh2_countdownInit = 15;          // 0x1000b198 initial value

// 0x1000b228-0x1000b4af: the string block (writable in the DLL, see the header). Byte image.
#define RH2_STR_BASE 0x1000b228u
static char rh2_strImage[0x288];

// 0x1000b1e0: [language][6] string pointers (0 = NULL). French/German: 5 strings + NULL.
static const uint32_t rh2_textPtr[3][6] = {
    { 0x1000b46c, 0x1000b458, 0x1000b420, 0x1000b410, 0x1000b3f8, 0x1000b3d8 },
    { 0x1000b398, 0x1000b384, 0x1000b334, 0x1000b318, 0x1000b2fc, 0 },
    { 0x1000b2c8, 0x1000b2b0, 0x1000b264, 0x1000b244, 0x1000b228, 0 },
};
#define RH2_FMT_COUNTDOWN 0x1000b494u   // the countdown's format (English in every language)

// ---- DLL globals (.data / .bss) -------------------------------------------------------------------
static struct {
    char        str[sizeof rh2_strImage];   // 0x1000b228 writable string block
    char       *text[6];      // 0x1000eec8 = &table[gLanguage] (the port resolves the pointers)
    int32_t     countdown;    // 0x1000b198 seconds left on the armed warhead (.data: 15)
    Window     *win;          // 0x1000e924 objective line
    int32_t     bridges;      // 0x1000e928 bridges destroyed
    int32_t     podIndex;     // 0x1000e92c last hooked pod - 0x10c0039
    int32_t     secondary;    // 0x1000e930 secondary objective achieved
    ObjId       armedAt;      // 0x1000eec4 trigger box where the warhead was armed
} st;

static char *RH2_Str(uint32_t va)
{
    return va ? st.str + (va - RH2_STR_BASE) : NULL;
}

// 0x10001600  EV_TRIGGER_ENTER 0x4001c: start the two fans 0x30019/0x3001b (once)
static void RH2_OnTriggerFans(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIGGER(0x1c))
        return;
    AnimSetUpdateInterval(RH2_ANIM(0x19), 100);
    AnimSetUpdateInterval(RH2_ANIM(0x1b), 100);
    AnimPlay(RH2_ANIM(0x19), 1, 0);
    AnimPlay(RH2_ANIM(0x1b), 1, 0);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH2_OnTriggerFans);
}

// 0x10001660  EV_TRIGGER_ENTER 0x40018: door 0x30017 opens
static void RH2_OnTriggerDoor17Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIGGER(0x18))
        return;
    AnimPlay(RH2_ANIM(0x17), 3, 0);
    Sound_PlayOnce(RH2_ANIM(0x17), 0xae);
}

// 0x100016a0  EV_TRIGGER_LEAVE 0x40018: door 0x30017 closes
static void RH2_OnTriggerDoor17Close(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIGGER(0x18))
        return;
    AnimPlay(RH2_ANIM(0x17), 3, 4);
    Sound_PlayOnce(RH2_ANIM(0x17), 0xa8);
}

// 0x100016e0  EV_TRIGGER_ENTER 0x40074: door 0x30032 opens
static void RH2_OnTriggerDoor32Open(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIGGER(0x74))
        return;
    AnimPlay(RH2_ANIM(0x32), 9, 0);
    Sound_PlayOnce(RH2_ANIM(0x32), 0x3f7);
}

// 0x10001720  EV_TRIGGER_LEAVE 0x40074: door 0x30032 closes
static void RH2_OnTriggerDoor32Close(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIGGER(0x74))
        return;
    AnimPlay(RH2_ANIM(0x32), 9, 4);
    Sound_PlayOnce(RH2_ANIM(0x32), 0x3f7);
}

// 0x10001760  EV_TRIGGER_ENTER 0x4006d: door 0x3006b opens once 0x8003d/3e/3f are all destroyed
static void RH2_OnTriggerDoor6B(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIGGER(0x6d))
        return;
    int32_t hp = (int32_t)Object_GetProperty(RH2_DESTR(0x3d), PROP_HITPOINTS);
    hp += (int32_t)Object_GetProperty(RH2_DESTR(0x3f), PROP_HITPOINTS);
    hp += (int32_t)Object_GetProperty(RH2_DESTR(0x3e), PROP_HITPOINTS);
    if (hp != 0)
        return;
    AnimPlay(RH2_ANIM(0x6b), 8, 0);
    Sound_PlayOnce(RH2_ANIM(0x6b), 0x3f7);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH2_OnTriggerDoor6B);
}

// 0x100017e0  EV_TRIGGER_ENTER 0x4001e: door 0x3001f opens once 0xf0021 is destroyed
static void RH2_OnTriggerDoor1F(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIGGER(0x1e))
        return;
    if (Object_GetProperty(RH2_ANIMDESTR_21, PROP_HITPOINTS) != 0)
        return;
    AnimPlay(RH2_ANIM(0x1f), 2, 0);
    Sound_PlayOnce(RH2_ANIM(0x1f), 0x3f7);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH2_OnTriggerDoor1F);
}

// 0x10001840  EV_ANIM_FINISHED (added by RH2_OnDestroyed): any object's anim 4 finished = the
// secondary target's collapse; a chain of explosions, turret 9 removed
static void RH2_OnSecondaryCollapsed(const LanderEvent *ev)
{
    if (ev->arg[1] != 4)
        return;
    Object_SetProperty(RH2_DESTR(0x24), PROP_HITPOINTS, 0);
    Object_SetDead(RH2_ANIMDESTR_SECONDARY);
    MobObj_Wake(0x1b);
    Sound_PlayOnce(RH2_EXPLOSION(0x1b), 0x2f9);
    MobObj_Wake(0x33);
    Sound_PlayOnce(RH2_EXPLOSION(0x33), 0x2f9);
    MobObj_Wake(0x34);
    Sound_PlayOnce(RH2_EXPLOSION(0x34), 0x2f9);
    MobObj_Wake(0x35);
    Sound_PlayOnce(RH2_EXPLOSION(0x35), 0x2f9);
    MobObj_Wake(0x36);
    Sound_PlayOnce(RH2_EXPLOSION(0x36), 0x2f9);
    MobObj_Wake(0x37);
    Sound_PlayOnce(RH2_EXPLOSION(0x37), 0x2f9);
    Object_SetDead(OBJ_ID(OT_TURRET, 9));
    EventType_RemoveHandler(EV_ANIM_FINISHED, RH2_OnSecondaryCollapsed);
}

// 0x10001920  EV_DESTROYED: 0xf0028 starts collapsing; 0xf0021 shows its wreck animation
static void RH2_OnDestroyed(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == RH2_ANIMDESTR_SECONDARY) {
        AnimPlay(RH2_ANIMDESTR_SECONDARY, 4, 0);
        EventType_AddHandler(EV_ANIM_FINISHED, NULL, RH2_OnSecondaryCollapsed);
        EventType_RemoveHandler(EV_DESTROYED, RH2_OnDestroyed);
    }
    if ((ObjId)ev->arg[0] == RH2_ANIMDESTR_21) {
        AnimSetUpdateInterval(RH2_ANIMDESTR_21, 0x14);
        AnimPlay(RH2_ANIMDESTR_21, 0xc, 0);
        MobObj_Wake(2);
        Object_SetDead(OBJ_ID(OT_LIGHTOBJECT, 0x22));
    }
}

// 0x100019b0  EV_TRIGGER_ENTER 0x40031 (added after the fifth bridge): back at the entrance
static void RH2_OnTriggerExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != RH2_TRIG_EXIT)
        return;
    int32_t cash;
    if (st.secondary == 0)
        cash = Lander_GetCash(RH2_LANDER) + 380000;
    else
        cash = Lander_GetCash(RH2_LANDER) + 400000;
    Lander_SetCash(RH2_LANDER, cash);
    MissionScore_End(1);                                          // 0x100013a0
}

// 0x10001a10  EV_ANIM_FINISHED (added by bridge D): anim 6 of 0x30020 done -> its pieces fall
static void RH2_OnBridgeDFallen(const LanderEvent *ev)
{
    if (ev->arg[1] != 6)
        return;
    Object_SetDead(RH2_ANIM(0x20));
    MobObj_Wake(0x1d);
    MobObj_Wake(0x20);
    MobObj_Wake(0x21);
    st.secondary = 1;
    WindowSetText(st.win, st.text[5]);    // the secondary objective line (NULL in French/German)
    InTime1(500, MobObj_Wake, 0x1f);
    InTime1(200, MobObj_Wake, 0x1e);
    EventType_RemoveHandler(EV_ANIM_FINISHED, RH2_OnBridgeDFallen);
}

// 0x10001ab0  bridge A (trigger 0x4002c) blows up
static void RH2_BlowBridgeA(void)
{
    MobObj_Wake(3);
    InTime1(100, MobObj_Wake, 5);
    InTime1(200, MobObj_Wake, 8);
    InTime1(300, MobObj_Wake, 0xb);
    InTime1(400, MobObj_Wake, 0xd);
    InTime1(500, MobObj_Wake, 0xf);
    InTime1(600, MobObj_Wake, 0x11);
    InTime1(700, MobObj_Wake, 0x14);
    InTime1(800, MobObj_Wake, 0x16);
    Object_SetProperty(RH2_DESTR(0x29), PROP_HITPOINTS, 0);
    Object_SetDead(OBJ_ID(OT_TURRET, 1));
    Object_SetDead(OBJ_ID(OT_TURRET, 0xc));
    Object_SetDead(OBJ_ID(OT_TURRET, 0x12));
    Object_SetDead(RH2_BRIDGE_A);
    Object_SetDead(MOB_ID(OT_ANIMTURRET, 0x44));
    Sound_PlayOnce(RH2_DESTR(0x29), 0x2f8);
    Sound_PlayOnce(RH2_EXPLOSION(3), 0x2fa);
    InTime2(500, Sound_PlayOnce, RH2_EXPLOSION(0xf), 0x2fa);
}

// 0x10001bc0  bridge B (trigger 0x4002d)
static void RH2_BlowBridgeB(void)
{
    MobObj_Wake(0x17);
    Object_SetProperty(RH2_DESTR(0x3e), PROP_HITPOINTS, 0);
    Object_SetProperty(RH2_DESTR(0x3d), PROP_HITPOINTS, 0);
    Object_SetProperty(RH2_DESTR(0x3f), PROP_HITPOINTS, 0);
    for (int32_t n = 0x6e; n <= 0x73; n++)
        Object_SetAlive(RH2_EMITTER(n));
    Object_SetDead(RH2_BRIDGE_B);
    Sound_PlayOnce(RH2_EXPLOSION(0x17), 0x2fa);
}

// 0x10001c70  bridge C (trigger 0x4002e)
static void RH2_BlowBridgeC(void)
{
    MobObj_Wake(0x18);
    InTime1(200, MobObj_Wake, 0x19);
    InTime1(400, MobObj_Wake, 0x1a);
    Object_SetProperty(RH2_DESTR(0x59), PROP_HITPOINTS, 0);
    Object_SetProperty(RH2_DESTR(0x2b), PROP_HITPOINTS, 0);
    Object_SetProperty(RH2_DESTR(0x2a), PROP_HITPOINTS, 0);
    Object_SetDead(OBJ_ID(OT_TURRET, 5));
    Object_SetDead(OBJ_ID(OT_TURRET, 6));
    Object_SetDead(OBJ_ID(OT_TURRET, 7));
    Object_SetDead(OBJ_ID(OT_TURRET, 8));
    Object_SetDead(RH2_BRIDGE_C);
    MobObj_Sleep(0x46);
    MobObj_Sleep(0x47);
    MobObj_Sleep(0x48);
    Sound_PlayOnce(RH2_DESTR(0x2b), 0x2fa);
}

// 0x10001d30  bridge D (trigger 0x4002f): anim 0x30020 collapses (-> RH2_OnBridgeDFallen)
static void RH2_BlowBridgeD(void)
{
    MobObj_Wake(0x1c);
    InTime1(500, MobObj_Wake, 0x24);
    InTime1(250, MobObj_Wake, 0x23);
    InTime1(350, MobObj_Wake, 0x25);
    InTime1(450, MobObj_Wake, 0x26);
    InTime1(550, MobObj_Wake, 0x27);
    InTime1(650, MobObj_Wake, 0x28);
    InTime1(2000, MobObj_Wake, 0x22);
    AnimSetUpdateInterval(RH2_ANIM(0x20), 7);
    AnimPlay(RH2_ANIM(0x20), 6, 0);
    Object_SetProperty(RH2_DESTR(0x25), PROP_HITPOINTS, 0);
    Object_SetDead(OBJ_ID(OT_TURRET, 0xb));
    Object_SetDead(OBJ_ID(OT_TURRET, 0xd));
    Object_SetDead(OBJ_ID(OT_TURRET, 0xe));
    Object_SetDead(OBJ_ID(OT_TURRET, 0xf));
    Object_SetDead(RH2_BRIDGE_D);
    Sound_PlayOnce(RH2_ANIM(0x20), 0x2f8);
    InTime2(500, Sound_PlayOnce, RH2_EXPLOSION(0x24), 0x2f8);
    InTime2(250, Sound_PlayOnce, RH2_EXPLOSION(0x23), 0x2f8);
    InTime2(350, Sound_PlayOnce, RH2_EXPLOSION(0x25), 0x2f8);
    InTime2(450, Sound_PlayOnce, RH2_EXPLOSION(0x26), 0x2f8);
    InTime2(550, Sound_PlayOnce, RH2_EXPLOSION(0x27), 0x2f8);
    InTime2(650, Sound_PlayOnce, RH2_EXPLOSION(0x28), 0x2f8);
    InTime2(2000, Sound_PlayOnce, RH2_EXPLOSION(0x22), 0x2f8);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, RH2_OnBridgeDFallen);
}

// 0x10001ee0  bridge E (trigger 0x40030)
static void RH2_BlowBridgeE(void)
{
    MobObj_Wake(0x29);
    InTime1(200, MobObj_Wake, 0x2a);
    InTime1(400, MobObj_Wake, 0x2b);
    InTime1(500, MobObj_Wake, 0x2c);
    InTime1(600, MobObj_Wake, 0x2d);
    Object_SetProperty(RH2_DESTR(0x23), PROP_HITPOINTS, 0);
    Object_SetProperty(RH2_DESTR(0x26), PROP_HITPOINTS, 0);
    Object_SetDead(OBJ_ID(OT_TURRET, 2));
    Object_SetDead(OBJ_ID(OT_TURRET, 0x13));
    Object_SetDead(OBJ_ID(OT_TURRET, 4));
    Object_SetDead(OBJ_ID(OT_TURRET, 3));
    Object_SetDead(RH2_BRIDGE_E);
    MobObj_Sleep(0x4a);
    MobObj_Sleep(0x49);
    Sound_PlayOnce(RH2_DESTR(0x23), 0x2fa);
    Sound_PlayOnce(RH2_EXPLOSION(0x29), 0x2f8);
    InTime2(400, Sound_PlayOnce, RH2_EXPLOSION(0x2b), 0x2f8);
}

// 0x10001fe0  EV_TICK_1S while a warhead is armed: countdown, then the bridge goes
static void RH2_CountdownTick(const LanderEvent *ev)
{
    (void)ev;
    if (st.countdown <= 10) {
        // sprintf into the string block itself (see the header)
        char *dst = st.text[4];
        char tmp[32];
        int n = snprintf(tmp, sizeof tmp, RH2_Str(RH2_FMT_COUNTDOWN), st.countdown);
        if (dst && n >= 0 && dst + n + 1 <= st.str + sizeof st.str)
            memcpy(dst, tmp, (size_t)n + 1);
        WindowSetText(st.win, st.text[4]);
    }
    if (st.countdown == 0) {
        MobObj_Sleep((st.podIndex + RH2_MOB_POD0) & 0xffff);
        EventType_RemoveHandler(EV_TICK_1S, RH2_CountdownTick);
        WindowSetText(st.win, st.text[0]);    // the warheads objective
        if (st.armedAt == RH2_BRIDGE_A) {
            RH2_BlowBridgeA();
            st.bridges++;
        } else if (st.armedAt == RH2_BRIDGE_B) {
            RH2_BlowBridgeB();
            st.bridges++;
        } else if (st.armedAt == RH2_BRIDGE_C) {
            RH2_BlowBridgeC();
            st.bridges++;
        } else if (st.armedAt == RH2_BRIDGE_D) {
            RH2_BlowBridgeD();
            st.bridges++;
        } else if (st.armedAt == RH2_BRIDGE_E) {
            RH2_BlowBridgeE();
            st.bridges++;
        }
    }
    if (st.bridges == 5) {
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH2_OnTriggerExit);
        WindowSetText(st.win, st.text[2]);    // the return objective
    }
    st.countdown--;
}

// 0x10002100  EV_TRIGGER_ENTER (added per hook): a warhead entered a (bridge) trigger box:
// release it and arm it
static void RH2_OnWarheadAtBridge(const LanderEvent *ev)
{
    int32_t pod = (int32_t)ev->arg[0];
    if (pod < (int32_t)RH2_POD_FIRST || pod > (int32_t)RH2_POD_LAST)
        return;
    st.armedAt = (ObjId)ev->arg[1];
    Object_SetProperty((ObjId)ev->arg[0], PROP_TARGET, -1);
    WindowSetText(st.win, st.text[3]);        // "WARHEAD ARMED"
    EventType_RemoveHandler(EV_TRIGGER_ENTER, RH2_OnWarheadAtBridge);
    st.countdown = 15;
    EventType_AddHandler(EV_TICK_1S, NULL, RH2_CountdownTick);
}

// 0x10002180  EV_POD_HOOKED: a warhead is hooked; the five bridge boxes now watch it
static void RH2_OnPodHooked(const LanderEvent *ev)
{
    int32_t pod = (int32_t)ev->arg[0];
    if (pod < (int32_t)RH2_POD_FIRST || pod > (int32_t)RH2_POD_LAST)
        return;
    Pod_Script_SetupMass((ObjId)pod, 0x5dc);
    WindowSetText(st.win, st.text[1]);        // the bridge objective
    Object_SetProperty(RH2_BRIDGE_A, PROP_TARGET, ev->arg[0]);
    Object_SetProperty(RH2_BRIDGE_B, PROP_TARGET, ev->arg[0]);
    Object_SetProperty(RH2_BRIDGE_C, PROP_TARGET, ev->arg[0]);
    Object_SetProperty(RH2_BRIDGE_D, PROP_TARGET, ev->arg[0]);
    Object_SetProperty(RH2_BRIDGE_E, PROP_TARGET, ev->arg[0]);
    st.podIndex = (int32_t)ev->arg[0] - (int32_t)RH2_POD_FIRST;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH2_OnWarheadAtBridge);
}

// 0x10002250  EV_PICKUP: shield (sets 50 %: the clamp is to 0x80, not 0x100), cash, missiles
static void RH2_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == RH2_PICKUP_SHIELD) {
        int32_t shield = (int32_t)Object_GetProperty(RH2_LANDER, PROP_HITPOINTS) + 0x80;
        Voice_ShieldAcquired(RH2_LANDER, 50);
        Object_SetProperty(RH2_LANDER, PROP_HITPOINTS, shield > 0x80 ? 0x80 : shield);
    }
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)RH2_PICKUP_CASH_FIRST && id <= (int32_t)RH2_PICKUP_CASH_LAST) {
        Lander_SetCash(RH2_LANDER, Lander_GetCash(RH2_LANDER) + gMediumCash);
        Voice_CashAcquired(RH2_LANDER);
    }
    if ((ObjId)ev->arg[0] == RH2_PICKUP_MISSILES_A || (ObjId)ev->arg[0] == RH2_PICKUP_MISSILES_B) {
        if (Lander_AddMissiles(RH2_LANDER, 2) == 0) {
            Voice_MissilesFull(RH2_LANDER);
            Object_SetAlive((ObjId)ev->arg[0]);   // full: the pickup stays
            return;
        }
        Voice_MissilesAcquired(RH2_LANDER);
    }
}

// 0x100023d0  EV_SWITCH 0xa0075: door 0x3006c follows the switch
static void RH2_OnSwitchDoor(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != RH2_SWITCH_DOOR)
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(RH2_ANIM(0x6c), 7, 0);
        Sound_PlayOnce(RH2_ANIM(0x6c), 0x3f7);
    }
    if (ev->arg[1] == 0) {
        AnimPlay(RH2_ANIM(0x6c), 7, 4);
        Sound_PlayOnce(RH2_ANIM(0x6c), 0x3f7);
    }
}

// 0x10002440  common set-up
static void RH2_Setup(void)
{
    for (int i = 0; i < 6; i++)
        st.text[i] = RH2_Str(rh2_textPtr[gLanguage][i]);
    for (int32_t id = (int32_t)RH2_ANIMTURRET_FIRST; id <= (int32_t)RH2_ANIMTURRET_LAST; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x32);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = 0; id <= 0x13; id++)                    // turrets 0..19
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x1e);
    for (int32_t id = (int32_t)RH2_FUELDUMP_FIRST; id <= (int32_t)RH2_FUELDUMP_LAST; id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 10);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x50);
    }
    Object_SetProperty(RH2_DESTR(0x14), PROP_ARMOUR, 0x3c);
    Object_SetProperty(RH2_DESTR(0x14), PROP_EXPLOSION, 0x78);
    Object_SetProperty(RH2_ANIMDESTR_SECONDARY, PROP_ARMOUR, 0x3c);
    Object_SetProperty(RH2_ANIMDESTR_SECONDARY, PROP_EXPLOSION, 0x78);
    Object_SetProperty(RH2_DESTR(0x26), PROP_ARMOUR, 0xff);
    Object_SetProperty(RH2_DESTR(0x27), PROP_ARMOUR, 0xff);
    Object_SetProperty(RH2_DESTR(0x76), PROP_ARMOUR, 10);
    SetGravity(10);
    gAirResistance = 10;
    gMainThrottlePower = 0x17;
    Environment_OverrideExteriorFog(0, 0, 0);
    Environment_OverrideInteriorFog(0, 0, 0);
    gInOutOverride = 1;
    gTurretTypes[0] = &rh2_turretType0;
    gTurretTypes[7] = &rh2_turretType7;
    Lander_SetHeadLight(RH2_LANDER, 0x271, 0x1000);
    gNbWaypoints = rh2_nbWaypoints;
    gWaypoints = rh2_waypoints;
    MissionScore_Init(RH2_LANDER, 180000, 8);                  // 0x10001520
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);    // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);        // the warheads objective
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10002340
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH2_OnTriggerFans);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH2_OnTriggerDoor17Open);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH2_OnTriggerDoor32Open);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH2_OnTriggerDoor17Close);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, RH2_OnTriggerDoor32Close);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH2_OnTriggerDoor6B);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, RH2_OnTriggerDoor1F);
    EventType_AddHandler(EV_DESTROYED, NULL, RH2_OnDestroyed);
    EventType_AddHandler(EV_POD_HOOKED, NULL, RH2_OnPodHooked);
    EventType_AddHandler(EV_PICKUP, NULL, RH2_OnPickup);
    EventType_AddHandler(EV_SWITCH, NULL, RH2_OnSwitchDoor);
    AnimPlay(RH2_ANIM(0x40), 5, 2);
    AnimPlay(RH2_ANIM(0x69), 0xa, 0x81);
    AnimPlay(RH2_ANIM(0x6a), 0xa, 0x81);
    AnimPlay(RH2_ANIM(0x33), 0xb, 0x81);
    AnimPlay(RH2_ANIM(0x34), 0xb, 0x81);
    AnimPlay(RH2_ANIM(0x35), 0xb, 0x81);
    AnimPlay(RH2_ANIM(0x36), 0xb, 0x81);
    AnimSetUpdateInterval(RH2_ANIM(0x69), 0x32);
    AnimSetUpdateInterval(RH2_ANIM(0x6a), 0x32);
    AnimSetUpdateInterval(RH2_ANIM(0x33), 0x32);
    AnimSetUpdateInterval(RH2_ANIM(0x34), 0x32);
    AnimSetUpdateInterval(RH2_ANIM(0x35), 0x32);
    AnimSetUpdateInterval(RH2_ANIM(0x36), 0x32);
    for (int32_t id = (int32_t)RH2_ANIM(0x33); id <= (int32_t)RH2_ANIM(0x36); id++)
        Sound_PlayLoop((ObjId)id, 0xd3, -1);
    Sound_PlayLoop(RH2_ANIM(0x6a), 0xd3, -1);
    Sound_PlayLoop(RH2_ANIM(0x40), 0xcc, -1);
    MobObj_Wake(0x40);                         // anim turrets
    MobObj_Wake(0x44);
    MobObj_Wake(0x45);
    MobObj_Wake(0x46);
    MobObj_Wake(0x47);
    MobObj_Wake(0x48);
    MobObj_Wake(0x49);
    MobObj_Wake(0x4a);
    MobObj_Wake(0x4b);
    MobObj_Wake(0x41);
    MobObj_Wake(0x42);
    MobObj_Wake(0x43);
    for (int32_t n = 0x6e; n <= 0x73; n++)      // bridge B's smoke, shown when it blows
        Object_SetDead(RH2_EMITTER(n));
    for (int32_t id = (int32_t)RH2_ANIMTURRET_FIRST; id <= (int32_t)RH2_ANIMTURRET_LAST; id++)
        Sound_PlayLoop((ObjId)id, 0x8d, -1);
    MobObj_Wake(0x39);                         // the five warheads
    MobObj_Wake(0x3a);
    MobObj_Wake(0x3b);
    MobObj_Wake(0x3c);
    MobObj_Wake(0x3d);
    Media_AudioPlay(0);
    FuelDump_SetFuel(OBJ_ID(OT_FUELDUMP, 0x38), 1000000);
    FuelDump_SetFuel(OBJ_ID(OT_FUELDUMP, 0x39), 1000000);
    FuelDump_SetFuel(OBJ_ID(OT_FUELDUMP, 0x3a), 1000000);
    FuelDump_SetFuel(OBJ_ID(OT_FUELDUMP, 0x3b), 1000000);
    FuelDump_SetFuel(OBJ_ID(OT_FUELDUMP, 0x37), 1000000);
    TexAnimPlay(0x21, 1, 100);
}

// 0x10002910  LevelStartLevel_Lander (export 0x10001010)
static void RH2_StartLander(void)
{
    MobObj_Wake(RH2_MOB_LANDER);
    MobObj_Wake(RH2_MOB_CAMERA);
    Object_SetProperty(RH2_CAMERA, PROP_TARGET, RH2_LANDER);
    RH2_Setup();
}

// 0x10002950  LevelStartLevel_Flycam (export 0x10001020)
static void RH2_StartFlycam(void)
{
    RH2_Setup();
    MobObj_Wake(RH2_MOB_FLYCAM);
}

// 0x10002970  LevelStopLevel (export 0x10001030): empty
static void RH2_Stop(void)
{
}

// 0x10001040
static uint32_t RH2_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x10002970, return 0. Port: fresh DLL data.
static int32_t RH2_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    memcpy(st.str, rh2_strImage, sizeof st.str);
    st.countdown = rh2_countdownInit;
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText rh2_gametext[] = {
    { .kind = GT_BYTES, .va = RH2_STR_BASE, .count = sizeof rh2_strImage, .out = rh2_strImage },
    { 0 },
};

const mission_script_t mission_rhea_2 = {
    .name          = "RHEA\\2",
    .get_version   = RH2_GetVersion,
    .load          = RH2_Load,
    .start_lander  = RH2_StartLander,
    .start_flycam  = RH2_StartFlycam,
    .start_animcam = NULL,
    .stop          = RH2_Stop,
    .text          = rh2_gametext,
};
