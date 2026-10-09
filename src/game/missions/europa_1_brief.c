// MISSIONS\EUROPA\1\brief.dll - faithful C port.
//
// 6 mails, briefing page, de-briefing: result 1 sends Callisto 2 mail 0, result 2 (the fast
// finish) Ganymede 1 mail 0 instead; both then send Callisto 2 mails 2..5, 7, 8 and mark Europa 1
// completed. Any other result sends nothing. Text records: 0 = briefing, 1..6 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EUROPA   6
#define WORLD_CALLISTO 7
#define WORLD_GANYMEDE 8

// 0x10006030: [language][mail]
static BriefMail e1b_mails[3][6];

static Window *e1b_win;   // 0x10009dd0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *E1B_ShowBriefing(Window *pane)
{
    e1b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(e1b_win, &rectHTML);
    WindowSetTextFile(e1b_win, 0);
    return e1b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t E1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1)
        SendMail(WORLD_CALLISTO, 1, 0);
    else if (result == 2)
        SendMail(WORLD_GANYMEDE, 0, 0);
    else
        return 0;
    SendMail(WORLD_CALLISTO, 1, 2);
    SendMail(WORLD_CALLISTO, 1, 3);
    SendMail(WORLD_CALLISTO, 1, 4);
    SendMail(WORLD_CALLISTO, 1, 5);
    SendMail(WORLD_CALLISTO, 1, 7);
    SendMail(WORLD_CALLISTO, 1, 8);
    Briefing_MarkLevelCompleted(WORLD_EUROPA, 0);
    return 0;
}

// 0x10001190  (LevelGetEmailInfo 0x10001070)
static int32_t E1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 6)
        return 5;
    *from = e1b_mails[gLanguage][index].from;
    *subject = e1b_mails[gLanguage][index].subject;
    return e1b_mails[gLanguage][index].category;
}

// 0x10001210  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0..5)
static Window *E1B_ShowEmail(Window *root, int32_t index)
{
    e1b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(e1b_win, &rectHTML);
    if ((uint32_t)index <= 5)
        WindowSetTextFile(e1b_win, index + 1);
    return e1b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void E1B_Nothing(void)
{
}

// 0x10001090
static uint32_t E1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t E1B_Load(const char *dllPath)
{
    (void)dllPath;
    e1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 6, .out = e1b_mails },
    { 0 },
};

const brief_script_t brief_europa_1 = {
    .name            = "EUROPA\\1",
    .get_version     = E1B_GetVersion,
    .load            = E1B_Load,
    .briefing        = E1B_Nothing,
    .get_email_info  = E1B_GetEmailInfo,
    .show_email      = E1B_ShowEmail,
    .show_briefing   = E1B_ShowBriefing,
    .show_debriefing = E1B_ShowDeBriefing,
    .stop            = E1B_Nothing,
    .text            = e1b_gametext,
};
