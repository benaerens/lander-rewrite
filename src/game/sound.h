#ifndef SOUND_H
#define SOUND_H

#include "engine.h"

void sound_init(void);
void sound_load_database(const char *mission_dir);  // global + mission sounds.dat (0x4800d0)
void sound_free_mission(void);
void sound_set_listener(vec3_t pos, vec3_t right);
void sound_update(void);                            // per frame: 3D positions, chains
void sound_stop_all(void);
void sound_set_volumes(float sfx, float music);

// internal forms used by the ports (eng_snd_*), volume 0..0xffff, loops -1 = forever
int32_t sound_play(ObjId owner, int32_t sid, int32_t loops, int32_t volume);
int32_t sound_chain(ObjId owner, int32_t volume, int loop, int n, const int32_t *sids);
void sound_stop(int32_t handle);
void sound_stop_object(ObjId owner);
void sound_set_volume(int32_t handle, int32_t volume);
void sound_set_freq(int32_t handle, float f);
float sound_get_freq(int32_t handle);
bool sound_playing(int32_t handle);

void music_play(int track);  // Media_AudioPlay: 0 = random, n = CD track n+1
void music_stop(void);

#endif
