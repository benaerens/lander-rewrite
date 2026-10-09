// MISSIONS\MARS\1A\brief.dll - faithful C port.
//
// 10 mails, briefing page, de-briefing: result 1 unlocks Mars 2 mails 0,1,3..7 (mail 2 is
// skipped) and Mars 3 mail 0. Any other result sends nothing. Text records: 0 = briefing,
// 1..10 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_MARS 4      // missions 1A, 1B, 2, 3, 4, 5 -> index 0..5

// 0x10006030: [language][mail]
static BriefMail mab_mails[3][10];

static Window *mab_win;   // 0x1000aa00

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *MAB_ShowBriefing(Window *pane)
{
    mab_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(mab_win, &rectHTML);
    WindowSetTextFile(mab_win, 0);
    return mab_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t MAB_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_MARS, 0);
        SendMail(WORLD_MARS, 2, 0);
        SendMail(WORLD_MARS, 2, 1);
        SendMail(WORLD_MARS, 2, 3);
        SendMail(WORLD_MARS, 2, 4);
        SendMail(WORLD_MARS, 2, 5);
        SendMail(WORLD_MARS, 2, 6);
        SendMail(WORLD_MARS, 2, 7);
        SendMail(WORLD_MARS, 3, 0);
    }
    return 0;
}

// 0x10001190  (LevelGetEmailInfo 0x10001070)
static int32_t MAB_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 10)
        return 5;
    *from = mab_mails[gLanguage][index].from;
    *subject = mab_mails[gLanguage][index].subject;
    return mab_mails[gLanguage][index].category;
}

// 0x10001210  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..9)
static Window *MAB_ShowEmail(Window *root, int32_t index)
{
    mab_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(mab_win, &rectHTML);
    if ((uint32_t)index <= 9)
        WindowSetTextFile(mab_win, index + 1);
    return mab_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void MAB_Nothing(void)
{
}

// 0x10001090
static uint32_t MAB_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t MAB_Load(const char *dllPath)
{
    (void)dllPath;
    mab_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText mab_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 10, .out = mab_mails },
    { 0 },
};

const brief_script_t brief_mars_1a = {
    .name            = "MARS\\1A",
    .get_version     = MAB_GetVersion,
    .load            = MAB_Load,
    .briefing        = MAB_Nothing,
    .get_email_info  = MAB_GetEmailInfo,
    .show_email      = MAB_ShowEmail,
    .show_briefing   = MAB_ShowBriefing,
    .show_debriefing = MAB_ShowDeBriefing,
    .stop            = MAB_Nothing,
    .text            = mab_gametext,
};
