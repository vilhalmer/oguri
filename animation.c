#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdlib.h>
#include <gdk/gdk.h>
#include "oguri.h"
#include "buffers.h"
#include "output.h"
#include "animation.h"

G_DEFINE_QUARK(oguri_new_frame, oguri_new_frame);

static bool oguri_is_first_cycle(GdkPixbuf * image) {
	return g_object_get_qdata(G_OBJECT(image), oguri_new_frame_quark()) ==
		&oguri_is_first_cycle;
}

static void oguri_mark_first_cycle(GdkPixbuf * image) {
	g_object_set_qdata(
			G_OBJECT(image), oguri_new_frame_quark(), &oguri_is_first_cycle);
}

static void scale_image_onto(
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

int oguri_render_frame(struct oguri_animation * anim) {
	gdk_pixbuf_animation_iter_advance(anim->frame_iter, NULL);
	GdkPixbuf * image = gdk_pixbuf_animation_iter_get_pixbuf(anim->frame_iter);

	// TODO: Maybe we should cache this on the animation?
	bool first_cycle = oguri_is_first_cycle(image);
	if (first_cycle) {
		++anim->frame_count;
	}

	// Keep track of the fact that we moved forward a frame, looping back to
	// the beginning if appropriate. Because we increment frame_count at the
	// beginning, this works just fine the first time through when we don't
	// actually know the final count yet.
	anim->frame_index = (anim->frame_index + 1) % anim->frame_count;

	struct oguri_output * output;
	wl_list_for_each(output, &anim->outputs, link) {
		struct oguri_buffer * buffer = oguri_next_buffer(output);

		if (!output->animation_cached) {
			if (!first_cycle && anim->frame_index == 0) {
				// This is a bit hacky. When we're past the first cycle, we
				// want to have as many buffers as the animation has frames,
				// because then we can keep each resized frame in a buffer
				// instead of re-scaling it each time. We also only need to
				// resize our buffer ring once and it will stay at that size,
				// so we just do it on frame zero of the cycle. Since we're
				// only doing this when the animation is uncached, and it will
				// be cached by the end of the second cycle, this happens once
				// (barring any display reconfiguration).
				if (!oguri_allocate_buffers(output, anim->frame_count)) {
					// TODO: This will freeze us at the current frame, probably
					// should quit instead.
					return -1;
				}
			}

			cairo_t *cairo = buffer->cairo;

			// Draw the frame into our source surface, at its native size.
			gdk_cairo_set_source_pixbuf(anim->source_cairo, image, 0, 0);
			cairo_paint(anim->source_cairo);

			scale_image_onto(
					cairo, anim->source_surface, output->width, output->height,
					OGURI_CENTER_X, OGURI_CENTER_Y);

			wl_surface_set_buffer_scale(output->surface, output->scale);

			if (!first_cycle && anim->frame_index == anim->frame_count - 1) {
				// We've seen every frame, which means this output has a
				// valid buffer for every frame of the animation and we can
				// stop drawing them.
				output->animation_cached = true;
			}
		}

		// TODO: This should mark the buffer as busy, but we're not actually
		// checking for that anyway.
		wl_surface_attach(output->surface, buffer->backing, 0, 0);
		wl_surface_damage(output->surface, 0, 0, output->width, output->height);
		wl_surface_commit(output->surface);
	}

	// Return the time that we need to wait before calling render_frame again.
	return gdk_pixbuf_animation_iter_get_delay_time(anim->frame_iter);
}

struct oguri_animation * oguri_animation_create(
		struct oguri_state * oguri, const char * path) {
	GError * error = NULL;
	GdkPixbufAnimation * image = gdk_pixbuf_animation_new_from_file(path, &error);

	if (error || !image) {
		fprintf(stderr, "Could not open image '%s'", path);
		return NULL;
	}

	struct oguri_animation * anim = calloc(1, sizeof(struct oguri_animation));
	wl_list_init(&anim->outputs);

	anim->oguri = oguri;
	anim->path = strdup(path);
	anim->image = image;
	anim->frame_iter = gdk_pixbuf_animation_get_iter(image, NULL);

	// We need a cairo surface of the image's size to draw each frame into
	// while scaling them up. This is as good a place for it as any.
	anim->source_surface = cairo_image_surface_create(
			CAIRO_FMT,
			gdk_pixbuf_animation_get_width(image),
			gdk_pixbuf_animation_get_height(image));
	anim->source_cairo = cairo_create(anim->source_surface);

	// This is undocumented at best, but the first time through the animation,
	// every frame is stored in the same pixbuf object. This means that we can
	// figure out when we've completed an entire cycle by tracking this object
	// via qdata. There is no supported way to determine when you've completed
	// a cycle, so this will have to do.
	GdkPixbuf * first = gdk_pixbuf_animation_iter_get_pixbuf(anim->frame_iter);
	oguri_mark_first_cycle(first);

	wl_list_insert(oguri->animations.prev, &anim->link);
	return anim;
}

void oguri_animation_destroy(struct oguri_animation * anim) {
	wl_list_remove(&anim->link);

	cairo_surface_destroy(anim->source_surface);
	cairo_destroy(anim->source_cairo);
	g_object_unref(anim->image);
	g_object_unref(anim->frame_iter);
	free(anim->path);

	// Put all of the associated outputs back into the idle list, in case we
	// want to reassign them to a new animation later. Destroying them doesn't
	// happen until they are removed from the display, or we are told to exit.
	wl_list_insert_list(&anim->oguri->idle_outputs, &anim->outputs);

	anim->oguri = NULL;
	free(anim);
}
