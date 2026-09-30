/*
 * atcmd - send an AT command to a glinkpkt channel and print the reply.
 * usage: atcmd <dev> <command> [timeout_ms]
 * example: atcmd /dev/at_mdm0 'AT+CREG?' 2000
 *
 * The AT channel is a glink_pkt device: plain read/write of raw bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

int main(int argc, char **argv)
{
	const char *dev, *cmd;
	int timeout_ms = 2000;
	int fd, n;
	char buf[4096];

	if (argc < 3) {
		fprintf(stderr, "usage: %s <dev> <command> [timeout_ms]\n", argv[0]);
		return 2;
	}
	dev = argv[1];
	cmd = argv[2];
	if (argc > 3)
		timeout_ms = atoi(argv[3]);

	fd = open(dev, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return 1;
	}

	n = write(fd, cmd, strlen(cmd));
	if (n < 0) {
		fprintf(stderr, "write %s: %s\n", dev, strerror(errno));
		return 1;
	}
	write(fd, "\r", 1);

	for (;;) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };

		n = poll(&pfd, 1, timeout_ms);
		if (n <= 0)
			break;
		n = read(fd, buf, sizeof(buf) - 1);
		if (n <= 0)
			break;
		buf[n] = '\0';
		fputs(buf, stdout);
		fflush(stdout);
		timeout_ms = 300; /* shorter wait for trailing data */
	}

	close(fd);
	return 0;
}
