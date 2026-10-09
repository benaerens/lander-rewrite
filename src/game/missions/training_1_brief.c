// MISSIONS\TRAINING\1\brief.dll - faithful C port.
//
// One mail ("Training"), the briefing page, and a de-briefing score sheet of the five stage
// times written by MISSIONR.DLL into gGame_ScoreData.training[0..4], drawn by its own 2D
// callback over the de-brief pane. No SendMail: training never unlocks anything.
//
// DLL exports: LevelLoadLevel 0x10001000, LevelShowBriefing 0x10001010, LevelShowDeBriefing
// 0x10001020, LevelShowEmail 0x10001040, LevelGetEmailInfo 0x10001060, LevelBriefing and
// LevelStopLevel 0x10001080 (the empty 0x100014e0), LevelGetVersion 0x10001090.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// 0x1000a030: par times (seconds) per stage
static const int32_t t1b_par[5] = { 50, 25, 25, 35, 110 };

// 0x1000a048: one mail per language, indexed [index + gLanguage] by LevelGetEmailInfo
static BriefMail t1b_mails[3];

// the score sheet per language: title, "not completed" line, stage line (formats)
static const char *t1b_sheet[3][3];

static struct {
    char    title[256];       // 0x1000de60
    char    line[5][256];     // 0x1000df60..0x1000e45f
    Window *win;              // 0x1000ea60
    Window *debriefPane;      // 0x1000ea64
} st;

// 0x100010a0  (LevelShowBriefing 0x10001010)
static Window *T1B_ShowBriefing(Window *pane)
{
    st.win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(st.win, &rectHTML);
    WindowSetTextFile(st.win, 0);
    return st.win;
}

// 0x100010f0  print centred on x in grey
static void T1B_PrintCentred(const char *s, int32_t x, int32_t y, int32_t font, void *otLayer)
{
    int32_t w, h;
    Font_GetStringLen(s, &w, &h, font);
    Font_Print(s, x - (w >> 1), y, 0x808080, font, otLayer);
}

// 0x10001140  g2dRenderCallback: draw the sheet in the de-brief pane's space
static void T1B_RenderSheet(void *otLayer)
{
    Win_SetTransform(st.debriefPane);
    T1B_PrintCentred(st.title, 320, 150, 1, otLayer);
    for (int i = 0; i < 5; i++)
        T1B_PrintCentred(st.line[i], 320, 190 + 20 * i, 1, otLayer);
    Win_SetTransform(NULL);
}

// 0x100011b0  score renderer (SetScoreRender): format the sheet, install the 2D callback
static void T1B_ScoreRender(void)
{
    const int32_t *times = gGame_ScoreData.training;
    int lang = gLanguage == 1 || gLanguage == 2 ? gLanguage : 0;    // (a switch, English by default)
    const char *title = t1b_sheet[lang][0], *notDone = t1b_sheet[lang][1], *done = t1b_sheet[lang][2];
    sprintf(st.title, "%s", title);       // the original passes the title as the format string
    for (int i = 0; i < 5; i++) {
        if (times[i] == 0)
            sprintf(st.line[i], notDone, i + 1);
        else
            sprintf(st.line[i], done, i + 1, t1b_par[i], times[i]);
    }
    g2dRenderCallback = T1B_RenderSheet;
}

// 0x10001330  (LevelShowDeBriefing 0x10001020)
static int32_t T1B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)result;
    st.debriefPane = pane;
    SetScoreRender(T1B_ScoreRender);
    return 0;
}

// 0x10001350  (LevelGetEmailInfo 0x10001060)
static int32_t T1B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 1)
        return 5;
    *from = t1b_mails[index + gLanguage].from;
    *subject = t1b_mails[index + gLanguage].subject;
    return t1b_mails[index + gLanguage].category;
}

// 0x100013c0  (LevelShowEmail 0x10001040): index 0..9 -> text record index + 1
static Window *T1B_ShowEmail(Window *root, int32_t index)
{
    st.win = WindowCreate(WIN_HTML, root);
    WindowSetRect(st.win, &rectHTML);
    if ((uint32_t)index <= 9)
        WindowSetTextFile(st.win, index + 1);
    return st.win;
}

// 0x100014e0  LevelBriefing / LevelStopLevel: empty
static void T1B_Nothing(void)
{
}

// 0x10001090
static uint32_t T1B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x100014e0, return 0. Port: fresh DLL data.
static int32_t T1B_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t1b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x1000a048, .count = 3, .out = t1b_mails },
    { .kind = GT_STRINGS, .va = 0x1000a458, .count = 1, .out = &t1b_sheet[0][0] },
    { .kind = GT_STRINGS, .va = 0x1000a43c, .count = 1, .out = &t1b_sheet[0][1] },
    { .kind = GT_STRINGS, .va = 0x1000a414, .count = 1, .out = &t1b_sheet[0][2] },
    { .kind = GT_STRINGS, .va = 0x1000a398, .count = 1, .out = &t1b_sheet[1][0] },
    { .kind = GT_STRINGS, .va = 0x1000a37c, .count = 1, .out = &t1b_sheet[1][1] },
    { .kind = GT_STRINGS, .va = 0x1000a354, .count = 1, .out = &t1b_sheet[1][2] },
    { .kind = GT_STRINGS, .va = 0x1000a404, .count = 1, .out = &t1b_sheet[2][0] },
    { .kind = GT_STRINGS, .va = 0x1000a3e4, .count = 1, .out = &t1b_sheet[2][1] },
    { .kind = GT_STRINGS, .va = 0x1000a3bc, .count = 1, .out = &t1b_sheet[2][2] },
    { 0 },
};

const brief_script_t brief_training_1 = {
    .name            = "TRAINING\\1",
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
