// Shared mission-DLL code, see mission_lib.h. Addresses: mercury_1 MISSIONR.DLL for the score
// library (mars_1a has the same code 0x20 bytes lower), training_2 MISSIONR.DLL for AnimSeq.
#include <string.h>
#include "mission_lib.h"

// ================================================================================================
// Score library
// ================================================================================================

// (mercury 0x10007030: 20 x char[15] rating words, part of the library's data; no code uses them,
// so the port leaves them out)

// Library globals (mercury addresses). Ratings are 4.12 (0x1000 = 100 %), -1 = not applicable.
static struct {
    int32_t rAccuracy;      // 0x10009d38 hits / shots fired
    int32_t rPickups;       // 0x10009d3c pickups / pickupsTotal
    int32_t rTime;          // 0x10009d40 1.0 if under par, else par / elapsed
    int32_t rShield;        // 0x10009d44 1 - 16 * shield lost
    int32_t rCollisions;    // 0x10009d48 1 - 16 * collisions
    int32_t rMissiles;      // 0x10009d4c 1 - 16 * missiles fired
    int32_t rOverall;       // 0x10009d50
    int32_t rMix;           // 0x10009d54
    int32_t rTimeBonus;     // 0x10009d58 (par - elapsed) / par, -1 when over par
    int32_t divMix;         // 0x1000a1a4 = 6
    int32_t divOverall;     // 0x1000a1a8 = 10
    int32_t startTime;      // 0x1000a1c0
    int32_t endTime;        // 0x1000a1c4
    int32_t elapsed;        // 0x1000a1c8 (compared unsigned)
    int32_t parTime;        // 0x1000a1cc
    int32_t shieldLost;     // 0x1000a1d0
    int32_t shieldLast;     // 0x1000a1d4
    int32_t collisions;     // 0x1000a1ec
    int32_t pickups;        // 0x1000a1f0 (never reset by Init: relies on the DLL being reloaded)
    int32_t pickupsTotal;   // 0x1000a1f4
    ObjId   lander;         // 0x1000a204
    int32_t result;         // 0x1000a208
} sc;

void MissionScore_Reset(void)
{
    memset(&sc, 0, sizeof sc);
}

// 0x10001070  EV_DESTROYED: count every destroyed object
static void Score_OnDestroyed(const LanderEvent *ev)
{
    (void)ev;
    gGame_ScoreData.kills++;
}

// 0x10001080  EV_PROJECTILE_HIT {target, damage, shooter, kind}
static void Score_OnHit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == sc.lander) {
        int32_t armour = (int32_t)Object_GetProperty(sc.lander, PROP_ARMOUR);
        int32_t lost = (int32_t)((uint32_t)ev->arg[1] << 7) / armour;   // shl 7; idiv
        sc.shieldLost += lost;
        sc.shieldLast -= lost;
        gGame_ScoreData.hitsTaken++;
    } else if ((ObjId)ev->arg[2] == sc.lander) {
        gGame_ScoreData.hits++;
    }
}

// 0x100010e0  EV_COLLISION: counts every body's impact (not only the player's) and folds any
// shield change since the last sample into the damage total
static void Score_OnCollision(const LanderEvent *ev)
{
    (void)ev;
    sc.collisions++;
    int32_t shield = (int32_t)Object_GetProperty(sc.lander, PROP_HITPOINTS);
    sc.shieldLost += sc.shieldLast - shield;
    sc.shieldLast = shield;
}

// 0x10001120  EV_LANDER_FIRED {lander, weaponKind, projectile}
static void Score_OnFired(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == sc.lander) {
        if (ev->arg[1] == 2)
            gGame_ScoreData.missilesFired++;
        else
            gGame_ScoreData.shotsFired++;
    }
}

// 0x10001150  EV_PICKUP: resync the shield sample (pickups may add shield) and count the pickup
static void Score_OnPickup(const LanderEvent *ev)
{
    (void)ev;
    sc.shieldLast = (int32_t)Object_GetProperty(sc.lander, PROP_HITPOINTS);
    sc.pickups++;
}

// 0x10001180
static void Score_RateAccuracy(void)
{
    int32_t shots = gGame_ScoreData.shotsFired;
    if (shots == 0 && gGame_ScoreData.missilesFired == 0) {
        sc.rAccuracy = -1;
        return;
    }
    sc.rAccuracy = (int32_t)((uint32_t)gGame_ScoreData.hits << 12) / (gGame_ScoreData.missilesFired + shots);
}

// 0x100011c0
static void Score_RatePickups(void)
{
    if (sc.pickupsTotal != 0) {
        sc.rPickups = (int32_t)((uint32_t)sc.pickups << 12) / sc.pickupsTotal;
        return;
    }
    sc.rPickups = -1;
}

// 0x100011f0  unsigned compare and unsigned divisions
static void Score_RateTime(void)
{
    uint32_t el = (uint32_t)sc.elapsed, par = (uint32_t)sc.parTime;
    if (el < par) {
        sc.rTime = 0x1000;
        // compiled as ((el * 0xfffff + par) << 12) / par, which is (par - el) << 12 modulo 2^32
        sc.rTimeBonus = (int32_t)(((par - el) << 12) / par);
        return;
    }
    sc.rTimeBonus = -1;
    sc.rTime = (int32_t)((par << 12) / el);
}

static int32_t Score_Penalty16(int32_t count)   // the body of 0x10001240 / 0x10001270 / 0x100012a0
{
    int32_t v = (int32_t)((uint32_t)count << 4);
    if (v > 0x1000)
        v = 0x1000;
    return 0x1000 - v;
}

// 0x100012d0
static void Score_Compute(void)
{
    Score_RateAccuracy();                                              // 0x10001180
    Score_RatePickups();                                               // 0x100011c0
    Score_RateTime();                                                  // 0x100011f0
    sc.rShield = Score_Penalty16(sc.shieldLost);                       // 0x10001240
    sc.rCollisions = Score_Penalty16(sc.collisions);                   // 0x10001270
    sc.rMissiles = Score_Penalty16(gGame_ScoreData.missilesFired);     // 0x100012a0

    int32_t base = 5 * sc.rTime + sc.rShield + sc.rCollisions + sc.rMissiles;
    if (sc.rAccuracy == -1 && sc.rPickups == -1)
        sc.rOverall = base / (sc.divOverall - 2);
    else if (sc.rAccuracy == -1)
        sc.rOverall = (sc.rPickups + base) / (sc.divOverall - 1);
    else if (sc.rPickups == -1)
        sc.rOverall = (sc.rAccuracy + base) / (sc.divOverall - 1);
    else
        sc.rOverall = (sc.rAccuracy + sc.rPickups + base) / sc.divOverall;
    sc.rMix = (3 * sc.rTime + sc.rCollisions + 2 * sc.rShield) / sc.divMix;
}

// rating (4.12) -> percent byte: (x * 100) / 4096 truncated toward zero (lea/shl, cdq/and/sar)
static uint8_t Score_Percent(int32_t x)
{
    int32_t v = (int32_t)((uint32_t)x * 100u);
    return (uint8_t)(v / 4096);
}

// 0x100013c0  compute the ratings, fill gGame_ScoreData, end the level
void MissionScore_End(int32_t result)
{
    sc.result = result;
    sc.endTime = gGameUpdateTime;
    sc.elapsed = sc.endTime - sc.startTime;
    Score_Compute();
    gGame_ScoreData.rateAccuracy = (sc.rAccuracy == -1) ? 0xff : Score_Percent(sc.rAccuracy);
    gGame_ScoreData.pickups = (int8_t)sc.pickups;
    gGame_ScoreData.pickupsTotal = (int8_t)sc.pickupsTotal;
    gGame_ScoreData.collisions = sc.collisions;
    gGame_ScoreData.timeMs = sc.elapsed;
    gGame_ScoreData.rateShield = Score_Percent(sc.rShield);
    gGame_ScoreData.rateCollisions = Score_Percent(sc.rCollisions);
    gGame_ScoreData.rateTime = Score_Percent(sc.rTime);
    gGame_ScoreData.rateMissiles = Score_Percent(sc.rMissiles);
    gGame_ScoreData.rateOverall = Score_Percent(sc.rOverall);
    gGame_ScoreData.rateTimeBonus = Score_Percent(sc.rTimeBonus);
    gGame_ScoreData.rateMix = (uint8_t)sc.rMix;          // raw low byte, not scaled
    LevelEnd(result);
}

// 0x10001540  called last in the mission set-up, so its handlers run before the mission's own
// (handlers are called newest first)
void MissionScore_Init(ObjId lander, int32_t parTimeMs, int32_t pickupsTotal)
{
    sc.lander = lander;
    sc.divOverall = 10;
    sc.divMix = 6;
    sc.startTime = gGameUpdateTime;
    sc.parTime = parTimeMs;
    sc.pickupsTotal = pickupsTotal;
    sc.shieldLast = (int32_t)Object_GetProperty(lander, PROP_HITPOINTS);
    sc.shieldLost = 0;
    gGame_ScoreData.kills = 0;
    gGame_ScoreData.shotsFired = 0;
    gGame_ScoreData.missilesFired = 0;
    gGame_ScoreData.hits = 0;
    gGame_ScoreData.hitsTaken = 0;
    sc.collisions = 0;
    EventType_AddHandler(EV_DESTROYED, NULL, Score_OnDestroyed);
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, Score_OnHit);
    EventType_AddHandler(EV_COLLISION, NULL, Score_OnCollision);
    EventType_AddHandler(EV_PICKUP, NULL, Score_OnPickup);
    EventType_AddHandler(EV_LANDER_FIRED, NULL, Score_OnFired);
}

// ================================================================================================
// AnimSeq (training_2 addresses)
// ================================================================================================

typedef struct AnimSeqSlot {
    uint8_t  count;      // +0  repetitions done of the current step (1-based)
    uint8_t  active;     // +1
    uint8_t  loop;       // +2  1 = restart at the end (mobs are slept and re-woken first)
    uint8_t  pad;
    ObjId    obj;        // +4
    uint8_t *start;      // +8
    uint8_t *cur;        // +c
} AnimSeqSlot;

static AnimSeqSlot g_animseq[ANIMSEQ_SLOTS];   // 0x1000dbe0

void AnimSeq_Reset(void)
{
    memset(g_animseq, 0, sizeof g_animseq);
}

// 0x10001000  EV_ANIM_FINISHED {object, anim}
void AnimSeq_OnAnimFinished(const LanderEvent *ev)
{
    for (int i = 0; i < ANIMSEQ_SLOTS; i++) {
        AnimSeqSlot *s = &g_animseq[i];
        if (s->active != 1 || (ObjId)ev->arg[0] != s->obj || ev->arg[1] != (int8_t)s->cur[0])
            continue;
        AnimStop(s->obj, 0);
        if (s->count == s->cur[1]) {
            s->count = 1;
            s->cur += 3;
        } else {
            s->count++;
        }
        if (s->cur[0] != 0xff) {
            AnimPlay(s->obj, (int8_t)s->cur[0], s->cur[2]);
        } else if (s->loop == 1) {
            if ((s->obj & 0x3000000u) == 0x1000000u) {
                MobObj_Sleep((int32_t)(s->obj & 0xffffu));
                MobObj_Wake((int32_t)(s->obj & 0xffffu));
            }
            s->count = 1;
            s->cur = s->start;
            AnimPlay(s->obj, (int8_t)s->cur[0], s->cur[2]);
        } else {
            LanderEvent done = { EV_ANIMSEQ_DONE, { 0 } };
            s->active = 0;
            done.arg[0] = (intptr_t)s->obj;
            done.arg[1] = (intptr_t)s->start;
            Event_Raise(&done);
        }
    }
}

// 0x10001120  first free slot; silently ignored when all 16 are busy
void AnimSeq_Start(ObjId obj, uint8_t *seq, int32_t loop)
{
    int i;
    for (i = 0; i < ANIMSEQ_SLOTS; i++)
        if (g_animseq[i].active == 0)
            break;
    if (i == ANIMSEQ_SLOTS)
        return;
    AnimSeqSlot *s = &g_animseq[i];
    s->loop = (uint8_t)loop;
    s->count = 1;
    s->active = 1;
    s->obj = obj;
    s->start = seq;
    s->cur = seq;
    AnimPlay(obj, (int8_t)seq[0], seq[2]);
}

// titan_3 / mimas_1 / earth_4 0x10001190 (their AnimSeq copy has it between Start and Init; the
// code before and after is training_2's byte for byte): every active sequence of obj stops, without
// an animation call (the current animation plays out, its end event no longer advances anything)
void AnimSeq_Stop(ObjId obj)
{
    for (int i = 0; i < ANIMSEQ_SLOTS; i++)
        if (g_animseq[i].active == 1 && g_animseq[i].obj == obj)
            g_animseq[i].active = 0;
}

// 0x10001190 (titan_3 / mimas_1 / earth_4: 0x100011c0)
void AnimSeq_Init(void)
{
    for (int i = 0; i < ANIMSEQ_SLOTS; i++)
        g_animseq[i].active = 0;
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, AnimSeq_OnAnimFinished);
    EventType_Register(EV_ANIMSEQ_DONE, 2);
}

// ================================================================================================
// Common helpers
// ================================================================================================

// mercury 0x100016c0 / mars_1a 0x10002280: EV_DESTROYED {object, source, size}
void MissionLib_OnDestroyedSound(const LanderEvent *ev)
{
    ObjId id = (ObjId)ev->arg[0];
    int32_t size = (int32_t)ev->arg[2];
    Sound_StopObject(id);
    if (size > 99)
        Sound_PlayOnce(id, 0x72);      // 114
    else if (size > 79)
        Sound_PlayOnce(id, 0x71);      // 113
    else if (size > 59)
        Sound_PlayOnce(id, 0x70);      // 112
    else if (size > 39)
        Sound_PlayOnce(id, 0x6f);      // 111
    else if (size > 0)
        Sound_PlayOnce(id, 0x6e);      // 110
}

// training_1 0x10001040 / training_2 0x10001220 (arithmetic shift)
void MissionLib_CenterRect(WinRect *r)
{
    r->x = (gWindowWidth - r->w) >> 1;
    r->y = (gWindowHeight - r->h) >> 1;
}
