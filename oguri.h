#ifndef _OGURI_H
#define _OGURI_H

#include <poll.h>
#include <stdbool.h>
#include <wayland-client.h>

// These are used to reserve a few pollfd slots for static stuff.
enum oguri_events {
	OGURI_WAYLAND_EVENT,
	OGURI_EVENT_COUNT,  // last
};

struct oguri_state {
	bool run;
	bool oneshot;  // Whether to exit when an active display disconnects.

	struct pollfd events[25];
	size_t fd_count;

	struct wl_display * display;
	struct wl_registry * registry;
	struct wl_compositor * compositor;
	struct wl_shm * shm;

	struct zwlr_layer_shell_v1 * layer_shell;
	struct zxdg_output_manager_v1 * output_manager;

	struct wl_list image_configs;  // oguri_image_config::link
	struct wl_list output_configs;  // oguri_output_config::link

	struct wl_list idle_outputs;  // oguri_output::link
	struct wl_list animations;  // oguri_animation::link
};

#endif
