// Pictures ("slides"): Slides_SelectDir 0x427140, Slide_Load 0x41d4a0, Slide_Draw 0x41d650,
// Slide_DrawScaled 0x41d720, Slide_Unload 0x41d4e0; the mission loading screen (Mission_Load 0x413d20)
// and the start-up sequence (Game_Startup 0x4101d0). re/specs/media.md §2, §4.
// A picture k of a set is the 6 textures 6k..6k+5 of the texture file MISC\SCREENS6 or MISC\SLIDES6
// (256x256, 256x256, 128x256 / 256x224, 256x224, 128x224). Only those 6 are read from the .db.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "media.h"
#include "../fs.h"
#include "../platform.h"
#include "../render.h"

#define PAGE 2048
#define PIC_W 640
#define PIC_H 480

// tile k of a picture: position and size (Slide_Draw's six FwSpriteDrawSection calls)
static const int tile_rect[6][4] = {
	{0, 0, 256, 256}, {256, 0, 256, 256}, {512, 0, 128, 256},
	{0, 256, 256, 224}, {256, 256, 256, 224}, {512, 256, 128, 224},
};

static int slide_set = 1;            // slot 10: 0 SCREENS6, 1 SLIDES6
static uint32_t *slide_pixels;       // the open picture, 640x480 0xAARRGGBB
static int slide_index = -1;

// ---------------------------------------------------------------- texture file reading

// Reads textures first..first+5 of the set into a 640x480 picture. false if the file is missing.
static bool load_picture(int set, int k, uint32_t *out) {
	const char *base = set ? "MISC\\SLIDES6\\PCPURE\\SLIDES6" : "MISC\\SCREENS6\\PCPURE\\SCREENS6";
	char path[128], real[1024];
	uint32_t dir_size;
	snprintf(path, sizeof(path), "%s.DIR", base);
	uint8_t *dir = fs_load(path, &dir_size);
	snprintf(path, sizeof(path), "%s.DB", base);
	if (!dir || dir_size < 0x30 || get_u32(dir) != 0xFACC00FF || !fs_resolve(path, real, sizeof(real))) {
		free(dir);
		return false;
	}
	FILE *f = fopen(real, "rb");
	if (!f) {
		free(dir);
		return false;
	}
	int count = (int)get_u32(dir + 4);
	bool ok = true;
	for (int i = 0; i < PIC_W * PIC_H; i++) {
		out[i] = 0xff000000;
	}
	uint8_t *buf = NULL;
	for (int t = 0; t < 6; t++) {
		int index = k * 6 + t;
		if (index < 0 || index >= count || 0x30 + index * 2 + 2 > (int)dir_size) {
			ok = false;
			break;
		}
		uint32_t e = get_u16(dir + 0x30 + index * 2) * 4u; // entry = id * 4
		if (e + 20 > dir_size) {
			ok = false;
			break;
		}
		uint32_t start = get_u16(dir + e + 8) * (uint32_t)PAGE;
		int bpp = dir[e + 15] == 4 ? 3 : 4;              // +0x0F format: 4 RGB888, 0 RGBA8888
		int w = get_u16(dir + e + 16), h = get_u16(dir + e + 18);
		buf = realloc(buf, (size_t)w * h * bpp);
		if (fseek(f, (long)start, SEEK_SET) || fread(buf, (size_t)w * h * bpp, 1, f) != 1) {
			ok = false;
			break;
		}
		// FwSpriteDrawSection(x, y, w, h) from texel (0, 0); opaque (every slide texel has alpha 255)
		const int *r = tile_rect[t];
		for (int y = 0; y < r[3] && y < h; y++) {
			for (int x = 0; x < r[2] && x < w; x++) {
				const uint8_t *s = buf + ((size_t)y * w + x) * bpp;
				out[(r[1] + y) * PIC_W + r[0] + x] = 0xff000000u | (s[0] << 16) | (s[1] << 8) | s[2];
			}
		}
	}
	free(buf);
	fclose(f);
	free(dir);
	return ok;
}

// ---------------------------------------------------------------- Slide_*

void media_slide_select_dir(int set) { // Slides_SelectDir 0x427140: "_DIR_MISC\SCREENS" / "SLIDES" + "6"
	slide_set = set ? 1 : 0;
}

bool media_slide_open(int k) { // Slide_Load 0x41d4a0
	if (k < 0) {
		k = rand() % 10 + 1; // "random loading background"
	}
	if (!slide_pixels) {
		slide_pixels = malloc(sizeof(uint32_t) * PIC_W * PIC_H);
	}
	slide_index = k;
	if (!load_picture(slide_set, k, slide_pixels)) {
		// missing textures draw nothing in the original; keep a black picture
		for (int i = 0; i < PIC_W * PIC_H; i++) slide_pixels[i] = 0xff000000;
		return false;
	}
	return true;
}

void media_slide_unload(void) { // Slide_Unload 0x41d4e0
	free(slide_pixels);
	slide_pixels = NULL;
	slide_index = -1;
}

// Copies the picture into the render buffer, scaled from a w x h screen (nearest texel).
static void blit_scaled(const uint32_t *pic, int w, int h) {
	uint32_t *px = render_pixels();
	int rw = render_width(), rh = render_height();
	if (w == rw && h == rh) {
		for (int y = 0; y < min(rh, PIC_H); y++) {
			memcpy(px + y * rw, pic + y * PIC_W, sizeof(uint32_t) * min(rw, PIC_W));
		}
		return;
	}
	for (int y = 0; y < rh; y++) {
		int sy = y * h / rh; // screen y -> picture y (the picture covers 640x480 of the w x h screen)
		if (sy >= PIC_H) continue;
		for (int x = 0; x < rw; x++) {
			int sx = x * w / rw;
			if (sx < PIC_W) px[y * rw + x] = pic[sy * PIC_W + sx];
		}
	}
}

void media_slide_draw(void) { // Slide_Draw 0x41d650: unscaled, 640x480 at the top left
	if (slide_pixels) {
		blit_scaled(slide_pixels, render_width(), render_height());
	}
}

void media_slide_draw_scaled(int w, int h) { // Slide_DrawScaled 0x41d720 (Picture_DrawTile 0x41d570)
	if (slide_pixels && w > 0 && h > 0) {
		blit_scaled(slide_pixels, w, h);
	}
}

bool media_slide_load(const char *name) {
	char set[16];
	int k = 0;
	if (!name || sscanf(name, "%15[^/\\:]%*[/\\:]%d", set, &k) != 2) {
		return false;
	}
	if (!strncasecmp(set, "screens", 7)) {
		media_slide_select_dir(0);
	} else if (!strncasecmp(set, "slides", 6)) {
		media_slide_select_dir(1);
	} else {
		return false;
	}
	return media_slide_open(k);
}

// ---------------------------------------------------------------- loading screen

static uint32_t *loading_pixels;

// FwTileDraw with mode 1: colour 0xAABBGGRR blended with its alpha over the screen
static void tile_blend(int x, int y, int w, int h, uint32_t abgr) {
	uint32_t *px = render_pixels();
	int rw = render_width(), rh = render_height();
	int a = abgr >> 24, r = abgr & 255, g = (abgr >> 8) & 255, b = (abgr >> 16) & 255;
	for (int yy = max(y, 0); yy < min(y + h, rh); yy++) {
		for (int xx = max(x, 0); xx < min(x + w, rw); xx++) {
			uint32_t c = px[yy * rw + xx];
			int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
			cr += (r - cr) * a / 255;
			cg += (g - cg) * a / 255;
			cb += (b - cb) * a / 255;
			px[yy * rw + xx] = 0xff000000u | (cr << 16) | (cg << 8) | cb;
		}
	}
}

void media_loading_draw(int progress) {
	// Game_ModeLoop: Slides_SelectDir(1); Slide_ShowAndWaitLoad(0); Slide_DrawScaled2Frames
	if (!loading_pixels) {
		loading_pixels = malloc(sizeof(uint32_t) * PIC_W * PIC_H);
		if (!load_picture(1, 0, loading_pixels)) {
			for (int i = 0; i < PIC_W * PIC_H; i++) loading_pixels[i] = 0xff000000;
		}
	}
	int rw = render_width(), rh = render_height();
	blit_scaled(loading_pixels, PIC_W, PIC_H);
	// Mission_Load: box {x = W - W*300/640, y = H*350/480, 260 x 16, 0x80000000} drawn into both
	// buffers once, then each frame the bar {x+2, y+2, progress (0..256) x 12, 0x40603030}. The screen
	// is never cleared, so the 25% bar layers pile up into a solid colour: drawn solid here.
	int x = rw == 640 ? 340 : rw - rw * 300 / 640;
	int y = rh == 480 ? 350 : rh * 350 / 480;
	tile_blend(x, y, 260, 16, 0x80000000);
	progress = clamp(progress, 0, 256);
	if (progress) {
		tile_blend(x + 2, y + 2, progress, 12, 0xff603030);
	}
}

void media_loading_done(void) {
	free(loading_pixels);
	loading_pixels = NULL;
}

// ---------------------------------------------------------------- start-up

enum { BOOT_OFF, BOOT_FILM, BOOT_WAIT, BOOT_LOGO };

static struct {
	int state;
	bool demo;
	double wait_start;
	uint32_t *soundtrack;   // the screen left by Slide_Draw2Frames(SCREENS6 #11)
} B;

static bool any_button_down(void) { // Input_AnyKey 0x4296a0: any button of any device
	for (int b = BTN_UP; b < BTN_MAX; b++) {
		if (input_down((button_t)b)) {
			return true;
		}
	}
	return false;
}

void media_startup_begin(bool demo) {
	B.demo = demo;
	media_film_open(media_film_slot_name(FILM_INTRO)); // Media_VideoPlay(0x29); missing: the slides follow at once
	B.state = BOOT_FILM;
}

static void startup_after_film(void) {
	// Video_SetModeFromConfig; Screen_ClearTwice
	if (!B.demo) {
		// Slides_SelectDir(0); Slide_ShowAndWaitLoad(0xb); Slide_Draw2Frames (draw, unload)
		media_slide_select_dir(0);
		media_slide_open(11);
		media_slide_draw();
		if (!B.soundtrack) {
			B.soundtrack = malloc(sizeof(uint32_t) * render_width() * render_height());
		}
		memcpy(B.soundtrack, render_pixels(), sizeof(uint32_t) * render_width() * render_height());
		media_slide_unload();
	}
	// Slides_SelectDir(1); Slide_ShowAndWaitLoad(2): the logo is loaded while #11 stays on screen
	media_slide_select_dir(1);
	media_slide_open(2);
	B.wait_start = platform_now();
	B.state = B.demo ? BOOT_LOGO : BOOT_WAIT;
}

bool media_startup_frame(void) {
	switch (B.state) {
	case BOOT_FILM:
		if (media_film_frame()) {
			return true;
		}
		media_film_close();
		startup_after_film();
		if (B.state == BOOT_WAIT) {
			memcpy(render_pixels(), B.soundtrack, sizeof(uint32_t) * render_width() * render_height());
			return true;
		}
		break;
	case BOOT_WAIT: {
		// Slide_Wait(200, 4000) 0x4100e0: up to 4 s; Esc ends it (and sets gLevelEndFlag), any key after 200 ms
		double t = (platform_now() - B.wait_start) * 1000;
		memcpy(render_pixels(), B.soundtrack, sizeof(uint32_t) * render_width() * render_height());
		if (t > 4000 || input_pressed(BTN_KEY(KEY_ESCAPE)) || (t > 200 && any_button_down())) {
			B.state = BOOT_LOGO;
		}
		return true;
	}
	case BOOT_LOGO:
		break;
	default:
		return false;
	}
	// Slide_Draw2Frames: the logo, left on screen while the front end loads; then Slides_SelectDir(demo)
	media_slide_draw();
	media_slide_unload();
	media_slide_select_dir(B.demo ? 1 : 0);
	free(B.soundtrack);
	B.soundtrack = NULL;
	B.state = BOOT_OFF;
	return false;
}
