// The front end of Lander (Psygnosis 1999), ported from the D3D exe 0x401000-0x40d000: the
// "in-ship computer" with its hub icons, category menus, screens (options, load / save, mail,
// briefing, shop, de-brief), the mail system that drives the campaign and the player state.
// Spec: re/specs/frontend.md. The UI system it is built on is src/game/ui.c + fe_controls.c.
#ifndef FE_H
#define FE_H

#include "../game/game.h"
#include "../game/ui.h"

// ---------------------------------------------------------------- program flow (main.c <-> FE)
// FE_Run 0x409fc0 as a per-frame state: fe_begin(entry, lastResult), then fe_frame() every frame
// until it returns a result: 1 play gCurWorld/gCurMission, 3 quit, 6 attract demo.
enum { FE_ENTRY_FRESH = 0, FE_ENTRY_LOADED = 1, FE_ENTRY_AFTER_MISSION = 2 };
enum { FE_RESULT_NONE = 0, FE_RESULT_PLAY = 1, FE_RESULT_QUIT = 3, FE_RESULT_NET = 4, FE_RESULT_DEMO = 6 };
void fe_init_once(void);                   // Game_Startup's front-end part + FE_InitOnce 0x408fc0
void fe_begin(int entry, int last_result); // Campaign_FrontEnd 0x410370 + FE_Run set-up
int fe_frame(void);                        // one FE_Run loop iteration; FE_RESULT_NONE while running
void fe_end(void);                         // FE_Run exit + FE_UnloadResources

extern int32_t gCurWorld, gCurMission;     // 0x49bf6c.. mission to play
extern int32_t gFESelWorld, gFESelMission; // 0x58372c / 0x583708 mission of the selected mail
extern int32_t gLastMissionResult;         // 0x490c20 (-1 = none yet)
extern int32_t gFrontEndEntry;             // 0x49bf64
extern uint32_t gSoftFruit;                // 0x55137c cheat bits (registry; --cheats N here)
extern int32_t gPlayerCashAtLaunch;        // cash when the mission was launched (save games)
extern LanderPlayerState gPlayerStateBackup;

// ---------------------------------------------------------------- strings (Lang_GetString 0x426920)
// lander.EXE's text, read at start-up (fe_strings.c, gametext.c)
const char *Text_Get(int id);
extern const char *fe_soundtrack_url;      // the Audio screen's second message line (0x48f954)

// ---------------------------------------------------------------- mail (frontend_api.c)
typedef struct {            // gMailRecords 0x579620, 0x50 bytes each
	uint8_t world, mission, index, read;
	uint8_t category;       // 1 Business, 2 News, 4 Personal, 5 none, 0xff not fetched yet
	uint8_t pad;
	int16_t seq;
	char from[20];          // +0x08
	char subject[52];       // +0x1c
} MailRecord;
#define MAIL_MAX 512
extern MailRecord gMailRecords[MAIL_MAX];
extern int32_t gMailCount;                 // 0x583638
extern int32_t gMailReplayPoint;           // 0x49b... (Briefing_SetReplayPoint)
extern int32_t gMailCountAtDebrief;        // 0x578418
extern int8_t gMissionResults[23 * 6];     // 0x583640
extern const int32_t gnNumMissionsForLevel[23];   // 0x48fad8

void Mail_Clear(void);                     // 0x407920
void Results_Clear(void);                  // 0x407950
void Mail_AddPlaceholders(int world, int mission);   // 0x407870
void Mail_FetchInfo(void);                 // 0x408160
void Mail_RollbackToReplayPoint(int replay, int count);  // 0x408230
void Briefing_MarkEmailAsRead(int32_t world, int32_t mission, int32_t index); // 0x407d10
void Briefing_SetReplayPoint(int32_t world, int32_t mission);                 // 0x407d80
void Campaign_InitMail(void);              // 0x408cf0
void Player_NewGame(void);                 // 0x407bb0
extern ScoreRenderFn gScoreRenderFn;       // 0x61f300

// the brief.dll of a mission (BriefDll_* 0x47b640..): loaded on demand like the original
const brief_script_t *fe_brief(void);      // the loaded one (NULL = none)
bool FE_LoadBriefDll(int world, int mission);        // 0x408060 (+ LevelBriefing)
void FE_LoadMissionTextPack(int world, int mission); // 0x4080b0 (world -1 = MISC\TEXT*.DAT)
void FE_LoadMissionPreview(int world, int mission);  // 0x404090 (the world texfile for HTML images)
const mission_def_t *fe_mission_def(int world, int mission);
int32_t BriefDll_GetEmailInfo(int32_t index, const char **from, const char **subject);
Window *BriefDll_ShowEmail(Window *root, int32_t index);
Window *BriefDll_ShowBriefing(Window *pane);
int32_t BriefDll_ShowDeBriefing(Window *pane, int32_t result);

// ---------------------------------------------------------------- front-end internals (fe_main.c)
typedef struct {            // screen descriptor gFEScreenDesc 0x5836e0 (FE_GetScreenDesc 0x406ae0)
	void (*enter)(void);
	void (*exit)(void);
	UiBarData *tabs;
	void (*tab_create)(void);
	void (*tab_destroy)(void);
	UiBarData *buttons;
	void (*button_action)(int index);
	const char *title;
} FeScreenDesc;
extern FeScreenDesc gFEScreenDesc;
extern int32_t gFECurScreen, gFECurCategory, gFEExitFlag, gFEResult;
extern int32_t gFEListMode;                // DAT_0049b280: 1 Business, 2 News, 3 Personal, 4..8 shop lists
extern int32_t gFENewMailMenu;             // DAT_0048f9e4: 1 = NEW MAIL menu active, 0 = ARCHIVE
extern int32_t gFEGameSaved;               // DAT_0048f9e0: 1 = nothing to save
extern int32_t gMailState;                 // DAT_005837c4: 0 list, 1 mail, 2 briefing, 3 START
extern Window *fe_root, *fe_pane, *fe_html, *fe_list, *fe_tabbar, *fe_buttonbar;
extern UiListDesc fe_list_desc;            // DAT_00583860
extern UiListData *fe_list_data;           // DAT_00583864
extern const char *fe_screen_titles[16];   // DAT_0049ab10
extern UiSprite fe_sprite6;                // {0, gGlobalTextures, 6, 0} (0x48f068)
extern const UiRect fe_bar_skin[16];       // 0x48f100

void FE_CreatePane(void);                  // 0x40a3c0
void FE_SetPaneRect(const WinRect *r);     // 0x40a370 (also rectHTML)
void FE_CreateHtmlWindow(void);            // 0x40a440
void FE_DestroyHtmlWindow(void);           // 0x40a480
void FE_OpenScreen(int screen);            // 0x408780
void FE_CloseScreen(void);                 // 0x408710
int FE_OpenCategory(int cat);              // 0x4084d0
void FE_CloseCategory(void);               // 0x408460
void FE_BuildHub(Window *root, int layout);// 0x406850
void FE_DestroyHub(void);                  // 0x406790
void FE_SetHub(Window *root, int layout, Window *highlight);   // 0x406980
void FE_HubHighlight(Window *w);           // 0x4068f0
void FE_CreateTabBar(UiBarData *tabs);     // 0x402490
void FE_DestroyTabBar(void);               // 0x402500
void FE_CreateButtonBar(UiBarData *b);     // 0x402520
void FE_DestroyButtonBar(void);            // 0x402590
void FE_DestroyList(void);                 // 0x402620
void FE_ShowMessage(const char *s);        // 0x407200
void FE_ShowMessage2(const char *s);       // 0x4072c0
void FE_HideMessage(void);                 // 0x407350
void FE_LaunchMission(void);               // 0x409b20
void FE_DebriefDone(void);                 // 0x409e50
void FE_RestartGame(void);                 // 0x4062c0
void FE_Enter(int entry, int last_result); // 0x408a30
void FE_Rebuild(int entry);                // 0x408c20
void FE_Quit(void);                        // 0x40a2a0
void FE_ArmDemoTimeout(void);              // 0x408a20
void fe_sound(int sid);                    // FUN_0047bdd0(-1, sid): front-end UI sound
void fe_format_cash(char *out, int32_t v); // FUN_00404b70 (appends)

// fe_mail.c
void Mail_Open(void);                      // 0x404670
void Mail_OpenBusiness(int index);         // 0x404710
void Mail_OpenNewsPersonal(int index);     // 0x4047f0
void Mail_ScreenEnter(void);               // 0x4043c0
void Mail_ScreenExit(void);                // 0x4043e0
void Mail_ScreenCreate(void);              // 0x404400 (Business)
void Mail_ScreenDestroy(void);             // 0x4045e0
void Mail_NewsCreate(void);                // 0x404760
void Mail_PersonalCreate(void);            // 0x4048b0
void Mail_OtherDestroy(void);              // 0x4047c0
void Mail_ListReset(void);                 // 0x404660
void fe_list_create(UiListData *d);        // 0x404980 (shop list)

// fe_shop.c
void LanderConfigs_Init(void);             // 0x4079c0
void Shop_ShowItem(int index);             // 0x404cc0
void Shop_Buy(void);                       // 0x405300
void Shop_Sell(void);                      // 0x405d60
void Shop_TabCreate(void), Shop_TabDestroy(void);   // screen 12 tab fns (by the current tab)
void Shop_ButtonAction(int index);
void Shop_HideItem(void);                  // 0x404af0
void Sell_Enter(void), Sell_Exit(void);    // screen 13
void Diag_Create(void), Diag_Destroy(void);// screen 14
void fe_shop_button3(Window *c);           // FUN_004016f0: the BUY / SELL button
extern UiBarData fe_buy_tabs, fe_buy_buttons;

// fe_screens.c
void Audio_TabCreate(void), Audio_TabDestroy(void);
void Visual_TabCreate(void), Visual_TabDestroy(void);
void Controls_Enter(void), Controls_Exit(void), Controls_TabCreate(void), Controls_TabDestroy(void);
void Advanced_TabCreate(void), Advanced_TabDestroy(void);   // Controls' ADVANCED tab (0x403ca0 / 0x403cb0)
void fe_controls_list_event(void);         // FE_OnControlEvent, CTL_LIST2 (_DAT_0048f4e0 = 1)
void fe_spinner_event(Window *c, intptr_t data);   // FUN_00401670
void Load_Enter(void), Load_Exit(void), Load_Action(int);
void Save_Enter(void), Save_Exit(void), Save_Action(int);
void Restart_Enter(void), Quit_Enter(void), Confirm_Exit(void), Quit_Action(int);
void Restart_Action(int);
void Credits_Enter(void);
void Debrief_Enter(void), Debrief_Exit(void);
void fe_screens_init(void);
void fe_slots_event(Window *c, int value, int state);   // the load / save list
void fe_edit_event(Window *c, int value);                // UI_LocalizeStrings part
extern UiBarData fe_audio_tabs, fe_visual_tabs, fe_controls_tabs, fe_ok_tab, fe_empty_tabs, fe_mail_tabs;
extern UiBarData fe_load_buttons, fe_save_buttons;
extern int32_t gSfxVolume, gMusicVolume;   // 0..0x1000 ("SFX Volume" / "Music Volume")
extern int32_t gFogDistance;               // "Fog Distance": the view distance of the missions
extern int32_t gBrightness;
extern int32_t gDemoTimeoutMs;             // "Demo Timeout" 0x48f9f8: idle ms on the title before the attract demo
void Config_Load(void);                    // Config_LoadRegistry 0x482180 (LANDER.CFG here)
void Config_Save(void);                    // Config_SaveRegistry 0x482430
void fe_ctrlconf_load_disc(void);          // CtrlConf_Load's CD fallback (the data folder's CTRLCONF.DAT)
void fe_text_pack_forget(void);
void fe_preview_forget(void);
Window *fe_mail_updown(UiUpDownDesc *d, const WinRect *r, bool create);

// fe_save.c (SAVEGAME.DAT, Save_* 0x41c320..)
#define SAVE_SLOTS 44
int Save_Load(int slot);                   // 0 error, 1 empty slot, 2 loaded
int Save_Write(int slot, const char *name);
void Save_ReadNames(void);                 // 0x41cc90
char *Save_SlotNameBuf(int slot);          // the slot's name ("" = empty), edited in place by the Save screen
const char *Save_SlotName(int slot);
int32_t Save_GetReplayCash(void);
int32_t fe_lander_fuel(int type);          // ArrayOfLanderConfigs[type].fuel

// fe_ingame.c: the in-game pause / quit dialogs and the game-over message (PauseMenu_Open 0x407420..)
void PauseMenu_Open(void);
void PauseMenu_Close(void);
void QuitConfirm_Open(void);
void QuitConfirm_Close(void);
void GameOverMsg_Show(int dead);
void GameOverMsg_Hide(void);
extern int32_t gExitLanderRequested;       // 0x49c574

#endif
