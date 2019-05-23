#ifndef _OGURI_OUTPUT_H
#define _OGURI_OUTPUT_H

#include <wayland-client.h>
#include <cairo.h>

#include "config.h"

struct oguri_state;

struct oguri_output {
	struct oguri_state * oguri;
	struct oguri_output_config * config;
	struct wl_list link;  // oguri_state::outputs

	char * name;
	struct wl_output * output;

	struct wl_surface * surface;
	struct zwlr_layer_surface_v1 * layer_surface;

	uint32_t width;
	uint32_t height;
	int32_t scale;

	unsigned int cached_frames;
	struct wl_list buffer_ring;  // oguri_buffer::link
	unsigned int buffer_count;
};

struct oguri_output * oguri_output_create(
		struct oguri_state * oguri, struct wl_output * wl_output);
void oguri_output_destroy(struct oguri_output * output);

#endif
