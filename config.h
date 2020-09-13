#ifndef OGURI_CONFIG_H
#define OGURI_CONFIG_H

#include <stdbool.h>
#include <stdio.h>
#include <cairo.h>

#include "oguri.h"

struct oguri_output_config {
	struct wl_list link;  // oguri_state::output_configs

	char * name;
	char * image_path;

	cairo_filter_t filter;

	enum {
		SCALING_MODE_FILL,
		SCALING_MODE_STRETCH,
		SCALING_MODE_TILE,
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
int load_config(struct oguri_state * oguri, FILE * config_file,
		const char * filename);

#endif
