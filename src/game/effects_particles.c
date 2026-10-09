// The particle pool (0x454480-0x454ef0): 2048 particles of 0x18 bytes in two linked lists (free,
// active), the Particles task (type 0x4991c0, 40 ms) and their drawing (0x454560). Effects 0-8 of the
// table at 0x499150; 8 = sparks drawn as lines, 4 = a horizontal ring, the others camera-facing sprites
// of global texture 2 (4 x 4 cells of 32 pixels).
#include <stdlib.h>
#include <string.h>

#include "effects_int.h"
#include "lander.h"

#define NPART 0x800
#define NIL 0xffff

typedef struct {
	uint16_t next, prev;   // +0x00 / +0x02
	uint16_t age;          // +0x04 ticks since the spawn
	uint8_t type;          // +0x06 effect
	uint8_t timer;         // +0x07 ticks left on this frame
	int32_t pos[3];        // +0x08 world
	uint8_t p[3];          // +0x14 parameter (u16 at +0x14) or velocity s8[3] (effect 8)
	uint8_t frame;         // +0x17 animation frame; effect 8: flags (1 other colour/order, 2 gravity, 4 slow drag)
} particle_t;

// 0x499150: {first frame, last frame, ticks per frame}
static const struct { uint32_t first, last, ticks; } part_defs[9] = {
	{0, 3, 2}, {9, 9, 6}, {9, 9, 6}, {4, 7, 4}, {8, 8, 18}, {10, 10, 6}, {12, 15, 4}, {0, 3, 8}, {11, 10, 16},
};

static particle_t parts[NPART];          // gParticles 0x553b68
static struct { uint16_t head, tail; } free_list, active_list; // 0x553b64, 0x55fb68
static int32_t render_frac;              // 0x553b58
static task_t *part_task;

uint32_t gParticleColour;                // 0x61e8e0

static uint16_t idx_of(const particle_t *p) { return (uint16_t)(p - parts); }

static void list_unlink(void *list, particle_t *p) { // 0x454480
	struct { uint16_t head, tail; } *l = list;
	if (p->next == NIL) l->tail = p->prev; else parts[p->next].prev = p->prev;
	if (p->prev == NIL) l->head = p->next; else parts[p->prev].next = p->next;
}

static void list_append(void *list, particle_t *p) { // 0x4544e0
	struct { uint16_t head, tail; } *l = list;
	uint16_t i = idx_of(p);
	if (l->tail == NIL) l->head = i; else parts[l->tail].next = i;
	p->next = NIL;
	p->prev = l->tail;
	l->tail = i;
}

static void part_kill(particle_t *p) { // 0x454b80
	list_unlink(&active_list, p);
	list_append(&free_list, p);
}

void *eng_particle_spawn(int32_t effect, const int32_t pos[3], int32_t param) { // 0x454ae0
	if (free_list.head == NIL || (uint8_t)effect > 8) return NULL; // (no caller passes more than 8)
	particle_t *p = &parts[free_list.head];
	if (p->prev != NIL) return NULL;
	list_unlink(&free_list, p);
	list_append(&active_list, p);
	p->type = (uint8_t)effect;
	memcpy(p->pos, pos, sizeof(p->pos));
	p->age = 0;
	p->timer = (uint8_t)part_defs[effect & 0xff].ticks;
	if ((uint8_t)effect != 8) { // sparks keep +0x14..+0x17 of the slot's last use until set_velocity
		p->p[0] = (uint8_t)param;
		p->p[1] = (uint8_t)(param >> 8);
		p->frame = (uint8_t)part_defs[effect & 0xff].first;
	}
	return p;
}

void eng_particle_set_velocity(void *particle, int32_t vx, int32_t vy, int32_t vz) {
	particle_t *p = particle;
	p->p[0] = (uint8_t)vx;
	p->p[1] = (uint8_t)vy;
	p->p[2] = (uint8_t)vz;
}

// byte +0x17 of a particle: the frame, or for sparks (effect 8) the flags 1 (second colour, drawn the
// other way round), 2 (gravity), 4 (slow drag) — e.g. 7 for the sparks of FUN_00454ef0
void eng_particle_set_flags(void *particle, uint8_t flags) {
	if (particle) ((particle_t *)particle)->frame = flags;
}

// FUN_00454ef0: n sparks at pos with the velocity v +- 32 per axis, flags
void fx_spark_burst(const int32_t pos[3], const int32_t v[3], int n, uint8_t flags) {
	particle_t *p;
	while (n != 0 && (p = eng_particle_spawn(8, pos, 0)) != NULL) {
		int32_t x = v[0] - 0x20 + (fx_rand() & 0x3f);
		int32_t y = v[1] - 0x20 + (fx_rand() & 0x3f);
		int32_t z = v[2] - 0x20 + (fx_rand() & 0x3f);
		p->p[2] = (uint8_t)clamp(z, -0x80, 0x7f);
		p->p[0] = (uint8_t)clamp(x, -0x80, 0x7f);
		p->p[1] = (uint8_t)clamp(y, -0x80, 0x7f);
		p->frame = flags;
		n--;
	}
}

// ---------------------------------------------------------------- the task

static void particles_update(task_t *t) { // Particles_Update 0x454cf0
	(void)t;
	uint16_t i = active_list.head;
	while (i != NIL) {
		particle_t *p = &parts[i];
		uint16_t next = p->next;
		switch (p->type) {
		case 6:
			p->pos[0] += (int8_t)p->p[0];
			p->pos[2] += (int8_t)p->p[1];
			break;
		case 7:
			p->pos[1] += 0x10;
			break;
		case 8: {
			if (p->frame & 2) {
				p->p[0] = (uint8_t)(p->p[0] + (int8_t)(phys_g.gravity[0] >> 3));
				p->p[1] = (uint8_t)(p->p[1] + (int8_t)(phys_g.gravity[1] >> 3));
				p->p[2] = (uint8_t)(p->p[2] + (int8_t)(phys_g.gravity[2] >> 3));
			}
			int8_t vx = (int8_t)p->p[0], vy = (int8_t)p->p[1], vz = (int8_t)p->p[2];
			p->pos[0] += vx;
			p->pos[1] += vy;
			p->pos[2] += vz;
			if (!(p->frame & 4)) {
				p->p[0] = (uint8_t)(vx >> 1); p->p[1] = (uint8_t)(vy >> 1); p->p[2] = (uint8_t)(vz >> 1);
			} else {
				p->p[0] = (uint8_t)(vx - (vx >> 2)); p->p[1] = (uint8_t)(vy - (vy >> 2)); p->p[2] = (uint8_t)(vz - (vz >> 2));
			}
			break;
		}
		}
		p->age++;
		if (--p->timer == 0) {
			if (p->type == 8 || p->frame == part_defs[p->type].last) {
				part_kill(p);
			} else {
				p->timer = (uint8_t)part_defs[p->type].ticks;
				p->frame++;
			}
		}
		i = next;
	}
}

static void particles_render(task_t *t, uint32_t time) { // Particles_Render 0x454e80
	int32_t period = (int32_t)t->type->period;
	render_frac = ((period - (int32_t)t->wake + (int32_t)time) * 0x1000) / period;
}

static const task_type_t particles_task = {1 | 8, particles_update, particles_render, NULL, NULL, NULL, NULL, 40};

void fx_particles_reset(void) { // Particles_Init 0x454bb0
	for (int i = 0; i < NPART; i++) {
		parts[i].prev = (uint16_t)(i - 1);
		parts[i].next = (uint16_t)(i + 1);
	}
	parts[NPART - 1].next = NIL; // the original writes this terminator one record too far
	parts[0].prev = NIL;
	free_list.head = 0;
	free_list.tail = NPART - 1;
	active_list.head = active_list.tail = NIL;
	part_task = NULL;
}

// Mission_Run creates the task after the mission's start function (so the particles of a tick are
// stepped after the objects that spawn them): the first frame creates it here.
void fx_particles_start(void) {
	if (!part_task) part_task = Task_CreateNow(&particles_task, NULL);
}

// ---------------------------------------------------------------- drawing (0x454560)

// FUN_0045b4e0: effect 4, a horizontal textured quad around the point (splash ring)
static void draw_ring(const texture_t *tex, int u0, int v0, int u1, int v1, vec3_t c, int32_t size, uint32_t level) {
	int32_t h = ((u1 - u0) * size) >> 13;
	vec3_t corner[4] = {vec3(c.x - h, c.y, c.z - h), vec3(c.x + h, c.y, c.z - h), vec3(c.x + h, c.y, c.z + h),
		vec3(c.x - h, c.y, c.z + h)};
	float tu0 = (u0 + 0.5f) / tex->width, tu1 = (u1 - 0.5f) / tex->width;
	float tv0 = (v0 + 0.5f) / tex->height, tv1 = (v1 - 0.5f) / tex->height;
	float us[4] = {tu0, tu1, tu1, tu0}, vs[4] = {tv0, tv0, tv1, tv1};
	uint32_t col = ((((level | 0xffff8000u) << 8) | level) << 8) | level;
	fxv_t q[4];
	float depth = 0;
	for (int i = 0; i < 4; i++) {
		vec3_t cam = fx_to_cam(corner[i]);
		if (cam.z <= 0) return; // RotTransPers failed
		depth = cam.z - 4;
		q[i] = (fxv_t){corner[i], us[i], vs[i], col};
	}
	fxv_t t[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
	fx_queue(t, 6, tex, FX_ADD, true, FX_LAYER_3D); (void)depth;
}

// 0x499290: spark colours, D3D 0x00RRGGBB
static const uint32_t spark_colours[2] = {0x00ffff00, 0x007050d0};

// FUN_0045b820 + FUN_0045b880: a line from a, seen by the camera of 40 ms ago (so a moving view
// streaks it), in the spark colour, to b (current camera) in black; additive, no fog
static void draw_spark(vec3_t a, vec3_t b, int colour, uint32_t level, mat4_t prev_view) {
	vec3_t cb = fx_to_cam(b);
	if ((int32_t)cb.z - 8 < 0x40) return;
	vec4_t pa = mat4_mul_vec4(prev_view, (vec4_t){a.x, a.y, a.z, 1});
	if ((int32_t)pa.z - 8 < 0x40) return;
	// the first point: where the previous view saw a, at that depth, placed in the current view
	float sx = fxc.cx + pa.x * fxc.h / pa.z, sy = fxc.cy - pa.y * fxc.h / pa.z;
	vec3_t wa = fx_unproject(sx, sy, pa.z - 8);
	vec3_t wb = fx_unproject(fxc.cx + cb.x * fxc.h / cb.z, fxc.cy + cb.y * fxc.h / cb.z, cb.z - 8);
	uint32_t c = 0xff000000u | fx_colour_scale(spark_colours[colour & 1], (int32_t)level);
	fx_queue_line(wa, wb, c, 0xff000000u, FX_ADD, false, FX_LAYER_3D);
}

void fx_particles_draw(void) {
	const texture_t *tex = fx_global_tex(2);
	mat4_t prev = fx_view_at(fxc.now - 0x28);
	vec3_t eye = fxc.eye;
	for (uint16_t i = active_list.head; i != NIL; i = parts[i].next) {
		particle_t *p = &parts[i];
		// relative to the camera, culled at +-0x8000 per axis (s16 GTE input)
		int32_t d[3] = {p->pos[0] - (int32_t)eye.x, p->pos[1] - (int32_t)eye.y, p->pos[2] - (int32_t)eye.z};
		if (abs(d[0]) >= 0x8000 || abs(d[1]) >= 0x8000 || abs(d[2]) >= 0x8000) continue;
		vec3_t w = vec3((float)p->pos[0], (float)p->pos[1], (float)p->pos[2]);
		if (p->type == 8) {
			int8_t vx = (int8_t)p->p[0], vy = (int8_t)p->p[1], vz = (int8_t)p->p[2];
			vec3_t a = vec3(w.x + (float)((render_frac * vx) >> 12), w.y + (float)((render_frac * vy) >> 12),
				w.z + (float)((render_frac * vz) >> 12));
			vec3_t b = vec3(a.x + vx, a.y + vy, a.z + vz);
			if (!(p->frame & 1)) draw_spark(a, b, 0, (uint32_t)p->timer << 8, prev);
			else draw_spark(b, a, 1, (uint32_t)p->timer << 8, prev);
			continue;
		}
		vec3_t cam = fx_to_cam(w);
		if (cam.z <= 0) continue;
		float sx = fxc.cx + cam.x * fxc.h / cam.z, sy = fxc.cy + cam.y * fxc.h / cam.z;
		if (sx <= -32768 || sx >= 32767 || sy <= -32768 || sy >= 32767) continue;
		int32_t z = ((int32_t)cam.z >> 2) * 4 - 8;
		if (z < 0x40) continue;
		uint32_t colour = 0x808080;
		int32_t scale, angle = 0;
		unsigned flags = 7;
		int blend = FX_ADD;
		uint32_t level = 0;
		switch (p->type) {
		case 0: {
			int32_t s = 0x200000 / z;
			scale = p->age < 2 ? ((fx_rsin((p->age * 0xc00) / 2) * s) >> 12) + s * 3 : s * 2;
			break;
		}
		case 1:
		case 2: {
			int32_t s;
			if (p->type == 2) { colour = 0x80c0; s = 0x400000 / z; }
			else { colour = 0x80ff; s = 0x800000 / z; }
			int32_t sn = fx_rsin((p->age << 11) / 6);
			scale = p->age < 3 ? ((sn * s) >> 12) + s * 3 : s * 2;
			z -= 0x40;
			break;
		}
		case 3: {
			int32_t s = 0x1000000 / z, g = 0x10 - p->age;
			int32_t k = (p->age + 1) * s;
			scale = ((k + ((k >> 31) & 0xf)) >> 4) + s;
			sy -= (float)((scale * 0x20) >> 13);
			colour = (uint32_t)(g * 8) * 0x010101u;
			break;
		}
		case 4:
			scale = ((p->age * 0xf00) / 0x12 + 0x100) * 0x20;
			level = (uint32_t)((p->p[0] | p->p[1] << 8) - ((p->p[0] | p->p[1] << 8) / 0x12) * p->age);
			break;
		case 6: {
			int32_t s = 0x1000000 / z;
			int32_t k = (p->age + 1) * s;
			scale = ((k + ((k >> 31) & 0xf)) >> 4) + s;
			sy -= (float)((scale * 0x20) >> 13);
			colour = gParticleColour;
			break;
		}
		case 7:
			scale = (0x800000 / z) << 1;
			colour = 0x404040;
			break;
		default:
			scale = 0x800000 / z;
			break;
		}
		uint8_t nf = (uint8_t)~p->frame;
		int u0 = (nf & 3) << 5, v0 = ((nf >> 2) & 3) << 5;
		if (p->type == 4) {
			if (tex) draw_ring(tex, u0, v0, u0 + 0x1f, v0 + 0x1f, w, scale, level);
			continue;
		}
		if (p->type == 6 || p->type == 7) { // alpha blended, rotated by the slot index
			blend = FX_ALPHA;
			flags = 0xf;
			uint32_t k = i ^ (i * 2u);
			k ^= k * 2;
			angle = (int32_t)((((k & 0xff) << 1) ^ (k & 0x1ff)) << 3);
		}
		fx_sprite_section(sx, sy, (float)z, tex, u0, v0, u0 + 0x1f, v0 + 0x1f, flags, colour, scale, angle, blend, true);
	}
}
