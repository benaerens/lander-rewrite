// Texture files: X.dir (index) + X.db (pixels in 2048-byte pages).
// See NOTES.md "Texture file".
#ifndef TEXFILE_H
#define TEXFILE_H

#include "render.h"

typedef struct {
	uint16_t id;      // byte offset of the entry in .dir / 4 (how the game names it)
	uint16_t flags;   // raw format word (0x0400 = RGB, 0 = RGBA)
	int frames;       // >1 for animated textures (frames back to back)
	texture_t *frame; // frames[] textures, each width x height
} tex_entry_t;

typedef struct {
	int count;
	tex_entry_t *entries;
} texfile_t;

// path without extension, e.g. "MISSIONS\\MARS\\TEXFILE\\PCPURE\\TEXFILE"
texfile_t *texfile_load(const char *path);
void texfile_free(texfile_t *tf);
tex_entry_t *texfile_by_id(texfile_t *tf, uint16_t id);

#endif
