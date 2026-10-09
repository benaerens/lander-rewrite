// MISSIONS\CALLISTO\2\brief.dll - faithful C port.
//
// 9 mails, briefing page, de-briefing: result 1 unlocks Europa 2 mails 0, 2..7 (mail 1 is
// skipped); any other result sends nothing. Text records: 0 = briefing, 1..9 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EUROPA   6
#define WORLD_CALLISTO 7

// 0x10006030: [language][mail]
static BriefMail c2b_mails[3][9];

static Window *c2b_win;   // 0x1000a6f0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *C2B_ShowBriefing(Window *pane)
{
    c2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(c2b_win, &rectHTML);
    WindowSetTextFile(c2b_win, 0);
    return c2b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t C2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_CALLISTO, 1);
        SendMail(WORLD_EUROPA, 1, 0);
        for (int32_t i = 2; i <= 7; i++)              // (unrolled)
            SendMail(WORLD_EUROPA, 1, i);
    }
    return 0;
}

// 0x10001180  (LevelGetEmailInfo 0x10001070)
static int32_t C2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 9)
        return 5;
    *from = c2b_mails[gLanguage][index].from;
    *subject = c2b_mails[gLanguage][index].subject;
    return c2b_mails[gLanguage][index].category;
}

// 0x10001200  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0x100012ec)
static Window *C2B_ShowEmail(Window *root, int32_t index)
{
    c2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(c2b_win, &rectHTML);
    if ((uint32_t)index <= 8)
        WindowSetTextFile(c2b_win, index + 1);
    return c2b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void C2B_Nothing(void)
{
}

// 0x10001090
static uint32_t C2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t C2B_Load(const char *dllPath)
{
    (void)dllPath;
    c2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText c2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 9, .out = c2b_mails },
    { 0 },
};

const brief_script_t brief_callisto_2 = {
    .name            = "CALLISTO\\2",
    .get_version     = C2B_GetVersion,
    .load            = C2B_Load,
    .briefing        = C2B_Nothing,
    .get_email_info  = C2B_GetEmailInfo,
    .show_email      = C2B_ShowEmail,
    .show_briefing   = C2B_ShowBriefing,
    .show_debriefing = C2B_ShowDeBriefing,
    .stop            = C2B_Nothing,
    .text            = c2b_gametext,
};
