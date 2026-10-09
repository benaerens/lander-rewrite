// Mission-script engine API of Lander (Psygnosis 1999).
//
// The original game runs each mission's logic from two native DLLs per mission folder:
//   MISSIONR.DLL  - in-game logic   (LevelStartLevel_Lander/_Flycam/_Animcam, LevelStopLevel)
//   brief.dll     - front-end text  (mail list, briefing, de-briefing / score sheet, SendMail)
// Both call back into functions and globals exported by lander.EXE. This header declares that
// export set exactly as the 64 retail DLLs use it (the union of their import tables, 114 symbols),
// with the ORIGINAL export names, so C ports of the scripts (re/port/missions/*.c) read like the
// DLLs. The rewrite's engine implements these. Addresses are lander.EXE D3D build (image base
// 0x400000). Full semantics, event payloads, data layouts and the porting guide:
// re/specs/mission-api.md.
//
// Conventions
//   * Every script-visible value is a 32-bit integer in the original; the API uses int32_t /
//     uint32_t, except values that are pointers in the original (object property values, event
//     filters, InTime arguments), which use intptr_t / pointer types so the rewrite works on 64-bit.
//   * Angles: 4096 = 360 degrees. 4.12 fixed point: 0x1000 = 1.0. Times in milliseconds of game
//     time (gGameUpdateTime). Positions: integer world units, Y up.
//   * Object ids are packed: see OBJ_ID / MOB_ID below and re/specs/mission.md section 3.4.
#ifndef LANDER_MISSION_API_H
#define LANDER_MISSION_API_H

#include <stddef.h>
#include <stdint.h>

#include "gametext.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------
// Object ids
// ---------------------------------------------------------------------------------------------
typedef uint32_t ObjId;

#define OBJ_ID(type, index)  ((ObjId)(((uint32_t)(type) << 16) | (uint32_t)(index)))           // placed object (0x1C record)
#define MOB_ID(type, index)  ((ObjId)((((uint32_t)(type) | 0x100u) << 16) | (uint32_t)(index))) // mob (0x24 record)
#define OBJ_NONE             ((ObjId)0xFFFFFFFFu)  // "any player" / "none" where an id is expected
#define OBJ_TYPE(id)         (((uint32_t)(id) >> 16) & 0xFFu)
#define OBJ_INDEX(id)        ((uint32_t)(id) & 0xFFFFu)
#define OBJ_TABLE(id)        (((uint32_t)(id) >> 24) & 3u)   // 0 object, 1 mob, 2 dynamic process
#define OBJ_ISMOB(id)        (((uint32_t)(id) & 0x3000000u) == 0x1000000u)

// Object types (ObjectTypes_RegisterAll 0x448760; catalogue in mission.md 5.2)
enum {
    OT_TURRET = 0, OT_LANDER = 1, OT_PROJECTILE = 2, OT_ANIMOBJECT = 3, OT_TRIGGERBOX = 4,
    OT_ANIMCAM = 5, OT_CAMERA = 6, OT_FLYCAM = 7, OT_DESTRUCTIBLE = 8, OT_EXPLOSION = 9,
    OT_SWITCH = 10, OT_POD = 12, OT_PARTICLEEMITTER = 13, OT_ANIMTURRET = 14,
    OT_ANIMDESTRUCTIBLE = 15, OT_FORCEBOX = 16, OT_FUELDUMP = 17, OT_PICKUP = 19,
    OT_INERTIALFRAME = 20, OT_AMBIENTFX = 21, OT_LIGHTOBJECT = 22, OT_SEGMENTED = 23,
    OT_MARKER = 25, OT_TRIGGERPLANE = 26, OT_SHORTFX = 27
};

// Object properties (Object_GetProperty / Object_SetProperty; per-type table in mission-api.md)
enum {
    PROP_POSITION  = 0,   // get: int32_t* {x,y,z}; set (lander/camera/pod/emitter/light): teleport
    PROP_TARGET    = 1,   // set: target/follow id (camera, turret, trigger box/plane, pod, light, frame)
    PROP_MATRIX    = 2,   // get: FwMatrix* (4.12 rotation + translation); set: lander/pod/anim orientation
    PROP_HITPOINTS = 3,   // u8 hit points / lander shield (0..0x100); set 0 destroys
    PROP_ARMOUR    = 4,   // u8 armour (damage divisor)
    PROP_STATE     = 5,   // switch state / turret enable / force-box strength / light state
    PROP_BODY      = 6,   // get: physics body (lander, pod)
    PROP_VELOCITY  = 7,   // get: velocity vector (lander, camera, anim objects)
    PROP_FLAGS     = 8,   // flag word; set value with bit 31 = clear those bits, else set them
    PROP_EXPLOSION = 9,   // set: explosion size used when destroyed
    PROP_ANIMSTATE = 10   // get: animation / destroy-level state
};
#define PROPFLAGS_CLEAR 0x80000000u   // PROP_FLAGS: "clear these bits" marker

// Lander PROP_FLAGS bits (process +0x1c, read every tick by Lander_Update 0x421900)
#define LF_LOCAL_PLAYER  0x0002u   // cockpit voice / HUD belong to this lander (Voice_Check 0x424b70)
#define LF_LOCK_CTRL0    0x0004u   // zero control-word bits 0-5   (first 6-bit axis field)
#define LF_LOCK_CTRL1    0x0008u   // zero control-word bits 6-11  (second axis field)
#define LF_LOCK_CTRL2    0x0010u   // zero control-word bits 12-17 (third axis field)
#define LF_LOCK_CTRL3    0x0020u   // zero control-word bits 18-23 (fourth axis field)
#define LF_LOCK_BUTTONS  0x0040u   // zero control-word bits 24-31 (buttons)
#define LF_LOCK_X        0x0080u   // gTrainingMission only: keep world X and zero X velocity
#define LF_LOCK_Y        0x0100u   // gTrainingMission only: keep world Y and zero Y velocity
#define LF_LOCK_Z        0x0200u   // gTrainingMission only: keep world Z and zero Z velocity
#define LF_ALIVE         0x8000u   // alive / targetable (get)

// ---------------------------------------------------------------------------------------------
// Fixed-point helper types (PSX style)
// ---------------------------------------------------------------------------------------------
typedef struct FwVector { int32_t vx, vy, vz, pad; } FwVector;           // PSX VECTOR
typedef struct FwMatrix { int16_t m[3][3]; int16_t pad; int32_t t[3]; } FwMatrix;  // PSX MATRIX, 0x20 bytes

// ---------------------------------------------------------------------------------------------
// Events (Events_RegisterAll 0x4493e0; payloads in mission-api.md section 3)
// ---------------------------------------------------------------------------------------------
enum {
    EV_PLAYER_JOINED  = 0x00,  // {lander}
    EV_PLAYER_LEFT    = 0x01,  // {lander, name}
    EV_TICK_1S        = 0x02,  // {}            every 1000 ms (Timer task)
    EV_TICK_100MS     = 0x03,  // {}            every 100 ms
    EV_TRIGGER_ENTER  = 0x04,  // {object, triggerbox}
    EV_TRIGGER_LEAVE  = 0x05,  // {object, triggerbox}
    EV_ANIM_WRAPPED   = 0x06,  // {object, anim}  looping animation wrapped
    EV_ANIM_TURNED    = 0x07,  // {object, anim}  ping-pong animation reversed at its end
    EV_DESTROYED      = 0x08,  // {object, damageSource, explosionSize}
    EV_PROJECTILE_HIT = 0x09,  // {target, damage, shooter, projectileKind}
    EV_SWITCH         = 0x0A,  // {switch, newState, shooter}
    EV_UI_0B          = 0x0B,  // front end only
    EV_POD_HOOKED     = 0x0C,  // {pod, lander}
    EV_ANIM_FINISHED  = 0x0D,  // {object, anim}  one-shot animation finished
    EV_UI_CONTROL     = 0x0E,  // front end only {window, control, value}
    EV_PICKUP         = 0x0F,  // {pickup, player}
    EV_COLLISION      = 0x10,  // {object, otherObject, impulse}  physics impact (impulse 0 or > 0x80)
    EV_LANDER_FIRED   = 0x11,  // {lander, weaponKind, projectile}  weaponKind 2 = missile
    EV_LANDER_SPAWNED = 0x12,  // {lander}
    EV_TRIGGER_PLANE  = 0x13,  // {player, plane, side}
    EV_FRAG_KEY       = 0x14,  // {0}  multiplayer frag-count key
    EV_ANIMSEQ_DONE   = 0x20   // DLL-private (registered by the AnimSeq helper, 2 params): {object, sequence}
};
#define EVENT_MAX_TYPES  64     // gEventHandlers 0x551010
#define EVENT_MAX_PARAMS 7      // an event task holds the id + 7 params

typedef struct LanderEvent {
    int32_t  type;               // +0  event id
    intptr_t arg[EVENT_MAX_PARAMS]; // +4  params (32-bit in the original)
} LanderEvent;

typedef void (*EventHandlerFn)(const LanderEvent *ev);

void EventType_Register(int32_t type, int32_t nParams);                               // 0x442ca0
void EventType_AddHandler(int32_t type, const intptr_t *filter, EventHandlerFn fn);   // 0x442cb0
void EventType_RemoveHandler(int32_t type, EventHandlerFn fn);                        // 0x442d20
void Event_Raise(const LanderEvent *ev);                                              // 0x442d70

// ---------------------------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------------------------
extern int32_t gGameUpdateTime;    // 0x60f2c0  game time (ms) of the task being run

// InTime(delay, fn, n, a0..a{n-1}): one-shot call fn(a0..) after delay ms (InTime 0x456980,
// task type 0x4991e0). n = 0..6; n > 6 = nothing is called. In this API the variadic arguments
// MUST be intptr_t (use the InTimeN macros); fn is called through a pointer taking n intptr_t
// arguments, so every InTime target must take only integer/pointer parameters.
typedef void (*InTimeFn)(void);
void InTime(int32_t delayMs, InTimeFn fn, int32_t nArgs, ...);                        // 0x456980
#define InTime0(d, f)                     InTime((d), (InTimeFn)(f), 0)
#define InTime1(d, f, a)                  InTime((d), (InTimeFn)(f), 1, (intptr_t)(a))
#define InTime2(d, f, a, b)               InTime((d), (InTimeFn)(f), 2, (intptr_t)(a), (intptr_t)(b))
#define InTime3(d, f, a, b, c)            InTime((d), (InTimeFn)(f), 3, (intptr_t)(a), (intptr_t)(b), (intptr_t)(c))
#define InTime4(d, f, a, b, c, e)         InTime((d), (InTimeFn)(f), 4, (intptr_t)(a), (intptr_t)(b), (intptr_t)(c), (intptr_t)(e))

// ---------------------------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------------------------
intptr_t Object_GetProperty(ObjId id, int32_t prop);              // 0x449a30  0 when the type has no getter
void     Object_SetProperty(ObjId id, int32_t prop, intptr_t value); // 0x449a60  no setter = crash in the original
void     Object_SetAlive(ObjId id);                               // 0x449900  undo Object_SetDead (object may be woken again)
void     Object_SetDead(ObjId id);                                // 0x449930  remove; not recreated by proximity
void     Object_AdvanceDestroyLevel(ObjId id, int32_t steps);     // 0x449990  model index += steps (wreck / toggle models)
void     MobObj_Wake(int32_t mobIndex);                           // 0x447360  create the mob's process (NOT idempotent)
void     MobObj_Sleep(int32_t mobIndex);                          // 0x447410  destroy the mob's process (NOT idempotent)
void     AnimPlay(ObjId id, int32_t anim, int32_t flags);         // 0x455e20  flags: bits0-1 mode (0 once,1 loop,2 ping-pong), 4 reverse, 0x20 first part only, 0x40 clone, 0x80 interpolate rotations
void     AnimStop(ObjId id, int32_t mode);                        // 0x455f00  0 = freeze at the current pose, 1 = time 0, no pose update
void     AnimSetUpdateInterval(ObjId id, int32_t frameTime);      // 0x455f70  ms per frame / 10 (0 = 100 ms)
void     TexAnimStop(int32_t texture);                         // 0x444390  stop a TexAnimPlay animation
void     TexAnimPlay(int32_t texture, int32_t mode, int32_t periodMs); // 0x444370  world-texfile animation: 1 loop, 2 ping-pong, 3 plasma
int32_t  TrigBox_Test(ObjId trigger, ObjId obj);                   // 0x4570c0  1 = obj's position inside the trigger box
void     FuelDump_SetFuel(ObjId fuelDump, int32_t amount);         // 0x45abb0  pool word 1 (fuel available)
void     Explosion_CreateDynamic(int32_t kind, int32_t size, const int32_t pos[3]); // 0x44f040
void     SnapToTarget(ObjId obj, ObjId target);                    // 0x444470  copy target's matrix + position
void     SetupOrientationTeleport(ObjId from, ObjId to);           // 0x4444c0  remember relative transform from->to
void     PositionTeleport(ObjId obj);                              // 0x444560  move obj by that transform (camera: 0x450d10)
void     OrientationTeleport(ObjId obj);                           // 0x444620  move + rotate obj (and its velocities)
int32_t  LanderAPI_LineOfSight(const int32_t from[3], const int32_t to[3]); // 0x4447a0  1 = clear line of sight (zones + tiles); 0 = blocked or |d| > 0x2000 on an axis
int32_t  Player_GetId(ObjId lander);                               // 0x444200  player id of a lander, -1 = none
uint32_t LndrRand(void);                                           // 0x444420  deterministic PRNG (seeded per mission)

// Pods (type 12): call from the EV_POD_HOOKED handler
void Pod_Script_SetupDynamicsCuboid(ObjId pod, int32_t sx, int32_t sy, int32_t sz, int32_t mass); // 0x44cd20
void Pod_Script_SetupHookPoint(ObjId pod, int32_t x, int32_t y, int32_t z);   // 0x44ce20  (0,0,0) = default point
void Pod_Script_SetupMass(ObjId pod, int32_t mass);                             // 0x44cdb0
void Pod_Script_SetupDrag(ObjId pod, int32_t drag);                             // 0x44cda0  GLOBAL: pod id ignored

// ---------------------------------------------------------------------------------------------
// Level control, physics and environment globals
// ---------------------------------------------------------------------------------------------
void LevelEnd(int32_t result);            // 0x444330  gMissionResult = result, gLevelEndFlag = 1 (>=1 success)
void SetLevelEndMovie(const char *name);  // 0x444760  film played after the mission (copied)
void AbortLevel(void);                    // 0x47b950  FATAL: message box + exit(3)
void SetGravity(int32_t gravity);         // 0x4442f0  default 26
void SetBumpMapping(int32_t texture, int32_t height, int32_t normal); // 0x4443b0  world texfile bump map

extern int32_t gAirResistance;            // 0x4935f0  default 21
extern int32_t gMainThrottlePower;        // 0x4935f4  default 48
extern int32_t gInOutOverride;            // 0x61f360  0 = auto, 1 = force interior lighting, 2 = force exterior
extern int32_t gTrainingMission;          // 0x61f304  1 = training rules (LF_LOCK_X/Y/Z, physics task variant)

typedef struct Waypoint { int32_t x, z; } Waypoint;   // HUD direction arrow targets
extern const Waypoint *gWaypoints;        // 0x61f198  set by the DLL to its own array
extern int32_t gNbWaypoints;              // 0x61f2a0

// Turret behaviour record (0x1c bytes). gTurretTypes[k] is used by turrets whose param byte 3
// & 0x7f == k. Slots 0..19 are reset per level to the built-in table 0x498f20; slots 20..31 are
// only valid if the mission sets them.
typedef struct TurretType {
    int32_t  muzzle[4];   // +0x00 VECTOR: projectile spawn offset in the gun's frame (x, y, z, pad)
    uint8_t  weapon;      // +0x10 low nibble projectile kind, high nibble variant
    uint8_t  twin;        // +0x11 0 single gun, 1 fire a mirrored pair, 2 alternate sides
    uint8_t  burst;       // +0x12 shots per burst (0 = never fires)
    uint8_t  shotDelay;   // +0x13 turret ticks between shots of a burst
    uint8_t  burstDelay;  // +0x14 turret ticks between bursts (also the initial delay)
    uint8_t  turnRate;    // +0x15 max yaw/pitch change per tick (4096 = 360 deg)
    uint16_t maxPitch;    // +0x16 elevation limit
    uint16_t range;       // +0x18 engagement distance (sum of |dx|,|dy|,|dz|)
    uint16_t pad;         // +0x1a
} TurretType;
#define TURRET_TYPE_SLOTS 32
extern const TurretType *gTurretTypes[TURRET_TYPE_SLOTS];  // 0x61e900

void Environment_SetInteriorAmbient(int32_t r, int32_t g, int32_t b);    // 0x40fb40  overwrites mission.dat section C
void Environment_SetExteriorAmbient(int32_t r, int32_t g, int32_t b);    // 0x40fb20
void Environment_OverrideInteriorFog(int32_t r, int32_t g, int32_t b);   // 0x40fb90
void Environment_OverrideExteriorFog(int32_t r, int32_t g, int32_t b);   // 0x40fb60
void Environment_OverrideViewDistance(int32_t fogNear, int32_t fogFar);  // 0x40fbe0
void Environment_RestoreViewDistance(void);                              // 0x40fc10

// ---------------------------------------------------------------------------------------------
// Player / lander
// ---------------------------------------------------------------------------------------------
int32_t Lander_GetCash(ObjId lander);                    // 0x424a40  -1 = gPlayerState cash (front end)
void    Lander_SetCash(ObjId lander, int32_t cash);      // 0x424a90
int32_t Lander_AddMissiles(ObjId lander, int32_t n);     // 0x424af0  returns how many were added
void    Lander_SetFuel(ObjId lander, int32_t fuel);      // 0x424ac0  clamped to the tank size
int32_t Lander_GetPlayerId(ObjId lander);                // 0x424a00
void    Lander_SetHeadLight(ObjId lander, int32_t coneAngle, int32_t intensity); // 0x424990  intensity 0 = off

void Voice_CashAcquired(ObjId lander);                   // 0x424bb0  SIDs 1027,1026
void Voice_ShieldAcquired(ObjId lander, int32_t amount); // 0x424be0  amount 25/50/75/100
void Voice_MissilesAcquired(ObjId lander);               // 0x424d30
void Voice_MissilesFull(ObjId lander);                   // 0x424da0
void Voice_HeatShieldAcquired(ObjId lander);             // 0x424d00
void Voice_SecurityPassAcquired(ObjId lander, int32_t pass); // 0x424d60
int32_t CockVox(int32_t priority);                       // 0x40c860  1 = may start a cockpit-voice line

extern int32_t gSmallCash;    // 0x49891c  2500
extern int32_t gMediumCash;   // 0x498920  5000
extern int32_t gLargeCash;    // 0x498924  7500

// Score record shown by Briefing_ShowScore and filled by the missions' score library
typedef struct GameScoreData {
    int8_t   pickups;         // +0x00 pickups collected            (shown)
    int8_t   pickupsTotal;    // +0x01 pickups in the level         (shown)
    uint8_t  rateShield;      // +0x02 ratings in percent, written by the DLL score library;
    uint8_t  rateCollisions;  // +0x03   not displayed by the retail engine
    uint8_t  rateTime;        // +0x04
    uint8_t  rateMissiles;    // +0x05
    uint8_t  rateAccuracy;    // +0x06 (0xff = nothing fired)
    uint8_t  rateOverall;     // +0x07
    uint8_t  rateTimeBonus;   // +0x08
    uint8_t  rateMix;         // +0x09 low byte of (3*time + collisions + 2*shield)/6 (4.12, unscaled)
    uint8_t  pad0a[2];
    int32_t  shotsFired;      // +0x0c non-missile shots            (shown, summed with +0x10)
    int32_t  missilesFired;   // +0x10
    int32_t  hits;            // +0x14                              (shown)
    int32_t  hitsTaken;       // +0x18                              (shown)
    int32_t  collisions;      // +0x1c EV_COLLISION count
    int32_t  kills;           // +0x20 EV_DESTROYED count (any object)
    int32_t  timeMs;          // +0x24 overwritten by Mission_Run with the engine's own time (shown)
    int32_t  training[8];     // +0x28 training missions: per-stage times / hit counts (brief.dll)
} GameScoreData;
extern GameScoreData gGame_ScoreData;     // 0x61f1c0  never cleared by the engine

// Multiplayer globals (deathmatch DLLs only)
extern int32_t  gMultiPlayerGame;         // 0x577c34
extern int32_t  gNbPlayers;               // 0x61f3c4
extern int32_t  gNetworkGameType;         // 0x551374
extern int32_t  gJoinedGame;              // 0x577c4c
extern int32_t  gThisPlayer;              // 0x61f2a4
extern ObjId    gPlayerObjects[16];       // 0x61f2c0
extern int32_t  gPlayerIds[16];           // 0x61f320
extern uint32_t gTeamAShipColour;         // 0x4a6de4  index into the ship colours (0)
extern uint32_t gTeamBShipColour;         // 0x49359c  index into the ship colours (1)
const char *Message_GetNameFromId(int32_t playerId);  // 0x4164e0  16-byte player-name records

// ---------------------------------------------------------------------------------------------
// Sound and music
// ---------------------------------------------------------------------------------------------
#define SND_COCKPIT_VOICE ((ObjId)0x021F7FFFu)   // pseudo object of the cockpit voice
int32_t Sound_PlayOnce(ObjId obj, int32_t sid);                 // 0x47d180  obj may be OBJ_NONE (2D)
int32_t Sound_PlayLoop(ObjId obj, int32_t sid, int32_t loops);  // 0x47d1a0  loops -1 = forever
int32_t Sound_PlayChain(ObjId obj, int32_t n, ...);             // 0x47d1c0  n int SIDs played in sequence
void    Sound_StopObject(ObjId obj);                            // 0x47d2c0  stop every voice of obj
void    Media_AudioPlay(int32_t track);                         // 0x4641e0  0 = random music track, n = track n (CD track n+1)

// ---------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------
int32_t AutoCam_GetUserAngle(ObjId camera);                // 0x450f60  preset index 0..4
void    AutoCam_SetUserAngle(ObjId camera, int32_t index); // 0x450f80  presets {-600,-350,0,350,600}
void    AutoCam_SetLagY(ObjId camera, int32_t lag);        // 0x450fc0
void    AutoCam_SnapToTarget(ObjId camera);                // 0x4509d0

// ---------------------------------------------------------------------------------------------
// UI: windows, text, 2D overlay
// ---------------------------------------------------------------------------------------------
typedef struct Window Window;                    // UI window (opaque)
typedef struct WinRect { int32_t x, y, w, h; } WinRect;
struct BorderInfoRec;                            // deathmatch HUD border sprite info (opaque)

#define WIN_TEXTLINE   0x0040u   // single text line: WindowSetText
#define WIN_FRAMED     0x0008u   // pane flag 2 (framed)
#define WIN_HTML       0x0400u   // HTML page: WindowSetTextFile(record of the current text file)

Window *WindowCreate(uint32_t flags, Window *parent);                    // 0x4284b0  parent NULL = top level
void    WindowDestroy(Window *w);                                        // 0x428620
void    WindowSetSize(Window *w, int32_t x, int32_t y, int32_t width, int32_t height); // 0x428630
void    WindowSetRect(Window *w, const WinRect *r);                      // 0x428840
void    WindowSetText(Window *w, const char *text);                      // 0x4288f0  stores the POINTER
void    WindowSetTextFile(Window *w, int32_t record);                    // 0x428980  record of gTextFile
void    WindowSetBorderInfo(Window *w, struct BorderInfoRec *info);      // 0x4289c0
void    Window_CreateTransform(Window *w);                               // 0x45d6b0
void    Win_SetTransform(Window *w);                                     // 0x45d660  NULL = screen space
void    Font_Print(const char *s, int32_t x, int32_t y, uint32_t colour, int32_t font, void *otLayer); // 0x430520
void    Font_GetStringLen(const char *s, int32_t *width, int32_t *height, int32_t font); // 0x430570
void    Print(int32_t x, int32_t y, uint32_t colour, const char *s);     // 0x444080  font 0, OT layer +0x10

typedef void (*Render2DFn)(void *otLayer);
extern Render2DFn g2dRenderCallback;      // 0x551370  called every frame with gOT+0x58 (reset per level)
extern WinRect    rectHTML;               // 0x48fc20  {216, 144, 345, 223} briefing/mail pane
extern int32_t    gWindowWidth;           // 0x5f7de0
extern int32_t    gWindowHeight;          // 0x5f7e04
extern struct BorderInfoRec BorderInfo;   // 0x583780
extern int32_t    gLanguage;              // 0x551378  0 English, 1 French, 2 German

// Generic vector helpers imported by the deathmatch DLLs
void    FwVectorAdd(const int32_t a[3], const int32_t b[3], int32_t out[3]);  // 0x4368d0
int32_t FwVectorNormalise(int32_t v[3]);                                     // 0x436830  returns the old length

// ---------------------------------------------------------------------------------------------
// Front end (brief.dll)
// ---------------------------------------------------------------------------------------------
void    SendMail(int32_t world, int32_t mission, int32_t index);         // 0x407c20  add mail (no duplicates)
void    Briefing_MarkLevelCompleted(int32_t world, int32_t mission);    // 0x407d30  Business mail -> read/archived
int32_t Briefing_GetExitCode(int32_t world, int32_t mission);           // 0x407df0  last result (s8) of a mission
int32_t Briefing_HasEmailBeenRead(int32_t world, int32_t mission, int32_t index); // 0x4078c0
void    Briefing_ShowScore(void);                                        // 0x407ed0  engine score sheet
typedef void (*ScoreRenderFn)(void);
void    SetScoreRender(ScoreRenderFn fn);                                // 0x444070  run right after LevelShowDeBriefing

// ---------------------------------------------------------------------------------------------
// Network snapshot support (deathmatch DLLs only)
// ---------------------------------------------------------------------------------------------
void GameState_RegisterDataBlock(void *block, int32_t size);  // 0x442930
void GameState_RegisterFunction(void (*fn)(void));            // 0x442960
void GameState_UnRegisterFunction(void (*fn)(void));          // 0x442980

// ---------------------------------------------------------------------------------------------
// Script interface: what the engine calls (the DLL exports), one struct per DLL.
// ---------------------------------------------------------------------------------------------
#define LEVEL_VERSION 0x10000000u      // LevelGetVersion value (checked by Dll_CheckVersion 0x47b3b0, but a
                                       // mismatch is never reported or fatal: 2 retail briefs return 0xa0000000)

typedef struct mission_script_t {      // MISSIONR.DLL
    const char *name;                  // "MERCURY\\1"
    uint32_t (*get_version)(void);     // LevelGetVersion
    int32_t  (*load)(const char *dllPath); // LevelLoadLevel, 0 = OK
    void     (*start_lander)(void);    // LevelStartLevel_Lander   (Mission_Run modes 0, 3, 4)
    void     (*start_flycam)(void);    // LevelStartLevel_Flycam   (mode 1)
    void     (*start_animcam)(void);   // LevelStartLevel_Animcam  (modes 2, 4); NULL = not exported
    void     (*stop)(void);            // LevelStopLevel
    const GameText *text;              // the DLL's text tables (loaded before LevelLoadLevel)
} mission_script_t;

typedef struct brief_script_t {        // brief.dll
    const char *name;
    uint32_t (*get_version)(void);     // LevelGetVersion
    int32_t  (*load)(const char *dllPath); // LevelLoadLevel
    void     (*briefing)(void);        // LevelBriefing
    // LevelGetEmailInfo: category (1 Business, 2 News, 4 Personal, 5 = no such mail);
    // *from -> 20-char sender field (+8), *subject -> 52-char subject field (+0x1c) of the mail record
    int32_t  (*get_email_info)(int32_t index, const char **from, const char **subject);
    Window  *(*show_email)(Window *root, int32_t index);       // LevelShowEmail
    Window  *(*show_briefing)(Window *pane);                   // LevelShowBriefing
    int32_t  (*show_debriefing)(Window *pane, int32_t result); // LevelShowDeBriefing
    void     (*stop)(void);            // LevelStopLevel
    const GameText *text;              // the DLL's text tables (loaded before LevelLoadLevel)
} brief_script_t;

#ifdef __cplusplus
}
#endif
#endif // LANDER_MISSION_API_H
