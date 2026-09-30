/*
 * finitmod - load a kernel module with finit_module(2), no userspace daemon.
 * usage: finitmod /path/module.ko
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

int main(int argc, char **argv)
{
	int fd;
	long r;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <module.ko> [param=value ...]\n", argv[0]);
		return 2;
	}
	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	/* params are not needed for the modules we load here */
	r = syscall(SYS_finit_module, fd, "", 0);
	if (r != 0) {
		fprintf(stderr, "finit_module(%s): %s (errno=%d)\n",
			argv[1], strerror(errno), errno);
		return 1;
	}
	printf("loaded %s\n", argv[1]);
	return 0;
}
