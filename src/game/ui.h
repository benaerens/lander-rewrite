// The original's UI system, ported: the window manager core (UI_* 0x4839b0-0x484220), the
// Window / Control layer (WindowCreate 0x4284b0.., Control_Create* 0x427b50..) and the window /
// control drawing (0x45d4b0-0x463c50), plus the compiled-HTML text pages (TextDoc_* 0x45c460..).
// The front end (src/fe) and the mission scripts (mission_api.h Window* exports) use it.
//
// Every UI element is a node of 0x68 bytes in the original; the fields keep their offsets in the
// comments because the drawing / control code reuses +0x54..+0x64 for different things per kind.
// Drawing goes into a layered primitive list like the original's ordering table: layers are drawn
// in ascending order, primitives within a layer last-in first-out (OT_Insert prepends).
#ifndef UI_H
#define UI_H

#include "engine.h"
#include "../texfile.h"

// ---------------------------------------------------------------- the node (0x68 bytes)
typedef int (*UiStateFn)(Window *w, int layer);        // open / update / close: return the next state
typedef void (*UiSelectFn)(Window *w);
typedef void (*UiMouseFn)(Window *w, int inside, int down);

enum { UI_OPENING, UI_OPEN, UI_CLOSING, UI_CLOSED, UI_FREE };

// position modes (flags & 7, FUN_00484030)
enum { UI_POS_CENTRE, UI_POS_ABS, UI_POS_BOTTOM, UI_POS_RIGHT, UI_POS_BOTTOM_RIGHT, UI_POS_PARENT, UI_POS_SCALED };
#define UIF_FOCUSABLE   0x008u
#define UIF_FOCUS_UP    0x010u   // focus request passes to the parent
#define UIF_FOCUS_CHILD 0x020u   // focus request passes to the first child
#define UIF_NAV_LINK    0x040u
#define UIF_STICKY      0x080u   // keeps the mouse capture while the button is held
#define UIF_POOLED      0x100u
#define UIF_DRAWN       0x200u
#define UIF_OPEN_NOW    0x400u   // open callback runs inside UI_OpenWindow
#define UIF_NO_MOUSE    0x800u

struct Window {
	Window *next, *prev, *parent, *child;   // +0x00..+0x0c
	int32_t state;                          // +0x10
	int32_t focused;                        // +0x14
	Window *nav_up, *nav_down;              // +0x18, +0x1c
	Window *nav_left, *nav_right;           // +0x20, +0x24
	uint32_t flags;                         // +0x28
	int32_t x, y, w, h;                     // +0x2c..+0x38
	int32_t depth;                          // +0x3c
	UiStateFn open, update, close;          // +0x40, +0x44, +0x48
	UiSelectFn select;                      // +0x4c
	UiMouseFn mouse;                        // +0x50
	intptr_t u54, u58, u5c, u60, u64;       // +0x54..+0x64: name/title, type, info, aux (per kind)
	bool used;
};

// ---------------------------------------------------------------- UI core
void UI_InitPool(void);                                 // 0x4839b0
Window *UI_AllocWindow(void);                           // 0x4839e0
void UI_OpenWindow(Window *w, Window *parent);          // 0x483a20
void UI_CloseWindow(Window *w);                         // 0x483ae0
void UI_SetFocus(Window *w);                            // 0x483b40
void UI_SetCancel(Window *w);                           // 0x483bd0 (Backspace target)
void UI_UpdateDraw(int layer);                          // 0x483be0 (input, callbacks, drawing)
Window *UI_GetRoot(void);                               // 0x483ed0
void UI_CloseAll(void);                                 // 0x483ee0
bool UI_MouseInside(Window *w);                         // 0x483690
void UI_GetScreenPos(const Window *w, int *x, int *y);  // 0x484030
bool UI_AnyFocused(const Window *w);                    // 0x4841a0
void UI_Flush(void);                                    // FUN_00409f10: finish the closing windows
void ui_draw_pointer(void);                             // UI_DrawPointer 0x484220 (if gUiShowPointer)
void UI_ResetIdle(void);                                // the idle time starts again (FE_Run)
extern int32_t gUiIdleMs;                               // 0x573180 ms since the mouse moved
extern int32_t gUiShowPointer;                          // 0x49a2a8
extern Window *gUiFocus;                                // 0x573174

// UI input: DirectInput codes of the original's UI actions (0x498314..)
enum { UIK_UP = 0xc8, UIK_DOWN = 0xd0, UIK_LEFT = 0xcb, UIK_RIGHT = 0xcd, UIK_SELECT = 0x1c, UIK_BACK = 0x0e,
	UIK_ESCAPE = 0x01, UIK_P = 0x19, UIK_MOUSE0 = 0x40000000, UIK_MOUSE1 = 0x40000001 };
bool ui_key_down(uint32_t code);                        // Input_IsDown
bool ui_key_pressed(uint32_t code);                     // Input_Pressed (until consumed)
void ui_key_consume(uint32_t code);                     // Input_Consume
void ui_input_frame(void);                              // Input_Poll (latch edges)
int ui_mouse_x(void), ui_mouse_y(void);                 // in 2D units (render_set_2d_scale)
// Where the UI's 640x480 screen (gWindowWidth x gWindowHeight) is in a wider frame (2D units; 0, 0 in
// the front end): the windows' positions include it (UI_GetScreenPos), the mission scripts' 2D render
// callback is drawn with it (game.c)
void ui_area_offset(int *ox, int *oy);

// ---------------------------------------------------------------- drawing (OT-style layers)
typedef struct {
	int32_t pad;
	texfile_t *tf;        // +4 texture file
	int32_t tex;          // +8 texture index
	int32_t state;        // +0xc
} UiSprite;             // sprite info {0, texfile, index, 0}

typedef struct { int16_t top, left, bottom, right; } UiRect;   // texel section (the original's order)

void ui_draw_begin(void);
void ui_draw_end(void);                                 // sort + draw everything queued
void ui_sprite(int layer, int x, int y, const UiSprite *s, const UiRect *r, bool blend);
void ui_sprite_tiled_h(int layer, int x, int y, int width, const UiSprite *s, const UiRect *r);  // 0x45e000
void ui_sprite_tiled_v(int layer, int x, int y, int height, const UiSprite *s, const UiRect *r); // 0x45e0f0
void ui_sprite_full(int layer, int x, int y, const UiSprite *s);                                   // FwSpriteDrawFX
void ui_tile(int layer, int x, int y, int w, int h, uint32_t colour, int mode);  // FwTileDraw (colour 0xAABBGGRR)
void ui_line(int layer, int x0, int y0, int x1, int y1, uint32_t colour);        // FwLineDraw (0x00BBGGRR)
void ui_text(int layer, const char *s, int n, int x, int y, uint32_t colour, int font);  // Font_PrintN
void ui_text_clipped(int layer, const char *s, int x, int y, uint32_t colour, int font, int width); // 0x45c630
void ui_picture(int layer, int x, int y, int w, int h, const texture_t *t);       // HTML <img>
void ui_set_transform(const Window *w);                 // Win_SetTransform (NULL = screen)
texture_t *ui_texture(const UiSprite *s);
extern texfile_t *gGlobalTextures;                      // misc\global (slot 0x18)
extern texfile_t *gStandardTextures;                    // misc\standard (slot 3)
extern texfile_t *gEnvironmentTexFile;                  // the world texfile of the mission shown (HTML images)

// ---------------------------------------------------------------- windows (WindowCreate flags)
#define WINF_BORDER   0x001u
#define WINF_VSCROLL  0x002u
#define WINF_HSCROLL  0x004u
#define WINF_BG       0x008u
#define WINF_TITLE    0x010u
#define WINF_TEXT     0x040u
#define WINF_MODEL    0x080u
#define WINF_SPRITE   0x100u
#define WINF_PICTURE  0x200u
#define WINF_HTML     0x400u
#define WINF_SECTION  0x800u

typedef struct {        // pane info, pool FUN_00428270 (0x24 bytes)
	int32_t pad;
	uint32_t flags;     // +4: 2 background, 4 text line, 0x10 sprite, 0x20 picture, 0x40 HTML, 0x80 section
	int32_t w, h;       // +8, +0xc: content size
	int32_t sx, sy;     // +0x10, +0x14: scroll position
	int32_t smx, smy;   // +0x18, +0x1c: scroll range
	void *data;         // +0x20: text / HtmlInfo / UiSprite
} PaneInfo;

typedef struct {                 // BorderInfo 0x583780: {&Border 0x48f5f8, &SpriteInfo_Border}
	const UiRect *rects;         // Border + 4: TL, TR, BL, BR, top, bottom, left, right
	const UiSprite *sprite;
} UiBorder;
// The mission API's BorderInfo export (mission_api.h) is what callers pass to WindowSetBorderInfo;
// the window frame itself is gUiBorder.
extern UiBorder gUiBorder;

void WindowSetTitle(Window *w, const char *title);      // 0x428890
void WindowGetRect(Window *w, WinRect *r);              // 0x428870
void WindowSetModel(Window *w, int32_t model, int32_t scale);   // 0x4288a0
void WindowSetModelFlags(Window *w, int32_t flags);     // 0x4288d0
void WindowSetPicture(Window *w, int32_t tex, texfile_t *tf);   // 0x428920
void WinAPI_DestroyVScroll(Window *w);                  // 0x4287e0

// ---------------------------------------------------------------- controls (node +0x58 = type)
enum {
	CTL_BUTTON = 1, CTL_CHECKBOX = 2, CTL_RADIO = 4, CTL_COMBO = 8, CTL_SLIDER1 = 0x10, CTL_SLIDER2 = 0x20,
	CTL_MENU = 0x80, CTL_TABBAR = 0x100, CTL_BUTTONBAR = 0x200, CTL_LIST = 0x400, CTL_LIST2 = 0x800,
	CTL_COMBO1 = 0x1000, CTL_HUBBUTTON = 0x2000, CTL_EDIT = 0x4000, CTL_SPINNER = 0x8000, CTL_PROGRESS = 0x10000,
	CTL_BUTTON3 = 0x20000, CTL_UPDOWN = 0x40000, CTL_EDIT2 = 0x80000, CTL_TEXTLINE = 0x100000
};

typedef struct {        // list data (0x48f450 / 0x48f470, menu data 0x48e088)
	int32_t first, visible, count, selected, hover, rowh;
	const char *const *strings;
} UiListData;

typedef struct {        // CTL_LIST descriptor (0x583860)
	const char *title;
	UiListData *data;
	Window *items;
} UiListDesc;

typedef struct {        // CTL_LIST2 descriptor (pause menu 0x48f848)
	UiListData *data;
	Window *items;
	void (*on_select)(int32_t index);   // called on activation (param_3[2])
	const char *const *col1;            // optional first column
	int32_t notify;
	int32_t col2x;                      // x of the second column
} UiList2Desc;

typedef struct {        // CTL_MENU (category menus 0x48e1e8): skin = 15 normal + 15 selected 20x20 icons
	const UiSprite *sprite;
	struct UiMenuInfo { const UiRect *skin; UiListData *data; Window *items; } *info;
} UiMenuDesc;

typedef struct {        // button data (hub icons 0x48e618, BUY 0x48f0c0)
	const UiRect *rects;   // [0] on, [1] normal, [2] hover flash (button3: 3 x {L, M, R})
	int32_t state;         // bit0 on, bit1 hover flash
	const char *label;
	int32_t countdown;
} UiButtonData;

typedef struct {        // CTL_BUTTON / CTL_HUBBUTTON descriptor {0, sprite, data}
	int32_t pad;
	const UiSprite *sprite;
	UiButtonData *data;
} UiButtonDesc;

typedef struct {        // CTL_BUTTON3 descriptor (0x48f0d0)
	const UiSprite *sprite;
	UiButtonData *data;
} UiButton3Desc;

typedef struct {        // tab / button bar data (0x48f188..): skin 0x48f100
	const UiRect *skin;
	int32_t extra;         // tab bar: the two extra buttons at the right end
	int32_t current;
	int32_t count;
	int32_t hover;
	const char *const *labels;
	Window *wins[5];
} UiBarData;

typedef struct {        // CTL_TABBAR / CTL_BUTTONBAR descriptor {sprite, data}
	const UiSprite *sprite;
	UiBarData *data;
} UiBarDesc;

typedef struct {        // CTL_TEXTLINE descriptor (0x48f018)
	uint32_t colour;       // title colour (0x00BBGGRR)
	int32_t font;          // text font
	const char *title;     // font 1
	const char *text;
	int32_t state;         // 1 hover (-2 = not focusable)
} UiTextLine;

typedef struct UiSlider {   // CTL_SLIDER2 data (audio 0x48e2f8..)
	const UiRect *skin;    // +0x00: [0x60] left cap, [0x68] right cap, [0x70] knob
	int32_t min, max;      // +0x04, +0x08
	int32_t value;         // +0x0c
	int32_t last;          // +0x10 value at the last on_change
	const char *label;     // +0x14
	void (*on_change)(struct UiSlider *s);   // +0x18
	int32_t flags;         // +0x1c bit0: label right of the bar
} UiSlider;

typedef struct {        // CTL_SLIDER2 descriptor
	const UiSprite *sprite;
	UiSlider *data;
} UiSliderDesc;

typedef struct UiCheck {    // CTL_CHECKBOX data (0x48ebc8..)
	const UiRect *rects;   // [0] off, [1] on
	void (*on_change)(struct UiCheck *c);
	const char *label;
	int32_t value;
} UiCheck;

typedef struct {
	const UiSprite *sprite;
	UiCheck *data;
} UiCheckDesc;

typedef struct UiUpDown {   // CTL_UPDOWN data (mail list scroll arrows 0x48eaf0)
	const UiRect *rects;   // [0] up, [1] down, then pressed variants
	void (*up)(void);
	void (*down)(void);
	Window *b_up, *b_down;
	int16_t pressed;
} UiUpDown;

typedef struct {
	const UiSprite *sprite;
	UiUpDown *data;
} UiUpDownDesc;

typedef struct UiCombo1 {   // CTL_COMBO1 data (the load / save list 0x48eb48): a scrolling list box
	const UiRect *skin;    // 0x48e820: [0..2] row L/M/R, [7..9] selected row, [11] up, [12] down arrow
	void (*on_select)(UiListData *list);   // +4
	UiListData *list;      // +8
	const char *title;     // +0xc
	Window *items, *b_up, *b_down;
} UiCombo1;

typedef struct {
	const UiSprite *sprite;
	UiCombo1 *data;
} UiCombo1Desc;

typedef struct UiSpinner {  // CTL_SPINNER data (the control configuration 0x48ed98)
	const UiRect *skin;         // +0x00: [0] left arrow, [1] right arrow, [2] / [3] pressed
	int32_t min, max, value;    // +0x04, +0x08, +0x0c
	const char *label;          // +0x10
	const char *const *strings; // +0x14 names of the values (layouts 3 and 4)
	Window *b_left, *b_right;   // +0x18, +0x1c the arrows' hit windows
	int16_t pressed;            // +0x20 arrow under the mouse (1 left, 2 right)
	uint8_t flags;              // +0x22 bits 0-2 layout, 0x10 name right-aligned, 0x80 label in font 1 (else 2)
} UiSpinner;

typedef struct {
	const UiSprite *sprite;
	UiSpinner *data;
} UiSpinnerDesc;

typedef struct UiEdit2 {    // CTL_EDIT2 data (the save-name editor 0x49b1a0): edits text in place
	int (*handler)(struct UiEdit2 *e);     // typing; returns 1 accept (Return), 2 cancel (Esc), 0
	int16_t max, len;
	char *text;
} UiEdit2;

Window *Control_Create(uint32_t type, Window *parent, void *desc);  // 0x427b50
Window *Control_CreateComboBox1(Window *parent, UiCombo1Desc *d);    // 0x427d30
Window *Control_CreateEdit2(Window *parent, UiEdit2 *d);             // 0x427f10
Window *Control_CreateSpinner(Window *parent, UiSpinnerDesc *d);     // 0x427f50
Window *Control_CreateButton(Window *parent, UiButtonDesc *d);       // 0x427c10
Window *Control_CreateButton2(Window *parent, UiButtonDesc *d);      // 0x427c40 (hub icon)
Window *Control_CreateButton3(Window *parent, UiButton3Desc *d);     // 0x427c70
Window *Control_CreateMenu(Window *parent, UiMenuDesc *d);           // 0x427d60
Window *Control_CreateSlider2(Window *parent, UiSliderDesc *d);      // 0x427de0
Window *Control_CreateCheckBox(Window *parent, UiCheckDesc *d);      // 0x427e40
Window *Control_CreateTabBar(Window *parent, UiBarDesc *d);          // 0x427f90
Window *Control_CreateButtonBar(Window *parent, UiBarDesc *d);       // 0x427fe0
Window *Control_CreateList(Window *parent, UiListDesc *d);           // 0x428010
Window *Control_CreateList2(Window *parent, UiList2Desc *d);         // 0x428030
Window *Control_CreateUpDown(Window *parent, UiUpDownDesc *d);       // 0x428080
Window *Control_CreateTextLine(Window *parent, UiTextLine *d);       // 0x4280b0
void Control_Destroy(Window *c);                                     // 0x427c00
void Control_SetSize(Window *c, int x, int y, int w, int h);         // 0x4280d0
void Control_SetRect(Window *c, const WinRect *r);                   // 0x428100
void Control_GetRect(Window *c, WinRect *r);                         // 0x4281e0

// ---------------------------------------------------------------- text files (gTextFile)
void ui_reset(void);
bool ui_load_text(const char *path);
void ui_load_mission_text(const char *mission_dir, bool ingame);
void ui_free_text(void);
void ui_draw(void);                                     // in-game: UI_UpdateDraw on the HUD layers

// model windows (WINF_MODEL): drawn by the front end after the UI (RenderList_Run(gOT+0x50))
void ui_draw_models(void);
extern ppm_t *gUiModels;                                // misc\objects.ppm while the front end runs

#endif
