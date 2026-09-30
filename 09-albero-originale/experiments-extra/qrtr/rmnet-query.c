/*
 * Read-only rmnet_ipa ioctls from the local stock kernel UAPI:
 * kernel_platform/msm-kernel/include/uapi/linux/msm_rmnet.h.
 * Build with that include/uapi directory supplied via -idirafter.
 * No raw control-device access and no data-path configuration.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <net/if.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/msm_rmnet.h>

_Static_assert(sizeof(struct rmnet_ioctl_extended_s) == 24,
	       "stock rmnet extended ioctl size");
_Static_assert(offsetof(struct rmnet_ioctl_extended_s, u) == 4,
	       "stock rmnet extended ioctl union offset");

static int query(int fd, const char *name, unsigned int selector,
		 const char *label)
{
	struct rmnet_ioctl_extended_s data = { .extended_ioctl = selector };
	struct ifreq request = { 0 };
	int result;

	memcpy(request.ifr_name, name, strlen(name) + 1);
	request.ifr_data = (char *)&data;
	result = ioctl(fd, RMNET_IOCTL_EXTENDED, &request);
	if (result < 0) {
		fprintf(stderr, "%s: %s (errno=%d)\n", label,
			strerror(errno), errno);
		return 1;
	}
	if (data.extended_ioctl != selector) {
		fprintf(stderr, "%s: selector changed unexpectedly\n", label);
		return 1;
	}
	if (selector == RMNET_IOCTL_GET_DRIVER_NAME)
		printf("%s=%.*s\n", label, IFNAMSIZ, (char *)data.u.if_name);
	else if (selector == RMNET_IOCTL_GET_EP_PAIR)
		printf("%s consumer=%u producer=%u\n", label,
		       data.u.ipa_ep_pair.consumer_pipe_num,
		       data.u.ipa_ep_pair.producer_pipe_num);
	else
		printf("%s=%u (0x%x)\n", label, data.u.data, data.u.data);
	return 0;
}

int main(int argc, char **argv)
{
	int fd, result = 0;

	if (argc != 2 || !*argv[1] || strlen(argv[1]) >= IFNAMSIZ) {
		fprintf(stderr, "usage: %s <rmnet_ipa-interface>\n", argv[0]);
		return 2;
	}
	if (!if_nametoindex(argv[1])) {
		perror("if_nametoindex");
		return 1;
	}
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}
	alarm(5);
	printf("interface=%s ioctl=0x%x struct_size=%zu\n", argv[1],
	       RMNET_IOCTL_EXTENDED, sizeof(struct rmnet_ioctl_extended_s));
	result |= query(fd, argv[1], RMNET_IOCTL_GET_DRIVER_NAME, "driver");
	result |= query(fd, argv[1], RMNET_IOCTL_GET_SUPPORTED_FEATURES, "features");
	result |= query(fd, argv[1], RMNET_IOCTL_GET_EPID, "endpoint");
	result |= query(fd, argv[1], RMNET_IOCTL_GET_EP_PAIR, "pipes");
	result |= query(fd, argv[1], RMNET_IOCTL_GET_MRU, "mru");
	result |= query(fd, argv[1], RMNET_IOCTL_GET_SG_SUPPORT, "scatter_gather");
	alarm(0);
	close(fd);
	return result;
}
