// The player's ship: Lander object (type 1) of Lander (Psygnosis 1999).
//
// Faithful C port of lander.EXE (Direct3D build, image base 0x400000):
//   0x420190-0x4234cf  lander object: create 0x421030, update 0x421900, render 0x4236f0,
//                      destroy 0x423aa0, save/load 0x423c50/0x423e30, contact callback 0x420ba0
//   0x424050-0x424dff  properties, Lander_* / Voice_* exports, tractor / fuel-dump / armour helpers
//   0x425980           model draw callback (flames, jets, beam, headlight glow)
//   0x426150-0x427117  attitude controller (quaternion error + bang-bang/linear torque, jets)
//   0x4273c0-0x427980  weapon slots and weapon mount models
// Every function carries its original address. The spec is re/specs/lander.md.
//
// Units: positions are world units (physics keeps them x8, see physics.h); angles 4096 = 360 deg;
// matrices and unit vectors 4.12; one update = one 40 ms tick. Control word: see LCW_* below.
//
// Dependencies: physics.h (rigid bodies, by the physics port), mission_api.h (ObjId, events,
// properties, Sound_*, CockVox, AutoCam_*, mission globals). Everything else the lander needs from
// the engine (models, transform nodes, lights, particles, HUD, internal sound calls, players, the
// object system) is declared at the bottom as eng_* adapters, each with its original address.
#ifndef LANDER_H
#define LANDER_H

#include <stdint.h>
#include "physics.h"
#include "mission_api.h"

// ------------------------------------------------------------------ control word (32 bits)
// Built by Ctrl_SampleLocal 0x4254e0 every 80 ms, delivered per player by CtrlDist 0x4257d0,
// read once per lander update by Ctrl_ReadWord 0x4258a0 (which clears the one-shot bits).
#define LCW_PITCH_SHIFT     0         // s6, action 2 (Pitch). Rest value -1 (see lander.md 2.2)
#define LCW_YAW_SHIFT       6         // s6, action 4 (Yaw) stored as (-1 - input): rest -> 0
#define LCW_ROLL_SHIFT      12        // s6, action 3 (Roll)
#define LCW_THROTTLE_SHIFT  18        // u6, action 0 (Thrust): (input + 32) & 63, 0 = off
#define LCW_PITCH_MASK      0x0000003Fu
#define LCW_YAW_MASK        0x00000FC0u
#define LCW_ROLL_MASK       0x0003F000u
#define LCW_THROTTLE_MASK   0x00FC0000u
#define LCW_FIRE            0x01000000u  // action 1, one-shot
#define LCW_RECOVER         0x02000000u  // action 7, held: strong attitude hold
#define LCW_TRACTOR         0x04000000u  // action 8, held
#define LCW_CAM_UP          0x08000000u  // action 5, one-shot
#define LCW_WEAPON          0x10000000u  // action 9, one-shot: next weapon
#define LCW_WAYPOINT        0x20000000u  // action 10, one-shot: next waypoint
#define LCW_CAM_DOWN        0x40000000u  // action 6, one-shot
#define LCW_KEEP_ON_READ    0x86FFFFFFu  // Ctrl_ReadWord: bits kept in the per-player word

// s6 field helpers (shl/sar pairs of the original)
#define LCW_S6(w, sh)       ((int32_t)((uint32_t)(w) << (26 - (sh))) >> 26)

// ------------------------------------------------------------------ process flags (proc +0x1c)
// Same word as PROP_FLAGS (mission_api.h LF_*). Set by scripts via Object_SetProperty(id, 8, v).
#define LPF_CAMERA_KEYS     0x0001u   // local player: Camera Up/Down move the AutoCam preset
#define LPF_HUD             0x0002u   // = LF_LOCAL_PLAYER: HUD, cockpit voice, warnings
#define LPF_LOCK_PITCH      0x0004u   // control-word field locks (LF_LOCK_CTRL0..3, LF_LOCK_BUTTONS)
#define LPF_LOCK_YAW        0x0008u   //   (yaw lock also disables the landing stabiliser)
#define LPF_LOCK_ROLL       0x0010u
#define LPF_LOCK_THROTTLE   0x0020u
#define LPF_LOCK_BUTTONS    0x0040u
#define LPF_PIN_X           0x0080u   // gTrainingMission only: keep X position, zero X velocity
#define LPF_PIN_Y           0x0100u
#define LPF_PIN_Z           0x0200u
#define LPF_SHADOW          0x0400u   // local player's projected shadow (Shadow_Create 0x41d810)
#define LPF_TEAM_B          0x0800u   // multiplayer team game: team B
#define LPF_TRACTOR_LATCH   0x1000u   // tractor ignored until the button is released (after refuel/pickup)
#define LPF_HOMING_TARGET   0x4000u   // set in multiplayer: homing missiles may lock on this lander
#define LPF_ALIVE           0x8000u   // LF_ALIVE: gun target (Lander_IsTargetable mode 1)

// ------------------------------------------------------------------ weapons (gWeaponDefs 0x497fd8)
#define LWF_ENERGY          0x01      // flags: heat gauge instead of ammo
#define LWF_AUTOFIRE        0x02      // flags: Fire is a held button (auto repeat) instead of an edge
#define LWF_HOMING          0x04      // flags: target scan mode 0 (homing targets), keeps its lock
#define LW_HEAT_MAX         0x4000

typedef struct {
	uint8_t  kind;        // +0x0 projectile kind (Objects_Create(2) param); 2 = homing missile
	uint8_t  flags;       // +0x1 LWF_*
	uint8_t  arg2;        // +0x2 projectile parameter (speed/size class?)
	uint8_t  arg3;        // +0x3 projectile parameter (sub type)
	uint32_t heatPerShot; // +0x4 energy weapons: heat added per shot (>>2 in multiplayer)
	uint16_t coolRate;    // +0x8 heat removed per tick once the 22-tick cooldown expired
	uint16_t pad;
	uint32_t mount;       // +0xc gWeaponMountTable index: 0 = lander hardpoints, 1..3 pods
} LanderWeaponDef;

typedef struct {
	int32_t  model;       // objects.ppm (global) entry of the mount model
	int16_t  muzzle[3];   // muzzle offset in the mount model
	int16_t  pad;
} LanderWeaponMountDef;   // 0x497fa8, 12 bytes

extern const LanderWeaponDef      gWeaponDefs[23];        // 0x497fd8
extern const LanderWeaponMountDef gWeaponMountTable[4];   // 0x497fa8

typedef struct {
	int32_t  type;        // +0 weapon id (1..22, gWeaponDefs index)
	int16_t  level;       // +4 ammo (missiles) or heat (energy, 0..0x4000)
	int16_t  cooldown;    // +6 22 after a shot; heat only decays when 0
} LanderWeaponSlot;

typedef struct {
	int32_t  nModels;      // +0x00 model instances of this mount (1, or 2 = mirrored pair)
	int16_t  modelInst[2]; // +0x04
	int16_t  muzzle[2][4]; // +0x08 muzzle positions in the lander frame {x, y, z, pad}
} LanderWeaponMount;       // 0x18

typedef struct {
	uint8_t  count;        // +0x304 slots in use (0..2)
	uint8_t  current;      // +0x305 selected slot (0xff = none)
	uint8_t  overheated;   // +0x306 energy weapon overheated (no fire until heat+shot <= 0x4000)
	uint8_t  nMounts;      // +0x307 mount models attached
	LanderWeaponSlot  slot[2];   // +0x308
	LanderWeaponMount mount[2];  // +0x318
} LanderWeapons;           // 0x44 bytes, lander +0x304..+0x347

// ------------------------------------------------------------------ attitude controller
typedef struct {
	int16_t  pos[3];       // +0x00 nozzle position (lander frame)
	int16_t  liftY;        // +0x06 added to y while the jet fires "positive"
	int16_t  nozzle[4];    // +0x08 (template data, not read by the code found)
	int32_t  dir[3];       // +0x10 flame direction 4.12
	int32_t  pad;          // +0x1c
	int32_t  intensity;    // +0x20 signed output (torque / 128 or / 180)
	int32_t  size;         // +0x24 flame base offset along dir
} LanderJet;               // 0x28; template gAttitudeJetTemplate 0x497e38

typedef struct {
	phys_body_t *body;     // +0x00 (lander +0x13c)
	int32_t  tqNormal[3];  // +0x04 (gain*I)>>9      max torque, normal mode
	int32_t  tqStrong[3];  // +0x10 (2*gain*I)>>9    max torque, Recover mode / braking
	int32_t  tqSmall[3];   // +0x1c (2*I)>>9         minimum (dead-band) torque
	int32_t  rateNormal[3];// +0x28 gain             linear-zone limit, normal mode
	int32_t  rateDead[3];  // +0x34 2                dead band of the linear zone
	int32_t  rateStrong[3];// +0x40 2*gain           linear-zone limit, Recover mode / braking
	int32_t  angleDead;    // +0x4c 2                angle error ignored below this
	LanderJet jets[8];     // +0x50 0/1 pitch(x), 2/3 roll(z), 4..7 yaw(y)
} LanderAttCtrl;           // 0x190, lander +0x13c..+0x2cb

// ------------------------------------------------------------------ engine flame / glow block
typedef struct {
	int16_t  pos[3];       // +0x00 exhaust point (rewritten per exhaust by the draw callback)
	int16_t  pad;
	int16_t  dir[4];       // +0x08 {0, -0x1000, 0, 0} flame direction
	int32_t  level;        // +0x10 throttle*64 (lander +0x2dc); 0 = light off
	int32_t  flickerSel;   // +0x14
	int32_t  flicker[2];   // +0x18 light scale - 0x1000 (written by the flame drawer)
	int32_t  viewBelow;    // +0x20 camera sees the nozzle from below (lander +0x2ec)
	uint32_t colour[2];    // +0x24 flame colours 0x00BBGGRR (lander +0x2f0, +0x2f4)
	void    *light;        // +0x2c point light (lander +0x2f8)
} LanderEngineFx;          // 0x30, template gEngineFxTemplate 0x4933c8

// ------------------------------------------------------------------ per lander type
typedef struct {
	int32_t  modelBase;    // +0x00 landers model index: base = intact, base+1 = wreck
	int16_t  jetY[2];      // +0x04 heights of the attitude jets
	int16_t  jetReach;     // +0x08 distance of the pitch/roll jets from the centre
	int16_t  jetSize;      // +0x0a yaw jet flame size
	int32_t  nExhaust;     // +0x0c main engine nozzles (1 or 2)
	int16_t  exhaust[2][4];// +0x10 {x, y, z, pad}
	uint32_t flameColour[2]; // +0x20
	uint8_t  hardpoints;   // +0x28 models on weapon mount 0 (1 or 2 mirrored)
	uint8_t  fireMode;     // +0x29 with 2 hardpoints: 1 alternate, 2 both at once
	int16_t  mirror[3];    // +0x2a mount-0 mirror offset (+ for model 0, - for model 1)
	int16_t  mountOfs[3];  // +0x30 mount-0 common offset (also HUD reticle x/y, Lander_CreateHud)
	int16_t  pad36;
	int16_t  podPos[3];    // +0x38 position of pod mounts (weapon mounts 1..3)
	int16_t  pad3e;
	int32_t  podRotZ;      // +0x40 roll of pod mounts about the model Z axis (0x42d070 / 0x42d0f0)
	int16_t  headlight[3]; // +0x44 headlight position
	int16_t  pad4a;
	int16_t  attGain;      // +0x4c attitude controller gain
	uint16_t mass;         // +0x4e 4.12 hull mass (armour adds 0x100 / 0x300)
	uint16_t thrust;       // +0x50 main engine thrust factor
	uint16_t yawRate;      // +0x52 heading change per yaw unit
} LanderModelDef;          // 0x54
extern const LanderModelDef gLanderModelTable[5];   // 0x4933f8: Hopper, Sky Diver, Big Red, Destria, Roche 81

// ------------------------------------------------------------------ player state (gPlayerState 0x577aa0)
typedef struct {
	int32_t  missiles;     // +0x00 (lander +0xd0)
	int32_t  fuel;         // +0x04 (+0xd4)
	int32_t  maxFuel;      // +0x08 (+0xd8)
	int32_t  shield;       // +0x0c (+0xdc) 0..255, 0 = dead
	int32_t  cash;         // +0x10 (+0xe0)
	int32_t  primary;      // +0x14 (+0xe4) weapon id
	int32_t  secondary;    // +0x18 (+0xe8) weapon id
	int32_t  engine;       // +0x1c (+0xec) 0..11
	int32_t  armour;       // +0x20 (+0xf0) 0..10
	int32_t  landerType;   // +0x24 (+0xf4) 0..4
	int32_t  team;         // +0x28 multiplayer team / colour (lander +0xf8 = ship colour index)
} LanderPlayerState;

// ------------------------------------------------------------------ the lander (gLanders[player])
typedef struct Lander {
	int32_t  heading;        // +0x000 0..0xfff, control-frame yaw
	int32_t  shieldWarn;     // +0x004 HUD shield warning level 0..3, 4 = dead
	int32_t  armourValue;    // +0x008 80/100/150/200 (PROP_ARMOUR)
	int32_t  armourShift0;   // +0x00c reflective: damage of projectile kinds 0/4 >>= this
	int32_t  armourShift1;   // +0x010 reactive:   damage of projectile kinds 1/2 >>= this
	int32_t  fuelWarn;       // +0x014 HUD fuel warning level 0..3
	int32_t  unk018;
	int32_t  scrapeHold;     // +0x01c ticks the scrape sound survives without contact (5)
	int32_t  respawnTimer;   // +0x020 40 ticks after death (multiplayer respawn)
	int32_t  renderFrac;     // +0x024 last render extrapolation fraction (4.12)
	int32_t  scrape;         // +0x028 friction impulse sum this tick -> smoothed scrape level
	int32_t  scrapeSmooth;   // +0x02c (3*old + new)/4
	int16_t  scrapeCount;    // +0x030 sum of normal impulses/8 this tick
	int16_t  dustTimer;      // +0x032 engine-wash/liquid effect cool-down (2 or 4 ticks)
	int16_t  waypoint;       // +0x034 selected waypoint
	int16_t  marker;         // +0x036 HUD marker (waypoint in SP, other player in MP), -1 none
	int16_t  player;         // +0x038 player slot
	int16_t  altSide;        // +0x03a alternate-fire side 0/1
	int16_t  stableTimer;    // +0x03c attitude control off after a contact (gStableCutOffTime)
	int16_t  scanTimer;      // +0x03e target scan every 5 ticks
	int16_t  unk040;
	int16_t  liquidContact;  // +0x042 touched a liquid surface this tick (contact callback)
	int16_t  damaged;        // +0x044 1 = wreck model shown
	int16_t  scrapeLevel;    // +0x046 scrape sound level 0..2
	int32_t  unk048;
	int32_t  sndScrape;      // +0x04c loop handles
	int32_t  sndTractor;     // +0x050
	int32_t  sndEngine;      // +0x054
	int32_t  sndPitch;       // +0x058 never started in this build (Lander_SlewSoundPitch)
	int32_t  tractorTime;    // +0x05c ms the tractor has been on (beam visual)
	FwMatrix attitude;       // +0x060 commanded attitude (body frame) = [inertial x] Ry(heading) x tilt
	FwMatrix display;        // +0x080 target x Ry(45deg); t = position (AutoCam, HUD)
	FwMatrix world;          // +0x0a0 copy of the transform node; t (+0xb4) = position
	int32_t  prevPos[4];     // +0x0c0 position before this tick (TriggerPlane)
	LanderPlayerState ps;    // +0x0d0 10 dwords from gPlayerState; +0x0f8 = ship colour index
	FwMatrix substep[2];     // +0x0fc PROP_MATRIX answers while Phys_Step runs
	LanderAttCtrl att;       // +0x13c
	LanderEngineFx engineFx; // +0x2cc (+0x2dc level, +0x2ec viewBelow, +0x2f0 colours, +0x2f8 light)
	phys_body_t *body;       // +0x2fc
	uint32_t ctrlWord;       // +0x300 control word applied this tick
	LanderWeapons weapons;   // +0x304
	ObjId    target;         // +0x348 weapon lock (-1 none)
	ObjId    hookTarget;     // +0x34c -1 none, id|0x80000000 beam locked, id = pod hooked
	void    *headlight;      // +0x350 spot light or NULL
	void    *beamCoord;      // +0x354 tractor beam transform node
	int16_t  headlightCone;  // +0x358
	int16_t  headlightGlow;  // +0x35a glow sprite handle
	int16_t  headlightLevel; // +0x35c intensity >> 3
	int16_t  targetList[49]; // +0x35e scan result, -1 terminated (0x8000 = mob index)
} Lander;                    // 0x3c0 in the original

#define LANDER_MAX           16
extern Lander gLanders[LANDER_MAX];   // 0x573b40

// ------------------------------------------------------------------ the task (object process)
// Task block 0x2c bytes: {next, wakeTime, type, data[8]}; the lander uses data as below.
typedef struct LanderProc {
	int32_t  wakeTime;       // task +0x04 next update time (render extrapolation)
	int32_t  period;         // type +0x1c (40)
	ObjId    id;             // +0x0c
	void    *coord;          // +0x10 transform node driven by the body (body frame)
	void    *modelCoord;     // +0x14 model render node: child of coord rotated Ry(45 deg) (0x431b50) =
	                         //       the ship's logical frame (weapons, targeting, PROP_MATRIX, HUD)
	Lander  *lander;         // +0x18
	uint32_t flags;          // +0x1c LPF_*
	int16_t  modelInst;      // +0x20
} LanderProc;

// Create arguments (Objects_Create(1, args) from MapObject_Wake / Lander_SpawnPlayer / state load)
typedef struct {
	ObjId    id;
	const int32_t *record;   // mob record: {x, y, z, packedRot, ...} (world units)
	int16_t *param;          // pool word: param[1] = player slot (-1 any), param[0] = body index on load
} LanderCreateArgs;

// ------------------------------------------------------------------ globals (lander module)
extern int32_t gMaxTiltAngle;      // 0x490120  0x400
extern int32_t gStableCutOffTime;  // 0x4935e0  2 (cheat 0x80: 0x400)
extern int32_t gTractorRange;      // 0x498a00  750
extern int32_t gAutoHeading;       // 0x4a6de8  never set in the exe
extern int32_t gCheatInfiniteFuel; // 0x4a6dec  gSoftFruit bit 1
extern int32_t gCheatInvulnerable; // 0x4a6df0  gSoftFruit bit 0
extern Lander *gHudLander;         // 0x4a6dcc  set by the HUD owner's update (warning levels)

// HUD outputs (written by the HUD owner's update; read by the HUD module)
typedef struct {
	int32_t stickRoll, stickPitch;   // 0x4933b8 / 0x4933bc: s6 + 32
	char    waypointChar;            // 0x4933c3 '1'+n or '-'
	int32_t waypointBearing;         // 0x4a6dd4
	int32_t compass;                 // 0x4a6dd8 (heading - 0x600) & 0xfff
	int32_t horizSpeed;              // 0x4a6ddc x8 units/tick
	int32_t vertSpeed;               // 0x4a6de0 clamped +-0x800
	const LanderWeaponDef *weapon;   // 0x577c54
	ObjId   target;                  // 0x577c5c
	int32_t weaponLevel;             // 0x577c60 ammo, or heat*112>>14
	int32_t lockIndicator;           // HUD widget 0x4a6da8 +0x58
} LanderHud;
extern LanderHud gLanderHud;

// ------------------------------------------------------------------ lander module API
void     Lander_Create(LanderProc *p, const LanderCreateArgs *args);          // 0x421030
void     Lander_Update(LanderProc *p);                                        // 0x421900
void     Lander_Render(LanderProc *p, int32_t t);                             // 0x4236f0
void     Lander_Destroy(LanderProc *p);                                       // 0x423aa0
int      Lander_DrawCallback(LanderProc *p);                                  // 0x425980
void     Lander_OnContact(phys_body_t *b, phys_contact_t *c, int32_t jn, int32_t jf); // 0x420ba0
ObjId    Lander_SpawnPlayer(int32_t player);                                  // 0x420f90

Lander  *Lander_FromHandle(ObjId id);                                         // 0x420f30
void     Lander_SetShield(ObjId id, int32_t shield);                          // 0x424270 (PROP_HITPOINTS set)
int32_t  Lander_ArmourScaleDamage(ObjId id, int32_t dmg, int32_t kind);       // 0x424690
int      Lander_TakeFuel(ObjId id, int32_t *dumpFuel);                        // 0x424740
int      Lander_IsTractorOn(ObjId id);                                        // 0x424840
int      Lander_SetHookTarget(ObjId id, ObjId target);                        // 0x424860
int32_t  Lander_SwapHeading(ObjId id, int32_t heading);                       // 0x4249e0
int32_t  Lander_GetHeading(ObjId id);                                         // 0x4204a0
const int32_t *Lander_GetPrevPos(ObjId id);                                   // 0x424a20
intptr_t Lander_GetProperty(ObjId id, int32_t prop);                          // getters 0x424050..0x4241d0
void     Lander_SetProperty(ObjId id, int32_t prop, intptr_t value);          // setters 0x4243a0..0x424620
int32_t  Lander_BodyChecksum(int32_t player);                                 // 0x424df0
// exported (mission_api.h prototypes): Lander_GetCash/SetCash/GetFuel/SetFuel/AddMissiles/
// GetPlayerId/SetHeadLight, Voice_* are implemented in lander.c.
int32_t  Lander_GetFuel(ObjId lander);                                        // 0x424a70

void     Lander_WarningVoiceCycle(void);                                      // 0x4205d0 (InTime target)

// attitude controller
void     AttCtrl_Run(LanderAttCtrl *c, const FwMatrix *target, int32_t rate[3], int strong, int isLocal); // 0x426330
int32_t  AttCtrl_Apply(const int32_t torque[3], LanderAttCtrl *c, int isLocal);                        // 0x427060

// ------------------------------------------------------------------ engine adapters
// Implemented by other modules of the rewrite. Addresses = the original routine.
extern LanderPlayerState gPlayerState;           // 0x577aa0 (front end / save game)
extern LanderPlayerState gMPPlayerState[16];     // 0x577760 multiplayer per-player configs
extern int32_t  gThisPlayerId;                   // 0x61f3c8 local player slot
extern uint32_t gShipColours[16];                // 0x4935a0 0x00BBGGRR
extern int32_t  gUseInertialFrame;               // 0x577a30
extern FwMatrix gInertialFrame;                  // 0x577a40
extern int32_t  gLevelEndFlag;                   // 0x577a2c
extern int32_t  gDamageSource;                   // 0x61f364
extern ObjId    gCameraObject;                   // 0x4989b0
extern int32_t  gFFDevicePresent;                // 0x4a6ea8 != 0: force-feedback effect available
extern int32_t  gMouseStickPresent;              // 0x49be58 != 0: mouse configuration active
extern int32_t  gProjectileSecondShot;           // 0x553b40 set around the 2nd shot of a pair
extern int32_t  gLoadingGameState;               // 0x5533d8 GameState_LoadTasks in progress
extern int32_t  gReflectionPass;                 // 0x49c5b4
extern int32_t  gMirrorProbe;                    // 0x491be4 pre-reflection probe pass
extern int32_t  gLanderAboveLiquid;              // 0x577a80
extern int32_t  gAutoCamQuery;                   // 0x553a14
extern int32_t  gOutsideFactor;                  // 0x4908c0

uint32_t eng_ctrl_read_word(int32_t player);                         // Ctrl_ReadWord 0x4258a0
int32_t  eng_player_add(void);                                       // Player_Add 0x4440b0
void     eng_player_set_object(int32_t player, ObjId id);            // 0x444250
void     eng_player_remove(int32_t player);                          // Player_Remove 0x444150
ObjId    eng_task_dynamic_id(LanderProc *p);                         // Task_ToIndex(p) | 0x02010000
void     eng_task_type_set_reflective(LanderProc *p);                // type flags |= 8 (reflection pass)
void     eng_objects_create(int32_t type, void *args);               // Objects_Create 0x449820
int      eng_map_find_sleeping_mob(int32_t type);                    // first mob of type not awake, -1
void     eng_map_set_mob_param_hi(ObjId id, int16_t v);              // 0x420f60
void     eng_map_set_mob_model(ObjId id, int16_t modelInst);         // mob record +0x20
void     eng_rot_pack_to_matrix(const int32_t *record, FwMatrix *out); // RotPackToMatrix 0x456bd0
void     eng_mission_game_over(int32_t dead);                        // Mission_GameOver 0x414680
// transform nodes / models
void    *eng_coord_alloc(void);                                      // Coord_Alloc(0x4e9100) 0x431350
void     eng_coord_free(void *c);                                    // 0x4313b0
const FwMatrix *eng_coord_world(void *c);                            // Coord_GetWorldMatrix 0x4314f0
void     eng_coord_identity(void *c);                                // 0x431730 on c+0xc
int16_t  eng_model_inst_create(int32_t globalModel);                 // 0x456410 (gGlobalModels)
void     eng_model_inst_release(int32_t globalModel, int16_t inst);  // 0x4565e0
void    *eng_model_inst_coord(int16_t inst);                         // render item coord
const model_t *eng_model_inst_model(int16_t inst);                   // render item model (dynamics block)
void     eng_model_inst_setup_lander(int16_t inst, void *coord, int32_t landerIndex,
                                     const uint32_t *teamColour);    // draw cb 0x425980, parent, 0x200 flag
void     eng_model_queue(int16_t inst);                              // RenderList_Queue 0x456530
void     eng_model_draw(LanderProc *p);                              // Model_Draw 0x46c040
// lights
void    *eng_light_create(int32_t kind, void *coord);                // 0x436bd0 (1 point, 2 spot, 3 dir)
void     eng_light_destroy(void *l);                                 // 0x436d90
void     eng_light_colour(void *l, int32_t r, int32_t g, int32_t b); // 0x437080
void     eng_light_range(void *l, int32_t inner, int32_t outer);     // 0x436e40
void     eng_light_cone(void *l, int32_t inner, int32_t outer);      // 0x436ec0
void     eng_light_position(void *l, int32_t x, int32_t y, int32_t z); // 0x4371a0
int16_t  eng_headlight_glow(int32_t level, int32_t cone, int32_t outer); // 0x4569a0
// effects (render side)
void    *eng_particle_spawn(int32_t effect, const int32_t pos[3], int32_t param); // 0x454ae0
void     eng_particle_set_velocity(void *particle, int32_t vx, int32_t vy, int32_t vz); // s8 at +0x14
extern uint32_t gParticleColour;                                     // 0x61e8e0 (effect 6)
void     eng_fx_engine_light_create(LanderEngineFx *fx, void *coord); // 0x40aeb0
void     eng_fx_engine_light_destroy(LanderEngineFx *fx);            // 0x40af00
void     eng_fx_engine_begin(LanderEngineFx *fx, void *modelCoord);  // 0x40ac00 (+0x431c40/0x431580)
void     eng_fx_engine_flame(LanderEngineFx *fx);                    // 0x40af70
void     eng_fx_attitude_jet(const LanderJet *jet);                  // 0x40ac30
void     eng_fx_tractor_beam(int32_t len, int32_t alpha, int32_t time, int32_t mode, void *orient); // 0x40c030
void     eng_fx_headlight_glow(int16_t glow, int32_t level, uint32_t rgb); // 0x4569d0
void     eng_fx_beam_orient(void *beamCoord, const int32_t dir[3]);  // 0x44ca20
void     eng_shadow_create(void);                                    // 0x41d810
void     eng_shadow_destroy(void);                                   // 0x41d970
void     eng_shadow_render(void *modelCoord);                        // 0x41d9f0
// sound (internal forms of the Sound_* exports)
int32_t  eng_snd_once(ObjId owner, int32_t sid);                      // 0x47bdd0
int32_t  eng_snd_once_vol(ObjId owner, int32_t sid, int32_t vol);     // 0x47be70
int32_t  eng_snd_once_excl(ObjId owner, int32_t sid);                 // 0x47bf20
int32_t  eng_snd_loop(ObjId owner, int32_t sid, int32_t loops);       // 0x47bfc0
int32_t  eng_snd_loop_vol(ObjId owner, int32_t sid, int32_t loops, int32_t vol); // 0x47c040
int32_t  eng_snd_chain(ObjId owner, int32_t n, ...);                  // 0x47c0d0
int32_t  eng_snd_loop_chain_vol(ObjId owner, int32_t vol, int32_t n, const int32_t *sids); // 0x47c210
void     eng_snd_stop(int32_t handle);                                // 0x47c350
void     eng_snd_stop_object(ObjId owner);                            // 0x47c3e0
void     eng_snd_set_volume(int32_t handle, int32_t vol);             // 0x47c490
float    eng_snd_get_freq(int32_t handle);                            // 0x47c450
void     eng_snd_set_freq(int32_t handle, float f);                   // 0x47c460
int      eng_snd_enabled(void);                                       // 0x47d340 == 1
// HUD / camera / input
void     eng_hud_create(LanderProc *p);                               // Lander_CreateHud 0x420770
void     eng_hud_destroy(void);                                       // 0x420ac0
int16_t  eng_hud_marker_add(int32_t what, uint32_t rgb, int32_t kind);// 0x413010
void     eng_hud_marker_remove(int16_t marker);                       // 0x4130f0
void     eng_hud_attitude_items(Lander *L, LanderProc *p);            // Lander_Render HUD block 0x4237d9
void     eng_input_reset_mouse_stick(void);                           // 0x42a030
void     eng_input_ff_fire(void);                                     // 0x429c30
void     eng_ctrl_set_fire_autorepeat(int on);                        // 0x425940
// world / objects
int      eng_surface_class(int32_t surface);                          // Surface_GetClass 0x451320
void     eng_forcebox_add_body(ObjId id, int32_t size);               // 0x44f0f0
void     eng_forcebox_remove_body(ObjId id);                          // 0x44f150
void     eng_pod_release(ObjId pod);                                  // Pod_SetTarget(pod, -3) 0x44d9e0
int      eng_mob_is_awake(int32_t mobIndex);                          // 0x447540
void     eng_targets_scan(int32_t mode, const FwMatrix *world, int16_t *out, ObjId owner); // 0x4269f0
ObjId    eng_target_list_id(int16_t entry);                           // list entry -> object id
int32_t  eng_rand(void);                                              // C rand() (visual jitter only)

// projectile create parameters (Lander_Update 0x422e8a; read by Projectile_Create 0x452990)
typedef struct {
	uint8_t  kind;           // +0x00 weapon def kind
	uint8_t  zero;           // +0x01
	int16_t  pad02;
	int32_t  f04;            // +0x04 0x100
	int32_t  f08;            // +0x08 0
	int32_t  muzzle[3];      // +0x0c in the model frame
	int32_t  f18;            // +0x18 not written
	int32_t  vel[3];         // +0x1c inherited velocity (missiles/rockets: body vel >> 3), else 0
	int32_t  f28;            // +0x28 not written
	const FwMatrix *world;   // +0x2c model-frame world matrix of the lander
	ObjId    target;         // +0x30 weapon lock: Projectile_Create aims every lander shot at it
	ObjId    owner;          // +0x34 lander id
	int32_t  arg2;           // +0x38 def arg2
	int32_t  arg3;           // +0x3c def arg3
} LanderShotParams;
void     eng_fire_projectile(const LanderShotParams *sp);             // Objects_Create(2, {?, ?, sp})

LanderProc *eng_lander_proc(ObjId id);                               // Objects_GetData(id, 0, &proc)
phys_matrix_t *eng_coord_matrix(void *coord);                         // node +0xc (driven by the body)
FwMatrix *eng_coord_local(void *coord);                               // node +0xc local matrix, t at +0x20
void     eng_light_set_flag10(void *l);                               // light +0x60 |= 0x10
void     eng_light_direction(void *l, const int32_t dir[3]);          // 0x4372f0
int32_t  eng_model_inst_tag(int16_t inst);                            // render item +8 (mount type)
void     eng_model_inst_set_tag(int16_t inst, int32_t tag);
void     eng_model_inst_attach(int16_t inst, void *parent, const int32_t t[3], int32_t rotZ); // 0x4275d0 body
int      eng_model_inst_ready(int16_t inst);                          // model record (or fallback) loaded
void     eng_weapons_tint_mount(LanderWeapons *w, int32_t mount, const uint32_t *rgb); // 0x427980
const int32_t *eng_target_forward(void);                              // transformed volume normal 4 (0x4a6f10+0x40)
void     eng_world_set_query(int32_t mask, ObjId exclude);            // gWorldQueryMask 0x4989f4, 0x498434
int32_t  eng_phys_query_slot(void);                                   // DAT_0058b3c0 (0/1)
int32_t  eng_phys_query_time(void);                                   // DAT_0058b394

#endif