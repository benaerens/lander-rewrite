#ifndef PNG_H
#define PNG_H

#include "types.h"

// pixels are 0xAARRGGBB; alpha is ignored
bool png_write(const char *path, const uint32_t *pixels, int width, int height);

#endif
