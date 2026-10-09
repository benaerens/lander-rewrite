// The mail screens (0x404000-0x404980): the NEW MAIL / ARCHIVE lists of Business, News and
// Personal mail (Mail_FillList 0x4082c0), reading a mail (the mission's brief.dll LevelShowEmail),
// ACCEPT -> the briefing (LevelShowBriefing), START -> FE_LaunchMission. Business mail moves to the
// archive when the mission is completed (Briefing_MarkLevelCompleted), News / Personal when read.
#include <stdio.h>
#include <string.h>

#include "fe.h"

int32_t gMailState;            // DAT_005837c4: 0 list, 1 a mail, 2 briefing, 3 briefing shown (START)
static int32_t mail_selected = -1;   // DAT_00583634 (row of the list)

// the list buffers (FUN_00401750): 15 rows of sender / subject / mail index / (world, mission)
static char row_from[15][20], row_subject[15][52];     // 0x579180 / 0x5792c0
static uint8_t row_index[15];                          // 0x583620
static int16_t row_wm[15][2];                          // 0x579020
static struct {
	int16_t count, filter, scroll, more;               // +0x10 .. +0x16
} mlist = {0, 1, 0, 0};

static const char *row_subject_ptr[16], *row_from_ptr[16]; // DAT_005795e0 / DAT_00579060
static UiListData mail_list_data = {0, 8, 8, 0, -1, 14, row_subject_ptr};   // 0x48f470
UiListDesc fe_list_desc;                              // DAT_00583860
UiListData *fe_list_data;                             // DAT_00583864

static const WinRect mail_list_rect = {224, 146, 70, 128};   // 0x48f4f8

static void Mail_FillList(int read) { // 0x4082c0
	int n = 0;
	Mail_FetchInfo();
	for (int i = 0; i < gMailCount; i++) {
		MailRecord *r = &gMailRecords[i];
		if (r->category != mlist.filter || r->read != read) continue;
		if (n >= mlist.scroll) {
			int row = n - mlist.scroll;
			if (row > 14) {
				mlist.more = 1;
				mlist.count = (int16_t)n;
				return;
			}
			memcpy(row_from[row], r->from, sizeof(row_from[row]));
			memcpy(row_subject[row], r->subject, sizeof(row_subject[row]));
			row_wm[row][0] = r->world;
			row_wm[row][1] = r->mission;
			row_index[row] = r->index;
		}
		n++;
	}
	mlist.more = 0;
	mlist.count = (int16_t)n;
}

static void mail_list_control(int count) { // FUN_004040f0
	fe_list_desc.title = NULL;
	fe_list_data = &mail_list_data;
	fe_list_desc.data = &mail_list_data;
	fe_list_desc.items = NULL;
	mail_list_data.first = 0;
	mail_list_data.visible = count;
	mail_list_data.count = count;
	mail_list_data.selected = -1;
	for (int i = 0; i < count && i < 15; i++) {
		row_from_ptr[i] = row_from[i];
		row_subject_ptr[i] = row_subject[i];
	}
	row_from_ptr[min(count, 15)] = row_subject_ptr[min(count, 15)] = NULL;
	if (count == 0) {
		row_from_ptr[0] = row_subject_ptr[0] = NULL;
	} else if (!fe_list) {
		fe_list = Control_CreateList(fe_pane, &fe_list_desc);
	}
	if (fe_list) Control_SetRect(fe_list, &mail_list_rect);
}

static void mail_refill(void) { // FUN_004041a0
	mlist.count = 0;
	Mail_FillList(gFENewMailMenu ? 0 : 1);
}

static void mail_scroll_up(void) { // FUN_004041e0
	if (mlist.scroll > 0) {
		mlist.scroll--;
		mail_refill();
		fe_sound(0x110);
	}
}

static void mail_scroll_down(void) { // FUN_00404210
	if (mlist.more == 1) {
		mlist.scroll++;
		row_from[14][0] = 0;
		row_subject[14][0] = 0;
		mail_refill();
		fe_sound(0x111);
	}
}

static const UiRect updown_rects[4] = {{62, 56, 74, 76}, {98, 56, 110, 76}, {74, 56, 86, 76}, {110, 56, 122, 76}}; // 0x48e888
static UiSprite updown_sprite = {0, NULL, 6, 0};             // 0x48e788
static UiUpDown mail_updown = {updown_rects, mail_scroll_up, mail_scroll_down, NULL, NULL, 0};   // 0x48eaf0
static UiUpDownDesc mail_updown_desc = {&updown_sprite, &mail_updown};   // 0x48eb10

Window *fe_mail_updown(UiUpDownDesc *d, const WinRect *r, bool create);

static void mail_arrows(void) { // FUN_00404250
	static const WinRect r = {204, 144, 12, 30};           // 0x48eb18
	updown_sprite.tf = gGlobalTextures;
	mail_updown.b_up = mail_updown.b_down = NULL;
	fe_mail_updown(&mail_updown_desc, &r, mlist.count - mlist.scroll >= 15);
}

static void mail_show_list(int filter, int read) { // FUN_004042c0 (new) / FUN_00404340 (archive)
	mlist.count = 0;
	mlist.scroll = 0;
	mlist.filter = (int16_t)filter;
	Mail_FillList(read);
	mail_list_control(mlist.count);
	mail_arrows();
	while (mlist.more) mail_scroll_down();   // show the newest mails
}

void Mail_ListReset(void) { gMailState = 0; } // FUN_00404660

// ---------------------------------------------------------------- the tab bar ("CLOSE / ACCEPT", "START")
static const char *labels_mail[2];    // DAT_0049a970: CLOSE, ACCEPT (Text 0x2c, 0x2d)
static const char *labels_start[1];   // DAT_0049aec8: START (Text 0x2e)
UiBarData fe_mail_tabs = {fe_bar_skin, 0, 0, 2, -1, labels_mail, {0}};   // 0x48f2a8

void Mail_ScreenEnter(void) { // FUN_004043c0
	mail_selected = -1;
	if (gMailState == 0) fe_mail_tabs.count = 0;
}

void Mail_ScreenExit(void) { // FUN_004043e0
	FE_DestroyList();
	if (!fe_tabbar) FE_CreateTabBar(gFEScreenDesc.tabs);
}

void Mail_ScreenCreate(void) { // 0x404400 (Business: sub-state 0 list, 1 mail, 2 accept -> briefing, 3 START)
	labels_mail[0] = Text_Get(0x2c);
	labels_mail[1] = Text_Get(0x2d);
	labels_start[0] = Text_Get(0x2e);
	if (!fe_tabbar) FE_CreateTabBar(gFEScreenDesc.tabs);
	switch (gMailState) {
		case 0:
			fe_mail_tabs.count = 0;
			mail_show_list(1, gFENewMailMenu ? 0 : 1);
			break;
		case 1: {
			int w = row_wm[mail_selected][0], m = row_wm[mail_selected][1];
			fe_mail_tabs.labels = labels_mail;
			if (!gFENewMailMenu) {
				fe_mail_tabs.count = 1;
			} else {
				fe_mail_tabs.count = 2;
				gFESelWorld = w;
				gFESelMission = m;
			}
			FE_LoadBriefDll(w, m);
			FE_LoadMissionTextPack(w, m);
			FE_LoadMissionPreview(w, m);
			fe_html = BriefDll_ShowEmail(fe_root, row_index[mail_selected]);
			break;
		}
		case 2:
			if (gFENewMailMenu) {
				Briefing_SetReplayPoint(gFESelWorld, gFESelMission);
				fe_mail_tabs.labels = labels_start;
				FE_CloseCategory();
				FE_CloseScreen();
				FE_CreatePane();
				static const WinRect wide = {51, 105, 538, 303};
				FE_SetPaneRect(&wide);
				FE_OpenScreen(9);
				FE_LoadMissionTextPack(gFESelWorld, gFESelMission);
				FE_LoadMissionPreview(gFESelWorld, gFESelMission);
				fe_html = BriefDll_ShowBriefing(fe_pane);
			}
			break;
		case 3:
			if (fe_mail_tabs.current == 0) {
				gMailState = 0;
				FE_DestroyHtmlWindow();
				FE_LaunchMission();
			}
			break;
		default:
			break;
	}
	fe_mail_tabs.current = -1;
}

void Mail_ScreenDestroy(void) { // FUN_004045e0
	switch (gMailState) {
		case 0:
			FE_DestroyList();
			break;
		case 1:
			gMailState = fe_mail_tabs.current == 1 ? 2 : 0;
			fe_mail_tabs.count = fe_mail_tabs.current == 1;
			FE_DestroyTabBar();
			FE_DestroyHtmlWindow();
			break;
		case 2:
			FE_DestroyTabBar();
			gMailState = 3;
			break;
		case 3:
			FE_DestroyTabBar();
			FE_DestroyHtmlWindow();
			break;
		default:
			break;
	}
}

void Mail_Open(void) { // 0x404670
	if (gMailState == 3) {
		FE_CloseCategory();
		FE_CloseScreen();
		FE_CreatePane();
		static const WinRect wide = {51, 105, 538, 303};
		FE_SetPaneRect(&wide);
		FE_OpenScreen(9);
		FE_LoadMissionTextPack(gFESelWorld, gFESelMission);
		fe_html = BriefDll_ShowBriefing(fe_pane);
	} else {
		gMailState = 0;
		if (FE_OpenCategory(3)) FE_OpenScreen(9);
	}
	fe_mail_tabs.current = -1;
	gFEListMode = 1;
}

void Mail_OpenBusiness(int index) { // 0x404710
	mail_selected = index;
	FE_DestroyList();
	gMailState = 1;
	FE_DestroyTabBar();
	Mail_ScreenCreate();
}

void Mail_OpenNewsPersonal(int index) { // 0x4047f0
	mail_selected = index;
	FE_DestroyList();
	gMailState = 1;
	FE_DestroyTabBar();
	fe_mail_tabs.count = 1;
	labels_mail[0] = Text_Get(0x2c);
	labels_mail[1] = Text_Get(0x2d);
	fe_mail_tabs.labels = labels_mail;
	FE_CreateTabBar(gFEScreenDesc.tabs);
	int w = row_wm[index][0], m = row_wm[index][1], idx = row_index[index];
	FE_LoadBriefDll(w, m);
	FE_LoadMissionTextPack(w, m);
	FE_LoadMissionPreview(w, m);
	fe_html = BriefDll_ShowEmail(fe_root, idx);
	Briefing_MarkEmailAsRead(w, m, idx);
}

static void mail_other_create(int filter) { // FUN_00404760 (News) / FUN_004048b0 (Personal)
	if (!fe_tabbar) FE_CreateTabBar(gFEScreenDesc.tabs);
	if (gMailState == 0) {
		fe_mail_tabs.count = 0;
		mail_show_list(filter, gFENewMailMenu ? 0 : 1);
	}
	fe_mail_tabs.current = -1;
}

void Mail_NewsCreate(void) { mail_other_create(2); }
void Mail_PersonalCreate(void) { mail_other_create(4); }

void Mail_OtherDestroy(void) { // FUN_004047c0
	if (gMailState == 0) {
		FE_DestroyList();
	} else if (gMailState == 1) {
		if (fe_mail_tabs.current == 0) gMailState = 0;
		FE_DestroyHtmlWindow();
	}
}

void fe_list_create(UiListData *d) { // FUN_00404980 (the shop list)
	static const WinRect r = {210, 160, 64, 0};             // 0x48f508
	UI_Flush();
	fe_list_data = d;
	d->selected = -1;
	if (!fe_list) {
		fe_list_desc.data = d;
		fe_list_desc.items = NULL;
		fe_list = Control_CreateList(fe_pane, &fe_list_desc);
		if (fe_list) {
			fe_list->flags |= UIF_FOCUSABLE;
			UI_SetFocus(fe_list);
			Control_SetRect(fe_list, &r);
		}
	}
}
