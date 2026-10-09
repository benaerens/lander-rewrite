// The built-in UI strings of lander.EXE (Lang_GetString / Text_Get 0x426920): 3 languages x 262
// string pointers at 0x493870 (stride 0x418; the Italian and Spanish tables are empty), read from
// the player's lander.EXE at start-up (gametext.c); cp1252. Entry 0x104 of the English and French
// tables points at a buffer in .bss (0x49b260): "".
#include <stddef.h>

#include "fe.h"
#include "../game/gametext.h"

#define FE_STRING_COUNT 262
static const char *fe_strings[3][FE_STRING_COUNT];

const char *fe_soundtrack_url;   // 0x48f954 (FUN_00402820)

// the text in lander.EXE: the Direct3D build's addresses, the Glide build's last
const GameText fe_exe_text[] = {
	{.kind = GT_POINTERS, .va = 0x493870, .count = 3 * FE_STRING_COUNT, .out = fe_strings, .va_glide = 0x48c7a8},
	{.kind = GT_STRINGS, .va = 0x48f954, .count = 1, .out = &fe_soundtrack_url, .va_glide = 0x488954},
	{0},
};

const char *Text_Get(int id) { // 0x426920
	int lang = gLanguage >= 0 && gLanguage < 3 ? gLanguage : 0;
	if (id < 0 || id >= FE_STRING_COUNT || !fe_strings[lang][id]) return "";
	return fe_strings[lang][id];
}