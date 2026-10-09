// MISSIONS\IO\2\brief.dll - faithful C port.
//
// 2 mails, briefing page, de-briefing: result 1 unlocks Callisto 2 mails 0, 1, 3, 4, 6, 7, 8;
// result 2 (done in time) unlocks Ganymede 1 mail 0 and Callisto 2 mails 1, 3, 4, 5, 7, 8; both
// then archive this mission's job mail (the only brief that marks it after the SendMails). Text
// records: 0 = briefing, 1..2 = mails. LevelGetVersion returns 0xa0000000 (harmless).
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100012b0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_CALLISTO 7
#define WORLD_GANYMEDE 8
#define WORLD_IO       9

// 0x10006030: [language][mail]
static BriefMail i2b_mails[3][2];

static Window *i2b_win;   // 0x100091a0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *I2B_ShowBriefing(Window *pane)
{
    i2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(i2b_win, &rectHTML);
    WindowSetTextFile(i2b_win, 0);
    return i2b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t I2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 2) {
        SendMail(WORLD_GANYMEDE, 0, 0);
        SendMail(WORLD_CALLISTO, 1, 1);
        SendMail(WORLD_CALLISTO, 1, 3);
        SendMail(WORLD_CALLISTO, 1, 4);
        SendMail(WORLD_CALLISTO, 1, 5);
    } else if (result == 1) {
        SendMail(WORLD_CALLISTO, 1, 0);
        SendMail(WORLD_CALLISTO, 1, 1);
        SendMail(WORLD_CALLISTO, 1, 3);
        SendMail(WORLD_CALLISTO, 1, 4);
        SendMail(WORLD_CALLISTO, 1, 6);
    } else {
        return 0;
    }
    SendMail(WORLD_CALLISTO, 1, 7);                   // (shared tail)
    SendMail(WORLD_CALLISTO, 1, 8);
    Briefing_MarkLevelCompleted(WORLD_IO, 1);
    return 0;
}

// 0x100011d0  (LevelGetEmailInfo 0x10001070)
static int32_t I2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 2)
        return 5;
    *from = i2b_mails[gLanguage][index].from;
    *subject = i2b_mails[gLanguage][index].subject;
    return i2b_mails[gLanguage][index].category;
}

// 0x10001240  (LevelShowEmail 0x10001050): mail 0 -> record 1, mail 1 -> record 2
static Window *I2B_ShowEmail(Window *root, int32_t index)
{
    i2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(i2b_win, &rectHTML);
    if (index == 0)
        WindowSetTextFile(i2b_win, 1);
    else if (index == 1)
        WindowSetTextFile(i2b_win, 2);
    return i2b_win;
}

// 0x100012b0  LevelBriefing / LevelStopLevel: empty
static void I2B_Nothing(void)
{
}

// 0x10001090
static uint32_t I2B_GetVersion(void)
{
    return 0xa0000000u;
}

// 0x10001000  two calls of the empty 0x100010a0, return 0. Port: fresh DLL data.
static int32_t I2B_Load(const char *dllPath)
{
    (void)dllPath;
    i2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText i2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 2, .out = i2b_mails },
    { 0 },
};

const brief_script_t brief_io_2 = {
    .name            = "IO\\2",
    .get_version     = I2B_GetVersion,
    .load            = I2B_Load,
    .briefing        = I2B_Nothing,
    .get_email_info  = I2B_GetEmailInfo,
    .show_email      = I2B_ShowEmail,
    .show_briefing   = I2B_ShowBriefing,
    .show_debriefing = I2B_ShowDeBriefing,
    .stop            = I2B_Nothing,
    .text            = i2b_gametext,
};
