// Command-line form of the game's disc set-up: extract a Lander CD image into a data folder.
//   disc_extract <image (.mds/.mdf, .cue/.bin, .iso, .img)> <out_dir>
#include <stdio.h>
#include <stdlib.h>

#include "../src/discimage.h"

static bool show(const char *what, uint64_t done, uint64_t total, void *user) {
	(void)user;
	static int last = -1;
	int pct = total ? (int)(done * 100 / total) : 0;
	if (pct != last) {
		last = pct;
		printf("\r%-28s %3d%%", what, pct);
		fflush(stdout);
	}
	// test: stop at this percentage
	const char *stop = getenv("DISC_STOP_AT");
	return !stop || pct < atoi(stop);
}

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s <disc image> <out_dir>\n", argv[0]);
		return 1;
	}
	char err[512];
	if (!disc_extract(argv[1], argv[2], show, NULL, err, sizeof(err))) {
		fprintf(stderr, "\n%s\n", err);
		return 1;
	}
	printf("\nok\n");
	return 0;
}
