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
#include "cairo.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include "oguri.h"
#include "animation.h"
#include "config.h"
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
// Main
//

static const char usage[] =
	"Usage: oguri [-c <config-path>]\n"
	"\n"
	"  -c  Path to the configuration file to use.\n"
	"      (default: $XDG_CONFIG_HOME/oguri/config)\n"
	"  -h  Show this text.\n"
	"\n"
	"To control oguri while it is running, use `ogurictl`.\n";

int main(int argc, char * argv[]) {
	struct oguri_state oguri = {0};
	wl_list_init(&oguri.image_configs);
	wl_list_init(&oguri.output_configs);
	wl_list_init(&oguri.idle_outputs);
	wl_list_init(&oguri.animations);

	char * config_path = strdup("$XDG_CONFIG_HOME/oguri/config");

	for (int argi = 1; argi < argc; ++argi) {
		if (strcmp(argv[argi], "-c") == 0) {
			free(config_path);
			config_path = strdup(argv[++argi]);
		}
		else if (strcmp(argv[argi], "-h") == 0) {
			printf(usage);
			return 0;
		}
		else {
			fprintf(stderr, usage);
			return 1;
		}
	}

	int load_status = load_config_file(&oguri, config_path);
	free(config_path);
	if (load_status < 0) {
		return 1;
	}

	oguri.display = wl_display_connect(NULL);
	assert(oguri.display);

	oguri.events[OGURI_WAYLAND_EVENT] = (struct pollfd) {
		.fd = wl_display_get_fd(oguri.display),
		.events = POLLIN,
	};
	oguri.fd_count = OGURI_EVENT_COUNT;  // Skip to the end of the special ones

	// Have all of the animations ready to go so that outputs can associate
	// themselves with one as each output appears.
	struct oguri_image_config * imgc;
	wl_list_for_each(imgc, &oguri.image_configs, link) {
		oguri_animation_create(&oguri, imgc->path);
	}

	oguri.registry = wl_display_get_registry(oguri.display);
	wl_registry_add_listener(oguri.registry, &registry_listener, &oguri);
	wl_display_roundtrip(oguri.display);
	assert(oguri.compositor && oguri.layer_shell && oguri.shm);

	oguri.run = true;
	while (oguri.run) {
		while (wl_display_prepare_read(oguri.display) != 0) {
			wl_display_dispatch_pending(oguri.display);
		}
		wl_display_flush(oguri.display);

		int polled = poll(oguri.events, oguri.fd_count, -1);
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
		if (oguri.events[OGURI_WAYLAND_EVENT].revents & POLLIN) {
			if (wl_display_read_events(oguri.display) != 0) {
				if (errno == 104) {
					// Compositor disconnected us, exit quietly.
				}
				else {
					fprintf(stderr, "Failed to read Wayland events: %s\n",
							strerror(errno));
				}
				oguri.run = false;
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

		// Now see if we need to draw a frame. In order to associate the
		// animations with the pollfd, we count as we iterate through them.
		// This is necessary because pollfds are stored in a boring ol' array.
		// There is probably a nicer way to do this.
		int anim_idx = OGURI_EVENT_COUNT;  // Skip the hard-coded fd indexes.
		struct oguri_animation * anim;
		wl_list_for_each(anim, &oguri.animations, link) {
			if (oguri.events[anim_idx].revents & POLLIN) {
				uint64_t expirations;
				ssize_t n = read(
						oguri.events[anim_idx].fd,
						&expirations,
						sizeof(expirations));

				if (n < 0) {
					fprintf(stderr, "Failed to read timer events\n");
					break;
				}

				// This will update the animation's timerfd automatically if
				// neccessary. (Spooky!)
				oguri_render_frame(anim);
			}

			++anim_idx;
		}
	}

	struct oguri_animation * anim, * anim_tmp;
	wl_list_for_each_safe(anim, anim_tmp, &oguri.animations, link) {
		oguri_animation_destroy(anim);
	}

	// At this point, because we've destroyed all of the animations, all
	// outputs should be idle again and will be cleaned up here.
	struct oguri_output * output, * output_tmp;
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
