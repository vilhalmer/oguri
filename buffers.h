#ifndef _OGURI_BUFFERS_H
#define _OGURI_BUFFERS_H

#include <wayland-client.h>
#include "cairo.h"

struct oguri_buffer {
	struct wl_list link;  // oguri_state::buffer_ring;

	bool busy;

	struct wl_buffer * backing;
	cairo_t * cairo;
	cairo_surface_t * cairo_surface;

	void * data;
};

struct oguri_buffer * oguri_allocate_buffer(struct oguri_state * oguri);
bool oguri_allocate_buffers(struct oguri_state * oguri);
struct oguri_buffer * next_buffer(struct oguri_state * oguri);

#endif
