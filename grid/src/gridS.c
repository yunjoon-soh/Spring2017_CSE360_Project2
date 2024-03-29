#include "gridS.h"

int main(int argc, char** argv) {
	char* end_ptr; // used for strtol
	int opt_a = 0, opt_n = 0, opt_d = 0, opt;
	long max_connection = 10; // default max: 10
	while ( (opt = getopt(argc, argv, "an:m:d")) != -1 ) {
		switch (opt) {
		case 'a':
			opt_a = 1;
			break;
		case 'n':
			// bad because it does not check the length
			// max_connection = atoi(optarg);

			// instead use this
			max_connection = Strtol(optarg, &end_ptr, 10);

			opt_n = 1;
			break;
		// case 'm': maybe a config file will be better way to handle this
		case 'd':
			opt_d++;
			break;
		default:
			fprintf(stderr, "%s\n", USAGE);
			exit(EXIT_FAILURE);
		}
	}

	DEBUG(1, "Option a: %d, Option n: %d %ld, Option d: %d\n", opt_a, opt_n, max_connection, opt_d);

	fprintf(stdout, "Starting Grid Server... Version: %s (Build: %d)\n", SERVER_VERSION, BUILDS);

	server();
}
