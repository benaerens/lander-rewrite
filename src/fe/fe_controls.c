// The original's controls (Control_Create* 0x427b50-0x4281e0) and their drawing / mouse code
// (FUN_0045f910 dispatch 0x45f910, per type 0x4601f0-0x463c50). A control is a UI node whose +0x58
// is its type and +0x5c the creator's descriptor; clicks raise event 0xE {control, value, state}
// which the front end handles (FE_OnControlEvent).
#include <stdio.h>
#include <string.h>

#include "../game/ui.h"
#include "../game/font.h"
#include "fe.h"

#define COL_TITLE    0x00b3ad00u   // DAT_0049929c
#define COL_LINE     0x00b3ad00u   // DAT_004992a0
#define COL_TEXT     0x00b3ad00u   // DAT_004992a4 / DAT_004992a8
#define COL_SELECTED 0x0000e0feu   // DAT_004992ac
#define COL_HOVER    0x00fff700u   // DAT_004992b0
#define COL_DISABLED 0x004c4a00u   // DAT_004992b4
#define COL_ON       0x00000000u   // DAT_0055fde4 / DAT_0055fde8 (never written: black on the lit pieces)

static void raise_control(Window *c, intptr_t value, intptr_t state) {
	LanderEvent ev = {EV_UI_CONTROL, {(intptr_t)c, value, state}};
	Event_Raise(&ev);
}

// ---------------------------------------------------------------- shared helpers

static void title_above(int layer, const char *s, int x, int y) { // FUN_00460150
	if (!s) return;
	int h = 0;
	for (const char *p = s; *p; p++) h = max(h, font_glyph_h((unsigned char)*p, 1));
	ui_text(layer, s, (int)strlen(s), x, y - h - 6, COL_TITLE, 1);
}

static void text_centred(int layer, const char *s, int x, int y, uint32_t c, int width) { // FUN_00462b10 (font 2)
	if (!s) return;
	int w = font_width(s, 2);
	if (width < w) {
		ui_text_clipped(layer, s, x, y, c, 2, width);
		return;
	}
	ui_text(layer, s, (int)strlen(s), x + ((width - w) >> 1), y, c, 2);
}

static int widest(const char *const *s, int n, int at_least) { // FUN_00463b70 / FUN_0045e690
	int w = at_least;
	for (int i = 0; i < n && s && s[i]; i++) {
		int sw = font_width(s[i], 0);
		if (w < sw) w = sw;
	}
	return w + 3;
}

static Window *hit_window(Window *parent, int x, int y, int w, int h, intptr_t id, UiMouseFn mouse) { // FUN_004605f0
	Window *n = UI_AllocWindow();
	if (!n) return NULL;
	n->x = x;
	n->y = y;
	n->w = w;
	n->h = h;
	n->flags |= 9;
	n->u5c = id;
	n->mouse = mouse;
	UI_OpenWindow(n, parent);
	return n;
}

// ---------------------------------------------------------------- list items (FUN_00463940..)
// The items node of a list / menu opens one row per frame (FUN_00463a50); each row is a focusable
// hit area whose index is +0x58.

static void row_activate(Window *r) { // FUN_004639f0
	Window *items = r->parent, *ctl = items->parent;
	UiListData *d = (UiListData *)items->u54;
	d->selected = (int32_t)r->u58;
	UI_SetFocus(ctl);
	raise_control(ctl, r->u58, 0);
}

static void row_mouse(Window *r, int inside, int down) { // FUN_00463960
	(void)down;
	if (!inside) return;
	if (ui_key_pressed(UIK_MOUSE0)) {
		ui_key_consume(UIK_MOUSE0);
		row_activate(r);
		return;
	}
	((UiListData *)r->parent->u54)->hover = (int32_t)r->u58;
}

static int row_update(Window *r, int layer) { (void)r; (void)layer; return UI_OPEN; }

static int items_open(Window *it, int layer) { // FUN_00463a50: one row per frame
	(void)layer;
	UiListData *d = (UiListData *)it->u54;
	int idx = (int)it->u58;
	const char *s = d && d->strings ? d->strings[idx] : NULL;
	if (!d || !s || idx >= d->visible) return UI_OPEN;
	Window *r = UI_AllocWindow();
	if (!r) return UI_OPEN;
	r->flags |= 0xd;
	r->y = d->rowh * idx;
	r->w = it->w;
	r->h = d->rowh;
	r->update = row_update;
	r->mouse = row_mouse;
	r->select = row_activate;
	r->u54 = (intptr_t)s;
	r->u58 = idx;
	UI_OpenWindow(r, it);
	if (idx == d->selected) UI_SetFocus(r);
	if (idx != 0) {
		Window *prev = (Window *)it->u5c;
		r->nav_up = prev;
		prev->nav_down = r;
	}
	it->u5c = (intptr_t)r;
	it->u58 = ++idx;
	it->h = d->rowh * idx;
	if (idx < d->visible) return d->count <= idx ? UI_OPEN : UI_OPENING;
	return UI_OPEN;
}

static int items_update(Window *it, int layer) { (void)it; (void)layer; return UI_OPEN; }

static int items_close(Window *it, int layer) { // FUN_00463b40
	(void)it;
	(void)layer;
	return UI_CLOSED;
}

static Window *items_create(Window *ctl, UiListData *d, int w) { // FUN_00463940 + set-up
	Window *it = UI_AllocWindow();
	if (!it) return NULL;
	it->open = items_open;
	it->update = items_update;
	it->close = items_close;
	it->w = w;
	it->flags |= 1;
	UI_SetCancel(ctl);
	UI_OpenWindow(it, ctl);
	it->u54 = (intptr_t)d;
	return it;
}

// ---------------------------------------------------------------- CTL_LIST (FUN_00463670)
static void list_draw(Window *c, int layer, UiListDesc *ld) {
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	UiListData *d = ld->data;
	if (!ld->items) {
		ld->items = items_create(c, d, widest(d ? d->strings : NULL, d ? d->count : 0, 4));
		if (!ld->items) return;
	}
	ld->items->x = x;
	ld->items->y = y;
	if (d) ld->items->w = widest(d->strings, d->count, 4);
	if (!d) return;
	title_above(layer, ld->title, x, y);
	d->hover += d->first;
	for (int i = d->first, ry = y; i < d->count; i++, ry += 14) {
		uint32_t col = i == d->selected ? COL_SELECTED : i == d->hover ? COL_HOVER : COL_TEXT;
		if (d->strings && d->strings[i]) ui_text(layer + 2, d->strings[i], (int)strlen(d->strings[i]), x, ry, col, 0);
	}
	d->hover = -1;
}

// ---------------------------------------------------------------- CTL_LIST2 (FUN_004637b0)
static void list2_draw(Window *c, int layer, UiList2Desc *ld) {
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	UiListData *d = ld->data;
	if (!ld->items) {
		ld->items = items_create(c, d, c->w);
		if (!ld->items) return;
	}
	ld->items->x = x;
	ld->items->y = y;
	if (!d) return;
	for (int i = d->first, ry = y; i < d->count; i++, ry += 14) {
		uint32_t c1 = COL_HOVER, c2 = COL_SELECTED;
		if (i != d->selected && i != d->hover) c1 = c2 = COL_TEXT;
		if (ld->col1 && ld->col1[i]) ui_text(layer + 2, ld->col1[i], (int)strlen(ld->col1[i]), x, ry, c1, 0);
		if (d->strings && d->strings[i]) ui_text(layer + 2, d->strings[i], (int)strlen(d->strings[i]), x + ld->col2x, ry, c2, 0);
	}
	d->hover = -1;
	if (ld->notify && ld->on_select) ld->on_select(d->selected);
}

// ---------------------------------------------------------------- CTL_MENU (FUN_004613d0)
static void menu_draw(Window *c, int layer, UiMenuDesc *md) {
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	struct UiMenuInfo *mi = md->info;
	UiListData *d = mi->data;
	if (!mi->items) {
		mi->items = items_create(c, d, c->w);
		if (!mi->items) return;
	}
	mi->items->x = x;
	mi->items->y = y;
	const UiRect *skin = mi->skin;
	int rowh = (skin[15].bottom - skin[15].top) + 2;
	d->rowh = rowh;
	d->hover += d->first;
	for (int i = d->first, k = 0, ry = y; i < d->first + d->visible; i++, k++, ry += rowh) {
		const UiRect *icon = i == d->selected ? &skin[15 + i] : &skin[d->first + k];
		ui_sprite(layer + 2, x, ry, md->sprite, icon, true);
		if (i < d->count && d->strings && d->strings[i]) {
			uint32_t col = i == d->selected ? COL_SELECTED : i == d->hover ? COL_HOVER : COL_TEXT;
			ui_text(layer + 2, d->strings[i], (int)strlen(d->strings[i]), x + rowh, ry + 0xb, col, 0);
		}
	}
	d->hover = -1;
}

// ---------------------------------------------------------------- buttons (FUN_00463550, 0x463650)
static void button_draw(Window *c, int layer, const UiSprite *s, UiButtonData *b) {
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	const UiRect *r;
	if (b->state == 0) {
		r = &b->rects[1];
	} else if (!(b->state & 2)) {
		r = &b->rects[0];
	} else {
		if (--b->countdown == 0) b->state ^= 2;
		r = &b->rects[2];
	}
	ui_sprite(layer, x, y, s, r, true);
	if (b->label && b->label[0]) {
		int w = font_width(b->label, 0);
		ui_text(layer + 2, b->label, (int)strlen(b->label), ((c->w - w) >> 1) + x, c->h + 6 + y, COL_TITLE, 0);
	}
}

static void button3_draw(Window *c, int layer, const UiSprite *s, UiButtonData *b) { // FUN_004631e0
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	const UiRect *r = b->rects;   // 3 rects per state: left, middle, right
	int base;
	uint32_t col = COL_LINE;
	if (b->state == 0) {
		base = 0;
	} else if (!(b->state & 1)) {
		if (--b->countdown == 0) b->state ^= 2;
		base = 3;
	} else {
		if (--b->countdown == 0) b->state ^= 1;
		base = 6;
		col = COL_ON;
	}
	ui_sprite(layer, x, y, s, &r[base], true);
	ui_sprite_tiled_h(layer, x + 7, y, c->w - 13, s, &r[base + 1]);
	ui_sprite(layer, x + c->w - 6, y, s, &r[base + 2], true);
	text_centred(layer + 1, b->label, x, y + 2, col, 0x5a);
}

static void button_toggle(Window *c, UiButtonData *b) { // FUN_00463490 + FUN_004634b0
	b->state = (b->state & ~2) == 0;
	raise_control(c, 0, 0);
}

// ---------------------------------------------------------------- CTL_TEXTLINE (FUN_004626a0)
static void textline_draw(Window *c, int layer, UiTextLine *t) {
	int x, y, tw = 0, th = 0, w2 = 0, h2 = 0;
	UI_GetScreenPos(c, &x, &y);
	if (t->title) {
		tw = font_width(t->title, 1);
		for (const char *p = t->title; *p; p++) th = max(th, font_glyph_h((unsigned char)*p, 1));
		ui_text(layer + 2, t->title, (int)strlen(t->title), x, y, t->colour, 1);
		th += 4;
	}
	if (t->text) {
		w2 = font_width(t->text, t->font);
		for (const char *p = t->text; *p; p++) h2 = max(h2, font_glyph_h((unsigned char)*p, t->font));
		uint32_t col = COL_SELECTED;
		if (!c->focused) {
			col = t->colour;
			if (t->state == 1) col = COL_HOVER;
		}
		if (t->state >= -1) t->state = -1;
		ui_text(layer + 2, t->text, (int)strlen(t->text), x, th + y, col, t->font);
	}
	if (tw <= w2) tw = w2;
	if (c->w < tw) c->w = tw;
	if (c->h < h2 + th) c->h = h2 + th;
}

// ---------------------------------------------------------------- CTL_CHECKBOX (FUN_00460a20)
static void checkbox_draw(Window *c, int layer, const UiSprite *s, UiCheck *d) {
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	ui_sprite(layer, x, y, s, &d->rects[d->value ? 1 : 0], true);
	if (d->label) {
		int h = 0;
		for (const char *p = d->label; *p; p++) h = max(h, font_glyph_h((unsigned char)*p, 0));
		ui_text(layer + 2, d->label, (int)strlen(d->label), x + 0x18, ((c->h - h) >> 1) + y, COL_TEXT, 0);
	}
}

// ---------------------------------------------------------------- CTL_SLIDER2 (FUN_00461780, FUN_00461580)
static void slider_set_from_knob(Window *k) { // FUN_0045e930 / FUN_0045e8c0 (int-valued sliders)
	Window *bar = k->parent;
	UiSlider *s = (UiSlider *)bar->parent->u5c ? ((UiSliderDesc *)bar->parent->u5c)->data : NULL;
	if (!s) return;
	int range = bar->w - 8;
	if (k->x < 0) k->x = 0;
	if (k->x > range) k->x = range;
	int v = range > 0 ? ((s->max - s->min) * k->x) / range + s->min : s->min;
	if (v > s->max) v = s->max;
	s->value = v;
}

static int knob_update(Window *k, int layer) { // FUN_0045e9c0 (bits 0/1 of +0x64 set: no number, no tile)
	(void)layer;
	Window *ctl = k->parent->parent;
	if (ctl && ctl->focused) {
		if (ui_key_down(UIK_LEFT)) { k->x -= 4; slider_set_from_knob(k); }
		else if (ui_key_down(UIK_RIGHT)) { k->x += 4; slider_set_from_knob(k); }
	}
	return UI_OPEN;
}

static void knob_mouse(Window *k, int inside, int down) { // FUN_0045eaa0: drag
	if (!inside || !down) return;
	int x, y, mx = ui_mouse_x();
	UI_GetScreenPos(k->parent, &x, &y);
	k->x = mx - x - 4;
	slider_set_from_knob(k);
}

static int bar_open(Window *bar, int layer) { // FUN_0045eb80
	(void)layer;
	Window *k = UI_AllocWindow();
	if (!k) return UI_OPEN;
	k->state = UI_OPEN;
	k->w = 8;
	k->flags |= 0x95;
	k->h = bar->h;
	k->update = knob_update;
	k->mouse = knob_mouse;
	UI_OpenWindow(k, bar);
	UiSlider *s = ((UiSliderDesc *)bar->parent->u5c)->data;
	k->x = s->max != s->min ? ((s->value - s->min) * (bar->w - 8)) / (s->max - s->min) : 0;
	k->w = bar->w;          // the hit area covers the whole bar
	k->x = 0;
	return UI_OPEN;
}

static int bar_update(Window *bar, int layer) { // FUN_00461580
	Window *ctl = bar->parent;
	UiSliderDesc *sd = (UiSliderDesc *)ctl->u5c;
	UiSlider *s = sd->data;
	const UiRect *skin = s->skin;
	int x, y;
	UI_GetScreenPos(bar, &x, &y);
	ui_sprite(layer, x + 2, y + 1, sd->sprite, &skin[12], true);
	ui_sprite(layer, x + bar->w - 6, y + 1, sd->sprite, &skin[13], true);
	int pos = s->max != s->min ? ((s->value - s->min) * (bar->w - 8)) / (s->max - s->min) : 0;
	ui_sprite(layer + 4, x + pos, y + 10, sd->sprite, &skin[14], true);
	if (pos) ui_tile(layer + 1, x + 4, y + 4, pos, 4, 0xb000fff7, 1);
	if (bar->w - pos - 8) ui_tile(layer + 1, x + 4 + pos, y + 4, bar->w - pos - 8, 4, COL_DISABLED, 0);
	if (s->label) {
		int w = font_width(s->label, 0), h = 0;
		for (const char *p = s->label; *p; p++) h = max(h, font_glyph_h((unsigned char)*p, 0));
		int tx = (s->flags & 1) ? x + 8 + bar->w : x - w - 8;
		ui_text(layer, s->label, (int)strlen(s->label), tx, ((ctl->h - h) >> 1) + y, COL_TEXT, 0);
	}
	if (s->value != s->last) {
		s->last = s->value;
		if (s->on_change) s->on_change(s);
	}
	return UI_OPEN;
}

static void slider_create_bar(Window *c, UiSlider *s) { // FUN_00461780
	s->last = s->value;
	Window *bar = UI_AllocWindow();
	if (!bar) return;
	bar->x = -4;
	bar->y = 0;
	bar->flags |= 0xd;
	bar->w = c->w;
	bar->h = s->skin[12].bottom - s->skin[12].top;
	bar->open = bar_open;
	bar->update = bar_update;
	UI_OpenWindow(bar, c);
}

// ---------------------------------------------------------------- CTL_TABBAR (FUN_00462ca0) / CTL_BUTTONBAR (FUN_00462fd0)
static void bar_item_mouse(Window *n, int inside, int down) { // FUN_00462a30
	(void)down;
	if (!inside) return;
	UiBarData *d = (UiBarData *)n->u5c;
	if (ui_key_pressed(UIK_MOUSE0)) {
		ui_key_consume(UIK_MOUSE0);
		d->current = (int32_t)n->u60;
		raise_control(n->parent, n->u60, 0);
		return;
	}
	d->hover = (int32_t)n->u60;
}

static Window *bar_item(Window *ctl, UiBarData *d, int x, int y, int w, int h, int idx) { // FUN_00462aa0
	Window *n = UI_AllocWindow();
	if (!n) return NULL;
	n->mouse = bar_item_mouse;
	n->flags |= 5;
	n->x = x; n->y = y; n->w = w; n->h = h;
	n->u5c = (intptr_t)d;
	n->u60 = idx;
	UI_SetCancel(ctl);
	UI_OpenWindow(n, ctl);
	return n;
}

static void tabbar_draw(Window *c, int layer, const UiSprite *s, UiBarData *d) {
	const UiRect *skin = d->skin;
	int endw = d->extra ? skin[0].right - skin[0].left : 10;
	Window *p = c->parent;
	if (p) {
		c->y = p->h - (skin[0].bottom - skin[0].top);
		c->w = p->w - 10;
	}
	c->x = 10;
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	int right = c->w - endw + x;
	if (d->extra) ui_sprite(layer, right, y, s, &skin[0], true);
	y += 10;
	ui_sprite(layer, x, y, s, &skin[5], true);   // start cap (+0x28)
	int capw = skin[5].right - skin[5].left;
	ui_line(layer, capw - 4 + x, y, right, y, COL_LINE);
	int tx = capw - 2;
	for (int i = 0; i < d->count; i++, tx += 86) {
		if (!d->wins[i]) {
			d->wins[i] = bar_item(c, d, tx, 10, 0x5a, 0x10, i);
		} else {
			int b = i == d->current ? 13 : i == d->hover ? 10 : 7;   // +0x68 / +0x50 / +0x38
			int l = i == d->current ? layer + 1 : layer;
			ui_sprite(l, x + tx, y + 2, s, &skin[b], true);
			ui_sprite_tiled_h(l, x + tx + 7, y + 2, 0x4d, s, &skin[b + 1]);
			ui_sprite(l, x + tx + 7 + 0x4d, y + 2, s, &skin[b + 2], true);
		}
		const char *label = d->labels ? d->labels[i] : NULL;
		if (label) text_centred(layer + 2, label, x + tx, y + 4, i == d->current ? COL_ON : COL_LINE, 0x5a);
	}
	d->hover = -1;
}

static void buttonbar_draw(Window *c, int layer, const UiSprite *s, UiBarData *d) {
	const UiRect *skin = d->skin;
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	int bx = 0, by = 0;
	bool left = true;
	for (int i = 0; i < d->count; i++) {
		if (!d->wins[i]) {
			d->wins[i] = bar_item(c, d, bx, by, 0x5a, 0x10, i);
		} else {
			int b = i == d->current ? 13 : i == d->hover ? 10 : 7;
			ui_sprite(layer, x + bx, y + by, s, &skin[b], true);
			ui_sprite_tiled_h(layer, x + bx + 7, y + by, 0x4d, s, &skin[b + 1]);
			ui_sprite(layer, x + bx + 7 + 0x4d, y + by, s, &skin[b + 2], true);
		}
		const char *label = d->labels ? d->labels[i] : NULL;
		if (label) text_centred(layer + 1, label, x + bx, y + by + 2, i == d->current ? COL_ON : COL_LINE, 0x5a);
		if (!left) {
			by += 0x10;
			bx = 0;
		} else {
			bx += 0x5b;
		}
		left = !left;
	}
	d->hover = -1;
}

// ---------------------------------------------------------------- CTL_UPDOWN (FUN_00462910)
static void updown_item_mouse(Window *n, int inside, int down) { // FUN_00462830 (auto-repeat while held)
	(void)down;
	if (!inside) return;
	Window *ctl = n->parent;
	UiUpDown *d = ((UiUpDownDesc *)ctl->u5c)->data;
	int id = (int)n->u5c;
	d->pressed = (int16_t)id;
	uint32_t now = Clock_Now();
	bool fire = false;
	if (ui_key_pressed(UIK_MOUSE0)) {
		ui_key_consume(UIK_MOUSE0);
		n->u60 = now + 0x1b8;
		fire = true;
	} else if (ui_key_down(UIK_MOUSE0)) {
		if ((uint32_t)n->u60 + 0x3c <= now) {
			n->u60 += 0x3c;
			fire = true;
		}
	} else {
		n->u60 = now - 0x3c;
	}
	if (fire) {
		void (*fn)(void) = id == 1 ? d->up : d->down;
		if (fn) fn();
		raise_control(ctl, 0, id);
	}
}

static void updown_draw(Window *c, int layer, const UiSprite *s, UiUpDown *d) {
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	const UiRect *up = &d->rects[d->pressed == 1 ? 2 : 0], *dn = &d->rects[d->pressed == 2 ? 3 : 1];
	d->pressed = 0;
	int dh = d->rects[1].bottom - d->rects[1].top;
	if (!d->b_up) {
		d->b_up = hit_window(c, x, y, 10, 10, 1, updown_item_mouse);
	} else {
		ui_sprite(layer, x, y, s, up, true);
	}
	if (!d->b_down) {
		d->b_down = hit_window(c, x, y + c->h - dh, 10, 10, 2, updown_item_mouse);
	} else {
		ui_sprite(layer, x, y + c->h - dh, s, dn, true);
	}
}

// ---------------------------------------------------------------- CTL_COMBO1 (FUN_00460710): list box with arrows
static void combo1_arrow_mouse(Window *n, int inside, int down) { // FUN_00460640
	(void)down;
	if (!inside) return;
	Window *ctl = n->parent;
	UiListData *l = ((UiCombo1Desc *)ctl->u5c)->data->list;
	if (!ui_key_pressed(UIK_MOUSE0)) return;
	ui_key_consume(UIK_MOUSE0);
	int id = (int)n->u5c;
	if (id == 1) {
		if (l->first > 0) l->first--;
		else id = 3;
	} else if (id == 2) {
		if (l->first < l->count - l->visible) l->first++;
		else id = 3;
	}
	raise_control(ctl, 0, id);
}

static void combo1_draw(Window *c, int layer, const UiSprite *s, UiCombo1 *d) {
	const UiRect *k = d->skin;
	UiListData *l = d->list;
	int rowh = (k[9].bottom - k[9].top) + 2;
	l->rowh = rowh;
	int x, y;
	UI_GetScreenPos(c, &x, &y);
	title_above(layer, d->title, x, y);
	if (!d->items) {
		d->items = items_create(c, l, c->w);
	} else if (l->count) {
		d->items->x = x;
		d->items->y = y;
	}
	if (l->visible < l->count) {
		if (!d->b_up) d->b_up = hit_window(c, x + c->w, y, 0x10, 8, 1, combo1_arrow_mouse);
		else ui_sprite(layer, x + c->w, y, s, &k[11], true);
		if (!d->b_down) d->b_down = hit_window(c, x + c->w, y + 8, 0x10, 8, 2, combo1_arrow_mouse);
		else ui_sprite(layer, x + c->w, y + 8, s, &k[12], true);
	}
	int lw = k[7].right - k[7].left, rw = k[9].right - k[9].left;
	for (int i = l->first, ry = y; i < l->first + l->visible; i++, ry += rowh) {
		int b = i == l->selected ? 7 : 0;
		ui_sprite(layer, x, ry, s, &k[b], true);
		ui_sprite_tiled_h(layer, x + lw, ry, c->w - lw - rw, s, &k[b + 1]);
		ui_sprite(layer, x + c->w - rw, ry, s, &k[b + 2], true);
		if (i < l->count && l->strings && l->strings[i]) {
			ui_text(layer + 1, l->strings[i], (int)strlen(l->strings[i]), x + lw, ry + 2,
				i == l->selected ? COL_ON : COL_TEXT, 0);
		}
	}
}

// ---------------------------------------------------------------- CTL_SPINNER (FUN_00461020): a value and two arrows
static void spinner_arrow_mouse(Window *n, int inside, int down) { // FUN_00460f40
	(void)down;
	if (!inside) return;
	Window *ctl = n->parent;
	UiSpinner *s = ((UiSpinnerDesc *)ctl->u5c)->data;
	int id = (int)n->u5c;
	s->pressed = (int16_t)id;
	if (!ui_key_pressed(UIK_MOUSE0)) return;
	ui_key_consume(UIK_MOUSE0);
	if (id == 1) {
		if (s->min < s->value) s->value--;
		else id = 3;
	} else if (id == 2) {
		if (s->value < s->max) s->value++;
		else id = 3;
	}
	raise_control(ctl, (intptr_t)s, id);   // event 0xE {control, the data, 1 / 2 / 3 at a limit}
}

static void spinner_draw(Window *c, int layer, const UiSprite *sp, UiSpinner *s) {
	const UiRect *left = &s->skin[0], *right = &s->skin[1];
	if (s->pressed == 1) left = &s->skin[2];
	else if (s->pressed == 2) right = &s->skin[3];
	s->pressed = 0;
	int font = (s->flags & 0x80) ? 1 : 2;
	int x, y, lw = 0, lh = 0;
	UI_GetScreenPos(c, &x, &y);
	if (s->label) {
		lw = font_width(s->label, font);
		for (const char *p = s->label; *p; p++) lh = max(lh, font_glyph_h((unsigned char)*p, font));
	}
	char num[16];
	snprintf(num, sizeof(num), "%d", (int)s->value);
	const char *name = s->strings ? s->strings[s->value] : NULL;
	int bx = x, by = y, lx = x, ly = y;
	switch (s->flags & 7) {
		case 0:   // the number left of the arrows, the label right-aligned above
			lx = x + c->w - lw;
			ly = y - lh;
			ui_text(layer + 2, num, (int)strlen(num), x - font_width(num, 1) - 8, y, COL_TITLE, 1);
			break;
		case 1:   // label, then the arrows below it and the number right of them
			bx = x + lw;
			by = y + lh + 3;
			ui_text(layer + 2, num, (int)strlen(num), x + lw + 0x12, y, COL_TITLE, 1);
			break;
		case 2:
			bx = x + lw + 6;
			break;
		case 3:   // the value's name right of the label, the arrows under the name
			if (name) ui_text(layer + 2, name, (int)strlen(name), x + lw + 6, y, COL_TITLE, 1);
			bx = x + lw + 6;
			by = y + lh + 6;
			break;
		case 4:   // the name above the arrows (right-aligned with flag 0x10)
			if (name) {
				int nx = (s->flags & 0x10) ? x + c->w - font_width(name, 1) : x;
				ui_text(layer + 2, name, (int)strlen(name), nx, y - lh - 6, COL_TITLE, 1);
			}
			bx = x + lw + 6;
			break;
		default:
			break;
	}
	if (s->label) ui_text(layer + 2, s->label, (int)strlen(s->label), lx, ly, COL_TITLE, font);
	if (!s->b_left) s->b_left = hit_window(c, bx, by, 0x18, 0x10, 1, spinner_arrow_mouse);   // FUN_00460ff0
	else ui_sprite(layer, bx, by, sp, left, true);                                           // FUN_0045e1d0
	bx += 0x1c;
	if (!s->b_right) s->b_right = hit_window(c, bx, by, 0x18, 0x10, 2, spinner_arrow_mouse);
	else ui_sprite(layer, bx, by, sp, right, true);
}

// ---------------------------------------------------------------- CTL_EDIT2 (FUN_00460e60): text edited in place
static void edit2_draw(Window *c, int layer, UiEdit2 *e) {
	if (!c->focused || !e->handler) return;
	int r = e->handler(e);
	if (r) raise_control(c, r, 0);
	if (Clock_Now() & 0x200) {          // the blinking cursor after the text
		int x, y, h = 0;
		UI_GetScreenPos(c, &x, &y);
		for (const char *p = e->text; p && *p; p++) h = max(h, font_glyph_h((unsigned char)*p, 0));
		if (!h) h = font_height(0);
		int w = e->text ? font_width(e->text, 0) : 0;
		ui_line(layer + 1, x + w, y, x + w, y + h, COL_DISABLED);
	}
}

// ---------------------------------------------------------------- dispatch (FUN_0045f910 / FUN_0045fc40 / FUN_0045ff80)

static int control_open(Window *c, int layer) { (void)c; (void)layer; return UI_OPEN; } // FUN_00427ac0

static int control_update(Window *c, int layer) { // FUN_00427bd0 -> FUN_0045f910
	void *desc = (void *)c->u5c;
	if (!desc) return UI_OPEN;
	ui_set_transform(c);
	switch ((uint32_t)c->u58) {
		case CTL_BUTTON:
		case CTL_HUBBUTTON: {
			UiButtonDesc *d = desc;
			button_draw(c, layer, d->sprite, d->data);
			break;
		}
		case CTL_BUTTON3: {
			UiButton3Desc *d = desc;
			button3_draw(c, layer, d->sprite, d->data);
			break;
		}
		case CTL_CHECKBOX: {
			UiCheckDesc *d = desc;
			checkbox_draw(c, layer, d->sprite, d->data);
			break;
		}
		case CTL_MENU: menu_draw(c, layer, desc); break;
		case CTL_TABBAR: {
			UiBarDesc *d = desc;
			tabbar_draw(c, layer, d->sprite, d->data);
			break;
		}
		case CTL_BUTTONBAR: {
			UiBarDesc *d = desc;
			buttonbar_draw(c, layer, d->sprite, d->data);
			break;
		}
		case CTL_LIST: list_draw(c, layer, desc); break;
		case CTL_LIST2: list2_draw(c, layer, desc); break;
		case CTL_UPDOWN: {
			UiUpDownDesc *d = desc;
			updown_draw(c, layer, d->sprite, d->data);
			break;
		}
		case CTL_TEXTLINE: textline_draw(c, layer, desc); break;
		case CTL_COMBO1: {
			UiCombo1Desc *d = desc;
			combo1_draw(c, layer, d->sprite, d->data);
			break;
		}
		case CTL_EDIT2: edit2_draw(c, layer, desc); break;
		case CTL_SPINNER: {
			UiSpinnerDesc *d = desc;
			spinner_draw(c, layer, d->sprite, d->data);
			break;
		}
		default: break;
	}
	return UI_OPEN;
}

static int control_close(Window *c, int layer) { // FUN_00427af0 -> FUN_0045fc40: forget the child windows
	(void)layer;
	void *desc = (void *)c->u5c;
	if (!desc) return UI_CLOSED;
	switch ((uint32_t)c->u58) {
		case CTL_MENU: ((UiMenuDesc *)desc)->info->items = NULL; break;
		case CTL_TABBAR:
		case CTL_BUTTONBAR: memset(((UiBarDesc *)desc)->data->wins, 0, sizeof(((UiBarDesc *)desc)->data->wins)); break;
		case CTL_LIST: ((UiListDesc *)desc)->items = NULL; break;
		case CTL_LIST2: ((UiList2Desc *)desc)->items = NULL; break;
		case CTL_UPDOWN: ((UiUpDownDesc *)desc)->data->b_up = ((UiUpDownDesc *)desc)->data->b_down = NULL; break;
		case CTL_COMBO1: {
			UiCombo1 *d = ((UiCombo1Desc *)desc)->data;
			d->items = d->b_up = d->b_down = NULL;
			break;
		}
		case CTL_SPINNER: {
			UiSpinner *d = ((UiSpinnerDesc *)desc)->data;
			d->b_left = d->b_right = NULL;
			break;
		}
		default: break;
	}
	return UI_CLOSED;
}

static void control_select(Window *c) { // FUN_00427b40 (empty) + keyboard activation of buttons
	(void)c;
}

static void control_mouse(Window *c, int inside, int down) { // FUN_0045ff80
	(void)down;
	if (!inside) return;
	uint32_t t = (uint32_t)c->u58;
	void *desc = (void *)c->u5c;
	if (t & CTL_COMBO) return;
	if (!ui_key_pressed(UIK_MOUSE0)) {
		if (t & CTL_BUTTON3) {
			UiButtonData *b = ((UiButton3Desc *)desc)->data;
			if (b->state == 0) { b->state = 2; b->countdown = 5; }
		}
		if (t & CTL_HUBBUTTON) {
			UiButtonData *b = ((UiButtonDesc *)desc)->data;
			if (b->state == 0) { b->state = 2; b->countdown = 5; }
		} else if (t & CTL_BUTTON) {
			UiButtonData *b = ((UiButtonDesc *)desc)->data;
			b->state |= 2;
			b->countdown = 5;
		} else if ((t & CTL_TEXTLINE) && ((UiTextLine *)desc)->state >= -1) {
			((UiTextLine *)desc)->state = 1;
		}
		return;
	}
	ui_key_consume(UIK_MOUSE0);
	if (t & CTL_BUTTON3) {
		UiButtonData *b = ((UiButton3Desc *)desc)->data;
		button_toggle(c, b);
		b->countdown = 8;
	}
	if (t & CTL_HUBBUTTON) {
		button_toggle(c, ((UiButtonDesc *)desc)->data);
	} else if (t & CTL_BUTTON) {
		button_toggle(c, ((UiButtonDesc *)desc)->data);
	} else if (t & CTL_CHECKBOX) {
		UiCheck *d = ((UiCheckDesc *)desc)->data;        // FUN_004609f0
		d->value = d->value == 0;
		if (d->on_change) d->on_change(d);
	} else if ((t & CTL_TEXTLINE) && ((UiTextLine *)desc)->state >= -1) {
		UI_SetFocus(c);
		if (c->select) c->select(c);
	}
}

Window *Control_Create(uint32_t type, Window *parent, void *desc) { // FUN_00427b50
	Window *c = UI_AllocWindow();
	if (!c) return NULL;
	c->x = c->y = c->w = c->h = 0;
	c->u58 = type;
	c->flags |= 0x401;
	c->open = control_open;
	c->update = control_update;
	c->close = control_close;
	c->select = control_select;
	c->mouse = control_mouse;
	c->u54 = (intptr_t)"Control";
	c->u5c = (intptr_t)desc;
	c->u60 = 0;
	c->u64 = 0;
	UI_OpenWindow(c, parent);
	return c;
}

void Control_Destroy(Window *c) { UI_CloseWindow(c); } // 0x427c00

Window *Control_CreateButton(Window *parent, UiButtonDesc *d) { return Control_Create(CTL_BUTTON, parent, d); }
Window *Control_CreateButton2(Window *parent, UiButtonDesc *d) { return Control_Create(CTL_HUBBUTTON, parent, d); }
Window *Control_CreateButton3(Window *parent, UiButton3Desc *d) { return Control_Create(CTL_BUTTON3, parent, d); }
Window *Control_CreateMenu(Window *parent, UiMenuDesc *d) { return Control_Create(CTL_MENU, parent, d); }
Window *Control_CreateCheckBox(Window *parent, UiCheckDesc *d) { return Control_Create(CTL_CHECKBOX, parent, d); }
Window *Control_CreateButtonBar(Window *parent, UiBarDesc *d) { return Control_Create(CTL_BUTTONBAR, parent, d); }
Window *Control_CreateList(Window *parent, UiListDesc *d) { return Control_Create(CTL_LIST, parent, d); }
Window *Control_CreateList2(Window *parent, UiList2Desc *d) { return Control_Create(CTL_LIST2, parent, d); }
Window *Control_CreateUpDown(Window *parent, UiUpDownDesc *d) { return Control_Create(CTL_UPDOWN, parent, d); }
Window *Control_CreateTextLine(Window *parent, UiTextLine *d) { return Control_Create(CTL_TEXTLINE, parent, d); }
Window *Control_CreateComboBox1(Window *parent, UiCombo1Desc *d) { return Control_Create(CTL_COMBO1, parent, d); }

Window *Control_CreateEdit2(Window *parent, UiEdit2 *d) { // 0x427f10
	Window *c = Control_Create(CTL_EDIT2, parent, d);
	if (c) c->flags |= UIF_FOCUSABLE;
	return c;
}

Window *Control_CreateSpinner(Window *parent, UiSpinnerDesc *d) { return Control_Create(CTL_SPINNER, parent, d); } // 0x427f50

Window *Control_CreateTabBar(Window *parent, UiBarDesc *d) { // 0x427f90
	Window *c = Control_Create(CTL_TABBAR, parent, d);
	if (c) c->flags |= 5;
	return c;
}

Window *Control_CreateSlider2(Window *parent, UiSliderDesc *d) { // 0x427de0
	Window *c = Control_Create(CTL_SLIDER2, parent, d);
	if (c) slider_create_bar(c, d->data);
	return c;
}

void Control_SetSize(Window *c, int x, int y, int w, int h) { // 0x4280d0
	if (!c) return;
	c->x = x; c->y = y; c->w = w; c->h = h;
	// the slider's bar child follows the control's width (FUN_00461780 copies it at creation)
	if ((uint32_t)c->u58 == CTL_SLIDER2 && c->child) c->child->w = w;
}

void Control_SetRect(Window *c, const WinRect *r) { if (c && r) Control_SetSize(c, r->x, r->y, r->w, r->h); } // 0x428100

void Control_GetRect(Window *c, WinRect *r) { r->x = c->x; r->y = c->y; r->w = c->w; r->h = c->h; } // 0x4281e0
