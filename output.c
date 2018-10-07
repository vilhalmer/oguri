#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include "oguri.h"
#include "output.h"
#include "buffers.h"

static void noop() {}  // For unused listener members.

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

	// The entire surface can be marked opaque, as it should be the lowest
	// z-index on the display.
	struct wl_region * opaque = wl_compositor_create_region(
			output->oguri->compositor);
	assert(opaque);
	wl_region_add(opaque, 0, 0, output->width, output->height);
	wl_surface_set_opaque_region(output->surface, opaque);
	wl_region_destroy(opaque);

	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	if (!oguri_allocate_buffers(output)) {
		fprintf(stderr, "No buffers, woe is me\n");
	}
}

static void layer_surface_closed(
		void * data,
		struct zwlr_layer_surface_v1 * layer_surface __attribute__((unused))) {
	struct oguri_output * output = data;
	if (output->oguri->oneshot) {
		// We were instructed to shut down when our display disappeared.
		// However, this function is called for all of the displays we know
		// about, not just the one in use. So, we need to check if the one that
		// just disappeared was idle or not.
		//
		// We assume that it is active until we've found it in the idle list,
		// because there's no global list of active outputs.
		bool is_active = true;
		struct oguri_output * idle_output;
		wl_list_for_each(idle_output, &output->oguri->idle_outputs, link) {
			if (idle_output == output) {
				is_active = false;
				break;
			}
		}

		// If it was active, we're ready to exit. Otherwise, keep running.
		output->oguri->run = !is_active;
	}
	oguri_output_destroy(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
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
// Outputs
//

struct oguri_output * oguri_output_create(
		struct oguri_state * oguri, struct wl_output * wl_output) {
	struct oguri_output * output = calloc(1, sizeof(struct oguri_output));
	output->oguri = oguri;
	wl_list_init(&output->buffer_ring);

	output->output = wl_output;
	wl_output_add_listener(wl_output, &output_listener, output);

	output->surface = wl_compositor_create_surface(oguri->compositor);
	assert(output->surface);

	struct wl_region * input_region = wl_compositor_create_region(
			oguri->compositor);
	assert(input_region);
	wl_surface_set_input_region(output->surface, input_region);
	wl_region_destroy(input_region);

	output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			oguri->layer_shell,
			output->surface,
			output->output,
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
			"wallpaper");

	assert(output->layer_surface);

	struct zxdg_output_v1 * xdg_output = zxdg_output_manager_v1_get_xdg_output(
			oguri->output_manager, output->output);
	zxdg_output_v1_add_listener(xdg_output, &xdg_output_listener, output);

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

	wl_display_roundtrip(oguri->display);

	wl_list_insert(oguri->idle_outputs.prev, &output->link);
	return output;
}

void oguri_output_destroy(struct oguri_output * output) {
	wl_list_remove(&output->link);

	free(output->name);

	wl_surface_destroy(output->surface);
	zwlr_layer_surface_v1_destroy(output->layer_surface);

	struct oguri_buffer * buffer, * tmp;
	wl_list_for_each_safe(buffer, tmp, &output->buffer_ring, link) {
		wl_list_remove(&buffer->link);
		// TODO: oguri_buffer_destroy
	}

	free(output);
}
