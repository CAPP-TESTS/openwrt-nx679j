/* Harmless fixture for vfs_write tracing. No modem or network access.
 * The caller owns the trace instance. Select only this PID, then write
 * exactly 32 synthetic bytes to a new regular file with write(2).
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int write_once(const char *path, const void *data, size_t size,
		      int flags)
{
	int fd = open(path, flags | O_CLOEXEC | O_NOFOLLOW, 0600);
	ssize_t written;

	if (fd < 0) {
		perror(path);
		return 1;
	}
	written = write(fd, data, size);
	if (written != (ssize_t)size) {
		if (written >= 0)
			errno = EIO;
		perror(path);
		close(fd);
		return 1;
	}
	if (close(fd)) {
		perror("close");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	char payload[] = "0123456789abcdef0123456789abcdef";
	char pid[32];
	int length;

	_Static_assert(sizeof(payload) - 1 == 32, "fixture must be 32 bytes");
	if (argc != 3) {
		fprintf(stderr, "usage: %s set_event_pid new-fixture-file\n", argv[0]);
		return 2;
	}
	alarm(5);
	length = snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
	if (length <= 0 || (size_t)length >= sizeof(pid))
		return 1;
	if (write_once(argv[1], pid, (size_t)length, O_WRONLY | O_TRUNC))
		return 1;
	return write_once(argv[2], payload, sizeof(payload) - 1,
			  O_WRONLY | O_CREAT | O_EXCL);
}
