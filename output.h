#ifndef _OGURI_OUTPUT_H
#define _OGURI_OUTPUT_H

#include <wayland-client.h>
#include <cairo.h>

struct oguri_state;

enum oguri_anchor_x {
	OGURI_CENTER_X = 0,
	OGURI_LEFT,
	OGURI_RIGHT,
};

enum oguri_anchor_y {
	OGURI_CENTER_Y = 0,
	OGURI_TOP,
	OGURI_BOTTOM,
};

struct oguri_output {
	struct oguri_state * oguri;
	struct wl_list link;  // oguri_state::outputs

	char * name;
	struct wl_output * output;

	struct wl_surface * surface;
	struct wl_region * input_region;
	struct zwlr_layer_surface_v1 * layer_surface;

	uint32_t width;
	uint32_t height;
	int32_t scale;

	struct wl_shm * shm; // Same as on oguri_state, used to allocate buffers.
	struct wl_list buffer_ring;  // oguri_buffer::link
};

struct oguri_output * oguri_output_create(
		struct oguri_state * oguri, struct wl_output * wl_output);
void oguri_output_destroy(struct oguri_output * output);

#endif
