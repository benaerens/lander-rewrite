#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model.h"
#include "fs.h"

#define PAGE 2048
#define PPM_MAGIC 0xD065C0CC

static float get_f32(const uint8_t *p) {
	uint32_t u = get_u32(p);
	float f;
	memcpy(&f, &u, 4);
	return f;
}

static vec3_t get_vec3(const uint8_t *p) {
	return vec3(get_f32(p), get_f32(p + 4), get_f32(p + 8));
}

// ---------------------------------------------------------------- PCP

model_t *model_from_pcp(const uint8_t *d, uint32_t size) {
	if (size < 0x64 || memcmp(d, "PCP*", 4)) {
		return NULL;
	}
	model_t *m = calloc(1, sizeof(model_t));
	m->version = ((uint32_t)get_u16(d + 6) << 16) | get_u16(d + 4);
	m->flags = get_u32(d + 0x5c);
	m->blob = malloc(size);
	memcpy(m->blob, d, size);
	m->blob_size = size;

	uint32_t off = get_u32(d + 0x08);
	m->num_vertices = get_u32(d + 0x0c);
	m->vertices = calloc(m->num_vertices + 1, sizeof(vec3_t));
	for (int i = 0; i < m->num_vertices; i++) {
		m->vertices[i] = get_vec3(d + off + i * 12);
	}

	off = get_u32(d + 0x18);
	m->num_normals = get_u32(d + 0x1c);
	m->normals = calloc(m->num_normals + 1, sizeof(vec3_t));
	for (int i = 0; i < m->num_normals; i++) {
		m->normals[i] = get_vec3(d + off + i * 12);
	}

	off = get_u32(d + 0x10);
	m->num_wedges = get_u32(d + 0x14);
	m->wedges = calloc(m->num_wedges + 1, sizeof(pcp_wedge_t));
	for (int i = 0; i < m->num_wedges; i++) {
		const uint8_t *w = d + off + i * 16;
		m->wedges[i] = (pcp_wedge_t){get_i16(w), get_i16(w + 2), get_i32(w + 4), get_f32(w + 8), get_f32(w + 12)};
	}

	off = get_u32(d + 0x20);
	m->num_tris = get_u32(d + 0x24);
	m->tris = calloc(m->num_tris + 1, sizeof(pcp_tri_t));
	for (int i = 0; i < m->num_tris; i++) {
		const uint8_t *t = d + off + i * 10;
		m->tris[i] = (pcp_tri_t){get_i16(t), get_u16(t + 2), {get_u16(t + 4), get_u16(t + 6), get_u16(t + 8)}};
	}

	off = get_u32(d + 0x28);
	m->num_colours = off ? get_u32(d + 0x2c) : 0;
	m->colours = calloc(m->num_colours + 1, sizeof(uint32_t));
	for (int i = 0; i < m->num_colours; i++) {
		m->colours[i] = get_u32(d + off + i * 4);
	}

	off = get_u32(d + 0x40);
	m->num_litverts = off ? get_i16(d + 0x52) : 0;
	m->litverts = calloc(m->num_litverts + 1, sizeof(pcp_litvert_t));
	for (int i = 0; i < m->num_litverts; i++) {
		const uint8_t *l = d + off + i * 12;
		m->litverts[i] = (pcp_litvert_t){get_i16(l), get_i16(l + 2), get_i32(l + 4)};
	}

	off = get_u32(d + 0x44);
	m->bbox_min = get_vec3(d + off);
	m->bbox_max = get_vec3(d + off + 0x30);
	for (int i = 0; i < 8; i++) {
		float r = vec3_len(get_vec3(d + off + i * 12));
		m->radius = max(m->radius, r);
	}

	off = get_u32(d + 0x48);
	m->num_textures = get_i32(d + off);
	m->textures = calloc(m->num_textures + 1, sizeof(int16_t));
	for (int i = 0; i < m->num_textures; i++) {
		m->textures[i] = get_i16(d + off + 4 + 2 * i);
	}

	off = get_u32(d + 0x54);
	if (off) {
		m->has_envmap = true;
		m->envmap_tex = get_i16(d + off);
		m->envmap_count = get_i32(d + off + 4);
		m->envmap_first = get_i32(d + off + 8) / 10;
	}
	return m;
}

void model_free(model_t *m) {
	if (!m) {
		return;
	}
	free(m->vertices);
	free(m->normals);
	free(m->wedges);
	free(m->tris);
	free(m->colours);
	free(m->litverts);
	free(m->textures);
	free(m->blob);
	free(m);
}

const uint8_t *model_tagged_block(const model_t *m, uint32_t tag, uint32_t *size_out) {
	int n = get_u32(m->blob + 0x60);
	for (int i = 0; i < n && i < 32; i++) {
		if (get_u32(m->blob + 0x64 + i * 8) == tag) {
			uint32_t off = get_u32(m->blob + 0x68 + i * 8);
			if (size_out) {
				*size_out = m->blob_size - off;
			}
			return off < m->blob_size ? m->blob + off : NULL;
		}
	}
	return NULL;
}

model_t *model_load_pcp(const char *path) {
	uint32_t size;
	uint8_t *d = fs_load(path, &size);
	if (!d) {
		return NULL;
	}
	model_t *m = model_from_pcp(d, size);
	free(d);
	return m;
}

// ---------------------------------------------------------------- PPM

ppm_t *ppm_load(const char *path) {
	uint32_t size;
	uint8_t *d = fs_load(path, &size);
	if (!d || size < 16 || get_u32(d) != PPM_MAGIC) {
		free(d);
		return NULL;
	}
	ppm_t *p = calloc(1, sizeof(ppm_t));
	p->count = get_u32(d + 4);
	uint32_t table = get_u32(d + 8);
	p->entries = calloc(p->count + 1, sizeof(ppm_entry_t));
	for (int i = 0; i < p->count; i++) {
		uint16_t start = get_u16(d + table + 4 * i);
		uint16_t pages = get_u16(d + table + 4 * i + 2);
		ppm_entry_t *e = &p->entries[i];
		if (!pages) {
			continue;
		}
		// bit 15: a group record in the header: {u16 n, u16 start_page, u16 dword_offset[n]}
		int n = 1;
		uint32_t base, offsets[64] = {0};
		if (start & 0x8000) {
			const uint8_t *g = d + (start & 0x7fff);
			n = min(get_u16(g), 64);
			base = get_u16(g + 2) * PAGE;
			for (int k = 0; k < n; k++) {
				offsets[k] = get_u16(g + 4 + 2 * k) * 4;
			}
		} else {
			base = start * PAGE;
		}
		uint32_t end = (uint32_t)((start & 0x8000 ? get_u16(d + (start & 0x7fff) + 2) : start) + pages) * PAGE;
		end = min(end, size);
		e->num_parts = n;
		e->parts = calloc(n, sizeof(model_t *));
		for (int k = 0; k < n; k++) {
			uint32_t a = base + offsets[k];
			uint32_t b = k + 1 < n ? base + offsets[k + 1] : end;
			if (a < b && b <= size) {
				e->parts[k] = model_from_pcp(d + a, b - a);
			}
		}
	}
	free(d);
	return p;
}

void ppm_free(ppm_t *p) {
	if (!p) {
		return;
	}
	for (int i = 0; i < p->count; i++) {
		for (int k = 0; k < p->entries[i].num_parts; k++) {
			model_free(p->entries[i].parts[k]);
		}
		free(p->entries[i].parts);
	}
	free(p->entries);
	free(p);
}

// ---------------------------------------------------------------- drawing

#define MAX_DRAW_VERTS (3 * 4096)

// Material colours go through the brightness/gamma table at load in the
// original (0x43c120): table[i] = 255 * (i/255)^(2.3 - 2*brightness/255).
static uint8_t gamma_table[256];
static bool gamma_ready;

void model_set_brightness(int brightness) {
	double g = 2.3 - 2.0 * brightness / 255.0;
	for (int i = 0; i < 256; i++) {
		gamma_table[i] = (uint8_t)(pow(i / 255.0, g) * 255.0);
	}
	gamma_ready = true;
}

static vec3_t material_rgb(uint32_t c) {
	if (!gamma_ready) {
		// Game_Main sets 200 at startup (decomp line 13242); the Visual options
		// slider ("Brightness" registry value >> 4) only changes it from the menu.
		model_set_brightness(200);
	}
	return vec3(gamma_table[(c >> 16) & 0xff] / 255.0f, gamma_table[(c >> 8) & 0xff] / 255.0f,
		gamma_table[c & 0xff] / 255.0f);
}

static vec3_t lit_colour(const model_t *m, vec3_t pos, vec3_t n, uint32_t material, const lighting_t *L) {
	vec3_t mat = material_rgb(material);
	vec3_t sum = L->ambient;
	for (int i = 0; i < L->num_dir; i++) {
		float d = -vec3_dot(n, L->dir[i]);
		if (d > 0) {
			sum = vec3_add(sum, vec3_mulf(L->dir_color[i], d));
		}
	}
	for (int i = 0; i < L->num_point; i++) {
		vec3_t to = vec3_sub(L->point_pos[i], pos);
		float dist = vec3_len(to);
		float range = L->point_inner[i] + L->point_falloff[i];
		if (dist >= range || dist <= 0) {
			continue;
		}
		float att = dist <= L->point_inner[i] ? 1 : 1 - (dist - L->point_inner[i]) / L->point_falloff[i];
		float d = vec3_dot(n, to) / dist;
		if (d > 0) {
			sum = vec3_add(sum, vec3_mulf(L->point_color[i], d * att));
		}
	}
	for (int i = 0; i < L->num_spot; i++) {
		vec3_t to = vec3_sub(L->spot[i].pos, pos);
		float dist = vec3_len(to);
		float range = L->spot[i].inner + L->spot[i].falloff;
		if (dist >= range || dist <= 0) {
			continue;
		}
		to = vec3_mulf(to, 1 / dist);
		float c = -vec3_dot(L->spot[i].dir, to); // cosine of the angle from the cone axis
		float d = vec3_dot(n, to);
		if (c <= L->spot[i].cos_outer || d <= 0) {
			continue;
		}
		float f = d;
		if (c < L->spot[i].cos_inner) {
			f *= (c - L->spot[i].cos_outer) / (L->spot[i].cos_inner - L->spot[i].cos_outer);
		}
		if (dist > L->spot[i].inner) {
			f *= 1 - (dist - L->spot[i].inner) / L->spot[i].falloff;
		}
		sum = vec3_add(sum, vec3_mulf(L->spot[i].color, f));
	}
	for (int i = 0; i < L->num_box; i++) {
		const float *e = L->box[i].ext;
		vec3_t d = vec3_sub(pos, L->box[i].pos);
		float b = vec3_dot(L->box[i].axis[1], d);
		float nl = vec3_dot(L->box[i].axis[1], n);
		float in, fall;
		if (b >= 0) {
			in = e[2];
			fall = e[3];
			nl = -nl;
		} else {
			in = e[6];
			fall = e[7];
			b = -b;
		}
		nl = min(nl + 0.25f, 1.0f); // wrap-around lighting
		float a = fabsf(vec3_dot(L->box[i].axis[0], d)), c = fabsf(vec3_dot(L->box[i].axis[2], d));
		if (nl <= 0 || a >= e[0] + e[1] || b >= in + fall || c >= e[4] + e[5]) {
			continue;
		}
		if (a > e[0]) nl *= 1 - (a - e[0]) / e[1];
		if (b > in) nl *= 1 - (b - in) / fall;
		if (c > e[4]) nl *= 1 - (c - e[4]) / e[5];
		sum = vec3_add(sum, vec3_mulf(L->box[i].color, nl));
	}
	return vec3(clamp(sum.x * mat.x, 0.0f, 1.0f), clamp(sum.y * mat.y, 0.0f, 1.0f), clamp(sum.z * mat.z, 0.0f, 1.0f));
}

// The original modulates texture x diffuse once (D3DTOP_MODULATE, 0x43c... stage
// setup at decomp line ~37816); kept as a knob for comparisons.
float model_light_gain = 1.0f;

// ---------------------------------------------------------------- texture classes
//
// How a texture becomes a D3D surface decides how the PCP renderer draws it (TexD3D_CreateSingle
// 0x438c00, FUN_00438e40 for animated ones, Pcp_BindTextures 0x46dc30):
//  * RGB (format 4): an opaque surface with the source colour key black (SetColorKey whenever
//    DAT_004984a0, which is always 1) and D3DRENDERSTATE_COLORKEYENABLE on (D3D_InitMatricesAndStates
//    0x43bc40): texels that are exactly 0,0,0 are not drawn (the 16-bit conversion 0x43aa40 turns any
//    other colour that would round to 0 into 1). Frames, grilles and lattices rely on it.
//  * RGBA (format 0), one frame, every texel away from the border with alpha <= 0x10 or >= 0xf0: the
//    alpha is dropped and the texture made opaque as above (comment of 0x438c00: binary alpha).
//  * any other RGBA texture, and every animated RGBA one: an alpha surface (record flags 3): blend
//    mode 1 = SRCALPHA / INVSRCALPHA with the texture's alpha, drawn with the opaque batches with z
//    writes on, no alpha test (the Venus water).
// The rewrite gives keyed textures alpha 0 on the black texels and 255 elsewhere (in place: the D3D
// surface is the same for every user of the texture) and draws them with the alpha test.

enum { TEX_KEYED, TEX_ALPHA };

#define MAX_PREPARED 16
static struct {
	const texfile_t *tf;
	const tex_entry_t *entries; // to notice a texfile freed and another allocated at the same place
	int count;
	uint8_t *cls;
} prepared[MAX_PREPARED];
static int next_prepared;

static int classify(tex_entry_t *e) {
	const texture_t *t = &e->frame[0];
	if (e->flags == 0x0400 || !t->pixels) return TEX_KEYED;
	if (e->frames > 1) return TEX_ALPHA;
	for (int y = 1; y < t->height - 1; y++) {
		for (int x = 1; x < t->width - 1; x++) {
			uint32_t a = t->pixels[y * t->width + x] >> 24;
			if (a < 0xf0 && a > 0x10) return TEX_ALPHA;
		}
	}
	return TEX_KEYED;
}

static const uint8_t *texfile_classes(texfile_t *tf) {
	if (!tf) return NULL;
	for (int i = 0; i < MAX_PREPARED; i++) {
		if (prepared[i].tf == tf && prepared[i].entries == tf->entries && prepared[i].count == tf->count) {
			return prepared[i].cls;
		}
	}
	return model_prepare_texfile(tf);
}

const uint8_t *model_prepare_texfile(texfile_t *tf) {
	if (!tf) return NULL;
	model_forget_texfile(tf);
	int slot = next_prepared;
	next_prepared = (next_prepared + 1) % MAX_PREPARED;
	free(prepared[slot].cls);
	prepared[slot].tf = tf;
	prepared[slot].entries = tf->entries;
	prepared[slot].count = tf->count;
	prepared[slot].cls = malloc(tf->count + 1);
	for (int i = 0; i < tf->count; i++) {
		tex_entry_t *e = &tf->entries[i];
		int c = classify(e);
		prepared[slot].cls[i] = (uint8_t)c;
		// TexD3D_CreateOpaque / CreateAlpha 0x43a2d0 / 0x43a580: square single surfaces get a mipmap
		// chain when "Mip Mapping" is on (the default); animated ones (FUN_00438e40) never do
		if (e->frame && e->frames == 1) e->frame[0].mip_ok = e->frame[0].width == e->frame[0].height;
		if (c != TEX_KEYED || !e->frame) continue;
		for (int f = 0; f < e->frames; f++) { // the colour key on every frame
			texture_t *t = &e->frame[f];
			for (int p = 0; t->pixels && p < t->width * t->height; p++) {
				uint32_t px = t->pixels[p] & 0xffffff;
				t->pixels[p] = px ? px | 0xff000000u : 0;
			}
		}
	}
	return prepared[slot].cls;
}

void model_forget_texfile(const texfile_t *tf) {
	for (int i = 0; tf && i < tf->count; i++) {
		for (int f = 0; tf->entries[i].frame && f < tf->entries[i].frames; f++) {
			render_texture_free_mips(&tf->entries[i].frame[f]);
		}
	}
	for (int i = 0; i < MAX_PREPARED; i++) {
		if (prepared[i].tf == tf) {
			free(prepared[i].cls);
			prepared[i].tf = NULL;
			prepared[i].entries = NULL;
			prepared[i].cls = NULL;
		}
	}
}

void (*model_translucent_sink)(const vertex_t v[3], const texture_t *tex, bool additive, float sort_z);

void model_draw(const model_t *m, mat4_t transform, texfile_t *tf, const lighting_t *L) {
	if (!m || !m->num_tris) {
		return;
	}
	// world-space normal transform (rotation part; the scale is ~1)
	mat4_t rot = transform;
	rot.m[12] = rot.m[13] = rot.m[14] = 0;

	// per lit vertex (v1.2) or per wedge (v1.0) colours
	int ncol = m->num_litverts ? m->num_litverts : m->num_wedges;
	rgba_t *col = malloc(sizeof(rgba_t) * (ncol + 1));
	for (int i = 0; i < ncol; i++) {
		int vi, ni;
		uint32_t material;
		if (m->num_litverts) {
			const pcp_litvert_t *lv = &m->litverts[i];
			vi = lv->vertex;
			ni = lv->normal;
			material = lv->colour >= 0 && lv->colour < m->num_colours ? m->colours[lv->colour] : 0xff7f7f7f;
		} else {
			const pcp_wedge_t *w = &m->wedges[i];
			vi = w->vertex;
			ni = w->normal;
			material = m->num_colours ? (w->lit >= 0 && w->lit < m->num_colours ? m->colours[w->lit] : 0xff7f7f7f)
				: (uint32_t)w->lit;
		}
		vec3_t c;
		if (ni < 0 || ni >= m->num_normals || !L) {
			c = material_rgb(material);
		} else {
			vec3_t wp = mat4_mul_point(transform, m->vertices[vi < m->num_vertices ? vi : 0]);
			vec3_t wn = mat4_mul_point(rot, m->normals[ni]);
			c = lit_colour(m, wp, wn, material, L);
		}
		float g = model_light_gain * 255;
		col[i] = rgba(min(255, (int)(c.x * g)), min(255, (int)(c.y * g)), min(255, (int)(c.z * g)), 255);
	}

	render_set_model(transform);
	const uint8_t *cls = texfile_classes(tf);
	mat4_t view = render_view();
	// FUN_0046e3c0: version <= 1.1 takes the v1.0 paths
	bool v12 = !((m->version & 0xffff0000u) <= 0x10000u && (m->version & 0xffff) < 2);
	// the environment map (Model_Draw packet +0x7c: model position - the camera node's, the real
	// camera even in the reflection pass, whose view is mirrored about y = 0)
	vec3_t env_rel = vec3(0, 0, 0);
	if (m->has_envmap) {
		const float *vm = view.m;
		vec3_t eye = vec3(-(vm[0] * vm[12] + vm[1] * vm[13] + vm[2] * vm[14]), -(vm[4] * vm[12] + vm[5] * vm[13] + vm[6] * vm[14]),
			-(vm[8] * vm[12] + vm[9] * vm[13] + vm[10] * vm[14]));
		if (render_view_mirrored()) eye.y = -eye.y;
		env_rel = vec3_sub(vec3(transform.m[12], transform.m[13], transform.m[14]), eye);
	}
	static vertex_t buf[MAX_DRAW_VERTS];
	// the opaque batches per texture (FUN_0046eb40 / FUN_0046f1c0: blend 0, or 1 for alpha textures);
	// translucent triangles (flag 0x8000) go to the translucency table (Translucent_AddPoly 0x4416e0)
	for (int ti = -1; ti < m->num_textures; ti++) {
		int16_t tex = ti < 0 ? -1 : m->textures[ti];
		int nv = 0;
		const texture_t *t = NULL;
		if (tex >= 0 && tf && tex < tf->count) {
			t = &tf->entries[tex].frame[0];
		}
		bool alpha_tex = t && cls && cls[tex] == TEX_ALPHA;
		// Pcp_BindTextures: the environment texture is blend mode 2 (ONE/ONE)
		bool env = m->has_envmap && tex == m->envmap_tex;
		blend_t blend = !t ? BLEND_OPAQUE : env ? BLEND_ONE_ONE : alpha_tex ? BLEND_ALPHA : BLEND_ALPHA_TEST;
		for (int i = 0; i < m->num_tris; i++) {
			const pcp_tri_t *tr = &m->tris[i];
			if (tr->tex != tex || (tr->flags & TRI_INVISIBLE)) {
				continue;
			}
			bool translucent = (tr->flags & TRI_TRANSLUCENT) != 0;
			vertex_t *v = translucent ? buf + MAX_DRAW_VERTS - 3 : &buf[nv];
			for (int k = 0; k < 3; k++) {
				const pcp_wedge_t *w = &m->wedges[tr->w[k] < m->num_wedges ? tr->w[k] : 0];
				v[k].pos = m->vertices[w->vertex >= 0 && w->vertex < m->num_vertices ? w->vertex : 0];
				v[k].u = t ? (w->u + 0.5f) / t->width : 0;
				v[k].v = t ? (w->v + 0.5f) / t->height : 0;
				if (env && w->normal >= 0 && w->normal < m->num_normals) {
					// PcpRender_EnvMapTriUVs 0x471e00 / PcpRender_SphereMapUV 0x471eb0: the view vector
					// e = vertex - eye reflected about the normal, both in world axes
					vec3_t p = mat4_mul_point(rot, v[k].pos), n = mat4_mul_point(rot, m->normals[w->normal]);
					vec3_t e = vec3_add(p, env_rel);
					float s = -2.0f * vec3_dot(n, e);
					vec3_t r = vec3_add(e, vec3_mulf(n, s));
					float sc = 0.5f / sqrtf(vec3_dot(r, r));
					v[k].u = min(0.5f - r.x * sc, 1.0f);
					v[k].v = min(r.y * sc + 0.5f, 1.0f);
				}
				int ci = m->num_litverts ? w->lit : tr->w[k];
				v[k].color = ci >= 0 && ci < ncol ? col[ci] : rgba(255, 255, 255, 255);
			}
			if (translucent) {
				// FUN_004415f0: blend mode 1 (flag 0x4000 clear) takes the texture's alpha
				// (D3DTSS_ALPHAOP SELECTARG1 TEXTURE); the 0x60 written into the vertices is what an
				// untextured one keeps. Mode 2 is ONE/ONE.
				bool additive = (tr->flags & TRI_ADDITIVE) != 0;
				for (int k = 0; k < 3; k++) v[k].color.a = t ? 0xff : 0x60;
				if (model_translucent_sink) {
					vertex_t wv[3];
					float z[3];
					for (int k = 0; k < 3; k++) {
						wv[k] = v[k];
						wv[k].pos = mat4_mul_point(transform, v[k].pos);
						z[k] = mat4_mul_point(view, wv[k].pos).z; // camera depth
					}
					// the sort key: v1.2 models (PcpRender_EmitTriV12 -> FUN_00441900) the sum of the
					// three depths, with an environment map (PcpRender_EmitTriV12Env) 90 / the sum of
					// the rhw = 10/z; older ones (Translucent_AddPoly) 3 * 10 / that sum
					float key;
					if (v12 && !m->has_envmap) {
						key = z[0] + z[1] + z[2];
					} else {
						float s = 0;
						for (int k = 0; k < 3; k++) s += z[k] > 0 ? 10.0f / z[k] : 0;
						key = s > 0 ? (v12 ? 90.0f : 30.0f) / s : 0;
					}
					model_translucent_sink(wv, t, additive, key);
				} else {
					render_tris(v, 3, t, additive ? BLEND_ONE_ONE : BLEND_ALPHA);
				}
				continue;
			}
			nv += 3;
			if (nv + 6 > MAX_DRAW_VERTS) {
				render_tris(buf, nv, t, blend);
				nv = 0;
			}
		}
		if (nv) {
			render_tris(buf, nv, t, blend);
		}
	}
	free(col);
}
