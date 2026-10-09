// MISSIONS\TRAINING\2\brief.dll - faithful C port.
//
// One mail ("Training"), the briefing page, and a de-briefing sheet of the hit counts written by
// MISSIONR.DLL into gGame_ScoreData.training ([0..3] hits, [4..7] totals). Same code as
// training_1's brief except the score renderer (and the data addresses).
//
// DLL exports: LevelLoadLevel 0x10001000, LevelShowBriefing 0x10001010, LevelShowDeBriefing
// 0x10001020, LevelShowEmail 0x10001040, LevelGetEmailInfo 0x10001060, LevelBriefing and
// LevelStopLevel 0x10001080 (the empty 0x10001530), LevelGetVersion 0x10001090.
#include <stdio.h>
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// 0x1000a030: one mail per language, indexed [index + gLanguage]
static BriefMail t2b_mails[3];

// the score sheet per language: title, ducks, targets, barrels, tanks (formats: hits, total) and in
// English a last line
static const char *t2b_sheet[3][6];

static struct {
    char    title[256];       // 0x1000de80
    char    line[5][256];     // 0x1000df80..0x1000e47f
    Window *win;              // 0x1000ea80
    Window *debriefPane;      // 0x1000ea84
} st;

// 0x100010a0  (LevelShowBriefing 0x10001010)
static Window *T2B_ShowBriefing(Window *pane)
{
    st.win = WindowCreate(WIN_HTML, pane);
    WindowSetRect(st.win, &rectHTML);
    WindowSetTextFile(st.win, 0);
    return st.win;
}

// 0x100010f0  print centred on x in grey
static void T2B_PrintCentred(const char *s, int32_t x, int32_t y, int32_t font, void *otLayer)
{
    int32_t w, h;
    Font_GetStringLen(s, &w, &h, font);
    Font_Print(s, x - (w >> 1), y, 0x808080, font, otLayer);
}

// 0x10001140  g2dRenderCallback: title + 4 lines (5 in English: a joke line)
static void T2B_RenderSheet(void *otLayer)
{
    int32_t n = (gLanguage == 0) ? 6 : 5;
    Win_SetTransform(st.debriefPane);
    T2B_PrintCentred(st.title, 320, 150, 1, otLayer);
    for (int32_t i = 0; i < n - 1; i++)
        T2B_PrintCentred(st.line[i], 320, 190 + 20 * i, 1, otLayer);
    Win_SetTransform(NULL);
}

// 0x100011d0  score renderer (SetScoreRender)
static void T2B_ScoreRender(void)
{
    const int32_t *c = gGame_ScoreData.training;
    int lang = gLanguage == 1 || gLanguage == 2 ? gLanguage : 0;    // (a switch, English by default)
    const char *const *s = t2b_sheet[lang];
    sprintf(st.title, "%s", s[0]);        // (the DLL passes the title and the last line as the format;
    for (int i = 0; i < 4; i++)           // they have no '%')
        sprintf(st.line[i], s[1 + i], c[i], c[4 + i]);
    if (lang == 0)
        sprintf(st.line[4], "%s", s[5]);
    g2dRenderCallback = T2B_RenderSheet;
}

// 0x10001380  (LevelShowDeBriefing 0x10001020)
static int32_t T2B_ShowDeBriefing(Window *pane, int32_t result)
{
    (void)result;
    st.debriefPane = pane;
    SetScoreRender(T2B_ScoreRender);
    return 0;
}

// 0x100013a0  (LevelGetEmailInfo 0x10001060)
static int32_t T2B_GetEmailInfo(int32_t index, const char **from, const char **subject)
{
    if (index >= 1)
        return 5;
    *from = t2b_mails[index + gLanguage].from;
    *subject = t2b_mails[index + gLanguage].subject;
    return t2b_mails[index + gLanguage].category;
}

// 0x10001410  (LevelShowEmail 0x10001040): index 0..9 -> text record index + 1
static Window *T2B_ShowEmail(Window *root, int32_t index)
{
    st.win = WindowCreate(WIN_HTML, root);
    WindowSetRect(st.win, &rectHTML);
    if ((uint32_t)index <= 9)
        WindowSetTextFile(st.win, index + 1);
    return st.win;
}

// 0x10001530  LevelBriefing / LevelStopLevel: empty
static void T2B_Nothing(void)
{
}

// 0x10001090
static uint32_t T2B_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x10001000  two calls of 0x10001530, return 0. Port: fresh DLL data.
static int32_t T2B_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText t2b_gametext[] = {
    { .kind = GT_MAILS, .va = 0x1000a030, .count = 3, .out = t2b_mails },
    { .kind = GT_STRINGS, .va = 0x1000a470, .count = 1, .out = &t2b_sheet[0][0] },
    { .kind = GT_STRINGS, .va = 0x1000a458, .count = 1, .out = &t2b_sheet[0][1] },
    { .kind = GT_STRINGS, .va = 0x1000a440, .count = 1, .out = &t2b_sheet[0][2] },
    { .kind = GT_STRINGS, .va = 0x1000a428, .count = 1, .out = &t2b_sheet[0][3] },
    { .kind = GT_STRINGS, .va = 0x1000a410, .count = 1, .out = &t2b_sheet[0][4] },
    { .kind = GT_STRINGS, .va = 0x1000a3f8, .count = 1, .out = &t2b_sheet[0][5] },
    { .kind = GT_STRINGS, .va = 0x1000a38c, .count = 1, .out = &t2b_sheet[1][0] },
    { .kind = GT_STRINGS, .va = 0x1000a378, .count = 1, .out = &t2b_sheet[1][1] },
    { .kind = GT_STRINGS, .va = 0x1000a364, .count = 1, .out = &t2b_sheet[1][2] },
    { .kind = GT_STRINGS, .va = 0x1000a350, .count = 1, .out = &t2b_sheet[1][3] },
    { .kind = GT_STRINGS, .va = 0x1000a33c, .count = 1, .out = &t2b_sheet[1][4] },
    { .kind = GT_STRINGS, .va = 0x1000a3e8, .count = 1, .out = &t2b_sheet[2][0] },
    { .kind = GT_STRINGS, .va = 0x1000a3d4, .count = 1, .out = &t2b_sheet[2][1] },
    { .kind = GT_STRINGS, .va = 0x1000a3c0, .count = 1, .out = &t2b_sheet[2][2] },
    { .kind = GT_STRINGS, .va = 0x1000a3b0, .count = 1, .out = &t2b_sheet[2][3] },
    { .kind = GT_STRINGS, .va = 0x1000a39c, .count = 1, .out = &t2b_sheet[2][4] },
    { 0 },
};

const brief_script_t brief_training_2 = {
    .name            = "TRAINING\\2",
    .get_version     = T2B_GetVersion,
    .load            = T2B_Load,
    .briefing        = T2B_Nothing,
    .get_email_info  = T2B_GetEmailInfo,
    .show_email      = T2B_ShowEmail,
    .show_briefing   = T2B_ShowBriefing,
    .show_debriefing = T2B_ShowDeBriefing,
    .stop            = T2B_Nothing,
    .text            = t2b_gametext,
};
