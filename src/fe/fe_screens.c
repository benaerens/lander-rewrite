// The option screens (Audio 0x402850.., Visual 0x402de0.., Controls 0x403230..), Load / Save
// (0x403cf0-0x403f60), Restart / Quit (0x406320, 0x406380), Credits (0x403fb0) and De-Brief
// (0x403fe0 / 0x4040c0). The options change the rewrite's settings (volumes, brightness, view
// distance, control preset); the original's DirectSound / Direct3D device choices have no
// counterpart here and are left out (see re/specs/frontend.md).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fe.h"
#include "../game/sound.h"
#include "../game/input_map.h"
#include "../model.h"
#include "../platform.h"
#include "../fs.h"

int32_t gSfxVolume = 0x1000, gMusicVolume = 0x1000;   // "SFX Volume" 0x48e2f0 / "Music Volume" 0x48e2f4
int32_t gFogDistance = 12800;                        // "Fog Distance" 0x490cac
int32_t gBrightness = 200 << 4;                      // DAT_00490ca8 ("Brightness"; Game_Main sets 200)
int32_t gControlConfig;                              // gCtrlConfCurrent (0 Keyboard, 1 Mouse, 2 Joystick)

static UiSprite opt_sprite = {0, NULL, 6, 0};        // 0x48e2e0
#define R(t, l, b, r) {t, l, b, r}
static const UiRect opt_skin[16] = {                 // 0x48e258: the shared control art
	R(0, 0, 60, 128), R(60, 0, 68, 12), R(60, 12, 70, 24), R(60, 24, 74, 44), R(60, 44, 76, 80), R(60, 80, 80, 128),
	R(80, 76, 96, 128), R(76, 28, 88, 76), R(104, 32, 120, 56), R(104, 56, 120, 80), R(96, 80, 112, 104),
	R(96, 104, 112, 128), R(64, 9, 76, 12), R(64, 13, 76, 16), R(63, 16, 68, 24), R(63, 24, 68, 32),
};
#undef R

UiBarData fe_empty_tabs = {fe_bar_skin, 0, 0, 0, -1, NULL, {0}};      // 0x48f278
static const char *ok_label[1];                                        // DAT_0049a6c8: OK (Text 0x2b)
UiBarData fe_ok_tab = {fe_bar_skin, 0, 0, 1, -1, ok_label, {0}};       // 0x48f248
static const char *audio_labels[2], *visual_labels[2], *controls_labels[2];
UiBarData fe_audio_tabs = {fe_bar_skin, 0, 0, 2, -1, audio_labels, {0}};       // 0x48f188
UiBarData fe_visual_tabs = {fe_bar_skin, 0, 0, 2, -1, visual_labels, {0}};     // 0x48f1b8
UiBarData fe_controls_tabs = {fe_bar_skin, 0, 0, 2, -1, controls_labels, {0}}; // 0x48f1e8

void fe_screens_init(void) { // the UI_LocalizeStrings part of these screens
	ok_label[0] = Text_Get(0x2b);
	audio_labels[0] = visual_labels[0] = Text_Get(0x10);
	audio_labels[1] = visual_labels[1] = controls_labels[1] = Text_Get(0x25);
	controls_labels[0] = Text_Get(0x26);
	opt_sprite.tf = gGlobalTextures;
}

static Window *make_slider(UiSlider *s, UiSliderDesc *d, const WinRect *r) {
	opt_sprite.tf = gGlobalTextures;
	d->sprite = &opt_sprite;
	d->data = s;
	Window *w = Control_CreateSlider2(fe_pane, d);
	if (w) Control_SetRect(w, r);
	return w;
}

static void destroy(Window **w) {
	if (*w) Control_Destroy(*w);
	*w = NULL;
}

static void apply_volumes(void) { sound_set_volumes(gSfxVolume / 4096.0f, gMusicVolume / 4096.0f); }

// ---------------------------------------------------------------- the settings (registry values)
// The original keeps them in HKLM\SOFTWARE\Psygnosis\Studios\Lander (table 0x4897f8 {name, data,
// size, flags}: Config_LoadRegistry 0x482180 at the start of Game_Main, Config_SaveRegistry 0x482430
// when it ends). Here: LANDER.CFG in platform_save_dir(), one "Name=value" line per value, the same
// names. Only the values the front end's options change are kept; the device / driver ones have no
// counterpart.
static const struct { const char *name; int32_t *v; } config_values[] = {
	{"SFX Volume", &gSfxVolume}, {"Music Volume", &gMusicVolume}, {"Fog Distance", &gFogDistance},
	{"Brightness", &gBrightness}, {"Demo Timeout", &gDemoTimeoutMs},
};

static void config_path(char *out, size_t n) { snprintf(out, n, "%s/LANDER.CFG", platform_save_dir()); }

// ---------------------------------------------------------------- CTRLCONF.DAT (CtrlConf_Load 0x40e770 / Save 0x40e5d0)
// The original reads it from the target directory, else from the CD (gSourceDir), and writes it to the
// target directory: here the save folder and the data folder. The 4 user configs + gCtrlConfCurrent.
static bool ctrlconf_found;             // a CTRLCONF.DAT of the save folder was read
static int32_t ctrlconf_dirty;          // DAT_0049bd90

static void ctrlconf_path(char *out, size_t n) { snprintf(out, n, "%s/CTRLCONF.DAT", platform_save_dir()); }

static void ctrlconf_load_saved(void) {
	char path[600];
	uint8_t buf[IM_CTRLCONF_SIZE];
	ctrlconf_path(path, sizeof(path));
	FILE *f = fopen(path, "rb");
	if (!f) return;
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	ctrlconf_found = im_ctrlconf_load(buf, n);
}

void fe_ctrlconf_load_disc(void) { // the CD's copy when the save folder has none (after the data folder is known)
	if (ctrlconf_found) return;
	uint32_t size;
	uint8_t *d = fs_load("CTRLCONF.DAT", &size);
	if (!d) return;
	// its user configs; the disc selects User 1 (the mouse layout) but the rewrite starts on Keyboard Default
	int32_t keep = im_config_current;
	im_ctrlconf_load(d, size);
	im_config_current = keep;
	free(d);
}

static void ctrlconf_save(void) { // CtrlConf_Save 0x40e5d0
	char path[600];
	uint8_t buf[IM_CTRLCONF_SIZE];
	im_ctrlconf_save(buf);
	ctrlconf_path(path, sizeof(path));
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fwrite(buf, 1, sizeof(buf), f);
	fclose(f);
	ctrlconf_found = true;
}

void Config_Load(void) { // Config_LoadRegistry 0x482180 (the values present replace the defaults)
	char path[600], line[128];
	ctrlconf_load_saved();
	config_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		for (size_t i = 0; i < len(config_values); i++) {
			if (!strcmp(line, config_values[i].name)) *config_values[i].v = (int32_t)strtol(eq + 1, NULL, 0);
		}
	}
	fclose(f);
	if (gSfxVolume < 0 || gSfxVolume > 0x1000) gSfxVolume = 0x1000;
	if (gMusicVolume < 0 || gMusicVolume > 0x1000) gMusicVolume = 0x1000;
	if (gFogDistance < 6400 || gFogDistance > 25600) gFogDistance = 12800;
	if (gBrightness < 0 || gBrightness > 0x1000) gBrightness = 200 << 4;
	apply_volumes();
}

void Config_Save(void) { // Config_SaveRegistry 0x482430 (Game_Main's end, FUN_0041c150)
	char path[600];
	config_path(path, sizeof(path));
	FILE *f = fopen(path, "w");
	if (!f) return;
	for (size_t i = 0; i < len(config_values); i++) fprintf(f, "%s=%d\n", config_values[i].name, (int)*config_values[i].v);
	fclose(f);
}

// ---------------------------------------------------------------- Audio (screen 0)
static void sfx_changed(UiSlider *s) { gSfxVolume = s->value; apply_volumes(); }       // FUN_00401220
static void music_changed(UiSlider *s) { gMusicVolume = s->value; apply_volumes(); }   // FUN_00401240
static UiSlider sfx_slider = {opt_skin, 0, 0x1000, 0, 0, NULL, sfx_changed, 0};         // 0x48e2f8
static UiSlider music_slider = {opt_skin, 0, 0x1000, 0, 0, NULL, music_changed, 0};     // 0x48e330
static UiSliderDesc sfx_desc, music_desc;
static Window *w_sfx, *w_music, *w_speakers;

void Audio_TabCreate(void) { // FUN_00402850 (tab 0) / FUN_00402a40 (tab 1, ADVANCED: device options, not here)
	FE_ShowMessage(Text_Get(0xf9));                 // FUN_00402820: the soundtrack message
	FE_ShowMessage2(fe_soundtrack_url);
	if (fe_audio_tabs.current == 1) return;
	sfx_slider.value = gSfxVolume;                  // FUN_004026d0
	music_slider.value = gMusicVolume;
	sfx_slider.label = Text_Get(0x4e);
	music_slider.label = Text_Get(0x4f);
	static const WinRect r1 = {320, 300, 128, 16}, r2 = {320, 320, 128, 16};
	if (!w_sfx) w_sfx = make_slider(&sfx_slider, &sfx_desc, &r1);
	if (!w_music) w_music = make_slider(&music_slider, &music_desc, &r2);
	// FUN_00402c30: the speaker configuration picture (stereo: texture 0xc of misc\global)
	if (!w_speakers && (w_speakers = WindowCreate(0x100, fe_pane))) {
		static const WinRect pr = {404, 144, 128, 128};        // 0x48f538
		WindowSetPicture(w_speakers, 0xc, gGlobalTextures);
		WindowSetRect(w_speakers, &pr);
	}
}

void Audio_TabDestroy(void) { // FUN_00402940 / FUN_00402b90
	destroy(&w_sfx);
	destroy(&w_music);
	FE_HideMessage();
	if (w_speakers) {
		WindowDestroy(w_speakers);
		w_speakers = NULL;
	}
}

// ---------------------------------------------------------------- Visual (screen 1)
static void brightness_changed(UiSlider *s) { // FUN_004013d0 -> FUN_004112d0
	gBrightness = s->value;
	int b = gBrightness >> 4;
	model_set_brightness(b > 0xff ? 0xff : b);
}
static void fog_changed(UiSlider *s) { gFogDistance = s->value; }      // FUN_00401400 -> FUN_00411390 (next mission)
static UiSlider brightness_slider = {opt_skin, 0, 0x1000, 0, 0, NULL, brightness_changed, 0};   // 0x48e3a8
static UiSlider fog_slider = {opt_skin, 6400, 25600, 0, 0, NULL, fog_changed, 0};              // 0x48e4a0..
static UiSliderDesc brightness_desc, fog_desc;
static Window *w_brightness, *w_fog;

void Visual_TabCreate(void) { // FUN_00402de0 (tab 0) / FUN_00402f50 (tab 1)
	if (fe_visual_tabs.current != 1) {
		brightness_slider.value = gBrightness;
		brightness_slider.label = Text_Get(0x52);
		static const WinRect r = {392, 250, 128, 16};       // 0x48e3d0
		if (!w_brightness) w_brightness = make_slider(&brightness_slider, &brightness_desc, &r);
	} else {
		fog_slider.value = gFogDistance;
		fog_slider.label = Text_Get(0xa3);                  // "View Distance"
		static const WinRect r = {392, 170, 128, 16};
		if (!w_fog) w_fog = make_slider(&fog_slider, &fog_desc, &r);
	}
}

void Visual_TabDestroy(void) { // FUN_00402ee0 / FUN_004030d0
	destroy(&w_brightness);
	destroy(&w_fog);
}

// ---------------------------------------------------------------- Controls (screen 2, 0x403230..0x403cc0)
// CONFIGURE: a spinner choosing one of the 7 configurations (3 presets, User 1-4) and the list of the 12
// actions with their bindings; clicking an action of a user config waits for a key / button / axis
// (FUN_004036d0 -> FUN_0040dd60: Ctrl_CaptureButton, Ctrl_CaptureAxis). ADVANCED: the axis bindings'
// mouse sensitivity / joystick dead zone sliders (the calibration). CTRLCONF.DAT is written on leaving.

#define CTL_ACTIONS 12
static const uint8_t ctl_allowed[CTL_ACTIONS] = {3, 1, 6, 6, 6, 1, 1, 1, 1, 1, 1, 1};   // DAT_0048fe90: 1 button, 2 axis, 4 key pair
static const char *ctl_action_names[CTL_ACTIONS + 1];      // DAT_0049ad98 (Text 0x16..0x21)
static char ctl_binding_text[CTL_ACTIONS][32];             // DAT_0049a978
static const char *ctl_bindings[CTL_ACTIONS + 1];          // DAT_0049a4f0
static int32_t ctl_edit_key;                               // DAT_0049ab04: 0, or the binding (1 / 2) being captured
static void ctl_key_select(int32_t row);
static UiListData ctl_keys_data = {0, CTL_ACTIONS, CTL_ACTIONS, -1, -1, 14, ctl_bindings};   // 0x48f490
static UiList2Desc ctl_keys_desc = {&ctl_keys_data, NULL, ctl_key_select, ctl_action_names, 0, 0x9e};   // 0x48f4d0
static Window *w_keys;                                     // DAT_0049b128
#define R(t, l, b, r) {t, l, b, r}
static const UiRect spinner_skin[4] = {R(0, 80, 16, 104), R(0, 104, 16, 128), R(16, 80, 32, 104), R(16, 104, 32, 128)}; // 0x48ecf8
#undef R
static UiSpinner ctl_spin = {spinner_skin, 0, 6, 0, NULL, im_config_names, NULL, NULL, 0, 0x43};   // 0x48ed98
static UiSpinnerDesc ctl_spin_desc = {&opt_sprite, &ctl_spin};                                      // 0x48edc0
static Window *w_spin;                                     // DAT_0049b09c
static bool ctrlconf_loaded;                               // DAT_0049bd8c

static void ui_mouse_off(Window *w) { // FUN_004073e0: a control and its children ignore the mouse
	if (!w) return;
	w->flags |= UIF_NO_MOUSE;
	for (Window *c = w->child; c; c = c->next) ui_mouse_off(c);
}

static void ui_mouse_on(Window *w) { // FUN_00407400
	if (!w) return;
	w->flags &= ~UIF_NO_MOUSE;
	for (Window *c = w->child; c; c = c->next) ui_mouse_on(c);
}

static int keys_for_mode(int mode) { return (mode & 0xc) ? 2 : (mode & 3) ? 1 : 0; } // FUN_0040db30

static void ctl_build_names(void) { // FUN_00403290: "key", "key1, key2", "axis (mouse)" ...
	const im_config_t *c = im_active_config();
	if (!c) return;
	for (int a = 0; a < CTL_ACTIONS; a++) {
		int mode = c->mode[a], n = keys_for_mode(mode);
		char *row = ctl_binding_text[a];
		row[0] = 0;
		for (int k = 0; k < n; k++) {
			char name[32];
			bool editing = ctl_edit_key != 0 && ctl_keys_data.selected == a;
			if (editing && ctl_edit_key == 1 && k == 1) continue;     // the second key is not shown yet
			uint8_t slot = im_action_slots[a][k];
			int kind = im_device_kind(c->flags, c->dev[a * 2 + k]);
			if ((editing && (ctl_edit_key == 1 || k == 1)) || slot == 0xff ||
				!im_code_name(kind, c->key[slot], mode == IM_MODE_AXIS || mode == IM_MODE_AXISPAIR, name, sizeof(name))) {
				snprintf(name, sizeof(name), "??");                   // PTR_DAT_0048f640
			} else if (n == 1 && kind == IM_DEV_MOUSE) {
				strncat(name, Text_Get(0xbd), sizeof(name) - strlen(name) - 1);   // " (mouse)"
			} else if (n == 1 && kind == IM_DEV_JOYSTICK) {
				strncat(name, Text_Get(0xbe), sizeof(name) - strlen(name) - 1);   // " (joystick)"
			}
			size_t l = strlen(row);
			snprintf(row + l, sizeof(ctl_binding_text[a]) - l, k ? ", %s" : "%s", name);
		}
	}
}

static void ctl_direction(char *buf, size_t size, int action, int k) { // FUN_004034d0
	if (action == IM_PITCH) {
		size_t l = strlen(buf);
		snprintf(buf + l, size - l, " %s", Text_Get(k ? 0xc5 : 0xc4));   // forwards / backwards
	} else if (action == IM_ROLL || action == IM_YAW) {
		size_t l = strlen(buf);
		snprintf(buf + l, size - l, " %s", Text_Get(k ? 0xc7 : 0xc6));   // left / right
	}
}

static void ctl_prompt(int action, int key) { // FUN_00403590: the prompt for the action's key
	static char prompt[128], line[128];                    // DAT_0049ac30, DAT_0049a728
	int t = 0;
	if (ctl_allowed[action] == 3) t = 3;
	if (ctl_allowed[action] == 6) t = (key == 2) + 1;
	snprintf(prompt, sizeof(prompt), "%s", Text_Get(0xbf + t));
	if (t == 1 || t == 3) {
		snprintf(line, sizeof(line), "%s %s", Text_Get(0xc3), ctl_action_names[action]);
		ctl_direction(line, sizeof(line), action, key - 1);
		FE_ShowMessage(prompt);
		FE_ShowMessage2(line);
	} else {
		snprintf(line, sizeof(line), " %s %s", Text_Get(0xc3), ctl_action_names[action]);
		ctl_direction(line, sizeof(line), action, key - 1);
		size_t l = strlen(prompt);
		snprintf(prompt + l, sizeof(prompt) - l, "%.*s", (int)(sizeof(prompt) - l - 1), line);
		FE_ShowMessage(prompt);
	}
}

// ---- capturing a binding (Ctrl_CaptureButton 0x40e310, Ctrl_CaptureAxis 0x40e0f0, FUN_0040dd60)
static int cap_polls;                      // DAT_0049be4c
static int32_t cap_last[8], cap_max[8];    // joystick axes 0..5, mouse axes 6..7

static void capture_reset(void) { cap_polls = 0; } // FUN_0040e0a0 (+ the joystick's capture range)

// a button pressed this frame: the UI's latched keys only when the UI has not used them (a click on a row)
static bool cap_pressed(uint32_t code) {
	uint32_t n = IM_CODE_INDEX(code);
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE) return ui_key_pressed(n ? UIK_MOUSE1 : UIK_MOUSE0);
	switch (code) {
		case UIK_UP: case UIK_DOWN: case UIK_LEFT: case UIK_RIGHT: case UIK_SELECT: case UIK_BACK: case UIK_ESCAPE: case UIK_P:
			return ui_key_pressed(code);
		default: break;
	}
	button_t b = im_code_to_button(code);
	return b != BTN_NONE && input_pressed(b);
}

static void cap_consume(uint32_t code) {
	if ((code & IM_CODE_DEVICE_MASK) == IM_CODE_MOUSE) ui_key_consume(IM_CODE_INDEX(code) ? UIK_MOUSE1 : UIK_MOUSE0);
	else if (!(code & IM_CODE_DEVICE_MASK)) ui_key_consume(code);
}

static bool capture_button(int *kind, uint32_t *code) { // Ctrl_CaptureButton: keyboard, mouse 0-1, joystick 0-9
	uint32_t found[5];
	int n = 0;
	for (uint32_t c = 0; c < 0x100 && n < 5; c++) {
		if (cap_pressed(c)) { found[n++] = c; *kind = IM_DEV_KEYBOARD; cap_consume(c); }
	}
	for (uint32_t c = IM_CODE_MOUSE; c < IM_CODE_MOUSE + 2 && n < 5; c++) {
		if (cap_pressed(c)) { found[n++] = c; *kind = IM_DEV_MOUSE; cap_consume(c); }
	}
	for (uint32_t c = IM_CODE_JOY; c < IM_CODE_JOY + 10 && n < 5 && input_pad_connected(); c++) {
		if (cap_pressed(c)) { found[n++] = c; *kind = IM_DEV_JOYSTICK; }
	}
	if (!n) return false;
	*code = found[0];   // (the device is the last one that had a press, as the original)
	return true;
}

static bool capture_axis(int *kind, uint32_t *code) { // Ctrl_CaptureAxis: the axis that moved most in 75 polls
	int32_t v[8] = {0};
	bool joy = input_pad_connected();
	for (int i = 0; i < 6 && joy; i++) v[i] = im_joy_scale(input_axis(im_joy_axis(i)), -1000, 1000, 500); // FUN_0040e0a0's range
	im_mouse_poll((int32_t)input_axis(AXIS_MOUSE_DX), (int32_t)input_axis(AXIS_MOUSE_DY));   // the FE's Input_Poll
	v[6] = im_mouse_axes[0];
	v[7] = im_mouse_axes[1];
	if (cap_polls == 0) {
		for (int i = 0; i < 8; i++) {
			cap_last[i] = v[i];
			cap_max[i] = 0;
		}
		cap_polls++;
		return false;
	}
	for (int i = 0; i < 8; i++) {
		if (i < 6 && !joy) continue;
		int32_t d = v[i] - cap_last[i];
		if (d < 0) d = -d;
		if (cap_max[i] < d) cap_max[i] = d;
		cap_last[i] = v[i];
	}
	if (++cap_polls != 0x4b) return false;
	cap_polls = 0;
	int best = 0;
	for (int i = 1; i < 8; i++) {
		if (cap_max[best] < cap_max[i]) best = i;
	}
	if (best < 6) {
		if (cap_max[best] <= 100) return false;
		*kind = IM_DEV_JOYSTICK;
		*code = (uint32_t)best;            // a bare axis index, as the original stores it
		return true;
	}
	if (cap_max[best] <= 0x32) return false;
	*kind = IM_DEV_MOUSE;
	*code = IM_CODE_MOUSE + (uint32_t)(best - 6);
	return true;
}

static uint32_t dev_id(const im_config_t *c, int kind) { // the device id in the config's own encoding
	if ((c->flags & 6) == 0) return (uint32_t)kind;     // 1 keyboard, 2 mouse, 3 joystick
	switch (kind) {
		case IM_DEV_KEYBOARD: return 0x6F1D2B61u;       // GUID_SysKeyboard.Data1
		case IM_DEV_MOUSE: return 0x6F1D2B60u;          // GUID_SysMouse.Data1
		default: return 0x4A4F5931u;                    // a joystick (the rewrite's pad has no DirectInput GUID)
	}
}

static bool binding_in_use(const im_config_t *c, uint32_t dev, uint32_t code, int action, int k, int cls) { // FUN_0040dc90
	for (int a = 0; a < IM_NUM_ACTIONS; a++) {
		for (int j = 0; j < 2; j++) {
			if ((a == action && j == k) || im_action_slots[a][j] == 0xff) continue;
			int m = c->mode[a];
			bool same_class = cls == 1 ? (m == 1 || m == 4 || m == 0x10) : (m == 2 || m == 8);
			if (same_class && c->dev[a * 2 + j] == dev && c->key[im_action_slots[a][j]] == code) return true;
		}
	}
	return false;
}

static int ctl_capture(int action, int32_t *key) { // FUN_0040dd60: 1 bound, -1 already in use, 0 waiting
	im_config_t *c = im_active_index() >= 3 ? &im_configs[im_active_index()] : NULL;
	if (!c) return 0;
	int k = *key - 1, kind = IM_DEV_NONE, n = 0, cls = 0;
	uint32_t code = 0;
	if (capture_button(&kind, &code) && (n = keys_for_mode(ctl_allowed[action] & 5))) {
		cls = 1;
	} else if (capture_axis(&kind, &code) && (n = keys_for_mode(ctl_allowed[action] & 10))) {
		cls = 2;
	} else {
		return 0;
	}
	uint32_t dev = dev_id(c, kind);
	// a single binding (a button, or an axis) always goes into the first slot; the original writes the
	// binding being captured and then clears the next entry, which corrupts the following action when an
	// axis arrives for a pair's second key
	if (n == 1) k = 0;
	if (binding_in_use(c, dev, code, action, k, cls)) return -1;
	c->key[im_action_slots[action][k]] = code;
	c->dev[action * 2 + k] = dev;
	if (n == 1) {
		c->mode[action] = cls == 1 ? IM_MODE_BUTTON : IM_MODE_AXIS;
		c->dev[action * 2 + 1] = 0;
		if (im_action_slots[action][1] != 0xff) c->key[im_action_slots[action][1]] = 0;
	} else {
		c->mode[action] = cls == 1 ? IM_MODE_KEYPAIR : IM_MODE_AXISPAIR;
	}
	im_select(c);   // the live key variables and samplers follow (the original writes them directly)
	ctrlconf_dirty = 1;
	*key = *key < n ? *key + 1 : 0;
	return 1;
}

static void ctl_key_select(int32_t row) { // FUN_004036d0: every frame while a row is being bound
	if (row < 0 || row >= CTL_ACTIONS) return;
	if (!(w_keys->flags & UIF_NO_MOUSE)) ui_mouse_off(w_keys);
	if (ctl_edit_key == 0) {
		ctl_edit_key = 1;
		ctl_build_names();
		ctl_prompt(row, ctl_edit_key);
	}
	int r = ctl_capture(row, &ctl_edit_key);
	if (r == -1) {
		FE_ShowMessage(Text_Get(0xe7));     // the key is taken
		return;
	}
	if (r == 1) {
		ctl_build_names();
		if (ctl_edit_key == 0) {
			ctl_keys_desc.notify = 0;
			ctl_keys_data.selected = -1;
			ui_mouse_on(w_keys);
			FE_HideMessage();
			return;
		}
		ctl_prompt(row, ctl_edit_key);
	}
}

void fe_controls_list_event(void) { ctl_keys_desc.notify = 1; } // FE_OnControlEvent, CTL_LIST2: a row was picked

static void ctl_config_changed(void) { // FUN_004037e0: the spinner moved
	if (im_config_select(ctl_spin.value) < 0) {
		FE_ShowMessage(Text_Get(0xc8));     // "Config invalid"
		ctl_build_names();
		return;
	}
	FE_ShowMessage(Text_Get(0xc9));         // "Config valid"
	if (im_config_current != ctl_spin.value) {
		im_config_current = ctl_spin.value;
		ctl_build_names();
		ctrlconf_dirty = 1;
	}
	if (im_config_current < 3) ui_mouse_off(w_keys);   // the presets cannot be changed
	else ui_mouse_on(w_keys);
}

void fe_spinner_event(Window *c, intptr_t data) { // FUN_00401670
	(void)c;
	if (data == (intptr_t)&ctl_spin) ctl_config_changed();
}

void Controls_Enter(void) { // 0x403230: CtrlConf_Load once (the rewrite reads it at start-up) and select
	if (!ctrlconf_loaded) {
		ctrlconf_loaded = true;
		im_config_select(im_config_current);
	}
}

void Controls_Exit(void) { // 0x403260
	if (ctrlconf_dirty) {
		ctrlconf_save();
		ctrlconf_dirty = 0;
	}
	FE_HideMessage();
}

void Controls_TabCreate(void) { // 0x403900: FUN_00403790 (the action list) + FUN_00403890 (the spinner)
	for (int i = 0; i < CTL_ACTIONS; i++) {
		ctl_action_names[i] = Text_Get(0x16 + i);
		ctl_bindings[i] = ctl_binding_text[i];
	}
	ctl_edit_key = 0;
	ctl_keys_data.selected = -1;
	ctl_keys_desc.notify = 0;
	ctl_keys_desc.items = NULL;
	ctl_build_names();
	static const WinRect lr = {214, 178, 286, 128};         // 0x48f518
	if (!w_keys && (w_keys = Control_CreateList2(fe_pane, &ctl_keys_desc))) Control_SetRect(w_keys, &lr);
	capture_reset();
	opt_sprite.tf = gGlobalTextures;
	if (!w_spin && (w_spin = Control_CreateSpinner(fe_pane, &ctl_spin_desc))) {
		static const WinRect sr = {236, 140, 142, 16};      // 0x48edc8
		Control_SetRect(w_spin, &sr);
		ctl_spin.value = im_config_current;
		ctl_config_changed();
	}
}

void Controls_TabDestroy(void) { // 0x403910
	destroy(&w_keys);
	destroy(&w_spin);
	if (ctrlconf_dirty && im_config_select(im_config_current) < 0) im_config_select(0);  // (the original: Mouse Default)
}

// ---- ADVANCED: the axis bindings' sensitivity / dead zone (FUN_00403ca0 / FUN_00403cb0)
static UiTextLine adv_title = {0x00b3ad00, 2, NULL, NULL, -2};     // 0x48efc8 "Current Config"
static UiTextLine adv_name = {0x00b3ad00, 1, NULL, NULL, -2};      // 0x48eff0
static Window *w_adv_title, *w_adv_name, *w_adv_list;              // DAT_0049b0d0 / d4, DAT_0049b12c
static UiListData adv_list_data = {0, CTL_ACTIONS, CTL_ACTIONS, -1, -1, 14, ctl_action_names};   // 0x48f4b0
static UiListDesc adv_list_desc = {NULL, &adv_list_data, NULL};
static UiSlider adv_slider[CTL_ACTIONS];                           // 0x49a520 + 0x20 i
static UiSliderDesc adv_slider_desc[CTL_ACTIONS];                  // 0x49ab58 + 8 i
static Window *w_adv_slider[CTL_ACTIONS];                          // DAT_0049a308
static int adv_kind[CTL_ACTIONS];
static uint32_t adv_axis[CTL_ACTIONS];

void Advanced_TabCreate(void) {
	for (int i = 0; i < CTL_ACTIONS; i++) ctl_action_names[i] = Text_Get(0x16 + i);
	adv_title.text = Text_Get(0x5f);                                // FUN_00403bc0
	adv_name.text = im_config_names[im_config_current];
	static const WinRect tr = {236, 140, 0, 0}, nr = {370, 140, 0, 0};   // 0x48efe0 / 0x48f008
	if (!w_adv_title && (w_adv_title = Control_CreateTextLine(fe_pane, &adv_title))) Control_SetRect(w_adv_title, &tr);
	if (!w_adv_name && (w_adv_name = Control_CreateTextLine(fe_pane, &adv_name))) Control_SetRect(w_adv_name, &nr);
	adv_list_desc.items = NULL;                                     // FUN_00403960
	if (!w_adv_list && (w_adv_list = Control_CreateList(fe_pane, &adv_list_desc))) {
		static const WinRect lr = {214, 178, 70, 128};              // 0x48f528
		Control_SetRect(w_adv_list, &lr);
		ui_mouse_off(w_adv_list);
	}
	const im_config_t *c = im_active_config();                      // FUN_004039e0: a slider per single-axis binding
	opt_sprite.tf = gGlobalTextures;
	for (int a = 0; a < CTL_ACTIONS; a++) {
		w_adv_slider[a] = NULL;
		if (!c || c->mode[a] != IM_MODE_AXIS) continue;
		UiSlider *s = &adv_slider[a];
		adv_kind[a] = im_device_kind(c->flags, c->dev[a * 2]);
		adv_axis[a] = IM_CODE_INDEX(c->key[im_action_slots[a][0]]);
		*s = (UiSlider){opt_skin, 0, 0, 0, 0, NULL, NULL, 1};
		if (adv_kind[a] == IM_DEV_MOUSE && adv_axis[a] < 2) {
			s->min = 600;
			s->max = 0x578;
			s->value = im_mouse_sensitivity[adv_axis[a]];
			s->label = Text_Get(0xca);                              // "Sensitivity"
		} else if (adv_kind[a] == IM_DEV_JOYSTICK && adv_axis[a] < 6) {
			s->min = 0;
			s->max = 10000;
			s->value = im_joy_deadzone[adv_axis[a]];
			s->label = Text_Get(0xcb);                              // "Deadzone"
		}
		s->last = s->value;
		WinRect r = {372, 0xae + 0xe * a, 70, 16};                  // 0x48f648
		w_adv_slider[a] = make_slider(s, &adv_slider_desc[a], &r);
	}
}

void Advanced_TabDestroy(void) {
	destroy(&w_adv_title);                                          // FUN_00403c60
	destroy(&w_adv_name);
	destroy(&w_adv_list);                                           // FUN_004039c0
	bool any = false;
	for (int a = 0; a < CTL_ACTIONS; a++) {                         // FUN_00403b40: keep the values, recalibrate
		if (!w_adv_slider[a]) continue;
		if (adv_kind[a] == IM_DEV_MOUSE && adv_axis[a] < 2) im_mouse_sensitivity[adv_axis[a]] = adv_slider[a].value;
		else if (adv_kind[a] == IM_DEV_JOYSTICK && adv_axis[a] < 6) im_joy_deadzone[adv_axis[a]] = adv_slider[a].value;
		destroy(&w_adv_slider[a]);
		any = true;
	}
	if (any) im_recalibrate();
}

// ---------------------------------------------------------------- Restart / Quit (screens 5, 6)
static Window *w_confirm;               // DAT_0049b0a0
static UiTextLine confirm_line = {0x00b3ad00, 1, NULL, NULL, -2};   // 0x48efb0 / 0x48ee10

static void confirm_open(int text) {
	static const WinRect r = {235, 188, 0, 0};              // 0x48ee28
	fe_ok_tab.current = -1;
	confirm_line.text = Text_Get(text);
	if (!w_confirm && (w_confirm = Control_CreateTextLine(fe_pane, &confirm_line))) Control_SetRect(w_confirm, &r);
}

void Restart_Enter(void) { confirm_open(0xb2); }   // FUN_00406320: the restart question
void Quit_Enter(void) { confirm_open(0xb0); }      // FUN_00406380: the quit question
void Confirm_Exit(void) { destroy(&w_confirm); }  // FUN_004063e0
void Restart_Action(int i) { (void)i; FE_RestartGame(); }
void Quit_Action(int i) { (void)i; FE_Quit(); }

// ---------------------------------------------------------------- Credits (screen 7)
void Credits_Enter(void) { // FUN_00403fb0: record 0x34 of misc\text*.dat
	FE_CreateHtmlWindow();
	WindowSetTextFile(fe_html, 0x34);
}

// ---------------------------------------------------------------- De-Brief (screen 8)
void Debrief_Enter(void) { // FUN_00403fe0
	fe_ok_tab.current = -1;
	FE_LoadBriefDll(gFESelWorld, gFESelMission);
	FE_LoadMissionTextPack(gFESelWorld, gFESelMission);
	gScoreRenderFn = NULL;
	if (gLastMissionResult >= 0) {
		gMailCountAtDebrief = gMailCount;
		BriefDll_ShowDeBriefing(fe_pane, gLastMissionResult);
	}
	if (gScoreRenderFn) gScoreRenderFn();
}

void Debrief_Exit(void) { // FUN_004040c0
	FE_DestroyHtmlWindow();
	g2dRenderCallback = NULL;
	gScoreRenderFn = NULL;
}

// ---------------------------------------------------------------- Load / Save (screens 4, 15)
static const char *load_labels[2], *save_labels[2];
UiBarData fe_load_buttons = {fe_bar_skin, 0, -1, 2, -1, load_labels, {0}};   // 0x48f370: CONTINUE, RE-PLAY
UiBarData fe_save_buttons = {fe_bar_skin, 0, -1, 2, -1, save_labels, {0}};   // 0x48f3a0: SAVE, CONTINUE

static const char *slot_names[SAVE_SLOTS + 1];
static UiListData slot_data = {0, 10, SAVE_SLOTS, 0, -1, 16, slot_names};   // 0x48eb28
#define R(t, l, b, r) {t, l, b, r}
static const UiRect combo1_skin[13] = {                                    // 0x48e820 (texture 6)
	R(17, 0, 31, 8), R(17, 10, 31, 18), R(17, 32, 31, 40), R(1, 41, 15, 49), R(1, 51, 15, 59), R(1, 61, 15, 69),
	R(1, 71, 15, 79), R(1, 0, 15, 8), R(1, 10, 15, 18), R(1, 32, 15, 40), R(1, 20, 15, 28),
	R(62, 56, 70, 72), R(98, 56, 106, 72),
};
#undef R
static UiSprite combo1_sprite = {0, NULL, 6, 0};                           // 0x48e788
static UiCombo1 slot_combo = {combo1_skin, NULL, &slot_data, NULL, NULL, NULL, NULL};   // 0x48eb48
static UiCombo1Desc slot_desc = {&combo1_sprite, &slot_combo};              // 0x48eb68
static Window *w_slots, *w_edit;                                           // DAT_0049b08c / DAT_0049b090

static void slots_create(void (*on_select)(UiListData *)) { // FUN_00403cf0 (Save_ReadNames) + FUN_00403d40
	static const WinRect r = {221, 168, 143, 135};                         // 0x48eb70
	Save_ReadNames();
	for (int i = 0; i < SAVE_SLOTS; i++) slot_names[i] = Save_SlotNameBuf(i);
	slot_names[SAVE_SLOTS] = NULL;
	slot_data.count = SAVE_SLOTS;
	combo1_sprite.tf = gGlobalTextures;
	slot_combo.title = Text_Get(0x60);                                     // "GAME POSITIONS"
	slot_combo.on_select = on_select;
	if (!w_slots && (w_slots = Control_CreateComboBox1(fe_pane, &slot_desc))) Control_SetRect(w_slots, &r);
}

void Load_Enter(void) { // FUN_00403db0
	load_labels[0] = Text_Get(0x2a);
	load_labels[1] = Text_Get(0x28);
	fe_load_buttons.current = -1;
	slots_create(NULL);    // (FUN_004014b0, a leftover that picks a debug mission, is not wanted here)
}

void Load_Exit(void) { destroy(&w_slots); } // FUN_00403d90

void Load_Action(int i) { // FUN_00403df0: CONTINUE (0) / RE-PLAY (1)
	gMailState = 0;
	int slot = slot_data.selected;
	if (slot < 0) return;
	int r = Save_Load(slot);                                               // FE_LoadSlot 0x408c60
	gPlayerState.maxFuel = fe_lander_fuel(gPlayerState.landerType);
	if (r != 2) return;
	if (i == 1 && gMailCountAtDebrief >= 0) {
		Mail_RollbackToReplayPoint(gMailReplayPoint, gMailCountAtDebrief);
		gPlayerState.cash = Save_GetReplayCash();
	}
	FE_CloseScreen();
	FE_DestroyHub();
	FE_Enter(FE_ENTRY_LOADED, 0);
}

// the name editor over the selected row (FUN_00403e80, FUN_00401500, FUN_00401640, FUN_0040f6d0)
static char edit_backup[0x20];                                           // DAT_0049a6f0
static int edit_row = -1;                                                // DAT_0048f63c

static int edit_keys(UiEdit2 *e) { // FUN_0040f6d0: Esc cancels (2), Return accepts (1), Backspace deletes
	if (input_pressed(BTN_KEY(KEY_ESCAPE))) return 2;
	if (input_pressed(BTN_KEY(KEY_RETURN))) return 1;
	if (input_pressed(BTN_KEY(KEY_BACKSPACE)) && e->len > 0) {
		e->text[--e->len] = 0;
		return 0;
	}
	int typed[32];
	int n = input_typed(typed, 32);
	for (int k = 0; k < n; k++) {
		if (typed[k] >= 32 && typed[k] < 256 && e->len < e->max - 1) {
			e->text[e->len++] = (char)typed[k];
			e->text[e->len] = 0;
		}
	}
	return 0;
}

static UiEdit2 edit_data = {edit_keys, 0x20, 0, NULL};                     // 0x49b1a0

static void edit_place(UiListData *l) { // FUN_00401500: the editor moves to the selected row
	if (edit_row != -1 && Save_SlotNameBuf(edit_row)) strncpy(Save_SlotNameBuf(edit_row), edit_backup, 0x20);
	char *row = Save_SlotNameBuf(l->selected);
	if (!row) return;
	memcpy(edit_backup, row, sizeof(edit_backup));
	edit_row = l->selected;
	edit_data.max = 0x20;
	edit_data.len = (int16_t)strlen(row);
	edit_data.text = row;
	if (w_edit) {
		UI_SetFocus(w_edit);
		if (w_slots) {
			WinRect r;
			Control_GetRect(w_slots, &r);   // = the rows window's position
			r.x += 8;
			r.y += (l->selected - l->first) * l->rowh;
			r.h = l->rowh;
			Control_SetRect(w_edit, &r);
		}
	}
}

void fe_edit_event(Window *c, int value) { // FUN_00401640: Esc puts the old name back
	(void)c;
	if (value == 2 && edit_row >= 0 && Save_SlotNameBuf(edit_row)) strncpy(Save_SlotNameBuf(edit_row), edit_backup, 0x20);
}

void fe_slots_event(Window *c, int value, int state) { // FE_OnControlEvent, combo1 (list box) part
	if (c != w_slots) return;
	if (state == 0) {
		fe_sound(0x10f);
		slot_data.selected = value + slot_data.first;
		if (slot_combo.on_select) slot_combo.on_select(&slot_data);
	} else if (state == 1) {
		fe_sound(0x111);
	} else if (state == 2) {
		fe_sound(0x110);
	} else if (state == 3) {
		fe_sound(0x114);
	}
}

void Save_Enter(void) { // FUN_00403ee0
	save_labels[0] = Text_Get(0x29);
	save_labels[1] = Text_Get(0x2a);
	fe_save_buttons.current = -1;
	if (slot_data.selected < 0) slot_data.selected = 0;
	slots_create(edit_place);
	if (!w_edit) {
		w_edit = Control_CreateEdit2(fe_pane, &edit_data);   // FUN_00403e80
		edit_row = -1;
		edit_place(&slot_data);
	}
}

void Save_Exit(void) { // FUN_00403f50
	destroy(&w_slots);
	destroy(&w_edit);
}

void Save_Action(int i) { // FUN_00403f60 -> FE_SaveAndContinue 0x408ca0
	if (i == 0 && slot_data.selected >= 0) {
		const char *name = Save_SlotNameBuf(slot_data.selected);
		Save_Write(slot_data.selected, name ? name : "");
		gFEGameSaved = 1;
	}
	FE_Rebuild(FE_ENTRY_LOADED);
}