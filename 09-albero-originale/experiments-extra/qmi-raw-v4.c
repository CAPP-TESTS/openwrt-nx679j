/* qmi-raw-v4.c: probe one glink_pkt QMI channel without blocking open. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEV_DEFAULT "/dev/smdcntl8"
#define GLINK_MAJOR 502
#define GLINK_MINOR 2
#define TIMEOUT_MS 5000

static void dump_hex(const char *tag, const unsigned char *p, size_t n)
{
	size_t i;

	printf("%s (%zu bytes)", tag, n);
	for (i = 0; i < n; i++) {
		if (!(i & 15))
			printf("\n  ");
		printf("%02x ", p[i]);
	}
	printf("\n");
}



int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : DEV_DEFAULT;
	/* QMUX marker + 4-byte QMUX header + 6-byte CTL header. */
	static const unsigned char tx[] = {
		0x01, 0x0b, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x01, 0x21, 0x00, 0x00, 0x00,
	};
	unsigned char rx[8192];
	struct pollfd pfd;
	ssize_t n;
	int fd, rc;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	printf("before open path=%s\n", path);
	/* The init already creates the glink_pkt nodes; do not stat/mknod a live
	 * driver node here: the vendor SELinux policy can block that lookup. */
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open(%s): %s (errno=%d)\n", path,
			strerror(errno), errno);
		return 1;
	}
	printf("open ok fd=%d\n", fd);

	dump_hex("TX", tx, sizeof(tx));
	n = write(fd, tx, sizeof(tx));
	printf("write returned %zd errno=%d (%s)\n", n, errno, strerror(errno));
	if (n != (ssize_t)sizeof(tx)) {
		close(fd);
		return 1;
	}

	pfd.fd = fd;
	pfd.events = POLLIN | POLLERR | POLLHUP;
	rc = poll(&pfd, 1, TIMEOUT_MS);
	printf("poll rc=%d revents=0x%x errno=%d (%s)\n", rc, pfd.revents,
		errno, strerror(errno));
	if (rc <= 0) {
		close(fd);
		return rc ? 1 : 2;
	}
	n = read(fd, rx, sizeof(rx));
	printf("read returned %zd errno=%d (%s)\n", n, errno, strerror(errno));
	if (n > 0)
		dump_hex("RX", rx, (size_t)n);
	close(fd);
	return n > 0 ? 0 : 1;
}
