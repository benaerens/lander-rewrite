// MISSIONS\TITAN\2\brief.dll - faithful C port.
//
// 10 mails, briefing page, de-briefing: only result 1 does anything: this mission's job mail is
// archived, then - if Mimas 1 ended with result 1 (Briefing_GetExitCode(12, 0)) - Titan 3 mails 0,
// 3 (not in German), 4..9 and Mimas 1 mail 1, otherwise Mimas 1 mails 0..6. Text records:
// 0 = briefing, 1..10 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100013a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_TITAN 11
#define WORLD_MIMAS 12

// 0x10006030: [language][mail]
static BriefMail t2b_mails[3][10];

static Window *t2b_win;   // 0x1000aa00

// 0x100010a0  (LevelShowBriefing 0x10001020)
static Window *T2B_ShowBriefing(Window *pane)
{
    t2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(t2b_win, &rectHTML);
    WindowSetTextFile(t2b_win, 0);
    return t2b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001030)
static int32_t T2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result != 1)
        return 0;
    Briefing_MarkLevelCompleted(WORLD_TITAN, 1);
    if (Briefing_GetExitCode(WORLD_MIMAS, 0) == 1) {
        SendMail(WORLD_TITAN, 2, 0);
        if (gLanguage != 2)                           // no German mail 3
            SendMail(WORLD_TITAN, 2, 3);
        SendMail(WORLD_TITAN, 2, 4);
        SendMail(WORLD_TITAN, 2, 5);
        SendMail(WORLD_TITAN, 2, 6);
        SendMail(WORLD_TITAN, 2, 7);
        SendMail(WORLD_TITAN, 2, 8);
        SendMail(WORLD_TITAN, 2, 9);
        SendMail(WORLD_MIMAS, 0, 1);
        return 0;
    }
    SendMail(WORLD_MIMAS, 0, 0);
    SendMail(WORLD_MIMAS, 0, 1);
    SendMail(WORLD_MIMAS, 0, 2);
    SendMail(WORLD_MIMAS, 0, 3);
    SendMail(WORLD_MIMAS, 0, 4);
    SendMail(WORLD_MIMAS, 0, 5);
    SendMail(WORLD_MIMAS, 0, 6);
    return 0;
}

// 0x10001200  (LevelGetEmailInfo 0x10001070)
static int32_t T2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 10)
        return 5;
    *from = t2b_mails[gLanguage][index].from;
    *subject = t2b_mails[gLanguage][index].subject;
    return t2b_mails[gLanguage][index].category;
}

// 0x10001280  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0x10001374)
static Window *T2B_ShowEmail(Window *root, int32_t index)
{
    t2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(t2b_win, &rectHTML);
    if ((uint32_t)index <= 9)
        WindowSetTextFile(t2b_win, index + 1);
    return t2b_win;
}

// 0x100013a0  LevelBriefing / LevelStopLevel: empty
static void T2B_Nothing(void)
{
}

// 0x10001090
static uint32_t T2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x100013a0, return 0. Port: fresh DLL data.
static int32_t T2B_Load(const char *dllPath)
{
    (void)dllPath;
    t2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 10, .out = t2b_mails },
    { 0 },
};

const brief_script_t brief_titan_2 = {
    .name            = "TITAN\\2",
    .get_version     = T2B_GetVersion,
    .load            = T2B_Load,
    .briefing        = T2B_Nothing,
    .get_email_info  = T2B_GetEmailInfo,
    .show_email      = T2B_ShowEmail,
    .show_briefing   = T2B_ShowBriefing,
    .show_debriefing = T2B_ShowDeBriefing,
    .stop            = T2B_Nothing,
    .text            = t2b_gametext,
};
