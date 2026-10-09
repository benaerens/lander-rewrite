#include <string.h>

#include "platform.h"

static struct {
	bool down[BTN_MAX];
	bool prev[BTN_MAX];
	float axis[AXIS_MAX];
	int text[32];
	int text_len;
	int mouse_x, mouse_y;
	bool pad;
} I = {.mouse_x = 320, .mouse_y = 240};

void input_set_pad_connected(bool on) {
	if (I.pad && !on) { // unplugged: nothing stays held
		for (int b = BTN_PAD_A; b <= BTN_PAD_RIGHT; b++) I.down[b] = false;
		I.axis[AXIS_PAD_LX] = I.axis[AXIS_PAD_LY] = I.axis[AXIS_PAD_RX] = I.axis[AXIS_PAD_RY] = 0;
		I.axis[AXIS_PAD_Z] = I.axis[AXIS_PAD_RZ] = 0;
	}
	I.pad = on;
}

bool input_pad_connected(void) { return I.pad; }

void input_set_button(button_t b, bool down) {
	if (b > BTN_NONE && b < BTN_MAX) {
		I.down[b] = down;
	}
}

void input_set_axis(axis_t a, float value) {
	if (a < AXIS_MAX) {
		if (a == AXIS_MOUSE_DX || a == AXIS_MOUSE_DY) {
			I.axis[a] += value; // mouse motion accumulates until the frame ends
		} else {
			I.axis[a] = value;
		}
	}
}

void input_text(int codepoint) {
	if (I.text_len < (int)len(I.text)) {
		I.text[I.text_len++] = codepoint;
	}
}

void input_set_mouse_pos(int x, int y) {
	I.mouse_x = x;
	I.mouse_y = y;
}

bool input_down(button_t b) { return b < BTN_MAX && I.down[b]; }
bool input_pressed(button_t b) { return b < BTN_MAX && I.down[b] && !I.prev[b]; }
bool input_released(button_t b) { return b < BTN_MAX && !I.down[b] && I.prev[b]; }
float input_axis(axis_t a) { return a < AXIS_MAX ? I.axis[a] : 0; }
int input_mouse_x(void) { return I.mouse_x; }
int input_mouse_y(void) { return I.mouse_y; }

int input_typed(int *out, int max) {
	int n = I.text_len < max ? I.text_len : max;
	memcpy(out, I.text, sizeof(int) * (size_t)n);
	return n;
}

void input_frame_end(void) {
	memcpy(I.prev, I.down, sizeof(I.down));
	I.axis[AXIS_MOUSE_DX] = I.axis[AXIS_MOUSE_DY] = 0;
	I.text_len = 0;
}
