// The Venus reflection pass of Mission_Run (0x4146c0). See render_reflect.c.
#ifndef RENDER_REFLECT_H
#define RENDER_REFLECT_H

#include "engine.h"

// Per frame after level_begin_frame (portal visibility known), before the scene's render callbacks run:
// whether this frame has a reflection pass (Venus, a camera object, the outside visible, the camera at
// or above y = 0). Also sets gMirrorProbe (gMirrorPass 0x491be4) for Lander_Render's liquid probe.
bool render_reflect_wanted(const camera_t *cam);
// The pass itself, drawn immediately with the mirrored camera; the caller records the main scene
// before it and replays it after (the original's OT+0x10 runs before OT+0x18).
void render_reflect_draw(const camera_t *cam, uint32_t now);

#endif
