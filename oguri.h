#ifndef _OGURI_H
#define _OGURI_H

#include <wayland-client.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "cairo.h"

struct oguri_state {
	char * output_name;
	char * image_path;

	bool run;

	struct wl_display * display;
	struct wl_compositor * compositor;
	struct wl_shm * shm;
	struct wl_surface * surface;
	struct wl_region * input_region;

	struct wl_list outputs;  // oguri_output::link
	struct oguri_output * selected_output;

	struct zxdg_output_manager_v1 * output_manager;
	struct zwlr_layer_shell_v1 * layer_shell;
	struct zwlr_layer_surface_v1 * layer_surface;

	uint32_t width;
	uint32_t height;

	GdkPixbufAnimation * image;
	GdkPixbufAnimationIter * frame_iter;

	struct wl_list buffer_ring;  // oguri_buffer::link
};

struct oguri_output {
	struct wl_list link;  // oguri_state::outputs

	char * name;
	struct wl_output * output;
	int32_t scale;
};

struct oguri_buffer {
	struct wl_list link;  // oguri_state::buffer_ring;

	bool busy;

	struct wl_buffer * backing;
	cairo_t * cairo;
	cairo_surface_t * cairo_surface;

	void * data;
};

struct oguri_buffer * next_buffer(struct oguri_state * oguri);

// Helpers to define no-op listener members without angering the compiler:

#define _incomplete_listener \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")

#define _end_incomplete_listener _Pragma("GCC diagnostic pop")

#endif
