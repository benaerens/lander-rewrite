#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fs.h"

#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#endif

static char root[512] = ".";

void fs_set_root(const char *dir) {
	snprintf(root, sizeof(root), "%s", dir);
	size_t n = strlen(root);
	while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\')) {
		root[--n] = 0;
	}
}

const char *fs_root(void) { return root; }

#ifdef _WIN32

bool fs_resolve(const char *path, char *out, size_t out_size) {
	int n = snprintf(out, out_size, "%s\\%s", root, path);
	if (n < 0 || (size_t)n >= out_size) {
		return false;
	}
	for (char *c = out; *c; c++) {
		if (*c == '/') *c = '\\';
	}
	struct stat st; // files and folders (fopen cannot open a folder on Windows)
	return stat(out, &st) == 0;
}

bool fs_list(const char *dir, fs_list_fn fn, void *user) {
	char pattern[1024];
	snprintf(pattern, sizeof(pattern), "%s\\*", dir);
	struct _finddata_t fd;
	intptr_t h = _findfirst(pattern, &fd);
	if (h == -1) {
		return false;
	}
	do {
		if (strcmp(fd.name, ".") && strcmp(fd.name, "..")) {
			fn(fd.name, (fd.attrib & _A_SUBDIR) != 0, user);
		}
	} while (_findnext(h, &fd) == 0);
	_findclose(h);
	return true;
}

void fs_make_dir(const char *dir) { _mkdir(dir); }

#else

bool fs_list(const char *dir, fs_list_fn fn, void *user) {
	DIR *d = opendir(dir);
	if (!d) {
		return false;
	}
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		struct stat st;
		fn(e->d_name, stat(path, &st) == 0 && S_ISDIR(st.st_mode), user);
	}
	closedir(d);
	return true;
}

void fs_make_dir(const char *dir) { mkdir(dir, 0755); }

// Walk the path one component at a time, matching each case-insensitively.
bool fs_resolve(const char *path, char *out, size_t out_size) {
	char cur[1024];
	snprintf(cur, sizeof(cur), "%s", root);
	const char *p = path;
	while (*p) {
		while (*p == '\\' || *p == '/') p++;
		if (!*p) break;
		char part[256];
		size_t n = 0;
		while (*p && *p != '\\' && *p != '/' && n < sizeof(part) - 1) {
			part[n++] = *p++;
		}
		part[n] = 0;
		DIR *d = opendir(cur);
		if (!d) {
			return false;
		}
		struct dirent *e;
		bool found = false;
		while ((e = readdir(d))) {
			if (strcasecmp(e->d_name, part) == 0) {
				size_t len = strlen(cur);
				snprintf(cur + len, sizeof(cur) - len, "/%s", e->d_name);
				found = true;
				break;
			}
		}
		closedir(d);
		if (!found) {
			return false;
		}
	}
	if (strlen(cur) >= out_size) {
		return false;
	}
	strcpy(out, cur);
	return true;
}

#endif

uint8_t *fs_load(const char *path, uint32_t *size_out) {
	char real[1024];
	if (!fs_resolve(path, real, sizeof(real))) {
		return NULL;
	}
	FILE *f = fopen(real, "rb");
	if (!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *data = malloc(size > 0 ? size : 1);
	if (data && fread(data, 1, size, f) != (size_t)size) {
		free(data);
		data = NULL;
	}
	fclose(f);
	if (data && size_out) {
		*size_out = (uint32_t)size;
	}
	return data;
}

bool fs_exists(const char *path) {
	char real[1024];
	return fs_resolve(path, real, sizeof(real));
}
