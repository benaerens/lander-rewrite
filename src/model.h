// PCP* models and the PPM archives that hold them. See re/specs/pcp.md.
#ifndef MODEL_H
#define MODEL_H

#include "types.h"
#include "render.h"
#include "texfile.h"

#define TRI_PARTNER_MASK 0x001f
#define TRI_ADDITIVE     0x4000
#define TRI_TRANSLUCENT  0x8000
#define TRI_INVISIBLE    0x2000

#define TAG_LITVERT_OCTREE 0x0C0CF00D
#define TAG_POLY_OCTREE    0x0CBEEEEE
#define TAG_POLY_PLANES    0x00B0FFED
#define TAG_DYNAMICS       0xFACCFACE

typedef struct {
	int16_t vertex, normal;
	int32_t lit;   // v1.2: lit-vertex index; v1.0: colour index or direct ARGB
	float u, v;    // texels
} pcp_wedge_t;

typedef struct {
	int16_t tex;   // texfile index (-1 = untextured)
	uint16_t flags;
	uint16_t w[3]; // wedges, clockwise front face
} pcp_tri_t;

typedef struct {
	int16_t normal, colour;
	int32_t vertex;
} pcp_litvert_t;

typedef struct {
	uint32_t version; // (major << 16) | minor
	uint32_t flags;
	int num_vertices, num_wedges, num_normals, num_tris, num_colours, num_litverts;
	vec3_t *vertices;
	vec3_t *normals;
	pcp_wedge_t *wedges;
	pcp_tri_t *tris;
	uint32_t *colours; // 0xAARRGGBB
	pcp_litvert_t *litverts;
	vec3_t bbox_min, bbox_max;
	float radius;
	int num_textures;
	int16_t *textures;
	bool has_envmap;
	int16_t envmap_tex;
	int envmap_first, envmap_count;
	// the original blob, for the tagged blocks (collision octrees, planes, dynamics)
	uint8_t *blob;
	uint32_t blob_size;
} model_t;

model_t *model_from_pcp(const uint8_t *data, uint32_t size);
void model_free(model_t *m);
const uint8_t *model_tagged_block(const model_t *m, uint32_t tag, uint32_t *size_out);

// A PPM archive: entries of one or more PCP parts.
typedef struct {
	int num_parts;
	model_t **parts;
} ppm_entry_t;

typedef struct {
	int count;
	ppm_entry_t *entries;
} ppm_t;

ppm_t *ppm_load(const char *path);
void ppm_free(ppm_t *p);
model_t *model_load_pcp(const char *path);

// Lighting environment for drawing models (world space). Formulas follow
// PcpLight_LitVertColour (0x472990): colour = (sum + ambient) * material + baked.
#define MAX_POINT_LIGHTS 32
#define MAX_SPOT_LIGHTS 8
#define MAX_BOX_LIGHTS 8

typedef struct {
	vec3_t ambient;          // 0..1
	int num_dir;
	vec3_t dir[4];           // world-space direction the light travels
	vec3_t dir_color[4];
	int num_point;
	vec3_t point_pos[MAX_POINT_LIGHTS];
	vec3_t point_color[MAX_POINT_LIGHTS];
	float point_inner[MAX_POINT_LIGHTS], point_falloff[MAX_POINT_LIGHTS];
	int num_spot;
	struct {
		vec3_t pos, dir, color;  // dir = the light's local z axis
		float inner, falloff, cos_inner, cos_outer;
	} spot[MAX_SPOT_LIGHTS];
	int num_box;
	struct {
		vec3_t pos, axis[3], color; // axes = the light's local x, y, z
		float ext[8]; // x inner/fall, +y inner/fall, z inner/fall, -y inner/fall
	} box[MAX_BOX_LIGHTS];
} lighting_t;

// Draws a model with the current render view; transform = model -> world.
void model_draw(const model_t *m, mat4_t transform, texfile_t *tf, const lighting_t *light);
void model_set_brightness(int brightness); // 0..255, default 128

// The texture classes of the original's D3D surfaces (see model.c): every texfile drawn by model_draw is
// prepared once (texels of colour-keyed textures get alpha 0 / 255 in place). level_load prepares the
// level's texfiles; a texfile must be forgotten before it is freed.
const uint8_t *model_prepare_texfile(texfile_t *tf);
void model_forget_texfile(const texfile_t *tf);

// When set (by the game, while a scene is drawn): the translucent triangles (PCP flag 0x8000) go here
// in world space, for the depth-sorted translucency table, instead of being drawn at once.
// v[].color.a = the alpha to use (0xff = the texture's), additive = ONE/ONE, sort_z = the original's
// sort value (camera depth units; table bucket = sort_z / 8).
extern void (*model_translucent_sink)(const vertex_t v[3], const texture_t *tex, bool additive, float sort_z);

#endif
