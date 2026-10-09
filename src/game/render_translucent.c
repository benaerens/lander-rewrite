// The PCP renderer's translucent triangles (flag 0x8000) in the scene's translucency table: the
// original sends them to the same 4096-bucket sort table as the effects (Translucent_AddPoly 0x4416e0
// for v1.0 models, FUN_00441900 for v1.2 ones from PcpRender_EmitTriV12 0x470680), flushed back to
// front after the opaque scene (OT+0x20; the reflection pass's own table at the head of OT+0x18).
// Drawn by FUN_00441500 / 0x4415f0 with blend mode 1 (SRCALPHA / INVSRCALPHA, alpha = the texture's)
// or 2 (ONE/ONE), the scene's fog colour (only the effects primitives set it to black for mode 2) and
// z writes still on (SortTable_Flush begins with D3D_Begin3DStates).
#include "render_translucent.h"
#include "effects_draw.h"

static void sink(const vertex_t v[3], const texture_t *tex, bool additive, float sort_z) {
	fxv_t f[3];
	for (int k = 0; k < 3; k++) {
		const rgba_t c = v[k].color;
		f[k] = (fxv_t){v[k].pos, v[k].u, v[k].v,
			(uint32_t)c.a << 24 | (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b};
	}
	fx_queue(f, 3, tex, additive ? FX_PCP_ADD : FX_PCP_ALPHA, true, sort_z);
}

void render_translucent_begin(void) { model_translucent_sink = sink; }
void render_translucent_end(void) { model_translucent_sink = NULL; }
