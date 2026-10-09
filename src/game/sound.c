// Sound database (sounds.dat, SoundDB_Parse 0x47dc90) and playback of sound ids
// (Sound_* exports 0x47d180..), positional by the owner object, plus music.
// See re/specs/small-formats.md §2.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <strings.h>

#include "sound.h"
#include "media.h"
#include "../mixer.h"
#include "../fs.h"
#include "../platform.h"

#define MAX_SID 3001
#define MAX_ENTRIES 500
#define MAX_VOICES 32
#define MAX_CHAIN 8

typedef struct {
	sfx_t *sfx;
	bool own_sfx;
	int type;          // 1 = 2D, 2 = 3D
	int priority;
	int group;         // 0 HUD/voice, 1 engine, 2 other
	float mindist, maxdist;
} snd_entry_t;

typedef struct {
	bool used;
	int handle;        // mixer voice
	ObjId owner;
	const snd_entry_t *e;
	float volume;      // 0..1
	float freq;
	int loops;
	int32_t chain[MAX_CHAIN];
	int chain_len, chain_pos;
	bool chain_loop;
	uint32_t serial;
} voice_t;

static snd_entry_t entries[MAX_ENTRIES];
static int num_entries, num_global;
static snd_entry_t *table[MAX_SID];
static voice_t voices[MAX_VOICES];
static uint32_t serial;
static float rolloff = 2.0f;
static vec3_t listener_pos, listener_right = {1, 0, 0};
static float vol_sfx = 1.0f, vol_music = 1.0f;

static const char *language_dir(void) {
	static const char *names[] = {"English", "French", "German"};
	return names[gLanguage >= 0 && gLanguage < 3 ? gLanguage : 0];
}

static sfx_t *find_wav(const char *dir, const char *file) {
	char path[512];
	const char *tries[4][2] = {{dir, "SOUND\\%s\\"}, {dir, "SOUND\\"}, {"", "SOUND\\%s\\"}, {"", "SOUND\\"}};
	for (int i = 0; i < 4; i++) {
		char sub[64];
		snprintf(sub, sizeof(sub), tries[i][1], language_dir());
		snprintf(path, sizeof(path), "%s%s%s%s", tries[i][0], tries[i][0][0] ? "\\" : "", sub, file);
		sfx_t *s = sfx_load_wav(path);
		if (s) {
			return s;
		}
	}
	return NULL;
}

static void parse_db(const char *path, const char *dir) { // SoundDB_Parse 0x47dc90
	uint32_t size;
	char *d = (char *)fs_load(path, &size);
	if (!d) {
		return;
	}
	d = realloc(d, size + 1);
	d[size] = 0;
	char *line = d;
	while (line && *line) {
		char *next = strchr(line, '\n');
		if (next) *next++ = 0;
		char *p = line;
		while (*p == ' ' || *p == '\t' || *p == '\r') p++;
		char tok[4] = {0};
		sscanf(p, "%3s", tok);
		if (!strncasecmp(tok, "END", 3)) {
			break;
		} else if (!strncasecmp(tok, "ROL", 3)) {
			char *q = p;
			while (*q && !isspace((unsigned char)*q)) q++;
			rolloff = (float)atof(q);
		} else if (isdigit((unsigned char)p[0])) {
			char *q = p;
			unsigned long sid = strtoul(q, &q, 0);
			char file[128] = {0}, strat[8] = {0}, vol[8] = {0};
			int n = 0;
			sscanf(q, " %127s%n", file, &n);
			q += n;
			long alias = -1;
			if (!strncmp(file, "ALIAS", 5)) {
				alias = (long)strtoul(q, &q, 0);
			}
			unsigned long prio = 0;
			float mind = 0, maxd = 0;
			sscanf(q, " %3s", strat);
			while (*q && isspace((unsigned char)*q)) q++;
			while (*q && !isspace((unsigned char)*q)) q++;
			prio = strtoul(q, &q, 0);
			sscanf(q, " %3s %f %f", vol, &mind, &maxd);
			if (sid >= MAX_SID || num_entries >= MAX_ENTRIES) {
				line = next;
				continue;
			}
			snd_entry_t e = {0};
			if (alias >= 0) {
				if (alias < MAX_SID && table[alias]) {
					e.sfx = table[alias]->sfx;
				}
			} else {
				e.sfx = find_wav(dir, file);
				e.own_sfx = true;
			}
			if (!e.sfx) { // missing WAV: the line is dropped
				line = next;
				continue;
			}
			e.type = strncasecmp(strat, "2DS", 3) ? 2 : 1;
			e.priority = (int)prio;
			e.group = !strncasecmp(vol, "HUD", 3) ? 0 : !strncasecmp(vol, "ENG", 3) ? 1 : 2;
			e.mindist = mind;
			e.maxdist = maxd;
			entries[num_entries] = e;
			table[sid] = &entries[num_entries++];
		}
		line = next;
	}
	free(d);
}

void sound_init(void) {
	memset(table, 0, sizeof(table));
	num_entries = num_global = 0;
	parse_db("SOUND\\SOUNDS.DAT", "SOUND");
	num_global = num_entries;
}

void sound_free_mission(void) { // SoundDB_FreeMission 0x47da90
	sound_stop_all();
	for (int i = num_global; i < num_entries; i++) {
		if (entries[i].own_sfx) {
			sfx_free(entries[i].sfx);
		}
	}
	for (int s = 0; s < MAX_SID; s++) {
		if (table[s] && table[s] >= &entries[num_global]) {
			table[s] = NULL;
		}
	}
	num_entries = num_global;
	// global ids redefined by the mission file are restored by re-pointing them
	for (int i = 0; i < num_global; i++) {
		(void)i;
	}
}

void sound_load_database(const char *mission_dir) {
	if (!num_global && !num_entries) {
		sound_init();
	}
	sound_free_mission();
	char path[256];
	snprintf(path, sizeof(path), "%s\\SOUNDS.DAT", mission_dir);
	parse_db(path, mission_dir);
}

void sound_set_listener(vec3_t pos, vec3_t right) {
	listener_pos = pos;
	listener_right = right;
}

void sound_set_volumes(float sfx, float music) {
	vol_sfx = sfx;
	vol_music = music;
	platform_audio_lock();
	mixer_set_master(1.0f, music);
	platform_audio_unlock();
}

// ---------------------------------------------------------------- voices

static voice_t *voice_from_handle(int32_t h) {
	if (h <= 0) return NULL;
	int i = (h - 1) % MAX_VOICES;
	voice_t *v = &voices[i];
	return v->used && v->serial == (uint32_t)h ? v : NULL;
}

static bool owner_pos(ObjId owner, vec3_t *out) {
	if (owner == OBJ_NONE || owner == SND_COCKPIT_VOICE || !Object_IsAwake(owner)) {
		return false;
	}
	const int32_t *p = (const int32_t *)Object_GetProperty(owner, PROP_POSITION);
	if (!p) {
		return false;
	}
	*out = vec3((float)p[0], (float)p[1], (float)p[2]);
	return true;
}

// 3D voices (DirectSound3D in the original): positions go to DirectSound in 1/64 world units
// (0.015625, FUN_00480fb0), so a sound's MINDIST / MAXDIST from SOUNDS.DAT are x64 in world units.
// A voice whose owner is asleep is not audible (FUN_0047f720 flags it, FUN_0047ff80 gives it no
// buffer), nor is one beyond its MAXDIST (FUN_0047ff80); inside that DirectSound's inverse rolloff
// applies: min / (min + rolloff * (d - min)).
#define DS_UNITS_PER_WORLD (1.0f / 64.0f)

static void voice_mix_params(voice_t *v, float *gain, float *pan) {
	*gain = v->volume * vol_sfx;
	*pan = 0;
	if (v->e->type != 2 || v->owner == OBJ_NONE || v->owner == SND_COCKPIT_VOICE) {
		return;   // 2D, or a 3D sound at the listener
	}
	vec3_t p;
	if (!owner_pos(v->owner, &p)) {
		*gain = 0;
		return;
	}
	vec3_t d = vec3_sub(p, listener_pos);
	float world = vec3_len(d), dist = world * DS_UNITS_PER_WORLD;
	float mind = v->e->mindist > 0 ? v->e->mindist : 1;
	float maxd = v->e->maxdist > mind ? v->e->maxdist : mind;
	if (dist > maxd) {
		*gain = 0;
		return;
	}
	if (dist > mind) {
		*gain *= mind / (mind + rolloff * (dist - mind));
	}
	if (world > 1) {
		*pan = clamp(vec3_dot(d, listener_right) / world, -1.0f, 1.0f) * 0.8f;
	}
}
// count: plays of the sample (the original's loop count, FUN_0047f500: 1 once, n times, -1 forever)
static bool voice_start(voice_t *v, int32_t sid, int count) {
	const snd_entry_t *e = sid >= 0 && sid < MAX_SID ? table[sid] : NULL;
	if (!e) {
		return false;
	}
	v->e = e;
	float gain, pan;
	voice_mix_params(v, &gain, &pan);
	platform_audio_lock();
	v->handle = mixer_play_n(e->sfx, gain, pan, v->freq, count == -1 ? 0 : max(count, 1));
	platform_audio_unlock();
	return v->handle >= 0;
}

static voice_t *voice_alloc(void) {
	for (int i = 0; i < MAX_VOICES; i++) {
		if (!voices[i].used) {
			voice_t *v = &voices[i];
			memset(v, 0, sizeof(*v));
			v->used = true;
			serial = (serial + 1) & 0xfffff;
			v->serial = serial * MAX_VOICES + (uint32_t)i + 1; // handle: (serial, slot)
			v->freq = 1;
			return v;
		}
	}
	return NULL;
}

int32_t sound_play(ObjId owner, int32_t sid, int32_t loops, int32_t volume) {
	if (sid < 0 || sid >= MAX_SID || !table[sid]) {
		return 0;
	}
	voice_t *v = voice_alloc();
	if (!v) {
		return 0;
	}
	v->owner = owner;
	v->volume = volume >= 0 ? volume / 65535.0f : 1;
	v->loops = loops;
	if (!voice_start(v, sid, loops)) {
		v->used = false;
		return 0;
	}
	return (int32_t)v->serial;
}

int32_t sound_chain(ObjId owner, int32_t volume, int loop, int n, const int32_t *sids) {
	if (n <= 0) {
		return 0;
	}
	if (owner == SND_COCKPIT_VOICE) { // one cockpit voice line at a time
		sound_stop_object(owner);
	}
	voice_t *v = voice_alloc();
	if (!v) {
		return 0;
	}
	v->owner = owner;
	v->volume = volume >= 0 ? volume / 65535.0f : 1;
	v->chain_len = min(n, MAX_CHAIN);
	memcpy(v->chain, sids, sizeof(int32_t) * v->chain_len);
	v->chain_loop = loop;
	v->chain_pos = 0;
	while (v->chain_pos < v->chain_len && !voice_start(v, v->chain[v->chain_pos], 1)) {
		v->chain_pos++;
	}
	if (v->chain_pos >= v->chain_len) {
		v->used = false;
		return 0;
	}
	return (int32_t)v->serial;
}

void sound_stop(int32_t h) {
	voice_t *v = voice_from_handle(h);
	if (v) {
		platform_audio_lock();
		mixer_stop(v->handle);
		platform_audio_unlock();
		v->used = false;
	}
}

void sound_stop_object(ObjId owner) {
	for (int i = 0; i < MAX_VOICES; i++) {
		if (voices[i].used && (voices[i].owner & 0x3ffffff) == (owner & 0x3ffffff)) {
			sound_stop((int32_t)voices[i].serial);
		}
	}
}

void sound_stop_all(void) {
	for (int i = 0; i < MAX_VOICES; i++) {
		if (voices[i].used) {
			sound_stop((int32_t)voices[i].serial);
		}
	}
}

void sound_set_volume(int32_t h, int32_t volume) {
	voice_t *v = voice_from_handle(h);
	if (v) v->volume = volume / 65535.0f;
}

void sound_set_freq(int32_t h, float f) {
	voice_t *v = voice_from_handle(h);
	if (v) v->freq = f;
}

float sound_get_freq(int32_t h) {
	voice_t *v = voice_from_handle(h);
	return v ? v->freq : 1;
}

bool sound_playing(int32_t h) { return voice_from_handle(h) != NULL; }

void sound_update(void) {
	// LANDER_SOUND_DEBUG=1: the playing voices and their gain every 2 s of game time
	static int debug = -1;
	static uint32_t last_print;
	if (debug < 0) debug = getenv("LANDER_SOUND_DEBUG") != NULL;
	if (debug && Clock_Now() - last_print >= 2000) {
		last_print = Clock_Now();
		for (int i = 0; i < MAX_VOICES; i++) {
			voice_t *v = &voices[i];
			if (!v->used || !v->e) continue;
			float g, pn;
			voice_mix_params(v, &g, &pn);
			vec3_t p = listener_pos;
			bool has = owner_pos(v->owner, &p);
			printf("sound t=%u owner %08x %s dist %.0f gain %.3f\n", Clock_Now(), v->owner, has ? "awake" : "-",
				vec3_len(vec3_sub(p, listener_pos)), g);
		}
	}
	for (int i = 0; i < MAX_VOICES; i++) {
		voice_t *v = &voices[i];
		if (!v->used) {
			continue;
		}
		platform_audio_lock();
		bool playing = mixer_playing(v->handle);
		platform_audio_unlock();
		if (!playing) {
			if (v->chain_len && v->chain_pos + 1 < v->chain_len) {
				v->chain_pos++;
				voice_start(v, v->chain[v->chain_pos], 1);
			} else if (v->chain_len && v->chain_loop) {
				v->chain_pos = 0;
				voice_start(v, v->chain[0], 1);
			} else {
				v->used = false;
			}
			continue;
		}
		float gain, pan;
		voice_mix_params(v, &gain, &pan);
		platform_audio_lock();
		mixer_set(v->handle, gain, pan, v->freq);
		platform_audio_unlock();
	}
	media_music_poll(); // AudioProbe task 0x491b20: Sound_Update, then Media_Poll (once per second)
}

// ---------------------------------------------------------------- music (Media_AudioPlay 0x4641e0)

void music_play(int track) { // CD track logic in media_music.c
	media_music_play(track);
	platform_audio_lock();
	mixer_set_master(1.0f, vol_music);
	platform_audio_unlock();
}

void music_stop(void) { media_music_stop(); } // Music_Stop 0x4643d0

// ---------------------------------------------------------------- exports (mission_api.h)

int32_t Sound_PlayOnce(ObjId obj, int32_t sid) { return sound_play(obj, sid, 1, -1); }
int32_t Sound_PlayLoop(ObjId obj, int32_t sid, int32_t loops) { return sound_play(obj, sid, loops, -1); }

int32_t Sound_PlayChain(ObjId obj, int32_t n, ...) {
	int32_t sids[MAX_CHAIN];
	va_list ap;
	va_start(ap, n);
	for (int i = 0; i < n; i++) {
		int32_t s = va_arg(ap, int32_t);
		if (i < MAX_CHAIN) sids[i] = s;
	}
	va_end(ap);
	return sound_chain(obj, -1, 0, min(n, MAX_CHAIN), sids);
}

void Sound_StopObject(ObjId obj) { sound_stop_object(obj); }
void Media_AudioPlay(int32_t track) { music_play(track); }

int32_t CockVox(int32_t priority) { // 0x40c860: may start a cockpit-voice line
	(void)priority;
	for (int i = 0; i < MAX_VOICES; i++) {
		if (voices[i].used && voices[i].owner == SND_COCKPIT_VOICE) {
			return 0;
		}
	}
	return 1;
}
