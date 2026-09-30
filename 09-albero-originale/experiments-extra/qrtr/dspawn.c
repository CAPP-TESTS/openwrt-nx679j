/*
 * dspawn - daemonize a command: fork, setsid, redirect stdout/stderr to a
 * log file, exec.  Parent prints the child pid and exits immediately.
 *
 * usage: dspawn <logfile> <cmd> [args...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <logfile> <cmd> [args...]\n", argv[0]);
		return 2;
	}

	pid_t p = fork();
	if (p < 0) {
		perror("fork");
		return 1;
	}
	if (p > 0) {
		printf("[dspawn] pid=%d log=%s\n", (int)p, argv[1]);
		return 0;
	}

	setsid();

	int lf = open(argv[1], O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (lf >= 0) {
		dup2(lf, 1);
		dup2(lf, 2);
	}
	int dn = open("/dev/null", O_RDONLY);
	if (dn >= 0)
		dup2(dn, 0);

	execv(argv[2], &argv[2]);
	_exit(127);
}
