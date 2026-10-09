# Lander rewrite. Host build (Linux / WSL) for development and tests.
CC ?= gcc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -D_DEFAULT_SOURCE
BUILD = build

# CORE has no platform dependencies; the game runtime needs platform_now() etc.
CORE = src/types.c src/render.c src/fs.c src/discimage.c src/texfile.c src/png.c src/mixer.c src/terrain.c \
	src/model.c src/mission.c src/level.c src/zones.c src/pe.c
# every .c of the game (new modules are picked up automatically; main.c is the program)
GAME = src/input.c $(filter-out src/game/main.c,$(wildcard src/game/*.c)) $(wildcard src/game/missions/*.c) \
	$(wildcard src/fe/*.c)
GAME_FLAGS = -Isrc -Isrc/game -DHUD_WITH_REWRITE_RENDER

all: $(BUILD)/render_test $(BUILD)/mixer_test $(BUILD)/terrain_test $(BUILD)/sched_test \
	$(BUILD)/viewer_headless $(BUILD)/lander_headless $(BUILD)/disc_extract $(BUILD)/film_test

$(BUILD)/%_test: tests/%_test.c $(CORE) src/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(CORE) -lm

$(BUILD)/sched_test: tests/sched_test.c src/game/sched.c src/game/events.c src/game/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/sched_test.c src/game/sched.c src/game/events.c -lm

MEDIA = src/game/media.c src/game/media_slide.c src/game/media_music.c
$(BUILD)/film_test: tests/film_test.c src/platform_headless.c $(CORE) src/input.c $(MEDIA) src/*.h src/game/*.h src/lib/pl_mpeg.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(GAME_FLAGS) -o $@ tests/film_test.c src/platform_headless.c $(CORE) src/input.c $(MEDIA) -lm

$(BUILD)/viewer_headless: src/viewer.c src/platform_headless.c $(CORE) src/input.c src/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/viewer.c src/platform_headless.c $(CORE) src/input.c -lm

$(BUILD)/lander_headless: src/game/main.c src/platform_headless.c $(CORE) $(GAME) src/*.h src/game/*.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(GAME_FLAGS) -rdynamic -o $@ src/game/main.c src/platform_headless.c $(CORE) $(GAME) -lm

$(BUILD)/disc_extract: tests/disc_extract.c src/discimage.c src/discimage.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/disc_extract.c src/discimage.c

clean:
	rm -rf $(BUILD)

.PHONY: all clean
