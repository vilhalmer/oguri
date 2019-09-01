#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include "oguri.h"
#include "buffers.h"
#include "output.h"
#include "animation.h"

static bool set_timer_milliseconds(int timer_fd, unsigned int delay) {
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
		cairo_filter_t filter,
		int32_t buffer_width,
		int32_t buffer_height,
		int anchor) {  // TODO: Probably should re-expose this enum.
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

		double offset = 0.0;
		if (anchor & ANCHOR_TOP) {
			offset = 0.0;
		}
		else if (anchor & ANCHOR_BOTTOM) {
			offset = ((double)buffer_height / scale) - height;
		}
		else {  // ANCHOR_CENTER
			offset = ((double)buffer_height / 2 / scale) - (height / 2);
		}

		cairo_set_source_surface(cairo, source, 0, offset);
	} else {
		scale = (double)buffer_height / height;
		cairo_scale(cairo, scale, scale);

		double offset = 0;
		if (anchor & ANCHOR_LEFT) {
			offset = 0.0;
		}
		else if (anchor & ANCHOR_RIGHT) {
			offset = ((double)buffer_width / scale) - width;
		}
		else {  // ANCHOR_CENTER
			offset = ((double)buffer_width / 2 / scale) - (width / 2);
		}

		cairo_set_source_surface(cairo, source, offset, 0);
	}

	cairo_pattern_set_filter(cairo_get_source(cairo), filter);
	cairo_paint(cairo);

	cairo_restore(cairo);
}

int oguri_render_frame(struct oguri_animation * anim) {
	gdk_pixbuf_animation_iter_advance(anim->frame_iter, NULL);
	GdkPixbuf * image = gdk_pixbuf_animation_iter_get_pixbuf(anim->frame_iter);

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

		if (output->cached_frames < anim->frame_count) {
			if (!first_cycle) {
				// When we're past the first cycle, we want to have as many
				// buffers as the animation has frames, because then we can
				// keep each resized frame in a buffer instead of re-scaling it
				// each time. However, we don't know which frame we started on,
				// so just attempt to resize every frame until we've cached
				// them all.
				if (!oguri_allocate_buffers(output, anim->frame_count)) {
					// TODO: This will freeze us at the current frame, probably
					// should quit instead.
					fprintf(stderr, "Unable to allocate %d frame buffers\n",
							anim->frame_count);
					return -1;
				}
			}

			// Draw the frame into our source surface, at its native size.
			oguri_cairo_surface_paint_pixbuf(anim->source_surface, image);

			// Then scale it into the buffer.
			scale_image_onto(
					buffer->cairo,
					anim->source_surface,
					output->config->filter,
					output->width * output->scale,
					output->height * output->scale,
					output->config->anchor);

			wl_surface_set_buffer_scale(output->surface, output->scale);

			if (!first_cycle) {
				// We count the number of frames we've cached to know when
				// we've cached them all, even if we didn't start at the first
				// frame of the animation. On the first cycle, however, we
				// don't want to cache because we don't know how many there
				// will be (or if the animation is even finite, technically).
				++output->cached_frames;
			}
		}

		// TODO: This should mark the buffer as busy, but we're not actually
		// checking for that anyway.
		wl_surface_attach(output->surface, buffer->backing, 0, 0);
		wl_surface_damage(output->surface, 0, 0, output->width, output->height);
		wl_surface_commit(output->surface);
	}

	// If we've got another frame to display, update our timer.
	int delay = gdk_pixbuf_animation_iter_get_delay_time(anim->frame_iter);
	if (delay > 0) {
		set_timer_milliseconds(anim->timerfd, (unsigned int)delay);
	}

	return delay;
}

struct oguri_animation * oguri_animation_create(
		struct oguri_state * oguri, char * image_path) {
	GError * error = NULL;
	GdkPixbufAnimation * image = gdk_pixbuf_animation_new_from_file(
			image_path, &error);

	if (error || !image) {
		fprintf(stderr, "Could not open image: '%s'\n", image_path);
		return NULL;
	}

	struct oguri_animation * anim = calloc(1, sizeof(struct oguri_animation));
	wl_list_init(&anim->outputs);

	anim->oguri = oguri;
	anim->path = strdup(image_path);
	anim->image = image;
	anim->frame_iter = gdk_pixbuf_animation_get_iter(image, NULL);

	// This is undocumented at best, but the first time through the animation,
	// every frame is stored in the same pixbuf object. This means that we can
	// figure out when we've completed an entire cycle by tracking this object
	// via qdata. There is no supported way to determine when you've completed
	// a cycle, so this will have to do.
	GdkPixbuf * first = gdk_pixbuf_animation_iter_get_pixbuf(anim->frame_iter);
	oguri_mark_first_cycle(first);

	// We're going to make the wild assumption that every frame in the
	// animation has the same number of channels.
	int channel_count = gdk_pixbuf_get_n_channels(first);

	// We need a cairo surface of the image's size to draw each frame into
	// while scaling them up. This is as good a place for it as any.
	anim->source_surface = cairo_image_surface_create(
			(channel_count == 3) ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32,
			gdk_pixbuf_animation_get_width(image),
			gdk_pixbuf_animation_get_height(image));

	anim->timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

	oguri->events[oguri->fd_count++] = (struct pollfd) {
		.fd = anim->timerfd,
		.events = POLLIN,
	};

	if (!set_timer_milliseconds(anim->timerfd, 1)) {  // Show first frame ASAP.
		fprintf(stderr, "Unable to schedule first timer\n");
	}

	wl_list_insert(oguri->animations.prev, &anim->link);
	return anim;
}

void oguri_animation_destroy(struct oguri_animation * anim) {
	wl_list_remove(&anim->link);

	cairo_surface_destroy(anim->source_surface);
	g_object_unref(anim->image);
	g_object_unref(anim->frame_iter);
	free(anim->path);

	// Put all of the associated outputs back into the idle list, in case we
	// want to reassign them to a new animation later. Destroying them doesn't
	// happen until they are removed from the display, or we are told to exit.
	wl_list_insert_list(&anim->oguri->idle_outputs, &anim->outputs);

	// TODO: It seems like we should do something about closing the timerfd
	// in here. However, this will make poll unhappy, because it still has the
	// same descriptor and expects all of its stuff to be open. We need a way
	// to signal the poll loop to clean it up on that end. Alternatively we
	// could keep a pointer to the entire pollfd.

	anim->oguri = NULL;
	free(anim);
}
