// The ship systems: Buy (screen 12: LANDER / WEAPONS / UPGRADE / OTHER), Sell (13) and Diagnostics
// (14). Shop_GetPrice 0x404a00, Shop_ShowItem 0x404cc0, Shop_FillList 0x405030, Shop_Buy 0x405300,
// the Buy tab functions 0x4055b0-0x405960, Sell 0x4059e0-0x405e20, Shop_ShowEquipment 0x405ef0,
// LanderConfigs_Init 0x4079c0. Prices and configurations are the exe's tables.
#include <stdio.h>
#include <string.h>

#include "fe.h"

// ---------------------------------------------------------------- tables

typedef struct {            // ArrayOfLanderConfigs 0x5790a0, 0x2c bytes each
	int32_t unk0;
	int32_t fuel;           // +0x04 tank size (4.12... fuel units; Diagnostics shows fuel >> 4 litres)
	int32_t unk8;
	uint32_t weapons;       // +0x0c weapon ids the lander can fit (bit = id)
	uint32_t armour;        // +0x10 armour ids
	int32_t index;          // +0x14
	uint32_t engines;       // +0x18 engine ids
	int32_t def_primary;    // +0x1c defaults when the current part does not fit
	int32_t def_secondary;  // +0x20
	int32_t def_engine;     // +0x24
	int32_t def_armour;     // +0x28
} LanderConfig;

static LanderConfig configs[5];

void LanderConfigs_Init(void) { // 0x4079c0
	configs[0] = (LanderConfig){0, 0x180000, 100, 0x400606, 0x48, 0, 0x20b, 10, 0x16, 9, 6};
	configs[1] = (LanderConfig){10, 0x200000, 400, 0x408e4e, 0x48, 1, 0x20b, 0xb, 0xf, 9, 6};
	configs[2] = (LanderConfig){0x1e, 0x300000, 1000, 0x408e4e, 0x1ec, 2, 0x61f, 0xb, 0xf, 10, 8};
	configs[3] = (LanderConfig){0x1e, 0x240000, 1000, 0x41bede, 0x1ec, 3, 0x61f, 0xd, 0x10, 10, 8};
	configs[4] = (LanderConfig){0x1e, 0x260000, 1000, 0x43fbfa, 0x7fe, 4, 0xfff, 0xe, 0x11, 0xb, 10};
}

int32_t fe_lander_fuel(int type) { return type >= 0 && type < 5 ? configs[type].fuel : configs[0].fuel; }

static const int32_t weapons_cost[24] = {0, 8000, 12000, 16000, 19000, 38000, 20000, 30000, 50000, 12000, 20000,
	60000, 45000, 60000, 95000, 30000, 60000, 100000, 10000, 10000, 10000, 300000, 30000, 0};   // 0x48fa00
static const int32_t armour_cost[12] = {0, 50000, 30000, 15000, 70000, 40000, 20000, 80000, 70000, 160000, 150000, 0}; // 0x48fa60
static const int32_t engines_cost[12] = {15000, 30000, 50000, 50000, 65000, 90000, 80000, 150000, 250000, 80000,
	170000, 300000};                                                                             // 0x48fa90
static const int32_t landers_cost[5] = {200000, 400000, 700000, 1000000, 9999995};               // 0x48fac0
static int32_t other_cost[3];                                                                    // 0x49b1b0

// 0x48f6f0: per item {misc\text*.dat record, global model}; bases: weapons 0, armour 0x17,
// engines 0x22, landers 0x2e, other 0x33
static const int16_t item_info[55][2] = {
	{-1, -1}, {0, -1}, {1, -1}, {2, -1}, {3, -1}, {4, -1}, {5, -1}, {6, -1}, {7, -1}, {8, -1}, {9, -1}, {10, -1},
	{11, -1}, {12, -1}, {13, -1}, {14, -1}, {15, -1}, {16, -1}, {17, -1}, {18, -1}, {19, -1}, {20, -1}, {21, -1},
	{-1, -1}, {22, -1}, {23, -1}, {24, -1}, {25, -1}, {26, -1}, {27, -1}, {28, -1}, {29, -1}, {30, -1}, {31, -1},
	{32, -1}, {33, -1}, {34, -1}, {35, -1}, {36, -1}, {37, -1}, {38, -1}, {39, -1}, {40, -1}, {41, -1}, {42, -1},
	{43, -1}, {44, 0}, {45, 2}, {46, 4}, {47, 6}, {48, 8}, {49, -1}, {50, -1}, {51, -1}, {-1, -1},
};

// names (UI_LocalizeStrings): weapons Text 0x69 + id, armour 0x7f + id, engines 0x8a + id, landers 0x96 + id,
// other 0x9b + id
static const char *weapon_name(int id) { return id > 0 && id < 23 ? Text_Get(0x69 + id) : ""; }
static const char *armour_name(int id) { return id > 0 && id < 11 ? Text_Get(0x7f + id) : ""; }
static const char *engine_name(int id) { return id >= 0 && id < 12 ? Text_Get(0x8a + id) : ""; }
static const char *lander_name(int id) { return id >= 0 && id < 5 ? Text_Get(0x96 + id) : ""; }

// ---------------------------------------------------------------- the list (Shop_FillList 0x405030)

static const char *item_names[24];      // DAT_0049ad18
static int32_t item_ids[24];            // DAT_0049a7b8
static int32_t current_id;              // DAT_0049a6c0 the owned item (-1 none)
static char owned_name[64];             // DAT_0049a438 "[ name ]"
static UiListData shop_list_data = {0, 8, 8, 0, -1, 14, item_names};   // 0x48f450
static int32_t other_sel = -1;          // DAT_00583874
static Window *buy_button;              // DAT_0049b0f4 (the BUY / SELL button)
static Window *sell_button;             // DAT_0049b0f8
static Window *item_model;              // DAT_0049b234
static int32_t buy_tab;                 // DAT_0048f2e0
static int32_t sell_value;              // DAT_0049ad50

static int Shop_FillList(int tab, int sub) { // 0x405030 -> the row of the owned item or -1
	int n = 0, found = -1, start = 0, end = 0;
	uint32_t mask = 0;
	int32_t *cur = &other_sel;
	const LanderConfig *cfg = &configs[gPlayerState.landerType];
	const char *(*name)(int) = lander_name;
	const char *title = "";
	switch (tab) {
		case 0:
			title = Text_Get(0x2f);
			mask = 0x3f;
			cur = &gPlayerState.landerType;
			end = 5;
			name = lander_name;
			break;
		case 1:
			mask = cfg->weapons;
			end = 0x17;
			start = 1;
			name = weapon_name;
			if (sub == 0) {
				cur = &gPlayerState.primary;
				mask &= 0x7ffe;
				title = Text_Get(0x62);
			} else {
				cur = &gPlayerState.secondary;
				mask &= 0x438000;
				title = Text_Get(0x63);
			}
			break;
		case 2:
			if (sub == 0) {
				gFEListMode = 6;
				title = Text_Get(0x39);
				mask = cfg->armour;
				start = 1;
				cur = &gPlayerState.armour;
				end = 0xb;
				name = armour_name;
			} else {
				gFEListMode = 7;
				title = Text_Get(0x3a);
				mask = cfg->engines;
				cur = &gPlayerState.engine;
				end = 0xc;
				name = engine_name;
			}
			break;
		case 3:
			title = Text_Get(0x32);
			mask = 7;
			other_sel = -1;
			cur = &other_sel;
			other_cost[0] = (gPlayerState.maxFuel - gPlayerState.fuel) * 0xd >> 12;
			other_cost[1] = (0xff - gPlayerState.shield) * 0x139ba >> 12;
			other_cost[2] = 10000;
			end = 3;
			break;
		default:
			break;
	}
	fe_list_desc.title = title;
	current_id = *cur;
	for (int i = start; i < end; i++) {
		if (!(mask & (1u << i))) continue;
		item_names[n] = tab == 3 ? Text_Get(0x9b + i) : name(i);
		item_ids[n] = i;
		if (*cur == i) {
			snprintf(owned_name, sizeof(owned_name), gLanguage == 0 ? "[ %s ]" : "[%s]", item_names[n]);
			item_names[n] = owned_name;
			found = n;
		}
		n++;
	}
	item_names[n] = NULL;
	shop_list_data.visible = n > 0xc ? 0xd : n;
	shop_list_data.count = n;
	return found;
}

static bool Shop_GetPrice(int row, int32_t *price, int32_t *tradein, int *rec, int *model) { // 0x404a00
	const int32_t *table = weapons_cost;
	int base = 0;
	switch (gFEListMode) {
		case 4: table = landers_cost; base = 0x2e; break;
		case 5: table = weapons_cost; base = 0; break;
		case 6: table = armour_cost; base = 0x17; break;
		case 7: table = engines_cost; base = 0x22; break;
		case 8: table = other_cost; base = 0x33; break;
		default: break;
	}
	int id = item_ids[row];
	*price = table[id];
	*rec = item_info[base + id][0];
	*model = item_info[base + id][1];
	*tradein = current_id != -1 ? (table[current_id] << 2) / 10 : 0;
	return current_id != id;
}

// ---------------------------------------------------------------- an item (Shop_ShowItem 0x404cc0)

static UiSprite button_sprite = {0, NULL, 6, 0};             // 0x48f068
static const UiRect button3_rects[9] = {                     // 0x48f078: normal, hover, pressed (L, M, R)
	{78, 100, 90, 107}, {78, 108, 90, 116}, {78, 117, 90, 124},
	{64, 100, 76, 107}, {64, 108, 76, 116}, {64, 117, 76, 124},
	{50, 100, 62, 107}, {50, 108, 62, 116}, {50, 117, 62, 124},
};
static UiButtonData buy_data = {button3_rects, 0, NULL, 0};  // 0x48f0c0
static UiButton3Desc buy_desc = {&button_sprite, &buy_data};  // 0x48f0d0

static void buy_button_create(void) { // FUN_00404920
	static const WinRect en = {550, 452, 80, 16}, other = {550, 462, 80, 16};   // 0x48e228 / 0x48e248
	button_sprite.tf = gGlobalTextures;
	buy_data.label = Text_Get(0x69);
	if (!buy_button && (buy_button = Control_CreateButton3(fe_pane, &buy_desc))) {
		Control_SetRect(buy_button, gLanguage == 0 ? &en : &other);
	}
}

static void buy_button_destroy(void) { // FUN_00404960
	if (buy_button) {
		Control_Destroy(buy_button);
		buy_button = NULL;
	}
}

void Shop_HideItem(void) { // 0x404af0
	FE_DestroyHtmlWindow();
	if (item_model) {
		WindowDestroy(item_model);
		item_model = NULL;
	}
	buy_button_destroy();
	FE_HideMessage();
}

void Shop_ShowItem(int row) { // 0x404cc0
	char buf[256];
	int32_t price = 0, tradein = 0;
	int rec = -1, model = -1;
	Shop_HideItem();
	fe_list_data->selected = row;
	bool other = Shop_GetPrice(row, &price, &tradein, &rec, &model);
	bool button = false;
	if (gFEListMode == 8) {
		if (row == 0) {
			if (price == 0) {
				FE_ShowMessage(Text_Get(0xcd));            // the tanks are full
			} else {
				snprintf(buf, sizeof(buf), "%s", Text_Get(0xcc));
				fe_format_cash(buf, price);
				FE_ShowMessage(buf);
				button = true;
			}
		} else if (row == 1) {
			if (price == 0) {
				FE_ShowMessage(Text_Get(0xcf));            // the shields are full
			} else {
				snprintf(buf, sizeof(buf), "%s", Text_Get(0xce));
				fe_format_cash(buf, price);
				FE_ShowMessage(buf);
				button = true;
			}
		} else if (row == 2) {
			int cap = gPlayerState.secondary == 0xf ? 5 : gPlayerState.secondary == 0x10 ? 10 :
				gPlayerState.secondary == 0x11 ? 0x14 : 0;
			if (cap <= gPlayerState.missiles) {
				FE_ShowMessage(Text_Get(0xd0));            // no room for a missile
				if (gLanguage == 2) FE_ShowMessage2(Text_Get(0x104));
			} else {
				snprintf(buf, sizeof(buf), Text_Get(0xd1), cap - gPlayerState.missiles);
				FE_ShowMessage(buf);
				snprintf(buf, sizeof(buf), "%s", Text_Get(0xd2));
				fe_format_cash(buf, price);
				FE_ShowMessage2(buf);
				button = true;
			}
		}
	} else if (other) {
		snprintf(buf, sizeof(buf), "%s", Text_Get(0xd3));      // "Cost: $"
		fe_format_cash(buf, price);
		size_t l = strlen(buf);
		snprintf(buf + l, sizeof(buf) - l, "  %s", Text_Get(0xd4));   // "Trade-in: $"
		fe_format_cash(buf, tradein);
		FE_ShowMessage(buf);
		snprintf(buf, sizeof(buf), "%s", Text_Get(0xd5));      // "You Pay: $"
		fe_format_cash(buf, price - tradein);
		FE_ShowMessage2(buf);
		button = true;
	}
	if (button) buy_button_create();
	if (rec != -1) {
		static const WinRect r = {365, 140, 204, 217};       // 0x48f6d0
		fe_html = WindowCreate(0x400, fe_pane);
		WindowSetRect(fe_html, &r);
		WindowSetTextFile(fe_html, rec);
	}
	if (model != -1) {
		static const WinRect r = {200, 230, 160, 130};       // 0x48f6e0
		item_model = WindowCreate(0x80, fe_pane);
		WindowSetRect(item_model, &r);
		WindowSetModel(item_model, model, 0x800);
		WindowSetModelFlags(item_model, 0xc);
	}
}

// ---------------------------------------------------------------- buying (Shop_Buy 0x405300)

static int32_t buy_sub;                 // DAT_0048f3f8 PRIMARY/SECONDARY or ARMOUR/ENGINE
static int32_t buy_sub_weapons, buy_sub_upgrade;   // DAT_0049b238 / DAT_0049b23c

void Shop_Buy(void) {
	if (fe_list_data->selected < 0 && buy_tab <= 2) return;
	int row = fe_list_data->first + fe_list_data->selected;
	int id = item_ids[row];
	if (buy_tab < 4) {
		int32_t price, tradein;
		int rec, model;
		Shop_GetPrice(row, &price, &tradein, &rec, &model);
		price -= tradein;
		if (gPlayerState.cash < price) return;
		gPlayerState.cash -= price;
	}
	int32_t primary = gPlayerState.primary, armour = gPlayerState.armour;
	const LanderConfig *c;
	switch (buy_tab) {
		case 0:                                  // a new lander: parts that do not fit go back to its defaults
			c = &configs[id];
			gPlayerState.shield = 0xff;
			gPlayerState.fuel = c->fuel;
			if (gPlayerState.primary && !(c->weapons & (1u << (gPlayerState.primary & 31)))) gPlayerState.primary = c->def_primary;
			if (gPlayerState.secondary && !(c->weapons & (1u << (gPlayerState.secondary & 31)))) gPlayerState.secondary = c->def_secondary;
			if (gPlayerState.armour && !(c->armour & (1u << (gPlayerState.armour & 31)))) gPlayerState.armour = c->def_armour;
			gPlayerState.maxFuel = gPlayerState.fuel;
			primary = gPlayerState.primary;
			armour = gPlayerState.armour;
			gPlayerState.landerType = id;
			if (!(c->engines & (1u << (gPlayerState.engine & 31)))) gPlayerState.engine = c->def_engine;
			break;
		case 1:
			primary = id;
			if (buy_sub != 0) {
				primary = gPlayerState.primary;
				if (buy_sub == 1) {
					gPlayerState.secondary = id;
					gPlayerState.missiles = id == 0xf ? 5 : id == 0x10 ? 10 : id == 0x11 ? 0x14 : 0;
				}
			}
			break;
		case 2:
			armour = id;
			if (buy_sub != 0) {
				armour = gPlayerState.armour;
				if (buy_sub == 1) gPlayerState.engine = id;
			}
			break;
		case 3:
			if (id == 0) {
				other_cost[0] = 0;
				gPlayerState.fuel = gPlayerState.maxFuel;
			} else if (id == 1) {
				gPlayerState.shield = 0xff;
				other_cost[1] = 0;
			} else if (id == 2) {
				gPlayerState.missiles++;
			}
			break;
		default:
			break;
	}
	gPlayerState.armour = armour;
	gPlayerState.primary = primary;
	FE_DestroyList();
	if (buy_tab < 4) {
		int r = Shop_FillList(buy_tab, buy_sub);
		fe_list_create(&shop_list_data);
		if (r == -1) {
			Shop_HideItem();
			return;
		}
		Shop_ShowItem(r);
	}
}

// ---------------------------------------------------------------- the Buy screen (screen 12)

static const char *buy_tab_labels[4];   // DAT_0049a710: LANDER WEAPONS UPGRADE OTHER (Text 0x2f..0x32)
static const char *weapon_buttons[2], *upgrade_buttons[2];    // 0x49ad70 / 0x49ad90
UiBarData fe_buy_tabs = {fe_bar_skin, 0, 0, 4, -1, buy_tab_labels, {0}};       // 0x48f2d8
UiBarData fe_buy_buttons = {fe_bar_skin, 0, -1, 2, -1, weapon_buttons, {0}};   // 0x48f3f0

static void buy_labels(void) {
	for (int i = 0; i < 4; i++) buy_tab_labels[i] = Text_Get(0x2f + i);
	weapon_buttons[0] = Text_Get(0x37);
	weapon_buttons[1] = Text_Get(0x38);
	upgrade_buttons[0] = Text_Get(0x39);
	upgrade_buttons[1] = Text_Get(0x3a);
}

static void buy_show(int tab, int sub) {
	int r = Shop_FillList(tab, sub);
	fe_list_create(&shop_list_data);
	if (r != -1) Shop_ShowItem(r);
}

void Shop_TabCreate(void) { // FUN_004055b0 / 0x405680 / 0x405780 / 0x405890 by the current tab
	buy_labels();
	buy_tab = fe_buy_tabs.current < 0 ? 0 : fe_buy_tabs.current;
	fe_buy_tabs.current = buy_tab;
	switch (buy_tab) {
		case 0:
			buy_sub = -1;
			fe_buy_buttons.labels = weapon_buttons;
			fe_buy_buttons.count = 0;
			gFEListMode = 4;
			buy_show(0, -1);
			break;
		case 1:
			buy_sub = buy_sub_weapons;
			fe_buy_buttons.labels = weapon_buttons;
			fe_buy_buttons.count = 2;
			buy_button_create();
			gFEListMode = 5;
			buy_show(1, buy_sub);
			break;
		case 2:
			buy_sub = buy_sub_upgrade;
			fe_buy_buttons.labels = upgrade_buttons;
			fe_buy_buttons.count = 2;
			buy_button_create();
			gFEListMode = buy_sub != 0 ? 7 : 6;
			buy_show(2, buy_sub);
			break;
		case 3:
			buy_sub = -1;
			fe_buy_buttons.labels = weapon_buttons;
			fe_buy_buttons.count = 0;
			gFEListMode = 8;
			buy_show(3, 0);
			break;
		default:
			break;
	}
	fe_buy_buttons.current = buy_sub;
}

void Shop_TabDestroy(void) { // FUN_00405620 / 0x405710 / 0x405820 / 0x405900
	if (buy_tab == 1) buy_sub_weapons = buy_sub;
	if (buy_tab == 2) buy_sub_upgrade = buy_sub;
	buy_button_destroy();
	FE_DestroyList();
	gFEListMode = 0;
	Shop_HideItem();
}

void Shop_ButtonAction(int index) { // FUN_00405640 / 0x405740 / 0x405850 / 0x405920
	buy_sub = index;
	if (buy_tab == 2) gFEListMode = index != 0 ? 7 : 6;
	int r = Shop_FillList(buy_tab == 3 ? 0 : buy_tab, index);
	FE_DestroyList();
	fe_list_create(&shop_list_data);
	if (r == -1) {
		Shop_HideItem();
		if (buy_tab == 1 || buy_tab == 2) buy_button_create();
		return;
	}
	Shop_ShowItem(r);
}

// ---------------------------------------------------------------- Diagnostics / Sell (Shop_ShowEquipment 0x405ef0)

static char line_fuel[48], line_shield[48];              // 0x49a7f0 / 0x49aed0
static UiTextLine eq_lines[6];                           // 0x48ee98, 0x48eec0, 0x48eee8, 0x48ef10, 0x48ef60, 0x48ef88
static Window *eq_win[6];                                // DAT_0049b0ac b0 b4 b8 c0 c4
static const WinRect eq_rects[6] = {{210, 140, 16, 16}, {210, 178, 16, 16}, {210, 216, 16, 16}, {210, 254, 16, 16},
	{210, 292, 16, 16}, {210, 330, 16, 16}};

static void Shop_ShowEquipment(int diagnostics) { // 0x405ef0
	const char *nothing = Text_Get(0x65);
	eq_lines[0] = (UiTextLine){0x00b3ad00, 0, Text_Get(0x62), gPlayerState.primary < 1 ? nothing : weapon_name(gPlayerState.primary), -2};
	eq_lines[1] = (UiTextLine){0x00b3ad00, 0, Text_Get(0x63), gPlayerState.secondary < 1 ? nothing : weapon_name(gPlayerState.secondary), -2};
	eq_lines[2] = (UiTextLine){0x00b3ad00, 0, Text_Get(0x39), gPlayerState.armour < 1 ? nothing : armour_name(gPlayerState.armour), -2};
	eq_lines[3] = (UiTextLine){0x00b3ad00, 0, Text_Get(0x3a), engine_name(gPlayerState.engine), -2};
	for (int i = 0; i < 4; i++) {
		if (!eq_win[i] && (eq_win[i] = Control_CreateTextLine(fe_pane, &eq_lines[i]))) Control_SetRect(eq_win[i], &eq_rects[i]);
	}
	if (diagnostics) {
		snprintf(line_fuel, sizeof(line_fuel), "%dL (%d%%)", gPlayerState.fuel >> 4,
			gPlayerState.maxFuel ? gPlayerState.fuel * 100 / gPlayerState.maxFuel : 0);
		snprintf(line_shield, sizeof(line_shield), "%d%%", gPlayerState.shield * 100 / 0xff);
		eq_lines[4] = (UiTextLine){0x00b3ad00, 0, Text_Get(0x9b), line_fuel, -2};
		eq_lines[5] = (UiTextLine){0x00b3ad00, 0, Text_Get(0xe0), line_shield, -2};
		for (int i = 4; i < 6; i++) {
			if (!eq_win[i] && (eq_win[i] = Control_CreateTextLine(fe_pane, &eq_lines[i]))) Control_SetRect(eq_win[i], &eq_rects[i]);
		}
	}
}

static void Shop_HideEquipment(void) { // FUN_004061f0
	for (int i = 0; i < 6; i++) {
		if (eq_win[i]) Control_Destroy(eq_win[i]);
		eq_win[i] = NULL;
	}
	FE_HideMessage();
}

static void sell_button_create(void) { // FUN_00405960
	static const WinRect en = {550, 452, 80, 16}, other = {550, 462, 80, 16};
	button_sprite.tf = gGlobalTextures;
	buy_data.label = Text_Get(0x3d);
	if (!sell_button && (sell_button = Control_CreateButton3(fe_pane, &buy_desc))) {
		Control_SetRect(sell_button, gLanguage == 0 ? &en : &other);
	}
}

static void sell_button_destroy(void) { // FUN_004059b0
	buy_data.label = Text_Get(0x69);
	if (sell_button) {
		Control_Destroy(sell_button);
		sell_button = NULL;
	}
}

static void sell_select(Window *w) { // FUN_004059e0: the sale price of the part
	char buf[256] = "", buf2[256];
	int32_t value = 0;
	bool sellable = false;
	if (w == eq_win[0]) {
		value = weapons_cost[gPlayerState.primary];
		if (gPlayerState.primary == 0) snprintf(buf, sizeof(buf), "%s", Text_Get(0xe1));
		else { snprintf(buf, sizeof(buf), "%s: %s", Text_Get(0x62), weapon_name(gPlayerState.primary)); sellable = true; }
	} else if (w == eq_win[1]) {
		value = weapons_cost[gPlayerState.secondary];
		if (gPlayerState.secondary == 0) snprintf(buf, sizeof(buf), "%s", Text_Get(0xe2));
		else { snprintf(buf, sizeof(buf), "%s: %s", Text_Get(0x63), weapon_name(gPlayerState.secondary)); sellable = true; }
	} else if (w == eq_win[2]) {
		value = armour_cost[gPlayerState.armour];
		if (gPlayerState.armour == 0) snprintf(buf, sizeof(buf), "%s", Text_Get(0xe3));
		else { snprintf(buf, sizeof(buf), "%s: %s", Text_Get(0x39), armour_name(gPlayerState.armour)); sellable = true; }
	} else if (w == eq_win[3]) {
		value = engines_cost[gPlayerState.engine];
		if (gPlayerState.engine == 0) {
			snprintf(buf, sizeof(buf), "%s", Text_Get(0xe4));     // the standard engine cannot be sold
		} else {
			snprintf(buf, sizeof(buf), "%s", Text_Get(0xe5));     // refit the standard engine
			sellable = true;
		}
	}
	if (sellable) sell_button_create();
	else sell_button_destroy();
	if (!buf[0]) {
		FE_HideMessage();
		return;
	}
	FE_ShowMessage(buf);
	if (!sellable) return;
	sell_value = value * 4 / 10;
	if (w == eq_win[3]) sell_value -= engines_cost[0];
	if (sell_value < 1) {
		snprintf(buf2, sizeof(buf2), "%s", Text_Get(0xd3));
		fe_format_cash(buf2, -sell_value);
		size_t l = strlen(buf2);
		snprintf(buf2 + l, sizeof(buf2) - l, "  (%s)", Text_Get(0xe6));   // "Uneconomic"
	} else {
		snprintf(buf2, sizeof(buf2), "%s", Text_Get(0xd4));
		fe_format_cash(buf2, sell_value);
	}
	FE_ShowMessage2(buf2);
}

void Sell_Enter(void) { // Sell_Create 0x405c50
	Shop_ShowEquipment(0);
	for (int i = 0; i < 4; i++) {
		eq_lines[i].state = -1;
		if (!eq_win[i]) continue;
		eq_win[i]->select = sell_select;
		eq_win[i]->flags |= UIF_FOCUSABLE;
	}
	// keyboard order: primary, secondary, armour, engine (wrapping)
	for (int i = 0; i < 4; i++) {
		if (!eq_win[i]) continue;
		eq_win[i]->nav_up = eq_win[(i + 3) & 3];
		eq_win[i]->nav_down = eq_win[(i + 1) & 3];
	}
}

void Sell_Exit(void) { // FUN_00405e20
	Shop_HideEquipment();
	sell_button_destroy();
}

void Shop_Sell(void) { // 0x405d60
	int32_t *part = NULL;
	if (eq_win[0] && eq_win[0]->focused) part = &gPlayerState.primary;
	else if (eq_win[1] && eq_win[1]->focused) part = &gPlayerState.secondary;
	else if (eq_win[2] && eq_win[2]->focused) part = &gPlayerState.armour;
	else if (eq_win[3] && eq_win[3]->focused) part = &gPlayerState.engine;
	if (part && *part != 0) {
		gPlayerState.cash += sell_value;
		*part = 0;
		sell_button_destroy();
		Shop_HideEquipment();
		Sell_Enter();
	}
}

void Diag_Create(void) { Shop_ShowEquipment(1); }   // FUN_00406290
void Diag_Destroy(void) { Shop_HideEquipment(); }   // FUN_004061f0

void fe_shop_button3(Window *c) { // FUN_004016f0: BUY on the Buy screen, SELL on the Sell screen
	if (c == buy_button) Shop_Buy();
	else if (c == sell_button) Shop_Sell();
}
