#ifndef _OGURI_BUFFERS_H
#define _OGURI_BUFFERS_H

#define CAIRO_FMT CAIRO_FORMAT_ARGB32

#include <cairo.h>
#include <wayland-client.h>

#include "output.h"

struct oguri_buffer {
	struct wl_list link;  // oguri_output::buffer_ring;

	bool busy;

	struct wl_buffer * backing;
	cairo_t * cairo;
	cairo_surface_t * cairo_surface;

	void * data;
	size_t size;
};

struct oguri_buffer * oguri_allocate_buffer(struct oguri_output * output);
bool oguri_allocate_buffers(struct oguri_output * output, unsigned int count);
struct oguri_buffer * oguri_next_buffer(struct oguri_output * output);
void oguri_buffer_destroy(struct oguri_buffer * buffer);

#endif
