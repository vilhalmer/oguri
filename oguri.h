#ifndef OGURI_H
#define OGURI_H

#include <poll.h>
#include <sys/un.h>
#include <stdbool.h>
#include <wayland-client.h>

// Maximum number of events we can keep track of, including both the reserved
// slots below and any active animations.
#define OGURI_EVENT_COUNT 25

// These are used to reserve a few pollfd slots for static stuff.
enum oguri_events {
	OGURI_SIGNAL_EVENT,
	OGURI_WAYLAND_EVENT,
	OGURI_IPC_CONNECT_EVENT,
	OGURI_IPC_CLIENT_EVENT,
	OGURI_FIRST_ANIM_EVENT,  // last
};

struct oguri_state {
	bool run;
	struct pollfd events[OGURI_EVENT_COUNT];

	struct wl_display * display;
	struct wl_registry * registry;
	struct wl_compositor * compositor;
	struct wl_shm * shm;

	struct zwlr_layer_shell_v1 * layer_shell;
	struct zxdg_output_manager_v1 * output_manager;

	struct sockaddr_un ipc_sock;

	struct wl_list output_configs;  // oguri_output_config::link
	struct wl_list idle_outputs;  // oguri_output::link
	struct wl_list animations;  // oguri_animation::link
};

void oguri_reconfigure(struct oguri_state * oguri, bool reload_cache);

#endif
