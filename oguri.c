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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include "cairo.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include "oguri.h"
#include "animation.h"
#include "output.h"


static void noop() {}  // For unused listener members.

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
		struct wl_output * output = wl_registry_bind(
				registry, name, &wl_output_interface, 3);
		oguri_output_create(oguri, output);
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
	wl_list_init(&oguri.idle_outputs);
	wl_list_init(&oguri.animations);

	bool swaybg_compat = false;

	int argi = 1;
	if (strcmp(argv[argi], "-s") == 0) {
		oguri.oneshot = swaybg_compat = true;
		++argi;
	}

	char * output_name = argv[argi++];
	char * image_path = argv[argi++];

	if (argi < argc && swaybg_compat) {
		fprintf(stderr, "Note: Scaling modes are not yet implemented\n");
	}

	struct oguri_animation * animation = oguri_animation_create(
			&oguri, image_path);

	if (!animation) {
		return 3;
	}

	oguri.display = wl_display_connect(NULL);
	assert(oguri.display);

	oguri.registry = wl_display_get_registry(oguri.display);
	wl_registry_add_listener(oguri.registry, &registry_listener, &oguri);
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
		wl_list_for_each(output, &oguri.idle_outputs, link) {
			if (i == output_number) {
				wl_list_remove(&output->link);
				wl_list_insert(&animation->outputs, &output->link);
				break;
			}
			++i;
		}
	}

	// If the output wasn't a number, we have to look up all the names.
	if (wl_list_empty(&animation->outputs)) {
		wl_list_for_each(output, &oguri.idle_outputs, link) {
			if (strcmp(output->name, output_name) == 0) {
				wl_list_remove(&output->link);
				wl_list_insert(&animation->outputs, &output->link);
				break;
			}
		}
	}

	// If we still haven't found a matching output, RIP.
	if (wl_list_empty(&animation->outputs)) {
		fprintf(stderr, "Could not find an output named '%s'\n",output_name);
		return 2;
	}

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

			int delay = oguri_render_frame(animation);
			if (delay > 0) {
				set_timer_milliseconds(fd, (unsigned int)delay);
			}
		}
	}

	struct oguri_animation * anim, * anim_tmp;
	wl_list_for_each_safe(anim, anim_tmp, &oguri.animations, link) {
		oguri_animation_destroy(anim);
	}

	// At this point, because we've destroyed all of the animations, all
	// outputs should be idle again and will be cleaned up here.
	struct oguri_output * output_tmp;
	wl_list_for_each_safe(output, output_tmp, &oguri.idle_outputs, link) {
		oguri_output_destroy(output);
	}

	zxdg_output_manager_v1_destroy(oguri.output_manager);
	zwlr_layer_shell_v1_destroy(oguri.layer_shell);

	wl_compositor_destroy(oguri.compositor);
	wl_shm_destroy(oguri.shm);
	wl_registry_destroy(oguri.registry);
	wl_display_disconnect(oguri.display);

	return 0;
}
