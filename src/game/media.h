// Films, pictures and music (spec: re/specs/media.md).
// * Films: the MPEG-1 films of the disc (INTRO, CUT1-3, OUTRO, CREDITS; 352x288, 25 fps, MP2 sound),
//   played through MCI / DirectShow by Media_VideoPlay 0x4645d0 in the original, decoded here with
//   pl_mpeg into the render buffer (full screen 640x480, like the original's full-screen window)
//   and the mixer. Esc skips a film, P pauses / resumes it.
// * Pictures ("slides"): 640x480 pictures of MISC\SCREENS6 / MISC\SLIDES6 (Slide_* 0x41d430-0x41d720):
//   front-end backgrounds, the start-up pictures, the mission loading screen.
// * Music: the CD audio tracks (Media_AudioPlay 0x4641e0, Media_Poll 0x4643a0, Music_Stop 0x4643d0;
//   the CD tracks are MUSIC\TRACKnn.WAV here). sound.c's music_play / music_stop forward to these.
#ifndef MEDIA_H
#define MEDIA_H

#include "engine.h"

// ---------------------------------------------------------------- films
// Film slots of the original's file table (Media_VideoPlay argument). MISC is the film named by
// SetLevelEndMovie (game_level_end_movie()); after PLUTO's (world 19) film the game also plays CREDITS.
enum {
	FILM_INTRO = 0x29, FILM_OUTRO = 0x2a, FILM_CUT1 = 0x2c, FILM_CUT2 = 0x2d, FILM_CUT3 = 0x2e,
	FILM_CS4 = 0x2f, FILM_CREDITS = 0x30, FILM_MISC = 0x31,
};
const char *media_film_slot_name(int slot);   // "INTRO", ... (NULL for MISC / unknown)

// Open a film by the name the game uses ("INTRO", "CUT2", SetLevelEndMovie's name; "<name>.MPG" in
// the data folder or MISC). Stops the CD music first, like Media_VideoPlay. Any film already playing
// and the queue are dropped. false when the film is not on the disc.
bool media_film_open(const char *name);
// The films after a mission (Game_ModeLoop 0x410440): SetLevelEndMovie's film (movie =
// game_level_end_movie(); "" = none), then CREDITS when the mission was on PLUTO (world 19).
// Returns true when something plays: call media_film_frame() each frame until it returns false.
bool media_level_end_films(const char *movie, int world);
// Add a film to play after the current one (opens it at once when nothing plays). false if missing.
bool media_film_queue(const char *name);
// Once per program frame: decodes what is due at platform_now(), draws the current picture into the
// whole render buffer and feeds the film's sound to the mixer. Esc ends the current film (the next
// queued one starts), P pauses / resumes. Returns false when nothing is left to play (the render
// buffer is then black).
bool media_film_frame(void);
void media_film_close(void);                  // stop everything, release the decoder and the sound
bool media_film_playing(void);
const char *media_film_name(void);            // the film playing ("" = none)
// Draw the frame due at film time t (seconds since the film was opened), decoding as needed; no
// clock, no keys. For tests and benchmarks. Returns false at the end of the film.
bool media_film_update(double t);
// Statistics of the current film: decoded video frames, decoded / pushed audio frames (stereo pairs)
typedef struct {
	int width, height;
	double framerate, duration;
	int samplerate;
	long video_frames, audio_frames, audio_pushed, audio_dropped;
	double decode_cpu, scale_cpu;   // processor seconds spent decoding (pl_mpeg) and scaling to the screen
} media_film_stats_t;
bool media_film_stats(media_film_stats_t *out);

// ---------------------------------------------------------------- pictures (slides)
// Picture sets: 0 = MISC\SCREENS6 (12 pictures: 0 work in progress, 1-10 front-end backgrounds,
// 11 the soundtrack advert), 1 = MISC\SLIDES6 (6: 0 title, 1 controls card, 2 logo, 3-5 adverts).
void media_slide_select_dir(int set);         // Slides_SelectDir 0x427140
bool media_slide_open(int k);                 // Slide_Load 0x41d4a0: picture k; k < 0 = random 1..10
void media_slide_draw(void);                  // Slide_Draw 0x41d650: 640x480 at the top left
void media_slide_draw_scaled(int w, int h);   // Slide_DrawScaled 0x41d720: from a w x h screen to the render size
void media_slide_unload(void);                // Slide_Unload 0x41d4e0
// Name form of select + open: "screens/<k>" or "slides/<k>" (case-insensitive; k may be -1 = random).
bool media_slide_load(const char *name);

// The mission loading screen (Game_ModeLoop 0x410440 + Mission_Load 0x413d20): the SLIDES6 title
// picture with the progress box; progress 0..256 (the bar's width in pixels). Loads the picture on
// the first call; media_loading_done() releases it.
void media_loading_draw(int progress);
void media_loading_done(void);

// ---------------------------------------------------------------- start-up (Game_Startup 0x4101d0)
// The media part of the start-up: the intro film, the "soundtrack" picture (SCREENS6 #11) shown up to
// 4 s (a key skips it after 200 ms), then the logo (SLIDES6 #2). Call media_startup_frame once per
// frame; it returns false when done, with the logo in the render buffer (shown while the front end
// loads). demo = the demo build's order (no soundtrack picture, no wait).
void media_startup_begin(bool demo);
bool media_startup_frame(void);

// ---------------------------------------------------------------- music (CD audio)
// Media_AudioPlay 0x4641e0 (mission API, sound.c -> music_play): 0 = random track, n = CD track n+1
void media_music_play(int track);
void media_music_stop(void);                  // Music_Stop 0x4643d0
void media_music_poll(void);                  // Media_Poll 0x4643a0: next track when one has ended
int media_music_track(void);                  // CD track playing (2..), 0 = none

#endif
