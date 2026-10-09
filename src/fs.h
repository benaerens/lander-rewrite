// Reading the original game data. Paths use the game's own spelling, e.g.
// "MISSIONS\\MARS\\1A\\TILES.PPM"; lookup is case-insensitive on every platform
// because the disc mixes cases ("MISSIONR.DLL" next to "mission.dat").
#ifndef FS_H
#define FS_H

#include "types.h"

void fs_set_root(const char *dir); // folder holding the files from the Lander CD
const char *fs_root(void);

// Loads a whole file; returns NULL if missing. Free with free().
uint8_t *fs_load(const char *path, uint32_t *size_out);
bool fs_exists(const char *path);

// Resolves the game path to a real file name; returns false if missing.
bool fs_resolve(const char *path, char *out, size_t out_size);

// Real folders (not game paths): fn for each entry of dir except "." and ".."; false if dir can't be read
typedef void (*fs_list_fn)(const char *name, bool is_dir, void *user);
bool fs_list(const char *dir, fs_list_fn fn, void *user);
void fs_make_dir(const char *dir);

static inline uint16_t get_u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline int16_t get_i16(const uint8_t *p) { return (int16_t)get_u16(p); }
static inline uint32_t get_u32(const uint8_t *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int32_t get_i32(const uint8_t *p) { return (int32_t)get_u32(p); }

#endif
