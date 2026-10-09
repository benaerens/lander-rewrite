// Combat: projectiles (type 2), turrets (0, 14), destructibles (8), explosions (9), damage and
// destruction, the lander's targeting. Exports Explosion_CreateDynamic (mission_api.h).
#ifndef WEAPONS_H
#define WEAPONS_H

#include "objtypes.h"

void weapons_register_types(void);
void weapons_reset(void);                                  // per mission
void weapons_fire_projectile(const LanderShotParams *sp);  // a lander shot (Objects_Create(2))
void Object_Destroyed(ObjId id, int32_t explosion_size);   // 0x459190: hit points reached 0

#endif
