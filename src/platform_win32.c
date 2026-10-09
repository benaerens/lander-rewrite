// Windows platform: a plain Win32 window showing the software-rendered frame
// with StretchDIBits, keyboard/mouse input (raw mouse motion for the mouse axes), the first
// joystick / game pad through winmm (joyGetPosEx), QueryPerformanceCounter timing and
// waveOut audio fed by the mixer, and the first-start screen that asks for the player's disc image.
// Needs user32, gdi32, winmm, comdlg32 and shell32.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "render.h"
#include "mixer.h"

#define AUDIO_RATE 44100
#define AUDIO_BUFFERS 4
#define AUDIO_FRAMES 1024

static struct {
	HWND hwnd;
	bool quit;
	bool fullscreen;
	WINDOWPLACEMENT placement;
	LARGE_INTEGER freq, start;
	char save_dir[MAX_PATH];
	char program_dir[MAX_PATH];

	HWAVEOUT wave;
	WAVEHDR hdr[AUDIO_BUFFERS];
	int16_t buf[AUDIO_BUFFERS][AUDIO_FRAMES * 2];
	CRITICAL_SECTION audio_lock;
	HANDLE audio_event;
	HANDLE audio_thread;

	double next_frame;      // frame pacing (platform_present)
	int display_w, display_h; // the last valid platform_display_size answer
	bool mouse_seen;        // the pointer's client position (mapped to the frame every frame)
	int mouse_cx, mouse_cy;

	int joy_id;             // the joystick in use (-1 none)
	JOYCAPSA joy_caps;
	double joy_retry;       // when to look for a joystick again
} W = {.joy_id = -1};

// the first-start screen (platform_ask_disc_image / platform_show_status)
static struct {
	bool active;            // the screen is up: system pointer, keys go to it
	char dropped[1024];     // a file dropped on the window
	bool browse, cancel, open_folder;
	HFONT big, small;
	int font_h;             // the fonts' size (follows the window's height)
	double last_draw;
} S;

double platform_now(void) {
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)(t.QuadPart - W.start.QuadPart) / (double)W.freq.QuadPart;
}

bool platform_should_quit(void) { return W.quit; }
const char *platform_save_dir(void) { return W.save_dir; }
const char *platform_program_dir(void) { return W.program_dir; }
void platform_set_title(const char *title) { SetWindowTextA(W.hwnd, title); }

void platform_set_icon(const char *program_file) {
	HICON big = NULL, small = NULL;
	if (ExtractIconExA(program_file, 0, &big, &small, 1) > 0) {
		if (big) SendMessage(W.hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
		if (small) SendMessage(W.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
	}
}

void platform_set_fullscreen(bool on) {
	DWORD style = GetWindowLong(W.hwnd, GWL_STYLE);
	if (on && !W.fullscreen) {
		MONITORINFO mi = {sizeof(mi)};
		if (GetWindowPlacement(W.hwnd, &W.placement) &&
			GetMonitorInfo(MonitorFromWindow(W.hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
			SetWindowLong(W.hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
			SetWindowPos(W.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}
	} else if (!on && W.fullscreen) {
		SetWindowLong(W.hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(W.hwnd, &W.placement);
		SetWindowPos(W.hwnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
	W.fullscreen = on;
}

// full screen: the monitor's size; in a window: its client area (Alt+Enter and window resizes change it,
// the game's next mission frame follows)
void platform_display_size(int *w, int *h) {
	int dw = 0, dh = 0;
	if (W.fullscreen) {
		MONITORINFO mi = {sizeof(mi)};
		if (GetMonitorInfo(MonitorFromWindow(W.hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
			dw = mi.rcMonitor.right - mi.rcMonitor.left;
			dh = mi.rcMonitor.bottom - mi.rcMonitor.top;
		}
	} else {
		RECT r;
		if (GetClientRect(W.hwnd, &r)) {
			dw = r.right - r.left;
			dh = r.bottom - r.top;
		}
	}
	if (dw > 0 && dh > 0) {    // (a minimised window has no client area: the last size stays)
		W.display_w = dw;
		W.display_h = dh;
	}
	*w = W.display_w > 0 ? W.display_w : 640;
	*h = W.display_h > 0 ? W.display_h : 480;
}

// where the frame is shown in the client area (aspect ratio kept, black bars around)
static void present_rect(int *dx, int *dy, int *dw, int *dh) {
	RECT r;
	GetClientRect(W.hwnd, &r);
	int cw = r.right - r.left, ch = r.bottom - r.top;
	int w = render_width(), h = render_height();
	if (w <= 0 || h <= 0) {
		*dx = *dy = 0;
		*dw = cw;
		*dh = ch;
		return;
	}
	*dw = cw;
	*dh = cw * h / w;
	if (*dh > ch) {
		*dh = ch;
		*dw = ch * w / h;
	}
	*dx = (cw - *dw) / 2;
	*dy = (ch - *dh) / 2;
}

// The original presents with a DirectDraw Flip, which waits for the vertical blank: everything the game
// steps per drawn frame (the front end's model windows turn 32/4096 a frame, button flashes count frames,
// lists open a row a frame) ran at the display's rate. Here frames are paced at 60 a second.
#define FRAME_SECONDS (1.0 / 60.0)

static void frame_pace(void) {
	double now = platform_now();
	if (W.next_frame <= 0 || now - W.next_frame > 0.25) W.next_frame = now;   // the start, or after a stall
	W.next_frame += FRAME_SECONDS;
	while ((now = platform_now()) < W.next_frame) {
		double left = W.next_frame - now;
		Sleep(left > 0.002 ? (DWORD)((left - 0.001) * 1000) : 0);
	}
}

void platform_present(void) {
	HDC dc = GetDC(W.hwnd);
	RECT r;
	GetClientRect(W.hwnd, &r);
	int cw = r.right - r.left, ch = r.bottom - r.top;
	int w = render_width(), h = render_height();
	int dx, dy, dw, dh;
	present_rect(&dx, &dy, &dw, &dh);
	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h; // top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	SetStretchBltMode(dc, COLORONCOLOR);
	StretchDIBits(dc, dx, dy, dw, dh, 0, 0, w, h, render_pixels(), &bi, DIB_RGB_COLORS, SRCCOPY);
	if (dx > 0 || dy > 0) {
		HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
		RECT bars[4] = {{0, 0, cw, dy}, {0, dy + dh, cw, ch}, {0, dy, dx, dy + dh}, {dx + dw, dy, cw, dy + dh}};
		for (int i = 0; i < 4; i++) {
			FillRect(dc, &bars[i], black);
		}
	}
	ReleaseDC(W.hwnd, dc);
	frame_pace();
}

// the pointer in frame pixels; again every frame, as the frame's size changes between the front end and
// a mission and with the window
static void mouse_to_frame(void) {
	int dx, dy, dw, dh;
	if (!W.mouse_seen) return;
	present_rect(&dx, &dy, &dw, &dh);
	if (dw > 0 && dh > 0) {
		input_set_mouse_pos((W.mouse_cx - dx) * render_width() / dw, (W.mouse_cy - dy) * render_height() / dh);
	}
}

static int map_vk(WPARAM vk) {
	if (vk >= 'A' && vk <= 'Z') return (int)(vk - 'A' + 'a');
	if (vk >= '0' && vk <= '9') return (int)vk;
	if (vk >= VK_F1 && vk <= VK_F12) return KEY_F1 + (int)(vk - VK_F1);
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return KEY_KP0 + (int)(vk - VK_NUMPAD0);
	switch (vk) {
		case VK_ESCAPE: return KEY_ESCAPE;
		case VK_RETURN: return KEY_RETURN;
		case VK_TAB: return KEY_TAB;
		case VK_BACK: return KEY_BACKSPACE;
		case VK_SPACE: return KEY_SPACE;
		case VK_UP: return KEY_UP;
		case VK_DOWN: return KEY_DOWN;
		case VK_LEFT: return KEY_LEFT;
		case VK_RIGHT: return KEY_RIGHT;
		case VK_LSHIFT: case VK_SHIFT: return KEY_LSHIFT;
		case VK_RSHIFT: return KEY_RSHIFT;
		case VK_LCONTROL: case VK_CONTROL: return KEY_LCTRL;
		case VK_RCONTROL: return KEY_RCTRL;
		case VK_LMENU: case VK_MENU: return KEY_LALT;
		case VK_RMENU: return KEY_RALT;
		case VK_INSERT: return KEY_INSERT;
		case VK_DELETE: return KEY_DELETE;
		case VK_HOME: return KEY_HOME;
		case VK_END: return KEY_END;
		case VK_PRIOR: return KEY_PAGEUP;
		case VK_NEXT: return KEY_PAGEDOWN;
		case VK_ADD: return KEY_KP_PLUS;
		case VK_SUBTRACT: return KEY_KP_MINUS;
		case VK_MULTIPLY: return KEY_KP_MULTIPLY;
		case VK_DIVIDE: return KEY_KP_DIVIDE;
		case VK_DECIMAL: return KEY_KP_PERIOD;
		case VK_OEM_COMMA: return ',';
		case VK_OEM_PERIOD: return '.';
		case VK_OEM_MINUS: return '-';
		case VK_OEM_PLUS: return '=';
		default: return -1;
	}
}

static void set_key(WPARAM vk, LPARAM lp, bool down) {
	// distinguish left/right shift/ctrl/alt
	if (vk == VK_SHIFT) {
		vk = MapVirtualKey((lp >> 16) & 0xff, MAPVK_VSC_TO_VK_EX);
	} else if (vk == VK_CONTROL) {
		vk = (lp & (1 << 24)) ? VK_RCONTROL : VK_LCONTROL;
	} else if (vk == VK_MENU) {
		vk = (lp & (1 << 24)) ? VK_RMENU : VK_LMENU;
	}
	int k = map_vk(vk);
	if (k >= 0) {
		input_set_button(BTN_KEY(k), down);
	}
	// generic menu buttons
	switch (vk) {
		case VK_UP: input_set_button(BTN_UP, down); break;
		case VK_DOWN: input_set_button(BTN_DOWN, down); break;
		case VK_LEFT: input_set_button(BTN_LEFT, down); break;
		case VK_RIGHT: input_set_button(BTN_RIGHT, down); break;
		case VK_RETURN: input_set_button(BTN_ACCEPT, down); break;
		case VK_ESCAPE: input_set_button(BTN_BACK, down); break;
		case 'P': input_set_button(BTN_PAUSE, down); break;
	}
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_CLOSE:
			W.quit = true;
			return 0;
		case WM_DROPFILES: {
			HDROP drop = (HDROP)wp;
			DragQueryFileA(drop, 0, S.dropped, sizeof(S.dropped));
			DragFinish(drop);
			return 0;
		}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			if (S.active) {
				if (wp == VK_ESCAPE) S.cancel = true;
				else if (wp == VK_RETURN || wp == 'B') S.browse = true;
				else if (wp == 'F') S.open_folder = true;
				return 0;
			}
			if (wp == VK_RETURN && (lp & (1 << 29))) { // Alt+Enter
				platform_set_fullscreen(!W.fullscreen);
				return 0;
			}
			if (wp == VK_F4 && (lp & (1 << 29))) {
				W.quit = true;
				return 0;
			}
			if (!(lp & (1 << 30))) { // ignore auto-repeat
				set_key(wp, lp, true);
			}
			return 0;
		case WM_KEYUP:
		case WM_SYSKEYUP:
			set_key(wp, lp, false);
			return 0;
		case WM_CHAR:
			if (wp >= 32) {
				input_text((int)wp);
			}
			return 0;
		case WM_MOUSEMOVE:
			W.mouse_cx = (short)LOWORD(lp);
			W.mouse_cy = (short)HIWORD(lp);
			W.mouse_seen = true;
			mouse_to_frame();
			return 0;
		case WM_SETCURSOR:   // the game draws its own pointer (UI_DrawPointer); no system arrow over the picture
			if (LOWORD(lp) == HTCLIENT && !S.active) {
				SetCursor(NULL);
				return TRUE;
			}
			break;
		case WM_INPUT: { // relative mouse motion for the mouse axes (the original's DirectInput mouse)
			RAWINPUT ri;
			UINT size = sizeof(ri);
			if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
				ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
				input_set_axis(AXIS_MOUSE_DX, (float)ri.data.mouse.lLastX);
				input_set_axis(AXIS_MOUSE_DY, (float)ri.data.mouse.lLastY);
			}
			break;
		}
		case WM_LBUTTONDOWN:
			if (S.active) S.browse = true;
			else input_set_button(BTN_MOUSE_LEFT, true);
			return 0;
		case WM_LBUTTONUP: input_set_button(BTN_MOUSE_LEFT, false); return 0;
		case WM_RBUTTONDOWN: input_set_button(BTN_MOUSE_RIGHT, true); return 0;
		case WM_RBUTTONUP: input_set_button(BTN_MOUSE_RIGHT, false); return 0;
		case WM_MBUTTONDOWN: input_set_button(BTN_MOUSE_MIDDLE, true); return 0;
		case WM_MBUTTONUP: input_set_button(BTN_MOUSE_MIDDLE, false); return 0;
		case WM_KILLFOCUS:
			for (int b = 1; b < BTN_MAX; b++) {
				input_set_button((button_t)b, false);
			}
			return 0;
		case WM_ERASEBKGND:
			return 1;
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- joystick / game pad (winmm)
// The original reads the first DirectInput joystick (c_dfDIJoystick: 6 axes, 10 buttons; the device list
// is enumerated again while there is none). Here: the first winmm joystick that answers joyGetPosEx,
// looked for every 2 s while there is none, read once per frame while the window has the focus.

static const button_t pad_buttons[10] = {BTN_PAD_A, BTN_PAD_B, BTN_PAD_X, BTN_PAD_Y, BTN_PAD_L1, BTN_PAD_R1,
	BTN_PAD_L2, BTN_PAD_R2, BTN_PAD_SELECT, BTN_PAD_START};

static float joy_norm(DWORD v, UINT lo, UINT hi) {
	if (hi <= lo) return 0;
	float f = ((float)v - (float)lo) / (float)(hi - lo) * 2.0f - 1.0f;
	return f < -1 ? -1 : f > 1 ? 1 : f;
}

static void joy_release(void) {
	for (int i = 0; i < 10; i++) input_set_button(pad_buttons[i], false);
	input_set_button(BTN_PAD_UP, false);
	input_set_button(BTN_PAD_DOWN, false);
	input_set_button(BTN_PAD_LEFT, false);
	input_set_button(BTN_PAD_RIGHT, false);
}

static bool joy_read(UINT id, JOYINFOEX *ji) {
	memset(ji, 0, sizeof(*ji));
	ji->dwSize = sizeof(*ji);
	ji->dwFlags = JOY_RETURNALL;
	return joyGetPosEx(id, ji) == JOYERR_NOERROR;
}

static void joy_poll(void) {
	JOYINFOEX ji;
	if (W.joy_id < 0) {
		double now = platform_now();
		if (now < W.joy_retry) return;
		W.joy_retry = now + 2.0;
		UINT n = joyGetNumDevs();
		for (UINT id = 0; id < n && id < 16 && W.joy_id < 0; id++) {
			if (joy_read(id, &ji) && joyGetDevCapsA(id, &W.joy_caps, sizeof(W.joy_caps)) == JOYERR_NOERROR) {
				W.joy_id = (int)id;
				input_set_pad_connected(true);
			}
		}
		if (W.joy_id < 0) return;
	}
	if (GetForegroundWindow() != W.hwnd) { // like a foreground DirectInput device: nothing while away
		joy_release();
		return;
	}
	if (!joy_read((UINT)W.joy_id, &ji)) {
		W.joy_id = -1;
		input_set_pad_connected(false);
		return;
	}
	const JOYCAPSA *c = &W.joy_caps;
	input_set_axis(AXIS_PAD_LX, joy_norm(ji.dwXpos, c->wXmin, c->wXmax));
	input_set_axis(AXIS_PAD_LY, joy_norm(ji.dwYpos, c->wYmin, c->wYmax));
	input_set_axis(AXIS_PAD_Z, (c->wCaps & JOYCAPS_HASZ) ? joy_norm(ji.dwZpos, c->wZmin, c->wZmax) : 0);
	// winmm's 4th-6th axes R, U, V: a game pad's right stick is U (x) and R (y), as DirectInput's Rx / Ry
	input_set_axis(AXIS_PAD_RX, (c->wCaps & JOYCAPS_HASU) ? joy_norm(ji.dwUpos, c->wUmin, c->wUmax) : 0);
	input_set_axis(AXIS_PAD_RY, (c->wCaps & JOYCAPS_HASR) ? joy_norm(ji.dwRpos, c->wRmin, c->wRmax) : 0);
	input_set_axis(AXIS_PAD_RZ, (c->wCaps & JOYCAPS_HASV) ? joy_norm(ji.dwVpos, c->wVmin, c->wVmax) : 0);
	for (int i = 0; i < 10; i++) input_set_button(pad_buttons[i], (ji.dwButtons >> i) & 1);
	bool up = false, down = false, left = false, right = false;
	if ((c->wCaps & JOYCAPS_HASPOV) && ji.dwPOV != JOY_POVCENTERED && ji.dwPOV < 36000) {  // hundredths of a degree
		DWORD a = ji.dwPOV;
		up = a > 27000 || a < 9000;
		right = a > 0 && a < 18000;
		down = a > 9000 && a < 27000;
		left = a > 18000;
	}
	input_set_button(BTN_PAD_UP, up);
	input_set_button(BTN_PAD_DOWN, down);
	input_set_button(BTN_PAD_LEFT, left);
	input_set_button(BTN_PAD_RIGHT, right);
}

void platform_pump_events(void) {
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			W.quit = true;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	mouse_to_frame();
	joy_poll();
}

// ---------------------------------------------------------------- audio

void platform_audio_lock(void) { EnterCriticalSection(&W.audio_lock); }
void platform_audio_unlock(void) { LeaveCriticalSection(&W.audio_lock); }

static DWORD WINAPI audio_thread(LPVOID arg) {
	while (!W.quit) {
		WaitForSingleObject(W.audio_event, 50);
		for (int i = 0; i < AUDIO_BUFFERS; i++) {
			WAVEHDR *h = &W.hdr[i];
			if (h->dwFlags & WHDR_DONE || !(h->dwFlags & WHDR_PREPARED)) {
				if (h->dwFlags & WHDR_PREPARED) {
					waveOutUnprepareHeader(W.wave, h, sizeof(*h));
				}
				EnterCriticalSection(&W.audio_lock);
				mixer_render(W.buf[i], AUDIO_FRAMES);
				LeaveCriticalSection(&W.audio_lock);
				memset(h, 0, sizeof(*h));
				h->lpData = (LPSTR)W.buf[i];
				h->dwBufferLength = sizeof(W.buf[i]);
				waveOutPrepareHeader(W.wave, h, sizeof(*h));
				waveOutWrite(W.wave, h, sizeof(*h));
			}
		}
	}
	return 0;
}

static void audio_init(void) {
	InitializeCriticalSection(&W.audio_lock);
	mixer_init(AUDIO_RATE);
	WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 2, AUDIO_RATE, AUDIO_RATE * 4, 4, 16, 0};
	W.audio_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (waveOutOpen(&W.wave, WAVE_MAPPER, &fmt, (DWORD_PTR)W.audio_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		W.wave = NULL;
		return;
	}
	W.audio_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}

static void audio_shutdown(void) {
	if (!W.wave) {
		return;
	}
	SetEvent(W.audio_event);
	WaitForSingleObject(W.audio_thread, 1000);
	waveOutReset(W.wave);
	for (int i = 0; i < AUDIO_BUFFERS; i++) {
		if (W.hdr[i].dwFlags & WHDR_PREPARED) {
			waveOutUnprepareHeader(W.wave, &W.hdr[i], sizeof(W.hdr[i]));
		}
	}
	waveOutClose(W.wave);
}

// ---------------------------------------------------------------- first start: the player's disc image

// a centred, word-wrapped text at y (drawn, or only measured); returns its height
static int setup_text(HDC dc, HFONT font, COLORREF colour, int y, int cw, const char *text, bool draw) {
	SelectObject(dc, font);
	SetTextColor(dc, colour);
	RECT r = {cw / 12, y, cw - cw / 12, y};
	DrawTextA(dc, text[0] ? text : " ", -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	int h = r.bottom - r.top;
	r.left = cw / 12;   // DT_CALCRECT narrowed it to the widest line
	r.right = cw - cw / 12;
	if (draw) DrawTextA(dc, text, -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
	return h;
}

// title, body lines, an error (orange) and a progress bar (progress >= 0), centred; returns the height
static int setup_layout(HDC dc, int y, int cw, const char *const *lines, int n, const char *error, float progress,
	bool draw) {
	int y0 = y;
	y += setup_text(dc, S.big, RGB(0, 235, 255), y, cw, lines[0], draw) + S.font_h;
	for (int i = 1; i < n; i++) {
		y += setup_text(dc, S.small, RGB(220, 225, 235), y, cw, lines[i], draw);
	}
	if (error && error[0]) {
		y += S.font_h / 2;
		y += setup_text(dc, S.small, RGB(255, 160, 60), y, cw, error, draw);
	}
	if (progress >= 0) {
		y += S.font_h;
		if (draw) {
			RECT bar = {cw / 3, y, cw - cw / 3, y + S.font_h}, fill = bar;
			HBRUSH frame = CreateSolidBrush(RGB(0, 235, 255)), body = CreateSolidBrush(RGB(0, 150, 170));
			FrameRect(dc, &bar, frame);
			fill.left += 2;
			fill.top += 2;
			fill.bottom -= 2;
			fill.right = fill.left + (LONG)((bar.right - bar.left - 4) * (progress > 1 ? 1 : progress));
			FillRect(dc, &fill, body);
			DeleteObject(frame);
			DeleteObject(body);
		}
		y += S.font_h;
	}
	return y - y0;
}

static void setup_draw(const char *const *lines, int n, const char *error, float progress) {
	RECT r;
	GetClientRect(W.hwnd, &r);
	int cw = r.right - r.left, ch = r.bottom - r.top;
	if (cw <= 0 || ch <= 0) return;   // minimised
	int font_h = ch / 34 < 16 ? 16 : ch / 34 > 40 ? 40 : ch / 34;   // the text follows the window's size
	if (font_h != S.font_h) {
		if (S.big) DeleteObject(S.big);
		if (S.small) DeleteObject(S.small);
		S.big = CreateFontA(font_h * 2, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
		S.small = CreateFontA(font_h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
			"Segoe UI");
		S.font_h = font_h;
	}
	HDC dc = GetDC(W.hwnd);
	HDC mem = CreateCompatibleDC(dc);
	HBITMAP bmp = CreateCompatibleBitmap(dc, cw, ch);
	HGDIOBJ old = SelectObject(mem, bmp);
	HBRUSH bg = CreateSolidBrush(RGB(10, 16, 34));
	FillRect(mem, &r, bg);
	DeleteObject(bg);
	SetBkMode(mem, TRANSPARENT);
	int h = setup_layout(mem, 0, cw, lines, n, error, progress, false);
	setup_layout(mem, h < ch ? (ch - h) / 2 : 0, cw, lines, n, error, progress, true);
	BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(W.hwnd, dc);
}

bool platform_ask_disc_image(char *path, size_t size, const char *disc_folder, const char *message) {
	bool was_full = W.fullscreen;
	if (was_full) platform_set_fullscreen(false);   // so the image can be dragged from Explorer
	S.active = true;
	S.dropped[0] = 0;
	S.browse = S.cancel = S.open_folder = false;
	DragAcceptFiles(W.hwnd, TRUE);
	SetForegroundWindow(W.hwnd);
	char folder[MAX_PATH], put[MAX_PATH + 96];   // Windows spelling of the disc folder, and its name
	snprintf(folder, sizeof(folder), "%s", disc_folder);
	for (char *c = folder; *c; c++) {
		if (*c == '/') *c = '\\';
	}
	const char *name = strrchr(folder, '\\') ? strrchr(folder, '\\') + 1 : folder;
	snprintf(put, sizeof(put), "the folder \"%s\" in the game's folder,", name);
	const char *lines[] = {
		"LANDER",
		"A fan-made remake of the 1999 game by Psygnosis. It contains no part of the game:",
		"it runs on your own Lander CD.",
		"",
		"Put your Lander disc image (.cue + .bin, .mds + .mdf, or .iso) into",
		put,
		"then start the game again.",
		"",
		"Or drag the image onto this window, or click to choose it.",
		"F opens the folder.   Esc quits.",
	};
	bool ok = false;
	while (!W.quit && !S.cancel) {
		platform_pump_events();
		setup_draw(lines, (int)(sizeof(lines) / sizeof(lines[0])), message, -1);
		if (S.dropped[0]) {
			snprintf(path, size, "%s", S.dropped);
			ok = true;
			break;
		}
		if (S.open_folder) {
			S.open_folder = false;
			ShellExecuteA(W.hwnd, "open", folder, NULL, NULL, SW_SHOWNORMAL);
		}
		if (S.browse) {
			S.browse = false;
			char file[MAX_PATH] = "";
			OPENFILENAMEA ofn = {0};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = W.hwnd;
			ofn.lpstrFilter = "Lander disc image (*.iso;*.cue;*.bin;*.mds;*.mdf;*.img)\0*.iso;*.cue;*.bin;*.mds;*.mdf;*.img\0"
				"All files\0*.*\0";
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrInitialDir = folder;
			ofn.lpstrTitle = "Choose your Lander disc image";
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (GetOpenFileNameA(&ofn)) {
				snprintf(path, size, "%s", file);
				ok = true;
				break;
			}
		}
		Sleep(16);
	}
	DragAcceptFiles(W.hwnd, FALSE);
	S.active = false;
	if (was_full) platform_set_fullscreen(true);
	return ok;
}

void platform_show_status(const char *text, float progress) {
	double now = platform_now();
	if (now - S.last_draw < 1.0 / 30 && progress < 1) {
		return;
	}
	S.last_draw = now;
	MSG msg;   // keep the window responsive (no game input meanwhile)
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) W.quit = true;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	const char *lines[] = {"LANDER", "Installing the game from your disc image...", text};
	setup_draw(lines, 3, NULL, progress);
}
// ---------------------------------------------------------------- main

int main(int argc, char **argv) {
	QueryPerformanceFrequency(&W.freq);
	QueryPerformanceCounter(&W.start);
	timeBeginPeriod(1);

	// save folder: %APPDATA%\LanderRewrite
	const char *appdata = getenv("APPDATA");
	snprintf(W.save_dir, sizeof(W.save_dir), "%s\\LanderRewrite", appdata ? appdata : ".");
	CreateDirectoryA(W.save_dir, NULL);
	DWORD n = GetModuleFileNameA(NULL, W.program_dir, sizeof(W.program_dir));   // the program's folder
	char *slash = n && n < sizeof(W.program_dir) ? strrchr(W.program_dir, '\\') : NULL;
	if (slash) *slash = 0; else strcpy(W.program_dir, ".");

	WNDCLASSA wc = {0};
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(1));   // src/win/lander.rc
	if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.lpszClassName = "LanderRewrite";
	RegisterClassA(&wc);
	RECT r = {0, 0, 1280, 960};
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	W.hwnd = CreateWindowA("LanderRewrite", "Lander", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
	RAWINPUTDEVICE mouse = {0x01, 0x02, 0, W.hwnd};   // generic desktop / mouse: WM_INPUT while in front
	RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
	joy_poll();   // a game pad that is already there is known before the game reads its control config

	// full screen like the original (DirectDraw exclusive mode); --windowed for a window, Alt+Enter switches
	bool windowed = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--windowed")) windowed = true;
	}
	if (!windowed) {
		platform_set_fullscreen(true);
	}

	audio_init();
	game_init(argc, argv);
	while (!W.quit) {
		platform_pump_events();
		game_frame();
	}
	game_cleanup();
	audio_shutdown();
	timeEndPeriod(1);
	return 0;
}
