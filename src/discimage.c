// Disc images -> game data folder (the C form of tools/prepare_data.py).
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define file_seek(f, o) _fseeki64(f, (long long)(o), SEEK_SET)
#define file_tell(f) ((uint64_t)_ftelli64(f))
#define file_seek_end(f) _fseeki64(f, 0, SEEK_END)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#define file_seek(f, o) fseeko(f, (off_t)(o), SEEK_SET)
#define file_tell(f) ((uint64_t)ftello(f))
#define file_seek_end(f) fseeko(f, 0, SEEK_END)
#endif

#include "discimage.h"

#define SECTOR 2048
#define MAX_TRACKS 99

typedef struct {
	int number;
	bool audio;
	char path[1024];
	uint64_t offset;     // byte offset of the track's first sector in its file
	int sector_size;     // 2048, 2352 or 2448 (with subchannel)
	uint32_t length;     // sectors
} track_t;

typedef struct {
	track_t tracks[MAX_TRACKS];
	int num_tracks;
	const track_t *data;
	FILE *f;             // the data track's file
	bool joliet;
	char *err;
	size_t err_size;
	disc_progress_fn progress;
	void *user;
	uint64_t done, total;
	int files;
	bool stopped;        // by the progress callback
} disc_t;

static bool fail(disc_t *d, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(d->err, d->err_size, fmt, ap);
	va_end(ap);
	return false;
}

static const char *ext_of(const char *path) {
	const char *dot = strrchr(path, '.');
	const char *slash = strrchr(path, '/'), *bslash = strrchr(path, '\\');
	if (!dot || (slash && slash > dot) || (bslash && bslash > dot)) return "";
	return dot;
}

static bool ext_is(const char *path, const char *ext) {
	const char *e = ext_of(path);
	for (; *e && *ext; e++, ext++) {
		if (tolower((unsigned char)*e) != tolower((unsigned char)*ext)) return false;
	}
	return !*e && !*ext;
}

static int strncasecmp_portable(const char *a, const char *b, size_t n) {
	for (size_t i = 0; i < n; i++) {
		int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[i]);
		if (ca != cb || !ca) return ca - cb;
	}
	return 0;
}

bool disc_is_image(const char *path) {
	static const char *exts[] = {".mds", ".mdf", ".cue", ".iso", ".bin", ".img"};
	for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
		if (ext_is(path, exts[i])) return true;
	}
	return false;
}

static uint64_t file_size(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	file_seek_end(f);
	uint64_t n = file_tell(f);
	fclose(f);
	return n;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

// the file next to path with another extension (tries both cases of the extension)
static bool sibling(const char *path, const char *ext, char *out, size_t size) {
	const char *e = ext_of(path);
	size_t base = (size_t)(e - path);
	for (int upper = 0; upper < 2; upper++) {
		snprintf(out, size, "%.*s", (int)base, path);
		size_t n = strlen(out);
		for (const char *x = ext; *x && n + 1 < size; x++) out[n++] = upper ? (char)toupper((unsigned char)*x) : *x;
		out[n] = 0;
		FILE *f = fopen(out, "rb");
		if (f) {
			fclose(f);
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------- track layouts

static bool tracks_from_mds(disc_t *d, const char *mds) { // Alcohol 120% media descriptor
	uint64_t size = file_size(mds);
	FILE *f = fopen(mds, "rb");
	if (!f || size < 0x60 || size > (1 << 24)) {
		if (f) fclose(f);
		return fail(d, "Cannot read %s", mds);
	}
	uint8_t *b = malloc(size);
	bool ok = fread(b, 1, size, f) == size;
	fclose(f);
	if (!ok || memcmp(b, "MEDIA DESCRIPTOR", 16)) {
		free(b);
		return fail(d, "%s is not an MDS file", mds);
	}
	char mdf[1024];
	if (!sibling(mds, ".mdf", mdf, sizeof(mdf))) {
		free(b);
		return fail(d, "The .mdf file that belongs to %s is missing (keep both files together)", mds);
	}
	int sessions = le16(b + 0x14);
	uint32_t sess_off = le32(b + 0x50);
	for (int s = 0; s < sessions && sess_off + s * 24 + 24 <= size; s++) {
		const uint8_t *so = b + sess_off + s * 24;
		int blocks = so[10];
		uint32_t blocks_off = le32(so + 20);
		for (int k = 0; k < blocks && blocks_off + k * 80 + 80 <= size; k++) {
			const uint8_t *bo = b + blocks_off + k * 80;
			int mode = bo[0], point = bo[4];
			if (point < 1 || point > 99 || d->num_tracks >= MAX_TRACKS) continue;
			uint32_t index_off = le32(bo + 12);
			if (index_off + 8 > size) continue;
			track_t *t = &d->tracks[d->num_tracks++];
			t->number = point;
			t->audio = (mode & 0x0f) == 0x09;
			snprintf(t->path, sizeof(t->path), "%s", mdf);
			t->sector_size = le16(bo + 16);
			t->offset = (uint64_t)le32(bo + 40) | (uint64_t)le32(bo + 44) << 32;
			t->length = le32(b + index_off + 4);
		}
	}
	free(b);
	return d->num_tracks > 0 || fail(d, "No tracks in %s", mds);
}

static bool tracks_from_cue(disc_t *d, const char *cue) {
	FILE *f = fopen(cue, "r");
	if (!f) return fail(d, "Cannot read %s", cue);
	char dir[1024], file[1024] = "", line[1024];
	snprintf(dir, sizeof(dir), "%s", cue);
	char *cut = strrchr(dir, '\\'), *cut2 = strrchr(dir, '/');
	if (cut2 > cut) cut = cut2;
	if (cut) cut[1] = 0; else dir[0] = 0;
	track_t *pending = NULL;
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (isspace((unsigned char)*p)) p++;
		if (!strncasecmp_portable(p, "FILE", 4)) {
			char *q = strchr(p, '"'), *q2 = q ? strchr(q + 1, '"') : NULL;
			char name[1024];
			if (q && q2) {
				snprintf(name, sizeof(name), "%.*s", (int)(q2 - q - 1), q + 1);
			} else {
				sscanf(p + 4, " %1023s", name);
			}
			snprintf(file, sizeof(file), "%s%s", strchr(name, ':') || name[0] == '/' ? "" : dir, name);
		} else if (!strncasecmp_portable(p, "TRACK", 5) && d->num_tracks < MAX_TRACKS) {
			int n = 0;
			char mode[32] = "";
			sscanf(p + 5, " %d %31s", &n, mode);
			pending = &d->tracks[d->num_tracks++];
			pending->number = n;
			pending->audio = !strncasecmp_portable(mode, "AUDIO", 5);
			pending->sector_size = pending->audio || strstr(mode, "2352") ? 2352 : 2048;
			snprintf(pending->path, sizeof(pending->path), "%s", file);
		} else if (!strncasecmp_portable(p, "INDEX", 5) && pending) {
			int idx = -1, m = 0, s = 0, fr = 0;
			if (sscanf(p + 5, " %d %d:%d:%d", &idx, &m, &s, &fr) == 4 && idx == 1) {
				pending->offset = (uint64_t)((m * 60 + s) * 75 + fr) * (uint64_t)pending->sector_size;
			}
		}
	}
	fclose(f);
	for (int i = 0; i < d->num_tracks; i++) { // up to the next track of the same file, or its end
		track_t *t = &d->tracks[i];
		uint64_t end = file_size(t->path);
		for (int j = i + 1; j < d->num_tracks; j++) {
			if (!strcmp(d->tracks[j].path, t->path)) {
				end = d->tracks[j].offset;
				break;
			}
		}
		t->length = end > t->offset ? (uint32_t)((end - t->offset) / (uint64_t)t->sector_size) : 0;
	}
	return d->num_tracks > 0 || fail(d, "No tracks in %s", cue);
}

// a single data track: .iso (2048), raw .bin / .img / .mdf (2352 or 2448 with subchannel)
static bool tracks_single(disc_t *d, const char *path) {
	uint64_t size = file_size(path);
	if (!size) return fail(d, "Cannot read %s", path);
	track_t *t = &d->tracks[d->num_tracks++];
	t->number = 1;
	snprintf(t->path, sizeof(t->path), "%s", path);
	t->sector_size = 2048;
	FILE *f = fopen(path, "rb");
	uint8_t head[16];
	if (f && fread(head, 1, 16, f) == 16 && !memcmp(head, "\0\377\377\377\377\377\377\377\377\377\377\0", 12)) {
		t->sector_size = size % 2448 == 0 && size % 2352 != 0 ? 2448 : 2352;
	}
	if (f) fclose(f);
	t->length = (uint32_t)(size / (uint64_t)t->sector_size);
	return true;
}

// ---------------------------------------------------------------- data track sectors

static bool read_sectors(disc_t *d, uint32_t lba, uint8_t *out, uint32_t count) {
	const track_t *t = d->data;
	if (lba + count > t->length) return false;
	if (t->sector_size == SECTOR) {
		file_seek(d->f, t->offset + (uint64_t)lba * SECTOR);
		return fread(out, SECTOR, count, d->f) == count;
	}
	uint8_t raw[2448];
	file_seek(d->f, t->offset + (uint64_t)lba * (uint64_t)t->sector_size);
	for (uint32_t i = 0; i < count; i++) {
		if (fread(raw, 1, (size_t)t->sector_size, d->f) != (size_t)t->sector_size) return false;
		// mode 2 (form 1): 8 bytes of subheader after the 16-byte header; mode 1: data at 16
		memcpy(out + i * SECTOR, raw + (raw[15] == 2 ? 24 : 16), SECTOR);
	}
	return true;
}

// ---------------------------------------------------------------- ISO 9660 / Joliet

static bool progress(disc_t *d, const char *what) {
	if (d->progress && !d->progress(what, d->done, d->total, d->user)) {
		d->stopped = true;
		return fail(d, "Stopped");
	}
	return true;
}

static bool extract_file(disc_t *d, uint32_t extent, uint32_t size, const char *path) {
	FILE *o = fopen(path, "wb");
	if (!o) return fail(d, "Cannot write %s", path);
	uint8_t *buf = malloc(64 * SECTOR);
	uint32_t left = size, lba = extent;
	bool ok = true;
	while (left && ok) {
		uint32_t n = min((left + SECTOR - 1) / SECTOR, 64u);
		ok = read_sectors(d, lba, buf, n);
		uint32_t bytes = min(left, n * SECTOR);
		ok = ok && fwrite(buf, 1, bytes, o) == bytes;
		left -= bytes;
		lba += n;
		d->done += bytes;
		if (ok && (lba & 0x1ff) == 0 && !progress(d, "Copying the game files")) break;
	}
	free(buf);
	fclose(o);
	d->files++;
	if (d->stopped) return false;
	return ok || fail(d, "The image is damaged or incomplete (reading %s)", path);
}

// walk = 0: count the bytes (for the progress bar), 1: extract
static bool walk_dir(disc_t *d, uint32_t extent, uint32_t size, const char *out, int pass, int depth) {
	if (depth > 16 || size > 1 << 22) return fail(d, "Unexpected directory structure in the image");
	uint32_t sectors = (size + SECTOR - 1) / SECTOR;
	uint8_t *data = malloc((size_t)sectors * SECTOR);
	if (!read_sectors(d, extent, data, sectors)) {
		free(data);
		return fail(d, "The image is damaged or incomplete (directory)");
	}
	bool ok = true;
	for (uint32_t pos = 0; pos < size && ok;) {
		uint8_t len = data[pos];
		if (len == 0) {
			pos = (pos / SECTOR + 1) * SECTOR;
			continue;
		}
		const uint8_t *r = data + pos;
		pos += len;
		int nlen = r[32];
		if (nlen == 1 && (r[33] == 0 || r[33] == 1)) continue;   // "." and ".."
		char name[256];
		int n = 0;
		if (d->joliet) {
			for (int i = 0; i + 1 < nlen && n < 255; i += 2) {
				int c = r[33 + i] << 8 | r[34 + i];
				name[n++] = c < 0x80 ? (char)c : '_';
			}
		} else {
			for (int i = 0; i < nlen && n < 255; i++) name[n++] = (char)r[33 + i];
		}
		name[n] = 0;
		char *semi = strchr(name, ';');
		if (semi) *semi = 0;
		if (!name[0] || strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) continue;
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", out, name);
		uint32_t ext = le32(r + 2), sz = le32(r + 10);
		if (r[25] & 2) {
			if (pass) make_dir(path);
			ok = walk_dir(d, ext, sz, path, pass, depth + 1);
		} else if (pass) {
			ok = extract_file(d, ext, sz, path);
		} else {
			d->total += sz;
		}
	}
	free(data);
	return ok;
}

static bool root_has(disc_t *d, uint32_t extent, uint32_t size, const char *want) {
	uint8_t *data = malloc(((size + SECTOR - 1) / SECTOR) * SECTOR);
	bool found = false;
	if (read_sectors(d, extent, data, (size + SECTOR - 1) / SECTOR)) {
		for (uint32_t pos = 0; pos < size && !found;) {
			uint8_t len = data[pos];
			if (len == 0) {
				pos = (pos / SECTOR + 1) * SECTOR;
				continue;
			}
			const uint8_t *r = data + pos;
			pos += len;
			char name[64];
			int n = 0;
			for (int i = 0; i < r[32] && n < 63; i += d->joliet ? 2 : 1) {
				name[n++] = (char)toupper(d->joliet ? r[34 + i] : r[33 + i]);
			}
			name[n] = 0;
			found = !strcmp(name, want);
		}
	}
	free(data);
	return found;
}

// ---------------------------------------------------------------- audio

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static bool write_wav(disc_t *d, const track_t *t, const char *path) {
	FILE *in = fopen(t->path, "rb"), *o = fopen(path, "wb");
	if (!in || !o) {
		if (in) fclose(in);
		if (o) fclose(o);
		return fail(d, "Cannot write %s", path);
	}
	uint32_t bytes = t->length * 2352u;
	uint8_t h[44] = "RIFF....WAVEfmt ";
	put32(h + 4, 36 + bytes);
	put32(h + 16, 16);
	h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;           // PCM, stereo
	put32(h + 24, 44100);
	put32(h + 28, 44100 * 4);
	h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;          // block 4, 16 bits
	memcpy(h + 36, "data", 4);
	put32(h + 40, bytes);
	fwrite(h, 1, 44, o);
	file_seek(in, t->offset);
	uint8_t raw[2448];
	bool ok = true;
	for (uint32_t i = 0; i < t->length && ok; i++) {
		ok = fread(raw, 1, (size_t)t->sector_size, in) == (size_t)t->sector_size && fwrite(raw, 1, 2352, o) == 2352;
		d->done += 2352;
		if (ok && (i & 0x3ff) == 0 && !progress(d, "Copying the music")) break;
	}
	fclose(in);
	fclose(o);
	if (d->stopped) return false;
	return ok || fail(d, "The image is damaged or incomplete (music track %d)", t->number);
}

// ---------------------------------------------------------------- main

bool disc_extract(const char *image, const char *out_dir, disc_progress_fn progress_fn, void *user,
	char *err, size_t err_size) {
	disc_t *d = calloc(1, sizeof(disc_t));
	d->err = err;
	d->err_size = err_size;
	d->progress = progress_fn;
	d->user = user;
	err[0] = 0;
	bool ok;
	char mds[1024];
	if (ext_is(image, ".mds")) {
		ok = tracks_from_mds(d, image);
	} else if (ext_is(image, ".mdf") && sibling(image, ".mds", mds, sizeof(mds))) {
		ok = tracks_from_mds(d, mds);
	} else if (ext_is(image, ".cue")) {
		ok = tracks_from_cue(d, image);
	} else if (ext_is(image, ".bin") || ext_is(image, ".img")) {
		char cue[1024];
		ok = sibling(image, ".cue", cue, sizeof(cue)) ? tracks_from_cue(d, cue) : tracks_single(d, image);
	} else if (disc_is_image(image)) {
		ok = tracks_single(d, image);
	} else {
		ok = fail(d, "%s is not a disc image (.iso, .cue/.bin, .mds/.mdf)", image);
	}
	for (int i = 0; ok && i < d->num_tracks && !d->data; i++) {
		if (!d->tracks[i].audio) d->data = &d->tracks[i];
	}
	if (ok && !d->data) ok = fail(d, "The image has no data track");
	if (ok && !(d->f = fopen(d->data->path, "rb"))) ok = fail(d, "Cannot read %s", d->data->path);

	// volume descriptors: the primary one, or the Joliet supplementary one (UCS-2 names)
	uint8_t vd[SECTOR], pvd[SECTOR], jvd[SECTOR];
	bool have_pvd = false, have_joliet = false;
	for (uint32_t lba = 16; ok && lba < 32 && read_sectors(d, lba, vd, 1); lba++) {
		if (memcmp(vd + 1, "CD001", 5) || vd[0] == 255) break;
		if (vd[0] == 1) {
			memcpy(pvd, vd, SECTOR);
			have_pvd = true;
		} else if (vd[0] == 2 && vd[88] == '%' && vd[89] == '/' && strchr("@CE", vd[90])) {
			memcpy(jvd, vd, SECTOR);
			have_joliet = true;
		}
	}
	if (ok && !have_pvd && !have_joliet) ok = fail(d, "No ISO 9660 file system in the image's data track");
	const uint8_t *root = ok ? (have_joliet ? jvd : pvd) + 156 : NULL;
	d->joliet = have_joliet;
	uint32_t root_extent = root ? le32(root + 2) : 0, root_size = root ? le32(root + 10) : 0;
	if (ok && !root_has(d, root_extent, root_size, "MISSIONS")) {
		ok = fail(d, "This is not a Lander disc (no MISSIONS folder on it)");
	}

	if (ok) { // the sizes first, for the progress bar
		ok = walk_dir(d, root_extent, root_size, out_dir, 0, 0);
		for (int i = 0; i < d->num_tracks; i++) {
			if (d->tracks[i].audio) d->total += d->tracks[i].length * 2352ull;
		}
	}
	if (ok) {
		make_dir(out_dir);
		ok = progress(d, "Copying the game files") && walk_dir(d, root_extent, root_size, out_dir, 1, 0);
	}
	if (ok) {
		char music[1024];
		snprintf(music, sizeof(music), "%s/MUSIC", out_dir);
		make_dir(music);
		for (int i = 0; ok && i < d->num_tracks; i++) {
			const track_t *t = &d->tracks[i];
			if (!t->audio) continue;
			char path[1100];
			snprintf(path, sizeof(path), "%s/TRACK%02d.WAV", music, t->number);
			ok = write_wav(d, t, path);
		}
	}
	if (ok) ok = progress(d, "Done");
	if (d->f) fclose(d->f);
	free(d);
	return ok;
}
