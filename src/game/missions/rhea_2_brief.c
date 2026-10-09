// MISSIONS\RHEA\2\brief.dll - faithful C port.
//
// 2 mails, briefing page, de-briefing: result 1 or 2 marks Rhea 2 completed and unlocks Titan 2
// mails 0, 2 (result 1) or 3 (result 2), 4..8 and Mimas 1 mail 0. (The MISSIONR.DLL only ever ends
// with result 1.) Text records: 0 = briefing, 1..2 = mails.
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
static BriefMail rh2b_mails[3][2];

static Window *rh2b_win;   // 0x100091a0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *RH2B_ShowBriefing(Window *pane)
{
    rh2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(rh2b_win, &rectHTML);
    WindowSetTextFile(rh2b_win, 0);
    return rh2b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t RH2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result != 1 && result != 2)
        return 0;
    Briefing_MarkLevelCompleted(WORLD_RHEA, 1);
    SendMail(WORLD_TITAN, 1, 0);
    SendMail(WORLD_TITAN, 1, result == 2 ? 3 : 2);
    SendMail(WORLD_TITAN, 1, 4);
    SendMail(WORLD_TITAN, 1, 5);
    SendMail(WORLD_TITAN, 1, 6);
    SendMail(WORLD_TITAN, 1, 7);
    SendMail(WORLD_TITAN, 1, 8);
    SendMail(WORLD_MIMAS, 0, 0);
    return 0;
}

// 0x100011c0  (LevelGetEmailInfo 0x10001070)
static int32_t RH2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 2)
        return 5;
    *from = rh2b_mails[gLanguage][index].from;
    *subject = rh2b_mails[gLanguage][index].subject;
    return rh2b_mails[gLanguage][index].category;
}

// 0x10001230  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..1)
static Window *RH2B_ShowEmail(Window *root, int32_t index)
{
    rh2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(rh2b_win, &rectHTML);
    if (index == 0)
        WindowSetTextFile(rh2b_win, 1);
    else if (index == 1)
        WindowSetTextFile(rh2b_win, 2);
    return rh2b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void RH2B_Nothing(void)
{
}

// 0x10001090
static uint32_t RH2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t RH2B_Load(const char *dllPath)
{
    (void)dllPath;
    rh2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText rh2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 2, .out = rh2b_mails },
    { 0 },
};

const brief_script_t brief_rhea_2 = {
    .name            = "RHEA\\2",
    .get_version     = RH2B_GetVersion,
    .load            = RH2B_Load,
    .briefing        = RH2B_Nothing,
    .get_email_info  = RH2B_GetEmailInfo,
    .show_email      = RH2B_ShowEmail,
    .show_briefing   = RH2B_ShowBriefing,
    .show_debriefing = RH2B_ShowDeBriefing,
    .stop            = RH2B_Nothing,
    .text            = rh2b_gametext,
};
