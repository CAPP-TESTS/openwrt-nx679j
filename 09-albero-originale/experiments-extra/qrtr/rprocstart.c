/*
 * rprocstart - write a remoteproc state file from a FORKED child, so the
 * caller never blocks on the synchronous rproc start (which waits for the
 * firmware boot handshake).  The child appends its exit status to
 * /tmp/rprocstart.status for later inspection.
 *
 * usage: rprocstart /sys/class/remoteproc/remoteproc3 [start|stop]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, char **argv)
{
	const char *path, *cmd = "start";
	pid_t p;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <state-path> [start|stop]\n", argv[0]);
		return 2;
	}
	path = argv[1];
	if (argc > 2)
		cmd = argv[2];

	p = fork();
	if (p < 0) {
		perror("fork");
		return 1;
	}
	if (p == 0) {
		int fd = open(path, O_WRONLY);
		ssize_t w = -1;
		int fd2;

		if (fd >= 0) {
			w = write(fd, cmd, strlen(cmd));
			close(fd);
		}
		fd2 = open("/tmp/rprocstart.status", O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd2 >= 0) {
			char b[160];
			int n = snprintf(b, sizeof(b), "%s <- %s: rc=%zd errno=%d(%s)\n",
					 path, cmd, w, w < 0 ? errno : 0,
					 w < 0 ? strerror(errno) : "ok");

			if (n > 0)
				(void)!write(fd2, b, (size_t)n);
			close(fd2);
		}
		_exit(w >= 0 ? 0 : 1);
	}
	printf("spawned pid=%d: %s <- %s\n", (int)p, path, cmd);
	return 0;
}
