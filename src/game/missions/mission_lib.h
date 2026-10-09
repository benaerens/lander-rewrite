// Code that the original mission DLLs share because it was compiled into each of them from common
// source files. Each DLL has its own copy at its own addresses (listed per function); only one
// mission runs at a time, so the rewrite keeps a single copy.
//
//   * score library - every campaign MISSIONR.DLL (not training_1/2, not the deathmatches; the
//     rating-word table is also present in deathmat_1 without the code):
//     tracks shots / hits / damage / collisions / pickups / time and writes gGame_ScoreData before
//     calling LevelEnd. Entry points MissionScore_Init / MissionScore_End.
//   * AnimSeq       - animation sequencer (16 slots, raises the DLL-private event 0x20 when a
//     sequence ends): training_2, earth_4, europa_1, io_2, mimas_1, rhea_1, titan_3.
//   * small helpers that most DLLs carry (explosion sound by size, centre a rect).
//
// IMPORTANT: the engine reloads the DLL for every mission start (MissionDll_Load 0x47b570 frees
// the previous one), so DLL globals always start from their initial image. The C ports emulate
// that by calling the *_Reset functions from their LevelLoadLevel.
#ifndef LANDER_MISSION_LIB_H
#define LANDER_MISSION_LIB_H

#include "../mission_api.h"

// ---- score library (mercury_1 0x10001070-0x10001614, mars_1a 0x10001050-0x100015f4) ----------

void MissionScore_Reset(void);                                              // port only: fresh DLL data
void MissionScore_Init(ObjId lander, int32_t parTimeMs, int32_t pickupsTotal); // mercury 0x10001540
void MissionScore_End(int32_t result);                                      // mercury 0x100013c0 -> LevelEnd

// ---- AnimSeq (training_2 0x10001000-0x100011c1) -----------------------------------------------
// A sequence is a byte string of 3-byte steps {s8 anim, u8 repeat, u8 AnimPlay flags} ended by an
// anim of -1 (0xff). Scripts patch the repeat bytes at run time, so sequences are writable.
#define ANIMSEQ_SLOTS 16
void AnimSeq_Reset(void);                                         // port only: fresh DLL data
void AnimSeq_Init(void);                                          // training_2 0x10001190
void AnimSeq_Start(ObjId obj, uint8_t *seq, int32_t loop);        // training_2 0x10001120
void AnimSeq_Stop(ObjId obj);                                     // titan_3 / mimas_1 / earth_4 0x10001190
void AnimSeq_OnAnimFinished(const LanderEvent *ev);               // training_2 0x10001000 (EV_ANIM_FINISHED)

// ---- brief.dll mail tables: BriefMail (gametext.h), read from the DLL as GT_MAILS ----------------

// ---- common helpers ----------------------------------------------------------------------------
// EV_DESTROYED handler: explosion sound 110..114 by explosion size (mercury 0x100016c0,
// mars_1a 0x10002280; training_2 has its own variant).
void MissionLib_OnDestroyedSound(const LanderEvent *ev);
// Centre a rect on the screen (training_1 0x10001040, training_2 0x10001220).
void MissionLib_CenterRect(WinRect *r);

#endif // LANDER_MISSION_LIB_H
