// MISSIONS\CALLISTO\1\brief.dll - faithful C port.
//
// 9 mails, briefing page, de-briefing: result 1 unlocks Io 1 mails 0..8; any other result sends
// nothing. Text records: 0 = briefing, 1..9 = mails. This brief.dll's LevelGetVersion returns
// 0xa0000000 (harmless, mission-api.md 1.1).
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x10001330), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_CALLISTO 7
#define WORLD_IO       9

// 0x10006030: [language][mail]
static BriefMail c1b_mails[3][9];

static Window *c1b_win;   // 0x1000a6f0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *C1B_ShowBriefing(Window *pane)
{
    c1b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(c1b_win, &rectHTML);
    WindowSetTextFile(c1b_win, 0);
    return c1b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t C1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_CALLISTO, 0);
        for (int32_t i = 0; i <= 8; i++)              // (unrolled) Io 1 mails 0..8
            SendMail(WORLD_IO, 0, i);
    }
    return 0;
}

// 0x100011a0  (LevelGetEmailInfo 0x10001070)
static int32_t C1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 9)
        return 5;
    *from = c1b_mails[gLanguage][index].from;
    *subject = c1b_mails[gLanguage][index].subject;
    return c1b_mails[gLanguage][index].category;
}

// 0x10001220  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0x1000130c)
static Window *C1B_ShowEmail(Window *root, int32_t index)
{
    c1b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(c1b_win, &rectHTML);
    if ((uint32_t)index <= 8)
        WindowSetTextFile(c1b_win, index + 1);
    return c1b_win;
}

// 0x10001330  LevelBriefing / LevelStopLevel: empty
static void C1B_Nothing(void)
{
}

// 0x10001090
static uint32_t C1B_GetVersion(void)
{
    return 0xa0000000u;
}

// 0x10001000  two calls of the empty 0x100010a0, return 0. Port: fresh DLL data.
static int32_t C1B_Load(const char *dllPath)
{
    (void)dllPath;
    c1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText c1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 9, .out = c1b_mails },
    { 0 },
};

const brief_script_t brief_callisto_1 = {
    .name            = "CALLISTO\\1",
    .get_version     = C1B_GetVersion,
    .load            = C1B_Load,
    .briefing        = C1B_Nothing,
    .get_email_info  = C1B_GetEmailInfo,
    .show_email      = C1B_ShowEmail,
    .show_briefing   = C1B_ShowBriefing,
    .show_debriefing = C1B_ShowDeBriefing,
    .stop            = C1B_Nothing,
    .text            = c1b_gametext,
};
