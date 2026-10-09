// Physical and camera object types: Pod (12, towable, Pod_Script_*), ForceBox (16), InertialFrame
// (20, gInertialFrame), AnimCam (5) and FlyCam (7) (the intro / attract cameras).
// Spec: re/specs/physobjs.md.
#ifndef PHYSOBJS_H
#define PHYSOBJS_H

#include "objtypes.h"
#include "camera.h"

void physobjs_register_types(void);
void physobjs_reset(void);                          // per mission
phys_body_t *physobjs_object_body(uint32_t handle); // 0x413880: property 6 for the physics, NULL = none

// The query slot / time a class-2 object's property 2 answers for (DAT_0058b3c0 / DAT_0058b394):
// slot 0 = the physics step's start (or the game time outside the step), 1 = its end. Set by the
// collision code (objects.c) before it asks; the pod (and the lander, see eng_phys_query_slot)
// answer with their body interpolated to that time.
extern int32_t gPhysQuerySlot, gPhysQueryTime;

// gInertialFrameInterp (InertialFrame render 0x457f20): the frame at render time; its column 1 is
// the camera's up vector while a frame is in use.
extern FwMatrix gInertialFrameInterp;

// Pod script API not in mission_api.h (no retail DLL imports it)
void Pod_Script_SetupSpringConstant(ObjId pod, int32_t k);   // 0x44ce80

// objtypes.c: the animation pose of an object part at game time `time` for query slot 0/1
// (property 2 of the animated types during the physics step), NULL when it is not animating;
// AnimState_Apply(state, 0)
const FwMatrix *anim_pose_at(ObjId handle, int32_t time, int slot);
void animobj_apply(task_t *t);

#include "game.h"   // game_view, gPhysicsTask, game_load_mission_mode

#endif
