// MISSIONS\MARS\1B\brief.dll - faithful C port.
//
// 3 mails, briefing page, de-briefing: result 1 unlocks Mars 2 mails 0, 2..7 (mail 1 is skipped;
// mars_1a skips mail 2 instead) and Mars 3 mail 0. Any other result sends nothing. Text records:
// 0 = briefing, 1..3 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_MARS 4      // missions 1A, 1B, 2, 3, 4, 5 -> index 0..5

// 0x10006030: [language][mail]
static BriefMail mbb_mails[3][3];

static Window *mbb_win;   // 0x100094b0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *MBB_ShowBriefing(Window *pane)
{
    mbb_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(mbb_win, &rectHTML);
    WindowSetTextFile(mbb_win, 0);
    return mbb_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t MBB_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_MARS, 1);
        SendMail(WORLD_MARS, 2, 0);
        SendMail(WORLD_MARS, 2, 2);
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
static int32_t MBB_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 3)
        return 5;
    *from = mbb_mails[gLanguage][index].from;
    *subject = mbb_mails[gLanguage][index].subject;
    return mbb_mails[gLanguage][index].category;
}

// 0x10001210  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..2)
static Window *MBB_ShowEmail(Window *root, int32_t index)
{
    mbb_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(mbb_win, &rectHTML);
    switch (index) {
    case 0: WindowSetTextFile(mbb_win, 1); break;
    case 1: WindowSetTextFile(mbb_win, 2); break;
    case 2: WindowSetTextFile(mbb_win, 3); break;
    default: break;
    }
    return mbb_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void MBB_Nothing(void)
{
}

// 0x10001090
static uint32_t MBB_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t MBB_Load(const char *dllPath)
{
    (void)dllPath;
    mbb_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText mbb_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 3, .out = mbb_mails },
    { 0 },
};

const brief_script_t brief_mars_1b = {
    .name            = "MARS\\1B",
    .get_version     = MBB_GetVersion,
    .load            = MBB_Load,
    .briefing        = MBB_Nothing,
    .get_email_info  = MBB_GetEmailInfo,
    .show_email      = MBB_ShowEmail,
    .show_briefing   = MBB_ShowBriefing,
    .show_debriefing = MBB_ShowDeBriefing,
    .stop            = MBB_Nothing,
    .text            = mbb_gametext,
};
