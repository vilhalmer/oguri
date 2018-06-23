#ifndef _OGURI_H
#define _OGURI_H

#include <poll.h>
#include <wayland-client.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "cairo.h"

enum oguri_events {
	OGURI_WAYLAND_EVENT,
	OGURI_TIMER_EVENT,
	OGURI_EVENT_COUNT,  // last
};

struct oguri_state {
	char * output_name;
	char * image_path;

	bool run;
	struct pollfd events[OGURI_EVENT_COUNT];

	struct wl_display * display;
	struct wl_compositor * compositor;
	struct wl_shm * shm;

	struct wl_list outputs;  // oguri_output::link
	struct oguri_output * selected_output;

	struct zwlr_layer_shell_v1 * layer_shell;
	struct zxdg_output_manager_v1 * output_manager;
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

// Helpers to define no-op listener members without angering the compiler:

#define _incomplete_listener \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")

#define _end_incomplete_listener _Pragma("GCC diagnostic pop")

#endif
