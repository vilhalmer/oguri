#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#include <wayland-client.h>

#include "config.h"
#include "oguri.h"

//
// Configurators
//

bool configure_global(
		struct oguri_state * oguri,
		char * name __attribute__((unused)),
		char * property,
		char * value) {
	// TODO: Do I even need this?
	(void)oguri;
	(void)property;
	(void)value;
	return false;
}

bool configure_image(
		struct oguri_state * oguri,
		char * image_name,
		char * property,
		char * value) {
	struct oguri_image_config * image = NULL;

	// Try to find an existing config that matches the name.
	struct oguri_image_config * cfg = NULL;
	wl_list_for_each(cfg, &oguri->image_configs, link) {
		if (strcmp(cfg->name, image_name) == 0) {
			image = cfg;
			break;
		}
	}

	// If we didn't find one, make a new one.
	// TODO: Should we avoid creating this until the property is validated?
	if (!image) {
		image = calloc(1, sizeof(struct oguri_image_config));
		if (!image) {
			fprintf(stderr, "Failed to allocate memory for image config\n");
			return false;
		}
		wl_list_init(&image->link);
		wl_list_insert(oguri->image_configs.prev, &image->link);

		image->name = strdup(image_name);
		image->filter = CAIRO_FILTER_BEST;
	}

	if (strcmp(property, "path") == 0) {
		image->path = strdup(value);
		return true;
	}
	else if (strcmp(property, "filter") == 0) {
		// https://cairographics.org/manual/cairo-cairo-pattern-t.html#cairo-filter-t
		if (strcmp(value, "fast") == 0) {
			image->filter = CAIRO_FILTER_FAST;
			return true;
		}
		else if (strcmp(value, "good") == 0) {
			image->filter = CAIRO_FILTER_GOOD;
			return true;
		}
		else if (strcmp(value, "best") == 0) {
			image->filter = CAIRO_FILTER_BEST;
			return true;
		}
		else if (strcmp(value, "nearest") == 0) {
			image->filter = CAIRO_FILTER_NEAREST;
			return true;
		}
		else if (strcmp(value, "bilinear") == 0) {
			image->filter = CAIRO_FILTER_BILINEAR;
			return true;
		}
		else {
			fprintf(stderr, "Unknown filter: '%s'\n", value);
			return false;
		}
	}
	else {
		fprintf(stderr, "Invalid image property: '%s'\n", property);
		return false;
	}

	assert(!"Unreached");
}

bool configure_output(
		struct oguri_state * oguri,
		char * output_name,
		char * property,
		char * value) {
	struct oguri_output_config * output = NULL;

	// Try to find an existing config that matches the name.
	struct oguri_output_config * cfg = NULL;
	wl_list_for_each(cfg, &oguri->output_configs, link) {
		if (strcmp(cfg->name, output_name) == 0) {
			output = cfg;
			break;
		}
	}

	// If we didn't find one, make a new one.
	// TODO: Should we avoid creating this until the property is validated?
	if (!output) {
		output = calloc(1, sizeof(struct oguri_output_config));
		if (!output) {
			fprintf(stderr, "Failed to allocate memory for output config\n");
			return false;
		}
		wl_list_init(&output->link);
		wl_list_insert(oguri->output_configs.prev, &output->link);

		output->name = strdup(output_name);
		output->scaling_mode = SCALING_MODE_FILL;
		output->anchor = ANCHOR_CENTER;
	}

	if (strcmp(property, "image") == 0) {
		struct oguri_image_config * image;
		wl_list_for_each(image, &oguri->image_configs, link) {
			if (strcmp(image->name, value) == 0) {
				output->image = image;
				return true;
			}
		}
		fprintf(stderr, "Unknown image: '%s' (has it been configured?)\n",
				value);
		return false;
	}
	else if (strcmp(property, "scaling-mode") == 0) {
		if (strcmp(value, "fill") == 0) {
			output->scaling_mode = SCALING_MODE_FILL;
			return true;
		}
		// TODO: All the other scaling modes.
		else {
			fprintf(stderr, "Unknown scaling mode: '%s'\n", value);
			return false;
		}
	}
	else if (strcmp(property, "anchor") == 0) {
		// TODO
		return false;
	}
	else {
		fprintf(stderr, "Invalid output property: '%s'\n", property);
		return false;
	}

	assert(!"Unreached");
}

oguri_configurator_t * configurator_from_string(const char * name) {
	if (strcmp(name, "global") == 0) {
		return configure_global;
	}
	else if (strcmp(name, "image") == 0) {
		return configure_image;
	}
	else if (strcmp(name, "output") == 0) {
		return configure_output;
	}

	return NULL;
}

//
// Config file parsing
//

static char * expand_config_path(const char * path) {
	if (!getenv("XDG_CONFIG_HOME")) {
		char * home = getenv("HOME");
		if (!home) {
			return NULL;
		}
		char config_home[strlen(home) + strlen("/.config") + 1];
		strcpy(config_home, home);
		strcat(config_home, "/.config");
		setenv("XDG_CONFIG_HOME", config_home, 1);
	}

	wordexp_t p = {0};
	if (wordexp(path, &p, 0) == 0) {
		if (p.we_wordv[0] && access(path, R_OK) != -1) {
			char * expanded = strdup(p.we_wordv[0]);
			wordfree(&p);
			return expanded;
		}
	}

	return NULL;
}

int load_config_file(struct oguri_state * oguri, const char * path) {
	char * expanded_path = expand_config_path(path);
	if (!path) {
		return 0;
	}

	FILE * config_file = fopen(path, "r");
	if (!config_file) {
		fprintf(stderr, "Unable to open %s for reading", path);
		free(expanded_path);
		return -1;
	}
	const char * filename = basename(expanded_path);

	// The top of the file is the global section until we hit the first set of
	// square brackets.
	oguri_configurator_t * configurator = configure_global;
	char * section_name = NULL;

	int ret = 0;
	int lineno = 0;
	char * line = NULL;
	size_t n = 0;
	while (getline(&line, &n, config_file) > 0) {
		++lineno;

		// Ignore blank lines and comments.
		if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
			continue;
		}

		// Strip the newline off, it is not part of the value.
		if (line[strlen(line) - 1] == '\n') {
			line[strlen(line) - 1] = '\0';
		}

		// Select our configurator based on the type of section.
		if (line[0] == '[' && line[strlen(line) - 1] == ']') {
			char * saveptr = NULL;
			char * section_type = strtok_r(line + 1, " ]", &saveptr);

			configurator = configurator_from_string(section_type);
			if (!configurator) {
				fprintf(stderr, "[%s:%d] Invalid section type: '%s'\n",
						filename, lineno, section_type);
				ret = -1;
				break;
			}

			if (!saveptr) {
				fprintf(stderr, "[%s:%d] Must specify name for %s\n",
						filename, lineno, section_type);
				ret = -1;
				break;
			}

			free(section_name);
			section_name = strdup(saveptr);
			continue;
		}

		char *eq = strchr(line, '=');
		if (!eq) {
			fprintf(stderr, "[%s:%d] Expected key=value\n", filename, lineno);
			ret = -1;
			break;
		}

		eq[0] = '\0';

		ret = configurator(oguri, section_name, line, eq + 1);
		if (ret < 0) {
			fprintf(stderr, "[%s:%d] Failed to parse property '%s'\n",
				filename, lineno, line);
			break;
		}
	}

	free(section_name);
	free(line);
	fclose(config_file);
	free(expanded_path);
	return ret;
}
