// Minimal PNG writer (stored deflate blocks, no compression) for screenshots
// and the headless test platform. No dependencies.
#include <stdio.h>
#include <stdlib.h>

#include "png.h"

static uint32_t crc_table[256];

static void crc_init(void) {
	for (uint32_t n = 0; n < 256; n++) {
		uint32_t c = n;
		for (int k = 0; k < 8; k++) {
			c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		}
		crc_table[n] = c;
	}
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		crc = crc_table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
	}
	return crc;
}

static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
	uint8_t hdr[8];
	put_be32(hdr, len);
	for (int i = 0; i < 4; i++) hdr[4 + i] = type[i];
	fwrite(hdr, 1, 8, f);
	if (len) fwrite(data, 1, len, f);
	uint32_t crc = crc_update(0xffffffffu, hdr + 4, 4);
	crc = crc_update(crc, data, len) ^ 0xffffffffu;
	uint8_t c[4];
	put_be32(c, crc);
	fwrite(c, 1, 4, f);
}

bool png_write(const char *path, const uint32_t *pixels, int width, int height) {
	if (!crc_table[1]) crc_init();
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	fwrite(sig, 1, 8, f);

	uint8_t ihdr[13];
	put_be32(ihdr, width);
	put_be32(ihdr + 4, height);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; // 8-bit RGB
	write_chunk(f, "IHDR", ihdr, 13);

	size_t row = 1 + (size_t)width * 3;
	size_t raw_len = row * height;
	uint8_t *raw = malloc(raw_len);
	for (int y = 0; y < height; y++) {
		uint8_t *r = raw + y * row;
		*r++ = 0;
		for (int x = 0; x < width; x++) {
			uint32_t p = pixels[y * width + x];
			*r++ = p >> 16; *r++ = p >> 8; *r++ = p;
		}
	}
	// zlib stream of stored blocks (max 65535 bytes each)
	size_t blocks = (raw_len + 65534) / 65535;
	size_t z_len = 2 + raw_len + blocks * 5 + 4;
	uint8_t *z = malloc(z_len), *zp = z;
	*zp++ = 0x78; *zp++ = 0x01;
	uint32_t s1 = 1, s2 = 0;
	for (size_t off = 0; off < raw_len; off += 65535) {
		size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
		*zp++ = off + n == raw_len;
		*zp++ = n; *zp++ = n >> 8; *zp++ = ~n; *zp++ = (~n) >> 8;
		for (size_t i = 0; i < n; i++) {
			uint8_t b = raw[off + i];
			*zp++ = b;
			s1 = (s1 + b) % 65521;
			s2 = (s2 + s1) % 65521;
		}
	}
	put_be32(zp, (s2 << 16) | s1);
	write_chunk(f, "IDAT", z, (uint32_t)z_len);
	write_chunk(f, "IEND", NULL, 0);
	free(raw);
	free(z);
	fclose(f);
	return true;
}
