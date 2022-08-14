#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "oguri_ipc.h"


static const char usage[] =
	"Usage: ogurictl COMMAND\n"
	"       ogurictl [--help] [--version]\n"
	"\n"
	"Available commands:\n"
	"  output NAME [<options>]\n"
	"    --anchor        Sides to which the image should be anchored\n"
	"    --filter        Scaling filter to apply to the image\n"
	"    --image         Path to the image to show on this output\n"
	"    --scaling-mode  Method used to fit the image to the output\n"
	"\n"
	"  reload\n"
	"\n"
	"General options:\n"
	"  -V, --version   Show the version of oguri\n"
	"  -h, --help      Show this text\n"
	"\n"
	"For the available values for each option, consult oguri(5).\n";


static const struct option general_options[] = {
	{"help", no_argument, 0, 0},
	{"version", no_argument, 0, 0},
	{0},
};


static struct option output_options[] = {
	{"anchor", required_argument, 0, 0},
	{"filter", required_argument, 0, 0},
	{"image", required_argument, 0, 0},
	{"scaling-mode", required_argument, 0, 0},
	{0},
};


int handle_output(int argc, char * argv[], char ** buffer, unsigned long * buffer_size) {
	if (optind >= argc) {
		fprintf(stderr, "No output name provided!\n\n%s", usage);
		return 1;
	}

	char * output_name = argv[optind++];

	int buffer_loc = 0;

	buffer_loc += snprintf(
			*buffer + buffer_loc,
			*buffer_size - buffer_loc,
			"[output %s]\n",
			output_name);

	int opt_result, opt_index;
	opterr = 0;  // Clarify that we're looking at output options in the errors.
	for (;;) {
		opt_result = getopt_long(argc, argv, ":", output_options, &opt_index);

		if (opt_result == '?') {
			fprintf(stderr,
					"%s: unrecognized output option '%s'\n\n%s",
					argv[0], argv[optind - 1], usage);
			return 1;
		}
		else if (opt_result == ':') {
			fprintf(stderr,
					"%s: output option '%s' requires an argument\n\n%s",
					argv[0], argv[optind - 1], usage);
			return 1;
		}
		else if (opt_result == -1) {
			break;
		}

    	buffer_loc += snprintf(
    			*buffer + buffer_loc,
				*buffer_size - buffer_loc,
    			"%s=%s\n",
				output_options[opt_index].name,
				optarg);
	}

	return 0;
}

static enum oguri_ipc_command parse_subcommand(char * subcommand) {
  if (strcmp(subcommand, "output") == 0) return OGURI_IPC_CONFIGURE;
  if (strcmp(subcommand, "reload") == 0) return OGURI_IPC_RELOAD_IMAGES;
  return OGURI_NOT_IPC_COMMAND;
}

int main(int argc, char * argv[]) {
	int opt_char, opt_index = -1;
	opt_char = getopt_long(argc, argv, "+hV", general_options, &opt_index);
	if (opt_char == 'h' || opt_index == 0) {
		printf("%s", usage);
		return 0;
	}
	else if (opt_char == 'V' || opt_index == 1) {
		printf("1.0\n");  // TODO: Dynamic
		return 0;
	}

	if (optind >= argc) {
		fprintf(stderr, "%s", usage);
		return 1;
	}

	char * subcommand = argv[optind++];

	// Create an initial buffer, the subcommand may need to resize it.
	unsigned long buffer_size = 1024;
	char * buffer = calloc(buffer_size, sizeof(char));

	// Since getopt state is global, we can just continue working in a
	// command-specific function.
	int subcommand_return = 0;
  char * cmd = calloc(1, sizeof(char));
  cmd [0] = parse_subcommand(subcommand);

	switch (cmd[0]) {
    case OGURI_IPC_CONFIGURE:
      subcommand_return = handle_output(argc, argv, &buffer, &buffer_size);
      break;
    case OGURI_IPC_RELOAD_IMAGES:
      break;
    default:
      fprintf(stderr, "Unknown command '%s'\n\n%s", subcommand, usage);
      free(buffer);
      return 1;
	}

	if (subcommand_return != 0) {
		free(buffer);
		return subcommand_return;
	}

	int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock_fd == -1) {
		perror("Unable to create socket");
		free(buffer);
		return 1;
	}

	struct sockaddr_un remote = {
		.sun_family = AF_UNIX,
	};
	int path_size = sizeof(remote.sun_path);

	// TODO: Configurable ipc path, but this means we have to read from the
	// config file in here too. :(
	const char * runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime) {
		runtime = "/tmp";
	}

	int len = snprintf(remote.sun_path, path_size, "%s/oguri", runtime);
	if (path_size <= len) {
		fprintf(stderr, "Socket path is too long, unable to connect\n");
		free(buffer);
		return 1;
	}

	int connected = connect(
			sock_fd, (struct sockaddr *)&remote, sizeof(remote));
	if (connected == -1) {
		perror("Unable to connect to oguri socket");
		free(buffer);
		return 1;
	}

	if (send(sock_fd, cmd, 1, 0) == -1) {
		perror("Unable to send command type to oguri");
		goto close_err;
	}
	if (send(sock_fd, buffer, strnlen(buffer, buffer_size - 1), 0) == -1) {
		perror("Unable to send command data to oguri");
		goto close_err;
	}

	int recv_len = recv(sock_fd, buffer, buffer_size - 1, 0);
	if (recv_len < 0) {
		perror("Unable to read response from oguri");
		goto close_err;
	}
	else if (recv_len > 0) {
		buffer[recv_len] = '\0';
		printf("%s", buffer);
	}
	else {
		// oguri had nothing to say.
	}

	free(buffer);
	close(sock_fd);
	return 0;

close_err:
	free(buffer);
	close(sock_fd);
	return 1;
}
