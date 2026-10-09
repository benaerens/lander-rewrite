// Saved games: SAVEGAME.DAT, byte-compatible with the original (Save_FormatFile 0x41c320,
// Save_CreateFile 0x41c5f0, Save_Load 0x41c660, Save_WriteSlotData 0x41c8d0, Save_Write 0x41cb30,
// Save_ReadNames 0x41cc90), kept in platform_save_dir() instead of the install folder.
//
//   0x00 header: u32 magic 0x5A7E9ADE, u32 file size, u32 slots (44), u32 names offset, u32 data offset
//   0x14 u32 slot offset[44] (relative to the data offset; slot size (14 + 2) * 45 * 4 + 0x40)
//   names: 44 x char[32] (empty = unused)
//   slot:  u32 length, u32 0, s32 mail count (-1 = empty), s32 replay point, s32 mails at de-brief;
//          11 x s32 player block {missiles, fuel, shield, cash, primary, secondary, engine, armour,
//          lander, cash at launch (-1), -1}; one s16 result per mission (world-major, gnNumMissionsForLevel);
//          u32 mail count; mail count x {u8 world, mission, index, read}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fe.h"
#include "../platform.h"

#define SAVE_MAGIC 0x5A7E9ADEu
#define SAVE_MISSIONS 45                        // Missions_Total 0x41c5d0
#define SAVE_SLOT_SIZE ((14 + 2) * SAVE_MISSIONS * 4 + 0x40)
#define NAMES_OFS (0x14 + SAVE_SLOTS * 4)
#define DATA_OFS (NAMES_OFS + SAVE_SLOTS * 0x20)

static char names[SAVE_SLOTS][32];
static int32_t replay_cash = -1;                // DAT_0049d43c

static void save_path(char *out, size_t n) { snprintf(out, n, "%s/SAVEGAME.DAT", platform_save_dir()); }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool write_at(FILE *f, long ofs, const void *d, size_t n) {
	return fseek(f, ofs, SEEK_SET) == 0 && fwrite(d, 1, n, f) == n;
}

static FILE *create_file(const char *path) { // Save_CreateFile + Save_FormatFile(f, 23, 44, 45, 14)
	FILE *f = fopen(path, "w+b");
	if (!f) return NULL;
	uint8_t h[0x14];
	put32(h, SAVE_MAGIC);
	put32(h + 4, (uint32_t)(SAVE_SLOT_SIZE * SAVE_SLOTS + DATA_OFS));
	put32(h + 8, SAVE_SLOTS);
	put32(h + 12, NAMES_OFS);
	put32(h + 16, DATA_OFS);
	bool ok = write_at(f, 0, h, sizeof(h));
	for (int i = 0; i < SAVE_SLOTS && ok; i++) {
		uint8_t o[4];
		put32(o, (uint32_t)(i * SAVE_SLOT_SIZE));
		ok = fwrite(o, 1, 4, f) == 4;
	}
	static const uint8_t zero[0x20];
	for (int i = 0; i < SAVE_SLOTS && ok; i++) ok = fwrite(zero, 1, 0x20, f) == 0x20;
	// every slot: header {size, 45, -1, -1, -1}, an empty player block, per mission {world, mission, 14, 0} + 14 x {-1, 0}
	for (int s = 0; s < SAVE_SLOTS && ok; s++) {
		uint8_t sh[0x14], pb[0x2c] = {0};
		put32(sh, SAVE_SLOT_SIZE);
		put32(sh + 4, SAVE_MISSIONS);
		put32(sh + 8, 0xffffffff);
		put32(sh + 12, 0xffffffff);
		put32(sh + 16, 0xffffffff);
		ok = write_at(f, DATA_OFS + s * SAVE_SLOT_SIZE, sh, sizeof(sh)) && fwrite(pb, 1, sizeof(pb), f) == sizeof(pb);
		for (int w = 0; w < 23 && ok; w++) {
			for (int m = 0; m < gnNumMissionsForLevel[w] && ok; m++) {
				uint8_t e[8] = {(uint8_t)w, 0, (uint8_t)m, 0, 14, 0, 0, 0};
				ok = fwrite(e, 1, 8, f) == 8;
				for (int k = 0; k < 14 && ok; k++) {
					uint8_t x[4] = {0xff, 0xff, 0, 0};
					ok = fwrite(x, 1, 4, f) == 4;
				}
			}
		}
	}
	if (!ok) {
		fclose(f);
		return NULL;
	}
	fflush(f);
	return f;
}

static uint8_t *read_all(long *size) {
	char path[600];
	save_path(path, sizeof(path));
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = *size > 0x14 ? malloc((size_t)*size) : NULL;
	if (d && fread(d, 1, (size_t)*size, f) != (size_t)*size) {
		free(d);
		d = NULL;
	}
	fclose(f);
	if (d && get32(d) != SAVE_MAGIC) {
		free(d);
		d = NULL;
	}
	return d;
}

static void read_names(void) { // Save_ReadNames 0x41cc90
	memset(names, 0, sizeof(names));
	long size;
	uint8_t *d = read_all(&size);
	if (!d) return;
	uint32_t n = get32(d + 8), no = get32(d + 12);
	for (uint32_t i = 0; i < n && i < SAVE_SLOTS && no + (i + 1) * 0x20 <= (uint32_t)size; i++) {
		memcpy(names[i], d + no + i * 0x20, 0x20);
		names[i][31] = 0;
	}
	free(d);
}

char *Save_SlotNameBuf(int slot) {
	return slot >= 0 && slot < SAVE_SLOTS ? names[slot] : NULL;
}

const char *Save_SlotName(int slot) {
	if (slot == 0) read_names();
	return Save_SlotNameBuf(slot);
}

void Save_ReadNames(void) { read_names(); }

int Save_Load(int slot) { // 0x41c660 -> 0 error, 1 empty, 2 ok
	long size;
	uint8_t *d = read_all(&size);
	if (!d) return 0;
	uint32_t n = get32(d + 8), dofs = get32(d + 16);
	int rv = 0;
	if (slot < 0 || (uint32_t)slot >= n || 0x14 + 4 * (uint32_t)n > (uint32_t)size) goto done;
	const uint8_t *p = d + dofs + get32(d + 0x14 + 4 * slot), *end = d + size;
	if (p + 0x14 + 0x2c > end) goto done;
	int32_t mails = (int32_t)get32(p + 8);
	if (mails < 0) {
		rv = 1;
		goto done;
	}
	gMailCountAtDebrief = (int32_t)get32(p + 16);
	gMailReplayPoint = (int32_t)get32(p + 12);
	p += 0x14;
	gPlayerState.missiles = (int32_t)get32(p);
	gPlayerState.fuel = (int32_t)get32(p + 4);
	gPlayerState.shield = (int32_t)get32(p + 8);
	gPlayerState.cash = (int32_t)get32(p + 12);
	gPlayerState.primary = (int32_t)get32(p + 16);
	gPlayerState.secondary = (int32_t)get32(p + 20);
	gPlayerState.engine = (int32_t)get32(p + 24);
	gPlayerState.armour = (int32_t)get32(p + 28);
	gPlayerState.landerType = (int32_t)get32(p + 32);
	replay_cash = (int32_t)get32(p + 36);
	p += 0x2c;
	for (int w = 0; w < 23; w++) {
		for (int m = 0; m < gnNumMissionsForLevel[w]; m++) {
			if (p + 2 > end) goto done;
			gMissionResults[w * 6 + m] = (int8_t)p[0];
			p += 2;
		}
	}
	if (p + 4 > end) goto done;
	gMailCount = (int32_t)get32(p);
	p += 4;
	if (gMailCount < 0 || gMailCount > MAIL_MAX) gMailCount = 0;
	for (int i = 0; i < gMailCount && p + 4 <= end; i++, p += 4) {
		MailRecord *r = &gMailRecords[i];
		r->world = p[0];
		r->mission = p[1];
		r->index = p[2];
		r->read = p[3];
		r->category = 0xff;
		r->from[0] = 0;
		r->subject[0] = 0;
	}
	rv = 2;
done:
	free(d);
	return rv;
}

int Save_Write(int slot, const char *name) { // 0x41cb30 + Save_WriteSlotData 0x41c8d0
	char path[600];
	save_path(path, sizeof(path));
	FILE *f = fopen(path, "r+b");
	if (f) {
		uint8_t m[4];
		if (fread(m, 1, 4, f) != 4 || get32(m) != SAVE_MAGIC) {
			fclose(f);
			f = NULL;
		}
	}
	if (!f && !(f = create_file(path))) return 0;
	char nm[32] = {0};
	snprintf(nm, sizeof(nm), "%s", name ? name : "");
	bool ok = write_at(f, NAMES_OFS + slot * 0x20, nm, strlen(nm) + 1);
	uint8_t buf[0x14 + 0x2c + SAVE_MISSIONS * 2 + 4 + MAIL_MAX * 4];
	uint8_t *p = buf + 0x14;
	int32_t pb[11] = {gPlayerState.missiles, gPlayerState.fuel, gPlayerState.shield, gPlayerState.cash, gPlayerState.primary,
		gPlayerState.secondary, gPlayerState.engine, gPlayerState.armour, gPlayerState.landerType,
		gPlayerCashAtLaunch ? gPlayerCashAtLaunch : -1, -1};
	gPlayerCashAtLaunch = 0;
	for (int i = 0; i < 11; i++, p += 4) put32(p, (uint32_t)pb[i]);
	uint8_t *start = p;
	for (int w = 0; w < 23; w++) {
		for (int m = 0; m < gnNumMissionsForLevel[w]; m++, p += 2) {
			int16_t r = gMissionResults[w * 6 + m];
			p[0] = (uint8_t)r;
			p[1] = (uint8_t)(r >> 8);
		}
	}
	put32(p, (uint32_t)gMailCount);
	p += 4;
	for (int i = 0; i < gMailCount; i++, p += 4) {
		p[0] = gMailRecords[i].world;
		p[1] = gMailRecords[i].mission;
		p[2] = gMailRecords[i].index;
		p[3] = gMailRecords[i].read;
	}
	put32(buf, (uint32_t)(p - start));
	put32(buf + 4, 0);
	put32(buf + 8, (uint32_t)gMailCount);
	put32(buf + 12, (uint32_t)gMailReplayPoint);
	put32(buf + 16, (uint32_t)gMailCountAtDebrief);
	ok = ok && write_at(f, DATA_OFS + slot * SAVE_SLOT_SIZE, buf, (size_t)(p - buf));
	fclose(f);
	read_names();
	return ok;
}

int32_t Save_GetReplayCash(void) { return replay_cash == -1 ? gPlayerState.cash : replay_cash; } // 0x41ce30
