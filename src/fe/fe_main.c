// The front end's frame loop and structure: FE_Run 0x409fc0, FE_Enter 0x408a30, the control event
// handler FE_OnControlEvent 0x409420, the hub (FE_BuildHub 0x406850), category menus
// (FE_OpenCategory 0x4084d0), screens (FE_OpenScreen 0x408780, FE_GetScreenDesc 0x406ae0), the pane
// (0x40a370..), messages (0x407200..), the cash line (0x4070c0) and the animated logos (0x406a40).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fe.h"
#include "../game/font.h"
#include "../game/sound.h"
#include "../game/media.h"
#include "../platform.h"
#include "../fs.h"

int32_t gCurWorld, gCurMission, gFESelWorld, gFESelMission;
int32_t gLastMissionResult = -1, gFrontEndEntry;
uint32_t gSoftFruit;
int32_t gPlayerCashAtLaunch;
LanderPlayerState gPlayerStateBackup;

FeScreenDesc gFEScreenDesc;
int32_t gFECurScreen = -1, gFECurCategory = -1, gFEExitFlag, gFEResult;
int32_t gFEListMode;          // DAT_0049b280
int32_t gFENewMailMenu = 1;   // DAT_0048f9e4
int32_t gFEGameSaved = 1;     // DAT_0048f9e0
static int32_t fe_hub_layout; // DAT_0049b158
static int32_t fe_demo_armed; // DAT_0049b294
int32_t gDemoTimeoutMs = 90000;  // 0x48f9f8 ("Demo Timeout")
static int32_t fe_logos;      // DAT_0049b29c
static bool fe_closing, fe_demo;

Window *fe_root;              // DAT_00578414
Window *fe_pane;              // DAT_0049b2b8
Window *fe_html;              // DAT_0049b2bc
static Window *fe_menuwin[2]; // DAT_0049b2b0 / b4
static Window *fe_menu[2];    // DAT_0049b04c / 050
static Window *fe_title_model;// DAT_0049b2c0
Window *fe_tabbar;            // DAT_0049b118
Window *fe_buttonbar;         // DAT_0049b11c
Window *fe_list;              // DAT_0049b120
static Window *fe_cashline;   // DAT_0049b0c8
static Window *fe_message;    // DAT_0049b244
static Window *fe_logo[2];    // DAT_0049b14c / 150
static Window *hub_new, *hub_continue, *hub_net, *hub_setup, *hub_game, *hub_mail, *hub_system; // 0x49b130..148

UiSprite fe_sprite6 = {0, NULL, 6, 0};        // 0x48f068 (texture 6 of misc\global: the widget art)
const UiRect fe_bar_skin[16] = {              // 0x48f100: tab / button bar pieces
	{0, 0, 40, 72}, {0, 96, 16, 120}, {20, 96, 36, 120}, {0, 72, 16, 96}, {16, 72, 32, 96},
	{92, 100, 106, 116}, {12, 40, 24, 56},
	{78, 100, 90, 107}, {78, 108, 90, 116}, {78, 117, 90, 124},
	{64, 100, 76, 107}, {64, 108, 76, 116}, {64, 117, 76, 124},
	{50, 100, 62, 107}, {50, 108, 62, 116}, {50, 117, 62, 124},
};
const char *fe_screen_titles[16];             // DAT_0049ab10 (Text 0..15)
static const char *fe_category_titles[6];     // DAT_0049acf8 (Text 0x10..0x15)

// ================================================================ sounds, numbers

void fe_sound(int sid) { Sound_PlayOnce(OBJ_NONE, sid); } // FUN_0047bdd0(-1, sid)

void fe_format_cash(char *out, int32_t v) { // FUN_00404b70: appends v with thousands separators
	char *p = out + strlen(out);
	bool big = false, mid = false;
	if (v < 0) {
		*p++ = '-';
		*p = 0;
		v = -v;
	}
	if (v > 999999999) {
		sprintf(p, "%d,", v / 1000000000);
		v %= 1000000000;
		p += strlen(p);
		big = true;
	}
	if (v > 999999 || big) {
		sprintf(p, big ? "%03d," : "%d,", v / 1000000);
		v %= 1000000;
		p += strlen(p);
		mid = true;
	}
	if (v < 1000 && !mid) {
		sprintf(p, "%d", v);
		return;
	}
	sprintf(p, mid ? "%03d," : "%d,", v / 1000);
	p += strlen(p);
	sprintf(p, "%03d", v % 1000);
}

// ================================================================ pane, html window, messages

static const WinRect pane_rect = {190, 105, 399, 303};      // 0x48fc98

static void fe_destroy_title_model(void) { // FUN_0040a500
	if (fe_title_model) {
		WindowDestroy(fe_title_model);
		fe_title_model = NULL;
	}
}

void FE_SetPaneRect(const WinRect *r) { // 0x40a370
	WindowSetRect(fe_pane, r);
	rectHTML.x = r->x + 0x1a;
	rectHTML.y = r->y + 0x27;
	rectHTML.w = r->w - 0x34;
	rectHTML.h = r->h - 0x4e;
}

void FE_CreatePane(void) { // 0x40a3c0
	if (!fe_pane) {
		fe_pane = WindowCreate(0x19, fe_root);
		if (fe_pane) {
			FE_SetPaneRect(&pane_rect);
			WindowSetBorderInfo(fe_pane, &BorderInfo);
			Window_CreateTransform(fe_pane);
		}
		fe_destroy_title_model();
	}
}

static void fe_destroy_pane(void) { // FUN_0040a420
	if (fe_pane) {
		WindowDestroy(fe_pane);
		fe_pane = NULL;
	}
}

void FE_CreateHtmlWindow(void) { // 0x40a440
	if (!fe_html) {
		fe_html = WindowCreate(0x402, fe_pane);
		if (fe_html) WindowSetRect(fe_html, &rectHTML);
	}
}

void FE_DestroyHtmlWindow(void) { // 0x40a480
	if (fe_html) {
		WindowDestroy(fe_html);
		fe_html = NULL;
	}
}

static char fe_msg1[256], fe_msg2[256];                     // 0x49a830 / 0x49a338
static UiTextLine fe_msg_desc = {0x00008000, 1, NULL, fe_msg1, -2};  // 0x48f7c8 (colour DAT_0048f800)
static const WinRect fe_msg_rect = {224, 452, 0, 0}, fe_msg_rect2 = {224, 446, 0, 0};

void FE_HideMessage(void) { // 0x407350
	if (fe_message) {
		Control_Destroy(fe_message);
		fe_message = NULL;
	}
}

void FE_ShowMessage(const char *s) { // 0x407200
	FE_HideMessage();
	fe_msg_desc.title = NULL;
	fe_msg_desc.text = fe_msg1;
	fe_message = Control_CreateTextLine(fe_root, &fe_msg_desc);
	if (fe_message) {
		fe_msg_desc.colour = 0x00008000;
		Control_SetRect(fe_message, &fe_msg_rect);
		snprintf(fe_msg1, sizeof(fe_msg1), "%s", s);
	}
}

void FE_ShowMessage2(const char *s) { // 0x4072c0: a second line below; the first moves up
	if (!fe_message) return;
	fe_msg_desc.colour = 0x00008000;
	Control_SetRect(fe_message, &fe_msg_rect2);
	snprintf(fe_msg2, sizeof(fe_msg2), "%s", s);
	fe_msg_desc.title = fe_msg1;
	fe_msg_desc.text = fe_msg2;
}

// ---------------------------------------------------------------- the cash line (0x4070c0, FUN_00407080)
static char fe_cash_text[32] = "$200000000";               // 0x48f948
static UiTextLine fe_cash_desc = {0, 1, NULL, fe_cash_text, -2};   // 0x48f018
static const WinRect fe_cash_rect = {266, 74, 0, 0};        // 0x48f030
static UiStateFn fe_cash_draw_orig;

static int fe_cash_draw(Window *w, int layer) { // FUN_00407080: "$" + the player's cash, every frame
	snprintf(fe_cash_text, sizeof(fe_cash_text), "$");
	fe_format_cash(fe_cash_text, gPlayerState.cash);
	return fe_cash_draw_orig(w, layer);
}

static void fe_create_cash_line(void) { // FE_CreateCashLine 0x4070c0
	if (!fe_cashline) {
		fe_cashline = Control_CreateTextLine(fe_root, &fe_cash_desc);
		if (fe_cashline) {
			fe_cash_draw_orig = fe_cashline->update;
			fe_cashline->update = fe_cash_draw;
			fe_cash_desc.colour = 0x00b3ad00;              // DAT_004992a8
			Control_SetRect(fe_cashline, &fe_cash_rect);
		}
	}
}

static void fe_destroy_cash_line(void) { // FUN_00407060
	if (fe_cashline) {
		Control_Destroy(fe_cashline);
		fe_cashline = NULL;
	}
}

// ---------------------------------------------------------------- the animated logos (FUN_004088f0, FUN_00406a40)
static const UiRect logo_rects[3] = {{0, 0, 42, 128}, {0, 0, 42, 128}, {0, 0, 42, 128}};   // 0x48f558
static const UiRect badge_rects[3] = {{0, 0, 64, 64}, {0, 0, 64, 64}, {0, 0, 64, 64}};     // 0x48f5b0
static UiSprite logo_sprite = {0, NULL, 0x18, 0}, badge_sprite = {0, NULL, 0x4b, 0};      // 0x48f548 / 0x48f5a0
static UiButtonData logo_data = {logo_rects, 0, "", 0}, badge_data = {badge_rects, 0, "", 0};
static UiButtonDesc logo_desc = {0, &logo_sprite, &logo_data}, badge_desc = {0, &badge_sprite, &badge_data};

static void fe_logos_create(void) { // FUN_004088f0 + FUN_00406a40: animated LANDER logo (tex 24) and badge (tex 75)
	if (fe_logos) return;
	logo_sprite.tf = badge_sprite.tf = gGlobalTextures;
	logo_sprite.tex = 0x18;     // 0x44 on the DVD edition (DAT_00563c50)
	badge_sprite.tex = 0x4b;    // 0x4c (AMD K6) with 3DNow! (DAT_00536348)
	static const WinRect logo_rect = {40, 30, 0, 0}, badge_rect = {554, 22, 0, 0};   // 0x48f590 / 0x48f5e8
	if (!fe_logo[0] && (fe_logo[0] = Control_CreateButton(fe_pane, &logo_desc))) Control_SetRect(fe_logo[0], &logo_rect);
	if (!fe_logo[1] && (fe_logo[1] = Control_CreateButton(fe_pane, &badge_desc))) Control_SetRect(fe_logo[1], &badge_rect);
	fe_logos = 1;
}

static void fe_logos_destroy(void) { // FUN_004089b0 (+ FUN_004088b0)
	if (!fe_logos) return;
	for (int i = 0; i < 2; i++) {
		if (fe_logo[i]) Control_Destroy(fe_logo[i]);
		fe_logo[i] = NULL;
	}
	fe_logos = 0;
}

// ================================================================ tab bar, button bar, list

static UiBarDesc fe_tab_desc = {&fe_sprite6, NULL};        // 0x48f180
static UiBarDesc fe_bb_desc = {&fe_sprite6, NULL};         // 0x48f368

void FE_CreateTabBar(UiBarData *tabs) { // 0x402490
	fe_tab_desc.data = tabs;
	if (!fe_tabbar) {
		if (!fe_pane) FE_CreatePane();
		memset(tabs->wins, 0, sizeof(tabs->wins));
		fe_tabbar = Control_CreateTabBar(fe_pane, &fe_tab_desc);
	}
}

void FE_DestroyTabBar(void) { // FUN_00402500
	if (fe_tabbar) {
		Control_Destroy(fe_tabbar);
		fe_tabbar = NULL;
	}
}

void FE_CreateButtonBar(UiBarData *b) { // 0x402520
	static const WinRect r1 = {213, 356, 132, 16}, r2 = {213, 340, 132, 32};   // 0x48f3d0 / 0x48f3e0
	fe_bb_desc.data = b;
	if (!fe_buttonbar) {
		fe_buttonbar = Control_CreateButtonBar(fe_pane, &fe_bb_desc);
		if (fe_buttonbar) Control_SetRect(fe_buttonbar, b->count > 2 ? &r2 : &r1);
	}
}

void FE_DestroyButtonBar(void) { // FUN_00402590
	if (fe_buttonbar) {
		memset(((UiBarDesc *)fe_buttonbar->u5c)->data->wins, 0, sizeof(((UiBarDesc *)fe_buttonbar->u5c)->data->wins));
		Control_Destroy(fe_buttonbar);
		fe_buttonbar = NULL;
	}
}

static Window *fe_updown;      // DAT_0049b088

static void fe_destroy_updown(void) { // FUN_004025e0
	if (fe_updown) {
		Control_Destroy(fe_updown);
		fe_updown = NULL;
	}
}

void FE_DestroyList(void) { // FUN_00402620
	if (fe_list) {
		Control_Destroy(fe_list);
		fe_list = NULL;
	}
	fe_destroy_updown();
}

Window *fe_mail_updown(UiUpDownDesc *d, const WinRect *r, bool create) { // FUN_00404250 helper
	if (!create) {
		fe_destroy_updown();
		return NULL;
	}
	if (!fe_updown && fe_list) {
		fe_updown = Control_CreateUpDown(fe_list, d);
		if (fe_updown) Control_SetRect(fe_updown, r);
	}
	return fe_updown;
}

// ================================================================ hub (FE_BuildHub 0x406850)

static UiSprite hub_sprite = {0, NULL, 0x12, 0};            // 0x48e5f0: texture 18 of misc\global
#define R(t, l, b, r) {t, l, b, r}
static const UiRect hub_rects[7][3] = {                     // [0] on, [1] normal, [2] hover (0x48e600..)
	{R(64, 80, 96, 120), R(0, 0, 32, 40), R(32, 40, 64, 80)},       // new game
	{R(64, 120, 96, 160), R(0, 40, 32, 80), R(32, 80, 64, 120)},    // continue
	{R(64, 160, 96, 200), R(0, 80, 32, 120), R(32, 120, 64, 160)},  // multiplayer
	{R(64, 200, 96, 240), R(0, 120, 32, 160), R(32, 160, 64, 200)}, // setup
	{R(96, 0, 128, 40), R(0, 160, 32, 200), R(32, 200, 64, 240)},   // game
	{R(96, 40, 128, 80), R(0, 200, 32, 240), R(64, 0, 96, 40)},     // mail
	{R(96, 80, 128, 120), R(32, 0, 64, 40), R(64, 40, 96, 80)},     // system
};
#undef R
static UiButtonData hub_data[7];
static UiButtonDesc hub_desc[7];
static const WinRect hub_pos[7] = {                         // 0x48e718..0x48e778 (no network game)
	{264, 30, 40, 32}, {328, 30, 40, 32}, {360, 30, 40, 32}, {392, 30, 40, 32}, {456, 30, 40, 32},
	{264, 30, 40, 32}, {328, 30, 40, 32},
};

static Window *hub_button(Window *root, int i) { // FUN_00406490
	hub_sprite.tf = gGlobalTextures;
	hub_data[i] = (UiButtonData){hub_rects[i], 0, "", 0};
	hub_desc[i] = (UiButtonDesc){0, &hub_sprite, &hub_data[i]};
	Window *b = Control_CreateButton2(root, &hub_desc[i]);
	if (b) Control_SetRect(b, &hub_pos[i]);
	return b;
}

void FE_BuildHub(Window *root, int layout) { // 0x406850
	switch (layout) {
		case 1:   // title (FUN_004064c0; "Enable Network Game" off: no multiplayer icon)
			hub_new = hub_button(root, 0);
			hub_continue = hub_button(root, 1);
			hub_setup = hub_button(root, 3);
			hub_game = hub_button(root, 4);
			fe_destroy_cash_line();
			break;
		case 2:   // campaign (FUN_004066f0)
			hub_mail = hub_button(root, 5);
			hub_system = hub_button(fe_root, 6);
			hub_setup = hub_button(root, 3);
			hub_game = hub_button(root, 4);
			fe_create_cash_line();
			break;
		case 4:   // FUN_004065c0
			hub_game = hub_button(root, 4);
			break;
		default:
			break;
	}
	fe_hub_layout = layout;
}

void FE_DestroyHub(void) { // 0x406790
	Window **all[7] = {&hub_new, &hub_continue, &hub_net, &hub_setup, &hub_game, &hub_mail, &hub_system};
	for (int i = 0; i < 7; i++) {
		if (*all[i]) Control_Destroy(*all[i]);
		*all[i] = NULL;
	}
	fe_hub_layout = 0;
}

void FE_HubHighlight(Window *w) { // 0x4068f0
	Window *all[7] = {hub_new, hub_continue, hub_net, hub_setup, hub_game, hub_mail, hub_system};
	for (int i = 0; i < 7; i++) {
		if (all[i]) ((UiButtonDesc *)all[i]->u5c)->data->state = all[i] == w ? 1 : 0;
	}
}

void FE_SetHub(Window *root, int layout, Window *highlight) { // 0x406980
	if (layout != fe_hub_layout) {
		FE_DestroyHub();
		fe_hub_layout = layout;
		FE_BuildHub(root, layout);
	}
	FE_HubHighlight(highlight == (Window *)-1 ? hub_mail : highlight);
}

// ================================================================ categories (FE_OpenCategory 0x4084d0)

static UiSprite menu_sprite = {0, NULL, 7, 0};              // 0x48e1d8: texture 7 (the menu icons)
#define R(t, l, b, r) {t, l, b, r}
static const UiRect menu_skin[30] = {                       // 0x48e0c8: per screen, normal then selected
	R(0, 0, 20, 20), R(0, 20, 20, 40), R(0, 40, 20, 60), R(0, 60, 20, 80), R(20, 60, 40, 80), R(40, 60, 60, 80),
	R(20, 0, 40, 20), R(20, 20, 40, 40), R(40, 0, 60, 20), R(80, 0, 100, 20), R(40, 20, 60, 40), R(60, 20, 80, 40),
	R(80, 20, 100, 40), R(0, 100, 20, 120), R(20, 100, 40, 120), R(40, 100, 60, 120),
	R(20, 0, 40, 20) /* unused */, R(20, 20, 40, 40), R(40, 20, 60, 40), R(40, 40, 60, 60), R(60, 40, 80, 60),
	R(100, 40, 120, 60), R(100, 80, 120, 100), R(60, 80, 80, 100), R(80, 100, 100, 120), R(100, 0, 120, 20),
	R(60, 0, 80, 20), R(60, 60, 80, 80), R(80, 60, 100, 80), R(80, 40, 100, 60),
};
#undef R
static UiListData menu_data[2] = {                          // 0x48e088 / 0x48e0a8
	{0, 0, 16, -1, -1, 0, fe_screen_titles}, {0, 0, 16, -1, -1, 0, fe_screen_titles},
};
static struct UiMenuInfo menu_info[2] = {{menu_skin, &menu_data[0], NULL}, {menu_skin, &menu_data[1], NULL}};
static UiMenuDesc menu_desc[2] = {{&menu_sprite, &menu_info[0]}, {&menu_sprite, &menu_info[1]}};

typedef struct { int first, count, selected; const char *title[2]; WinRect win[2], menu[2]; } CategoryDesc;

static bool fe_category_desc(int cat, CategoryDesc *d) { // FE_GetCategoryDesc 0x406ef0
	static const WinRect win_a = {29, 107, 143, 104}, win_game = {29, 107, 143, 130}, win_b = {29, 240, 143, 104};
	static const WinRect menu_a = {37, 135, 127, 100}, menu_game = {37, 135, 127, 130}, menu_b = {37, 268, 127, 100};
	memset(d, 0, sizeof(*d));
	switch (cat) {
		case 1: *d = (CategoryDesc){0, 3, -1, {fe_category_titles[0], NULL}, {win_a}, {menu_a}}; break;
		case 2: *d = (CategoryDesc){4, 4, -1, {fe_category_titles[1], NULL}, {win_game}, {menu_game}}; break;
		case 3: *d = (CategoryDesc){9, 3, 9, {fe_category_titles[2], fe_category_titles[3]}, {win_a, win_b}, {menu_a, menu_b}}; break;
		case 4: *d = (CategoryDesc){0xc, 3, -1, {fe_category_titles[4], NULL}, {win_a}, {menu_a}}; break;
		case 5: *d = (CategoryDesc){8, 1, 8, {fe_category_titles[5], NULL}, {win_a}, {menu_a}}; break;
		default: d->first = d->count = d->selected = -1; return false;
	}
	return true;
}

static Window *fe_menu_window(const WinRect *r) { // FE_CreateMenuWindow 0x40a2e0
	Window *w = WindowCreate(0x19, fe_root);
	if (w) {
		WindowSetRect(w, r);
		WindowSetBorderInfo(w, &BorderInfo);
		Window_CreateTransform(w);
		fe_destroy_title_model();
	}
	return w;
}

static void fe_close_menu(int i) { // FUN_00402470
	if (fe_menu[i]) {
		UI_CloseWindow(fe_menu[i]);
		fe_menu[i] = NULL;
	}
}

static void fe_close_menu_window(int i) { // FUN_0040a330 / FUN_0040a350
	if (fe_menuwin[i]) {
		WindowDestroy(fe_menuwin[i]);
		fe_menuwin[i] = NULL;
	}
}

void FE_CloseCategory(void) { // 0x408460
	fe_close_menu_window(0);
	fe_close_menu_window(1);
	fe_close_menu(0);
	fe_close_menu(1);
	gFECurCategory = 0;
}

int FE_OpenCategory(int cat) { // 0x4084d0
	if (cat > 5 || cat == gFECurCategory) return 0;
	gFECurCategory = cat;
	fe_close_menu_window(0);
	fe_close_menu_window(1);
	CategoryDesc d;
	bool has = fe_category_desc(cat, &d);
	fe_close_menu(0);
	if (has && d.win[0].w) {
		menu_data[0].first = d.first;
		menu_data[0].visible = d.count;
		menu_data[0].selected = d.selected;
		fe_menuwin[0] = fe_menu_window(&d.win[0]);
		if (fe_menuwin[0]) {
			WinRect r = {d.menu[0].x + 2, d.menu[0].y, d.menu[0].w - 2, d.menu[0].h};
			fe_menu[0] = Control_CreateMenu(fe_menuwin[0], &menu_desc[0]);
			if (fe_menu[0]) Control_SetRect(fe_menu[0], &r);
		}
	}
	fe_close_menu(1);
	if (has && d.win[1].w) {
		menu_data[1].first = d.first;
		menu_data[1].visible = d.count;
		menu_data[1].selected = -1;
		fe_menuwin[1] = fe_menu_window(&d.win[1]);
		if (fe_menuwin[1]) {
			WinRect r = {d.menu[1].x + 2, d.menu[1].y, d.menu[1].w - 2, d.menu[1].h};
			fe_menu[1] = Control_CreateMenu(fe_menuwin[1], &menu_desc[1]);
			if (fe_menu[1]) Control_SetRect(fe_menu[1], &r);
		}
	}
	if (has && d.title[0]) {
		WindowSetTitle(fe_menuwin[0], d.title[0]);
		if (d.title[1]) WindowSetTitle(fe_menuwin[1], d.title[1]);
	}
	return 1;
}

static void fe_deselect_menu(int i) { // FUN_00401790 / FUN_004017b0
	menu_data[i].selected = -1;
}

// ================================================================ screens (FE_GetScreenDesc 0x406ae0)

static void fe_get_screen_desc(FeScreenDesc *d, int screen) {
	memset(d, 0, sizeof(*d));
	if (screen == -1) screen = -2;
	if (screen == -2) screen = menu_data[0].selected;
	switch (screen) {
		case 0:
			d->tabs = &fe_audio_tabs;
			d->tab_create = Audio_TabCreate;
			d->tab_destroy = Audio_TabDestroy;
			break;
		case 1:
			d->tabs = &fe_visual_tabs;
			d->tab_create = Visual_TabCreate;
			d->tab_destroy = Visual_TabDestroy;
			break;
		case 2:
			d->enter = Controls_Enter;
			d->exit = Controls_Exit;
			d->tabs = &fe_controls_tabs;
			if (fe_controls_tabs.current == 1) {     // ADVANCED (FUN_00403ca0 / FUN_00403cb0)
				d->tab_create = Advanced_TabCreate;
				d->tab_destroy = Advanced_TabDestroy;
			} else {                                 // CONFIGURE (FUN_00403900 / FUN_00403910)
				d->tab_create = Controls_TabCreate;
				d->tab_destroy = Controls_TabDestroy;
			}
			break;
		case 4:
			d->enter = Load_Enter;
			d->exit = Load_Exit;
			d->tabs = &fe_empty_tabs;
			d->buttons = &fe_load_buttons;
			d->button_action = Load_Action;
			break;
		case 5:
			d->enter = Restart_Enter;
			d->exit = Confirm_Exit;
			d->tabs = &fe_empty_tabs;
			d->buttons = &fe_ok_tab;
			d->button_action = Restart_Action;
			break;
		case 6:
			d->enter = Quit_Enter;
			d->exit = Confirm_Exit;
			d->tabs = &fe_empty_tabs;
			d->buttons = &fe_ok_tab;
			d->button_action = Quit_Action;
			break;
		case 7:
			d->enter = Credits_Enter;
			d->exit = FE_DestroyHtmlWindow;
			break;
		case 8:
			d->enter = Debrief_Enter;
			d->exit = Debrief_Exit;
			d->tabs = &fe_ok_tab;
			break;
		case 9:
			d->enter = Mail_ScreenEnter;
			d->exit = Mail_ScreenExit;
			d->tabs = &fe_mail_tabs;
			d->tab_create = Mail_ScreenCreate;
			d->tab_destroy = Mail_ScreenDestroy;
			break;
		case 10:
			d->enter = Mail_ScreenEnter;
			d->exit = Mail_ScreenExit;
			d->tabs = &fe_mail_tabs;
			d->tab_create = Mail_NewsCreate;
			d->tab_destroy = Mail_OtherDestroy;
			break;
		case 11:
			d->enter = Mail_ScreenEnter;
			d->exit = Mail_ScreenExit;
			d->tabs = &fe_mail_tabs;
			d->tab_create = Mail_PersonalCreate;
			d->tab_destroy = Mail_OtherDestroy;
			break;
		case 12:
			d->tabs = &fe_buy_tabs;
			d->buttons = &fe_buy_buttons;
			d->tab_create = Shop_TabCreate;
			d->tab_destroy = Shop_TabDestroy;
			d->button_action = Shop_ButtonAction;
			break;
		case 13:
			d->enter = Sell_Enter;
			d->exit = Sell_Exit;
			d->tabs = &fe_empty_tabs;
			break;
		case 14:
			d->tabs = &fe_empty_tabs;
			d->tab_create = Diag_Create;
			d->tab_destroy = Diag_Destroy;
			break;
		case 15:
			d->enter = Save_Enter;
			d->exit = Save_Exit;
			d->tabs = &fe_empty_tabs;
			d->buttons = &fe_save_buttons;
			d->button_action = Save_Action;
			break;
		default:
			break;
	}
	if (screen >= 0 && screen < 16) d->title = fe_screen_titles[screen];
}

static void fe_flush(void) { UI_Flush(); } // FUN_00409f10: let closing windows finish

static void fe_exit_screen(FeScreenDesc *d) { // FUN_00408410
	if (d->exit) d->exit();
	if (d->tabs) {
		if (d->tab_destroy) d->tab_destroy();
		FE_DestroyTabBar();
	}
	if (d->buttons) FE_DestroyButtonBar();
	FE_DestroyList();
	fe_flush();
}

void FE_CloseScreen(void) { // 0x408710
	if (gFEScreenDesc.tab_destroy) {
		gFEScreenDesc.tab_destroy();
		gFEScreenDesc.tab_destroy = NULL;
	}
	if (gFEScreenDesc.exit) {
		gFEScreenDesc.exit();
		gFEScreenDesc.exit = NULL;
	}
	if (gFEScreenDesc.buttons) FE_DestroyButtonBar();
	gFECurScreen = -1;
	FE_DestroyTabBar();
	fe_destroy_pane();
	fe_flush();
	WindowSetTitle(fe_pane, NULL);
}

void FE_OpenScreen(int screen) { // 0x408780
	if (screen >= 16 || screen == gFECurScreen) return;
	if (screen != -2) {
		gFECurScreen = screen;
		fe_exit_screen(&gFEScreenDesc);
	}
	if (screen == 12 || screen == 7) FE_LoadMissionTextPack(-1, -1);
	FE_CreatePane();
	fe_get_screen_desc(&gFEScreenDesc, screen);
	WindowSetTitle(fe_pane, gFEScreenDesc.title);
	if (gFEScreenDesc.enter) gFEScreenDesc.enter();
	if (gFEScreenDesc.tabs) {
		FE_CreateTabBar(gFEScreenDesc.tabs);
		if (gFEScreenDesc.tab_create) gFEScreenDesc.tab_create();
	}
	if (gFEScreenDesc.buttons) FE_CreateButtonBar(gFEScreenDesc.buttons);
}

// ================================================================ flow: enter, launch, de-brief, restart

static const WinRect wide_pane = {51, 105, 538, 303};      // 0x48e078

void FE_ArmDemoTimeout(void) { // 0x408a20: the title model window (FUN_0040a4a0) and the attract demo
	if (!fe_title_model) {
		static const WinRect r = {20, 100, 600, 300};      // 0x48fca8
		fe_title_model = WindowCreate(0x80, NULL);
		WindowSetRect(fe_title_model, &r);
		WindowSetModel(fe_title_model, 0x16, 0xc00);
		WindowSetModelFlags(fe_title_model, 6);
	}
	// "Demo Timeout" (gDemoTimeoutMs) without mouse movement -> FE result 6, the attract demo; a hub
	// icon disarms it
	fe_demo_armed = 1;
}

void FE_Enter(int entry, int last) { // 0x408a30
	int layout;
	fe_logos_create();
	fe_hub_layout = 0;
	if (entry != FE_ENTRY_AFTER_MISSION && !gFEGameSaved && last > 0) {
		FE_CreatePane();
		FE_OpenScreen(15);
		if (gFECurCategory == 3) gFECurCategory = 0;
		gFEGameSaved = 1;
		return;
	}
	if (entry == FE_ENTRY_FRESH) {
		FE_ArmDemoTimeout();
		gFEGameSaved = 1;
		layout = 1;
	} else if (entry == FE_ENTRY_LOADED) {
		entry = 3;
		layout = 2;
	} else {
		layout = entry;
		if (entry == FE_ENTRY_AFTER_MISSION) {
			gFEGameSaved = last < 1;
			entry = 5;
			layout = 2;
		}
	}
	if (entry == 3) {
		Mail_Open();
		FE_BuildHub(fe_root, layout);
		FE_HubHighlight(hub_mail);
		return;
	}
	if (entry != 5) {
		FE_OpenCategory(entry);
		FE_BuildHub(fe_root, layout);
		FE_HubHighlight(NULL);
		fe_get_screen_desc(&gFEScreenDesc, -2);
		return;
	}
	FE_CreatePane();
	FE_SetPaneRect(&wide_pane);
	FE_OpenScreen(8);
}

void FE_Rebuild(int entry) { // 0x408c20
	FE_CloseScreen();
	FE_DestroyHub();
	fe_logos_destroy();
	FE_Enter(entry, 0);
}

void FE_LaunchMission(void) { // 0x409b20
	gFEExitFlag = 1;
	gFEResult = FE_RESULT_PLAY;
	gCurWorld = gFESelWorld;
	gCurMission = gFESelMission;
}

void FE_Quit(void) { // 0x40a2a0
	gFEExitFlag = 1;
	gFEResult = FE_RESULT_QUIT;
}

void FE_RestartGame(void) { // 0x4062c0
	FE_CloseCategory();
	FE_CloseScreen();
	FE_DestroyHub();
	gMultiPlayerGame = 0;
	FE_BuildHub(fe_root, 1);
	FE_HubHighlight(NULL);
	gMailState = 0;
	Player_NewGame();
	FE_ArmDemoTimeout();
}

void FE_DebriefDone(void) { // 0x409e50
	if (gCurWorld == 0x13 && G.mission_result != 0) {   // Pluto won: the game is complete
		FE_RestartGame();
		return;
	}
	FE_DestroyHub();
	FE_CloseCategory();
	FE_CloseScreen();
	if (!gFEGameSaved) {
		FE_OpenScreen(15);
		if (gFECurCategory != 0) FE_OpenCategory(0);
	} else {
		Mail_Open();
		FE_BuildHub(fe_root, 2);
		FE_HubHighlight(hub_mail);
	}
}

// ================================================================ control events (FE_OnControlEvent 0x409420)

static void fe_on_control(const LanderEvent *ev) {
	Window *c = (Window *)ev->arg[0];
	int value = (int)ev->arg[1], state = (int)ev->arg[2];
	if (!c || !c->used) return;
	uint32_t t = (uint32_t)c->u58;
	if (t & CTL_MENU) {
		if (c == fe_menu[0]) {
			if (!gFENewMailMenu) {
				gFECurScreen = -2;
				fe_deselect_menu(1);
			}
			gFENewMailMenu = 1;
		} else if (c == fe_menu[1]) {
			if (gFENewMailMenu == 1) {
				gFECurScreen = -2;
				fe_deselect_menu(0);
			}
			gFENewMailMenu = 0;
		} else {
			return;
		}
		fe_sound(0x10f);
		if (value < 13) {
			UiListData *d = ((UiMenuDesc *)c->u5c)->info->data;
			int screen = value + d->first;
			d->selected = screen;
			if (gFECurScreen != screen) {
				FE_CloseScreen();
				fe_flush();
				if (screen < 9 || screen > 11) {
					gFEListMode = 0;
					FE_OpenScreen(screen);
					return;
				}
				gFEListMode = screen - 8;
				Mail_ListReset();
				FE_OpenScreen(screen);
			}
		}
		return;
	}
	if (t & CTL_COMBO) return;
	if (t & CTL_COMBO1) {
		fe_slots_event(c, value, state);
		return;
	}
	if (t & CTL_EDIT2) {
		if (value) fe_edit_event(c, value);
		return;
	}
	if (t & CTL_BUTTON) {
		fe_sound(((UiButtonDesc *)c->u5c)->data->state & 1 ? 0x112 : 0x113);
		return;
	}
	if (t & CTL_HUBBUTTON) {
		fe_demo_armed = 0;
		UiButtonData *b = ((UiButtonDesc *)c->u5c)->data;
		if (b->state == 0) {
			b->state = 1;
			return;
		}
		fe_sound(0x10f);
		FE_HubHighlight(c);
		FE_CloseScreen();
		int cat = -1;
		if (c == hub_game) {
			cat = 2;
		} else if (c == hub_new) {
			FE_OpenCategory(3);
			Campaign_InitMail();
			FE_OpenScreen(9);
			gFEListMode = 1;
			gFEGameSaved = 1;
			FE_DestroyHub();
			FE_BuildHub(fe_root, 2);
			FE_HubHighlight(hub_mail);
		} else if (c == hub_continue) {
			FE_OpenCategory(3);
			gFEGameSaved = 1;
			FE_SetHub(fe_root, 2, hub_mail ? hub_mail : (Window *)-1);
		} else if (c == hub_setup) {
			cat = 1;
		} else if (c == hub_mail) {
			Mail_Open();
			gFEListMode = 1;
		} else if (c == hub_system) {
			cat = 4;
		}
		if (cat < 0) return;
		if (FE_OpenCategory(cat) && cat == 3) {
			FE_OpenScreen(9);
			gFEListMode = 1;
		}
		gFECurScreen = -1;
		return;
	}
	if (t & CTL_TABBAR) {
		fe_sound(0x10f);
		if (value > 3) return;
		if (!gFEScreenDesc.tabs) return;
		if (gFEScreenDesc.tab_destroy) gFEScreenDesc.tab_destroy();
		if (gFEScreenDesc.buttons) FE_DestroyButtonBar();
		fe_get_screen_desc(&gFEScreenDesc, gFECurScreen);
		if (gFEExitFlag == 1) {
			gFEScreenDesc.tab_destroy = NULL;
			return;
		}
		if (gFEScreenDesc.tabs == &fe_ok_tab && gFECurScreen == 8) {
			FE_DebriefDone();
			return;
		}
		if (gFEScreenDesc.tab_create) gFEScreenDesc.tab_create();
		if (gFEScreenDesc.buttons) FE_CreateButtonBar(gFEScreenDesc.buttons);
		return;
	}
	if (t & CTL_BUTTONBAR) {
		fe_sound(0x10f);
		if (gFEScreenDesc.buttons && gFEScreenDesc.button_action) gFEScreenDesc.button_action(value);
		return;
	}
	if (t & CTL_LIST) {
		if (c != fe_list) return;
		switch (gFEListMode) {
			case 1: Mail_OpenBusiness(fe_list_data->first + value); return;
			case 2:
			case 3: Mail_OpenNewsPersonal(fe_list_data->first + value); return;
			case 4: case 5: case 6: case 7: case 8: Shop_ShowItem(value); return;
			default: return;
		}
	}
	if (t & CTL_UPDOWN) {
		if (state == 1) fe_sound(0x111);
		else if (state == 2) fe_sound(0x110);
		else if (state == 3) fe_sound(0x114);
		return;
	}
	if (t & CTL_LIST2) {      // a row of a two-column list: the key editor starts on its next frame
		fe_controls_list_event();
		return;
	}
	if (t & CTL_SPINNER) {
		if (value) fe_spinner_event(c, (intptr_t)ev->arg[1]);
		if (state == 1) fe_sound(0x111);
		else if (state == 2) fe_sound(0x110);
		else if (state == 3) fe_sound(0x114);
		return;
	}
	if (t & CTL_BUTTON3) {
		fe_shop_button3(c);
		return;
	}
}

// ================================================================ FE_Run (0x409fc0) as begin / frame / end

static void fe_background_open(void) { // Slide_Load(-1): a random front-end background, SCREENS6 1..10
	media_slide_select_dir(0);
	media_slide_open(-1);
}

static void fe_background_draw(void) { media_slide_draw(); }   // Slide_Draw 0x41d650

static void fe_background_close(void) { media_slide_unload(); } // Slide_Unload 0x41d4e0

static const task_type_t fe_timer_type;

static void fe_timer_update(task_t *t) { // the Timer task 0x491ae0 (events 3 / 2)
	static int ticks;
	(void)t;
	LanderEvent ev = {EV_TICK_100MS, {0}};
	Event_Raise(&ev);
	if (++ticks >= 10) {
		ticks = 0;
		ev.type = EV_TICK_1S;
		Event_Raise(&ev);
	}
}

static const task_type_t fe_timer_type = {0, fe_timer_update, NULL, NULL, NULL, NULL, NULL, 100};

void fe_init_once(void) { // Game_Startup (front-end part) + FE_InitOnce 0x408fc0
	for (int i = 0; i < 16; i++) fe_screen_titles[i] = Text_Get(i);           // UI_LocalizeStrings
	for (int i = 0; i < 6; i++) fe_category_titles[i] = Text_Get(0x10 + i);
	fe_screens_init();
	fe_ctrlconf_load_disc();
	LanderConfigs_Init();
	Player_NewGame();
	Campaign_InitMail();
	sound_init();
	sound_load_database("MISSIONS\\MERCURY\\1");   // the FE sound bank (MISSIONS\DEMO\1 is not on the disc)
	gLastMissionResult = -1;
	gFrontEndEntry = FE_ENTRY_FRESH;
}

void fe_begin(int entry, int last) { // Campaign_FrontEnd 0x410370 + FE_Run set-up
	fe_background_open();
	// FE_LoadResources 0x409120: the global texture file and models
	ui_reset();
	sound_load_database("MISSIONS\\MERCURY\\1");
	if (!gUiModels) gUiModels = ppm_load("MISC\\OBJECTS.PPM");
	fe_sprite6.tf = gGlobalTextures;
	menu_sprite.tf = gGlobalTextures;
	gUiShowPointer = 1;
	gFEResult = 0;
	gFEExitFlag = 0;
	gFECurScreen = -1;
	gFECurCategory = -1;
	fe_root = fe_pane = fe_html = fe_list = fe_tabbar = fe_buttonbar = NULL;
	fe_menuwin[0] = fe_menuwin[1] = fe_menu[0] = fe_menu[1] = fe_title_model = NULL;
	fe_cashline = fe_message = fe_logo[0] = fe_logo[1] = NULL;
	hub_new = hub_continue = hub_net = hub_setup = hub_game = hub_mail = hub_system = NULL;
	fe_updown = NULL;
	fe_logos = 0;
	fe_hub_layout = 0;
	fe_closing = fe_demo = false;
	gFESelWorld = gCurWorld;     // FE_ResetGlobals 0x4022b0
	gFESelMission = gCurMission;
	fe_root = UI_AllocWindow();
	UI_OpenWindow(fe_root, NULL);
	gNbPlayers = 0;
	g2dRenderCallback = NULL;
	Events_Reset();
	Events_RegisterAll();
	Sched_Reset();
	EventType_AddHandler(EV_UI_CONTROL, NULL, fe_on_control);
	FE_LoadBriefDll(gFESelWorld, gFESelMission);   // BriefDll_LevelBriefing
	FE_Enter(entry, last);
	Task_CreateNow(&fe_timer_type, NULL);
	Clock_Reset();
	UI_ResetIdle();     // DAT_00573180 = 0, _DAT_0057317c = 0
}

int fe_frame(void) { // one iteration of the FE_Run loop
	if (!UI_GetRoot()) return gFEResult ? gFEResult : FE_RESULT_QUIT;
	uint32_t now = Clock_Update();
	// the attract demo: "Demo Timeout" ms without mouse movement, or D held with cheat bit 0x100
	if (fe_demo_armed && (gDemoTimeoutMs < gUiIdleMs || ((gSoftFruit & 0x100) && input_down(BTN_KEY('d'))))) fe_demo = true;
	if ((platform_should_quit() || fe_demo) && !fe_closing) {
		fe_logos_destroy();
		fe_destroy_title_model();
		UI_CloseAll();
		fe_closing = true;
		if (!fe_demo) gFEResult = FE_RESULT_QUIT;
	}
	Sched_RunUntil(now);
	ui_input_frame();
	sound_update();
	render_frame_begin(rgba(0, 0, 0, 255));
	render_reset_scissor();
	fe_background_draw();
	ui_draw_begin();
	UI_UpdateDraw(1);
	ui_draw_end();
	ui_draw_models();
	render_reset_scissor();
	if (g2dRenderCallback) {   // (gOT + 0x58) queued primitives are drawn at once here
		ui_draw_begin();
		g2dRenderCallback(NULL);
		ui_draw_end();
	}
	ui_draw_pointer();
	if (gFEExitFlag == 1) {
		if (gFEScreenDesc.tab_destroy) gFEScreenDesc.tab_destroy();
		fe_destroy_cash_line();
		fe_logos_destroy();
		fe_destroy_title_model();
		UI_CloseAll();
		gFEExitFlag = 0;
	}
	if (UI_GetRoot()) return FE_RESULT_NONE;
	return fe_demo ? FE_RESULT_DEMO : (gFEResult ? gFEResult : FE_RESULT_QUIT);
}

void fe_end(void) { // the end of FE_Run + FE_UnloadResources 0x4092f0
	fe_logos_destroy();
	gFEScreenDesc.exit = gFEScreenDesc.tab_destroy = NULL;
	gFEScreenDesc.tabs = gFEScreenDesc.buttons = NULL;
	fe_root = fe_pane = fe_html = NULL;
	g2dRenderCallback = NULL;
	gScoreRenderFn = NULL;
	sound_stop_all();
	fe_background_close();
	ui_free_text();
	fe_text_pack_forget();
	if (gEnvironmentTexFile) {
		texfile_free(gEnvironmentTexFile);
		gEnvironmentTexFile = NULL;
	}
	fe_preview_forget();
	gUiShowPointer = 0;
	Sched_DestroyAll();
}
