#ifndef _OGURI_CONFIG_H
#define _OGURI_CONFIG_H

#include "oguri.h"

typedef int oguri_configurator_t(struct oguri_state *, char *, char *, char *);
oguri_configurator_t * configurator_from_string(const char * name);

int load_config_file(struct oguri_state * oguri, const char * path);

#endif
