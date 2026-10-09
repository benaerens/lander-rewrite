#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mission.h"
#include "fs.h"

static int32_t sext(uint32_t v, int bits) {
	return (int32_t)(v << (32 - bits)) >> (32 - bits);
}

void rot_unpack(uint32_t packed, int *ax, int *ay, int *az) {
	*ax = sext(packed >> 21, 11) * 2;
	*ay = sext(packed >> 10, 11) * 2;
	*az = sext(packed, 10) * 4;
}

mat4_t rot_matrix(uint32_t packed) {
	int ax, ay, az;
	rot_unpack(packed, &ax, &ay, &az);
	float x = ax * (2 * PI / 4096), y = ay * (2 * PI / 4096), z = az * (2 * PI / 4096);
	float sx = sinf(x), cx = cosf(x), sy = sinf(y), cy = cosf(y), sz = sinf(z), cz = cosf(z);
	mat4_t m = mat4_identity();
	// row r, column c at m[c * 4 + r]
	m.m[0] = cz * cy;               m.m[4] = sx * sy * cz - cx * sz; m.m[8] = cx * sy * cz + sx * sz;
	m.m[1] = cy * sz;               m.m[5] = sx * sy * sz + cx * cz; m.m[9] = cx * sy * sz - sx * cz;
	m.m[2] = -sy;                   m.m[6] = cy * sx;                m.m[10] = cy * cx;
	return m;
}

vec3_t packed_vec(uint32_t packed) {
	return vec3(sext(packed >> 21, 11) * 4.0f, sext(packed >> 10, 11) * 4.0f, sext(packed, 10) * 4.0f);
}

int32_t mission_object_id(int table, int type, int index) {
	return ((uint32_t)table << 24) | ((uint32_t)type << 16) | (index & 0xffff);
}

static void read_rec(const uint8_t *p, map_rec_t *r, bool typed) {
	r->pos = (ivec3_t){get_i32(p), get_i32(p + 4), get_i32(p + 8)};
	r->rot = get_u32(p + 12);
	r->zone = get_i16(p + 16);
	r->model = get_i16(p + 18);
	if (typed) {
		r->type = p[20];
		r->param = get_i16(p + 22);
	} else {
		r->type = 0xff;
		r->param = -1;
	}
}

static void load_section_a(mission_t *m, const uint8_t *a, uint32_t avail, const int *model_parts) {
	m->num_rec[REC_STATIC] = get_u32(a + 4);
	m->num_rec[REC_OBJECT] = get_u32(a + 8);
	m->num_rec[REC_MOB] = get_u32(a + 12);
	static const int rec_size[3] = {0x14, 0x1c, 0x24};
	for (int t = 0; t < 3; t++) {
		uint32_t off = get_u32(a + 0x10 + 4 * t);
		m->rec[t] = calloc(m->num_rec[t] + 1, sizeof(map_rec_t));
		for (int i = 0; i < m->num_rec[t]; i++) {
			read_rec(a + off + i * rec_size[t], &m->rec[t][i], t != REC_STATIC);
		}
	}
	// the parameter pool runs up to the runtime list area (tables are packed in slot order)
	uint32_t pool_off = get_u32(a + 0x1c), next = get_u32(a + 0x20);
	m->num_pool = next > pool_off ? (next - pool_off) / 4 : 0;
	m->pool = calloc(m->num_pool + 1, sizeof(int32_t));
	for (int i = 0; i < m->num_pool; i++) {
		m->pool[i] = get_i32(a + pool_off + 4 * i);
	}

	uint32_t part_index = get_u32(a + 0x28), parts = get_u32(a + 0x2c);
	uint32_t proj = get_u32(a + 0x30), destroy = get_u32(a + 0x40), bbox = get_u32(a + 0x44);
	m->projectile_model[0] = get_i32(a + proj);
	m->projectile_model[1] = get_i32(a + proj + 4);
	m->models = calloc(m->num_models + 1, sizeof(model_info_t));
	m->destroy_step = calloc(m->num_models + 1, sizeof(int16_t));
	m->bbox = calloc(m->num_models + 1, sizeof(*m->bbox));
	bool has_bbox = bbox != 0x44;
	for (int i = 0; i < m->num_models; i++) {
		int n = model_parts ? model_parts[i] : 1;
		model_info_t *mi = &m->models[i];
		mi->num_parts = n < 1 ? 1 : n;
		mi->parts = calloc(mi->num_parts, sizeof(model_part_t));
		const uint8_t *e = a + parts + get_u16(a + part_index + 2 * i) * 4;
		for (int k = 0; k < mi->num_parts; k++) {
			const uint8_t *p = n > 1 ? e + k * 0x18 : e;
			model_part_t *mp = &mi->parts[k];
			mp->radius = get_u16(p);
			mp->aux = get_u16(p + 2);
			mp->center = packed_vec(get_u32(p + 4));
			if (n > 1) {
				mp->pos = (ivec3_t){get_i32(p + 8), get_i32(p + 12), get_i32(p + 16)};
				mp->rot = get_u32(p + 20);
			}
		}
		m->destroy_step[i] = get_i16(a + destroy + 2 * i);
		for (int k = 0; k < 6; k++) {
			m->bbox[i][k] = has_bbox ? get_i16(a + bbox + 12 * i + 2 * k) : (k < 3 ? -128 : 128);
		}
	}
	(void)avail;
}

static void load_section_b(mission_t *m, const uint8_t *b) {
	if (get_u32(b) != 0xC0C500CA) {
		return;
	}
	m->num_anims = get_u32(b + 4);
	m->anims = calloc(m->num_anims + 1, sizeof(anim_t));
	for (int i = 0; i < m->num_anims; i++) {
		const uint8_t *h = b + 8 + i * 8;
		anim_t *an = &m->anims[i];
		an->num_frames = get_u16(h);
		an->num_parts = get_u16(h + 2);
		int enc = h[4], stride = h[5];
		const uint8_t *data = h + get_u16(h + 6);
		an->keys = calloc((size_t)an->num_frames * an->num_parts + 1, sizeof(anim_key_t));
		for (int f = 0; f < an->num_frames; f++) {
			for (int p = 0; p < an->num_parts; p++) {
				anim_key_t *k = &an->keys[f * an->num_parts + p];
				const uint8_t *fp = data + f * stride;
				switch (enc) {
					case 0: k->y = (int8_t)fp[p]; break;
					case 1: k->y = get_i16(fp + 2 * p); break;
					case 2: k->y = get_i32(fp + 4 * p); break;
					case 3: k->rot = get_u32(fp + 4 * p); break;
					default: {
						uint32_t w0 = get_u32(fp + 12 * p), w1 = get_u32(fp + 12 * p + 4);
						k->x = sext(w0, 21);
						k->y = sext(w1, 21);
						k->z = (sext(w1 >> 21, 11) << 11) | (int32_t)(w0 >> 21);
						k->rot = get_u32(fp + 12 * p + 8);
					}
				}
			}
		}
	}
}

static void load_section_e(mission_t *m, const uint8_t *e) {
	if (get_u32(e) != 0xD065D1C5) {
		return;
	}
	for (int i = 0; i < 4; i++) {
		m->num_lights[i] = get_u32(e + 4 + 4 * i);
	}
	const uint8_t *p = e + get_u32(e + 0x14);
	m->dir_lights = calloc(m->num_lights[0] + 1, sizeof(light_dir_t));
	for (int i = 0; i < m->num_lights[0]; i++, p += 12) {
		uint32_t d = get_u32(p);
		// direction = -(x*4, y*4, z*8) in the packedCentre layout; length 4096
		vec3_t v = vec3(sext(d >> 21, 11) * 4.0f, sext(d >> 10, 11) * 4.0f, sext(d, 10) * 8.0f);
		m->dir_lights[i] = (light_dir_t){vec3_mulf(v, -1.0f / 4096), rgba(p[4], p[5], p[6], 255)};
	}
	p = e + get_u32(e + 0x18);
	m->point_lights = calloc(m->num_lights[1] + 1, sizeof(light_point_t));
	for (int i = 0; i < m->num_lights[1]; i++, p += 0x18) {
		m->point_lights[i] = (light_point_t){{get_i32(p), get_i32(p + 4), get_i32(p + 8)},
			rgba(p[12], p[13], p[14], 255), get_i32(p + 16), get_i32(p + 20)};
	}
	p = e + get_u32(e + 0x1c);
	m->spot_lights = calloc(m->num_lights[2] + 1, sizeof(light_spot_t));
	for (int i = 0; i < m->num_lights[2]; i++, p += 0x20) {
		m->spot_lights[i] = (light_spot_t){{get_i32(p), get_i32(p + 4), get_i32(p + 8)}, get_u32(p + 12),
			rgba(p[16], p[17], p[18], 255), get_i32(p + 20), get_i32(p + 24), get_u16(p + 28), get_u16(p + 30)};
	}
	p = e + get_u32(e + 0x20);
	m->box_lights = calloc(m->num_lights[3] + 1, sizeof(light_box_t));
	for (int i = 0; i < m->num_lights[3]; i++, p += 0x34) {
		light_box_t *b = &m->box_lights[i];
		b->pos = (ivec3_t){get_i32(p), get_i32(p + 4), get_i32(p + 8)};
		b->rot = get_u32(p + 12);
		b->color = rgba(p[16], p[17], p[18], 255);
		for (int k = 0; k < 8; k++) {
			b->box[k] = get_i32(p + 20 + 4 * k);
		}
	}
}

mission_t *mission_load(const char *path, int num_models, const int *model_parts) {
	uint32_t size;
	uint8_t *d = fs_load(path, &size);
	if (!d || size < 0x18 || get_u32(d) != MISSION_MAGIC) {
		free(d);
		return NULL;
	}
	mission_t *m = calloc(1, sizeof(mission_t));
	m->num_models = num_models;
	uint32_t sa = get_u32(d + 4), sb = get_u32(d + 8), sc = get_u32(d + 0xc);
	uint32_t sd = get_u32(d + 0x10), se = get_u32(d + 0x14);

	if (get_u32(d + sa) == 0xB0110CC5) {
		load_section_a(m, d + sa, sb - sa, model_parts);
	}
	load_section_b(m, d + sb);
	if (get_u32(d + sc) == 0x00B16A55) {
		const uint8_t *c = d + sc;
		m->interior = rgba(c[4], c[5], c[6], 255);
		m->exterior = rgba(c[8], c[9], c[10], 255);
		m->fog_near = get_i32(c + 12);
		m->fog_far = get_i32(c + 16);
	}
	if (get_u32(d + sd) == 0xC0CB0FFA) {
		m->num_surface = d[sd + 4];
		m->surface_class = calloc(m->num_surface + 1, 1);
		memcpy(m->surface_class, d + sd + 5, m->num_surface);
	}
	load_section_e(m, d + se);
	free(d);
	return m;
}

void mission_free(mission_t *m) {
	if (!m) {
		return;
	}
	for (int t = 0; t < 3; t++) {
		free(m->rec[t]);
	}
	free(m->pool);
	for (int i = 0; i < m->num_models; i++) {
		free(m->models[i].parts);
	}
	free(m->models);
	free(m->destroy_step);
	free(m->bbox);
	for (int i = 0; i < m->num_anims; i++) {
		free(m->anims[i].keys);
	}
	free(m->anims);
	free(m->surface_class);
	free(m->dir_lights);
	free(m->point_lights);
	free(m->spot_lights);
	free(m->box_lights);
	free(m);
}
