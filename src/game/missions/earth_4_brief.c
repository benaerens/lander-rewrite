// MISSIONS\EARTH\4\brief.dll - faithful C port.
//
// 7 mails, briefing page, de-briefing: result 1 unlocks Mars 1A mails 0..9; result 2 unlocks
// Mars 1B mails 0..2 and Mars 1A mails 3..9. Both then mark Earth 4 completed (after the mails,
// unlike the other briefs). Text records: 0 = briefing, 1..7 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EARTH 2
#define WORLD_MARS  4     // missions 1A, 1B, 2, 3, 4, 5 -> index 0..5

// 0x10006030: [language][mail] (0x104 bytes each)
static BriefMail e4b_mails[3][7];

static Window *e4b_win;   // 0x1000a0e0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *E4B_ShowBriefing(Window *pane)
{
    e4b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(e4b_win, &rectHTML);
    WindowSetTextFile(e4b_win, 0);
    return e4b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030); both results share the tail from SendMail(MARS, 0, 3)
static int32_t E4B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    switch (result) {
    case 1:
        SendMail(WORLD_MARS, 0, 0);
        SendMail(WORLD_MARS, 0, 1);
        SendMail(WORLD_MARS, 0, 2);
        break;
    case 2:
        SendMail(WORLD_MARS, 1, 0);
        SendMail(WORLD_MARS, 1, 1);
        SendMail(WORLD_MARS, 1, 2);
        break;
    default:
        return 0;
    }
    for (int32_t i = 3; i <= 9; i++)
        SendMail(WORLD_MARS, 0, i);
    Briefing_MarkLevelCompleted(WORLD_EARTH, 3);
    return 0;
}

// 0x100011d0  (LevelGetEmailInfo 0x10001070)
static int32_t E4B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 7)
        return 5;
    *from = e4b_mails[gLanguage][index].from;
    *subject = e4b_mails[gLanguage][index].subject;
    return e4b_mails[gLanguage][index].category;
}

// 0x10001250  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..6)
static Window *E4B_ShowEmail(Window *root, int32_t index)
{
    e4b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(e4b_win, &rectHTML);
    if ((uint32_t)index <= 6)
        WindowSetTextFile(e4b_win, index + 1);
    return e4b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void E4B_Nothing(void)
{
}

// 0x10001090
static uint32_t E4B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t E4B_Load(const char *dllPath)
{
    (void)dllPath;
    e4b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e4b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 7, .out = e4b_mails },
    { 0 },
};

const brief_script_t brief_earth_4 = {
    .name            = "EARTH\\4",
    .get_version     = E4B_GetVersion,
    .load            = E4B_Load,
    .briefing        = E4B_Nothing,
    .get_email_info  = E4B_GetEmailInfo,
    .show_email      = E4B_ShowEmail,
    .show_briefing   = E4B_ShowBriefing,
    .show_debriefing = E4B_ShowDeBriefing,
    .stop            = E4B_Nothing,
    .text            = e4b_gametext,
};
