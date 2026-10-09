// MISSIONS\EUROPA\2\brief.dll - faithful C port.
//
// 8 mails, briefing page, de-briefing: result 1 or 2 marks Europa 2 completed and sends Titan 1
// mail 0, then mail 1 (result 1) or mail 2 (result 2), then mails 3..10. Any other result sends
// nothing. Text records: 0 = briefing, 1..8 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EUROPA 6
#define WORLD_TITAN  11

// 0x10006030: [language][mail]
static BriefMail e2b_mails[3][8];

static Window *e2b_win;   // 0x1000a3e0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *E2B_ShowBriefing(Window *pane)
{
    e2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(e2b_win, &rectHTML);
    WindowSetTextFile(e2b_win, 0);
    return e2b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t E2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 2) {
        Briefing_MarkLevelCompleted(WORLD_EUROPA, 1);
        SendMail(WORLD_TITAN, 0, 0);
        SendMail(WORLD_TITAN, 0, 2);
    } else if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_EUROPA, 1);
        SendMail(WORLD_TITAN, 0, 0);
        SendMail(WORLD_TITAN, 0, 1);
    } else {
        return 0;
    }
    for (int32_t i = 3; i <= 10; i++)
        SendMail(WORLD_TITAN, 0, i);
    return 0;
}

// 0x100011d0  (LevelGetEmailInfo 0x10001070)
static int32_t E2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 8)
        return 5;
    *from = e2b_mails[gLanguage][index].from;
    *subject = e2b_mails[gLanguage][index].subject;
    return e2b_mails[gLanguage][index].category;
}

// 0x10001240  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0..7)
static Window *E2B_ShowEmail(Window *root, int32_t index)
{
    e2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(e2b_win, &rectHTML);
    if ((uint32_t)index <= 7)
        WindowSetTextFile(e2b_win, index + 1);
    return e2b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void E2B_Nothing(void)
{
}

// 0x10001090
static uint32_t E2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t E2B_Load(const char *dllPath)
{
    (void)dllPath;
    e2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 8, .out = e2b_mails },
    { 0 },
};

const brief_script_t brief_europa_2 = {
    .name            = "EUROPA\\2",
    .get_version     = E2B_GetVersion,
    .load            = E2B_Load,
    .briefing        = E2B_Nothing,
    .get_email_info  = E2B_GetEmailInfo,
    .show_email      = E2B_ShowEmail,
    .show_briefing   = E2B_ShowBriefing,
    .show_debriefing = E2B_ShowDeBriefing,
    .stop            = E2B_Nothing,
    .text            = e2b_gametext,
};
