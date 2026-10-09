// MISSIONS\TITAN\3\brief.dll - faithful C port.
//
// 9 mails, briefing page, de-briefing: results 1 and 2 archive this mission's job mail and unlock
// Neptune 1 mails 1..13 plus a Neptune job mail: result 1 -> Neptune 1 mail 0, result 2 -> Neptune 2
// mail 0 (the mission only ever ends with result 1). Text records: 0 = briefing, 1..9 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_TITAN   11
#define WORLD_NEPTUNE 15

// 0x10006030: [language][mail]
static BriefMail t3b_mails[3][9];

static Window *t3b_win;   // 0x1000a6f0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *T3B_ShowBriefing(Window *pane)
{
    t3b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(t3b_win, &rectHTML);
    WindowSetTextFile(t3b_win, 0);
    return t3b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t T3B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 2) {
        Briefing_MarkLevelCompleted(WORLD_TITAN, 2);
        SendMail(WORLD_NEPTUNE, 1, 0);
    } else if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_TITAN, 2);
        SendMail(WORLD_NEPTUNE, 0, 0);
    } else {
        return 0;
    }
    for (int32_t i = 1; i <= 0xd; i++)                // (unrolled; shared tail 0x1000114b)
        SendMail(WORLD_NEPTUNE, 0, i);
    return 0;
}

// 0x100011f0  (LevelGetEmailInfo 0x10001070)
static int32_t T3B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 9)
        return 5;
    *from = t3b_mails[gLanguage][index].from;
    *subject = t3b_mails[gLanguage][index].subject;
    return t3b_mails[gLanguage][index].category;
}

// 0x10001270  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0x1000135c)
static Window *T3B_ShowEmail(Window *root, int32_t index)
{
    t3b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(t3b_win, &rectHTML);
    if ((uint32_t)index <= 8)
        WindowSetTextFile(t3b_win, index + 1);
    return t3b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void T3B_Nothing(void)
{
}

// 0x10001090
static uint32_t T3B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x100010a0, return 0. Port: fresh DLL data.
static int32_t T3B_Load(const char *dllPath)
{
    (void)dllPath;
    t3b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t3b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 9, .out = t3b_mails },
    { 0 },
};

const brief_script_t brief_titan_3 = {
    .name            = "TITAN\\3",
    .get_version     = T3B_GetVersion,
    .load            = T3B_Load,
    .briefing        = T3B_Nothing,
    .get_email_info  = T3B_GetEmailInfo,
    .show_email      = T3B_ShowEmail,
    .show_briefing   = T3B_ShowBriefing,
    .show_debriefing = T3B_ShowDeBriefing,
    .stop            = T3B_Nothing,
    .text            = t3b_gametext,
};
