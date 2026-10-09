// Renderer smoke test: draws textured, fogged cubes using real textures from
// the game data and writes a PNG.
// usage: render_test <game data dir> <out.png>
#include <stdio.h>
#include <stdlib.h>

#include "../src/render.h"
#include "../src/texfile.h"
#include "../src/fs.h"
#include "../src/png.h"

static void cube(float s, rgba_t c, vertex_t *out) {
	static const int faces[6][4] = {
		{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3},
	};
	vec3_t p[8];
	for (int i = 0; i < 8; i++) {
		p[i] = vec3(i & 1 ? s : -s, i & 2 ? s : -s, i & 4 ? s : -s);
	}
	int n = 0;
	for (int f = 0; f < 6; f++) {
		const int *q = faces[f];
		vertex_t v[4] = {
			{p[q[0]], 0, 0, c}, {p[q[1]], 1, 0, c}, {p[q[2]], 1, 1, c}, {p[q[3]], 0, 1, c},
		};
		out[n++] = v[0]; out[n++] = v[1]; out[n++] = v[2];
		out[n++] = v[0]; out[n++] = v[2]; out[n++] = v[3];
	}
}

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s <game data dir> <out.png>\n", argv[0]);
		return 1;
	}
	fs_set_root(argv[1]);
	texfile_t *tf = texfile_load("MISSIONS\\MARS\\TEXFILE\\PCPURE\\TEXFILE");
	if (!tf) {
		fprintf(stderr, "could not load the Mars texture file\n");
		return 1;
	}
	printf("textures: %d\n", tf->count);

	render_init(640, 480);
	render_frame_begin(rgba(90, 40, 20, 255));
	render_set_fog(true, rgba(90, 40, 20, 255), 300, 1400);
	render_set_view(mat4_look_at(vec3(0, 150, -500), vec3(0, 0, 200), vec3(0, 1, 0)));

	vertex_t v[36];
	cube(60, rgba(255, 255, 255, 255), v);
	int k = 0;
	for (int z = 0; z < 6; z++) {
		for (int x = -3; x <= 3; x++) {
			mat4_t m = mat4_mul(mat4_translate(vec3(x * 160, 0, z * 220)),
				mat4_mul(mat4_rotate_y(0.3f * x + z), mat4_rotate_x(0.2f * z)));
			render_set_model(m);
			tex_entry_t *te = &tf->entries[(k++ * 7) % tf->count];
			render_tris(v, 36, &te->frame[0], te->flags == 0 ? BLEND_ALPHA_TEST : BLEND_OPAQUE);
		}
	}
	// a translucent additive cube in front
	render_set_model(mat4_mul(mat4_translate(vec3(0, 60, -250)), mat4_rotate_y(0.7f)));
	cube(50, rgba(255, 255, 255, 160), v);
	render_set_depth(true, false);
	render_tris(v, 36, &tf->entries[3].frame[4], BLEND_ADD);
	render_set_depth(true, true);

	// 2D: the HUD map texture in a corner
	tex_entry_t *last = &tf->entries[tf->count - 1];
	render_quad_2d(8, 8, 128, 128, 0, 0, 1, 1, rgba(255, 255, 255, 255), &last->frame[0], BLEND_ALPHA);

	png_write(argv[2], render_pixels(), render_width(), render_height());
	printf("wrote %s\n", argv[2]);
	texfile_free(tf);
	render_cleanup();
	return 0;
}
