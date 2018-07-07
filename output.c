#include <stdbool.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "oguri.h"
#include "output.h"
#include "buffers.h"

struct oguri_output * oguri_output_create(struct oguri_state * oguri) {
		// We have to keep track of all outputs, even though we will only use
		// one. This is because we can't create xdg_outputs from the wl_outputs
		// until later on, which means we don't know their names yet.
		struct oguri_output * output = calloc(1, sizeof(struct oguri_output));

		wl_list_init(&output->buffer_ring);
		output->shm = oguri->shm;
		wl_list_insert(&oguri->outputs, &output->link);

		return output;
}

void oguri_output_destroy(struct oguri_output * output) {
	free(output->name);

	wl_surface_destroy(output->surface);
	wl_region_destroy(output->input_region);
	zwlr_layer_surface_v1_destroy(output->layer_surface);

	g_object_unref(output->image);
	g_object_unref(output->frame_iter);

	// Caches the latest frame from the source
	cairo_surface_destroy(output->source_surface);
	cairo_destroy(output->source_cairo);

	output->shm = NULL;  // This cannot be freed here.

	struct oguri_buffer * buffer, * tmp;
	wl_list_for_each_safe(buffer, tmp, &output->buffer_ring, link) {
		wl_list_remove(&buffer->link);
		// TODO: oguri_buffer_destroy
	}

	free(output);
}
