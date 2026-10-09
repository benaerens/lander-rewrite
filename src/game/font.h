#ifndef FONT_H
#define FONT_H

#include "engine.h"

bool font_init(void);
int font_width(const char *s, int font);
int font_width_n(const char *s, int n, int font);
int font_height(int font);
int font_glyph_h(unsigned char c, int font);   // glyph rect height (Font_GetStringLen's h is the max)
void font_print(const char *s, int x, int y, uint32_t colorref, int font);   // colour 0x00BBGGRR
void font_print_n(const char *s, int n, int x, int y, uint32_t colorref, int font);

#endif
