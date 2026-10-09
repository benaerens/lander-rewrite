// MISSIONS\TITAN\1\brief.dll - faithful C port.
//
// 11 mails, briefing page, de-briefing: results 1 and 2 both archive this mission's job mail and
// unlock Rhea 1 mails 1..6 plus a Rhea job mail: result 1 (reactor core replaced) -> Rhea 1 mail 0,
// result 2 (core replaced and the bonus pod 0x10c0003 recovered) -> Rhea 2 mail 0. Text records:
// 0 = briefing, 1..11 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_TITAN 11
#define WORLD_RHEA  13

// 0x10006030: [language][mail]
static BriefMail t1b_mails[3][11];

static Window *t1b_win;   // 0x1000ad10

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *T1B_ShowBriefing(Window *pane)
{
    t1b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(t1b_win, &rectHTML);
    WindowSetTextFile(t1b_win, 0);
    return t1b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t T1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 2) {
        Briefing_MarkLevelCompleted(WORLD_TITAN, 0);
        SendMail(WORLD_RHEA, 1, 0);
    } else if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_TITAN, 0);
        SendMail(WORLD_RHEA, 0, 0);
    } else {
        return 0;
    }
    SendMail(WORLD_RHEA, 0, 1);                       // (shared tail 0x10001143)
    SendMail(WORLD_RHEA, 0, 2);
    SendMail(WORLD_RHEA, 0, 3);
    SendMail(WORLD_RHEA, 0, 4);
    SendMail(WORLD_RHEA, 0, 5);
    SendMail(WORLD_RHEA, 0, 6);
    return 0;
}

// 0x100011a0  (LevelGetEmailInfo 0x10001070)
static int32_t T1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 11)
        return 5;
    *from = t1b_mails[gLanguage][index].from;
    *subject = t1b_mails[gLanguage][index].subject;
    return t1b_mails[gLanguage][index].category;
}

// 0x10001220  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0x1000132c)
static Window *T1B_ShowEmail(Window *root, int32_t index)
{
    t1b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(t1b_win, &rectHTML);
    if ((uint32_t)index <= 10)
        WindowSetTextFile(t1b_win, index + 1);
    return t1b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void T1B_Nothing(void)
{
}

// 0x10001090
static uint32_t T1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of the empty 0x100010a0, return 0. Port: fresh DLL data.
static int32_t T1B_Load(const char *dllPath)
{
    (void)dllPath;
    t1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 11, .out = t1b_mails },
    { 0 },
};

const brief_script_t brief_titan_1 = {
    .name            = "TITAN\\1",
    .get_version     = T1B_GetVersion,
    .load            = T1B_Load,
    .briefing        = T1B_Nothing,
    .get_email_info  = T1B_GetEmailInfo,
    .show_email      = T1B_ShowEmail,
    .show_briefing   = T1B_ShowBriefing,
    .show_debriefing = T1B_ShowDeBriefing,
    .stop            = T1B_Nothing,
    .text            = t1b_gametext,
};
