// Fonts: misc\fonts.fti glyph tables + misc\fonts texture file (0x430170-0x430570).
// See re/specs/small-formats.md §4.
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "../fs.h"
#include "../texfile.h"

static struct {
	uint8_t glyph[3][224][4]; // u, v, w, h for codes 0x20..0xff
	texfile_t *tex;
	bool loaded;
} F;

bool font_init(void) {
	if (F.loaded) {
		return true;
	}
	uint32_t size;
	uint8_t *d = fs_load("MISC\\FONTS.FTI", &size);
	if (!d || size < 0x14) {
		free(d);
		return false;
	}
	int n = get_u16(d);
	for (int f = 0; f < n && f < 3; f++) {
		uint32_t off = get_u16(d + 2 + 2 * f);
		if (off + 224 * 4 <= size) {
			memcpy(F.glyph[f], d + off, 224 * 4);
		}
	}
	free(d);
	F.tex = texfile_load("MISC\\FONTS\\PCPURE\\FONTS");
	F.loaded = F.tex != NULL;
	return F.loaded;
}

static const uint8_t *glyph(int font, unsigned char c) { // Font_GetGlyphRect 0x430170
	static const uint8_t empty[4] = {0, 0, 0, 0};
	if (c == 10) c = ' ';
	if (c < 0x20 || font < 0 || font > 2) return empty;
	return F.glyph[font][c - 0x20];
}

int font_width(const char *s, int font) {
	int w = 0;
	for (; *s; s++) {
		w += glyph(font, (unsigned char)*s)[2];
	}
	return w;
}

int font_width_n(const char *s, int n, int font) {
	int w = 0;
	for (int i = 0; i < n && s[i]; i++) {
		w += glyph(font, (unsigned char)s[i])[2];
	}
	return w;
}

int font_glyph_h(unsigned char c, int font) {
	return glyph(font, c)[3];
}

int font_height(int font) {
	static const int h[3] = {10, 12, 8};
	return font >= 0 && font < 3 ? h[font] : 10;
}

void font_print_n(const char *s, int n, int x, int y, uint32_t colorref, int font) { // Font_PrintClipped 0x4302d0
	if (!F.loaded || font < 0 || font > 2 || font >= F.tex->count) {
		return;
	}
	const texture_t *t = &F.tex->entries[font].frame[0];
	rgba_t col = rgba(colorref & 0xff, (colorref >> 8) & 0xff, (colorref >> 16) & 0xff, 255);
	for (int i = 0; i < n && s[i]; i++) {
		const uint8_t *g = glyph(font, (unsigned char)s[i]);
		if (g[2] && g[3]) {
			render_quad_2d((float)x, (float)y, g[2], g[3], (float)g[0] / t->width, (float)g[1] / t->height,
				(float)(g[0] + g[2]) / t->width, (float)(g[1] + g[3]) / t->height, col, t, BLEND_ALPHA);
		}
		x += g[2];
	}
}

void font_print(const char *s, int x, int y, uint32_t colorref, int font) {
	font_print_n(s, (int)strlen(s), x, y, colorref, font);
}

// ---------------------------------------------------------------- exports

void Font_Print(const char *s, int32_t x, int32_t y, uint32_t colour, int32_t font, void *ot) { // 0x430520
	(void)ot;
	font_print(s, x, y, colour, font);
}

void Font_GetStringLen(const char *s, int32_t *w, int32_t *h, int32_t font) { // 0x430570
	if (w) *w = font_width(s, font);
	int hh = 0;
	for (const char *p = s; *p; p++) {
		int gh = glyph(font, (unsigned char)*p)[3];
		if (gh > hh) hh = gh;
	}
	if (h) *h = hh;
}

void Print(int32_t x, int32_t y, uint32_t colour, const char *s) { // 0x444080
	font_print(s, x, y, colour, 0);
}
