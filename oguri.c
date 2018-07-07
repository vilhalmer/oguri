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
#include "cairo.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "oguri.h"
#include "output.h"
#include "buffers.h"


static void noop() {}  // For unused listener members.


G_DEFINE_QUARK(oguri_new_frame, oguri_new_frame);

bool oguri_is_first_cycle(GdkPixbuf * image) {
	return g_object_get_qdata(G_OBJECT(image), oguri_new_frame_quark()) ==
		&oguri_is_first_cycle;
}

void oguri_mark_first_cycle(GdkPixbuf * image) {
	g_object_set_qdata(
			G_OBJECT(image), oguri_new_frame_quark(), &oguri_is_first_cycle);
}


void scale_image_onto(
		cairo_t * cairo,
		cairo_surface_t * source,
		int32_t buffer_width,
		int32_t buffer_height,
		enum oguri_anchor_x anchor_x,
		enum oguri_anchor_y anchor_y) {
	// TODO: I guess I should implement the other scaling modes as well even
	// though fill is the only correct one.
	double width = cairo_image_surface_get_width(source);
	double height = cairo_image_surface_get_height(source);

	double window_ratio = (double)buffer_width / buffer_height;
	double bg_ratio = width / height;

	cairo_save(cairo);

	double scale;
	if (window_ratio > bg_ratio) {
		scale = (double)buffer_width / width;
		cairo_scale(cairo, scale, scale);

		double offset = 0;
		switch (anchor_y) {
		case OGURI_CENTER_Y:
			offset = ((double)buffer_height / 2 / scale) - (height / 2);
			break;
		case OGURI_TOP:
			offset = 0.0;
			break;
		case OGURI_BOTTOM:
			offset = ((double)buffer_height / scale) - height;
			break;
		}

		cairo_set_source_surface(cairo, source, 0, offset);
	} else {
		scale = (double)buffer_height / height;
		cairo_scale(cairo, scale, scale);

		double offset = 0;
		switch (anchor_x) {
		case OGURI_CENTER_X:
			offset = ((double)buffer_width / 2 / scale) - (width / 2);
			break;
		case OGURI_LEFT:
			offset = 0.0;
			break;
		case OGURI_RIGHT:
			offset = ((double)buffer_width / scale) - width;
			break;
		}

		cairo_set_source_surface(cairo, source, offset, 0);
	}

	cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_NEAREST);
	cairo_paint(cairo);

	cairo_restore(cairo);
}

void render_frame(struct oguri_output * output) {
	struct oguri_buffer * buffer = next_buffer(output);
	cairo_t *cairo = buffer->cairo;

	GdkPixbuf * image = gdk_pixbuf_animation_iter_get_pixbuf(output->frame_iter);

	// Draw the frame into our source surface, at its native size.
	gdk_cairo_set_source_pixbuf(output->source_cairo, image, 0, 0);
	cairo_paint(output->source_cairo);

	// Now scale that source surface onto the destination.
	scale_image_onto(
			cairo, output->source_surface, output->width, output->height,
			OGURI_CENTER_X, OGURI_CENTER_Y);

	wl_surface_set_buffer_scale(output->surface, output->scale);
	wl_surface_attach(output->surface, buffer->backing, 0, 0);
	wl_surface_damage(output->surface, 0, 0, output->width, output->height);
	wl_surface_commit(output->surface);
}

bool oguri_load_image(struct oguri_output * output, const char * path) {
	GError * error = NULL;
	output->image = gdk_pixbuf_animation_new_from_file(path, &error);
	if (!output->image) {
		fprintf(stderr, "Could not open image '%s'", path);
		return false;
	}

	output->frame_iter = gdk_pixbuf_animation_get_iter(output->image, NULL);
	GdkPixbuf * first = gdk_pixbuf_animation_iter_get_pixbuf(output->frame_iter);

	// This is undocumented at best, but the first time through the animation,
	// every frame is stored in the same pixbuf object. This means that we can
	// figure out when we've completed an entire cycle by tracking this object
	// via qdata. There is no supported way to determine when you've completed
	// a cycle, so this will have to do.
	oguri_mark_first_cycle(first);

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

struct wl_output_listener output_listener = {
	.scale = handle_output_scale,
	.geometry = noop,
	.mode = noop,
	.done = noop,
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

struct zxdg_output_v1_listener xdg_output_listener = {
	.name = handle_xdg_output_name,
	.done = handle_xdg_output_done,
	.logical_position = noop,
	.logical_size = noop,
	.description = noop,
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
	struct oguri_output * output = data;

	output->width = width;
	output->height = height;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (!oguri_allocate_buffers(output)) {
		fprintf(stderr, "No buffers, woe is me\n");
	}
}

static void layer_surface_closed(
		void * data,
		struct zwlr_layer_surface_v1 * layer_surface __attribute__((unused))) {
	oguri_output_destroy((struct oguri_output *) data);
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
		struct oguri_output * output = oguri_output_create(oguri);
		output->output = wl_registry_bind(
				registry, name, &wl_output_interface, 3);
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

static const struct wl_registry_listener registry_listener = {
	.global = handle_registry,
	.global_remove = noop,
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

static const char usage[] = "Usage: oguri [-s] <output> <path>\n";

static bool parse_int(const char *s, int *out) {
	errno = 0;
	char *end;
	*out = (int)strtol(s, &end, 10);
	return errno == 0 && end[0] == '\0';
}

int main(int argc, char * argv[]) {
	if (argc < 3) {
		printf(usage);
		return 1;
	}

	struct oguri_state oguri = {0};
	wl_list_init(&oguri.outputs);

	bool swaybg_compat = false;

	int argi = 1;
	if (strcmp(argv[argi], "-s") == 0) {
		swaybg_compat = true;
		++argi;
	}

	char * output_name = argv[argi++];
	char * image_path = argv[argi++];

	if (argi < argc && swaybg_compat) {
		fprintf(stderr, "Note: Scaling modes are not yet implemented\n");
	}

	oguri.display = wl_display_connect(NULL);
	assert(oguri.display);

	struct wl_registry *registry = wl_display_get_registry(oguri.display);
	wl_registry_add_listener(registry, &registry_listener, &oguri);
	wl_display_roundtrip(oguri.display);
	assert(oguri.compositor && oguri.layer_shell && oguri.shm);

	// Second roundtrip to get output properties.
	wl_display_roundtrip(oguri.display);

	// Parse the requested output as a number if we're in swaybg mode.
	// Note that we'll still fall back to parsing it as a name, even though
	// we'd never expect to get that from sway.
	int output_number = -1;
	struct oguri_output * output;

	if (swaybg_compat && parse_int(output_name, &output_number)) {
		int i = 0;
		wl_list_for_each(output, &oguri.outputs, link) {
			if (i == output_number) {
				oguri.selected_output = output;
				break;
			}
			++i;
		}
	}

	// If the output wasn't a number, we have to look up all the names.
	if (!oguri.selected_output) {
		// Fetch the names of each available output so we can decide which one
		// we were instructed to draw onto.
		wl_list_for_each(output, &oguri.outputs, link) {
			struct zxdg_output_v1 * xdg_output = zxdg_output_manager_v1_get_xdg_output(
					oguri.output_manager, output->output);
			zxdg_output_v1_add_listener(
					xdg_output, &xdg_output_listener, output);
		}
		wl_display_roundtrip(oguri.display);

		// Now we can look for the one we wanted.
		wl_list_for_each(output, &oguri.outputs, link) {
			if (strcmp(output->name, output_name) == 0) {
				oguri.selected_output = output;
				break;
			}
		}
	}

	// If we still haven't found a matching output, RIP.
	if (!oguri.selected_output) {
		fprintf(stderr, "Could not find an output named '%s'\n",output_name);
		return 2;
	}

	if (!oguri_load_image(oguri.selected_output, image_path)) {
		return 3;
	}

	output = oguri.selected_output;

	output->source_surface = cairo_image_surface_create(CAIRO_FMT,
			gdk_pixbuf_animation_get_width(output->image),
			gdk_pixbuf_animation_get_height(output->image));
	output->source_cairo = cairo_create(output->source_surface);

	output->surface = wl_compositor_create_surface(oguri.compositor);
	assert(output->surface);

	output->input_region = wl_compositor_create_region(oguri.compositor);
	assert(output->input_region);
	wl_surface_set_input_region(output->surface, output->input_region);

	output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			oguri.layer_shell,
			output->surface,
			output->output,
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
			"wallpaper");

	assert(output->layer_surface);

	zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_anchor(output->layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
	zwlr_layer_surface_v1_add_listener(output->layer_surface,
			&layer_surface_listener, output);
	wl_surface_commit(output->surface);
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

			gdk_pixbuf_animation_iter_advance(
					oguri.selected_output->frame_iter, NULL);
			int delay = gdk_pixbuf_animation_iter_get_delay_time(
					oguri.selected_output->frame_iter);
			if (delay > 0) {
				set_timer_milliseconds(fd, (unsigned int)delay);
			}
			render_frame(oguri.selected_output);
		}
	}

	struct oguri_output * tmp;
	wl_list_for_each_safe(output, tmp, &oguri.outputs, link) {
		wl_list_remove(&output->link);
		oguri_output_destroy(output);
	}

	return 0;
}
