// Lander (1999) control mapping: control configurations (CTRLCONF.DAT + 3 built-in presets), the
// per-action samplers and the 32-bit control word. Faithful translation of lander.EXE (D3D build)
// 0x40e440-0x40eae0, 0x40f5f0-0x40f6a0, 0x424eb0-0x425940. Spec: re/specs/camera-input-hud.md §2.
//
// The DirectInput device layer is replaced by im_host_t callbacks working on the original binding
// codes: DIK scan code (keyboard), 0x40000000|n (mouse button/axis n), 0x80000000|n (joystick n).
#ifndef LANDER_PORT_INPUT_MAP_H
#define LANDER_PORT_INPUT_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define IM_CODE_DEVICE_MASK 0xC0000000u
#define IM_CODE_MOUSE 0x40000000u
#define IM_CODE_JOY 0x80000000u
#define IM_CODE_INDEX(c) ((c) & 0x3FFFFFFFu)

enum { IM_DEV_NONE = 0, IM_DEV_KEYBOARD = 1, IM_DEV_MOUSE = 2, IM_DEV_JOYSTICK = 3 };

// Actions (index into gActionKeys 0x493600 / gActionKinds 0x493678 / config mode[] and dev[]).
enum {
	IM_THRUST, IM_FIRE, IM_PITCH, IM_ROLL, IM_YAW, IM_CAMERA_UP, IM_CAMERA_DOWN, IM_RECOVER, IM_TRACTOR,
	IM_WEAPON_SELECT, IM_WAYPOINT, IM_FRAG_COUNT, IM_FULL_TILT, IM_PAUSE, IM_UNUSED14,
	IM_NUM_ACTIONS
};

// Action kinds (gActionKinds)
enum { IM_KIND_HELD = 1, IM_KIND_ANALOG = 2, IM_KIND_EDGE = 0x10 };
// Config modes (config +0x4c)
enum { IM_MODE_NONE = 0, IM_MODE_BUTTON = 1, IM_MODE_AXIS = 2, IM_MODE_KEYPAIR = 4, IM_MODE_AXISPAIR = 8 };

// Control word bits (Ctrl_SampleLocal 0x4254e0)
#define IM_W_PITCH_SHIFT 0       // 6-bit two's complement, -1 = centre
#define IM_W_YAW_SHIFT 6         // stored as (-1 - yaw)
#define IM_W_ROLL_SHIFT 12
#define IM_W_THRUST_SHIFT 18     // 0..63
#define IM_W_FIRE (1u << 24)
#define IM_W_RECOVER (1u << 25)
#define IM_W_TRACTOR (1u << 26)
#define IM_W_CAMERA_UP (1u << 27)
#define IM_W_WEAPON_SELECT (1u << 28)
#define IM_W_WAYPOINT (1u << 29)
#define IM_W_CAMERA_DOWN (1u << 30)
#define IM_W_ONESHOT_KEEP 0x86FFFFFFu // Ctrl_ReadWord clears bits 24, 27, 28, 29, 30

static inline int32_t im_sext6(uint32_t v) { return (int32_t)((v & 63) ^ 32) - 32; }
static inline int32_t im_word_pitch(uint32_t w) { return im_sext6(w >> IM_W_PITCH_SHIFT); }
static inline int32_t im_word_yaw(uint32_t w) { return im_sext6(w >> IM_W_YAW_SHIFT); }   // = -1 - sampled yaw
static inline int32_t im_word_roll(uint32_t w) { return im_sext6(w >> IM_W_ROLL_SHIFT); }
static inline int32_t im_word_thrust(uint32_t w) { return (int32_t)((w >> IM_W_THRUST_SHIFT) & 63); }

// One configuration, 0xd4 bytes on disk (little endian).
typedef struct {
	uint32_t flags;     // +0x00 bits 1-2: dev[] encoding 0 small ids, 2 GUID Data1, 4 runtime
	uint32_t key[18];   // +0x04 per key slot
	uint8_t mode[15];   // +0x4c per action
	uint8_t pad;        // +0x5b
	uint32_t dev[30];   // +0x5c dev[2a], dev[2a+1]
} im_config_t;

#define IM_CONFIG_SIZE 0xD4
#define IM_CTRLCONF_SIZE 0x358
#define IM_CTRLCONF_MAGIC 0xB56FACCAu

extern const im_config_t im_presets[3];          // Keyboard / Mouse / Joystick Default (0x4902a8)
extern const char *im_config_names[7];           // 0x490160 (read from lander.EXE)
extern const uint8_t im_action_slots[IM_NUM_ACTIONS][2]; // key slot(s) per action, 0xff = none
extern const uint8_t im_action_kinds[IM_NUM_ACTIONS];    // 0x493678 (action 1 is changed at run time)

// CTRLCONF.DAT (0x40e770 / 0x40e5d0). Returns false on a bad size/magic. Device ids are kept as stored.
bool im_ctrlconf_parse(const uint8_t *data, size_t size, im_config_t users[4], uint32_t *selected);
void im_ctrlconf_write(uint8_t out[IM_CTRLCONF_SIZE], const im_config_t users[4], uint32_t selected);

// Device id of a binding as the 7 configs store it: 1/2/3 (presets) or a GUID Data1 (user configs):
// 0x6F1D2B60 = system mouse, 0x6F1D2B61 = system keyboard, anything else = a joystick.
int im_device_kind(uint32_t flags, uint32_t dev);

// ---------------------------------------------------------------------------------------------
// host device state (replaces the DirectInput layer, 0x429480-0x4297c0). The codes passed carry the
// device of the binding (0 keyboard, IM_CODE_MOUSE, IM_CODE_JOY) taken from the config's dev[] entry
// (the original reads the device from dev[]: a captured joystick axis is stored as a bare index).
typedef struct {
	bool (*is_down)(uint32_t code);    // InputDev_IsDown
	bool (*pressed)(uint32_t code);    // down now, up at the previous poll
	void (*consume)(uint32_t code);    // forget the current edge
	int32_t (*axis)(uint32_t code);    // raw axis (see im_mouse_poll for the mouse convention)
	bool (*device_present)(int kind);  // IM_DEV_*: the device list has one (keyboard / mouse always)
} im_host_t;

extern im_host_t im_host;

// Mouse axes as the original builds them (Mouse_Poll 0x42a060): per poll the delta is clamped to
// +-100 and ADDED to a running sum (never reset while the device stays acquired). Feed raw deltas
// here once per input poll (80 ms task) and return im_mouse_axes[n] from im_host.axis.
extern int32_t im_mouse_axes[2];
void im_mouse_poll(int32_t dx, int32_t dy);
void im_mouse_reset(void);

// Joystick axes are expected in -0x400..0x400 with a 3.2 % dead zone (0x140 of 10000).
extern int32_t im_mouse_sensitivity[2]; // 0x490138, default 1000 -> +-401 counts full scale
extern int32_t im_joy_deadzone[6];      // gJoyDeadZone 0x490140: DIPROP_DEADZONE per axis, 1/10000 of the range
// A joystick axis as DirectInput reports it with SetRange(lo, hi) and the dead zone (0..10000): the
// zone around the centre reads as the centre, the rest is stretched over the range. v = -1..1.
int32_t im_joy_scale(float v, int32_t lo, int32_t hi, int32_t deadzone);

// ---------------------------------------------------------------------------------------------
// the 7 configurations (gCtrlConfTable 0x4902a8) and the active one
#define IM_NUM_CONFIGS 7
extern im_config_t im_configs[IM_NUM_CONFIGS]; // 0..2 the presets, 3..6 User 1-4 (CTRLCONF.DAT)
extern const im_config_t im_user_defaults[4];  // User 1-4 as the EXE has them before a CTRLCONF.DAT is read
extern int32_t im_config_current;              // gCtrlConfCurrent
// CtrlConf_Select 0x40e510: makes configuration i the active one (live keys, axis maps, samplers);
// -1 when one of its devices is missing (CtrlConf_ResolveDevices 0x40e440), nothing changes then.
int im_config_select(int index);
const im_config_t *im_active_config(void);    // DAT_0049be54
int im_active_index(void);                    // -1 none yet
// CtrlConf_Load / CtrlConf_Save as a buffer: the 4 user configs and gCtrlConfCurrent
bool im_ctrlconf_load(const uint8_t *data, size_t size);
void im_ctrlconf_save(uint8_t out[IM_CTRLCONF_SIZE]);

// ---------------------------------------------------------------------------------------------
// runtime
void im_select(const im_config_t *cfg); // CtrlConf_Select + Ctrl_SetupAxes + Ctrl_BindActions
void im_recalibrate(void);              // the active config again (new sensitivities / dead zones)
void im_set_fire_auto(bool automatic);  // 0x425940: weapon flag 2 -> fire is "held"
int32_t im_sample_action(int action);   // run the action's sampler (0x424eb0-0x425100)
uint32_t im_sample_word(uint32_t prev); // Ctrl_SampleLocal 0x4254e0 (every 80 ms)
uint32_t im_read_word(uint32_t *word);  // Ctrl_ReadWord 0x4258a0: returns the word, clears one-shots

// ---------------------------------------------------------------------------------------------
// demo playback (DEMO.DAT, magic 0x800611EE): a header (player state, world / mission, physics values,
// u16 players at 0x54, u32 records at 0x58, u32 record size at 0x5c, u32 streams at 0x60) and from
// 0x64 one record per CtrlDist tick (80 ms) beginning with every player's control word.
#define IM_DEMO_MAGIC 0x800611EEu
#define IM_DEMO_HEADER 0x64
bool im_demo_start(const uint8_t *data, size_t size);   // Demo_Init + Demo_Poll's record set-up (copies)
void im_demo_stop(void);                                // Demo_End
bool im_demo_active(void);                              // gDemoPlayback
// Ctrl_PlaybackAll 0x425770 (FUN_0040ee40): the next n words; false when the recording has ended (the
// original then sets gLevelEndFlag and returns a zero word)
bool im_demo_next(uint32_t *words, int n);

// ---------------------------------------------------------------------------------------------
// the rewrite's devices (src/platform.h) behind the binding codes
#include "../platform.h"
button_t im_code_to_button(uint32_t code);   // a key / mouse button / joystick button 0..9 (BTN_NONE if none)
axis_t im_joy_axis(int n);                   // joystick axis n (X Y Z Rx Ry Rz) -> the platform's pad axis
int im_dik_to_key(uint32_t dik);             // DirectInput scan code -> KEY_* / ASCII (-1 none)
// The name of a binding as the key editor shows it (DirectInput's object names, FUN_004295b0 /
// FUN_00429560): kind = IM_DEV_*, axis = an axis binding. Returns false for an unknown one.
bool im_code_name(int kind, uint32_t code, bool axis, char *out, size_t size);

#endif
