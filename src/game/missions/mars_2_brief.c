// MISSIONS\MARS\2\brief.dll - faithful C port.
//
// 8 mails, briefing page, de-briefing: result 1 marks Mars 2 completed, then if Mars 3 was already
// won (exit code 1) it sends Mars 4 mails 0, 2..6 and Mars 3 mails 1 and 3, otherwise Mars 3 mails
// 0..6. Any other result sends nothing. Text records: 0 = briefing, 1..8 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x10001360), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_MARS 4      // missions 1A, 1B, 2, 3, 4, 5 -> index 0..5

// 0x10006030: [language][mail]
static BriefMail m2b_mails[3][8];

static Window *m2b_win;   // 0x1000a3e0

// 0x100010a0  (LevelShowBriefing 0x10001020)
static Window *M2B_ShowBriefing(Window *pane)
{
    m2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(m2b_win, &rectHTML);
    WindowSetTextFile(m2b_win, 0);
    return m2b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001030)
static int32_t M2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result != 1)
        return 0;
    Briefing_MarkLevelCompleted(WORLD_MARS, 2);
    if (Briefing_GetExitCode(WORLD_MARS, 3) == 1) {
        SendMail(WORLD_MARS, 4, 0);
        SendMail(WORLD_MARS, 4, 2);
        SendMail(WORLD_MARS, 4, 3);
        SendMail(WORLD_MARS, 4, 4);
        SendMail(WORLD_MARS, 4, 5);
        SendMail(WORLD_MARS, 4, 6);
        SendMail(WORLD_MARS, 3, 1);
        SendMail(WORLD_MARS, 3, 3);
        return 0;
    }
    SendMail(WORLD_MARS, 3, 0);
    SendMail(WORLD_MARS, 3, 1);
    SendMail(WORLD_MARS, 3, 2);
    SendMail(WORLD_MARS, 3, 3);
    SendMail(WORLD_MARS, 3, 4);
    SendMail(WORLD_MARS, 3, 5);
    SendMail(WORLD_MARS, 3, 6);
    return 0;
}

// 0x100011f0  (LevelGetEmailInfo 0x10001070)
static int32_t M2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 8)
        return 5;
    *from = m2b_mails[gLanguage][index].from;
    *subject = m2b_mails[gLanguage][index].subject;
    return m2b_mails[gLanguage][index].category;
}

// 0x10001260  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0..7)
static Window *M2B_ShowEmail(Window *root, int32_t index)
{
    m2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(m2b_win, &rectHTML);
    if ((uint32_t)index <= 7)
        WindowSetTextFile(m2b_win, index + 1);
    return m2b_win;
}

// 0x10001360  LevelBriefing / LevelStopLevel: empty
static void M2B_Nothing(void)
{
}

// 0x10001090
static uint32_t M2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x10001360, return 0. Port: fresh DLL data.
static int32_t M2B_Load(const char *dllPath)
{
    (void)dllPath;
    m2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 8, .out = m2b_mails },
    { 0 },
};

const brief_script_t brief_mars_2 = {
    .name            = "MARS\\2",
    .get_version     = M2B_GetVersion,
    .load            = M2B_Load,
    .briefing        = M2B_Nothing,
    .get_email_info  = M2B_GetEmailInfo,
    .show_email      = M2B_ShowEmail,
    .show_briefing   = M2B_ShowBriefing,
    .show_debriefing = M2B_ShowDeBriefing,
    .stop            = M2B_Nothing,
    .text            = m2b_gametext,
};
