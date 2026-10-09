// Lander (1999) control mapping - faithful C translation. See input_map.h and
// re/specs/camera-input-hud.md §2. Addresses are lander.EXE (D3D build) virtual addresses.
#include <string.h>

#include "input_map.h"
#include "gametext.h"

#include <stdio.h>
#include <stdlib.h>

im_host_t im_host;
int32_t im_mouse_axes[2];
int32_t im_mouse_sensitivity[2] = {1000, 1000}; // 0x490138
int32_t im_joy_deadzone[6] = {0x140, 0x140, 0x140, 0x140, 0x140, 0x140}; // 0x490140

// ---------------------------------------------------------------------------------------------
// static tables

// gCtrlConfTable 0x4902a8, entries 0..2. Device ids: 1 keyboard, 2 mouse, 3 joystick.
const im_config_t im_presets[3] = {
	{0x0, // Keyboard Default
		{0xc8, 0xd0, 0xcb, 0xcd, 0x2e, 0x2f, 0x1d, 0x2d, 0x39, 0xe, 0x31, 0xf, 0x2a, 0x11, 0x1e, 0x2c, 0x19, 0x13},
		{1, 1, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 0x00,
		{1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}},
	{0x0, // Mouse Default
		{0x40000001, 0x0, 0x40000000, 0x0, 0x2e, 0x2f, 0x40000000, 0x2d, 0x39, 0xe, 0x31, 0xf, 0x0, 0x11, 0x1e, 0x2c, 0x19, 0x13},
		{1, 1, 2, 2, 4, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1}, 0x00,
		{2, 0, 1, 0, 2, 0, 2, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0}},
	{0x0, // Joystick Default
		{0x80000001, 0x0, 0x80000000, 0x0, 0x2e, 0x2f, 0x80000000, 0x2d, 0x39, 0xe, 0x31, 0xf, 0x0, 0x11, 0x1e, 0x2c, 0x19, 0x13},
		{1, 1, 2, 2, 4, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1}, 0x00,
		{3, 0, 1, 0, 3, 0, 3, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0}},
};

const char *im_config_names[7];

// the names' text in lander.EXE (gametext.c)
const GameText im_exe_text[] = {
	{.kind = GT_POINTERS, .va = 0x490160, .count = 7, .out = im_config_names, .va_glide = 0x489160},
	{0},
};

// gCtrlConfTable entries 3..6 as the EXE has them (0x490524..): mouse, mouse with fire on the right
// button, keypad, W/X A/D. CTRLCONF.DAT replaces them.
const im_config_t im_user_defaults[4] = {
	{0x0, // User 1
		{0x40000001, 0x0, 0x40000000, 0x0, 0x2e, 0x2f, 0x40000000, 0x2d, 0x39, 0xe, 0x31, 0xf, 0x0, 0x11, 0x1e, 0x2c, 0x19, 0x13},
		{1, 1, 2, 2, 4, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1}, 0x00,
		{2, 0, 1, 0, 2, 0, 2, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0}},
	{0x0, // User 2
		{0x40000001, 0x0, 0x40000000, 0x0, 0x2e, 0x2f, 0x40000000, 0x40000001, 0x39, 0xe, 0x31, 0xf, 0x0, 0x11, 0x1e, 0x2c, 0x19, 0x13},
		{1, 1, 2, 2, 4, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1}, 0x00,
		{2, 0, 2, 0, 2, 0, 2, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0}},
	{0x0, // User 3
		{0x48, 0x50, 0x4b, 0x4d, 0x2c, 0x2d, 0x1d, 0x9c, 0x39, 0xe, 0x31, 0xf, 0x2a, 0x11, 0x10, 0x1e, 0x19, 0x13},
		{1, 1, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 0x00,
		{1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}},
	{0x0, // User 4
		{0x11, 0x2d, 0x1e, 0x20, 0xcb, 0xcd, 0x9d, 0x1c, 0x39, 0xe, 0x31, 0xf, 0x2a, 0x15, 0x48, 0x50, 0x19, 0x13},
		{1, 1, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 0x00,
		{1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}},
};

im_config_t im_configs[IM_NUM_CONFIGS];
int32_t im_config_current;   // gCtrlConfCurrent (the rewrite starts on Keyboard Default, see Config_Load)
static bool configs_ready;

static void configs_init(void) {
	if (configs_ready) return;
	configs_ready = true;
	for (int i = 0; i < 3; i++) im_configs[i] = im_presets[i];
	for (int i = 0; i < 4; i++) im_configs[3 + i] = im_user_defaults[i];
}

// gActionKeys 0x493600 resolved through gKeySlotVars 0x4901e8 to key slots.
const uint8_t im_action_slots[IM_NUM_ACTIONS][2] = {
	{6, 0xff}, {7, 0xff}, {0, 1}, {2, 3}, {4, 5}, {14, 0xff}, {15, 0xff}, {13, 0xff}, {8, 0xff},
	{9, 0xff}, {10, 0xff}, {11, 0xff}, {12, 0xff}, {16, 0xff}, {17, 0xff}};

// gActionKinds 0x493678
const uint8_t im_action_kinds[IM_NUM_ACTIONS] = {
	IM_KIND_ANALOG, IM_KIND_EDGE, IM_KIND_ANALOG, IM_KIND_ANALOG, IM_KIND_ANALOG, IM_KIND_EDGE, IM_KIND_EDGE,
	IM_KIND_HELD, IM_KIND_HELD, IM_KIND_EDGE, IM_KIND_EDGE, IM_KIND_HELD, IM_KIND_HELD, IM_KIND_EDGE, IM_KIND_EDGE};

// ---------------------------------------------------------------------------------------------
// CTRLCONF.DAT

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void config_read(im_config_t *c, const uint8_t *p) {
	c->flags = rd32(p);
	for (int i = 0; i < 18; i++) c->key[i] = rd32(p + 4 + 4 * i);
	memcpy(c->mode, p + 0x4c, 15);
	c->pad = p[0x5b];
	for (int i = 0; i < 30; i++) c->dev[i] = rd32(p + 0x5c + 4 * i);
}

static void config_write(uint8_t *p, const im_config_t *c) {
	uint32_t flags = c->flags;
	if ((flags & 6) == 4) flags = (flags & ~4u) | 2; // CtrlConf_Save: runtime pointers -> GUID Data1
	wr32(p, flags);
	for (int i = 0; i < 18; i++) wr32(p + 4 + 4 * i, c->key[i]);
	memcpy(p + 0x4c, c->mode, 15);
	p[0x5b] = c->pad;
	for (int i = 0; i < 30; i++) wr32(p + 0x5c + 4 * i, c->dev[i]);
}

bool im_ctrlconf_parse(const uint8_t *data, size_t size, im_config_t users[4], uint32_t *selected) { // 0x40e770
	if (size < IM_CTRLCONF_SIZE || rd32(data) != IM_CTRLCONF_MAGIC) {
		return false;
	}
	// The original skips a user config whose devices are not all present (CtrlConf_ResolveDevices).
	for (int i = 0; i < 4; i++) config_read(&users[i], data + 8 + i * IM_CONFIG_SIZE);
	*selected = rd32(data + 4);
	return true;
}

void im_ctrlconf_write(uint8_t out[IM_CTRLCONF_SIZE], const im_config_t users[4], uint32_t selected) { // 0x40e5d0
	memset(out, 0, IM_CTRLCONF_SIZE);
	wr32(out, IM_CTRLCONF_MAGIC);
	wr32(out + 4, selected);
	for (int i = 0; i < 4; i++) config_write(out + 8 + i * IM_CONFIG_SIZE, &users[i]);
}

int im_device_kind(uint32_t flags, uint32_t dev) {
	if (dev == 0) return IM_DEV_NONE;
	if ((flags & 6) == 0) return (int)dev; // presets: 1/2/3
	if (dev == 0x6F1D2B60u) return IM_DEV_MOUSE;    // GUID_SysMouse
	if (dev == 0x6F1D2B61u) return IM_DEV_KEYBOARD; // GUID_SysKeyboard
	return IM_DEV_JOYSTICK;
}

// ---------------------------------------------------------------------------------------------
// mouse axes (Mouse_Poll 0x42a060, Mouse_MoveCursor 0x429cd0)

void im_mouse_poll(int32_t dx, int32_t dy) {
	if (dx > 100) dx = 100;
	if (dx < -100) dx = -100;
	if (dy > 100) dy = 100;
	if (dy < -100) dy = -100;
	im_mouse_axes[0] += dx; // running sum, deliberately not clamped (as the original)
	im_mouse_axes[1] += dy;
}

void im_mouse_reset(void) { im_mouse_axes[0] = im_mouse_axes[1] = 0; } // 0x42a030

// ---------------------------------------------------------------------------------------------
// axis maps (0x40f5f0 .. 0x40f6a0, 0x4251d0 .. 0x425210)

typedef struct {
	int32_t in_min, in_max, out_min, out_max, centre, mul, shift;
} axis_map_t;

static struct {
	const im_config_t *cfg;              // gCtrlConf 0x49be54
	uint32_t key[18];                    // live key variables (0x490874.., by slot)
	uint8_t kind[IM_NUM_ACTIONS];        // 0x5738e1 + a*0x24
	axis_map_t map[IM_NUM_ACTIONS];      // 0x5738e4 + a*0x24
	bool bound[IM_NUM_ACTIONS];          // the original keeps a stale sampler otherwise
	struct { int32_t min, max; } mouse_cal[2], joy_cal[6]; // 0x49bda0, 0x49bdd0
} M;

static int32_t map_scale(const axis_map_t *m, int32_t raw) { // AxisMap_Scale 0x40f5f0
	if (raw < m->in_min) raw = m->in_min;
	else if (raw > m->in_max) raw = m->in_max;
	return ((raw - m->in_min) * m->mul >> m->shift) + m->out_min;
}

static int32_t map_is_on(const axis_map_t *m, int32_t v) { return v > m->centre; } // 0x40f630
static int32_t map_from_button(const axis_map_t *m, bool down) { return down ? m->out_max : m->out_min; } // 0x40f650

static int32_t map_from_pair(const axis_map_t *m, bool k0, bool k1) { // 0x40f670
	if (k0 && k1) return m->centre;
	if (k0) return m->out_min;
	if (k1) return m->out_max;
	return m->centre;
}

static void map_set_out6(axis_map_t *m) { m->out_min = -32; m->out_max = 31; m->centre = -1; } // 0x4251d0

static void map_init(axis_map_t *m, int32_t shift) { // 0x40f6a0
	m->shift = shift;
	m->mul = ((m->out_max - m->out_min) << shift) / (m->in_max - m->in_min);
}

static void map_setup_for(axis_map_t *m, uint32_t code) { // 0x425210
	map_set_out6(m);
	uint32_t n = IM_CODE_INDEX(code);
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE && n < 2) {
		m->in_min = M.mouse_cal[n].min;
		m->in_max = M.mouse_cal[n].max;
		map_init(m, 0xc);
	} else if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_JOY && n < 6) {
		m->in_min = M.joy_cal[n].min;
		m->in_max = M.joy_cal[n].max;
		map_init(m, 0xc);
	}
}

static void calibrate_axis(uint32_t code) { // Ctrl_CalibrateAxis 0x40e9a0
	uint32_t n = IM_CODE_INDEX(code);
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE && n < 2) {
		M.mouse_cal[n].min = im_mouse_sensitivity[n] - 0x579;
		M.mouse_cal[n].max = 0x579 - im_mouse_sensitivity[n];
	} else if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_JOY && n < 6) {
		// DirectInput range -0x400..0x400, dead zone gJoyDeadZone[n] = 0x140 (host's job)
		M.joy_cal[n].min = -0x400;
		M.joy_cal[n].max = 0x400;
	}
}

static uint32_t action_key(int a, int k) {
	uint8_t s = im_action_slots[a][k];
	return s == 0xff ? 0 : M.key[s];
}

// the binding with its device (dev[2a+k]) in the code: the original passes the device itself to the
// device layer, so a captured joystick axis (a bare index 0..5) still reads the joystick
static uint32_t action_code(int a, int k) {
	uint32_t c = action_key(a, k);
	switch (M.cfg ? im_device_kind(M.cfg->flags, M.cfg->dev[a * 2 + k]) : IM_DEV_NONE) {
		case IM_DEV_KEYBOARD: return c & 0xff;
		case IM_DEV_MOUSE: return IM_CODE_INDEX(c) | IM_CODE_MOUSE;
		case IM_DEV_JOYSTICK: return IM_CODE_INDEX(c) | IM_CODE_JOY;
		default: return c;
	}
}

// ---------------------------------------------------------------------------------------------
// selection (CtrlConf_Select 0x40e510, Ctrl_SetupAxes 0x40ea20, Ctrl_BindActions 0x425260)

static void bind_actions(void) {
	for (int a = 0; a < IM_NUM_ACTIONS; a++) {
		uint8_t kind = im_action_kinds[a];
		if (a == IM_FIRE) kind = M.kind[a] ? M.kind[a] : kind; // may have been switched by im_set_fire_auto
		M.kind[a] = kind;
		uint8_t mode = M.cfg->mode[a];
		M.bound[a] = true;
		switch (mode) {
		case IM_MODE_BUTTON:
		case IM_MODE_KEYPAIR:
			if (kind == IM_KIND_ANALOG) map_set_out6(&M.map[a]);
			else if (kind != IM_KIND_HELD && kind != IM_KIND_EDGE) M.bound[a] = false;
			break;
		case IM_MODE_AXIS:
			// held/edge on an axis compare the map without (re)initialising it: they only work if an
			// earlier config set this action's map up (quirk of the original)
			if (kind == IM_KIND_ANALOG) map_setup_for(&M.map[a], action_code(a, 0));
			break;
		case IM_MODE_AXISPAIR:
			if (kind == IM_KIND_ANALOG) {
				map_setup_for(&M.map[a], action_code(a, 0));
				map_setup_for(&M.map[a], action_code(a, 1)); // the second binding's calibration wins
			} else {
				M.bound[a] = false;
			}
			break;
		default:
			M.bound[a] = false;
			break;
		}
	}
}

void im_select(const im_config_t *cfg) {
	M.cfg = cfg;
	memcpy(M.key, cfg->key, sizeof(M.key));
	for (int a = 0; a < IM_NUM_ACTIONS; a++) { // Ctrl_SetupAxes
		if (cfg->mode[a] == IM_MODE_AXIS || cfg->mode[a] == IM_MODE_AXISPAIR) {
			for (int k = 0; k < 2; k++) {
				if (cfg->dev[a * 2 + k] != 0) calibrate_axis(action_code(a, k));
			}
		}
	}
	im_mouse_reset();
	bind_actions();
}

void im_recalibrate(void) { // FUN_00403b40 / Ctrl_CalibrateAxis after the ADVANCED sliders: new ranges, maps
	if (M.cfg) im_select(M.cfg);
}

// ---------------------------------------------------------------------------------------------
// the configurations (CtrlConf_* 0x40e440-0x40e9a0)

static bool device_present(int kind) { // the device list (the rewrite: keyboard, mouse, the first pad)
	if (im_host.device_present) return im_host.device_present(kind);
	return kind == IM_DEV_KEYBOARD || kind == IM_DEV_MOUSE || (kind == IM_DEV_JOYSTICK && input_pad_connected());
}

static bool config_devices_present(const im_config_t *c) { // CtrlConf_ResolveDevices 0x40e440
	for (int i = 0; i < 30; i++) {
		int kind = im_device_kind(c->flags, c->dev[i]);
		if (kind == IM_DEV_NONE) continue;
		if (kind != IM_DEV_KEYBOARD && kind != IM_DEV_MOUSE && kind != IM_DEV_JOYSTICK) return false;
		if (!device_present(kind)) return false;
	}
	return true;
}

int im_config_select(int index) { // CtrlConf_Select 0x40e510
	configs_init();
	if (index < 0 || index >= IM_NUM_CONFIGS || !config_devices_present(&im_configs[index])) return -1;
	im_select(&im_configs[index]);
	return 0;
}

const im_config_t *im_active_config(void) { return M.cfg; }

int im_active_index(void) {
	return M.cfg && M.cfg >= im_configs && M.cfg < im_configs + IM_NUM_CONFIGS ? (int)(M.cfg - im_configs) : -1;
}

bool im_ctrlconf_load(const uint8_t *data, size_t size) { // CtrlConf_Load 0x40e770
	configs_init();
	im_config_t users[4];
	uint32_t selected;
	if (!im_ctrlconf_parse(data, size, users, &selected)) return false;
	// The original skips a user config naming a device it does not find at that moment (keeps the EXE's
	// one, and a later save loses it). Here the configs are read at start-up, before a game pad has been
	// seen, so they are all taken; a missing device is refused when the config is selected.
	for (int i = 0; i < 4; i++) im_configs[3 + i] = users[i];
	if (selected < IM_NUM_CONFIGS) im_config_current = (int32_t)selected;
	return true;
}

void im_ctrlconf_save(uint8_t out[IM_CTRLCONF_SIZE]) { // CtrlConf_Save 0x40e5d0
	configs_init();
	im_ctrlconf_write(out, &im_configs[3], (uint32_t)im_config_current);
}

// ---------------------------------------------------------------------------------------------
// joystick axes as DirectInput delivers them (DIPROP_RANGE, DIPROP_DEADZONE)

int32_t im_joy_scale(float v, int32_t lo, int32_t hi, int32_t deadzone) {
	if (v < -1) v = -1;
	if (v > 1) v = 1;
	int32_t centre = (lo + hi) / 2, half = (hi - lo) / 2;
	int32_t d = (int32_t)(v * (float)half);
	int32_t dz = half * deadzone / 10000;
	if (dz >= half) return centre;
	int32_t ad = d < 0 ? -d : d;
	if (ad <= dz) return centre;
	int32_t out = (ad - dz) * half / (half - dz);
	return centre + (d < 0 ? -out : out);
}

void im_set_fire_auto(bool automatic) { // 0x425940
	M.kind[IM_FIRE] = automatic ? IM_KIND_HELD : IM_KIND_EDGE;
}

// ---------------------------------------------------------------------------------------------
// samplers (0x424eb0 .. 0x425100)

static bool dn(uint32_t c) { return im_host.is_down && im_host.is_down(c); }
static bool pr(uint32_t c) { return im_host.pressed && im_host.pressed(c); }
static int32_t ax(uint32_t c) { return im_host.axis ? im_host.axis(c) : 0; }

int32_t im_sample_action(int a) {
	if (!M.cfg || !M.bound[a]) {
		return M.kind[a] == IM_KIND_ANALOG ? -1 : 0;
	}
	axis_map_t *m = &M.map[a];
	uint32_t k0 = action_code(a, 0), k1 = action_code(a, 1);
	switch (M.cfg->mode[a]) {
	case IM_MODE_BUTTON:
		if (M.kind[a] == IM_KIND_HELD) return dn(k0);                    // 0x424eb0
		if (M.kind[a] == IM_KIND_ANALOG) return map_from_button(m, dn(k0)); // 0x424f20
		if (pr(k0)) {                                                     // 0x424ee0
			if (im_host.consume) im_host.consume(k0);
			return 1;
		}
		return 0;
	case IM_MODE_KEYPAIR:
		if (M.kind[a] == IM_KIND_HELD) return (int)dn(k1) & (int)dn(k0);  // 0x424fc0 (both evaluated)
		if (M.kind[a] == IM_KIND_ANALOG) return map_from_pair(m, dn(k0), dn(k1)); // 0x424f60
		return (int)pr(k1) & (int)pr(k0);                                 // 0x425010
	case IM_MODE_AXIS:
		if (M.kind[a] == IM_KIND_ANALOG) return map_scale(m, ax(k0));     // 0x425060
		return map_is_on(m, map_scale(m, ax(k0)));                        // 0x4250a0 / 0x4250f0
	case IM_MODE_AXISPAIR: {                                              // 0x425100
		int32_t v0 = map_scale(m, ax(k0)), v1 = map_scale(m, ax(k1)), r;
		switch (a) {
		case IM_THRUST:
			r = v1 - v0;
			if (r > 15) r = 15;
			if (r < -16) r = -16;
			return r - 1;
		case IM_PITCH: case IM_ROLL: r = v1 + v0; break;
		case IM_YAW: r = v0 - v1; break;
		default: return 0; // undefined in the original (uses a key code as the value)
		}
		if (r > 31) r = 31;
		if (r < -32) r = -32;
		return r;
	}
	default:
		return 0;
	}
}

// ---------------------------------------------------------------------------------------------
// the control word (Ctrl_SampleLocal 0x4254e0); call every 80 ms with the previous local word

uint32_t im_sample_word(uint32_t w) {
	int32_t v;
	v = im_sample_action(IM_ROLL);
	w = (w & 0xfffc0fffu) | ((uint32_t)(v & 0x3f) << 12);
	v = (int8_t)im_sample_action(IM_YAW);
	w = ((((uint32_t)(-1 - v) << 6) ^ w) & 0xfc0u) ^ w;
	v = im_sample_action(IM_PITCH);
	w = (w & 0xffffffc0u) | (uint32_t)(v & 0x3f);
	v = (int8_t)im_sample_action(IM_THRUST);
	w = ((((uint32_t)(v + 0x3fe0)) << 18 ^ w) & 0xfc0000u) ^ w;
	if (M.cfg && M.cfg->mode[IM_THRUST] == IM_MODE_AXIS) { // throttle axis: invert
		uint32_t t = (w >> 18) & 0x3f;
		w = ((((uint32_t)-(int32_t)(t + 1)) << 18 ^ w) & 0xfc0000u) ^ w;
	}
	if (M.cfg && M.cfg->mode[IM_FULL_TILT] != 0 && !im_sample_action(IM_FULL_TILT)) {
		if (M.cfg->mode[IM_PITCH] == IM_MODE_KEYPAIR) {
			w = (((uint32_t)((int32_t)(w << 26) >> 27) ^ w) & 0x3fu) ^ w;     // pitch: sext6 >> 1
		}
		if (M.cfg->mode[IM_ROLL] == IM_MODE_KEYPAIR) {
			w = (((uint32_t)((int32_t)(w << 14) >> 15) ^ w) & 0x3f000u) ^ w;  // roll: sext6 >> 1
		}
	}
	w &= 0x00ffffffu;
	if (im_sample_action(IM_RECOVER)) w |= IM_W_RECOVER;
	if (im_sample_action(IM_FIRE)) w |= IM_W_FIRE;
	if (im_sample_action(IM_TRACTOR)) w |= IM_W_TRACTOR;
	if (im_sample_action(IM_CAMERA_UP)) w |= IM_W_CAMERA_UP;
	if (im_sample_action(IM_CAMERA_DOWN)) w |= IM_W_CAMERA_DOWN;
	if (im_sample_action(IM_WEAPON_SELECT)) w |= IM_W_WEAPON_SELECT;
	if (im_sample_action(IM_WAYPOINT)) w |= IM_W_WAYPOINT;
	return w;
}

uint32_t im_read_word(uint32_t *word) { // 0x4258a0
	uint32_t w = *word;
	*word &= IM_W_ONESHOT_KEEP;
	return w;
}

// ---------------------------------------------------------------------------------------------
// demo playback (Demo_Init 0x40eb20, Demo_Poll 0x40ebb0, FUN_0040ee40, Demo_End 0x40ed80)

static struct {
	uint8_t *data;
	size_t size;
	uint32_t players, records, stride;
	uint32_t record, word;   // DAT_00577dc0, _DAT_00577db0
	bool active;
} D;

bool im_demo_start(const uint8_t *data, size_t size) {
	im_demo_stop();
	if (!data || size < IM_DEMO_HEADER || rd32(data) != IM_DEMO_MAGIC) return false;
	uint32_t players = (uint32_t)(data[0x54] | data[0x55] << 8), records = rd32(data + 0x58), stride = rd32(data + 0x5c);
	if (!(rd32(data + 0x60) & 1) || !players || stride < players * 4 || (size - IM_DEMO_HEADER) / stride < records) return false;
	D.data = malloc(size);
	if (!D.data) return false;
	memcpy(D.data, data, size);
	D.size = size;
	D.players = players;
	D.records = records;
	D.stride = stride;
	D.record = D.word = 0;
	D.active = true;
	return true;
}

void im_demo_stop(void) {
	free(D.data);
	memset(&D, 0, sizeof(D));
}

bool im_demo_active(void) { return D.active; }

bool im_demo_next(uint32_t *words, int n) {
	bool ok = true;
	for (int i = 0; i < n; i++) {
		if (!D.active || D.record == D.records) { // the end: gLevelEndFlag, a zero word (DAT_0049beb4)
			words[i] = 0;
			ok = false;
			continue;
		}
		words[i] = rd32(D.data + IM_DEMO_HEADER + D.stride * D.record + D.word * 4);
		if (++D.word == D.players) {
			D.word = 0;
			D.record++;
		}
	}
	return ok;
}

// ---------------------------------------------------------------------------------------------
// the rewrite's devices
// DirectInput scan code -> rewrite key (rewrite/src/platform.h: lower-case ASCII + KEY_*)
int im_dik_to_key(uint32_t dik) {
	static const char row1[] = "1234567890-=";   // 0x02..0x0d
	static const char row2[] = "qwertyuiop[]";   // 0x10..0x1b
	static const char row3[] = "asdfghjkl;'`";   // 0x1e..0x29
	static const char row4[] = "\\zxcvbnm,./";   // 0x2b..0x35
	if (dik >= 0x02 && dik <= 0x0d) return row1[dik - 0x02];
	if (dik >= 0x10 && dik <= 0x1b) return row2[dik - 0x10];
	if (dik >= 0x1e && dik <= 0x29) return row3[dik - 0x1e];
	if (dik >= 0x2b && dik <= 0x35) return row4[dik - 0x2b];
	if (dik >= 0x3b && dik <= 0x44) return KEY_F1 + (int)(dik - 0x3b);
	switch (dik) {
	case 0x01: return KEY_ESCAPE; case 0x0e: return KEY_BACKSPACE; case 0x0f: return KEY_TAB;
	case 0x1c: return KEY_RETURN; case 0x1d: return KEY_LCTRL; case 0x2a: return KEY_LSHIFT;
	case 0x36: return KEY_RSHIFT; case 0x37: return KEY_KP_MULTIPLY; case 0x38: return KEY_LALT;
	case 0x39: return KEY_SPACE; case 0x57: return KEY_F11; case 0x58: return KEY_F12;
	case 0x47: return KEY_KP7; case 0x48: return KEY_KP8; case 0x49: return KEY_KP9; case 0x4a: return KEY_KP_MINUS;
	case 0x4b: return KEY_KP4; case 0x4c: return KEY_KP5; case 0x4d: return KEY_KP6; case 0x4e: return KEY_KP_PLUS;
	case 0x4f: return KEY_KP1; case 0x50: return KEY_KP2; case 0x51: return KEY_KP3; case 0x52: return KEY_KP0;
	case 0x53: return KEY_KP_PERIOD; case 0x9c: return KEY_KP_ENTER; case 0x9d: return KEY_RCTRL;
	case 0xb5: return KEY_KP_DIVIDE; case 0xb8: return KEY_RALT; case 0xc7: return KEY_HOME;
	case 0xc8: return KEY_UP; case 0xc9: return KEY_PAGEUP; case 0xcb: return KEY_LEFT; case 0xcd: return KEY_RIGHT;
	case 0xcf: return KEY_END; case 0xd0: return KEY_DOWN; case 0xd1: return KEY_PAGEDOWN;
	case 0xd2: return KEY_INSERT; case 0xd3: return KEY_DELETE;
	}
	return -1;
}

button_t im_code_to_button(uint32_t code) {
	uint32_t n = IM_CODE_INDEX(code);
	switch (code & IM_CODE_DEVICE_MASK) {
	case 0: {
		int k = n < 0x100 ? im_dik_to_key(n) : -1;
		return k < 0 ? BTN_NONE : BTN_KEY(k);
	}
	case IM_CODE_MOUSE:
		return n == 0 ? BTN_MOUSE_LEFT : n == 1 ? BTN_MOUSE_RIGHT : n == 2 ? BTN_MOUSE_MIDDLE : BTN_NONE;
	case IM_CODE_JOY: {
		static const button_t pad[10] = {BTN_PAD_A, BTN_PAD_B, BTN_PAD_X, BTN_PAD_Y, BTN_PAD_L1, BTN_PAD_R1,
			BTN_PAD_L2, BTN_PAD_R2, BTN_PAD_SELECT, BTN_PAD_START};
		return n < 10 ? pad[n] : BTN_NONE;
	}
	}
	return BTN_NONE;
}

axis_t im_joy_axis(int n) { // c_dfDIJoystick's lX lY lZ lRx lRy lRz
	static const axis_t a[6] = {AXIS_PAD_LX, AXIS_PAD_LY, AXIS_PAD_Z, AXIS_PAD_RX, AXIS_PAD_RY, AXIS_PAD_RZ};
	return n >= 0 && n < 6 ? a[n] : AXIS_MAX;
}

// DirectInput 5's object names (the keyboard ones are the driver's key names, GetKeyNameText style)
static const char *key_name(uint32_t dik) {
	static const char *const names[0x59] = {
		NULL, "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
		"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Ctrl", "A", "S",
		"D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "Shift", "\\", "Z", "X", "C", "V",
		"B", "N", "M", ",", ".", "/", "Right Shift", "Num *", "Alt", "Space", "Caps Lock", "F1", "F2", "F3",
		"F4", "F5", "F6", "F7", "F8", "F9", "F10", "Num Lock", "Scroll Lock", "Num 7", "Num 8", "Num 9", "Num -", "Num 4", "Num 5", "Num 6",
		"Num +", "Num 1", "Num 2", "Num 3", "Num 0", "Num Del", NULL, NULL, NULL, "F11", "F12",
	};
	if (dik < 0x59) return names[dik];
	switch (dik) {
		case 0x9c: return "Num Enter"; case 0x9d: return "Right Ctrl"; case 0xb5: return "Num /";
		case 0xb7: return "Prnt Scrn"; case 0xb8: return "Right Alt"; case 0xc5: return "Pause";
		case 0xc7: return "Home"; case 0xc8: return "Up"; case 0xc9: return "Page Up"; case 0xcb: return "Left";
		case 0xcd: return "Right"; case 0xcf: return "End"; case 0xd0: return "Down"; case 0xd1: return "Page Down";
		case 0xd2: return "Insert"; case 0xd3: return "Delete";
		default: return NULL;
	}
}

bool im_code_name(int kind, uint32_t code, bool axis, char *out, size_t size) { // FUN_004295b0 / FUN_00429560
	static const char *const joy_axes[6] = {"X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation", "Z Rotation"};
	uint32_t n = IM_CODE_INDEX(code);
	const char *s = NULL;
	if (kind == IM_DEV_KEYBOARD && !axis) {
		s = key_name(n & 0xff);
	} else if (kind == IM_DEV_MOUSE) {
		if (axis && n < 2) s = n ? "Y Axis" : "X Axis";
		else if (!axis && n < 2) s = n ? "Button 1" : "Button 0";
	} else if (kind == IM_DEV_JOYSTICK) {
		if (axis && n < 6) s = joy_axes[n];
		else if (!axis && n < 10) {
			snprintf(out, size, "Button %d", (int)n + 1);
			return true;
		}
	}
	if (!s) return false;
	snprintf(out, size, "%s", s);
	return true;
}
