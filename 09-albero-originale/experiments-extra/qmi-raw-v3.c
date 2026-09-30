/*
 * qmi-raw-v3.c - one controlled QMUX CTL transaction on glink_pkt.
 *
 * The Qualcomm userspace QMI wire format is reconstructed from libqmi's
 * qmux_header/control_header layout and the glink_pkt driver: glink_pkt
 * forwards each write as one rpmsg packet, so this program sends one complete
 * QMUX frame and reads one complete rpmsg packet.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_DEV "/dev/smdcntl8"
#define GLINK_MAJOR 504
#define GLINK_MINOR 2
#define READ_TIMEOUT_MS 6000

static void ensure_node(const char *path)
{
	struct stat st;

	if (stat(path, &st) == 0)
		return;
	if (mknod(path, S_IFCHR | 0666,
		  (dev_t)GLINK_MAJOR << 8 | GLINK_MINOR) != 0)
		fprintf(stderr, "mknod %s: %s\n", path, strerror(errno));
}

static void dump_hex(const char *tag, const unsigned char *buf, size_t len)
{
	size_t i;

	printf("%s (%zu bytes)", tag, len);
	for (i = 0; i < len; i++) {
		if ((i & 15) == 0)
			printf("\n  ");
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

static int send_ctl_get_version(int fd)
{
	/*
	 * marker 0x01
	 * QMUX length = frame length minus marker = 0x000b
	 * QMUX flags/service/client = 00/00/00
	 * CTL flags/transaction/message/tlv-length = 00/01/0021/0000
	 */
	static const unsigned char frame[] = {
		0x01, 0x0b, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x01, 0x21, 0x00, 0x00, 0x00,
	};
	ssize_t n;

	dump_hex("TX", frame, sizeof(frame));
	n = write(fd, frame, sizeof(frame));
	if (n != (ssize_t)sizeof(frame)) {
		fprintf(stderr, "write returned %zd: %s\n", n,
			strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : DEFAULT_DEV;
	unsigned char buf[8192];
	struct pollfd pfd;
	ssize_t n;
	int fd, rc;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	ensure_node(path);
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return 1;
	}

	if (send_ctl_get_version(fd) != 0) {
		close(fd);
		return 1;
	}

	pfd.fd = fd;
	pfd.events = POLLIN | POLLERR | POLLHUP;
	rc = poll(&pfd, 1, READ_TIMEOUT_MS);
	if (rc < 0) {
		fprintf(stderr, "poll: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	if (rc == 0) {
		printf("RX timeout after %d ms\n", READ_TIMEOUT_MS);
		close(fd);
		return 2;
	}
	printf("poll revents=0x%x\n", pfd.revents);

	n = read(fd, buf, sizeof(buf));
	if (n < 0) {
		fprintf(stderr, "read: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	if (n == 0) {
		printf("RX EOF\n");
		close(fd);
		return 2;
	}
	dump_hex("RX", buf, (size_t)n);
	close(fd);
	return 0;
}
