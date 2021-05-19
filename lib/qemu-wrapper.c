#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void usage(void)
{
	puts("qemu-wrapper - wrapper for qemu to catch test success/failure\n"
	     "Usage: qemu-wrapper [OPTIONS] -- <qemu-command>\n"
	     "\n"
	     "Options\n"
	     "      -S              Exit on success\n"
	     "      -F              Exit on failure\n"
	     "      -T TIMEOUT      Timeout after TIMEOUT seconds\n"
	     "      -h              Display this help and exit\n");
}

int main(int argc, char *argv[])
{
	bool exit_on_success = false;
	bool exit_on_failure = false;
	bool exit_on_timeout = false;
	unsigned long timeout = 0;
	int opt, ret = EXIT_FAILURE;
	struct timespec start, ts;

	//setlinebuf(stdin);
	//setlinebuf(stdout);

	if (clock_gettime(CLOCK_MONOTONIC, &start)) {
		fprintf(stderr, "clock_gettime error: %m\n");
		exit(EXIT_FAILURE);
	}

	while ((opt = getopt(argc, argv, "SFT:h")) != -1) {
		switch (opt) {
		case 'S':
			exit_on_success = true;
			break;
		case 'F':
			exit_on_failure = true;
			break;
		case 'T':
			errno = 0;
			timeout = strtoul(optarg, NULL, 10);
			if (errno) {
				fprintf(stderr, "error parsing timeout: %m\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case '?':
			usage();
			exit(EXIT_FAILURE);
		}
	}

#if 1
	int pipefd[2];
	if (pipe(pipefd)) {
		fprintf(stderr, "error creating pipe: %m\n");
		exit(EXIT_FAILURE);
	}

	pid_t child = fork();
	if (child < 0) {
		fprintf(stderr, "fork error: %m\n");
		exit(EXIT_FAILURE);
	}

	if (!child) {
#if 1
		if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
			fprintf(stderr, "dup2 error: %m\n");
			exit(EXIT_FAILURE);
		}
#endif
		close(pipefd[1]);

		execvp(argv[optind], argv + optind);
		fprintf(stderr, "error execing %s: %m\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	FILE *childf = fdopen(pipefd[0], "r");
	if (!childf) {
		fprintf(stderr, "fdopen error: %m\n");
		goto out;
	}
#else
	unsigned i, cmdlen = 0;
	for (i = optind; argv[i]; i++)
		cmdlen += strlen(argv[i]) + 1;

	char *cmd = malloc(cmdlen + 1);
	*cmd = 0;
	cmdlen = 0;
	for (i = optind; argv[i]; i++) {
		unsigned len = strlen(argv[i]);
		memcpy(cmd + cmdlen, argv[i], len);
		cmdlen += len;
		cmd[cmdlen++] = ' ';
	}
	cmd[cmdlen] = 0;

	FILE *childf = popen(cmd, "r");
	free(cmd);
#endif

	size_t n = 0, len;
	char *line = NULL;

	while ((len = getline(&line, &n, childf)) >= 0) {
		if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
			fprintf(stderr, "clock_gettime error: %m\n");
			exit(EXIT_FAILURE);
		}

		unsigned long elapsed = ts.tv_sec - start.tv_sec;
		printf("%.4lu ", elapsed);
		fputs(line, stdout);

		if (exit_on_success &&
		    strstr(line, "TEST SUCCESS")) {
			ret = 0;
			break;
		}

		if (exit_on_failure && strstr(line, "TEST FAILED"))
			break;

		if (exit_on_failure && strstr(line, "Kernel panic")) {
			/* Read output for two more seconds, then exit */
			break;
		}
	}
out:
	//kill(child, SIGKILL);
	exit(ret);
}
