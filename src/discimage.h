// Game data from the player's own Lander CD image: the data track's ISO 9660 files plus the CD-audio
// tracks as MUSIC\TRACKnn.WAV (44.1 kHz stereo). Nothing of the game is shipped with the rewrite.
// Images: Alcohol .mds (+ .mdf), .cue (+ .bin), .iso (2048-byte sectors, no music), raw .bin / .img
// (2352-byte sectors, data track only), .mdf alone (data track only).
#ifndef DISCIMAGE_H
#define DISCIMAGE_H

#include "types.h"

// what = the step ("Copying the game files", "Copying the music"), done / total in bytes; false = stop
typedef bool (*disc_progress_fn)(const char *what, uint64_t done, uint64_t total, void *user);

bool disc_is_image(const char *path);          // by its extension
// Extract into out_dir (created). false with a message in err when the image cannot be read, is not a
// Lander disc or the progress callback stopped it.
bool disc_extract(const char *image, const char *out_dir, disc_progress_fn progress, void *user,
	char *err, size_t err_size);

#endif
