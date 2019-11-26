#ifndef _OGURI_ANIMATION_H
#define _OGURI_ANIMATION_H

#include <poll.h>
#include <cairo.h>
#include <wayland-client.h>

#include "cairo-pixbuf.h"
#include "config.h"

struct oguri_state;

struct oguri_animation {
	struct oguri_state * oguri;
	struct wl_list link;

	// In order to update our timer for each frame, we keep a reference to the
	// timerfd that we placed into oguri::events.
	int timerfd;
	int event_index;

	char * path;
	GdkPixbufAnimation * image;
	GdkPixbufAnimationIter * frame_iter;
	cairo_surface_t * source_surface;

	bool first_cycle;
	unsigned int frame_count;

	struct wl_list outputs;  // oguri_output::link
};

int oguri_render_frame(struct oguri_animation * anim);
bool oguri_animation_schedule_frame(
		struct oguri_animation * anim, unsigned int delay);
struct oguri_animation * oguri_animation_create(
		struct oguri_state * oguri, char * image_path);
void oguri_animation_destroy(struct oguri_animation * anim);

#endif
