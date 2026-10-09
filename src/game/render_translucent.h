// The PCP translucent triangles in the scene's depth-sorted translucency table (render_translucent.c).
#ifndef RENDER_TRANSLUCENT_H
#define RENDER_TRANSLUCENT_H

#include "engine.h"

// between begin and end (the scene of a frame, up to the translucency flush) model_draw hands its
// translucent triangles to the effects' sorted list instead of drawing them at once
void render_translucent_begin(void);
void render_translucent_end(void);

#endif
