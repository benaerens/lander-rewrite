// MISSIONS\EARTH\1\brief.dll - faithful C port.
//
// 11 mails, briefing page, de-briefing: result 1 only. If Earth 2 already succeeded it unlocks
// Earth 4 mails 0, 2..6 and Earth 2 mail 1, else Earth 2 mails 1, 0, 2..7. Text records:
// 0 = briefing, 1..11 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100013c0), LevelShowBriefing 0x10001020,
// LevelShowDeBriefing 0x10001030, LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070,
// LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EARTH 2

// 0x10006030: [language][mail] (0x104 bytes each)
static BriefMail e1b_mails[3][11];

static Window *e1b_win;   // 0x1000ad10

// 0x100010a0  (LevelShowBriefing 0x10001020)
static Window *E1B_ShowBriefing(Window *pane)
{
    e1b_win = WindowCreate(0x402, pane);
    WindowSetRect(e1b_win, &rectHTML);
    WindowSetTextFile(e1b_win, 0);
    return e1b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001030)
static int32_t E1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result != 1)
        return 0;
    Briefing_MarkLevelCompleted(WORLD_EARTH, 0);
    if (Briefing_GetExitCode(WORLD_EARTH, 1) >= 1) {
        Briefing_MarkLevelCompleted(WORLD_EARTH, 0);   // (marked twice in the original)
        SendMail(WORLD_EARTH, 3, 0);
        SendMail(WORLD_EARTH, 3, 2);
        SendMail(WORLD_EARTH, 3, 3);
        SendMail(WORLD_EARTH, 3, 4);
        SendMail(WORLD_EARTH, 3, 5);
        SendMail(WORLD_EARTH, 3, 6);
        SendMail(WORLD_EARTH, 1, 1);
        return 0;
    }
    SendMail(WORLD_EARTH, 1, 1);
    SendMail(WORLD_EARTH, 1, 0);
    for (int32_t i = 2; i <= 7; i++)
        SendMail(WORLD_EARTH, 1, i);
    return 0;
}

// 0x10001200  (LevelGetEmailInfo 0x10001070)
static int32_t E1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 11)
        return 5;
    *from = e1b_mails[gLanguage][index].from;
    *subject = e1b_mails[gLanguage][index].subject;
    return e1b_mails[gLanguage][index].category;
}

// 0x10001280  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..10)
static Window *E1B_ShowEmail(Window *root, int32_t index)
{
    e1b_win = WindowCreate(0x402, root);
    WindowSetRect(e1b_win, &rectHTML);
    if ((uint32_t)index <= 10)
        WindowSetTextFile(e1b_win, index + 1);
    return e1b_win;
}

// 0x100013c0  LevelBriefing / LevelStopLevel: empty
static void E1B_Nothing(void)
{
}

// 0x10001090
static uint32_t E1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100013c0, return 0. Port: fresh DLL data.
static int32_t E1B_Load(const char *dllPath)
{
    (void)dllPath;
    e1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText e1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 11, .out = e1b_mails },
    { 0 },
};

const brief_script_t brief_earth_1 = {
    .name            = "EARTH\\1",
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
