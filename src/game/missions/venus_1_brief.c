// MISSIONS\VENUS\1\brief.dll - faithful C port.
//
// 8 mails, briefing page, de-briefing: result 1 unlocks Earth 1 mails 0, 1, 3..10 and Earth 2
// mail 0; result 2 unlocks Earth 3 mail 0, Earth 2 mails 0, 1 and Earth 1 mails 3..10 (Earth 1
// mail 2 is never sent from here). Text records: 0 = briefing, 1..8 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_VENUS 1
#define WORLD_EARTH 2

// 0x10006030: [language][mail] (0x104 bytes each)
static BriefMail v1b_mails[3][8];

static Window *v1b_win;   // 0x1000a3e0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *V1B_ShowBriefing(Window *pane)
{
    v1b_win = WindowCreate(0x402, pane);
    WindowSetRect(v1b_win, &rectHTML);
    WindowSetTextFile(v1b_win, 0);
    return v1b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030); results 1 and 2 share the tail from SendMail(EARTH, 0, 3)
static int32_t V1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    switch (result) {
    case 1:
        Briefing_MarkLevelCompleted(WORLD_VENUS, 0);
        SendMail(WORLD_EARTH, 0, 0);
        SendMail(WORLD_EARTH, 1, 0);
        SendMail(WORLD_EARTH, 0, 1);
        break;
    case 2:
        Briefing_MarkLevelCompleted(WORLD_VENUS, 0);
        SendMail(WORLD_EARTH, 2, 0);
        SendMail(WORLD_EARTH, 1, 0);
        SendMail(WORLD_EARTH, 1, 1);
        break;
    default:
        return 0;
    }
    for (int32_t i = 3; i <= 10; i++)
        SendMail(WORLD_EARTH, 0, i);
    return 0;
}

// 0x100011f0  (LevelGetEmailInfo 0x10001070)
static int32_t V1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 8)
        return 5;
    *from = v1b_mails[gLanguage][index].from;
    *subject = v1b_mails[gLanguage][index].subject;
    return v1b_mails[gLanguage][index].category;
}

// 0x10001260  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..7)
static Window *V1B_ShowEmail(Window *root, int32_t index)
{
    v1b_win = WindowCreate(0x402, root);
    WindowSetRect(v1b_win, &rectHTML);
    if ((uint32_t)index <= 7)
        WindowSetTextFile(v1b_win, index + 1);
    return v1b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void V1B_Nothing(void)
{
}

// 0x10001090
static uint32_t V1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t V1B_Load(const char *dllPath)
{
    (void)dllPath;
    v1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText v1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 8, .out = v1b_mails },
    { 0 },
};

const brief_script_t brief_venus_1 = {
    .name            = "VENUS\\1",
    .get_version     = V1B_GetVersion,
    .load            = V1B_Load,
    .briefing        = V1B_Nothing,
    .get_email_info  = V1B_GetEmailInfo,
    .show_email      = V1B_ShowEmail,
    .show_briefing   = V1B_ShowBriefing,
    .show_debriefing = V1B_ShowDeBriefing,
    .stop            = V1B_Nothing,
    .text            = v1b_gametext,
};
