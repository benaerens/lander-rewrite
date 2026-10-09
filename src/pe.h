// The original's Windows programs read as data: lander.EXE and each mission's MISSIONR.DLL and
// brief.dll hold the game's text, which the rewrite takes from the player's files at run time.
// 32-bit PE images; addresses are virtual addresses (the optional header's image base + RVA), as in
// the disassembly. Nothing is run or relocated.
#ifndef PE_H
#define PE_H

#include "types.h"

#define PE_MAX_SECTIONS 16

typedef struct {
	uint32_t va;            // virtual address of the section
	uint32_t size;          // bytes the file holds for it (min(virtual size, raw size))
	uint32_t vsize;         // bytes in memory: the rest is zero-filled (bss)
	uint32_t raw;           // file offset
} pe_section_t;

typedef struct {
	uint8_t *data;          // the whole file
	uint32_t file_size;
	uint32_t image_base;
	int num_sections;
	pe_section_t sections[PE_MAX_SECTIONS];
} pe_image_t;

// Loads a PE file through fs_load (a game path); NULL if it is missing or not a 32-bit PE image.
pe_image_t *pe_load(const char *path);
void pe_free(pe_image_t *pe);

// size bytes at va, or NULL unless they are all in one section's file data (bss reads as missing)
const uint8_t *pe_at(const pe_image_t *pe, uint32_t va, uint32_t size);
bool pe_u32(const pe_image_t *pe, uint32_t va, uint32_t *out);
// The NUL-terminated string at va (a pointer into pe->data, valid until pe_free), or NULL. In a
// section's zero-filled part (a buffer the program fills at run time) it is "".
const char *pe_string(const pe_image_t *pe, uint32_t va);
// count string pointers at va into table; a zero pointer gives NULL. false if a pointer or the
// array is outside the image (the table is then partly filled).
bool pe_string_table(const pe_image_t *pe, uint32_t va, const char **table, int count);

#endif
