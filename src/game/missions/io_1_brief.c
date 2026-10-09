// MISSIONS\IO\1\brief.dll - faithful C port.
//
// 9 mails, briefing page, de-briefing: result 1 unlocks Europa 1 mails 0..5; result 2 (done in
// time) unlocks Io 2 mails 0, 1 and Europa 1 mails 2..5. Opening mail 2
// while it is still unread costs the player 5 credits. Text records: 0 = briefing, 1..9 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_EUROPA 6
#define WORLD_IO     9

// 0x10006030: [language][mail]
static BriefMail i1b_mails[3][9];

static Window *i1b_win;   // 0x1000a6f0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *I1B_ShowBriefing(Window *pane)
{
    i1b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(i1b_win, &rectHTML);
    WindowSetTextFile(i1b_win, 0);
    return i1b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t I1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 2) {
        Briefing_MarkLevelCompleted(WORLD_IO, 0);
        SendMail(WORLD_IO, 1, 0);
        SendMail(WORLD_IO, 1, 1);
    } else if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_IO, 0);
        SendMail(WORLD_EUROPA, 0, 0);
        SendMail(WORLD_EUROPA, 0, 1);
    } else {
        return 0;
    }
    for (int32_t i = 2; i <= 5; i++)                  // (shared tail)
        SendMail(WORLD_EUROPA, 0, i);
    return 0;
}

// 0x100011b0  (LevelGetEmailInfo 0x10001070)
static int32_t I1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 9)
        return 5;
    *from = i1b_mails[gLanguage][index].from;
    *subject = i1b_mails[gLanguage][index].subject;
    return i1b_mails[gLanguage][index].category;
}

// 0x10001230  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0x10001348)
static Window *I1B_ShowEmail(Window *root, int32_t index)
{
    i1b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(i1b_win, &rectHTML);
    if ((uint32_t)index > 8)
        return i1b_win;
    if (index == 2 && Briefing_HasEmailBeenRead(WORLD_IO, 0, 2) == 0)
        Lander_SetCash((ObjId)-1, Lander_GetCash((ObjId)-1) - 5);   // the birthday "gift"
    WindowSetTextFile(i1b_win, index + 1);
    return i1b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void I1B_Nothing(void)
{
}

// 0x10001090
static uint32_t I1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t I1B_Load(const char *dllPath)
{
    (void)dllPath;
    i1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText i1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 9, .out = i1b_mails },
    { 0 },
};

const brief_script_t brief_io_1 = {
    .name            = "IO\\1",
    .get_version     = I1B_GetVersion,
    .load            = I1B_Load,
    .briefing        = I1B_Nothing,
    .get_email_info  = I1B_GetEmailInfo,
    .show_email      = I1B_ShowEmail,
    .show_briefing   = I1B_ShowBriefing,
    .show_debriefing = I1B_ShowDeBriefing,
    .stop            = I1B_Nothing,
    .text            = i1b_gametext,
};
