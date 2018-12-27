#ifndef _OGURI_ANIMATION_H
#define _OGURI_ANIMATION_H

#include <wayland-client.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

struct oguri_state;

struct oguri_animation {
	struct oguri_state * oguri;
	struct wl_list link;

	char * path;
	GdkPixbufAnimation * image;
	GdkPixbufAnimationIter * frame_iter;
	cairo_surface_t * source_surface;
	cairo_t * source_cairo;
	cairo_filter_t filter;

	unsigned int frame_count;
	unsigned int frame_index;

	struct wl_list outputs;  // oguri_output::link
};

int oguri_render_frame(struct oguri_animation * anim);
struct oguri_animation * oguri_animation_create(
		struct oguri_state * oguri, const char * path);
void oguri_animation_destroy(struct oguri_animation * anim);

#endif
