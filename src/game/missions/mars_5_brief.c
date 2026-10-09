// MISSIONS\MARS\5\brief.dll - faithful C port.
//
// 7 mails, briefing page, de-briefing: result 1 marks Mars 5 completed and sends Callisto 1 mails
// 0..8 (Callisto's brief offers fewer; the extra records are created all the same). Any other
// result sends nothing. Text records: 0 = briefing, 1..7 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_MARS     4  // missions 1A, 1B, 2, 3, 4, 5 -> index 0..5
#define WORLD_CALLISTO 7

// 0x10006030: [language][mail]
static BriefMail m5b_mails[3][7];

static Window *m5b_win;   // 0x1000a0e0

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *M5B_ShowBriefing(Window *pane)
{
    m5b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(m5b_win, &rectHTML);
    WindowSetTextFile(m5b_win, 0);
    return m5b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t M5B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_MARS, 5);
        for (int32_t i = 0; i <= 8; i++)
            SendMail(WORLD_CALLISTO, 0, i);
    }
    return 0;
}

// 0x100011a0  (LevelGetEmailInfo 0x10001070)
static int32_t M5B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 7)
        return 5;
    *from = m5b_mails[gLanguage][index].from;
    *subject = m5b_mails[gLanguage][index].subject;
    return m5b_mails[gLanguage][index].category;
}

// 0x10001220  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (jump table 0..6)
static Window *M5B_ShowEmail(Window *root, int32_t index)
{
    m5b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(m5b_win, &rectHTML);
    if ((uint32_t)index <= 6)
        WindowSetTextFile(m5b_win, index + 1);
    return m5b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void M5B_Nothing(void)
{
}

// 0x10001090
static uint32_t M5B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t M5B_Load(const char *dllPath)
{
    (void)dllPath;
    m5b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText m5b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 7, .out = m5b_mails },
    { 0 },
};

const brief_script_t brief_mars_5 = {
    .name            = "MARS\\5",
    .get_version     = M5B_GetVersion,
    .load            = M5B_Load,
    .briefing        = M5B_Nothing,
    .get_email_info  = M5B_GetEmailInfo,
    .show_email      = M5B_ShowEmail,
    .show_briefing   = M5B_ShowBriefing,
    .show_debriefing = M5B_ShowDeBriefing,
    .stop            = M5B_Nothing,
    .text            = m5b_gametext,
};
