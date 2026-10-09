// MISSIONS\MARS\3\brief.dll - faithful C port.
//
// 8 mails, briefing page, de-briefing: result 1 marks Mars 3 completed, then if Mars 2 was already
// won (exit code 1) it sends Mars 4 mails 0, 2..6 and 1, otherwise Mars 2 mail 0, Mars 4 mail 1 and
// Mars 3 mails 2, 4, 5, 6. Any other result sends nothing. Text records: 0 = briefing, 1..8 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x10001350), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_MARS 4      // missions 1A, 1B, 2, 3, 4, 5 -> index 0..5

// 0x10006030: [language][mail]
static BriefMail m3b_mails[3][8];

static Window *m3b_win;   // 0x1000a3e0

// 0x100010a0  (LevelShowBriefing 0x10001020)
static Window *M3B_ShowBriefing(Window *pane)
{
    m3b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(m3b_win, &rectHTML);
    WindowSetTextFile(m3b_win, 0);
    return m3b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001030)
static int32_t M3B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result != 1)
        return 0;
    Briefing_MarkLevelCompleted(WORLD_MARS, 3);
    if (Briefing_GetExitCode(WORLD_MARS, 2) == 1) {
        SendMail(WORLD_MARS, 4, 0);
        SendMail(WORLD_MARS, 4, 2);
        SendMail(WORLD_MARS, 4, 3);
        SendMail(WORLD_MARS, 4, 4);
        SendMail(WORLD_MARS, 4, 5);
        SendMail(WORLD_MARS, 4, 6);
        SendMail(WORLD_MARS, 4, 1);
        return 0;
    }
    SendMail(WORLD_MARS, 2, 0);
    SendMail(WORLD_MARS, 4, 1);
    SendMail(WORLD_MARS, 3, 2);
    SendMail(WORLD_MARS, 3, 4);
    SendMail(WORLD_MARS, 3, 5);
    SendMail(WORLD_MARS, 3, 6);
    return 0;
}

// 0x100011e0  (LevelGetEmailInfo 0x10001070)
static int32_t M3B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 8)
        return 5;
    *from = m3b_mails[gLanguage][index].from;
    *subject = m3b_mails[gLanguage][index].subject;
    return m3b_mails[gLanguage][index].category;
}

// 0x10001250  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0..7)
static Window *M3B_ShowEmail(Window *root, int32_t index)
{
    m3b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(m3b_win, &rectHTML);
    if ((uint32_t)index <= 7)
        WindowSetTextFile(m3b_win, index + 1);
    return m3b_win;
}

// 0x10001350  LevelBriefing / LevelStopLevel: empty
static void M3B_Nothing(void)
{
}

// 0x10001090
static uint32_t M3B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x10001350, return 0. Port: fresh DLL data.
static int32_t M3B_Load(const char *dllPath)
{
    (void)dllPath;
    m3b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m3b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 8, .out = m3b_mails },
    { 0 },
};

const brief_script_t brief_mars_3 = {
    .name            = "MARS\\3",
    .get_version     = M3B_GetVersion,
    .load            = M3B_Load,
    .briefing        = M3B_Nothing,
    .get_email_info  = M3B_GetEmailInfo,
    .show_email      = M3B_ShowEmail,
    .show_briefing   = M3B_ShowBriefing,
    .show_debriefing = M3B_ShowDeBriefing,
    .stop            = M3B_Nothing,
    .text            = m3b_gametext,
};
