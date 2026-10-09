// Mixer smoke test: plays game sound effects over a music track and writes
// the mix to a WAV file. usage: mixer_test <game data dir> <music.wav> <out.wav>
#include <stdio.h>
#include <stdlib.h>

#include "../src/mixer.h"
#include "../src/fs.h"

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: %s <game data dir> <music.wav> <out.wav>\n", argv[0]);
		return 1;
	}
	fs_set_root(argv[1]);
	mixer_init(44100);
	sfx_t *thrust = sfx_load_wav("SOUND\\THRUSTLP.WAV");
	sfx_t *explo = sfx_load_wav("SOUND\\EXPLO1S.WAV");
	sfx_t *laser = sfx_load_wav("SOUND\\LASER.WAV");
	if (!thrust || !explo || !laser) {
		fprintf(stderr, "missing sounds\n");
		return 1;
	}
	if (!mixer_music_play(argv[2], false)) {
		fprintf(stderr, "music failed\n");
		return 1;
	}
	mixer_set_master(1.0f, 0.5f);

	int seconds = 4, total = 44100 * seconds, block = 1024;
	int16_t *out = malloc(sizeof(int16_t) * 2 * total);
	int th = mixer_play(thrust, 0.8f, -0.5f, 1.0f, true);
	for (int pos = 0, b = 0; pos < total; pos += block, b++) {
		if (b == 40) mixer_play(explo, 1.0f, 0.5f, 1.0f, false);
		if (b % 20 == 0) mixer_play(laser, 0.6f, 0.0f, 1.0f + (b % 60) / 60.0f, false);
		mixer_set(th, 0.8f, -0.5f + pos / (float)total, 0.8f + pos / (float)total * 0.6f);
		mixer_render(out + pos * 2, min(block, total - pos));
	}

	FILE *f = fopen(argv[3], "wb");
	fwrite("RIFF", 1, 4, f); put32(f, 36 + total * 4); fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16); put16(f, 1); put16(f, 2); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 16);
	fwrite("data", 1, 4, f); put32(f, total * 4);
	fwrite(out, 4, total, f);
	fclose(f);
	printf("wrote %s (%d s)\n", argv[3], seconds);
	return 0;
}
