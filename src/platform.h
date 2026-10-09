// The only interface a port has to implement (see platform_*.c). The game
// renders into render_pixels() and mixes audio with mixer_render(); the
// platform shows the pixels, plays the samples and feeds input.
#ifndef PLATFORM_H
#define PLATFORM_H

#include "types.h"

// Logical buttons. Platforms map their keys/pads onto these; the game maps
// them onto actions (thrust, fire, ...) through its control configuration.
typedef enum {
	BTN_NONE,
	BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
	BTN_ACCEPT, BTN_BACK, BTN_PAUSE,
	BTN_KEY_FIRST, // raw keyboard keys follow: BTN_KEY_FIRST + platform-independent key code
	BTN_KEY_LAST = BTN_KEY_FIRST + 256,
	BTN_MOUSE_LEFT, BTN_MOUSE_RIGHT, BTN_MOUSE_MIDDLE,
	BTN_PAD_A, BTN_PAD_B, BTN_PAD_X, BTN_PAD_Y,
	BTN_PAD_L1, BTN_PAD_R1, BTN_PAD_L2, BTN_PAD_R2, BTN_PAD_START, BTN_PAD_SELECT,
	BTN_PAD_UP, BTN_PAD_DOWN, BTN_PAD_LEFT, BTN_PAD_RIGHT,
	BTN_MAX
} button_t;

// Pad / joystick axes are -1..1 (the first joystick; DirectInput's order X, Y, Z, Rx, Ry, Rz is LX, LY,
// PAD_Z, RX, RY, PAD_RZ). Its buttons 0..9 are BTN_PAD_A, B, X, Y, L1, R1, L2, R2, SELECT, START and the
// hat is BTN_PAD_UP..RIGHT. The mouse axes are relative motion (counts) since the last frame.
typedef enum { AXIS_PAD_LX, AXIS_PAD_LY, AXIS_PAD_RX, AXIS_PAD_RY, AXIS_MOUSE_DX, AXIS_MOUSE_DY, AXIS_PAD_Z, AXIS_PAD_RZ,
	AXIS_MAX } axis_t;

// Key codes inside the BTN_KEY range (ASCII for printable keys).
enum {
	KEY_ESCAPE = 27, KEY_RETURN = 13, KEY_TAB = 9, KEY_BACKSPACE = 8, KEY_SPACE = 32,
	KEY_UP = 128, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_LALT, KEY_RALT,
	KEY_INSERT, KEY_DELETE, KEY_HOME, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN,
	KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
	KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4, KEY_KP5, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9,
	KEY_KP_PLUS, KEY_KP_MINUS, KEY_KP_MULTIPLY, KEY_KP_DIVIDE, KEY_KP_ENTER, KEY_KP_PERIOD,
};

#define BTN_KEY(k) ((button_t)(BTN_KEY_FIRST + (k)))

// ---- implemented by the platform
double platform_now(void);              // seconds, monotonic
void platform_present(void);            // show render_pixels() (render_width x render_height)
void platform_pump_events(void);        // read input, window events; may set platform_quit
bool platform_should_quit(void);
void platform_set_fullscreen(bool on);
// The size the frame is shown at: the monitor in full screen, the window's client area otherwise (the
// game makes its missions' frames this aspect). The platform shows any render_width x render_height
// frame aspect-correct in it (black bars around a frame of another aspect).
void platform_display_size(int *w, int *h);
const char *platform_save_dir(void);    // writable folder for settings and saved games
const char *platform_program_dir(void); // the folder the program is in
void platform_set_title(const char *title);
void platform_set_icon(const char *program_file);   // the window's icon from this program (the original's)
// First start without game data: ask the player for their own Lander disc image. disc_folder = the
// folder next to the program where the image can be put instead; message = why (e.g. the last error),
// NULL the first time. Fills path; false = the player wants to quit.
bool platform_ask_disc_image(char *path, size_t size, const char *disc_folder, const char *message);
// While the disc image is copied: a status line and progress 0..1 (also keeps the window responsive)
void platform_show_status(const char *text, float progress);
// The mixer may run on an audio thread: wrap mixer_* calls from the game in these.
void platform_audio_lock(void);
void platform_audio_unlock(void);

// ---- implemented by input.c, called by the platform
void input_set_button(button_t b, bool down);
void input_set_axis(axis_t a, float value);
void input_text(int codepoint);         // typed characters, for name entry
void input_set_mouse_pos(int x, int y); // absolute pointer position in render_pixels() coordinates
void input_set_pad_connected(bool on);  // a joystick / game pad is there (its axes and buttons are valid)

// ---- used by the game
bool input_down(button_t b);
bool input_pressed(button_t b);         // went down since the previous frame
bool input_released(button_t b);
float input_axis(axis_t a);
int input_mouse_x(void);                // pointer position (front-end menus)
int input_mouse_y(void);
int input_typed(int *out, int max);     // characters typed this frame (cp1252 codes)
bool input_pad_connected(void);
void input_frame_end(void);             // call once per game frame

// ---- implemented by the game (main.c)
void game_init(int argc, char **argv);
void game_frame(void);                  // one frame: update + render + audio
void game_cleanup(void);

#endif
