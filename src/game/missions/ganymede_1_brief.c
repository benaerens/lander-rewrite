// MISSIONS\GANYMEDE\1\brief.dll - faithful C port.
//
// One mail. The table holds that mail once per language back to back and is indexed with
// index + gLanguage (like the training briefs). De-briefing: result 1 or 2 unlocks Europa 2 mails
// 0, 1, 3..7 (mail 2 is skipped). Text records: 0 = briefing, 1 = the mail.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelShowBriefing 0x10001010, LevelShowDeBriefing
// 0x10001020, LevelShowEmail 0x10001040, LevelGetEmailInfo 0x10001060, LevelBriefing and
// LevelStopLevel 0x10001080 (-> the empty 0x10001240), LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EUROPA   6
#define WORLD_GANYMEDE 8

// 0x10006030: [index + language]
static BriefMail g1b_mails[3];

static Window *g1b_win;   // 0x10008e90

// 0x100010a0  (LevelShowBriefing 0x10001010)
static Window *G1B_ShowBriefing(Window *pane)
{
    g1b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(g1b_win, &rectHTML);
    WindowSetTextFile(g1b_win, 0);
    return g1b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001020)
static int32_t G1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result > 0 && result <= 2) {
        Briefing_MarkLevelCompleted(WORLD_GANYMEDE, 0);
        SendMail(WORLD_EUROPA, 1, 0);
        SendMail(WORLD_EUROPA, 1, 1);
        for (int32_t i = 3; i <= 7; i++)              // (unrolled)
            SendMail(WORLD_EUROPA, 1, i);
    }
    return 0;
}

// 0x10001180  (LevelGetEmailInfo 0x10001060)
static int32_t G1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 1)
        return 5;
    *from = g1b_mails[index + gLanguage].from;
    *subject = g1b_mails[index + gLanguage].subject;
    return g1b_mails[index + gLanguage].category;
}

// 0x100011f0  (LevelShowEmail 0x10001040): mail 0 -> text record 1
static Window *G1B_ShowEmail(Window *root, int32_t index)
{
    g1b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(g1b_win, &rectHTML);
    if (index == 0)
        WindowSetTextFile(g1b_win, 1);
    return g1b_win;
}

// 0x10001240  LevelBriefing / LevelStopLevel: empty
static void G1B_Nothing(void)
{
}

// 0x10001090
static uint32_t G1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  LevelLoadLevel, returns 0. Port: fresh DLL data.
static int32_t G1B_Load(const char *dllPath)
{
    (void)dllPath;
    g1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText g1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3, .out = g1b_mails },
    { 0 },
};

const brief_script_t brief_ganymede_1 = {
    .name            = "GANYMEDE\\1",
    .get_version     = G1B_GetVersion,
    .load            = G1B_Load,
    .briefing        = G1B_Nothing,
    .get_email_info  = G1B_GetEmailInfo,
    .show_email      = G1B_ShowEmail,
    .show_briefing   = G1B_ShowBriefing,
    .show_debriefing = G1B_ShowDeBriefing,
    .stop            = G1B_Nothing,
    .text            = g1b_gametext,
};
