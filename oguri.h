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

	struct wl_display * display;
	struct wl_compositor * compositor;
	struct wl_shm * shm;

	struct zwlr_layer_shell_v1 * layer_shell;
	struct zxdg_output_manager_v1 * output_manager;

	struct wl_list outputs;  // oguri_output::link
	struct oguri_output * selected_output;
};

#endif
