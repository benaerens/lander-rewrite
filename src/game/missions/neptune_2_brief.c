// MISSIONS\NEPTUNE\2\brief.dll - faithful C port.
//
// One mail, briefing page, de-briefing: result 1 marks Neptune level 1 (this mission) completed and
// sends the Pluto mails 0..6 (as Neptune 1 does). Text records: 0 = briefing, 1 = the mail.
// The mail table (0x10006030) holds inline records {char from[0x80]; char subject[0x80]; int
// category} (0x104 bytes), one per language; LevelGetEmailInfo indexes it with index + language.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelShowBriefing 0x10001010 (-> 0x100010a0),
// LevelShowDeBriefing 0x10001020 (-> 0x100010f0), LevelShowEmail 0x10001040 (-> 0x100011e0),
// LevelGetEmailInfo 0x10001060 (-> 0x10001170), LevelBriefing and LevelStopLevel 0x10001080
// (-> the empty 0x10001230), LevelGetVersion 0x10001090.
#include "mission_lib.h"
#include "missions.h"

#define WORLD_NEPTUNE 15
#define WORLD_PLUTO   19

// 0x10006030: [language][mail]
static BriefMail ne2b_mails[3][1];

static Window *ne2b_win;   // 0x10008e90

// 0x100010a0  (LevelShowBriefing 0x10001010)
static Window *NE2B_ShowBriefing(Window *pane)
{
    ne2b_win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(ne2b_win, &rectHTML);
    WindowSetTextFile(ne2b_win, 0);
    return ne2b_win;
}

// 0x100010f0  (LevelShowDeBriefing 0x10001020)
static int32_t NE2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)pane;
    SetScoreRender(Briefing_ShowScore);
    if (result == 1) {
        Briefing_MarkLevelCompleted(WORLD_NEPTUNE, 1);
        for (int32_t i = 0; i <= 6; i++)
            SendMail(WORLD_PLUTO, 0, i);
    }
    return 0;
}

// 0x10001170  (LevelGetEmailInfo 0x10001060)
static int32_t NE2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 1)
        return 5;
    *from = ne2b_mails[gLanguage][index].from;
    *subject = ne2b_mails[gLanguage][index].subject;
    return ne2b_mails[gLanguage][index].category;
}

// 0x100011e0  (LevelShowEmail 0x10001040): mail 0 -> text record 1
static Window *NE2B_ShowEmail(Window *root, int32_t index)
{
    ne2b_win = WindowCreate(WIN_HTML, root);
    WindowSetRect(ne2b_win, &rectHTML);
    if (index == 0)
        WindowSetTextFile(ne2b_win, 1);
    return ne2b_win;
}

// 0x10001230  LevelBriefing / LevelStopLevel: empty
static void NE2B_Nothing(void)
{
}

// 0x10001090
static uint32_t NE2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x10001230, return 0. Port: fresh DLL data.
static int32_t NE2B_Load(const char *dllPath)
{
    (void)dllPath;
    ne2b_win = NULL;
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText ne2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x10006030, .count = 3 * 1, .out = ne2b_mails },
    { 0 },
};

const brief_script_t brief_neptune_2 = {
    .name            = "NEPTUNE\\2",
    .get_version     = NE2B_GetVersion,
    .load            = NE2B_Load,
    .briefing        = NE2B_Nothing,
    .get_email_info  = NE2B_GetEmailInfo,
    .show_email      = NE2B_ShowEmail,
    .show_briefing   = NE2B_ShowBriefing,
    .show_debriefing = NE2B_ShowDeBriefing,
    .stop            = NE2B_Nothing,
    .text            = ne2b_gametext,
};
