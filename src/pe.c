#include <stdlib.h>
#include <string.h>

#include "pe.h"
#include "fs.h"

pe_image_t *pe_load(const char *path) {
	uint32_t size = 0;
	uint8_t *d = fs_load(path, &size);
	if (!d) return NULL;
	pe_image_t *pe = calloc(1, sizeof(*pe));
	if (!pe || size < 0x40 || d[0] != 'M' || d[1] != 'Z') goto bad;
	uint32_t nt = get_u32(d + 0x3c);
	if (nt > size - 24 || memcmp(d + nt, "PE\0\0", 4)) goto bad;
	uint32_t nsec = get_u16(d + nt + 6), opt_size = get_u16(d + nt + 20), opt = nt + 24;
	if (opt_size < 0x60 || opt + opt_size > size || get_u16(d + opt) != 0x10b) goto bad;   // PE32
	pe->data = d;
	pe->file_size = size;
	pe->image_base = get_u32(d + opt + 28);
	uint32_t sec = opt + opt_size;
	for (uint32_t i = 0; i < nsec && pe->num_sections < PE_MAX_SECTIONS; i++, sec += 40) {
		if (sec + 40 > size) goto bad;
		uint32_t vsize = get_u32(d + sec + 8), rva = get_u32(d + sec + 12);
		uint32_t rsize = get_u32(d + sec + 16), raw = get_u32(d + sec + 20);
		uint32_t n = vsize && vsize < rsize ? vsize : rsize;
		if (raw > size) n = 0;
		else if (n > size - raw) n = size - raw;
		pe->sections[pe->num_sections++] = (pe_section_t){pe->image_base + rva, n, vsize > n ? vsize : n, raw};
	}
	return pe;
bad:
	free(d);
	free(pe);
	return NULL;
}

void pe_free(pe_image_t *pe) {
	if (!pe) return;
	free(pe->data);
	free(pe);
}

// the section holding va: its file data from va on, *left bytes of it
static const uint8_t *pe_find(const pe_image_t *pe, uint32_t va, uint32_t *left) {
	for (int i = 0; i < pe->num_sections; i++) {
		const pe_section_t *s = &pe->sections[i];
		if (va >= s->va && va - s->va < s->size) {
			*left = s->size - (va - s->va);
			return pe->data + s->raw + (va - s->va);
		}
	}
	return NULL;
}

const uint8_t *pe_at(const pe_image_t *pe, uint32_t va, uint32_t size) {
	uint32_t left;
	const uint8_t *p = pe_find(pe, va, &left);
	return p && size <= left ? p : NULL;
}

bool pe_u32(const pe_image_t *pe, uint32_t va, uint32_t *out) {
	const uint8_t *p = pe_at(pe, va, 4);
	if (!p) return false;
	*out = get_u32(p);
	return true;
}

const char *pe_string(const pe_image_t *pe, uint32_t va) {
	uint32_t left;
	const uint8_t *p = pe_find(pe, va, &left);
	if (p) return memchr(p, 0, left) ? (const char *)p : NULL;
	for (int i = 0; i < pe->num_sections; i++) {   // zero-filled memory
		const pe_section_t *s = &pe->sections[i];
		if (va >= s->va && va - s->va < s->vsize) return "";
	}
	return NULL;
}

bool pe_string_table(const pe_image_t *pe, uint32_t va, const char **table, int count) {
	for (int i = 0; i < count; i++) {
		uint32_t ptr;
		if (!pe_u32(pe, va + 4 * (uint32_t)i, &ptr)) return false;
		table[i] = ptr ? pe_string(pe, ptr) : NULL;
		if (ptr && !table[i]) return false;
	}
	return true;
}
