/*
 * holdopen - daemonize and hold a path (char device / file) open forever.
 * usage: holdopen <path> [statusfile]
 *
 * Used for downstream-kernel quirks where a subsystem is stopped unless
 * some userspace process keeps its control device open.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static void note(const char *file, const char *fmt, const char *a, long v)
{
	int lf = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (lf >= 0) {
		dprintf(lf, fmt, a, v);
		close(lf);
	}
}

int main(int argc, char **argv)
{
	pid_t pid;
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <path> [statusfile]\n", argv[0]);
		return 2;
	}
	pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}
	if (pid > 0) {
		printf("[holdopen] child pid=%d holding %s\n", (int)pid, argv[1]);
		return 0;
	}
	setsid();
	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		if (argc > 2)
			note(argv[2], "open %s failed (errno out of band)\n", argv[1], errno);
		_exit(1);
	}
	if (argc > 2)
		note(argv[2], "holding %s fd (pid alive)\n", argv[1], 0);
	for (;;)
		pause();
}
