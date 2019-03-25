#ifndef _OGURI_CONFIG_H
#define _OGURI_CONFIG_H

#include <cairo.h>

#include "oguri.h"

struct oguri_image_config {
	struct wl_list link;  // oguri_state::image_configs

	char * name;
	char * path;
	cairo_filter_t filter;
};

struct oguri_output_config {
	struct wl_list link;  // oguri_state::output_configs

	char * name;
	struct oguri_image_config * image;

	enum {
		SCALING_MODE_FILL,
		SCALING_MODE_STRETCH,
		SCALING_MODE_FIT,
		SCALING_MODE_CENTER,
		SCALING_MODE_TILE,
		SCALING_MODE_COUNT  // Last.
	} scaling_mode;

	enum {  // Bitmask, top/bottom and left/right are mutually exclusive.
		ANCHOR_CENTER = 0,
		ANCHOR_TOP = 1,
		ANCHOR_LEFT = 2,
		ANCHOR_BOTTOM = 4,
		ANCHOR_RIGHT = 8,
	} anchor;
};

typedef bool oguri_configurator_t(struct oguri_state *, char *, char *, char *);
oguri_configurator_t * configurator_from_string(const char * name);

int load_config_file(struct oguri_state * oguri, const char * path);

#endif
