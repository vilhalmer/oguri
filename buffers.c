//
// Shared memory buffers
//
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "oguri.h"
#include "buffers.h"

static void buffer_handle_release(
		void *data,
		struct wl_buffer *wl_buffer __attribute__((unused))) {
	((struct oguri_buffer *) data)->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_handle_release,
};

struct oguri_buffer * oguri_allocate_buffer(struct oguri_state * oguri) {
	const cairo_format_t cairo_fmt = CAIRO_FORMAT_ARGB32;
	uint32_t stride = cairo_format_stride_for_width(cairo_fmt, oguri->width);
	size_t size = stride * oguri->height;
	if (size < 1) {
		fprintf(stderr, "Tiny buffer\n");
		return NULL;
	}

	static const char * template = "/oguri-buffer-XXXXXX";
	const char * path = getenv("XDG_RUNTIME_DIR");
	if (!path) {
		fprintf(stderr, "XDG_RUNTIME_DIR is not set\n");
		return NULL;
	}

	size_t name_size = strlen(template) + 1 + strlen(path) + 1;
	char name[name_size];
	snprintf(name, name_size, "%s/%s", path, template);

	int fd = mkstemp(name);
	if (fd < 0) {
		fprintf(stderr, "Failed to create buffer backing memory\n");
		return NULL;
	}

	if (ftruncate(fd, size) < 0) {
		close(fd);
		return NULL;
	}

	void * data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (!data) {
		fprintf(stderr, "Failed to map backing memory\n");
		close(fd);
		return NULL;
	}

	struct oguri_buffer * buffer = calloc(1, sizeof(struct oguri_buffer));

	struct wl_shm_pool * pool = wl_shm_create_pool(oguri->shm, fd, size);
	buffer->backing = wl_shm_pool_create_buffer(
			pool, 0, oguri->width, oguri->height, stride,
			WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->backing, &buffer_listener, buffer);

	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->data = data;
	buffer->cairo_surface = cairo_image_surface_create_for_data(
			data, cairo_fmt, oguri->width, oguri->height, stride);
	buffer->cairo = cairo_create(buffer->cairo_surface);

	return buffer;
}

bool oguri_allocate_buffers(struct oguri_state * oguri) {
	for (size_t i = 0; i < 2; ++i) {
		struct oguri_buffer * new_buffer = oguri_allocate_buffer(oguri);
		if (!new_buffer) {
			return false;
		}
		wl_list_insert(oguri->buffer_ring.prev, &new_buffer->link);
	}
	return true;
}

struct oguri_buffer * next_buffer(struct oguri_state * oguri) {
	struct oguri_buffer * current = wl_container_of(
			oguri->buffer_ring.next, current, link);
	if (!current->busy) {
		return current;
	}
	wl_list_remove(&current->link);
	wl_list_insert(oguri->buffer_ring.prev, &current->link);

	// TODO: This one might be busy too.
	return wl_container_of(oguri->buffer_ring.next, current, link);
}
