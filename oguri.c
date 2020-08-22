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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cairo.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include "oguri.h"
#include "animation.h"
#include "config.h"
#include "output.h"

//
// Signal handler
//

static int signal_pipe[2];
void signal_handler(int number) {
	if (write(signal_pipe[1], &number, sizeof(number)) < 0) {
		// Ignoring the error, there's nothing to be done about it in here.
		// Need to look at it to placate gcc, though.
		// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66425
	}
}

//
// Wayland registry
//

static void noop() {}  // For unused listener members.

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
// IPC
//

static int oguri_ipc_create(struct oguri_state * oguri) {
	int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock_fd == -1) {
		perror("Unable to create IPC socket, IPC is disabled");
		return -1;
	}
	if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == -1) {
		perror("Unable to set nonblocking, IPC is disabled");
		return -1;
	}

	oguri->ipc_sock.sun_family = AF_UNIX;
	int path_size = sizeof(oguri->ipc_sock.sun_path);

	// TODO: Configurable ipc path
	const char * runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime) {
		runtime = "/tmp";
	}
	if (path_size <= snprintf(
				oguri->ipc_sock.sun_path, path_size, "%s/oguri", runtime)) {
		fprintf(stderr, "Socket path is too long, IPC is disabled\n");
		return -1;
	}

	unlink(oguri->ipc_sock.sun_path);
	if (bind(sock_fd, (struct sockaddr *)&oguri->ipc_sock,
				sizeof(oguri->ipc_sock)) == -1) {
		perror("Unable to bind IPC socket, IPC is disabled");
		return -1;
	}

	if (listen(sock_fd, 1) == -1) {
		perror("Unable to listen on IPC socket, IPC is disabled");
		return -1;
	}

	return sock_fd;
}

static void oguri_ipc_destroy(struct oguri_state * oguri) {
	close(oguri->events[OGURI_IPC_CONNECT_EVENT].fd);
	unlink(oguri->ipc_sock.sun_path);
	// TODO: Gracefully disconnect a client if one exists.
}

static void oguri_ipc_handle_command(
		struct oguri_state * oguri, const int client) {
	// Duplicate the IPC descriptor before attempting to load config from it
	// because load_config closes the descriptor when it's done.
	int config_fd = dup(client);
	if (config_fd < 0) {
		perror("Could not duplicate IPC file descriptor");
		if (write(client, "Unable to read config from IPC\n", 32) < 0) {
			fprintf(stderr, "Error replying to ipc command\n");
		}
		close(client);
		return;
	}

	FILE * ipc_config = fdopen(config_fd, "r");
	int loaded = load_config(oguri, ipc_config, "ipc");
	if (loaded == -1) {
		// TODO: Expose the error messages instead of writing them to oguri's
		// stderr, where they will go nowhere.
		if (write(client, "Invalid configuration\n", 23) < 0) {
			fprintf(stderr, "Error replying to ipc command\n");
		}
	}

	// TODO: If there was an error reading the config, we might have partially
	// applied it. We're going to reconfig so that nothing gets out of sync
	// internally, but this should be fixed in the config handlers.
	oguri_reconfigure(oguri);

	close(client);
	oguri->events[OGURI_IPC_CLIENT_EVENT].fd = -1;
}

//
// Reconfiguration
//

// oguri_reconfigure is called after configuration changes in such a way that
// requires outputs to potentially be assigned to different animations. All
// outputs are returned to the idle_outputs list, and then one-by-one matched
// to the correct animation again. The animations will continue on their merry
// way, so outputs which end up back on the same animation will continue from
// the frame they were on. Finally, any animations which no longer have any
// outputs assigned will be cleaned up.
//
// This is not particularly efficient, but it's extremely simple which makes it
// unlikely to introduce bugs. We also don't have that many outputs.
void oguri_reconfigure(struct oguri_state * oguri) {
	// Return all outputs to the idle list.
	struct oguri_animation * anim;
	wl_list_for_each(anim, &oguri->animations, link) {
		wl_list_insert_list(&oguri->idle_outputs, &anim->outputs);
		wl_list_init(&anim->outputs);
	}

	// Now attempt to associate each output with its config, and therefore
	// animation. This might create new animations as needed.
	struct oguri_output * output, * tmp;
	wl_list_for_each_safe(output, tmp, &oguri->idle_outputs, link) {
		output->config = NULL;
		output->cached_frames = 0;

		struct oguri_output_config * opc, * wildcard_opc = NULL;
		wl_list_for_each(opc, &output->oguri->output_configs, link) {
			if (strcmp(opc->name, output->name) == 0) {
				output->config = opc;
				break;
			}
			if (strcmp(opc->name, "*") == 0) {
				// Collect this as we pass by if it exists, so we can apply it if
				// there's no exact match.
				wildcard_opc = opc;
			}
		}

		if (!output->config) {
			output->config = wildcard_opc;
		}

		struct oguri_animation * found_anim = NULL;
		if (output->config) {
			wl_list_for_each(anim, &output->oguri->animations, link) {
				if (strcmp(anim->path, output->config->image_path) == 0) {
					found_anim = anim;
					break;
				}
			}

			if (!found_anim) {
				// No animation exists, so make one. Note that this may still
				// fail, in which case this output will become idle.
				// TODO: It would be better to get any possible failures out of
				// the way at config time. The primary one is the image not
				// existing, which could be easily checked without creating an
				// animation.
				found_anim = oguri_animation_create(oguri,
						output->config->image_path);
			}
		}

		if (found_anim) {
			wl_list_remove(&output->link);
			wl_list_insert(found_anim->outputs.prev, &output->link);

			// Force a render to ensure there's a frame displayed on the output
			// even if the configured image is static.
			oguri_animation_schedule_frame(found_anim, 1);
		}
	}

	// Finally, clean up any animations which no longer have any outputs.
	struct oguri_animation * anim_tmp;
	wl_list_for_each_safe(anim, anim_tmp, &oguri->animations, link) {
		if (wl_list_empty(&anim->outputs)) {
			oguri_animation_destroy(anim);
		}
	}
}

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

	if (pipe(signal_pipe) == -1) {
		perror("Unable to create pipe for signal handler");
		return 1;
	}
	struct sigaction act;
    act.sa_handler = &signal_handler;
	int error = sigaction(SIGINT, &act, NULL);
	error += sigaction(SIGTERM, &act, NULL);
	error += sigaction(SIGQUIT, &act, NULL);
	if (error < 0) {
		perror("Unable to install signal handler");
		return 1;
	}

	// Ignore SIGPIPE, may occur on the IPC socket.
	signal(SIGPIPE, SIG_IGN);

	oguri.display = wl_display_connect(NULL);
	assert(oguri.display);

	oguri.events[OGURI_SIGNAL_EVENT] = (struct pollfd) {
		.fd = signal_pipe[0],
		.events = POLLIN,
	};
	oguri.events[OGURI_WAYLAND_EVENT] = (struct pollfd) {
		.fd = wl_display_get_fd(oguri.display),
		.events = POLLIN,
	};
	oguri.events[OGURI_IPC_CONNECT_EVENT] = (struct pollfd) {
		.fd = oguri_ipc_create(&oguri),
		.events = POLLIN,
	};
	oguri.events[OGURI_IPC_CLIENT_EVENT] = (struct pollfd) {
		.fd = -1,  // This event is idle when no client is connected.
		.events = POLLIN,
	};

	// Fill in the rest of the event fds with -1.
	for (size_t i = OGURI_FIRST_ANIM_EVENT; i < OGURI_EVENT_COUNT; ++i) {
		oguri.events[i] = (struct pollfd) {
			.fd = -1,
		};
	}

	oguri.registry = wl_display_get_registry(oguri.display);
	wl_registry_add_listener(oguri.registry, &registry_listener, &oguri);
	wl_display_roundtrip(oguri.display);
	assert(oguri.compositor && oguri.layer_shell && oguri.shm);

	int load_status = load_config_file(&oguri, config_path);
	free(config_path);
	if (load_status < 0) {
		return 1;
	}

	oguri_reconfigure(&oguri);

	oguri.run = true;
	while (oguri.run) {
		while (wl_display_prepare_read(oguri.display) != 0) {
			wl_display_dispatch_pending(oguri.display);
		}
		wl_display_flush(oguri.display);

		int polled = poll(oguri.events, OGURI_EVENT_COUNT, -1);
		if (polled < 0) {
			wl_display_cancel_read(oguri.display);
			continue;
		}

		if (oguri.events[OGURI_SIGNAL_EVENT].revents & POLLIN) {
			int signal_number;
			const ssize_t size = sizeof(signal_number);
			if (read(signal_pipe[0], &signal_number, size) < size) {
				// Do nothing, I guess?
			}
			else {
				if (signal_number == SIGINT ||
						signal_number == SIGTERM ||
						signal_number == SIGQUIT) {
					oguri.run = false;
				}
			}
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

		// Check for a new IPC connection.
		if (oguri.events[OGURI_IPC_CONNECT_EVENT].revents & POLLIN) {
			// If accept fails for some reason, it will return -1. We just let
			// that happen, because it will simply disable the client pollfd.
			int client = accept(oguri.events[OGURI_IPC_CONNECT_EVENT].fd,
					NULL, NULL);

			if (client != -1) {
				int flags;
				if ((flags = fcntl(client, F_GETFL)) == -1 ||
						fcntl(client, F_SETFL, flags|O_NONBLOCK) == -1) {
					perror("Unable to set nonblocking on IPC client socket");
					close(client);
					client = -1;
				}
			}

			oguri.events[OGURI_IPC_CLIENT_EVENT].fd = client;
		}

		if (oguri.events[OGURI_IPC_CLIENT_EVENT].revents & POLLIN) {
			int client = oguri.events[OGURI_IPC_CLIENT_EVENT].fd;
			oguri_ipc_handle_command(&oguri, client);
		}

		// Now see if we need to draw any frames.
		struct oguri_animation * anim;
		wl_list_for_each(anim, &oguri.animations, link) {
			if (oguri.events[anim->event_index].revents & POLLIN) {
				uint64_t expirations;
				ssize_t n = read(
						oguri.events[anim->event_index].fd,
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

	oguri_ipc_destroy(&oguri);

	zxdg_output_manager_v1_destroy(oguri.output_manager);
	zwlr_layer_shell_v1_destroy(oguri.layer_shell);

	wl_compositor_destroy(oguri.compositor);
	wl_shm_destroy(oguri.shm);
	wl_registry_destroy(oguri.registry);
	wl_display_disconnect(oguri.display);

	return 0;
}
