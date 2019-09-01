//
// Shared memory buffers
//
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "oguri.h"
#include "buffers.h"

static int pid_shm_open(const char * prefix, int oflag, mode_t mode) {
	static const char format[] = "%s-%d";

	pid_t pid = getpid();
	int length = snprintf(NULL, 0, format, prefix, pid);
	if (length < 0) {
		return -1;
	}
	char * name = calloc(1, length);
	length = snprintf(name, length, format, prefix, pid);
	if (length < 0) {
		return -1;
	}

	int fd = shm_open(name, oflag, mode);
	if (fd < 0) {
		return -1;
	}

	shm_unlink(name);
	return fd;
}

static void buffer_handle_release(
		void *data,
		struct wl_buffer *wl_buffer __attribute__((unused))) {
	((struct oguri_buffer *) data)->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_handle_release,
};

struct oguri_buffer * oguri_allocate_buffer(struct oguri_output * output) {
	uint32_t stride = cairo_format_stride_for_width(
			CAIRO_FMT,
			output->width * output->scale);
	size_t size = stride * output->height * output->scale;
	if (size < 1) {
		fprintf(stderr, "Tiny buffer\n");
		return NULL;
	}

	// O_EXCL shouldn't be necessary here, but I would rather have it fail if
	// something weird happens.
	errno = 0;
	int fd = pid_shm_open("/oguri-buffer", O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		fprintf(stderr, "Failed to create buffer backing memory: %s\n",
				strerror(errno));
		return NULL;
	}
	shm_unlink("/oguri-buffer");

	errno = 0;
	if (ftruncate(fd, size) < 0) {
		fprintf(stderr, "Failed to resize buffer memory: %s\n",
				strerror(errno));
		close(fd);
		return NULL;
	}

	errno = 0;
	void * data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (!data) {
		fprintf(stderr, "Failed to map backing memory: %s\n", strerror(errno));
		close(fd);
		return NULL;
	}

	struct oguri_buffer * buffer = calloc(1, sizeof(struct oguri_buffer));

	struct wl_shm_pool * pool = wl_shm_create_pool(
			output->oguri->shm, fd, size);
	buffer->backing = wl_shm_pool_create_buffer(
			pool, 0,
			output->width * output->scale,
			output->height * output->scale,
			stride,
			WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->backing, &buffer_listener, buffer);

	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->data = data;
	buffer->size = size;
	buffer->cairo_surface = cairo_image_surface_create_for_data(
			data,
			CAIRO_FMT,
			output->width * output->scale,
			output->height * output->scale,
			stride);
	buffer->cairo = cairo_create(buffer->cairo_surface);

	return buffer;
}

bool oguri_allocate_buffers(struct oguri_output * output, unsigned int count) {
	if (output->buffer_count >= count) {
		// TODO: We could shrink the pool here to free up some memory.
		return true;
	}

	for (; output->buffer_count < count; ++output->buffer_count) {
		struct oguri_buffer * new_buffer = oguri_allocate_buffer(output);
		if (!new_buffer) {
			return false;
		}
		wl_list_insert(output->buffer_ring.prev, &new_buffer->link);
	}
	return true;
}

struct oguri_buffer * oguri_next_buffer(struct oguri_output * output) {
	struct oguri_buffer * current = wl_container_of(
			output->buffer_ring.next, current, link);
	wl_list_remove(&current->link);
	wl_list_insert(output->buffer_ring.prev, &current->link);

	return wl_container_of(output->buffer_ring.next, current, link);
}

void oguri_buffer_destroy(struct oguri_buffer * buffer) {
	wl_list_remove(&buffer->link);

	cairo_destroy(buffer->cairo);
	cairo_surface_destroy(buffer->cairo_surface);
	wl_buffer_destroy(buffer->backing);

	munmap(buffer->data, buffer->size);
	free(buffer);
}
