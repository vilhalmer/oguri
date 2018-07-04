#ifndef _OGURI_OUTPUT_H
#define _OGURI_OUTPUT_H

#include <wayland-client.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo.h>

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
	struct wl_list link;  // oguri_state::outputs

	char * name;
	struct wl_output * output;

	struct wl_surface * surface;
	struct wl_region * input_region;
	struct zwlr_layer_surface_v1 * layer_surface;

	GdkPixbufAnimation * image;
	GdkPixbufAnimationIter * frame_iter;

	// Caches the latest frame from the source
	cairo_surface_t * source_surface;
	cairo_t * source_cairo;

	uint32_t width;
	uint32_t height;
	int32_t scale;

	struct wl_shm * shm; // Same as on oguri_state, used to allocate buffers.
	struct wl_list buffer_ring;  // oguri_buffer::link
};

#endif
