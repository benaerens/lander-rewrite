// MISSIONS\RHEA\1\brief.dll - faithful C port.
//
// 7 mails, briefing page, de-briefing: result 1 marks Rhea 1 completed and unlocks Titan 2 mails
// 0, 1, 4..8 and Mimas 1 mail 0; any other result sends nothing. Text records: 0 = briefing,
// 1..7 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_TITAN 11
#define WORLD_MIMAS 12
#define WORLD_RHEA  13

// 0x10006030: [language][mail]
static BriefMail rh1b_mails[3][7];

static Window *rh1b_win;   // 0x1000a0e0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *RH1B_ShowBriefing(Window *pane)
{
    rh1b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(rh1b_win, &rectHTML);
    WindowSetTextFile(rh1b_win, 0);
    return rh1b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t RH1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_RHEA, 0);
        SendMail(WORLD_TITAN, 1, 0);
        SendMail(WORLD_TITAN, 1, 1);
        SendMail(WORLD_TITAN, 1, 4);
        SendMail(WORLD_TITAN, 1, 5);
        SendMail(WORLD_TITAN, 1, 6);
        SendMail(WORLD_TITAN, 1, 7);
        SendMail(WORLD_TITAN, 1, 8);
        SendMail(WORLD_MIMAS, 0, 0);
    }
    return 0;
}

// 0x10001190  (LevelGetEmailInfo 0x10001070)
static int32_t RH1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 7)
        return 5;
    *from = rh1b_mails[gLanguage][index].from;
    *subject = rh1b_mails[gLanguage][index].subject;
    return rh1b_mails[gLanguage][index].category;
}

// 0x10001210  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..6, jump table 0x100012c8)
static Window *RH1B_ShowEmail(Window *root, int32_t index)
{
    rh1b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(rh1b_win, &rectHTML);
    if ((uint32_t)index <= 6)
        WindowSetTextFile(rh1b_win, index + 1);
    return rh1b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void RH1B_Nothing(void)
{
}

// 0x10001090
static uint32_t RH1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t RH1B_Load(const char *dllPath)
{
    (void)dllPath;
    rh1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText rh1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 7, .out = rh1b_mails },
    { 0 },
};

const brief_script_t brief_rhea_1 = {
    .name            = "RHEA\\1",
    .get_version     = RH1B_GetVersion,
    .load            = RH1B_Load,
    .briefing        = RH1B_Nothing,
    .get_email_info  = RH1B_GetEmailInfo,
    .show_email      = RH1B_ShowEmail,
    .show_briefing   = RH1B_ShowBriefing,
    .show_debriefing = RH1B_ShowDeBriefing,
    .stop            = RH1B_Nothing,
    .text            = rh1b_gametext,
};
