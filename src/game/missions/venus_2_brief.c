// MISSIONS\VENUS\2\brief.dll - faithful C port (the disc's BRIEFING.DLL is another build; the game
// loads brief.dll).
//
// 8 mails, briefing page, de-briefing: result 1 unlocks Earth 1 mail 0, result 2 Earth 3 mail 0;
// both then unlock Earth 2 mail 0 and Earth 1 mails 2..10. Text records: 0 = briefing, 1..8 =
// mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_VENUS 1
#define WORLD_EARTH 2

// 0x10006030: [language][mail] (0x104 bytes each)
static BriefMail v2b_mails[3][8];

static Window *v2b_win;   // 0x1000a3e0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *V2B_ShowBriefing(Window *pane)
{
    v2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(v2b_win, &rectHTML);
    WindowSetTextFile(v2b_win, 0);
    return v2b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030); results 1 and 2 differ only in the first mail
static int32_t V2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    switch (result) {
    case 1:
        Briefing_MarkLevelCompleted(WORLD_VENUS, 1);
        SendMail(WORLD_EARTH, 0, 0);
        break;
    case 2:
        Briefing_MarkLevelCompleted(WORLD_VENUS, 1);
        SendMail(WORLD_EARTH, 2, 0);
        break;
    default:
        return 0;
    }
    SendMail(WORLD_EARTH, 1, 0);
    for (int32_t i = 2; i <= 10; i++)
        SendMail(WORLD_EARTH, 0, i);
    return 0;
}

// 0x100011d0  (LevelGetEmailInfo 0x10001070)
static int32_t V2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 8)
        return 5;
    *from = v2b_mails[gLanguage][index].from;
    *subject = v2b_mails[gLanguage][index].subject;
    return v2b_mails[gLanguage][index].category;
}

// 0x10001240  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..7)
static Window *V2B_ShowEmail(Window *root, int32_t index)
{
    v2b_win = WindowCreate(0x402, root);
    WindowSetRect(v2b_win, &rectHTML);
    if ((uint32_t)index <= 7)
        WindowSetTextFile(v2b_win, index + 1);
    return v2b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void V2B_Nothing(void)
{
}

// 0x10001090
static uint32_t V2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t V2B_Load(const char *dllPath)
{
    (void)dllPath;
    v2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText v2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 8, .out = v2b_mails },
    { 0 },
};

const brief_script_t brief_venus_2 = {
    .name            = "VENUS\\2",
    .get_version     = V2B_GetVersion,
    .load            = V2B_Load,
    .briefing        = V2B_Nothing,
    .get_email_info  = V2B_GetEmailInfo,
    .show_email      = V2B_ShowEmail,
    .show_briefing   = V2B_ShowBriefing,
    .show_debriefing = V2B_ShowDeBriefing,
    .stop            = V2B_Nothing,
    .text            = v2b_gametext,
};
