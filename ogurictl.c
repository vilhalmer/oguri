#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>


static const char usage[] =
	"Usage: ogurictl output [<options>] <name>\n"
	"       ogurictl [--help] [--version]\n"
	"\n"
	"Output options\n"
	"  <name>          The name of the output to configure\n"
	"  --anchor        Sides to which the image should be anchored\n"
	"  --filter	       Scaling filter to apply to the image\n"
	"  --image         Path to the image to show on this output\n"
	"  --scaling-mode  Method used to fit the image to the output\n"
	"\n"
	"General options\n"
	"  -V, --version   Show the version of oguri\n"
	"  -h, --help      Show this text\n"
	"\n"
	"For the available values for each option, consult oguri(5)\n";


int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, usage);
		return 1;
	}

	// Attempt to parse the options into a command.

	int argi = 1;
	char * arg = argv[argi];
	if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
		printf("%s", usage);
		return 0;
	}
	else if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
		printf("1.0\n");  // TODO: Dynamic
		return 0;
	}

	char buffer[1024] = {0};  // Matches the length on the oguri side.
	int buffer_loc = 0;

	buffer_loc += snprintf(
			buffer + buffer_loc, sizeof(buffer) - buffer_loc, "[%s ", arg);
	++argi;

	// We need to find the name argument, which might be anywhere along the
	// command line. We're just going to loop twice to make things easier.

	int name_i = 0;
	for (; argi < argc; ++argi) {
		arg = argv[argi];

		// Skip each option and its value, which may be in the same arg or the
		// next one depending on whether it was specified with an equal sign.
		if (strncmp(arg, "--", 2) == 0) {
			if (strchr(arg, '=') == NULL) {
				++argi;
			}
			continue;
		}

		buffer_loc += snprintf(
				buffer + buffer_loc, sizeof(buffer) - buffer_loc,
				"%s]\n", arg);
		name_i = argi;
		break;
	}

	if (name_i == 0) {
		fprintf(stderr, "No name provided\n");
		return 1;
	}

	// Now start over to process the options, skipping the name.

	for (argi = 2; argi < argc; ++argi) {
		if (argi == name_i) {
			continue;
		}
		arg = argv[argi];

		if (strncmp(arg, "--", 2) != 0) {
			fprintf(stderr, "Invalid non-option: '%s'\n", arg);
			return 1;
		}

		// If this option has an equal sign, it means the value is part of the
		// same argi. If not, it's the next one and we need to insert the
		// equal sign ourselves, then consume an extra argi.

		buffer_loc += snprintf(
				buffer + buffer_loc, sizeof(buffer) - buffer_loc,
				"%s", &arg[2]);

		if (strchr(arg, '=') == NULL) {
			// Consume the next option and append it.
			++argi;
			if (argi >= argc) {
				fprintf(stderr, "No value for option '%s'\n", arg);
				return 1;
			}
			arg = argv[argi];

			buffer_loc += snprintf(
					buffer + buffer_loc, sizeof(buffer) - buffer_loc,
					"=%s", arg);
		}

		// Finish off the option with a newline.

		buffer_loc += snprintf(buffer + buffer_loc, 2, "\n");
	}

	int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock_fd == -1) {
		perror("Unable to create socket");
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
		return 1;
	}

	int connected = connect(
			sock_fd, (struct sockaddr *)&remote, sizeof(remote));
	if (connected == -1) {
		perror("Unable to connect to oguri socket");
		return 1;
	}

	if (send(sock_fd, buffer, strnlen(buffer, sizeof(buffer) - 1), 0) == -1) {
		perror("Unable to send command to oguri");
		goto close_err;
	}

	int recv_len = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
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

	close(sock_fd);
	return 0;

close_err:
	close(sock_fd);
	return 1;
}
