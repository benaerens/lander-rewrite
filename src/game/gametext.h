// The original's text, read from the player's files at run time: the rewrite's source holds none of
// it. A module lists where its tables are in lander.EXE or in its mission's MISSIONR.DLL / brief.dll
// (a GameText list, ended by a count of 0); gametext_load fills the module's arrays with copies of
// the text, once. Read with pe.c.
#ifndef GAMETEXT_H
#define GAMETEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	GT_POINTERS = 1,    // count string pointers at va -> const char *out[count] (a zero pointer gives NULL)
	GT_STRINGS,         // count strings at va, va + stride, ... -> const char *out[count]
	GT_MAILS,           // count brief.dll mail records at va -> BriefMail out[count]
	GT_BYTES,           // count bytes at va -> char out[count]
};

typedef struct GameText {
	uint8_t kind;       // GT_*
	uint32_t va;        // address in the file (lander.EXE: the Direct3D build's)
	int32_t count;      // 0 ends a list
	void *out;
	uint32_t stride;    // GT_STRINGS: bytes from one string to the next
	uint32_t va_glide;  // lander.EXE text: the address in the Glide build
} GameText;

// brief.dll mail table record (0x104 bytes: char from[128] +0, char subject[128] +0x80, int32 category
// +0x100); tables are [language][mail]
typedef struct BriefMail {
	const char *from;   // -> LevelGetEmailInfo *from    (the engine keeps 20 chars)
	const char *subject;// -> LevelGetEmailInfo *subject (the engine keeps 52 chars)
	int32_t category;   // 1 Business, 2 News, 4 Personal
} BriefMail;

// Fills the arrays of list from the file (a game path), the first time it is called for that list.
// false (with a message on stderr) if the file or a table cannot be read: the strings are then "".
bool gametext_load(const char *path, const GameText *list);

// The game data (fs_root) lacks a file the text is read from (or lander.EXE's text cannot be read):
// true with its name in what.
bool gametext_missing(char *what, size_t size);

// Start-up, after the game data is found: lander.EXE's text (the Direct3D build's, else the Glide
// build's). Exits with a message when it cannot be read.
void gametext_init(void);

// LANDER_CHECK_STRINGS: every table of both lander.EXE builds and of every mission DLL compared with
// the arrays (or loaded into them); a dump of the text to the file in LANDER_STRINGS_DUMP. Returns the
// number of differences.
int gametext_check(void);

// lander.EXE's text, listed by the modules that use it
extern const GameText fe_exe_text[], hud_exe_text[], im_exe_text[];

#endif
