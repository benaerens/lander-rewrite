// MISSIONS\MIMAS\1\brief.dll - faithful C port.
//
// 7 mails, briefing page, de-briefing: result 1 marks Mimas 1 completed; then, if Titan 2 ended
// with result 1, Titan 3 mails 0, 3 (not in German), 4..9 and 1; otherwise Titan 2 mail 0, Titan 3
// mail 1 and Mimas 1 mails 1..6. Any other result sends nothing. Text records: 0 = briefing,
// 1..7 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_TITAN 11
#define WORLD_MIMAS 12

// 0x10006030: [language][mail]
static BriefMail mib_mails[3][7];

static Window *mib_win;   // 0x1000a0e0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *MIB_ShowBriefing(Window *pane)
{
    mib_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(mib_win, &rectHTML);
    WindowSetTextFile(mib_win, 0);
    return mib_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t MIB_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result != 1)
        return 0;
    Briefing_MarkLevelCompleted(WORLD_MIMAS, 0);
    if (Briefing_GetExitCode(WORLD_TITAN, 1) == 1) {
        SendMail(WORLD_TITAN, 2, 0);
        if (gLanguage != 2)
            SendMail(WORLD_TITAN, 2, 3);
        SendMail(WORLD_TITAN, 2, 4);
        SendMail(WORLD_TITAN, 2, 5);
        SendMail(WORLD_TITAN, 2, 6);
        SendMail(WORLD_TITAN, 2, 7);
        SendMail(WORLD_TITAN, 2, 8);
        SendMail(WORLD_TITAN, 2, 9);
        SendMail(WORLD_TITAN, 2, 1);
        return 0;
    }
    SendMail(WORLD_TITAN, 1, 0);
    SendMail(WORLD_TITAN, 2, 1);
    SendMail(WORLD_MIMAS, 0, 1);
    SendMail(WORLD_MIMAS, 0, 2);
    SendMail(WORLD_MIMAS, 0, 3);
    SendMail(WORLD_MIMAS, 0, 4);
    SendMail(WORLD_MIMAS, 0, 5);
    SendMail(WORLD_MIMAS, 0, 6);
    return 0;
}

// 0x10001210  (LevelGetEmailInfo 0x10001070)
static int32_t MIB_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 7)
        return 5;
    *from = mib_mails[gLanguage][index].from;
    *subject = mib_mails[gLanguage][index].subject;
    return mib_mails[gLanguage][index].category;
}

// 0x10001290  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..6, jump table 0x10001348)
static Window *MIB_ShowEmail(Window *root, int32_t index)
{
    mib_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(mib_win, &rectHTML);
    if ((uint32_t)index <= 6)
        WindowSetTextFile(mib_win, index + 1);
    return mib_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void MIB_Nothing(void)
{
}

// 0x10001090
static uint32_t MIB_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t MIB_Load(const char *dllPath)
{
    (void)dllPath;
    mib_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText mib_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 7, .out = mib_mails },
    { 0 },
};

const brief_script_t brief_mimas_1 = {
    .name            = "MIMAS\\1",
    .get_version     = MIB_GetVersion,
    .load            = MIB_Load,
    .briefing        = MIB_Nothing,
    .get_email_info  = MIB_GetEmailInfo,
    .show_email      = MIB_ShowEmail,
    .show_briefing   = MIB_ShowBriefing,
    .show_debriefing = MIB_ShowDeBriefing,
    .stop            = MIB_Nothing,
    .text            = mib_gametext,
};
