//                         _
//   ___   __ _ _   _ _ __(_)
//  / _ \ / _` | | | | '__| |
// | (_) | (_| | |_| | |  | |
//  \___/ \__, |\__,_|_|  |_|
//        |___/
//
// Because your battery life was too good.
//
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <gdk/gdk.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include "oguri.h"
#include "cairo.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "buffers.h"


void scale_image_onto(
		cairo_t * cairo,
		cairo_surface_t * source,
		int32_t buffer_width,
		int32_t buffer_height) {
	// TODO: I guess I should implement the other scaling modes as well even
	// though fill is the only correct one.
	double width = cairo_image_surface_get_width(source);
	double height = cairo_image_surface_get_height(source);

	double window_ratio = (double)buffer_width / buffer_height;
	double bg_ratio = width / height;

	double scale;
	if (window_ratio > bg_ratio) {
		scale = (double)buffer_width / width;
		cairo_scale(cairo, scale, scale);
		cairo_set_source_surface(cairo, source,
				0, (double)buffer_height / 2 / scale - height / 2);
	} else {
		scale = (double)buffer_height / height;
		cairo_scale(cairo, scale, scale);
		cairo_set_source_surface(cairo, source,
				(double)buffer_width / 2 / scale - width / 2, 0);
	}

	cairo_scale(cairo, 1 / scale, 1 / scale);
}

void render_frame(struct oguri_state * oguri) {
	struct oguri_buffer * buffer = next_buffer(oguri);
	cairo_t *cairo = buffer->cairo;

	GdkPixbuf * image = gdk_pixbuf_animation_iter_get_pixbuf(oguri->frame_iter);

	// Draw the frame into our source surface, at its native size.
	gdk_cairo_set_source_pixbuf(oguri->source_cairo, image, 0, 0);
	cairo_paint(oguri->source_cairo);

	// Now scale that source surface onto the destination.
	scale_image_onto(cairo, oguri->source_surface, oguri->width, oguri->height);

	cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_NEAREST);
	cairo_paint(cairo);

	wl_surface_set_buffer_scale(oguri->surface, oguri->selected_output->scale);
	wl_surface_attach(oguri->surface, buffer->backing, 0, 0);
	wl_surface_damage(oguri->surface, 0, 0, oguri->width, oguri->height);
	wl_surface_commit(oguri->surface);
}

bool oguri_load_image(struct oguri_state * oguri) {
	GError * error = NULL;
	oguri->image = gdk_pixbuf_animation_new_from_file(
			oguri->image_path, &error);
	if (!oguri->image) {
		fprintf(stderr, "Could not open image '%s'", oguri->image_path);
		return false;
	}

	oguri->frame_iter = gdk_pixbuf_animation_get_iter(oguri->image, NULL);
	return true;
}

//
// Wayland outputs
//

static void handle_output_scale(
		void * data,
		struct wl_output * output __attribute__((unused)),
		int32_t factor) {
	((struct oguri_output *) data)->scale = factor;
}

_incomplete_listener

static void handle_wl_output_geometry(
		void *data, struct wl_output *output, int32_t x, int32_t y,
		int32_t width_mm, int32_t height_mm, int32_t subpixel,
		const char *make, const char *model, int32_t transform) {}

static void handle_wl_output_mode(
		void *data, struct wl_output *output, uint32_t flags, int32_t width,
		int32_t height, int32_t refresh) {}

static void handle_wl_output_done(void *data, struct wl_output *output) {}

_end_incomplete_listener

struct wl_output_listener output_listener = {
	.scale = handle_output_scale,
	// no-ops:
	.geometry = handle_wl_output_geometry,
	.mode = handle_wl_output_mode,
	.done = handle_wl_output_done,
};

//
// XDG Output Manager
//

static void handle_xdg_output_name(
		void * data,
		struct zxdg_output_v1 * xdg_output __attribute__((unused)),
		const char *name) {
	((struct oguri_output *) data)->name = strdup(name);
}

static void handle_xdg_output_done(
		void * data __attribute__((unused)),
		struct zxdg_output_v1 * xdg_output) {
	// We have no further use for this object.
	zxdg_output_v1_destroy(xdg_output);
}

_incomplete_listener

static void handle_xdg_output_description(
		void *data, struct zxdg_output_v1 *output, const char *description) {}

static void handle_xdg_output_logical_size(
		void *data, struct zxdg_output_v1 *output, int width, int height) {}

static void handle_xdg_output_logical_position(
		void *data, struct zxdg_output_v1 *output, int x, int y) {}

_end_incomplete_listener

struct zxdg_output_v1_listener xdg_output_listener = {
	.name = handle_xdg_output_name,
	.done = handle_xdg_output_done,
	// no-ops:
	.logical_position = handle_xdg_output_logical_position,
	.logical_size = handle_xdg_output_logical_size,
	.description = handle_xdg_output_description,
};

//
// wlroots layer surface
//

static void layer_surface_configure(
		void * data,
		struct zwlr_layer_surface_v1 * layer_surface,
		uint32_t serial,
		uint32_t width,
		uint32_t height) {
	struct oguri_state * oguri = data;

	oguri->width = width;
	oguri->height = height;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (!oguri_allocate_buffers(oguri)) {
		fprintf(stderr, "No buffers, woe is me\n");
	}

	//render_frame(oguri);  // TODO: Testing timer
}

static void layer_surface_closed(
		void * data,
		struct zwlr_layer_surface_v1 * layer_surface) {
	struct oguri_state * oguri = data;

	zwlr_layer_surface_v1_destroy(layer_surface);
	wl_surface_destroy(oguri->surface);
	wl_region_destroy(oguri->input_region);
	oguri->run = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

//
// Wayland registry
//

static void handle_registry(
		void * data,
		struct wl_registry * registry,
		uint32_t name,
		const char * interface,
		uint32_t version __attribute__((unused))) {
	struct oguri_state * oguri = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		oguri->compositor = wl_registry_bind(
				registry, name, &wl_compositor_interface, 3);
	}
	else if (strcmp(interface, wl_shm_interface.name) == 0) {
		oguri->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	}
	else if (strcmp(interface, wl_output_interface.name) == 0) {
		// We have to keep track of all outputs, even though we will only use
		// one. This is because we can't create xdg_outputs from the wl_outputs
		// until later on, which means we don't know their names yet.
		struct oguri_output * output = calloc(1, sizeof(struct oguri_output));
		output->output = wl_registry_bind(
				registry, name, &wl_output_interface, 3);
		wl_list_insert(&oguri->outputs, &output->link);
		wl_output_add_listener(output->output, &output_listener, output);
	}
	else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		oguri->output_manager = wl_registry_bind(
				registry, name, &zxdg_output_manager_v1_interface, 2);
	}
	else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		oguri->layer_shell = wl_registry_bind(
				registry, name, &zwlr_layer_shell_v1_interface, 1);
	}
}

_incomplete_listener

static void handle_global_remove(
		void *data, struct wl_registry *registry, uint32_t name) {}

_end_incomplete_listener

static const struct wl_registry_listener registry_listener = {
	.global = handle_registry,
	// no-ops:
	.global_remove = handle_global_remove,
};

//
// Timers
//

bool set_timer_milliseconds(int timer_fd, unsigned int delay) {
	struct itimerspec spec = {
		.it_value = (struct timespec) {
			.tv_sec = delay / 1000,
			.tv_nsec = (delay % 1000) * (long)1000000,
		},
	};
	int ret = timerfd_settime(timer_fd, 0, &spec, NULL);
	if (ret < 0) {
		fprintf(stderr, "Timer error: %s\n", strerror(errno));
		return false;
	}

	return true;
}

//
// Main
//

static const char usage[] = "Usage: oguri <output> <path>\n";

int main(int argc, char * argv[]) {
	if (argc != 3) {
		printf(usage);
		return 1;
	}

	struct oguri_state oguri = {0};
	wl_list_init(&oguri.outputs);
	wl_list_init(&oguri.buffer_ring);

	oguri.output_name = argv[1];
	oguri.image_path = argv[2];

	if (!oguri_load_image(&oguri)) {
		return 2;
	}

	oguri.source_surface = cairo_image_surface_create(CAIRO_FMT,
			gdk_pixbuf_animation_get_width(oguri.image),
			gdk_pixbuf_animation_get_height(oguri.image));
	oguri.source_cairo = cairo_create(oguri.source_surface);

	oguri.display = wl_display_connect(NULL);
	assert(oguri.display);

	struct wl_registry *registry = wl_display_get_registry(oguri.display);
	wl_registry_add_listener(registry, &registry_listener, &oguri);
	wl_display_roundtrip(oguri.display);
	assert(oguri.compositor && oguri.layer_shell && oguri.shm);

	// Second roundtrip to get output properties.
	wl_display_roundtrip(oguri.display);

	// Fetch the names of each available output so we can decide which one we
	// were instructed to draw onto.
	struct oguri_output * output;
	wl_list_for_each(output, &oguri.outputs, link) {
		struct zxdg_output_v1 * xdg_output = zxdg_output_manager_v1_get_xdg_output(
				oguri.output_manager, output->output);
		zxdg_output_v1_add_listener(xdg_output, &xdg_output_listener, output);
	}
	wl_display_roundtrip(oguri.display);

	// Now we can look for the one we wanted.
	wl_list_for_each(output, &oguri.outputs, link) {
		if (strcmp(output->name, oguri.output_name) == 0) {
			oguri.selected_output = output;
			break;
		}
	}
	if (!oguri.selected_output) {
		fprintf(stderr, "Could not find an output named '%s'\n",
				oguri.output_name);
		return 3;
	}

	oguri.surface = wl_compositor_create_surface(oguri.compositor);
	assert(oguri.surface);

	oguri.input_region = wl_compositor_create_region(oguri.compositor);
	assert(oguri.input_region);
	wl_surface_set_input_region(oguri.surface, oguri.input_region);

	oguri.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			oguri.layer_shell,
			oguri.surface,
			oguri.selected_output->output,
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
			"wallpaper");

	assert(oguri.layer_surface);

	zwlr_layer_surface_v1_set_size(oguri.layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_anchor(oguri.layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_exclusive_zone(oguri.layer_surface, -1);
	zwlr_layer_surface_v1_add_listener(oguri.layer_surface,
			&layer_surface_listener, &oguri);
	wl_surface_commit(oguri.surface);
	wl_display_roundtrip(oguri.display);

	// Set up our poll descriptors.
	int polled = 0;
	struct pollfd events[] = {
		[OGURI_WAYLAND_EVENT] = (struct pollfd) {
			.fd = wl_display_get_fd(oguri.display),
			.events = POLLIN,
		},
		[OGURI_TIMER_EVENT] = (struct pollfd) {
			.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC),
			.events = POLLIN,
		}
	};

	// Prepare the animation iterator with the current time and imediately
	// schedule it for display.
	oguri.frame_iter = gdk_pixbuf_animation_get_iter(oguri.image, NULL);

	if (!set_timer_milliseconds(events[OGURI_TIMER_EVENT].fd, 1)) {  // ASAP
		fprintf(stderr, "Unable to schedule first timer\n");
	}

	oguri.run = true;
	while (oguri.run) {
		while (wl_display_prepare_read(oguri.display) != 0) {
			wl_display_dispatch_pending(oguri.display);
		}
		wl_display_flush(oguri.display);

		polled = poll(events, OGURI_EVENT_COUNT, -1);
		if (polled < 0) {
			wl_display_cancel_read(oguri.display);
			if (errno == EINTR) {
				// Keep going if this was a signal interrupt. The signal
				// handler will set oguri.run to false if appropriate.
				continue;
			}
			fprintf(stderr, "Poll failure: %s\n", strerror(-polled));
			break;
		}

		// Read wayland events first so we can handle any resizing, etc, before
		// attempting to draw again.
		if (events[OGURI_WAYLAND_EVENT].revents & POLLIN) {
			if (wl_display_read_events(oguri.display) != 0) {
				fprintf(stderr, "Failed to read Wayland events: %s\n",
					strerror(errno));
				break;
			}
		}
		else {
			wl_display_cancel_read(oguri.display);
		}

		// At this point, we may have been shut down. Might as well not waste
		// time drawing.
		if (!oguri.run) {
			break;
		}

		// Now see if we need to draw the next frame.
		if (events[OGURI_TIMER_EVENT].revents & POLLIN) {
			int fd = events[OGURI_TIMER_EVENT].fd;

			uint64_t expirations;
			ssize_t n = read(
					events[OGURI_TIMER_EVENT].fd,
					&expirations,
					sizeof(expirations));

			if (n < 0) {
				fprintf(stderr, "Failed to read timer events\n");
				break;
			}

			gdk_pixbuf_animation_iter_advance(oguri.frame_iter, NULL);
			int delay = gdk_pixbuf_animation_iter_get_delay_time(
					oguri.frame_iter);
			if (delay < 0) {
				continue;  // Static image, no need to do this again.
			}
			set_timer_milliseconds(fd, (unsigned int)delay);
			render_frame(&oguri);
		}
	}

	struct oguri_output * tmp;
	wl_list_for_each_safe(output, tmp, &oguri.outputs, link) {
		wl_list_remove(&output->link);
		free(output->name);
		free(output);
	}

	return 0;
}
