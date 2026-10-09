// Lander (1999) in-game HUD - faithful C translation of lander.EXE (D3D build) 0x411710-0x413170 and
// the HUD parts of the lander (Lander_CreateHud 0x420770, Lander_Update 0x4231ab-0x4233a2, Lander render
// 0x4237d9-0x423927). Spec: re/specs/camera-input-hud.md §3. Mock-up: re/out/hud/hud_640x480.png.
//
// The original HUD is a set of UI windows drawing with FwLineDraw / FwTileDraw / FwSpriteDrawSectionFX /
// Font_Print into the GUI layer, plus three 3D models in front of the camera. Here every draw goes
// through hud_draw_t, so the rewrite can implement it with render_quad_2d (rewrite/src/render.h).
#ifndef LANDER_PORT_HUD_H
#define LANDER_PORT_HUD_H

#include <stdint.h>
#include <stdbool.h>

// Colours are Windows COLORREF 0x00BBGGRR as in the original (hud_colorref_to_rgba converts).
#define HUD_GREEN_LINE 0x8000u   // lines
#define HUD_GREEN_TEXT 0xb000u   // Font_Print colour
#define HUD_RED_NEEDLE 0x80u
#define HUD_TEX_HUD 0x3a         // global texture 58 (misc\global), 128x128 RGBA atlas

// Blend modes as the primitives pass them to the renderer: 0 opaque, 1 alpha, 2 additive.
enum { HUD_BLEND_OPAQUE = 0, HUD_BLEND_ALPHA = 1, HUD_BLEND_ADD = 2 };

typedef struct { int16_t top, left, bottom, right; } hud_rect_t; // texel rect (FwSpriteDrawSectionFX src)

typedef struct {
	// FwLineDraw: 1-pixel line, colour COLORREF, blend 2 = additive (every HUD line)
	void (*line)(int x0, int y0, int x1, int y1, uint32_t colorref, int blend);
	// FwTileDraw: filled rectangle (the warning-light fill: alpha blend, opaque colour)
	void (*tile)(int x, int y, int w, int h, uint32_t colorref, int blend);
	// FwSpriteDrawSectionFX from the global texture file: texel rect at (x, y), 1:1 size.
	// colour_on: modulate by colorref (fx flag 1), else white.
	void (*sprite)(int x, int y, int tex, hud_rect_t src, bool colour_on, uint32_t colorref, int blend);
	// Font_Print / Font_GetStringLen (font 0 normal, 1 titles, 2 small)
	void (*text)(const char *s, int x, int y, uint32_t colorref, int font);
	int (*text_width)(const char *s, int font);
	// FUN_00431df0: world point -> screen (s16 x, y, camera z); returns the flags (bit 0 = "draw").
	int (*project)(const int32_t world[3], int *sx, int *sy, int *sz);
	// draw a misc\*.pcp model with a camera-space rotation (4.12) and translation;
	// wire_colorref 0 = solid (lit) draw, else additive wireframe (Pcp_DrawInstanceWire) in that colour;
	// screen_dx: moved that far right on the screen (widescreen: kept beside the bottom-right scales)
	void (*model)(int model, const int16_t rot[3][3], const int32_t pos[3], uint32_t wire_colorref, int screen_dx);
	// object position (Object_GetProperty(id, 0)); false if the object has none
	bool (*object_pos)(uint32_t id, int32_t out[3]);
	// cockpit voice (Sound_PlayChain(0x21F7FFF, n, sids...)) gated by CockVox(priority)
	bool (*cockvox)(int priority);
	void (*voice_chain)(int n, const int *sids);
} hud_draw_t;

extern hud_draw_t hud_host;

enum { HUD_MODEL_ATTITUDE, HUD_MODEL_FRAME, HUD_MODEL_COMPASS };

// weapon descriptor (gWeaponDescs 0x497fd8, 16 bytes)
typedef struct {
	uint8_t kind;   // 2 missiles, 7 flares, other: primary
	uint8_t flags;  // 1 heat bar instead of ammo, 2 automatic fire, 4 homing (lock-on marker)
	uint8_t b2, b3;
	int32_t w4, w8, wc;
} hud_weapon_desc_t;

#define HUD_MAX_MARKERS 20

typedef struct {
	uint32_t id;      // object id or waypoint index, 0xffffffff = free
	uint32_t colour;  // blip colour
	uint32_t flags;   // bit 0 = waypoint index
	int32_t bearing, dist, dy;
	bool has_blip;
	int bx, by;       // blip window position
	uint32_t blip_colour, halo_colour;
	bool halo;
} hud_marker_t;

typedef struct {
	// screen and clock: the frame in 2D units (480 high; wider than 640 on a widescreen display: the
	// windows anchored at the right or bottom move with the edges as in the original at other resolutions)
	int w, h;
	uint32_t now;            // gNow (ms), drives the blinking warning lights
	// live values (the original windows hold pointers to these)
	int32_t shield;          // lander +0xdc, 0..255
	int32_t fuel, max_fuel;  // lander +0xd4 / +0xd8
	int32_t pitch_input;     // gHudPitchInput 0x4933bc (0..63, 31 at rest)
	int32_t roll_input;      // gHudRollInput 0x4933b8
	int32_t vertical_speed;  // gHudVerticalSpeed 0x4a6de0, +-0x800
	int32_t contacts;        // body +0x48c
	// weapon panel (0x577c54 / 0x577c60 / 0x577c5c)
	const hud_weapon_desc_t *weapon;
	int32_t weapon_ammo;
	uint32_t lock_target;
	int language;            // gLanguage 0 en, 1 fr, 2 de
	// waypoint dial
	char wp_label[5];        // gHudWaypointLabel 0x4933c0 "WP  "
	bool multiplayer;
	// sights: lander-local offsets and colours
	int32_t sight_off[3][3];
	uint32_t sight_colour[3];
	int32_t sight_size;
	// player
	int32_t player_pos[3];
	int32_t player_yaw;      // lander +0
	int16_t lander_rot[3][3];  // lander world rotation (lander +0x80)
	int32_t lander_t[3];
	// radar
	hud_marker_t markers[HUD_MAX_MARKERS];
	const int32_t (*waypoints)[2]; // gWaypoints {x, z}
	int num_waypoints;
	int32_t cur_waypoint;      // lander +0x34
	int32_t wp_marker;         // lander +0x36 (-1 = none)
	// derived values computed but not shown by the retail HUD
	int32_t hidden_wp_bearing, hidden_heading, hidden_ground_speed;
} hud_t;

// Lander_CreateHud 0x420770. lander_type 0..4 selects the sight origin (table 0x493428).
void hud_create(hud_t *h, int screen_w, int screen_h, int lander_type, bool multiplayer, int language);

// Per 40 ms tick for the HUD-owning lander, after Ctrl_ReadWord (0x4231ab-0x4233a2).
// word = control word; vel = body velocity (+0x434..+0x43c).
void hud_tick(hud_t *h, uint32_t word, const int32_t vel[3]);

// Per frame (Hud_Draw 0x411d10 + the HUD windows in creation order).
// cam_rot = gViewRot (world -> camera rotation, 4.12, rows = camera axes) for the attitude models.
void hud_draw(hud_t *h, const int16_t cam_rot[3][3]);

int hud_radar_add(hud_t *h, uint32_t id, uint32_t colour, uint32_t flags); // 0x413010
void hud_radar_remove(hud_t *h, int index);                               // 0x4130f0
void hud_radar_remove_all(hud_t *h);                                      // 0x413140

#ifdef HUD_WITH_REWRITE_RENDER
// Fills hud_host.line/tile/sprite with render_quad_2d-based implementations (hud.c); the caller sets
// hud_tex58 to the decoded global texture 58 and provides text, text_width, project and model.
struct texture_t;
extern const struct texture_t *hud_tex58;
void hud_use_rewrite_render(void);
#endif

// colour helper: COLORREF -> 0xAARRGGBB with alpha 0xff
static inline uint32_t hud_colorref_to_argb(uint32_t c) {
	return 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
}

#endif
