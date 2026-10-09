// CD music: Media_AudioPlay 0x4641e0, Media_Poll 0x4643a0, Music_Stop 0x4643d0 with the backend the
// retail CD uses, MCI "cdaudio" (backend 2, FUN_00464f70 / FUN_00465210 / FUN_00465610). The audio
// tracks of the disc are files here: CD track n = MUSIC\TRACKnn.WAV. re/specs/media.md §3.
// The CD's track count includes the data track 1, so "random" (rand() % tracks + 1, CD track + 1) can
// pick a track past the last one: nothing plays until the next poll moves on to track 2, as on the CD.
#include <stdio.h>
#include <stdlib.h>

#include "media.h"
#include "../fs.h"
#include "../mixer.h"
#include "../platform.h"

#define POLL_PERIOD 1.0 // the AudioProbe task polls when its time is a whole second

static struct {
	bool init;
	bool active;        // DAT_00560f6c: set by Media_AudioPlay, cleared by Music_Stop
	int num_tracks;     // DAT_00561280: "status cdaudio number of tracks" (data track included)
	int cur;            // DAT_00561278: music track; the CD track is cur + 1
	bool specific;      // DAT_00499344: a track was asked for (else random)
	bool playing;       // DAT_00561274
	double next_poll;
} MU;

static bool track_path(int cd_track, char *out, size_t size) {
	char path[64];
	snprintf(path, sizeof(path), "MUSIC\\TRACK%02d.WAV", cd_track);
	return fs_resolve(path, out, size);
}

static bool music_init(void) { // Music_Init(5) 0x4640d0 -> FUN_00465460: open cdaudio, count the tracks
	if (!MU.init) {
		char real[1024];
		MU.num_tracks = 1;
		while (MU.num_tracks < 99 && track_path(MU.num_tracks + 1, real, sizeof(real))) {
			MU.num_tracks++;
		}
		MU.init = true;
	}
	return MU.num_tracks > 1; // "1 < number of tracks"
}

static void select_track(int n) { // FUN_00465210 (cdaudio)
	if (n > MU.num_tracks || n < 1) {
		n = 1;
	}
	MU.cur = n;
}

static void cd_stop(void) { // FUN_00465150 "stop cdaudio"
	platform_audio_lock();
	mixer_music_stop();
	platform_audio_unlock();
	MU.playing = false;
}

static void cd_play(int n) { // FUN_00464f70
	if (n == 0) {
		select_track(rand() % MU.num_tracks + 1);
		MU.specific = false;
	} else {
		select_track(n);
		MU.specific = true;
	}
	if (MU.playing) {
		cd_stop();
	}
	// "status cdaudio length track <cur+1>" fails for a track that is not on the disc: nothing plays
	char real[1024];
	if (!track_path(MU.cur + 1, real, sizeof(real))) {
		return;
	}
	// "play cdaudio from <cur+1>" (to the end of the disc; Media_Poll stops it at the track's end)
	platform_audio_lock();
	bool ok = mixer_music_play(real, false);
	platform_audio_unlock();
	MU.playing = ok;
}

void media_music_play(int track) { // Media_AudioPlay 0x4641e0
	MU.active = true;
	if (!music_init()) {
		return;
	}
	cd_play(track);
	MU.next_poll = platform_now() + POLL_PERIOD;
}

void media_music_poll(void) { // Media_Poll 0x4643a0 -> FUN_00465610
	if (!MU.active || !MU.init || MU.num_tracks <= 1) {
		return;
	}
	double now = platform_now();
	if (now < MU.next_poll) {
		return;
	}
	MU.next_poll = now + POLL_PERIOD;
	// "status cdaudio position": still inside track cur+1 and not at its end -> nothing to do
	platform_audio_lock();
	bool in_track = MU.playing && mixer_music_playing();
	platform_audio_unlock();
	if (in_track) {
		return;
	}
	if (!MU.specific && ++MU.cur == MU.num_tracks) {
		MU.cur = 0;
	}
	cd_stop();
	cd_play(MU.cur + 1); // a specific track from now on: the next track each time
}

void media_music_stop(void) { // Music_Stop 0x4643d0
	if (MU.active) {
		if (MU.playing) {
			cd_stop();
		}
		MU.active = false;
	}
}

int media_music_track(void) {
	platform_audio_lock();
	bool on = MU.active && MU.playing && mixer_music_playing();
	platform_audio_unlock();
	return on ? MU.cur + 1 : 0;
}
