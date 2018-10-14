#ifndef _OGURI_H
#define _OGURI_H

#include <poll.h>
#include <wayland-client.h>
#include "output.h"

enum oguri_events {
	OGURI_WAYLAND_EVENT,
	OGURI_TIMER_EVENT,
	OGURI_EVENT_COUNT,  // last
};

struct oguri_state {
	bool run;
	bool oneshot;  // Whether to exit when an active display disconnects.
	bool dirty;  // Whether we need to re-render

	struct wl_display * display;
	struct wl_registry * registry;
	struct wl_compositor * compositor;
	struct wl_shm * shm;

	struct zwlr_layer_shell_v1 * layer_shell;
	struct zxdg_output_manager_v1 * output_manager;

	struct wl_list idle_outputs;  // oguri_output::link
	struct wl_list animations;  // oguri_animation::link
};

#endif
