// MISSIONS\EARTH\2\brief.dll - faithful C port.
//
// 8 mails, briefing page, de-briefing that depends on how Venus went: if a Venus mission ended
// with result 1 (block A) or 2 (block B), results 1 / 2 of this mission unlock Earth 4, Earth 3
// and missing Earth 1/2 mails depending on Earth 1 / Earth 3 results. In block A the result-1
// code runs on into the result-2 code (no break in the original). SetScoreRender is called last.
// Text records: 0 = briefing, 1..8 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100015e0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_VENUS 1
#define WORLD_EARTH 2

// 0x10006030: [language][mail] (0x104 bytes each)
static BriefMail e2b_mails[3][8];

static Window *e2b_win;   // 0x1000a3e0

// 0x100010a0  (LevelShowBriefing 0x10001020)
static Window *E2B_ShowBriefing(Window *pane)
{
    e2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(e2b_win, &rectHTML);
    WindowSetTextFile(e2b_win, 0);
    return e2b_win;
}

static void E2B_SendEarth4(void)        // Earth 4 mails 0, 3..6 (a run the original repeats inline)
{
    SendMail(WORLD_EARTH, 3, 0);
    SendMail(WORLD_EARTH, 3, 3);
    SendMail(WORLD_EARTH, 3, 4);
    SendMail(WORLD_EARTH, 3, 5);
    SendMail(WORLD_EARTH, 3, 6);
}

static void E2B_SendEarth2Rest(void)    // Earth 2 mails 2..7 (repeated inline in the original)
{
    for (int32_t i = 2; i <= 7; i++)
        SendMail(WORLD_EARTH, 1, i);
}

// 0x100010f0  (LevelShowDeBriefing 0x10001030)
static int32_t E2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    if (Briefing_GetExitCode(WORLD_VENUS, 0) == 1 || Briefing_GetExitCode(WORLD_VENUS, 1) == 1) {
        switch (result) {
        case 1:                                                     // 0x10001144
            Briefing_MarkLevelCompleted(WORLD_EARTH, 1);
            if (Briefing_GetExitCode(WORLD_EARTH, 0) == 1) {
                E2B_SendEarth4();
            } else {
                SendMail(WORLD_EARTH, 1, 1);
                SendMail(WORLD_EARTH, 0, 0);
                E2B_SendEarth2Rest();
            }
            SendMail(WORLD_EARTH, 2, 4);
            // the original runs on into the result-2 code
            // fall through
        case 2:                                                   // 0x100011f0
            Briefing_MarkLevelCompleted(WORLD_EARTH, 1);
            SendMail(WORLD_EARTH, 3, 2);
            if (Briefing_GetExitCode(WORLD_EARTH, 0) == 1) {
                E2B_SendEarth4();
            } else {
                SendMail(WORLD_EARTH, 1, 1);
                SendMail(WORLD_EARTH, 0, 0);
                E2B_SendEarth2Rest();
            }
            SendMail(WORLD_EARTH, 2, 3);
            SendMail(WORLD_EARTH, 2, 4);
            break;
        default:
            break;
        }
    }
    if (Briefing_GetExitCode(WORLD_VENUS, 0) == 2 || Briefing_GetExitCode(WORLD_VENUS, 1) == 2) {
        switch (result) {
        case 1:                                                     // 0x100013b7
            Briefing_MarkLevelCompleted(WORLD_EARTH, 1);
            if (Briefing_GetExitCode(WORLD_EARTH, 2) >= 1) {
                E2B_SendEarth4();
            } else {
                SendMail(WORLD_EARTH, 2, 0);
                E2B_SendEarth2Rest();
            }
            break;
        case 2:                                                     // 0x100012eb
            Briefing_MarkLevelCompleted(WORLD_EARTH, 1);
            SendMail(WORLD_EARTH, 3, 2);
            if (Briefing_GetExitCode(WORLD_EARTH, 2) >= 1) {
                E2B_SendEarth4();
                SendMail(WORLD_EARTH, 2, 4);
                SendMail(WORLD_EARTH, 2, 3);
            } else {
                SendMail(WORLD_EARTH, 2, 0);
                E2B_SendEarth2Rest();
                SendMail(WORLD_EARTH, 2, 3);
                SendMail(WORLD_EARTH, 2, 4);
            }
            break;
        default:
            break;
        }
    }
    SetScoreRender(Briefing_ShowScore);
    return 0;
}

// 0x10001470  (LevelGetEmailInfo 0x10001070)
static int32_t E2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 8)
        return 5;
    *from = e2b_mails[gLanguage][index].from;
    *subject = e2b_mails[gLanguage][index].subject;
    return e2b_mails[gLanguage][index].category;
}

// 0x100014e0  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..7)
static Window *E2B_ShowEmail(Window *root, int32_t index)
{
    e2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(e2b_win, &rectHTML);
    if ((uint32_t)index <= 7)
        WindowSetTextFile(e2b_win, index + 1);
    return e2b_win;
}

// 0x100015e0  LevelBriefing / LevelStopLevel: empty
static void E2B_Nothing(void)
{
}

// 0x10001090
static uint32_t E2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100015e0, return 0. Port: fresh DLL data.
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

const brief_script_t brief_earth_2 = {
    .name            = "EARTH\\2",
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
