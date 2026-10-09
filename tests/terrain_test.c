// Loads zonedata.dat / tiledata.dat of the given missions and prints a summary
// (for cross-checking against re/tools/terrain_parse.py).
// usage: terrain_test <game data dir> <mission dir>...   e.g. MISSIONS/MARS/1A
#include <stdio.h>
#include <stdlib.h>

#include "../src/terrain.h"
#include "../src/fs.h"

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s <game data dir> <mission dir>...\n", argv[0]);
		return 1;
	}
	fs_set_root(argv[1]);
	int failures = 0;
	for (int a = 2; a < argc; a++) {
		char path[512];
		snprintf(path, sizeof(path), "%s/ZONEDATA.DAT", argv[a]);
		zonedata_t *zd = zonedata_load(path);
		snprintf(path, sizeof(path), "%s/TILEDATA.DAT", argv[a]);
		tiledata_t *td = tiledata_load(path);
		if (!zd || !td) {
			printf("%-22s missing data\n", argv[a]);
			failures++;
			continue;
		}
		int ext = zd->portal_count[0], twins = 0;
		for (int i = 0; i < zd->num_portals; i++) {
			portal_t *p = &zd->portals[i];
			int t = p->target;
			int first = zd->portal_first[t + 1], count = zd->portal_count[t + 1];
			// find the twin: a portal of the target zone that leads back to this portal's owner
			for (int k = first; k < first + count; k++) {
				portal_t *q = &zd->portals[k];
				int dx = q->center.x - p->center.x, dy = q->center.y - p->center.y, dz = q->center.z - p->center.z;
				if ((long long)dx * dx + (long long)dy * dy + (long long)dz * dz < 3000LL * 3000LL) {
					twins++;
					break;
				}
			}
		}
		tile_cell_t cells[16];
		int center = tiledata_lookup(td, 0, 0, cells, 16);
		printf("%-22s zones=%3d portals=%4d (exterior %2d, twins %4d) planes=%4d pvs0=%2d | "
			"regions=%d+%d repeats=%d overflow=%d  cell(0,0): n=%d model=%d rot=%d y=%d\n",
			argv[a], zd->num_zones, zd->num_portals, ext, twins, zd->num_planes,
			zd->num_zones ? zd->pvs_count[1] : 0, td->num_regions, td->num_all_regions - td->num_regions,
			td->num_repeats, td->num_overflow, center, center ? cells[0].model : -1,
			center ? cells[0].rotation : -1, center ? cells[0].y : 0);
		zonedata_free(zd);
		tiledata_free(td);
	}
	return failures != 0;
}
