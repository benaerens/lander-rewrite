// MISSIONS\PLUTO\1\brief.dll - faithful C port.
//
// 8 mails, briefing page, de-briefing: result 1 or 2 marks Pluto completed and sends Pluto mail 7
// (the Lander team's congratulations). Text records: 0 = briefing, 1..8 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelShowBriefing 0x10001010, LevelShowDeBriefing
// 0x10001020, LevelShowEmail 0x10001040, LevelGetEmailInfo 0x10001060, LevelBriefing and
// LevelStopLevel 0x10001080 (-> the empty 0x100012a0), LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_PLUTO 19

// 0x10006030: [language][mail]
static BriefMail plb_mails[3][8];

static Window *plb_win;   // 0x1000a3e0

// 0x100010a0  (LevelShowBriefing 0x10001010)
static Window *PLB_ShowBriefing(Window *pane)
{
    plb_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(plb_win, &rectHTML);
    WindowSetTextFile(plb_win, 0);
    return plb_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001020)
static int32_t PLB_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result > 0 && result <= 2) {
        Briefing_MarkLevelCompleted(WORLD_PLUTO, 0);
        SendMail(WORLD_PLUTO, 0, 7);
    }
    return 0;
}

// 0x10001130  (LevelGetEmailInfo 0x10001060)
static int32_t PLB_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 8)
        return 5;
    *from = plb_mails[gLanguage][index].from;
    *subject = plb_mails[gLanguage][index].subject;
    return plb_mails[gLanguage][index].category;
}

// 0x100011a0  (LevelShowEmail 0x10001040): mail i -> text record i + 1 (0..7, jump table 0x10001274)
static Window *PLB_ShowEmail(Window *root, int32_t index)
{
    plb_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(plb_win, &rectHTML);
    if ((uint32_t)index <= 7)
        WindowSetTextFile(plb_win, index + 1);
    return plb_win;
}

// 0x100012a0  LevelBriefing / LevelStopLevel: empty
static void PLB_Nothing(void)
{
}

// 0x10001090
static uint32_t PLB_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100012a0, return 0. Port: fresh DLL data.
static int32_t PLB_Load(const char *dllPath)
{
    (void)dllPath;
    plb_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText plb_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 8, .out = plb_mails },
    { 0 },
};

const brief_script_t brief_pluto_1 = {
    .name            = "PLUTO\\1",
    .get_version     = PLB_GetVersion,
    .load            = PLB_Load,
    .briefing        = PLB_Nothing,
    .get_email_info  = PLB_GetEmailInfo,
    .show_email      = PLB_ShowEmail,
    .show_briefing   = PLB_ShowBriefing,
    .show_debriefing = PLB_ShowDeBriefing,
    .stop            = PLB_Nothing,
    .text            = plb_gametext,
};
