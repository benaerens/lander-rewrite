// The front end's side of the mission API and the campaign state: the mail records (SendMail
// 0x407c20 .. Mail_RollbackToReplayPoint 0x408230), mission results, the player state for a new
// game (Player_NewGame 0x407bb0), the score sheet (Briefing_ShowScore 0x407ed0) and the brief.dll
// glue (BriefDll_* 0x47b640.., FE_LoadBriefDll 0x408060, FE_LoadMissionTextPack 0x4080b0).
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "ui.h"
#include "font.h"
#include "../fe/fe.h"
#include "../texfile.h"
#include "../platform.h"

MailRecord gMailRecords[MAIL_MAX];      // 0x579620
int32_t gMailCount;                     // 0x583638
int32_t gMailReplayPoint = -1;
int32_t gMailCountAtDebrief = -1;       // 0x578418
int8_t gMissionResults[23 * 6];         // 0x583640
ScoreRenderFn gScoreRenderFn;           // 0x61f300
const int32_t gnNumMissionsForLevel[23] = {1, 2, 4, 1, 6, 1, 2, 2, 1, 2, 1, 3, 1, 2, 1, 2, 2, 2, 2, 1, 4, 2, 0};

// ---------------------------------------------------------------- mail records

void Mail_Clear(void) { // 0x407920
	for (int i = 0; i < MAIL_MAX; i++) gMailRecords[i].seq = -1;
	gMailCount = 0;
	gMailReplayPoint = -1;
	gMailCountAtDebrief = -1;
}

void Results_Clear(void) { // 0x407950: the played missions of each world = -1 (never played)
	for (int w = 0; w < 23; w++) {
		for (int m = 0; m < gnNumMissionsForLevel[w] && m < 6; m++) gMissionResults[w * 6 + m] = -1;
	}
}

void Mail_AddPlaceholders(int world, int mission) { // 0x407870: 14 unfetched records of a mission
	for (int i = 0; i < 14 && gMailCount < MAIL_MAX; i++) {
		MailRecord *r = &gMailRecords[gMailCount];
		r->world = (uint8_t)world;
		r->mission = (uint8_t)mission;
		r->index = (uint8_t)i;
		r->category = 0xff;
		r->read = 0;
		r->seq = (int16_t)gMailCount;
		r->from[0] = 0;
		r->subject[0] = 0;
		gMailCount++;
	}
}

static MailRecord *Mail_Find(int world, int mission, int index) { // 0x407cc0
	for (int i = 0; i < gMailCount; i++) {
		MailRecord *r = &gMailRecords[i];
		if (r->world == world && r->mission == mission && r->index == index) return r;
	}
	return NULL;
}

void SendMail(int32_t world, int32_t mission, int32_t index) { // 0x407c20: add a mail unless it exists
	if (Mail_Find(world, mission, index) || gMailCount >= MAIL_MAX) return;
	MailRecord *r = &gMailRecords[gMailCount];
	r->world = (uint8_t)world;
	r->mission = (uint8_t)mission;
	r->index = (uint8_t)index;
	r->read = 0;
	r->seq = (int16_t)gMailCount;
	r->category = 0xff;
	r->from[0] = 0;
	r->subject[0] = 0;
	gMailCount++;
}

void Briefing_MarkEmailAsRead(int32_t world, int32_t mission, int32_t index) { // 0x407d10
	MailRecord *r = Mail_Find(world, mission, index);
	if (r) r->read = 1;
}

void Briefing_MarkLevelCompleted(int32_t world, int32_t mission) { // 0x407d30: the Business mail -> archive
	for (int i = 0; i < gMailCount; i++) {
		MailRecord *r = &gMailRecords[i];
		if (r->world == world && r->mission == mission && r->category == 1) {
			r->read = 1;
			return;
		}
	}
}

void Briefing_SetReplayPoint(int32_t world, int32_t mission) { // 0x407d80
	for (int i = 0; i < gMailCount; i++) {
		MailRecord *r = &gMailRecords[i];
		if (r->world == world && r->mission == mission && r->category == 1) {
			gMailReplayPoint = i;
			return;
		}
	}
}

int32_t Briefing_GetExitCode(int32_t world, int32_t mission) { // 0x407df0
	return world >= 0 && world < 23 && mission >= 0 && mission < 6 ? gMissionResults[world * 6 + mission] : 0;
}

int32_t Briefing_HasEmailBeenRead(int32_t world, int32_t mission, int32_t index) { // 0x4078c0
	MailRecord *r = Mail_Find(world, mission, index);
	return r ? r->read : 0;
}

void Mail_FetchInfo(void) { // 0x408160: LevelGetEmailInfo for each record not fetched yet
	int lw = -1, lm = -1;
	for (int i = 0; i < gMailCount; i++) {
		MailRecord *r = &gMailRecords[i];
		if (r->category != 0xff) continue;
		if ((lw != r->world || lm != r->mission) && FE_LoadBriefDll(r->world, r->mission)) {
			lw = r->world;
			lm = r->mission;
		}
		const char *from = "", *subject = "";
		r->category = (uint8_t)BriefDll_GetEmailInfo(r->index, &from, &subject);
		strncpy(r->from, from ? from : "", sizeof(r->from));
		strncpy(r->subject, subject ? subject : "", sizeof(r->subject));
		r->from[19] = 0;
		r->subject[51] = 0;
	}
}

void Mail_RollbackToReplayPoint(int replay, int count) { // 0x408230 (RE-PLAY)
	Mail_FetchInfo();
	if (replay >= 0 && replay < MAIL_MAX && gMailRecords[replay].category == 1) {
		gMailRecords[replay].read = 0;
		gMailCount = count;
		return;
	}
	for (int i = gMailCount - 1; i >= 0; i--) {
		MailRecord *r = &gMailRecords[i];
		if (r->category == 1 && r->read == 1) {
			r->read = 0;
			if (gMailCount < count) count = gMailCount;
			gMailCount = count;
			if (count < i) gMailCount = i;
			return;
		}
	}
}

void Campaign_InitMail(void) { // 0x408cf0: Mercury 1, Training 1 and 2
	Mail_Clear();
	Results_Clear();
	if ((gSoftFruit & 0x200) && input_down(BTN_KEY('e'))) {   // cheat: every mail of the campaign
		static const uint8_t wm[][2] = {{0, 0}, {1, 0}, {1, 1}, {2, 0}, {2, 1}, {2, 2}, {2, 3}, {4, 0}, {4, 1},
			{4, 2}, {4, 3}, {4, 4}, {4, 5}, {6, 0}, {6, 1}, {7, 0}, {7, 1}, {8, 0}, {9, 0}, {9, 1}, {11, 0},
			{11, 1}, {11, 2}, {12, 0}, {13, 0}, {13, 1}, {15, 0}, {15, 1}, {19, 0}};
		for (size_t k = 0; k < len(wm); k++) {
			for (int i = 0; i < 12; i++) SendMail(wm[k][0], wm[k][1], i);
		}
	} else {
		Mail_AddPlaceholders(0, 0);
	}
	Mail_AddPlaceholders(0x15, 0);
	Mail_AddPlaceholders(0x15, 1);
	gFESelWorld = 0;
	gFESelMission = 0;
}

// ---------------------------------------------------------------- player

void Player_NewGame(void) { // 0x407bb0: the Hopper, Pulse 1000, Standard C armour, $5000
	memset(&gPlayerState, 0, sizeof(gPlayerState));
	gPlayerState.shield = 0xff;
	gPlayerState.primary = 1;
	gPlayerState.secondary = 0;
	gPlayerState.cash = (gSoftFruit & 0x20) ? 5000 + 0x17d64b8 : 5000;
	gPlayerState.engine = 0;
	gPlayerState.armour = 3;
	gPlayerState.landerType = 0;
	gPlayerState.fuel = 0x180000;       // ArrayOfLanderConfigs[0] fuel capacity (DAT_005790a4)
	gPlayerState.maxFuel = 0x180000;
	gPlayerState.team = 0;
}

// ---------------------------------------------------------------- score sheet (Briefing_ShowScore 0x407ed0)

static char score_lines[10][256];          // 0x578420 + 0x100 * i

static void score_print(const char *s, int x, int y, int font) { // FUN_00407e10: centred, teal
	// queued like Font_Print into the OT, so the de-brief pane's fly-in transform applies (fe_frame
	// draws the 2D callback between ui_draw_begin / ui_draw_end)
	ui_text(0, s, (int)strlen(s), x - (font_width(s, font) >> 1), y, 0x00b3ad00, font);
}

static void score_render(void *ot) { // FUN_00407e60 (g2dRenderCallback)
	(void)ot;
	Win_SetTransform(fe_pane);
	score_print(score_lines[0], 320, 150, 1);
	for (int i = 1, y = 190; i < 10; i++, y += 20) score_print(score_lines[i], 320, y, 1);
	Win_SetTransform(NULL);
}

void Briefing_ShowScore(void) { // 0x407ed0
	const GameScoreData *s = &gGame_ScoreData;
	memset(score_lines, 0, sizeof(score_lines));
	snprintf(score_lines[0], 256, "%s", Text_Get(G.mission_result < 1 ? 0xf0 : 0xef));
	int shots = s->shotsFired + s->missilesFired;
	snprintf(score_lines[1], 256, "%s: %d", Text_Get(0xf2), shots);
	snprintf(score_lines[2], 256, "%s: %d", Text_Get(0xf3), s->hits);
	if (shots == 0) snprintf(score_lines[3], 256, "%s: -", Text_Get(0xf5));
	else snprintf(score_lines[3], 256, "%s: %d%%", Text_Get(0xf5), s->hits * 100 / shots);
	snprintf(score_lines[4], 256, "%s: %d", Text_Get(0xf4), s->hitsTaken);
	snprintf(score_lines[5], 256, "%s: %d %s %d", Text_Get(0xf1), s->pickups, Text_Get(0xf6), s->pickupsTotal);
	snprintf(score_lines[6], 256, "%s: %d %s", Text_Get(0xf7), s->timeMs / 1000, Text_Get(0xf8));
	g2dRenderCallback = score_render;
}

void SetScoreRender(ScoreRenderFn fn) { gScoreRenderFn = fn; } // 0x444070

// ---------------------------------------------------------------- brief.dll (BriefDll_* 0x47b5d0..)

static const mission_def_t *brief_def;     // the mission whose brief.dll is loaded (DAT_00577c04)

const mission_def_t *fe_mission_def(int world, int mission) {
	int n;
	const mission_def_t *m = mission_list(&n);
	for (int i = 0; i < n; i++) {
		if (m[i].world_index == world && m[i].mission_index == mission) return &m[i];
	}
	return NULL;
}

const brief_script_t *fe_brief(void) { return brief_def ? brief_def->brief : NULL; }

static void brief_unload(void) { // BriefDll_Unload 0x47b4f0: LevelStopLevel
	const brief_script_t *b = fe_brief();
	if (b && b->stop) b->stop();
	brief_def = NULL;
}

bool FE_LoadBriefDll(int world, int mission) { // 0x408060 (Mission_LoadBriefDllFor + LevelBriefing)
	const mission_def_t *m = fe_mission_def(world, mission);
	if (m != brief_def) {
		brief_unload();
		if (m && m->brief) {
			char path[64];
			snprintf(path, sizeof(path), "MISSIONS\\%s\\%s\\BRIEF.DLL", m->world, m->mission);
			brief_def = m;
			gametext_load(path, m->brief->text);   // the DLL's text, the first time (gametext.c)
			if (m->brief->load) m->brief->load(path);
		}
	}
	const brief_script_t *b = fe_brief();
	if (!b) return false;
	if (b->briefing) b->briefing();
	return true;
}

int32_t BriefDll_GetEmailInfo(int32_t index, const char **from, const char **subject) { // 0x47b6c0
	const brief_script_t *b = fe_brief();
	if (!b || !b->get_email_info) return 5;     // no brief.dll (or not ported yet): no such mail
	return b->get_email_info(index, from, subject);
}

Window *BriefDll_ShowEmail(Window *root, int32_t index) {
	const brief_script_t *b = fe_brief();
	return b && b->show_email ? b->show_email(root, index) : NULL;
}

Window *BriefDll_ShowBriefing(Window *pane) {
	const brief_script_t *b = fe_brief();
	return b && b->show_briefing ? b->show_briefing(pane) : NULL;
}

int32_t BriefDll_ShowDeBriefing(Window *pane, int32_t result) {
	const brief_script_t *b = fe_brief();
	return b && b->show_debriefing ? b->show_debriefing(pane, result) : 0;
}

// ---------------------------------------------------------------- text packs and the world texfile

static int text_pack_kind;                 // DAT_0049b298: 0 none, 1 mission, 2 MISC
static int text_pack_world = -1, text_pack_mission = -1;

void FE_LoadMissionTextPack(int world, int mission) { // 0x4080b0
	if (world == -1) {
		if (text_pack_kind == 2) return;
		static const char *names[3] = {"MISC\\TEXT.DAT", "MISC\\TEXTF.DAT", "MISC\\TEXTG.DAT"};
		int lang = gLanguage >= 0 && gLanguage < 3 ? gLanguage : 0;
		if (!ui_load_text(names[lang])) ui_load_text(names[0]);
		text_pack_kind = 2;
		return;
	}
	if (text_pack_kind == 1 && text_pack_world == world && text_pack_mission == mission) return;
	const mission_def_t *m = fe_mission_def(world, mission);
	text_pack_kind = 0;
	if (!m) return;
	char dir[64];
	snprintf(dir, sizeof(dir), "MISSIONS\\%s\\%s", m->world, m->mission);
	ui_load_mission_text(dir, false);
	text_pack_kind = 1;
	text_pack_world = world;
	text_pack_mission = mission;
}

void fe_text_pack_forget(void) { text_pack_kind = 0; }

static int preview_world = -1;

void FE_LoadMissionPreview(int world, int mission) { // 0x404090: the world texfile (gEnvironmentTexFile)
	(void)mission;
	if (world == preview_world && gEnvironmentTexFile) return;
	if (gEnvironmentTexFile) {
		texfile_free(gEnvironmentTexFile);
		gEnvironmentTexFile = NULL;
	}
	const mission_def_t *m = fe_mission_def(world, mission);
	if (!m) return;
	char path[96];
	snprintf(path, sizeof(path), "MISSIONS\\%s\\TEXFILE\\PCPURE\\TEXFILE", m->world);
	gEnvironmentTexFile = texfile_load(path);
	preview_world = gEnvironmentTexFile ? world : -1;
}

void fe_preview_forget(void) { preview_world = -1; }
