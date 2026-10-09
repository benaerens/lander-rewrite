// The player's ship: Lander object (type 1). See lander.h and re/specs/lander.md.
//
// Faithful port of lander.EXE (D3D build). Integer arithmetic is reproduced operation by
// operation (shifts, truncating divisions, 16-bit stores) because demos and network games replay
// control words only. Rendering, sound, HUD and the object system are reached through the eng_*
// adapters declared in lander.h; rigid-body physics through physics.h.
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "lander.h"

// =================================================================================== data tables

// gLanderModelTable 0x4933f8 (5 x 0x54), decoded from the exe's initialised data
const LanderModelDef gLanderModelTable[5] = {
	// Hopper
	{ 0, {14, 73}, 199, 141, 1, {{0, -24, 0, 0}, {0, 0, 0, 0}}, {0xc86464, 0xa00a0a},
	  1, 0, {0, 0, 0}, {90, 45, 90}, 0, {0, 0, 0}, 0, 0, {-99, 46, 132}, 0, 26, 0x0aab, 0x0c00, 26 },
	// Sky Diver
	{ 2, {1, 59}, 344, 243, 2, {{61, -86, 0, 0}, {-61, -86, 0, 0}}, {0xc86464, 0xa00a0a},
	  2, 1, {41, 0, 0}, {0, -38, 166}, 0, {0, -37, 162}, 0, 0, {-116, 73, 100}, 0, 24, 0x1000, 0x0f00, 26 },
	// Big Red
	{ 4, {-52, 78}, 335, 237, 2, {{147, 0, 0, 0}, {-147, 0, 0, 0}}, {0xaac8c8, 0x000032},
	  2, 1, {11, 31, -16}, {166, 58, 131}, 0, {-141, 32, 154}, 0, -1024, {78, 28, 216}, 0, 14, 0x2200, 0x1aab, 18 },
	// Destria
	{ 6, {-3, 100}, 329, 233, 2, {{56, 28, 0, 0}, {-56, 28, 0, 0}}, {0xc86464, 0xa00a0a},
	  1, 0, {0, 0, 0}, {120, 83, 0}, 0, {-148, 32, 205}, 0, -239, {-95, 73, 6}, 0, 48, 0x0f00, 0x1000, 40 },
	// Roche 81
	{ 8, {-115, -115}, 366, 259, 2, {{127, -83, 0, 0}, {-127, -83, 0, 0}}, {0xc86464, 0xa00a0a},
	  2, 1, {128, 0, 0}, {0, -83, 192}, 0, {-96, 64, 147}, 0, -307, {202, 106, 104}, 0, 40, 0x12ab, 0x1300, 36 },
};

// gWeaponDefs 0x497fd8: {kind, flags, arg2, arg3, heatPerShot, coolRate, pad, mount}
const LanderWeaponDef gWeaponDefs[23] = {
	{0, 1, 0x10, 0, 0x000, 0x00, 0, 0},  //  0 none
	{4, 1, 0x10, 0, 0x264, 0x25, 0, 0},  //  1 Pulse 1000
	{4, 1, 0x18, 0, 0x264, 0x25, 0, 0},  //  2 Pulse 1500
	{4, 1, 0x1c, 0, 0x264, 0x25, 0, 0},  //  3 Pulse 2000
	{4, 1, 0x20, 0, 0x264, 0x25, 0, 0},  //  4 Pulse 2500
	{4, 1, 0x28, 0, 0x264, 0x25, 0, 0},  //  5 Pulse 5000
	{8, 1, 0x20, 0, 0x36e, 0x25, 0, 0},  //  6 Beam 500
	{8, 1, 0x28, 0, 0x36e, 0x31, 0, 0},  //  7 Beam 1000
	{8, 1, 0x30, 0, 0x36e, 0x49, 0, 0},  //  8 Beam 1500
	{9, 1, 0x18, 0, 0x640, 0x78, 0, 0},  //  9 Solid Slug
	{9, 1, 0x28, 1, 0x640, 0xa0, 0, 0},  // 10 Rasl
	{9, 1, 0x40, 2, 0x640, 0xc8, 0, 0},  // 11 DPU
	{9, 3, 0x08, 3, 0x125, 0x49, 0, 0},  // 12 Rapid Fire   (auto)
	{9, 3, 0x0c, 4, 0x1b7, 0x49, 0, 0},  // 13 Flechette    (auto)
	{9, 3, 0x10, 5, 0x24c, 0x49, 0, 0},  // 14 Explosive    (auto)
	{2, 4, 0x60, 6, 0, 0, 0, 1},         // 15 Wildcat Missile    (homing, 5)
	{2, 4, 0x60, 7, 0, 0, 0, 2},         // 16 Wolverine Missiles (homing, 10)
	{2, 4, 0x60, 8, 0, 0, 0, 3},         // 17 Tiger Missiles     (homing, 20)
	{1, 0, 0x30, 6, 0, 0, 0, 1},         // 18 Wildcat Rocket
	{1, 0, 0x30, 7, 0, 0, 0, 2},         // 19 Wolverine Rockets
	{1, 0, 0x30, 8, 0, 0, 0, 3},         // 20 Tiger Rockets
	{5, 1, 0x10, 0, 0x24c, 0x25, 0, 0},  // 21 Pacifier
	{7, 1, 0x10, 0, 0x4000, 0x124, 0, 0},// 22 Flare (one shot fills the gauge)
};

// gWeaponMountTable 0x497fa8: global model + muzzle offset
const LanderWeaponMountDef gWeaponMountTable[4] = {
	{0x10, {0, 6, 0x8f}, 0}, {0x11, {0, 0x18, 0x5f}, 0}, {0x12, {0, 0x23, 0x5f}, 0}, {0x13, {0, 0x23, 0x5f}, 0},
};

// gAttitudeJetTemplate 0x497e38 (8 x 0x28). Mutable: Lander_Create patches it per lander type
// before copying it into the controller (the original writes the global template).
static LanderJet sJetTemplate[8] = {
	{{0, 0, 410}, 0, {0, 0, 128, 0}, {0, 0x1000, 0}, 0, 0, 16},
	{{0, 0, -410}, 0, {0, 0, -128, 0}, {0, 0x1000, 0}, 0, 0, 16},
	{{-410, 0, 0}, 0, {-128, 0, 0, 0}, {0, 0x1000, 0}, 0, 0, 16},
	{{410, 0, 0}, 0, {128, 0, 0, 0}, {0, 0x1000, 0}, 0, 0, 16},
	{{-197, 0, -197}, 0, {-64, 0, -64, 0}, {2896, 0, -2896}, 0, 0, 305},
	{{197, 0, 197}, 0, {64, 0, 64, 0}, {2896, 0, -2896}, 0, 0, 305},
	{{-197, 0, 197}, 0, {-64, 0, 64, 0}, {-2896, 0, -2896}, 0, 0, 305},
	{{197, 0, -197}, 0, {64, 0, -64, 0}, {-2896, 0, -2896}, 0, 0, 305},
};

// gEngineFxTemplate 0x4933c8 (copied to lander +0x2cc)
static const LanderEngineFx sEngineFxTemplate = {
	{0, -80, 0}, 0, {0, -0x1000, 0, 0}, 0x400, 0, {0x1000, 0x1000}, 0, {0xc86464, 0xa00a0a}, NULL
};

static const int32_t sDefaultHullDims[3] = {0x300, 0xc8, 0x300};   // 0x4933a8

// =================================================================================== globals
Lander  gLanders[LANDER_MAX];        // 0x573b40
int32_t gMaxTiltAngle      = 0x400;  // 0x490120
int32_t gStableCutOffTime  = 2;      // 0x4935e0
int32_t gTractorRange      = 750;    // 0x498a00
int32_t gAutoHeading       = 0;      // 0x4a6de8
int32_t gCheatInfiniteFuel = 0;      // 0x4a6dec
int32_t gCheatInvulnerable = 0;      // 0x4a6df0
Lander *gHudLander         = NULL;   // 0x4a6dcc
LanderHud gLanderHud;
static int32_t sFFCooldown;          // 0x4a6dd0
static int32_t sWarnVoiceToggle;     // 0x4a6df4

// =================================================================================== fixed-point math
// PSX-style library of the exe (0x435400-0x436a10). The x87 FPU runs with 24-bit precision in
// single player (Mission_Run) and 53-bit in network games (mode 4): products are rounded to that
// precision, fsin/fcos/fpatan results are not; _ftol (0x4881bc) truncates toward zero; fistp rounds
// to nearest-even. lm_fpu53 selects the precision.
int lm_fpu53 = 0;
static double lm_pc(double x) { return lm_fpu53 ? x : (double)(float)x; }
static int32_t lm_ftol(double x) { return (int32_t)x; }
static int32_t lm_fistp(double x)
{
	double r = floor(x + 0.5);
	if (r - x == 0.5 && fmod(r, 2.0) != 0.0) r -= 1.0;
	return (int32_t)r;
}

static int32_t lm_rsin(int32_t a)                      // rsin 0x435450
{
	double x = lm_pc((double)a * 6.283185307179586) * 0.000244140625;
	return lm_ftol(lm_pc(sin(x) * 4096.0));
}
static int32_t lm_rcos(int32_t a)                      // rcos 0x435470
{
	double x = lm_pc((double)a * 6.283185307179586) * 0.000244140625;
	return lm_ftol(lm_pc(cos(x) * 4096.0));
}
static int32_t lm_ratan2(int32_t y, int32_t x)          // ratan2 0x435490: atan2(y, x) in 4096/turn
{
	double r = lm_pc(atan2((double)y, (double)x) * 4096.0);
	return lm_ftol(lm_pc(r * 0.15915494309189535));
}
static int32_t lm_catan(int32_t t)                     // catan 0x4354b0: atan(t/4096) in 4096/turn
{
	double r = lm_pc(atan((double)t * 0.000244140625) * 4096.0);
	return lm_ftol(lm_pc(r * 0.15915494309189535));
}
static int32_t lm_fwsqrt(int32_t x)                    // FwSqrt 0x4354e0
{
	return lm_fistp(lm_pc(sqrt((double)x)));
}
static int32_t lm_qsqrt32(int32_t x)                   // 0x426310: (int)(sqrt(x) * 32.0f)
{
	return lm_ftol(lm_pc(sqrt((double)x)) * 32.0);
}

static void lm_identity(FwMatrix *m)                   // 0x431730 (rotation only)
{
	memset(m->m, 0, sizeof m->m);
	m->m[0][0] = m->m[1][1] = m->m[2][2] = 0x1000;
}

static void lm_mul_matrix0(const FwMatrix *a, const FwMatrix *b, FwMatrix *out) // MulMatrix0 0x435790
{
	int16_t r[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r[i][j] = (int16_t)((a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] +
			                     a->m[i][2] * b->m[2][j] + 0x800) >> 12);
	memcpy(out->m, r, sizeof r);
}

static void lm_transpose(const FwMatrix *a, FwMatrix *out)            // TransposeMatrix 0x436230
{
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out->m[i][j] = a->m[j][i];
}

static void lm_invmatvec2(const FwMatrix *m, const int32_t v[3], int32_t out[3]) // invmatvec2 0x436aa0: M^T v
{
	int32_t r[3];
	for (int i = 0; i < 3; i++)
		r[i] = m->m[0][i] * v[0] + m->m[1][i] * v[1] + m->m[2][i] * v[2];
	for (int i = 0; i < 3; i++)
		out[i] = (r[i] + 0x800) >> 12;
}

// 0x42d1e0: out = a x R, R = [[c,0,s],[0,1,0],[-s,0,c]], c = rcos(-angle), s = rsin(-angle)
static void lm_mul_rot_y(const FwMatrix *a, FwMatrix *out, int32_t angle)
{
	FwMatrix r;
	lm_identity(&r);
	r.m[0][0] = r.m[2][2] = (int16_t)lm_rcos(-angle);
	int16_t s = (int16_t)lm_rsin(-angle);
	r.m[0][2] = s;
	r.m[2][0] = (int16_t)-s;
	lm_mul_matrix0(a, &r, out);
}

// 0x42d410: out = in x Q, Q = rotation by `angle` about `axis` expressed in the frame of `in`
// (axis is brought into that frame with invmatvec2). 16-bit stores as in the original.
static void lm_rotate_axis(const FwMatrix *in, FwMatrix *out, int32_t angle, const int32_t axis[3])
{
	int32_t a[3];
	FwMatrix q, rin = *in;
	lm_invmatvec2(in, axis, a);
	int32_t zz = a[2] * a[2] >> 12, xx = a[0] * a[0] >> 12, yy = a[1] * a[1] >> 12;
	int32_t c = lm_rcos(-angle);
	q.m[0][0] = (int16_t)((int16_t)((0x1000 - xx) * c >> 12) + (int16_t)xx);
	q.m[1][1] = (int16_t)((int16_t)((0x1000 - yy) * c >> 12) + (int16_t)yy);
	q.m[2][2] = (int16_t)((int16_t)((0x1000 - zz) * c >> 12) + (int16_t)zz);
	int32_t t = 0x1000 - c;
	int32_t s = lm_rsin(-angle);
	int16_t sz = (int16_t)(s * a[2] >> 12);
	int16_t xy = (int16_t)(((a[1] * a[0] >> 12) * t) >> 12);
	q.m[0][1] = (int16_t)(xy - sz);
	q.m[1][0] = (int16_t)(sz + xy);
	int16_t sy = (int16_t)(s * a[1] >> 12);
	int16_t xz = (int16_t)(((a[2] * a[0] >> 12) * t) >> 12);
	q.m[0][2] = (int16_t)(sy + xz);
	q.m[2][0] = (int16_t)(xz - sy);
	int16_t sx = (int16_t)(a[0] * s >> 12);
	int16_t yz = (int16_t)(((a[2] * a[1] >> 12) * t) >> 12);
	q.m[2][1] = (int16_t)(sx + yz);
	q.m[1][2] = (int16_t)(yz - sx);
	lm_mul_matrix0(&rin, &q, out);   // MulRotMatrix0(q, out) with the rot matrix = in
}

// 0x420460: a' = (a*c + b*s) >> 12, b' = (b*c - a*s) >> 12
static void lm_rotate_pair(int32_t s, int32_t c, int16_t *a, int16_t *b)
{
	int32_t av = *a, bv = *b;
	*a = (int16_t)((av * c + bv * s) >> 12);
	*b = (int16_t)((bv * c - av * s) >> 12);
}

static int32_t lm_abs(int32_t v) { return v < 0 ? -v : v; }

// 0x420430: a - b wrapped to +-0x800
static int32_t lm_angle_diff(int32_t a, int32_t b)
{
	int32_t d = a - b;
	if (lm_abs(d) > 0x800)
		d += (d < 0) ? 0x1000 : -0x1000;
	return d;
}

// 0x42d070: v = Rz v, Rz = [[c,-s,0],[s,c,0],[0,0,1]], c = rcos(-a), s = rsin(-a) (ApplyMatrixLV)
static void lm_vec_rot_z(int32_t v[3], int32_t a)
{
	int32_t c = lm_rcos(-a), s = lm_rsin(-a);
	int32_t x = v[0], y = v[1];
	v[0] = (c * x + 0x800 + (-s) * y) >> 12;
	v[1] = (s * x + 0x800 + c * y) >> 12;
	v[2] = (0x1000 * v[2] + 0x800) >> 12;
}

// =================================================================================== small helpers
Lander *Lander_FromHandle(ObjId id)                     // 0x420f30
{
	LanderProc *p = eng_lander_proc(id);                // Objects_GetData(id, 0, &proc)
	return p ? p->lander : NULL;
}

static int lander_is_local(const LanderProc *p) { return gThisPlayer == (int32_t)p->id; }

// 0x42d920 with the transform node of the process: coord = pos (+ vel*frac), rotated by angvel*frac
static void lander_sync_coord(LanderProc *p, int32_t frac)
{
	(void)p;
	phys_body_t *b = p->lander->body;
	if (b->coord)
		phys_body_write_transform(b, b->coord, frac);
}

static void lander_headlight_off(Lander *L)             // 0x4205b0
{
	eng_light_destroy(L->headlight);
	L->headlight = NULL;
}

// 0x4204c0: spot light at the per-model headlight point
static void lander_headlight_on(Lander *L, LanderProc *p, int32_t cone, int32_t intensity)
{
	const LanderModelDef *md = &gLanderModelTable[L->ps.landerType];
	L->headlight = eng_light_create(2, p->coord);
	eng_light_set_flag10(L->headlight);                // light +0x60 |= 0x10
	int32_t v = intensity >> 3;
	L->headlightLevel = (int16_t)v;
	eng_light_colour(L->headlight, v, v, v);
	eng_light_range(L->headlight, 0x1000, 0x2000);
	int32_t outer = (cone >> 3) + cone;
	eng_light_cone(L->headlight, cone - (cone >> 1), outer);
	eng_light_position(L->headlight, md->headlight[0], md->headlight[1], md->headlight[2]);
	L->headlightGlow = eng_headlight_glow((uint16_t)L->headlightLevel >> 4, cone, outer);
	L->headlightCone = (int16_t)cone;
}

// 0x420d10: model instance (global models), draw callback 0x425980, team tint, mob record +0x20
static void lander_create_model(LanderProc *p, Lander *L)
{
	const LanderModelDef *md = &gLanderModelTable[L->ps.landerType];
	p->modelInst = eng_model_inst_create(md->modelBase + L->damaged);
	p->modelCoord = eng_model_inst_coord(p->modelInst);
	eng_model_inst_setup_lander(p->modelInst, p->coord, (int32_t)(L - gLanders),
	                            gMultiPlayerGame ? &gShipColours[L->ps.team] : NULL);
	eng_map_set_mob_model(p->id, p->modelInst);
}

static void lander_release_model(LanderProc *p, Lander *L)  // 0x420e20
{
	eng_model_inst_release(gLanderModelTable[L->ps.landerType].modelBase + L->damaged, p->modelInst);
}

// =================================================================================== weapons
// 0x4274c0
static void weapons_add_slot(LanderWeapons *w, int32_t type, int32_t missiles)
{
	if (w->count == 2 || type == 0)
		return;
	LanderWeaponSlot *s = &w->slot[w->count];
	s->type = type;
	s->cooldown = 0;
	s->level = (gWeaponDefs[type].flags & LWF_ENERGY) ? 0 : (int16_t)missiles;
	w->count++;
}

// 0x427920
static void weapons_select(LanderWeapons *w, int32_t idx, int32_t player)
{
	if (w->count == 0) {
		w->current = 0xff;
		return;
	}
	if (idx < 0 || idx >= w->count)
		return;
	w->current = (uint8_t)idx;
	if (player == gThisPlayerId)
		eng_ctrl_set_fire_autorepeat((gWeaponDefs[w->slot[idx].type].flags & LWF_AUTOFIRE) != 0);
}

// 0x4273c0
static void weapons_select_next(LanderWeapons *w, int isHud)
{
	if (w->count == 0)
		return;
	if (w->count == 1) {
		if (isHud && CockVox(1))
			eng_snd_chain(SND_COCKPIT_VOICE, 2, 0x410, 0x41d);
		return;
	}
	w->current++;
	if (w->current == w->count)
		w->current = 0;
	int32_t type = w->slot[w->current].type;
	if (!isHud)
		return;
	if (CockVox(1)) {
		int32_t sid = gWeaponDefs[type].kind == 1 ? 0x416 : gWeaponDefs[type].kind == 2 ? 0x410 : 0x412;
		eng_snd_chain(SND_COCKPIT_VOICE, 2, sid, 0x418);
	}
	eng_ctrl_set_fire_autorepeat((gWeaponDefs[type].flags & LWF_AUTOFIRE) != 0);
}

// 0x4275d0: attach the mount model(s) of `type` and compute the muzzle points (lander frame).
// mount 0 = the lander's own hardpoints (perModel +0x28), mounts 1..3 = missile pods at podPos.
static void weapons_attach_mount(LanderWeapons *w, int32_t type, int16_t landerInst, const LanderModelDef *md)
{
	if (type == 0)
		return;
	int32_t mt = (int32_t)gWeaponDefs[type].mount;
	const LanderWeaponMountDef *mdef = &gWeaponMountTable[mt];
	for (int i = 0; i < w->nMounts; i++)
		if (eng_model_inst_tag(w->mount[i].modelInst[0]) == mt)   // render item +8 = mount type
			return;
	LanderWeaponMount *m = &w->mount[w->nMounts++];
	m->nModels = (mt == 0) ? md->hardpoints : 1;
	void *parent = eng_model_inst_coord(landerInst);
	for (int k = 0; k < m->nModels; k++) {
		m->modelInst[k] = eng_model_inst_create(mdef->model);
		eng_model_inst_set_tag(m->modelInst[k], mt);
		int32_t t[3] = {0, 0, 0};
		if (mt == 0) {
			int sgn = (k == 0) ? 1 : -1;
			for (int a = 0; a < 3; a++)
				t[a] = sgn * md->mirror[a] + md->mountOfs[a];
			m->muzzle[k][0] = (int16_t)(gWeaponMountTable[0].muzzle[0] + t[0]);
			m->muzzle[k][1] = (int16_t)(gWeaponMountTable[0].muzzle[1] + t[1]);
			m->muzzle[k][2] = (int16_t)(gWeaponMountTable[0].muzzle[2] + t[2]);
		} else {
			t[0] = md->podPos[0];
			t[1] = md->podPos[1];
			t[2] = md->podPos[2];
			int32_t mz[3] = {mdef->muzzle[0], mdef->muzzle[1], mdef->muzzle[2]};
			lm_vec_rot_z(mz, md->podRotZ);                      // 0x42d070
			m->muzzle[k][0] = (int16_t)(mz[0] + t[0]);
			m->muzzle[k][1] = (int16_t)(mz[1] + t[1]);
			m->muzzle[k][2] = (int16_t)(mz[2] + t[2]);
		}
		// every part of the mount model: parent = the lander model's transform node, translation
		// += t, pods rolled by podRotZ (0x42d0f0), render item depth bias 0xc0
		eng_model_inst_attach(m->modelInst[k], parent, t, mt == 0 ? 0 : md->podRotZ);
	}
}

static void weapons_release_mounts(LanderWeapons *w)    // 0x4278a0
{
	for (int i = 0; i < w->nMounts; i++)
		for (int k = 0; k < w->mount[i].nModels; k++)
			eng_model_inst_release(gWeaponMountTable[eng_model_inst_tag(w->mount[i].modelInst[0])].model,
			                       w->mount[i].modelInst[k]);
}

static void weapons_queue_mounts(const LanderWeapons *w) // 0x427580
{
	for (int i = 0; i < w->nMounts; i++)
		for (int k = 0; k < w->mount[i].nModels; k++)
			eng_model_queue(w->mount[i].modelInst[k]);
}

// 0x420e70
static void lander_init_weapons(LanderProc *p, Lander *L)
{
	const LanderModelDef *md = &gLanderModelTable[L->ps.landerType];
	LanderWeapons *w = &L->weapons;
	w->count = 0;
	w->current = 0;
	weapons_add_slot(w, L->ps.primary, L->ps.missiles);
	weapons_add_slot(w, L->ps.secondary, L->ps.missiles);
	w->nMounts = 0;
	weapons_attach_mount(w, L->ps.primary, p->modelInst, md);    // with gGlobalModels selected
	weapons_attach_mount(w, L->ps.secondary, p->modelInst, md);
	weapons_select(w, 0, L->player);
}

// 0x426c00: choose the target with the smallest distance / cos(angle to the forward axis)
static void lander_pick_target(const int16_t *list, ObjId *target, int keep, ObjId owner)
{
	uint32_t best = 0xffffffffu;
	ObjId bestId = OBJ_NONE;
	const int32_t *me = (const int32_t *)Object_GetProperty(owner, PROP_POSITION);
	const int32_t *fwd = eng_target_forward();          // transformed volume normal 4 (PTR 0x497d50 + 0x40)
	for (; *list != -1; list++) {
		ObjId id = eng_target_list_id(*list);
		const int32_t *pos = (const int32_t *)Object_GetProperty(id, PROP_POSITION);
		if (!pos)
			continue;
		int32_t d[3] = {pos[0] - me[0], pos[1] - me[1], pos[2] - me[2]};
		if (lm_abs(d[0]) >= 0x8000 || lm_abs(d[1]) >= 0x8000 || lm_abs(d[2]) >= 0x8000)
			continue;
		int32_t len = phys_normalise(d);
		if (len == 0)
			continue;
		uint32_t dot = (uint32_t)((d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2] + 0x800) >> 12);
		if (dot == 0)
			continue;
		uint32_t score = (uint32_t)(len << 8) / dot;      // unsigned: targets behind score huge
		if (score < best) {
			best = score;
			bestId = id;
		}
	}
	if (keep && bestId == OBJ_NONE && *target != OBJ_NONE &&
	    (!(*target & 0x1000000) || eng_mob_is_awake((int32_t)(*target & 0xffff)))) {
		const int32_t *tp = (const int32_t *)Object_GetProperty(*target, PROP_POSITION);
		if (tp[0] <= me[0] + 0x3200 && me[0] - 0x3200 <= tp[0] &&
		    tp[1] <= me[1] + 0x3200 && me[1] - 0x3200 <= tp[1] &&
		    tp[2] <= me[2] + 0x3200 && me[2] - 0x3200 <= tp[2] &&
		    (Object_GetProperty(*target, PROP_FLAGS) & 0x4000))
			return;                                        // homing weapons keep their lock
	}
	*target = bestId;
}

// =================================================================================== attitude controller
// 0x426fe0
static void attctrl_set_jet_pair(int a, int b, int32_t v, LanderJet *jets, int isLocal)
{
	if (isLocal && (jets[a].intensity == 0 || jets[b].intensity == 0)) {
		int32_t m = lm_abs(v);
		if (m > 0x100) {
			if (m >= 0x200)
				m = 0x200;
			eng_snd_once_vol(gThisPlayer, 0x13, m * 64 + 0x3fff);   // thruster puff
		}
	}
	jets[a].intensity = -v;
	jets[b].intensity = v;
}

// Lander_AttitudeCtrl2_ 0x427060: add the body-frame torque, drive the 8 jets
int32_t AttCtrl_Apply(const int32_t torque[3], LanderAttCtrl *c, int isLocal)
{
	phys_body_t *b = c->body;
	b->torque[0] += torque[0];
	b->torque[1] += torque[1];
	b->torque[2] += torque[2];
	int32_t x = torque[0] / 128;                  // (v + (v>>31 & 0x7f)) >> 7
	attctrl_set_jet_pair(0, 1, x, c->jets, isLocal);
	int32_t z = torque[2] / 128;
	attctrl_set_jet_pair(2, 3, z, c->jets, isLocal);
	int32_t y = torque[1] / 90;                   // idiv by 0x5a (magic 0xb60b60b7)
	attctrl_set_jet_pair(4, 5, y >> 1, c->jets, isLocal);
	attctrl_set_jet_pair(6, 7, y >> 1, c->jets, isLocal);
	return lm_abs(x) + lm_abs(z) + lm_abs(y);
}

// Lander_AttitudeCtrl_ 0x426330.
//   target : commanded attitude (lander +0x60)
//   rate   : desired angular velocity (world frame in, body frame after the call; only y is used
//            by the lander: the yaw rate)
//   strong : 1 = Recover button (bigger torque/rate limits)
// Per body axis i: err = rotation-vector component (radians 4.12) that turns the body onto the
// target, rel = w_body[i] - rate[i]. The output torque is a clamped linear term or a bang-bang
// value chosen by a stopping-distance test (rel^2 / (4*limit)).
void AttCtrl_Run(LanderAttCtrl *c, const FwMatrix *target, int32_t rate[3], int strong, int isLocal)
{
	phys_body_t *b = c->body;
	const FwMatrix *B = (const FwMatrix *)&b->cur->mat;
	FwMatrix Bt, R;
	int32_t out[3] = {0, 0, 0};
	int32_t q[3], w;                                   // q = {x, y, z} at [esp+0x20]

	lm_transpose(B, &Bt);
	lm_mul_matrix0(&Bt, target, &R);                   // R = B^T T (body -> target)
	int32_t r00 = R.m[0][0], r11 = R.m[1][1], r22 = R.m[2][2];
	w = r00 + r11 + 0x1000 + r22;
	w = w > 0 ? lm_qsqrt32(w) : 0;
	q[0] = r00 - r11 - r22 + 0x1000;  q[0] = q[0] > 0 ? lm_qsqrt32(q[0]) : 0;
	q[1] = r11 - r00 - r22 + 0x1000;  q[1] = q[1] > 0 ? lm_qsqrt32(q[1]) : 0;
	q[2] = r22 - r00 - r11 + 0x1000;  q[2] = q[2] > 0 ? lm_qsqrt32(q[2]) : 0;
	// largest component, then the others from the off-diagonal sums (0x4261d0..0x4262c0)
	int which;                                         // 0 x, 1 y, 2 z, 3 w
	if (w < q[0]) {
		if (q[1] < q[0]) which = (q[2] < q[0]) ? 0 : 2;
		else which = (q[2] < q[1]) ? 1 : 2;
	} else if (q[1] < w) {
		which = (q[2] < w) ? 3 : 2;
	} else {
		which = (q[2] < q[1]) ? 1 : 2;
	}
	int32_t r01 = R.m[0][1], r02 = R.m[0][2], r10 = R.m[1][0], r12 = R.m[1][2], r20 = R.m[2][0], r21 = R.m[2][1];
	switch (which) {
	case 0:
		if (r12 - r21 < 0) q[0] = -q[0];
		q[1] = ((r01 + r10) << 12) / (q[0] * 4);
		q[2] = ((r02 + r20) << 12) / (q[0] * 4);
		break;
	case 1:
		if (r20 - r02 < 0) q[1] = -q[1];
		q[0] = ((r01 + r10) << 12) / (q[1] * 4);
		q[2] = ((r12 + r21) << 12) / (q[1] * 4);
		break;
	case 2:
		if (r01 - r10 < 0) q[2] = -q[2];
		q[0] = ((r02 + r20) << 12) / (q[2] * 4);
		q[1] = ((r12 + r21) << 12) / (q[2] * 4);
		break;
	default:
		q[0] = ((r12 - r21) << 12) / (w * 4);
		q[1] = ((r20 - r02) << 12) / (w * 4);
		q[2] = ((r01 - r10) << 12) / (w * 4);
		break;
	}
	// rotation vector: axis * angle (radians 4.12)
	int32_t s = 0x1000 - (w * w >> 12);                // 0x426150
	s = s < 0 ? 0 : lm_fwsqrt(s) << 6;
	int32_t ang = (w == 0) ? 0x800 : lm_catan((s << 12) / w) << 1;
	int32_t k = (s != 0) ? (ang * 0x6488) / s : 0;      // 0x6488 = 2*pi*4096
	phys_scale(q, k, q);

	int32_t wb[3];
	lm_invmatvec2(B, b->angvel, wb);                   // body-frame angular velocity
	lm_invmatvec2(B, rate, rate);                      // body-frame desired rate

	for (int i = 0; i < 3; i++) {
		int32_t K       = b->inertia[i];
		int32_t tqSmall = c->tqSmall[i];
		int32_t dead    = c->rateDead[i];
		int32_t tqBig   = c->tqStrong[i];              // braking always uses the strong limits
		int32_t rateBig = c->rateStrong[i];
		int32_t tqMax   = strong ? c->tqStrong[i]   : c->tqNormal[i];
		int32_t rateMax = strong ? c->rateStrong[i] : c->rateNormal[i];
		int32_t err = q[i];
		int32_t rel = wb[i] - rate[i];
		int32_t aerr = lm_abs(err);
		int32_t o = 0, set = 1;

		if (aerr > c->angleDead) {
			int32_t stop = (rel * rel) / (rateMax * 4);
			int32_t half = err / 2;                    // toward zero (cdq; sub; sar)
			int32_t pred = (rel < 0) ? half - stop : half + stop;
			if (lm_abs(pred) < aerr) {
				// can still accelerate toward the target
				if (lm_abs(rel) > 600 && ((err > 0 && rel > 0) || (err < 0 && rel < 0))) {
					set = 0;                           // coasting fast toward it: no torque
				} else {
					int32_t u = ((err >> 1) - rel + 1) >> 1;
					int32_t au = lm_abs(u);
					if (au < rateMax)
						o = (au > dead) ? (u * K + 0x100) >> 9 : (err < 0 ? -tqSmall : tqSmall);
					else
						o = (err < 0) ? -tqMax : tqMax;
				}
			} else if ((rel > 0 && err < 0) || (rel < 0 && err > 0)) {
				// overshooting while turning away: cancel the rate
				int32_t v = -rel, av = lm_abs(v);
				if (av < rateBig)
					o = (av > dead) ? (v * K + 0x100) >> 9 : (rel > 0 ? -tqSmall : tqSmall);
				else
					o = (rel > 0) ? -tqBig : tqBig;
			} else {
				// will overshoot: brake with a = rel^2 / (2 err)
				int32_t v = (rel * rel) / (-2 * err), av = lm_abs(v);
				if (av < rateBig)
					o = (av > dead) ? (v * K + 0x100) >> 9 : (err < 0 ? -tqSmall : tqSmall);
				else
					o = (err > 0) ? -tqBig : tqBig;
			}
		} else if (rel != 0) {
			// inside the angle dead zone: damp the rate
			int32_t v = -rel, av = lm_abs(v);
			if (av < rateMax)
				o = (av > c->rateDead[i]) ? (v * K + 0x100) >> 9 : (rel > 0 ? -tqSmall : tqSmall);
			else
				o = (rel > 0) ? -tqMax : tqMax;
		} else {
			set = 0;
		}
		if (set)
			out[i] = o;
	}
	AttCtrl_Apply(out, c, isLocal);
}

// =================================================================================== sounds / HUD
// 0x420630 / 0x4206c0 / 0x4205d0: cockpit warnings, repeated every 11 s
static void lander_shield_warning_voice(void)
{
	if (CockVox(0)) {
		switch (gHudLander->shieldWarn) {
		case 1: eng_snd_chain(SND_COCKPIT_VOICE, 3, 8, 0x419, 0x40f); break;
		case 2: eng_snd_chain(SND_COCKPIT_VOICE, 4, 8, 0x404, 0x419, 0x40f); break;
		case 3: eng_snd_chain(SND_COCKPIT_VOICE, 4, 8, 0x407, 0x419, 0x40f); break;
		}
	}
	InTime0(11000, Lander_WarningVoiceCycle);
}
static void lander_fuel_warning_voice(void)
{
	if (CockVox(0)) {
		switch (gHudLander->fuelWarn) {
		case 1: eng_snd_chain(SND_COCKPIT_VOICE, 3, 8, 0x40a, 0x40f); break;
		case 2: eng_snd_chain(SND_COCKPIT_VOICE, 4, 8, 0x404, 0x40a, 0x40f); break;
		case 3: eng_snd_chain(SND_COCKPIT_VOICE, 4, 8, 0x407, 0x40a, 0x40f); break;
		}
	}
	InTime0(11000, Lander_WarningVoiceCycle);
}
void Lander_WarningVoiceCycle(void)                     // 0x4205d0
{
	Lander *L = gHudLander;
	if (L->shieldWarn == 4)
		return;
	if (L->fuelWarn == 0) {
		if (L->shieldWarn != 0)
			lander_shield_warning_voice();
		return;
	}
	if (L->shieldWarn == 0) {
		lander_fuel_warning_voice();
		return;
	}
	if (sWarnVoiceToggle)
		lander_shield_warning_voice();
	else
		lander_fuel_warning_voice();
	sWarnVoiceToggle = 1 - sWarnVoiceToggle;
}

static void lander_set_fuel_warn(int32_t level)          // 0x423450
{
	Lander *L = gHudLander;
	if (L->fuelWarn == level)
		return;
	if (L->fuelWarn == 0 && (L->shieldWarn == 0 || L->shieldWarn == 4)) {
		L->fuelWarn = level;
		Lander_WarningVoiceCycle();
	}
	L->fuelWarn = level;
}

static void lander_set_shield_warn(int32_t level)        // 0x423490
{
	Lander *L = gHudLander;
	int32_t old = L->shieldWarn;
	if (old == level)
		return;
	if (L->fuelWarn == 0 && (old == 0 || old == 4) && level != 0 && level != 4) {
		L->shieldWarn = level;
		Lander_WarningVoiceCycle();
	}
	L->shieldWarn = level;
}

// 0x423550: scrape loop, sound set by the class of the last touched surface (gContactSurfaceClass)
static void lander_set_scrape_sound(Lander *L, ObjId id, int32_t level, int32_t scrape)
{
	static const int32_t set0[8]  = {0x424, 0x425, 0x426, 0x427, 0x428, 0x429, 0x42a, 0x42b};
	static const int32_t set1[10] = {0x438, 0x439, 0x43a, 0x43b, 0x43c, 0x43d, 0x43e, 0x43f, 0x440, 0x441};
	static const int32_t set3[9]  = {0x42e, 0x42f, 0x430, 0x431, 0x432, 0x433, 0x434, 0x435, 0x436};
	if (L->sndScrape)
		eng_snd_stop(L->sndScrape);
	if (level == 0) {
		L->sndScrape = 0;
	} else if ((uint32_t)phys_g.surface_last <= 6) {
		int32_t vol = (scrape - 0x200) * 70;
		if (vol > 50000)
			vol = 50000;
		switch (phys_g.surface_last) {
		case 1:  L->sndScrape = eng_snd_loop_chain_vol(id, vol, 10, set1); break;
		case 3:  L->sndScrape = eng_snd_loop_chain_vol(id, vol, 9, set3); break;
		default: L->sndScrape = eng_snd_loop_chain_vol(id, vol, 8, set0); break;
		}
	}
	L->scrapeLevel = (int16_t)level;
}

// 0x4234d0 (only reached with x = 0: the +0x58 sound is never started in this build)
static void lander_slew_sound_pitch(Lander *L, int32_t x)
{
	float target = (float)(1.0 - (float)x * -0.006723001599311829);
	float cur = eng_snd_get_freq(L->sndPitch);
	float up = cur + 0.1f, down = cur - 0.05f;
	if (up <= target)
		eng_snd_set_freq(L->sndPitch, up);
	else if (!(down < target))
		eng_snd_set_freq(L->sndPitch, down);
}

// =================================================================================== create
// 0x420230: box hull used when the model has no 0xFACCFACE dynamics block
static void lander_setup_default_hull(phys_body_t *b, const int32_t dims[3], const int32_t pos8[3],
                                      phys_matrix_t *coord, int32_t mass)
{
	int32_t hx = dims[0] >> 1, hy = dims[1] >> 1, hz = dims[2] >> 1;
	phys_body_init(b, mass);
	int32_t yb = -((hy * 3000) >> 12);                 // feet level
	int32_t h8 = (hy << 9) >> 12;
	phys_body_add_point(b, hx, yb, 0);
	phys_body_add_point(b, 0, yb, hz);
	phys_body_add_point(b, 0, hy, hz);
	phys_body_add_point(b, hx, hy, 0);
	phys_body_add_point(b, 0, hy, -hz);
	phys_body_add_point(b, -hx, hy, 0);
	phys_body_add_point(b, -hx, yb, 0);
	phys_body_add_point(b, 0, yb, -hz);
	phys_body_add_point(b, 0, -hy, h8);
	phys_body_add_point(b, 0, hy, h8);
	int32_t m8 = mass >> 3;
	b->mass = 0;
	static const int sx[8] = {1, 0, 0, 1, 0, -1, -1, 0}, sy[8] = {-1, -1, 1, 1, 1, 1, -1, -1},
	                  sz[8] = {0, 1, 1, 0, -1, 0, 0, -1};
	for (int i = 0; i < 8; i++) {
		int32_t v[3] = {sx[i] * hx, sy[i] * hy, sz[i] * hz};
		phys_body_add_mass(b, v, m8);
	}
	phys_body_set_position(b, pos8);
	b->coord = coord;
}

void Lander_Create(LanderProc *p, const LanderCreateArgs *args)   // 0x421030
{
	int32_t pos8[3];
	int32_t player = -1;
	int32_t massAdd = 0;
	Lander *L;

	eng_task_type_set_reflective(p);                   // type flags |= 8: drawn in the Venus mirror pass
	if (args) {
		p->id = args->id;
		pos8[0] = args->record[0] << 3;
		pos8[1] = args->record[1] << 3;
		pos8[2] = args->record[2] << 3;
		player = args->param[1];
	} else {
		pos8[0] = 0;
		pos8[1] = 0x200;
		pos8[2] = 0;
		p->id = eng_task_dynamic_id(p);
	}
	if (player == -1) {
		player = eng_player_add();
		gThisPlayerId = player;
	}
	eng_player_set_object(player, p->id);
	p->flags = 0;
	L = &gLanders[player];
	p->lander = L;

	if (gMultiPlayerGame) {
		const LanderPlayerState *cfg = &gMPPlayerState[player];
		memcpy(&L->ps, cfg, 10 * sizeof(int32_t));
		if (gNetworkGameType == 1) {
			if (cfg->team) {
				L->ps.team = (int32_t)gTeamBShipColour;
				p->flags |= LPF_TEAM_B;
			} else {
				L->ps.team = (int32_t)gTeamAShipColour;
			}
		} else {
			L->ps.team = cfg->team;
		}
		p->flags |= LPF_HOMING_TARGET;
	} else if (gThisPlayerId == player) {
		memcpy(&L->ps, &gPlayerState, 10 * sizeof(int32_t));   // other SP landers keep old contents
	}

	const LanderModelDef *md = &gLanderModelTable[L->ps.landerType];
	L->player = (int16_t)player;
	L->heading = 0;
	L->stableTimer = 0;
	L->waypoint = 0;
	L->marker = -1;
	L->scanTimer = 1;
	L->altSide = 0;
	L->scrapeLevel = 0;
	L->sndScrape = 0;
	L->damaged = 0;
	L->ctrlWord = 0;
	L->sndEngine = 0;
	L->sndPitch = 0;
	L->scrapeCount = 0;
	L->scrape = 0;
	L->scrapeSmooth = 0;
	L->hookTarget = OBJ_NONE;
	L->sndTractor = 0;
	L->beamCoord = eng_coord_alloc();
	lm_identity(&L->attitude);
	lm_identity(&L->world);
	memcpy(L->prevPos, L->world.t, sizeof L->world.t);
	lm_mul_rot_y(&L->attitude, &L->display, -0x200);

	switch (L->ps.armour) {                            // jump table 0x4218d4
	case 0:  massAdd = 0;     L->armourValue = 80;  L->armourShift0 = 0; L->armourShift1 = 0; break;
	case 1:  massAdd = 0x300; L->armourValue = 200; L->armourShift0 = 0; L->armourShift1 = 0; break; // Standard (A)
	case 2:  massAdd = 0x300; L->armourValue = 150; L->armourShift0 = 0; L->armourShift1 = 0; break; // Standard (B)
	case 3:  massAdd = 0x300; L->armourValue = 100; L->armourShift0 = 0; L->armourShift1 = 0; break; // Standard (C)
	case 4:  massAdd = 0x100; L->armourValue = 200; L->armourShift0 = 0; L->armourShift1 = 0; break; // Light (A)
	case 5:  massAdd = 0x100; L->armourValue = 150; L->armourShift0 = 0; L->armourShift1 = 0; break; // Light (B)
	case 6:  massAdd = 0x100; L->armourValue = 100; L->armourShift0 = 0; L->armourShift1 = 0; break; // Light (C)
	case 7:  massAdd = 0x300; L->armourValue = 150; L->armourShift0 = 1; L->armourShift1 = 0; break; // Reflective (B)
	case 8:  massAdd = 0x300; L->armourValue = 150; L->armourShift0 = 0; L->armourShift1 = 1; break; // Reactive (B)
	case 9:  massAdd = 0x100; L->armourValue = 200; L->armourShift0 = 1; L->armourShift1 = 0; break; // Light Reflective (A)
	case 10: massAdd = 0x100; L->armourValue = 200; L->armourShift0 = 0; L->armourShift1 = 1; break; // Light Reactive (A)
	default: break;                                    // out of range: fields untouched, massAdd 0
	}

	p->coord = eng_coord_alloc();
	lander_create_model(p, L);

	// patch the global jet template with this lander's dimensions, then copy it (0x427120)
	{
		int16_t r = md->jetReach, h = (int16_t)((uint16_t)md->jetReach >> 1);
		int16_t y0 = md->jetY[0], y1 = md->jetY[1];
		sJetTemplate[0].pos[2] = r;   sJetTemplate[1].pos[2] = (int16_t)-r;
		sJetTemplate[2].pos[0] = (int16_t)-r; sJetTemplate[3].pos[0] = r;
		sJetTemplate[4].pos[0] = sJetTemplate[4].pos[2] = (int16_t)-h;
		sJetTemplate[5].pos[0] = sJetTemplate[5].pos[2] = h;
		sJetTemplate[6].pos[0] = (int16_t)-h; sJetTemplate[6].pos[2] = h;
		sJetTemplate[7].pos[0] = h;   sJetTemplate[7].pos[2] = (int16_t)-h;
		for (int j = 4; j < 8; j++)
			sJetTemplate[j].size = (uint16_t)md->jetSize;
		for (int j = 0; j < 4; j++)
			sJetTemplate[j].liftY = 0;
		sJetTemplate[0].pos[1] = sJetTemplate[3].pos[1] = y0;
		sJetTemplate[1].pos[1] = sJetTemplate[2].pos[1] = y1;
		sJetTemplate[4].pos[1] = y1;
		sJetTemplate[5].pos[1] = y0;
		sJetTemplate[4].liftY = sJetTemplate[5].liftY = 0;
		sJetTemplate[6].pos[1] = sJetTemplate[7].pos[1] = y0;
		sJetTemplate[6].liftY = sJetTemplate[7].liftY = (int16_t)(y1 - y0);
	}
	L->engineFx = sEngineFxTemplate;
	L->engineFx.colour[0] = md->flameColour[0];
	L->engineFx.colour[1] = md->flameColour[1];
	eng_fx_engine_light_create(&L->engineFx, p->coord);
	memcpy(L->att.jets, sJetTemplate, sizeof sJetTemplate);

	phys_body_t *b = gLoadingGameState ? &phys_bodies[args->param[0]] : phys_body_alloc();
	L->body = b;
	phys_body_init(b, 0x1000);
	if (phys_body_setup_dynamics(b, eng_model_inst_model(p->modelInst), 1, massAdd + md->mass) != 0) {
		lander_setup_default_hull(b, sDefaultHullDims, pos8, eng_coord_matrix(p->coord), massAdd + md->mass);
	} else {
		b->coord = eng_coord_matrix(p->coord);
		if (!gLoadingGameState)
			phys_body_set_position(b, pos8);
	}
	b->owner = (int32_t)p->id;
	eng_forcebox_add_body(p->id, 0x140);
	b->on_contact = Lander_OnContact;
	if (!gLoadingGameState && args) {
		eng_rot_pack_to_matrix(args->record, (FwMatrix *)&b->state[0].mat);
		eng_rot_pack_to_matrix(args->record, (FwMatrix *)&b->state[1].mat);
		L->heading = lm_ratan2(b->cur->mat.m[2], b->cur->mat.m[8]);   // atan2(m02, m22)
	}

	// attitude controller (0x421746)
	LanderAttCtrl *c = &L->att;
	c->angleDead = 2;
	c->body = b;
	for (int i = 0; i < 3; i++) {
		int32_t g = md->attGain, I = b->inertia[i];
		c->tqStrong[i]   = ((g << 1) * I) >> 9;
		c->tqSmall[i]    = (I << 1) >> 9;
		c->tqNormal[i]   = (I * g) >> 9;
		c->rateStrong[i] = g << 1;
		c->rateDead[i]   = 2;
		c->rateNormal[i] = g;
	}
	if (!gLoadingGameState)
		lander_sync_coord(p, 0);
	L->world = *eng_coord_world(p->modelCoord);      // model frame
	memcpy(L->display.t, L->world.t, sizeof L->world.t);
	memcpy(L->prevPos, L->world.t, sizeof L->world.t);
	L->headlight = NULL;
	lander_init_weapons(p, L);
	if (gThisPlayerId == player) {
		p->flags |= LPF_CAMERA_KEYS;                   // 0x420b90
		eng_hud_create(p);                             // 0x420770 (sets LPF_HUD, gThisPlayer = id)
		p->flags |= LPF_SHADOW;                        // 0x420b50
		eng_shadow_create();
		sFFCooldown = 0;
	}
	if (gMultiPlayerGame) {
		const uint32_t *col = &gShipColours[L->ps.team];
		eng_weapons_tint_mount(&L->weapons, 0, col);   // 0x427980
		eng_weapons_tint_mount(&L->weapons, 1, col);
		if (gThisPlayer != (int32_t)p->id)
			L->marker = eng_hud_marker_add((int32_t)p->id, *col & 0xffffff, 0);
	}
}

// 0x420f90: wake the first sleeping Lander mob for a (network) player
ObjId Lander_SpawnPlayer(int32_t player)
{
	int mob = eng_map_find_sleeping_mob(OT_LANDER);
	if (mob < 0)
		return OBJ_NONE;
	ObjId id = MOB_ID(OT_LANDER, mob);
	eng_map_set_mob_param_hi(id, (int16_t)player);
	MobObj_Wake(mob);
	LanderEvent ev = {EV_LANDER_SPAWNED, {(intptr_t)id}};
	Event_Raise(&ev);
	return id;
}

// =================================================================================== update
static void lander_respawn(LanderProc *p, Lander *L)     // 0x421a67 (multiplayer)
{
	L->ps.shield = 0xff;
	L->ps.fuel = L->ps.maxFuel;
	for (int i = 0; i < L->weapons.count; i++) {
		LanderWeaponSlot *s = &L->weapons.slot[i];
		int energy = gWeaponDefs[s->type].flags & LWF_ENERGY;
		s->level = 0;
		if (!energy && i == 1) {
			switch (L->ps.secondary) {
			case 15: s->level = 5; break;
			case 16: s->level = 10; break;
			case 17: s->level = 20; break;
			}
		}
	}
	LanderEvent ev = {EV_LANDER_SPAWNED, {(intptr_t)p->id}};
	Event_Raise(&ev);
	if (L->damaged) {
		lander_release_model(p, L);
		L->damaged = 0;
		lander_create_model(p, L);
		lander_init_weapons(p, L);
	}
}

static void lander_wreck_model(LanderProc *p, Lander *L, int killEngineLight)
{
	weapons_release_mounts(&L->weapons);               // with gGlobalModels selected
	if (L->headlight)
		lander_headlight_off(L);
	if (killEngineLight)
		eng_fx_engine_light_destroy(&L->engineFx);
	lander_release_model(p, L);
	L->damaged = 1;
	lander_create_model(p, L);
}

// engine wash under the main engine (0x42224a)
static void lander_engine_wash(LanderProc *p, Lander *L, const phys_state_t *st, const int32_t up[3], int32_t thrust)
{
	phys_hit_t hit;
	int32_t d[3], end[3];
	L->dustTimer = 2;
	eng_world_set_query(3, OBJ_NONE);
	phys_scale(up, -0x400, d);
	end[0] = st->mat.t[0] + d[0];
	end[1] = st->mat.t[1] + d[1];
	end[2] = st->mat.t[2] + d[2];
	if (phys_segment_hit(st->mat.t, end, 6, &hit)) {
		int cls = eng_surface_class(hit.surface);
		if (cls == 4 && thrust > 0x80) {              // liquid: splash, stronger when closer
			int32_t k = ((hit.point[1] - st->mat.t[1] + 0x400) << 5) >> 8;
			if (k < 0)
				k = 0;
			eng_particle_spawn(3, hit.point, 0);
			eng_particle_spawn(4, hit.point, k);
		} else if ((cls == 2 || cls == 5) && thrust > 0x80) {   // dust / snow spray to both sides
			const FwMatrix *wm = eng_coord_world(p->modelCoord);
			int32_t a[3] = {wm->m[0][0], 0, wm->m[2][0]};
			int32_t bv[3] = {-wm->m[0][1], 0, -wm->m[2][1]};
			int32_t s[3];
			phys_normalise(a);
			phys_scale(a, 0x10, a);
			phys_scale(bv, 0x10, bv);
			gParticleColour = (cls != 2 ? 0xf9ede4u : 0u) + 0x61216u;   // 0x061216 dark / 0xfffffa white
			s[0] = bv[0] + a[0]; s[2] = bv[2] + a[2];
			eng_particle_spawn(6, hit.point, ((s[2] & 0xff) << 8) | (s[0] & 0xff));
			a[0] = -a[0]; a[2] = -a[2];
			s[0] = bv[0] + a[0]; s[2] = bv[2] + a[2];
			eng_particle_spawn(6, hit.point, ((s[2] & 0xff) << 8) | (s[0] & 0xff));
		}
	}
	eng_world_set_query(-1, OBJ_NONE);
}

void Lander_Update(LanderProc *p)                        // 0x421900
{
	Lander *L = p->lander;
	phys_body_t *b = L->body;
	phys_state_t *st = b->cur;                         // [esp+0x30]
	const LanderModelDef *md = &gLanderModelTable[L->ps.landerType];
	const int32_t fuelAtStart = L->ps.fuel;            // [esp+0x98]
	const uint32_t pf = p->flags;                      // [esp+0x7c] snapshot
	const ObjId id = p->id;
	int32_t F[3] = {0, 0, 0};                          // [esp+0x5c] force for this tick
	int32_t rate[3] = {0, 0, 0};
	int32_t thrust;
	uint32_t cw = eng_ctrl_read_word(L->player);

	// ---- control locks
	if (pf & LPF_LOCK_PITCH)    cw &= 0xFFFFFFC0u;
	if (pf & LPF_LOCK_YAW)      cw &= 0xFFFFF03Fu;
	if (pf & LPF_LOCK_ROLL)     cw &= 0xFFFC0FFFu;
	if (pf & LPF_LOCK_THROTTLE) cw &= 0xFF03FFFFu;
	if (pf & LPF_LOCK_BUTTONS)  cw &= 0x00FFFFFFu;

	// ---- dead or out of fuel
	if (L->ps.shield == 0 || L->ps.fuel == 0) {
		if (gMultiPlayerGame) {
			if (L->ps.shield == 0 && L->damaged == 0)
				lander_wreck_model(p, L, 0);
			if (L->respawnTimer)
				L->respawnTimer--;
			if (L->respawnTimer == 0 && (cw & LCW_FIRE))
				lander_respawn(p, L);
		} else {
			if (L->ps.shield == 0) {
				if (L->damaged == 0)
					lander_wreck_model(p, L, 1);
				eng_mission_game_over(1);             // the dead message
			} else {
				eng_mission_game_over(0);             // the out-of-fuel message
			}
		}
		// keep throttle and the camera keys; yaw field = 16: the heading (and the chase camera)
		// keeps turning slowly around the wreck
		cw = (cw & 0x48FC0000u) | 0x400u;
	}
	if (!(L->hookTarget & 0x80000000u))               // a hooked pod: tractor button ignored
		cw &= ~LCW_TRACTOR;
	L->ctrlWord = cw;

	// ---- buttons
	if ((cw & LCW_RECOVER) && gThisPlayer == (int32_t)id && gMouseStickPresent)
		eng_input_reset_mouse_stick();

	if ((cw & LCW_TRACTOR) && !(p->flags & LPF_TRACTOR_LATCH)) {
		if (L->tractorTime == 0 && L->sndTractor == 0) {
			eng_snd_once(id, 0x1e);
			L->sndTractor = eng_snd_loop(id, 0x1f, -1);
		}
		L->tractorTime += 40;
		if (L->hookTarget == OBJ_NONE) {              // beam on nothing: 0x400 fuel per tick
			L->ps.fuel -= 0x400;
			if (L->ps.fuel < 0)
				L->ps.fuel = 0;
		}
	} else {
		if (L->sndTractor) {
			eng_snd_stop(L->sndTractor);
			L->sndTractor = 0;
		}
		L->tractorTime = 0;
		if (cw & LCW_TRACTOR) {                       // still held since the latch was set
			cw &= ~LCW_TRACTOR;
			L->ctrlWord = cw;
		} else {
			p->flags &= ~LPF_TRACTOR_LATCH;
		}
	}

	if (p->flags & LPF_CAMERA_KEYS) {
		if (cw & LCW_CAM_UP) {
			int32_t a = AutoCam_GetUserAngle(gCameraObject);
			if (a)
				AutoCam_SetUserAngle(gCameraObject, a - 1);
		}
		if (cw & LCW_CAM_DOWN) {
			int32_t a = AutoCam_GetUserAngle(gCameraObject) + 1;
			if (a != 5)
				AutoCam_SetUserAngle(gCameraObject, a);
		}
	}

	// ---- heading (yaw input turns the control frame)
	if (cw & LCW_YAW_MASK) {
		L->heading = (L->heading + ((LCW_S6(cw, LCW_YAW_SHIFT) * (int32_t)md->yawRate + 0x10) >> 5)) & 0xfff;
	} else if (gAutoHeading) {
		int32_t vx = b->vel[0], vz = b->vel[2];
		int32_t sp = lm_fwsqrt(lm_abs(vx) * lm_abs(vx) + lm_abs(vz) * lm_abs(vz));
		if (sp > 500) {
			int32_t a = (lm_ratan2(vx, vz) - 0x200) & 0xfff;
			if (sp < 2) {                              // dead branch in the original
				int32_t d = lm_angle_diff(a, L->heading) * (sp - 500) / 500;
				L->heading = (L->heading - d) & 0xfff;
			} else {
				L->heading = a;
			}
		}
	}

	// ---- commanded attitude: stick tilt (45 deg mixed), heading, inertial frame
	{
		int32_t R = LCW_S6(cw, LCW_ROLL_SHIFT) << 7;
		int32_t P = LCW_S6(cw, LCW_PITCH_SHIFT) << 7;
		int32_t sum = P + R, dif = P - R;
		int32_t v[3];
		v[0] = (dif * 2896) >> 12;
		v[1] = 0;
		v[2] = (-sum * 2896) >> 12;
		int32_t r2 = v[0] * v[0] + v[2] * v[2];
		lm_identity(&L->attitude);
		if (r2 != 0) {
			int32_t r = lm_fwsqrt(r2);
			v[0] = (v[0] << 12) / r;
			v[2] = (v[2] << 12) / r;
			if (r > 0x1000)
				r = 0x1000;
			lm_rotate_axis(&L->attitude, &L->attitude, (r * gMaxTiltAngle) >> 12, v);
		}
		int32_t s = lm_rsin(L->heading), c = lm_rcos(L->heading);
		for (int j = 0; j < 3; j++)
			lm_rotate_pair(s, c, &L->attitude.m[0][j], &L->attitude.m[2][j]);
		if (gUseInertialFrame)
			lm_mul_matrix0(&gInertialFrame, &L->attitude, &L->attitude);
		lm_mul_rot_y(&L->attitude, &L->display, -0x200);
	}

	// ---- landing stabiliser: no attitude control for gStableCutOffTime ticks after a contact
	if (b->impulse_sum != 0 && !(pf & LPF_LOCK_YAW))
		L->stableTimer = (int16_t)gStableCutOffTime;
	if (cw & LCW_THROTTLE_MASK)
		L->stableTimer = 0;

	{
		int isLocal = lander_is_local(p);
		if (L->ps.fuel != 0 && L->ps.shield != 0) {
			if (cw & LCW_RECOVER) {
				AttCtrl_Run(&L->att, &L->attitude, rate, 1, isLocal);
			} else if (L->stableTimer == 0) {
				rate[1] = (-25736 * LCW_S6(cw, LCW_YAW_SHIFT)) >> 12;   // 2*pi*4096 per yaw unit
				AttCtrl_Run(&L->att, &L->attitude, rate, 0, isLocal);
			} else {
				AttCtrl_Apply(rate, &L->att, isLocal);
				L->stableTimer--;
			}
		} else {
			AttCtrl_Apply(rate, &L->att, isLocal);
		}
	}

	// ---- main engine
	{
		int32_t t = (int32_t)((cw >> LCW_THROTTLE_SHIFT) & 0x3f);
		L->engineFx.level = (t << 2) << 4;
		thrust = ((int32_t)md->thrust * (t << 2)) >> 12;
		if (L->ps.engine >= 6 && L->ps.engine <= 8)   // Military engines
			thrust += thrust >> 1;
	}

	if (L->dustTimer == 0 && L->liquidContact != 0) { // touched liquid: splash under the ship
		phys_hit_t hit;
		int32_t a[3] = {st->mat.t[0], st->mat.t[1] + 0x200, st->mat.t[2]};
		int32_t z[3] = {st->mat.t[0], st->mat.t[1] - 0x200, st->mat.t[2]};
		eng_world_set_query(-1, id);
		if (phys_segment_hit(a, z, 2, &hit) && eng_surface_class(hit.surface) == 4) {
			eng_particle_spawn(4, hit.point, 0xc8);
			L->dustTimer = 4;
		}
		eng_world_set_query(-1, OBJ_NONE);
	}

	if (L->ps.fuel != 0 && thrust != 0 && L->ps.shield != 0) {
		int32_t vol = (thrust & 0xff) << 8;            // mov ch,bl: low byte only
		if (L->sndEngine == 0 && (uint32_t)L->ps.engine <= 11) {
			int32_t k = L->ps.engine % 3;              // jump table 0x4233b4
			eng_snd_once(id, 0x0d + k);
			L->sndEngine = eng_snd_loop_vol(id, 0x10 + k, -1, vol);
		}
		if (L->sndEngine)
			eng_snd_set_volume(L->sndEngine, vol);
		int32_t up[3] = {st->mat.m[1], st->mat.m[4], st->mat.m[7]};   // body Y axis
		phys_scale(up, (thrust * gMainThrottlePower << 3) >> 8, F);
		if (L->dustTimer == 0)
			lander_engine_wash(p, L, st, up, thrust);
	} else {
		if (L->sndEngine) {
			eng_snd_stop(L->sndEngine);
			L->sndEngine = 0;
		}
		if (L->sndPitch) {
			lander_slew_sound_pitch(L, 0);
			if (eng_snd_get_freq(L->sndPitch) <= 1.0500000007450581) {
				eng_snd_stop(L->sndPitch);
				L->sndPitch = 0;
			}
		}
		L->engineFx.level = 0;
		F[0] = F[1] = F[2] = 0;
	}
	if (L->dustTimer)
		L->dustTimer--;
	L->liquidContact = 0;

	// ---- fuel
	if (!gCheatInfiniteFuel && L->ps.shield != 0) {
		int32_t use = thrust;
		if ((uint32_t)L->ps.engine <= 11) {           // jump table 0x4233fc
			if (L->ps.engine % 3 == 1) use -= use >> 2;   // Lean: 75 %
			else if (L->ps.engine % 3 == 2) use -= use >> 1;   // Super Lean: 50 %
		}
		L->ps.fuel -= use;
		if (L->ps.fuel < 0)
			L->ps.fuel = 0;
	}

	// ---- HUD warnings (HUD owner only)
	if (p->flags & LPF_HUD) {
		gHudLander = L;
		int32_t f = L->ps.fuel, level = 0;
		if (f != 0) {
			float mf = (float)L->ps.maxFuel;
			if (f < lm_ftol(lm_pc((double)mf * 0.05))) level = 3;
			else if (f < lm_ftol(lm_pc((double)mf * 0.1))) level = 2;
			else if (f < lm_ftol(lm_pc((double)mf * 0.15))) level = 1;
			else {
				int32_t t25 = lm_ftol(lm_pc((double)mf * 0.25));
				int32_t t50 = lm_ftol(lm_pc((double)mf * 0.5));
				int32_t t75 = lm_ftol(lm_pc((double)mf * 0.75));
				int32_t pc = 0;
				if (fuelAtStart > t25 && f < t25) pc = 0x3ff;           // "25 %"
				else if (fuelAtStart > t50 && f < t50) pc = 0x400;      // "50 %"
				else if (fuelAtStart > t75 && f < t75) pc = 0x401;      // "75 %"
				if (pc && CockVox(0))
					eng_snd_chain(SND_COCKPIT_VOICE, 3, 8, 0x40a, pc);
			}
		}
		lander_set_fuel_warn(level);
		int32_t sh = L->ps.shield;
		if (sh == 0) {
			if (L->shieldWarn != 4)
				eng_snd_once_excl(id, 0x14);
			level = 4;
		} else {
			level = sh < 12 ? 3 : sh < 25 ? 2 : sh < 38 ? 1 : 0;
		}
		lander_set_shield_warn(level);
	}

	// ---- forces: thrust + gravity*mass*8 - velocity*air*8
	{
		int32_t g[3], d[3];
		phys_scale(phys_g.gravity, b->mass << 3, g);
		F[0] += g[0]; F[1] += g[1]; F[2] += g[2];
		phys_scale(b->vel, (gAirResistance << 12) >> 9, d);
		F[0] -= d[0]; F[1] -= d[1]; F[2] -= d[2];
		phys_body_add_force(b, F, 1, st->pos);
	}

	// ---- training missions step the physics here, right after the forces (PhysTask is idle)
	if (gTrainingMission) {
		int32_t keep[3] = {0, 0, 0};
		if (pf & (LPF_PIN_X | LPF_PIN_Y | LPF_PIN_Z))
			memcpy(keep, b->cur->pos, sizeof keep);
		phys_g.step_start = gGameUpdateTime;
		phys_g.step_end = gGameUpdateTime + p->period;
		phys_g.in_physics = 1;
		phys_g.interp = 0;
		phys_step();
		phys_g.in_physics = 0;
		phys_g.interp = 0;
		if (pf & LPF_PIN_X) { b->cur->pos[0] = keep[0]; b->vel[0] = 0; }
		if (pf & LPF_PIN_Y) { b->cur->pos[1] = keep[1]; b->vel[1] = 0; }
		if (pf & LPF_PIN_Z) { b->cur->pos[2] = keep[2]; b->vel[2] = 0; }
	}

	lander_sync_coord(p, 0);
	memcpy(L->prevPos, L->world.t, sizeof L->world.t);
	L->world = *eng_coord_world(p->modelCoord);      // model frame
	memcpy(L->display.t, L->world.t, sizeof L->world.t);

	if (L->ps.shield == 0) {                           // burning wreck
		int32_t pos[3];
		pos[0] = L->world.t[0] + (eng_rand() & 0xff) - 0x7f;
		pos[1] = L->world.t[1] + (eng_rand() & 0xff) - 0x7f;
		pos[2] = L->world.t[2] + (eng_rand() & 0xff) - 0x7f;
		eng_particle_spawn(7, pos, 0);
	}

	// ---- impact damage (sum of normal impulses this step)
	if (b->impulse_sum > 0x600 && L->ps.shield != 0) {
		int32_t a = ((int32_t)((uint32_t)b->impulse_sum << 12) / b->mass - 0x600) >> 1;
		a = (a * 341) / (L->armourValue << 8);         // can be negative for heavy ships (heals)
		int32_t s = L->ps.shield - a;
		if (s < 0)
			s = 0;
		Lander_SetShield(id, s);
	}

	// ---- scrape (friction) damage
	if (L->scrape != 0) {
		if (L->scrapeCount != 0) {
			L->scrape /= L->scrapeCount;
			if (L->scrape > 1500)
				L->scrape = 1500;
		} else {
			L->scrape = 0;
		}
	}
	L->scrape = L->scrapeSmooth = (L->scrapeSmooth * 3 + L->scrape) >> 2;
	if (L->scrape > 0x40 && L->ps.shield != 0) {
		int32_t d = ((L->scrape - 0x40) * L->ps.shield * 1228) / ((L->armourValue * 255) << 12);
		if (d > 2)
			d = 2;
		int32_t s = L->ps.shield - d;
		if (s < 0)
			s = 0;
		Lander_SetShield(id, s);
	}

	// ---- collision sound by surface class
	if (b->impulse_sum > 0x100 && (uint32_t)phys_g.surface_last <= 6) {
		static const int32_t base[7] = {0x3e9, 0x47, 0x3d5, 0x5b, 0x52, 0x65, 0x3df};
		int32_t imp = b->impulse_sum, vol = (imp - 0x100) * 20;
		if (vol > 0xffff)
			vol = 0xffff;
		int32_t sid = base[phys_g.surface_last];
		if (imp > 600)
			sid += (phys_g.surface_last == 4) ? 5 : 4;   // hard impact variant
		eng_snd_once_vol(id, sid, vol);
	}

	// ---- scrape sound
	if (L->scrape != 0) {
		L->scrapeHold = 5;
		if (eng_snd_enabled()) {
			int32_t s = L->scrape;
			switch (L->scrapeLevel) {
			case 0:
				if (s > 0x7ffffff) lander_set_scrape_sound(L, id, 2, s);
				else if (s > 0x200) lander_set_scrape_sound(L, id, 1, s);
				break;
			case 1:
				if (s > 0x7ffffff) lander_set_scrape_sound(L, id, 2, s);
				else if (s < 0x200) lander_set_scrape_sound(L, id, 0, s);
				else if (L->sndScrape) eng_snd_set_volume(L->sndScrape, s * 70 > 50000 ? 50000 : s * 70);
				break;
			case 2:
				if (s < 0x200) lander_set_scrape_sound(L, id, 0, s);
				else if (s < 0x7ffffff) lander_set_scrape_sound(L, id, 1, s);
				else if (L->sndScrape) eng_snd_set_volume(L->sndScrape, s * 70 > 50000 ? 50000 : s * 70);
				break;
			}
		}
	} else if (L->scrapeLevel != 0) {
		if (--L->scrapeHold == 0)
			lander_set_scrape_sound(L, id, 0, 0);
	}
	L->scrapeCount = 0;
	L->scrape = 0;

	// ---- weapons
	LanderWeapons *w = &L->weapons;
	if (w->count != 0) {
		LanderWeaponSlot *slot = &w->slot[w->current];
		const LanderWeaponDef *def = &gWeaponDefs[slot->type];
		if (!(def->flags & LWF_ENERGY) && slot->level == 0) {
			L->scanTimer = 1;
			L->targetList[0] = -1;
			L->target = OBJ_NONE;
		} else {
			if (--L->scanTimer == 0) {
				eng_targets_scan((def->flags & LWF_HOMING) ? 0 : 1, eng_coord_world(p->modelCoord), L->targetList, id);
				L->scanTimer = 5;
			}
			lander_pick_target(L->targetList, &L->target, (def->flags & LWF_HOMING) ? 1 : 0, id);
		}
		if (p->flags & LPF_HUD)
			gLanderHud.lockIndicator = (L->target != OBJ_NONE);

		if (cw & LCW_FIRE) {
			int32_t vel[3] = {0, 0, 0};
			int fire = 1;
			if (!(def->flags & LWF_ENERGY)) {
				if (slot->level == 0 || (def->kind == 2 && L->target == OBJ_NONE)) {
					fire = 0;                          // empty, or a missile without a lock
				} else {
					vel[0] = b->vel[0] >> 3;
					vel[1] = b->vel[1] >> 3;
					vel[2] = b->vel[2] >> 3;
					slot->level--;
				}
			} else {
				int32_t heat = (int32_t)def->heatPerShot;
				if (gMultiPlayerGame)
					heat >>= 2;
				if (heat + slot->level > LW_HEAT_MAX) {
					if (w->overheated) {
						fire = 0;
					} else {
						w->overheated = 1;
						if (L->player == gThisPlayerId && CockVox(0))
							eng_snd_chain(SND_COCKPIT_VOICE, 2, 0x412, 0x411);
					}
				}
				if (fire && w->overheated)
					fire = 0;
				if (fire) {
					slot->level = (int16_t)(slot->level + heat);
					if (slot->level > LW_HEAT_MAX)
						slot->level = LW_HEAT_MAX;
				}
			}
			if (fire) {
				LanderShotParams sp;                   // projectile create params ([esp+0x9c])
				memset(&sp, 0, sizeof sp);
				sp.kind = def->kind;
				sp.f04 = 0x100;
				sp.f08 = 0;
				sp.world = eng_coord_world(p->modelCoord);      // model frame (body x Ry(45))
				sp.target = L->target;
				sp.owner = id;
				sp.arg2 = def->arg2;
				sp.arg3 = def->arg3;
				sp.vel[0] = vel[0]; sp.vel[1] = vel[1]; sp.vel[2] = vel[2];
				const LanderWeaponMount *m = &w->mount[w->current];
				for (int k = 0; k < 3; k++)            // s16 -> s32 (movsx, 0x422f0f)
					sp.muzzle[k] = m->muzzle[L->altSide][k];
				eng_fire_projectile(&sp);              // Objects_Create(2, {?, ?, &sp})
				if (m->nModels > 1) {
					if (md->fireMode == 2) {           // both hardpoints at once
						for (int k = 0; k < 3; k++)
							sp.muzzle[k] = m->muzzle[L->altSide == 0][k];
						gProjectileSecondShot = 1;
						eng_fire_projectile(&sp);
						gProjectileSecondShot = 0;
					}
					if (md->fireMode == 1)             // alternate
						L->altSide = (int16_t)(L->altSide == 0);
				}
				LanderEvent ev = {EV_LANDER_FIRED, {(intptr_t)id, (intptr_t)(int8_t)def->kind, (intptr_t)L->target}};
				Event_Raise(&ev);
				slot->cooldown = 0x16;
				if (gThisPlayer == (int32_t)id && gFFDevicePresent) {
					if (sFFCooldown == 0) {
						eng_input_ff_fire();
						sFFCooldown = 10;
					} else {
						sFFCooldown--;
					}
				}
			}
		} else if (gThisPlayer == (int32_t)id && gFFDevicePresent && sFFCooldown) {
			sFFCooldown--;
		}

		// heat decay of every energy slot
		for (int i = 0; i < w->count; i++) {
			LanderWeaponSlot *s = &w->slot[i];
			const LanderWeaponDef *d = &gWeaponDefs[s->type];
			if ((d->flags & LWF_ENERGY) && s->level != 0) {
				if (s->cooldown != 0) {
					s->cooldown--;
				} else {
					s->level = (int16_t)(s->level - (int16_t)d->coolRate);
					if (s->level < 0)
						s->level = 0;
				}
			}
		}
		if ((def->flags & LWF_ENERGY) && (uint32_t)(def->heatPerShot + slot->level) <= LW_HEAT_MAX)
			w->overheated = 0;
		if (cw & LCW_WEAPON)
			weapons_select_next(w, (p->flags & LPF_HUD) != 0);
	}

	// ---- waypoints (single player)
	if (!gMultiPlayerGame && gNbWaypoints) {
		if (cw & LCW_WAYPOINT) {
			if (++L->waypoint >= gNbWaypoints)
				L->waypoint = 0;
			if (p->flags & LPF_HUD) {
				if (L->marker != -1)
					eng_hud_marker_remove(L->marker);
				L->marker = eng_hud_marker_add(L->waypoint, 0xff, 1);
			}
		}
		if (L->marker == -1)
			L->marker = eng_hud_marker_add(L->waypoint, 0xff, 1);
	}

	// ---- HUD outputs
	if (p->flags & LPF_HUD) {
		LanderHud *h = &gLanderHud;
		h->stickRoll = LCW_S6(cw, LCW_ROLL_SHIFT) + 32;
		h->stickPitch = LCW_S6(cw, LCW_PITCH_SHIFT) + 32;
		if (!gMultiPlayerGame) {
			int32_t brg;
			if (gWaypoints) {
				if ((cw & LCW_WAYPOINT) && CockVox(1)) {
					if (L->waypoint < 3)
						eng_snd_chain(SND_COCKPIT_VOICE, 3, 0x41f, 0x3fc + L->waypoint, 0x418);
					else
						eng_snd_chain(SND_COCKPIT_VOICE, 2, 0x41f, 0x418);
				}
				h->waypointChar = (char)('1' + L->waypoint);
				const Waypoint *wp = &gWaypoints[L->waypoint];
				brg = lm_ratan2(wp->x - st->mat.t[0], st->mat.t[2] - wp->z);
			} else {
				if ((cw & LCW_WAYPOINT) && CockVox(1))
					eng_snd_chain(SND_COCKPIT_VOICE, 2, 0x41f, 0x41d);
				h->waypointChar = '-';
				brg = lm_ratan2(-st->mat.t[0], st->mat.t[2]);
			}
			h->waypointBearing = (brg + L->heading - 0x600) & 0xfff;
		}
		h->compass = (L->heading - 0x600) & 0xfff;
		h->horizSpeed = lm_fwsqrt(b->vel[0] * b->vel[0] + b->vel[2] * b->vel[2]);
		h->vertSpeed = b->vel[1] > 0x800 ? 0x800 : b->vel[1] < -0x800 ? -0x800 : b->vel[1];
		if (w->count) {
			const LanderWeaponSlot *s = &w->slot[w->current];
			const LanderWeaponDef *d = &gWeaponDefs[s->type];
			h->weapon = d;
			h->weaponLevel = (d->flags & LWF_ENERGY) ? (s->level * 112) / 16384 : s->level;
			h->target = L->target;
		}
	}
}

// =================================================================================== render
void Lander_Render(LanderProc *p, int32_t t)             // 0x4236f0
{
	Lander *L = p->lander;
	const int32_t *pos = eng_coord_local(p->coord)->t;  // coord +0x20

	if (gMirrorProbe && !gReflectionPass) {
		// pre-reflection probe: is the ship above a liquid (class 4) surface?
		phys_hit_t hit;
		int32_t z[3] = {pos[0], pos[1] - 0x1000, pos[2]};
		eng_world_set_query(3, OBJ_NONE);
		gLanderAboveLiquid = 0;
		if (phys_segment_hit(pos, z, 6, &hit) && eng_surface_class(hit.surface) == 4)
			gLanderAboveLiquid = 1;
		eng_world_set_query(-1, OBJ_NONE);
	}
	if (!gReflectionPass) {
		uint32_t frac = ((uint32_t)(t - p->wakeTime + p->period) << 12) / (uint32_t)p->period;
		lander_sync_coord(p, (int32_t)frac);
		L->renderFrac = (int32_t)frac;
		if (p->flags & LPF_HUD)
			eng_hud_attitude_items(L, p);            // attitude ball from L->display, camera axes
		if (p->flags & LPF_SHADOW) {
			if (gOutsideFactor == 0) {               // indoors: temporary white light straight down
				int32_t down[3] = {0, -0x1000, 0};
				void *lt = eng_light_create(3, NULL);
				eng_light_direction(lt, down);
				eng_light_colour(lt, 0xff, 0xff, 0xff);
				eng_light_set_flag10(lt);
				eng_shadow_render(p->modelCoord);
				eng_light_destroy(lt);
			} else {
				eng_shadow_render(p->modelCoord);
			}
			eng_world_set_query(-1, OBJ_NONE);
		}
	}
	if ((L->ctrlWord & LCW_TRACTOR) && L->hookTarget != OBJ_NONE) {
		const int32_t *tp = (const int32_t *)Object_GetProperty(L->hookTarget & 0x7fffffff, PROP_POSITION);
		const FwMatrix *bm = eng_coord_local(L->beamCoord);
		int32_t d[3] = {tp[0] - pos[0] - bm->m[0][1], tp[1] - pos[1] - bm->m[1][1], tp[2] - pos[2] - bm->m[2][1]};
		phys_normalise(d);
		eng_fx_beam_orient(L->beamCoord, d);           // 0x44ca20
	}
	eng_model_queue(p->modelInst);                     // gGlobalModels
	if ((!gReflectionPass || gLanderAboveLiquid) && L->damaged == 0)
		weapons_queue_mounts(&L->weapons);
}

// render item draw callback 0x425980 (called by the render list for the lander model)
int Lander_DrawCallback(LanderProc *p)
{
	Lander *L = p->lander;
	if (gReflectionPass && !gLanderAboveLiquid)
		return 0;
	if (!eng_model_inst_ready(p->modelInst))          // model (or its fallback) not loaded
		return 0;
	const LanderModelDef *md = &gLanderModelTable[L->ps.landerType];
	eng_fx_engine_begin(&L->engineFx, p->modelCoord);  // light colour, viewBelow (camera y <= -64)
	for (int i = 0; i < md->nExhaust; i++) {
		L->engineFx.pos[0] = md->exhaust[i][0];
		L->engineFx.pos[1] = md->exhaust[i][1];
		L->engineFx.pos[2] = md->exhaust[i][2];
		eng_fx_engine_flame(&L->engineFx);            // 0x40af70
	}
	if (L->ctrlWord & LCW_TRACTOR) {
		int32_t len = ((L->renderFrac * 40) >> 12) + L->tractorTime;
		int32_t alpha = ((uint32_t)len < 0x400) ? (int32_t)(((uint32_t)len << 8) >> 10) : 0x100;
		if (L->hookTarget == OBJ_NONE)
			eng_fx_tractor_beam(0x300, alpha, len, 0, NULL);          // straight down (camera rot)
		else
			eng_fx_tractor_beam(0x300, 0x96, len, 2, L->beamCoord);   // toward the target
	}
	for (int i = 0; i < 8; i++)
		if (L->att.jets[i].intensity != 0)
			eng_fx_attitude_jet(&L->att.jets[i]);      // 0x40ac30 (model transform set)
	if (L->headlight) {
		uint32_t lv = (uint16_t)L->headlightLevel;
		eng_fx_headlight_glow(L->headlightGlow, (int32_t)(lv >> 4), lv | lv << 8 | lv << 16);
	}
	eng_model_draw(p);
	return 1;
}

// =================================================================================== destroy
void Lander_Destroy(LanderProc *p)                       // 0x423aa0
{
	Lander *L = p->lander;
	if (L->ps.secondary >= 15 && L->ps.secondary <= 20)
		L->ps.missiles = L->weapons.slot[1].level;     // +0x314
	if (L->player == gThisPlayerId) {                  // maxFuel and team are not written back
		gPlayerState.missiles = L->ps.missiles;
		gPlayerState.fuel = L->ps.fuel;
		gPlayerState.shield = L->ps.shield;
		gPlayerState.cash = L->ps.cash;
		gPlayerState.primary = L->ps.primary;
		gPlayerState.secondary = L->ps.secondary;
		gPlayerState.engine = L->ps.engine;
		gPlayerState.armour = L->ps.armour;
		gPlayerState.landerType = L->ps.landerType;
	}
	if (L->headlight)
		lander_headlight_off(L);
	eng_coord_free(L->beamCoord);
	if (L->damaged == 0 || gMultiPlayerGame)          // SP wreck already lost its engine light
		eng_fx_engine_light_destroy(&L->engineFx);
	if (L->damaged == 0)
		weapons_release_mounts(&L->weapons);
	lander_release_model(p, L);
	eng_coord_free(p->coord);
	if (p->flags & LPF_HUD)
		eng_hud_destroy();
	if (p->flags & LPF_SHADOW)
		eng_shadow_destroy();
	phys_body_free(L->body);
	if (L->hookTarget != OBJ_NONE)
		eng_pod_release(L->hookTarget);                // Pod_SetTarget(hookTarget, -3)
	eng_player_remove(L->player);
	eng_forcebox_remove_body(p->id);
	eng_snd_stop_object(p->id);
	if (gMultiPlayerGame && gThisPlayer != (int32_t)p->id)
		eng_hud_marker_remove(L->marker);
}

// =================================================================================== contact callback
// 0x420ba0, body->on_contact: called per contact response with the normal impulse jn and the
// friction value jf of that contact
void Lander_OnContact(phys_body_t *b, phys_contact_t *c, int32_t jn, int32_t jf)
{
	Lander *L = Lander_FromHandle((ObjId)b->owner);
	if (!L)
		return;
	if (phys_g.liquid) {                               // liquid surface (classes 4, 8)
		L->liquidContact = 1;
		return;
	}
	if (jf > 0x200) {                                  // sparks
		int32_t wp[3] = {b->cur->wp[c->point][0] >> 3, b->cur->wp[c->point][1] >> 3, b->cur->wp[c->point][2] >> 3};
		int32_t v[3];
		v[0] = (c->rel_vel[0] >> 4) + (eng_rand() & 0x1f) - 0x10;
		v[1] = (c->rel_vel[1] >> 4) + (eng_rand() & 0x1f) - 0x10;
		v[2] = (c->rel_vel[2] >> 4) + (eng_rand() & 0x1f) - 0x10;
		for (int i = 0; i < 3; i++) {
			v[i] += c->normal[i] >> 8;
			if (v[i] > 0x7f) v[i] = 0x7f;
			if (v[i] < -0x80) v[i] = -0x80;
		}
		void *pt = eng_particle_spawn(8, wp, 0);
		if (pt)
			eng_particle_set_velocity(pt, v[0], v[1], v[2]);
	}
	L->scrape += jf;
	L->scrapeCount = (int16_t)(L->scrapeCount + (jn >> 3));
}

// =================================================================================== properties
intptr_t Lander_GetProperty(ObjId id, int32_t prop)
{
	static int32_t vel[3];
	LanderProc *p = eng_lander_proc(id);
	Lander *L = p ? p->lander : NULL;
	switch (prop) {
	case PROP_POSITION:                                // 0x424050
		return L ? (intptr_t)L->world.t : 0;
	case PROP_TARGET:                                  // 0x424140
		return 0xff;
	case PROP_MATRIX:                                  // 0x424080
		if (!L)
			return 0;
		if (gAutoCamQuery)
			return (intptr_t)&L->display;              // the commanded attitude: steady camera
		if (phys_g.in_physics) {
			FwMatrix *m = &L->substep[eng_phys_query_slot()];
			phys_body_interpolate(L->body, (phys_matrix_t *)m, eng_phys_query_time());
			m->t[0] >>= 3; m->t[1] >>= 3; m->t[2] >>= 3;
			return (intptr_t)m;
		}
		return (intptr_t)&L->world;
	case PROP_HITPOINTS:                               // 0x424100
		return L ? L->ps.shield : 0;
	case PROP_ARMOUR:                                  // 0x424120
		return L ? L->armourValue : 0;
	case PROP_BODY:                                    // 0x424150
		return L ? (intptr_t)L->body : 0;
	case PROP_VELOCITY:                                // 0x4241d0: world units per second
		for (int i = 0; i < 3; i++)
			vel[i] = ((L->body->vel[i] * 1000) / p->period) >> 3;
		return (intptr_t)vel;
	case PROP_FLAGS:                                   // 0x424170
		return (intptr_t)p->flags;
	}
	return 0;
}

void Lander_SetProperty(ObjId id, int32_t prop, intptr_t value)
{
	LanderProc *p = eng_lander_proc(id);
	Lander *L = p ? p->lander : NULL;
	switch (prop) {
	case PROP_HITPOINTS:
		Lander_SetShield(id, (int32_t)value);
		break;
	case PROP_POSITION: {                              // 0x4243a0 teleport
		const int32_t *v = (const int32_t *)value;
		if (!L)
			return;
		int32_t p8[3] = {v[0] << 3, v[1] << 3, v[2] << 3};
		phys_body_set_position(L->body, p8);
		for (int i = 0; i < 3; i++) {
			L->body->cur->mat.t[i] = v[i];
			L->body->next->mat.t[i] = v[i];
			L->world.t[i] = v[i];
			L->prevPos[i] = v[i];
			L->display.t[i] = v[i];
		}
		L->scanTimer = 1;
		break;
	}
	case PROP_MATRIX: {                                // 0x4244c0 orientation
		const FwMatrix *v = (const FwMatrix *)value;
		FwMatrix m;
		if (!L)
			return;
		// The original copies a stack matrix whose translation was never written (garbage) into the
		// body states, L->world and L->target; this port keeps the current translations instead.
		m.t[0] = L->world.t[0]; m.t[1] = L->world.t[1]; m.t[2] = L->world.t[2];
		lm_mul_rot_y(v, &m, 0x200);                    // body = M x Ry(-45 deg)
		memcpy(L->body->state[0].mat.m, m.m, sizeof m.m);
		memcpy(L->body->state[1].mat.m, m.m, sizeof m.m);
		memcpy(L->world.m, m.m, sizeof m.m);
		L->attitude = m;
		lm_mul_rot_y(&L->attitude, &L->display, -0x200);
		memcpy(L->prevPos, L->world.t, sizeof L->world.t);
		memcpy(L->display.t, L->attitude.t, sizeof L->attitude.t);
		L->heading = lm_ratan2(L->body->cur->mat.m[2], L->body->cur->mat.m[8]);
		memset(L->body->vel, 0, 3 * sizeof(int32_t));
		memset(L->body->angvel, 0, 3 * sizeof(int32_t));
		if (gThisPlayer == (int32_t)id && gMouseStickPresent)
			eng_input_reset_mouse_stick();
		break;
	}
	case PROP_FLAGS: {                                 // 0x424620
		uint32_t v = (uint32_t)value;
		if (v & PROPFLAGS_CLEAR) {
			if (v & LPF_HUD)
				eng_hud_destroy();
			p->flags &= ~(v & 0x7fffffffu);
		} else {
			p->flags |= (uint32_t)(int32_t)(int16_t)v;  // movsx: 0x8000 sets bits 15..31
			if (v & LPF_TRACTOR_LATCH)
				p->lander->ctrlWord &= ~LCW_TRACTOR;
		}
		break;
	}
	}
}

// =================================================================================== exported / helper API
void Lander_SetShield(ObjId id, int32_t shield)          // 0x424270
{
	Lander *L = Lander_FromHandle(id);
	if (!L)
		return;
	if (shield < 0)
		shield = 0;
	if (L->player == gThisPlayerId) {                  // "shield N %" when crossing 75/50/25 %
		int32_t old = L->ps.shield, pc = 0;
		if (old > 0x3f && shield <= 0x3f) pc = 0x3ff;
		else if (old > 0x7f && shield <= 0x7f) pc = 0x400;
		else if (old > 0xbf && shield <= 0xbf) pc = 0x401;
		if (pc && CockVox(0))
			eng_snd_chain(SND_COCKPIT_VOICE, 3, 8, 0x419, pc);
	}
	if (gCheatInvulnerable)
		return;
	int32_t old = L->ps.shield;
	L->ps.shield = shield;
	if (old != 0 && shield == 0) {
		L->respawnTimer = 0x28;
		LanderEvent ev = {EV_DESTROYED, {(intptr_t)id,
		                  gDamageSource != -1 ? gDamageSource : L->body->touched, 0x19}};
		Event_Raise(&ev);
	}
}

int32_t Lander_ArmourScaleDamage(ObjId id, int32_t dmg, int32_t kind)   // 0x424690
{
	Lander *L = Lander_FromHandle(id);
	if (!L)
		return 0;
	switch (L->ps.armour) {                            // hit sound by armour family
	case 0: case 1: case 2: case 3: eng_snd_once(id, 0x28); break;
	case 4: case 5: case 6:         eng_snd_once(id, 0x2b); break;
	case 7: case 9:                 eng_snd_once(id, 0x29); break;
	case 8: case 10:                eng_snd_once(id, 0x2a); break;
	}
	switch (kind) {                                    // projectile kind
	case 0: case 4: return dmg >> (L->armourShift0 & 31);   // reflective
	case 1: case 2: return dmg >> (L->armourShift1 & 31);   // reactive
	}
	return dmg;
}

int Lander_TakeFuel(ObjId id, int32_t *dumpFuel)          // 0x424740 (FuelDump_Update)
{
	Lander *L = Lander_FromHandle(id);
	int done = 0;
	int32_t amount;
	if (!(L->ctrlWord & LCW_TRACTOR))
		return 1;                                      // beam released: stop
	if (*dumpFuel <= 0x1000) {
		if (L->ps.fuel <= L->ps.maxFuel) {
			if (L->player == gThisPlayerId && CockVox(1))
				eng_snd_once(SND_COCKPIT_VOICE, 0x409);
			done = 1;
		}
		amount = *dumpFuel;
	} else {
		amount = 0x1000;                               // 0x1000 per 40 ms tick
	}
	L->ps.fuel += amount;
	if (L->ps.fuel > L->ps.maxFuel) {
		if (L->player == gThisPlayerId && CockVox(1))
			eng_snd_chain(SND_COCKPIT_VOICE, 2, 0x40a, 0x40b);   // "fuel full"
		int32_t over = L->ps.fuel;
		L->ps.fuel = L->ps.maxFuel;
		done = 1;
		amount += L->ps.maxFuel - over;
	}
	*dumpFuel -= amount;
	return done;
}

int Lander_IsTractorOn(ObjId id)                          // 0x424840
{
	Lander *L = Lander_FromHandle(id);
	return L ? (int)((L->ctrlWord >> 24) & 4) : 0;
}

int Lander_SetHookTarget(ObjId id, ObjId t)               // 0x424860
{
	Lander *L = Lander_FromHandle(id);
	uint32_t type = (t >> 16) & 0xff;
	if (!L)
		return 1;
	if (t != OBJ_NONE && type == OT_POD && gMultiPlayerGame && gNetworkGameType == 1) {
		uint32_t lf = (uint32_t)Object_GetProperty(id, PROP_FLAGS);
		uint32_t pf = (uint32_t)Object_GetProperty(t, PROP_FLAGS);
		if (((lf >> 5) & 0x40) == (pf & 0x40))
			return 0;                                  // team game: own team's pod
	}
	if (L->hookTarget != OBJ_NONE && t != OBJ_NONE && (L->hookTarget & 0x7fffffffu) != t)
		return 0;
	if (L->sndTractor)
		eng_snd_stop(L->sndTractor);
	if (L->hookTarget != OBJ_NONE && t == OBJ_NONE)
		L->sndTractor = eng_snd_loop(id, 0x1f, -1);    // back to the idle beam hum
	L->hookTarget = t;
	if (!(t & 0x80000000u)) {
		eng_snd_once(id, 0x21);                        // pod hooked
		return 1;
	}
	if (type == OT_POD)
		L->sndTractor = eng_snd_loop(id, 0x20, -1);
	else if (type == OT_FUELDUMP)
		L->sndTractor = eng_snd_loop(id, 0x22, -1);
	eng_snd_once(id, 0x23);
	eng_coord_identity(L->beamCoord);
	return 1;
}

void Lander_SetHeadLight(ObjId lander, int32_t coneAngle, int32_t intensity)   // 0x424990
{
	LanderProc *p = eng_lander_proc(lander);
	Lander *L = p->lander;
	if (L->headlight)
		lander_headlight_off(L);
	if (intensity != 0)
		lander_headlight_on(L, p, coneAngle, intensity);
}

int32_t Lander_SwapHeading(ObjId id, int32_t heading)     // 0x4249e0
{
	Lander *L = Lander_FromHandle(id);
	if (!L)
		return 0;
	int32_t old = L->heading;
	L->heading = heading;
	return old;
}

int32_t Lander_GetHeading(ObjId id)                       // 0x4204a0
{
	Lander *L = Lander_FromHandle(id);
	return L ? L->heading : 0;
}

const int32_t *Lander_GetPrevPos(ObjId id)                // 0x424a20
{
	Lander *L = Lander_FromHandle(id);
	return L ? L->prevPos : NULL;
}

int32_t Lander_GetPlayerId(ObjId lander)                  // 0x424a00
{
	Lander *L = Lander_FromHandle(lander);
	return L ? L->player : -1;
}

int32_t Lander_GetCash(ObjId lander)                      // 0x424a40
{
	if (lander == OBJ_NONE)
		return gPlayerState.cash;
	Lander *L = Lander_FromHandle(lander);
	return L ? L->ps.cash : 0;
}

void Lander_SetCash(ObjId lander, int32_t cash)           // 0x424a90
{
	if (lander == OBJ_NONE) {
		gPlayerState.cash = cash;
		return;
	}
	Lander *L = Lander_FromHandle(lander);
	if (L)
		L->ps.cash = cash;
}

int32_t Lander_GetFuel(ObjId lander)                      // 0x424a70
{
	Lander *L = Lander_FromHandle(lander);
	return L ? L->ps.fuel : 0;
}

void Lander_SetFuel(ObjId lander, int32_t fuel)           // 0x424ac0
{
	Lander *L = Lander_FromHandle(lander);
	if (L)
		L->ps.fuel = fuel > L->ps.maxFuel ? L->ps.maxFuel : fuel;
}

int32_t Lander_AddMissiles(ObjId lander, int32_t n)       // 0x424af0: returns how many were added
{
	Lander *L = Lander_FromHandle(lander);
	int32_t cap = 0;
	if (!L)
		return n;
	switch (L->ps.secondary) {
	case 15: cap = 5; break;
	case 16: cap = 10; break;
	case 17: cap = 20; break;
	}
	if (cap == 0)
		return 0;
	int32_t v = L->weapons.slot[1].level + n;
	L->ps.missiles = v;
	if (v > cap) {
		L->ps.missiles = cap;
		n += cap - v;
	}
	L->weapons.slot[1].level = (int16_t)L->ps.missiles;
	return n;
}

int32_t Lander_BodyChecksum(int32_t player)               // 0x424df0 (network desync check)
{
	const int16_t *m = gLanders[player].body->cur->mat.m;
	int32_t s = 0;
	for (int i = 0; i < 9; i++)
		s += m[i];
	return s;
}

// Voice_* exports (0x424b70-0x424da0): cockpit voice lines for the HUD owner
static int voice_check(ObjId lander, int32_t prio)        // Voice_Check 0x424b70
{
	LanderProc *p = eng_lander_proc(lander);
	return (p->flags & LPF_HUD) && CockVox(prio);
}
void Voice_CashAcquired(ObjId lander)
{
	if (voice_check(lander, 1)) Sound_PlayChain(SND_COCKPIT_VOICE, 2, 0x403, 0x402);
}
void Voice_ShieldAcquired(ObjId lander, int32_t amount)
{
	if (!voice_check(lander, 1))
		return;
	switch (amount) {
	case 25:  Sound_PlayChain(SND_COCKPIT_VOICE, 3, 0x3ff, 0x419, 0x402); break;
	case 50:  Sound_PlayChain(SND_COCKPIT_VOICE, 3, 0x400, 0x419, 0x402); break;
	case 75:  Sound_PlayChain(SND_COCKPIT_VOICE, 3, 0x401, 0x419, 0x402); break;
	case 100: Sound_PlayChain(SND_COCKPIT_VOICE, 3, 0x40b, 0x419, 0x402); break;
	}
}
void Voice_HeatShieldAcquired(ObjId lander)
{
	if (voice_check(lander, 1)) Sound_PlayChain(SND_COCKPIT_VOICE, 2, 0x40c, 0x402);
}
void Voice_MissilesAcquired(ObjId lander)
{
	if (voice_check(lander, 1)) Sound_PlayChain(SND_COCKPIT_VOICE, 2, 0x410, 0x402);
}
void Voice_SecurityPassAcquired(ObjId lander, int32_t pass)
{
	if (voice_check(lander, 1)) Sound_PlayChain(SND_COCKPIT_VOICE, 3, 0x417, pass + 0x3fb, 0x402);
}
void Voice_MissilesFull(ObjId lander)
{
	if (voice_check(lander, 1)) Sound_PlayChain(SND_COCKPIT_VOICE, 2, 0x410, 0x40b);
}
