#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "texfile.h"
#include "fs.h"

#define PAGE 2048
#define TEXFILE_MAGIC 0xFACC00FF

texfile_t *texfile_load(const char *path) {
	char name[512];
	uint32_t dir_size = 0, db_size = 0;
	snprintf(name, sizeof(name), "%s.dir", path);
	uint8_t *dir = fs_load(name, &dir_size);
	snprintf(name, sizeof(name), "%s.db", path);
	uint8_t *db = fs_load(name, &db_size);
	if (!dir || !db || dir_size < 0x30 || get_u32(dir) != TEXFILE_MAGIC) {
		free(dir);
		free(db);
		return NULL;
	}

	texfile_t *tf = calloc(1, sizeof(texfile_t));
	tf->count = get_u32(dir + 4);
	tf->entries = calloc(tf->count, sizeof(tex_entry_t));
	for (int i = 0; i < tf->count; i++) {
		uint16_t id = get_u16(dir + 0x30 + i * 2);
		const uint8_t *e = dir + id * 4;
		if ((uint32_t)id * 4 + 20 > dir_size) {
			continue;
		}
		uint32_t start = get_u16(e + 8) * PAGE;
		int frames = get_u16(e + 12);
		uint16_t format = get_u16(e + 14);
		int w = get_u16(e + 16), h = get_u16(e + 18);
		int bpp = format == 0x0400 ? 3 : 4;
		frames = frames < 1 ? 1 : frames;

		tex_entry_t *te = &tf->entries[i];
		te->id = id;
		te->flags = format;
		te->frames = frames;
		te->frame = calloc(frames, sizeof(texture_t));
		for (int f = 0; f < frames; f++) {
			texture_t *t = &te->frame[f];
			t->width = w;
			t->height = h;
			t->pixels = malloc(sizeof(uint32_t) * w * h);
			// frames are stored back to back without padding: the entry's page count is
			// ceil(frames * w * h * bpp / PAGE) for every multi-frame texture on the disc (e.g. the
			// front end's LANDER logo, 12 x 128x42 in 95 pages)
			uint32_t frame_bytes = (uint32_t)(w * h * bpp);
			const uint8_t *src = db + start + f * frame_bytes;
			if (start + f * frame_bytes + w * h * bpp > db_size) {
				memset(t->pixels, 0, sizeof(uint32_t) * w * h);
				continue;
			}
			for (int p = 0; p < w * h; p++) {
				uint8_t r = src[0], g = src[1], b = src[2];
				uint8_t a = bpp == 4 ? src[3] : 255;
				t->pixels[p] = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
				src += bpp;
			}
		}
	}
	free(dir);
	free(db);
	return tf;
}

void texfile_free(texfile_t *tf) {
	if (!tf) {
		return;
	}
	for (int i = 0; i < tf->count; i++) {
		for (int f = 0; f < tf->entries[i].frames; f++) {
			free(tf->entries[i].frame[f].pixels);
		}
		free(tf->entries[i].frame);
	}
	free(tf->entries);
	free(tf);
}

tex_entry_t *texfile_by_id(texfile_t *tf, uint16_t id) {
	for (int i = 0; i < tf->count; i++) {
		if (tf->entries[i].id == id) {
			return &tf->entries[i];
		}
	}
	return NULL;
}
