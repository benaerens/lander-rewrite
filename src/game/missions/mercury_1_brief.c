// MISSIONS\MERCURY\1\brief.dll - faithful C port.
//
// Mail list (13 mails, 12 for French/German), briefing page, de-briefing: on success unlock the
// Venus mails (result 1: Venus 1 mails 0-7; result 2: Venus 2 mails 0-6 and Venus 1 mail 7).
// Text records: 0 = briefing, 1..13 = the mails (current text file = MISSIONS\MERCURY\1\TEXT*.DAT).
//
// DLL exports: LevelLoadLevel 0x10001000, LevelShowBriefing 0x10001010, LevelShowDeBriefing
// 0x10001020, LevelShowEmail 0x10001040, LevelGetEmailInfo 0x10001060, LevelBriefing and
// LevelStopLevel 0x10001080 (both the empty 0x100013f0), LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_MERCURY 0
#define WORLD_VENUS   1

// 0x10006030: [language][mail] (0x104 bytes each)
static BriefMail m1b_mails[3][13];

static Window *m1b_win;   // 0x1000b320

// 0x100010a0  (LevelShowBriefing 0x10001010)
static Window *M1B_ShowBriefing(Window *pane)
{
    m1b_win = WindowCreate(0x402, pane);
    WindowSetRect(m1b_win, &rectHTML);
    WindowSetTextFile(m1b_win, 0);
    return m1b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001020)
static int32_t M1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    switch (result) {
    case 0:
        break;
    case 1:
        Briefing_MarkLevelCompleted(WORLD_MERCURY, 0);
        for (int32_t i = 0; i <= 7; i++)
            SendMail(WORLD_VENUS, 0, i);          // Venus 1 mails 0..7
        break;
    case 2:
        Briefing_MarkLevelCompleted(WORLD_MERCURY, 0);
        for (int32_t i = 0; i <= 6; i++)
            SendMail(WORLD_VENUS, 1, i);          // Venus 2 mails 0..6
        SendMail(WORLD_VENUS, 0, 7);              // (the code falls into the tail of case 1)
        break;
    default:
        break;
    }
    return 0;
}

// 0x100011f0  (LevelGetEmailInfo 0x10001060): 13 mails in English, 12 otherwise
static int32_t M1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (gLanguage == 0) {
        if (index >= 13)
            return 5;
    } else if (index >= 12) {
        return 5;
    }
    *from = m1b_mails[gLanguage][index].from;
    *subject = m1b_mails[gLanguage][index].subject;
    return m1b_mails[gLanguage][index].category;
}

// 0x10001290  (LevelShowEmail 0x10001040): mail i -> text record i + 1 (0..12)
static Window *M1B_ShowEmail(Window *root, int32_t index)
{
    m1b_win = WindowCreate(0x402, root);
    WindowSetRect(m1b_win, &rectHTML);
    if ((uint32_t)index <= 12)
        WindowSetTextFile(m1b_win, index + 1);
    return m1b_win;
}

// 0x100013f0  LevelBriefing / LevelStopLevel (0x10001080): empty
static void M1B_Nothing(void)
{
}

// 0x10001090
static uint32_t M1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100013f0, return 0. Port: fresh DLL data.
static int32_t M1B_Load(const char *dllPath)
{
    (void)dllPath;
    m1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 13, .out = m1b_mails },
    { 0 },
};

const brief_script_t brief_mercury_1 = {
    .name            = "MERCURY\\1",
    .get_version     = M1B_GetVersion,
    .load            = M1B_Load,
    .briefing        = M1B_Nothing,
    .get_email_info  = M1B_GetEmailInfo,
    .show_email      = M1B_ShowEmail,
    .show_briefing   = M1B_ShowBriefing,
    .show_debriefing = M1B_ShowDeBriefing,
    .stop            = M1B_Nothing,
    .text            = m1b_gametext,
};
