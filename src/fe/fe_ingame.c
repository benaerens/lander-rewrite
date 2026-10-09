// The in-game dialogs: pause menu (PauseMenu_Open 0x407420: Continue / Abort Mission / Exit Lander),
// quit confirmation (QuitConfirm_Open 0x407520: the question, yes / no) and the
// game-over message (GameOverMsg_Show 0x4076d0). Mission_Run (game.c) drives them.
#include <string.h>

#include "fe.h"
#include "../game/font.h"

int32_t gExitLanderRequested;          // DAT_0049c574

static Window *pause_win, *pause_list;          // DAT_0049a6c4 / DAT_0049b248
static Window *quit_win, *quit_list, *quit_text; // DAT_0049a720 / DAT_0049b24c / DAT_0049b250
static Window *over_win, *over_text[2];         // DAT_0049b254 / DAT_0049b258 / DAT_0049b25c

static const char *pause_items[4];              // DAT_0049a6d0: Text 0xd6..0xd8
static const char *quit_items[3];               // DAT_0049aaf8: Text 0xd9, 0xda
static UiListData pause_data = {0, 3, 3, -1, -1, 14, pause_items};   // 0x48f828
static UiListData quit_data = {0, 2, 2, -1, -1, 14, quit_items};     // 0x48f890

void game_pause_select(int index);              // game.c: Mission_Resume / Abort / quit confirmation
static void pause_select(int32_t i) { if (i >= 0) game_pause_select((int)i); }   // PauseMenu_OnSelect 0x4074e0
static void quit_select(int32_t i);
static UiList2Desc pause_desc = {&pause_data, NULL, pause_select, NULL, -1, 0};  // 0x48f848
static UiList2Desc quit_desc = {&quit_data, NULL, quit_select, NULL, -1, 0};     // 0x48f8b0
static UiTextLine quit_line = {0x00b3ad00, 0, NULL, NULL, -2};                  // 0x48f8c8
static UiTextLine over_line[2] = {{0x00b3ad00, 0, NULL, NULL, -2}, {0x00b3ad00, 0, NULL, NULL, -2}}; // 0x48f910 / 0x48f928

static Window *dialog(int w, int h) {
	WinRect r = {128, 128, w, h};
	Window *win = WindowCreate(9, NULL);
	WindowSetRect(win, &r);
	WindowSetBorderInfo(win, &BorderInfo);
	Window_CreateTransform(win);
	return win;
}

void PauseMenu_Open(void) { // 0x407420
	for (int i = 0; i < 3; i++) pause_items[i] = Text_Get(0xd6 + i);
	pause_items[3] = NULL;
	int w = font_width(Text_Get(0xd7), 0);
	pause_win = dialog(w + 0x10, 0x34);
	pause_data.selected = -1;
	pause_desc.items = NULL;
	pause_list = Control_CreateList2(pause_win, &pause_desc);
	if (pause_list) {
		pause_list->flags |= 5;
		pause_list->flags |= UIF_FOCUSABLE;
		static const WinRect r = {8, 8, 100, 36};             // 0x48f818
		Control_SetRect(pause_list, &r);
		UI_SetFocus(pause_list);
	}
}

void QuitConfirm_Close(void) { // 0x407650
	if (quit_text) {
		Control_Destroy(quit_text);
		quit_text = NULL;
	}
	if (quit_list) {
		Control_Destroy(quit_list);
		WindowDestroy(quit_win);
		quit_list = NULL;
		quit_win = NULL;
	}
}

void PauseMenu_Close(void) { // 0x4076a0
	if (pause_list) {
		Control_Destroy(pause_list);
		WindowDestroy(pause_win);
		pause_list = NULL;
		pause_win = NULL;
	}
	QuitConfirm_Close();
}

void QuitConfirm_Open(void) { // 0x407520
	quit_items[0] = Text_Get(0xd9);
	quit_items[1] = Text_Get(0xda);
	quit_items[2] = NULL;
	quit_line.text = Text_Get(0xdb);
	int w = font_width(quit_line.text, 0);
	quit_win = dialog(w + 0x10, 0x38);
	quit_data.selected = -1;
	quit_desc.items = NULL;
	quit_list = Control_CreateList2(quit_win, &quit_desc);
	if (quit_list) {
		quit_list->flags |= 5;
		quit_list->flags |= UIF_FOCUSABLE;
		WinRect r = {8, 24, w, 36};                            // 0x48f870
		Control_SetRect(quit_list, &r);
		UI_SetFocus(quit_list);
	}
	quit_text = Control_CreateTextLine(quit_win, &quit_line);
	if (quit_text) {
		quit_text->flags |= 5;
		WinRect r = {8, 8, w, 36};                             // 0x48f880
		Control_SetRect(quit_text, &r);
	}
}

static void quit_select(int32_t i) { // QuitConfirm_OnSelect 0x407630
	if (i == 0) {
		gExitLanderRequested = 1;
		gLevelEndFlag = 1;
	} else if (i == 1) {
		QuitConfirm_Close();
		PauseMenu_Open();
	}
}

void GameOverMsg_Show(int dead) { // 0x4076d0
	over_line[0].text = Text_Get(dead ? 0xdd : 0xdc);
	over_line[1].text = Text_Get(0xde);
	int w = max(font_width(over_line[0].text, 0), font_width(over_line[1].text, 0));
	over_win = dialog(w + 0x10, 0x2c);
	for (int i = 0; i < 2; i++) {
		over_text[i] = Control_CreateTextLine(over_win, &over_line[i]);
		if (over_text[i]) {
			over_text[i]->flags |= 5;
			WinRect r = {8, i ? 24 : 8, w, 36};                // 0x48f8f0 / 0x48f900
			Control_SetRect(over_text[i], &r);
		}
	}
}

void GameOverMsg_Hide(void) { // FUN_00407810
	if (over_win) WindowDestroy(over_win);
	for (int i = 0; i < 2; i++) {
		if (over_text[i]) Control_Destroy(over_text[i]);
		over_text[i] = NULL;
	}
	over_win = NULL;
}
