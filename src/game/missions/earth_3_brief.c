// MISSIONS\EARTH\3\brief.dll - faithful C port.
//
// 6 mails, briefing page, de-briefing: results 1 and 2 alike. If Earth 2 already succeeded it
// unlocks Earth 4 mails 0, 2..6, else Earth 2 mails 1..7 and 0; then Earth 3 mail 5. Text
// records: 0 = briefing, 1..6 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x10001340), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EARTH 2

// 0x10006030: [language][mail] (0x104 bytes each)
static BriefMail e3b_mails[3][6];

static Window *e3b_win;   // 0x10009dd0

// 0x100010a0  (LevelShowBriefing 0x10001020)
static Window *E3B_ShowBriefing(Window *pane)
{
    e3b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(e3b_win, &rectHTML);
    WindowSetTextFile(e3b_win, 0);
    return e3b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001030)
static int32_t E3B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result != 1 && result != 2)
        return 0;
    Briefing_MarkLevelCompleted(WORLD_EARTH, 2);
    if (Briefing_GetExitCode(WORLD_EARTH, 1) >= 1) {
        SendMail(WORLD_EARTH, 3, 0);
        for (int32_t i = 2; i <= 6; i++)
            SendMail(WORLD_EARTH, 3, i);
    } else {
        for (int32_t i = 1; i <= 7; i++)
            SendMail(WORLD_EARTH, 1, i);
        SendMail(WORLD_EARTH, 1, 0);
    }
    SendMail(WORLD_EARTH, 2, 5);
    return 0;
}

// 0x100011f0  (LevelGetEmailInfo 0x10001070)
static int32_t E3B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 6)
        return 5;
    *from = e3b_mails[gLanguage][index].from;
    *subject = e3b_mails[gLanguage][index].subject;
    return e3b_mails[gLanguage][index].category;
}

// 0x10001270  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..5)
static Window *E3B_ShowEmail(Window *root, int32_t index)
{
    e3b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(e3b_win, &rectHTML);
    if ((uint32_t)index <= 5)
        WindowSetTextFile(e3b_win, index + 1);
    return e3b_win;
}

// 0x10001340  LevelBriefing / LevelStopLevel: empty
static void E3B_Nothing(void)
{
}

// 0x10001090
static uint32_t E3B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x10001340, return 0. Port: fresh DLL data.
static int32_t E3B_Load(const char *dllPath)
{
    (void)dllPath;
    e3b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e3b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 6, .out = e3b_mails },
    { 0 },
};

const brief_script_t brief_earth_3 = {
    .name            = "EARTH\\3",
    .get_version     = E3B_GetVersion,
    .load            = E3B_Load,
    .briefing        = E3B_Nothing,
    .get_email_info  = E3B_GetEmailInfo,
    .show_email      = E3B_ShowEmail,
    .show_briefing   = E3B_ShowBriefing,
    .show_debriefing = E3B_ShowDeBriefing,
    .stop            = E3B_Nothing,
    .text            = e3b_gametext,
};
