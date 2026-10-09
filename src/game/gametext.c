#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gametext.h"
#include "game.h"
#include "../pe.h"
#include "../fs.h"

// the two builds of the program on the disc; GameText.va is the first's, .va_glide the second's
static const char *const exe_paths[2] = {"LNDRD3DR\\LANDER.EXE", "LNDRGLDR\\LANDER.EXE"};
static const GameText *const exe_lists[] = {fe_exe_text, hud_exe_text, im_exe_text};

#define MAIL_RECORD 0x104       // brief.dll mail record: from +0, subject +0x80, category +0x100

static const GameText *loaded[128];     // the lists whose arrays are filled
static int num_loaded;

static bool is_loaded(const GameText *list) {
	for (int i = 0; i < num_loaded; i++) {
		if (loaded[i] == list) return true;
	}
	return false;
}

static void mark_loaded(const GameText *list) {
	if (!is_loaded(list) && num_loaded < (int)len(loaded)) loaded[num_loaded++] = list;
}

static int table_strings(const GameText *t) { // string slots of a table (a mail has two)
	return t->kind == GT_MAILS ? 2 * t->count : t->kind == GT_BYTES ? 0 : t->count;
}

// The text of table t in the file at va: str[] (pointers into pe; NULL for a zero pointer), the mails'
// categories in cat[], a byte block in *bytes. false if the file does not have it.
static bool table_read(const pe_image_t *pe, const GameText *t, uint32_t va, const char **str, int32_t *cat,
	const uint8_t **bytes) {
	switch (t->kind) {
		case GT_POINTERS:
			return pe_string_table(pe, va, str, t->count);
		case GT_STRINGS:
			for (int i = 0; i < t->count; i++) {
				if (!(str[i] = pe_string(pe, va + (uint32_t)i * t->stride))) return false;
			}
			return true;
		case GT_MAILS:
			for (int i = 0; i < t->count; i++) {
				uint32_t rec = va + (uint32_t)i * MAIL_RECORD;
				uint32_t c;
				str[2 * i] = pe_string(pe, rec);
				str[2 * i + 1] = pe_string(pe, rec + 0x80);
				if (!str[2 * i] || !str[2 * i + 1] || !pe_u32(pe, rec + 0x100, &c)) return false;
				cat[i] = (int32_t)c;
			}
			return true;
		case GT_BYTES:
			return (*bytes = pe_at(pe, va, (uint32_t)t->count)) != NULL;
	}
	return false;
}

// the string slot i of a table's array
static const char **table_slot(const GameText *t, int i) {
	if (t->kind == GT_MAILS) {
		BriefMail *m = (BriefMail *)t->out + i / 2;
		return i & 1 ? &m->subject : &m->from;
	}
	return (const char **)t->out + i;
}

// Fills table t's array with copies of its text in the file (they are kept until the program ends).
static bool table_load(const pe_image_t *pe, const GameText *t, uint32_t va) {
	int n = table_strings(t);
	const char **str = calloc((size_t)n + 1, sizeof(*str));
	int32_t *cat = calloc((size_t)t->count + 1, sizeof(*cat));
	const uint8_t *bytes = NULL;
	bool ok = str && cat && pe && table_read(pe, t, va, str, cat, &bytes);
	if (t->kind == GT_BYTES) {
		if (ok) memcpy(t->out, bytes, (size_t)t->count);
		else memset(t->out, 0, (size_t)t->count);
	} else {
		size_t total = 0;
		for (int i = 0; ok && i < n; i++) total += str[i] ? strlen(str[i]) + 1 : 0;
		char *block = ok ? malloc(total + 1) : NULL, *p = block;
		ok = ok && block;
		for (int i = 0; i < n; i++) {
			if (!ok) {
				*table_slot(t, i) = "";
			} else if (str[i]) {
				size_t l = strlen(str[i]) + 1;
				memcpy(p, str[i], l);
				*table_slot(t, i) = p;
				p += l;
			} else {
				*table_slot(t, i) = NULL;
			}
		}
		if (t->kind == GT_MAILS) {
			for (int i = 0; i < t->count; i++) ((BriefMail *)t->out)[i].category = ok ? cat[i] : 5;   // 5: no such mail
		}
	}
	free(str);
	free(cat);
	return ok;
}

static bool list_load(const pe_image_t *pe, const char *path, const GameText *list, bool glide) {
	bool ok = true;
	for (const GameText *t = list; t->count; t++) {
		uint32_t va = glide ? t->va_glide : t->va;
		if (!table_load(pe, t, va)) {
			fprintf(stderr, "%s: no text at 0x%08x in %s (game data %s)\n", pe ? "bad file" : "missing file", va,
				path, fs_root());
			ok = false;
		}
	}
	mark_loaded(list);
	return ok;
}

bool gametext_load(const char *path, const GameText *list) {
	if (!list || is_loaded(list)) return true;
	pe_image_t *pe = pe_load(path);
	bool ok = list_load(pe, path, list, false);
	pe_free(pe);
	return ok;
}

static bool table_ok(const pe_image_t *pe, const GameText *t, uint32_t va) { // the file has table t at va
	const char **str = calloc((size_t)table_strings(t) + 1, sizeof(*str));
	int32_t *cat = calloc((size_t)t->count + 1, sizeof(*cat));
	const uint8_t *bytes;
	bool ok = str && cat && table_read(pe, t, va, str, cat, &bytes);
	free(str);
	free(cat);
	return ok;
}

// the build of lander.EXE that has all of its text (the Direct3D one first); NULL if none
static pe_image_t *exe_open(int *build) {
	for (int b = 0; b < 2; b++) {
		pe_image_t *pe = pe_load(exe_paths[b]);
		bool ok = pe != NULL;
		for (size_t l = 0; ok && l < len(exe_lists); l++) {
			for (const GameText *t = exe_lists[l]; ok && t->count; t++) ok = table_ok(pe, t, b ? t->va_glide : t->va);
		}
		if (ok) {
			*build = b;
			return pe;
		}
		pe_free(pe);
	}
	return NULL;
}

static void dll_path(char *out, size_t size, const mission_def_t *m, const char *file) {
	snprintf(out, size, "MISSIONS\\%s\\%s\\%s", m->world, m->mission, file);
}

bool gametext_missing(char *what, size_t size) {
	int build;
	pe_image_t *exe = exe_open(&build);
	if (!exe) {
		snprintf(what, size, "LANDER.EXE (%s or %s)", exe_paths[0], exe_paths[1]);
		return true;
	}
	pe_free(exe);
	int n;
	const mission_def_t *m = mission_list(&n);
	for (int i = 0; i < n; i++) {
		if (m[i].script && m[i].script->text) {
			dll_path(what, size, &m[i], "MISSIONR.DLL");
			if (!fs_exists(what)) return true;
		}
		if (m[i].brief && m[i].brief->text) {
			dll_path(what, size, &m[i], "BRIEF.DLL");
			if (!fs_exists(what)) return true;
		}
	}
	return false;
}

void gametext_init(void) {
	int build = 0;
	pe_image_t *pe = exe_open(&build);
	if (!pe) {
		fprintf(stderr, "Lander: the game's text could not be read from %s or %s in the game data %s.\n",
			exe_paths[0], exe_paths[1], fs_root());
		exit(1);
	}
	for (size_t l = 0; l < len(exe_lists); l++) list_load(pe, exe_paths[build], exe_lists[l], build == 1);
	pe_free(pe);
}

// ---------------------------------------------------------------- LANDER_CHECK_STRINGS

static bool list_empty(const GameText *list) {
	for (const GameText *t = list; t->count; t++) {
		for (int i = 0; i < table_strings(t); i++) {
			if (*table_slot(t, i)) return false;
		}
		for (int i = 0; t->kind == GT_BYTES && i < t->count; i++) {
			if (((const char *)t->out)[i]) return false;
		}
	}
	return true;
}

static void put_escaped(FILE *f, const char *s, size_t n) {
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c < 0x20 || c >= 0x7f || c == '\\') fprintf(f, "\\x%02x", c);
		else fputc(c, f);
	}
}

static void show(const char *s) {
	if (!s) {
		fprintf(stderr, "NULL");
		return;
	}
	fputc('"', stderr);
	put_escaped(stderr, s, strlen(s));
	fputc('"', stderr);
}

// table t's array against the file at va: the number of differences (reported on stderr)
static int table_check(const pe_image_t *pe, const char *path, const GameText *t, uint32_t va) {
	int n = table_strings(t), diffs = 0;
	const char **str = calloc((size_t)n + 1, sizeof(*str));
	int32_t *cat = calloc((size_t)t->count + 1, sizeof(*cat));
	const uint8_t *bytes = NULL;
	if (!str || !cat || !table_read(pe, t, va, str, cat, &bytes)) {
		fprintf(stderr, "%s 0x%08x: cannot be read\n", path, va);
		diffs = 1;
	} else if (t->kind == GT_BYTES) {
		for (int i = 0; i < t->count; i++) {
			if (bytes[i] != ((const uint8_t *)t->out)[i]) {
				fprintf(stderr, "%s 0x%08x: byte %d is 0x%02x in the file, 0x%02x in the array\n", path, va, i,
					bytes[i], ((const uint8_t *)t->out)[i]);
				diffs++;
			}
		}
	} else {
		for (int i = 0; i < n; i++) {
			const char *a = str[i], *b = *table_slot(t, i);
			if (a == b || (a && b && !strcmp(a, b))) continue;
			fprintf(stderr, "%s 0x%08x [%d]: file ", path, va, i);
			show(a);
			fprintf(stderr, ", array ");
			show(b);
			fputc('\n', stderr);
			diffs++;
		}
		for (int i = 0; t->kind == GT_MAILS && i < t->count; i++) {
			if (cat[i] != ((const BriefMail *)t->out)[i].category) {
				fprintf(stderr, "%s 0x%08x mail %d: category %d in the file, %d in the array\n", path, va, i,
					cat[i], ((const BriefMail *)t->out)[i].category);
				diffs++;
			}
		}
	}
	free(str);
	free(cat);
	return diffs;
}

static void list_dump(FILE *f, const char *path, const GameText *list) {
	for (const GameText *t = list; t->count; t++) {
		if (t->kind == GT_BYTES) {
			fprintf(f, "%s 0x%08x bytes: ", path, t->va);
			put_escaped(f, t->out, (size_t)t->count);
			fputc('\n', f);
			continue;
		}
		for (int i = 0; i < table_strings(t); i++) {
			const char *s = *table_slot(t, i);
			fprintf(f, "%s 0x%08x [%d]: ", path, t->va, i);
			if (s) put_escaped(f, s, strlen(s));
			else fputs("(NULL)", f);
			fputc('\n', f);
		}
		for (int i = 0; t->kind == GT_MAILS && i < t->count; i++) {
			fprintf(f, "%s 0x%08x mail %d category %d\n", path, t->va, i, ((const BriefMail *)t->out)[i].category);
		}
	}
}

static int list_check(const char *path, const GameText *list, int *tables, FILE *dump) {
	if (!list) return 0;
	if (!is_loaded(list) && list_empty(list)) gametext_load(path, list);
	pe_image_t *pe = pe_load(path);
	int diffs = 0;
	if (!pe) {
		fprintf(stderr, "%s: missing\n", path);
		diffs++;
	}
	for (const GameText *t = list; pe && t->count; t++, ++*tables) diffs += table_check(pe, path, t, t->va);
	pe_free(pe);
	if (dump) list_dump(dump, path, list);
	return diffs;
}

int gametext_check(void) {
	const char *dump_path = getenv("LANDER_STRINGS_DUMP");
	FILE *dump = dump_path ? fopen(dump_path, "w") : NULL;
	int diffs = 0, tables = 0;
	// lander.EXE: the arrays against both builds
	int build;
	pe_image_t *pe = exe_open(&build);
	for (size_t l = 0; pe && l < len(exe_lists); l++) {
		if (!is_loaded(exe_lists[l]) && list_empty(exe_lists[l])) list_load(pe, exe_paths[build], exe_lists[l], build == 1);
	}
	pe_free(pe);
	for (int b = 0; b < 2; b++) {
		pe = pe_load(exe_paths[b]);
		if (!pe) {
			fprintf(stderr, "%s: missing\n", exe_paths[b]);
			diffs++;
			continue;
		}
		for (size_t l = 0; l < len(exe_lists); l++) {
			for (const GameText *t = exe_lists[l]; t->count; t++, tables++) {
				diffs += table_check(pe, exe_paths[b], t, b ? t->va_glide : t->va);
			}
		}
		pe_free(pe);
	}
	for (size_t l = 0; dump && l < len(exe_lists); l++) list_dump(dump, "LANDER.EXE", exe_lists[l]);
	// the missions' DLLs
	int n;
	const mission_def_t *m = mission_list(&n);
	for (int i = 0; i < n; i++) {
		char path[96];
		if (m[i].script) {
			dll_path(path, sizeof(path), &m[i], "MISSIONR.DLL");
			diffs += list_check(path, m[i].script->text, &tables, dump);
		}
		if (m[i].brief) {
			dll_path(path, sizeof(path), &m[i], "BRIEF.DLL");
			diffs += list_check(path, m[i].brief->text, &tables, dump);
		}
	}
	if (dump) fclose(dump);
	printf("strings check: %d tables, %d differences\n", tables, diffs);
	return diffs;
}
