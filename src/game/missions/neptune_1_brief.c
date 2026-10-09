// MISSIONS\NEPTUNE\1\brief.dll - faithful C port.
//
// 14 mails, briefing page, de-briefing: result 1 marks Neptune 1 completed and unlocks Pluto mails
// 0..6. Mail 8 is a lottery win: opening it the first time (Briefing_HasEmailBeenRead) adds
// 20,000,000 to the player's cash (Lander_GetCash/SetCash(-1): the front end's player state).
// Text records: 0 = briefing, 1..14 = mails.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelBriefing and LevelStopLevel 0x10001010 (-> the
// empty 0x100010a0), LevelShowBriefing 0x10001020, LevelShowDeBriefing 0x10001030,
// LevelShowEmail 0x10001050, LevelGetEmailInfo 0x10001070, LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_NEPTUNE 15
#define WORLD_PLUTO   19

// 0x10006030: [language][mail]
static BriefMail ne1b_mails[3][14];

static Window *ne1b_win;   // 0x1000b630

// 0x100010b0  (LevelShowBriefing 0x10001020)
static Window *NE1B_ShowBriefing(Window *pane)
{
    ne1b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(ne1b_win, &rectHTML);
    WindowSetTextFile(ne1b_win, 0);
    return ne1b_win;
}

// 0x10001100  (LevelShowDeBriefing 0x10001030)
static int32_t NE1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_NEPTUNE, 0);
        for (int32_t i = 0; i <= 6; i++)
            SendMail(WORLD_PLUTO, 0, i);
    }
    return 0;
}

// 0x10001180  (LevelGetEmailInfo 0x10001070)
static int32_t NE1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 14)
        return 5;
    *from = ne1b_mails[gLanguage][index].from;
    *subject = ne1b_mails[gLanguage][index].subject;
    return ne1b_mails[gLanguage][index].category;
}

// 0x10001210  (LevelShowEmail 0x10001050): mail i -> text record i + 1 (0..13, jump table
// 0x10001384); mail 8 pays the lottery win once
static Window *NE1B_ShowEmail(Window *root, int32_t index)
{
    ne1b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(ne1b_win, &rectHTML);
    if ((uint32_t)index > 13)
        return ne1b_win;
    if (index == 8 && !Briefing_HasEmailBeenRead(WORLD_NEPTUNE, 0, 8))      // 0x100012da
        Lander_SetCash(OBJ_NONE, Lander_GetCash(OBJ_NONE) + 20000000);
    WindowSetTextFile(ne1b_win, index + 1);
    return ne1b_win;
}

// 0x100010a0  LevelBriefing / LevelStopLevel: empty
static void NE1B_Nothing(void)
{
}

// 0x10001090
static uint32_t NE1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100010a0, return 0. Port: fresh DLL data.
static int32_t NE1B_Load(const char *dllPath)
{
    (void)dllPath;
    ne1b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText ne1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 14, .out = ne1b_mails },
    { 0 },
};

const brief_script_t brief_neptune_1 = {
    .name            = "NEPTUNE\\1",
    .get_version     = NE1B_GetVersion,
    .load            = NE1B_Load,
    .briefing        = NE1B_Nothing,
    .get_email_info  = NE1B_GetEmailInfo,
    .show_email      = NE1B_ShowEmail,
    .show_briefing   = NE1B_ShowBriefing,
    .show_debriefing = NE1B_ShowDeBriefing,
    .stop            = NE1B_Nothing,
    .text            = ne1b_gametext,
};
