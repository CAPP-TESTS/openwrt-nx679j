/*
 * Stock msm_rmnet.h UAPI; no direct modem/control-device writes.
 * rmnet_ipa.c handle3_{ingress,egress}_format accepts the QMAP format.
 * Ingress aggregation is enabled only with values derived from stock IPA:
 * IPA_GENERIC_RX_BUFF_BASE_SZ=8192 and IPA_GENERIC_AGGR_PKT_LIMIT=0.
 * Build with the stock include/uapi directory supplied via -idirafter.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <limits.h>
#include <net/if.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/ethtool.h>
#include <linux/msm_rmnet.h>
#include <linux/sockios.h>

_Static_assert(sizeof(struct rmnet_ioctl_extended_s) == 24, "stock ioctl size");
_Static_assert(offsetof(struct rmnet_ioctl_extended_s, u) == 4, "stock union");
enum {
	/* ipa_dp.c: IPA_GENERIC_RX_BUFF_BASE_SZ and IPA_GENERIC_AGGR_PKT_LIMIT. */
	STOCK_IPA_GENERIC_RX_BUFF_BASE_SZ = 8192,
	STOCK_IPA_GENERIC_AGGR_PKT_LIMIT = 0
};

static void pipe_request(struct rmnet_ioctl_extended_s *data, int ingress)
{
	memset(data, 0, sizeof(*data));
	if (ingress) {
		data->extended_ioctl = RMNET_IOCTL_SET_INGRESS_DATA_FORMAT;
		data->u.data = RMNET_IOCTL_INGRESS_FORMAT_MAP |
			       RMNET_IOCTL_INGRESS_FORMAT_DEAGGREGATION |
			       RMNET_IOCTL_INGRESS_FORMAT_DEMUXING |
			       RMNET_IOCTL_INGRESS_FORMAT_AGG_DATA;
		/* Source-backed stock IPA values, not Android UL netlink values. */
		data->u.ingress_format.agg_size =
			STOCK_IPA_GENERIC_RX_BUFF_BASE_SZ;
		data->u.ingress_format.agg_count =
			STOCK_IPA_GENERIC_AGGR_PKT_LIMIT;
	} else {
		data->extended_ioctl = RMNET_IOCTL_SET_EGRESS_DATA_FORMAT;
		data->u.data = RMNET_IOCTL_EGRESS_FORMAT_MAP |
			       RMNET_IOCTL_EGRESS_FORMAT_MUXING;
	}
}

static int mux_request(struct rmnet_ioctl_extended_s *data,
		       const char *text, const char *name)
{
	char *end;
	unsigned long mux;
	errno = 0;
	mux = strtoul(text, &end, 0);
	/* rmnet_config.c validates against RMNET_MAX_LOGICAL_EP (256). */
	if (!*text || *text == '-' || *end || errno || mux > UINT8_MAX ||
	    !*name || strlen(name) >= IFNAMSIZ)
		return -1;
	memset(data, 0, sizeof(*data));
	data->extended_ioctl = RMNET_IOCTL_ADD_MUX_CHANNEL;
	data->u.rmnet_mux_val.mux_id = (uint32_t)mux;
	memcpy(data->u.rmnet_mux_val.vchannel_name, name, strlen(name) + 1);
	return 0;
}

static int stock_ioctl(int fd, const char *name,
		       struct rmnet_ioctl_extended_s *data)
{
	struct ifreq request = { 0 };
	unsigned int selector = data->extended_ioctl;
	int rc;
	memcpy(request.ifr_name, name, strlen(name) + 1);
	request.ifr_data = (char *)data;
	rc = ioctl(fd, RMNET_IOCTL_EXTENDED, &request);
	if (rc < 0) {
		fprintf(stderr, "ioctl=0x%x selector=%u: %s (errno=%d)\n",
			RMNET_IOCTL_EXTENDED, selector, strerror(errno), errno);
		return -1;
	}
	if (data->extended_ioctl != selector) {
		fputs("unexpected ioctl selector in response\n", stderr);
		return -1;
	}
	return 0;
}

static int require_plain_qmap_features(int fd, const char *name)
{
	/* Stock include/linux/netdev_features.h: GRO_HW bit55, feature count59. */
	enum { STOCK_GRO_HW_BIT = 55, STOCK_FEATURE_BLOCKS = (59 + 31) / 32 };
	struct {
		uint32_t cmd, size;
		struct ethtool_get_features_block blocks[STOCK_FEATURE_BLOCKS];
	} features = { .cmd = ETHTOOL_GFEATURES, .size = STOCK_FEATURE_BLOCKS };
	struct ifreq request = { 0 };
	unsigned int i;

	memcpy(request.ifr_name, name, strlen(name) + 1);
	request.ifr_data = (char *)&features;
	if (ioctl(fd, SIOCETHTOOL, &request) < 0 ||
	    features.size != STOCK_FEATURE_BLOCKS) {
		fputs("refused: cannot verify stock netdev feature bitmap\n", stderr);
		return -1;
	}
	for (i = 0; i < features.size; i++)
		printf("NETDEV features_block=%u active=0x%08x available=0x%08x\n",
		       i, features.blocks[i].active, features.blocks[i].available);
	if (features.blocks[STOCK_GRO_HW_BIT / 32].active &
	    (1u << (STOCK_GRO_HW_BIT % 32))) {
		fputs("refused: GRO_HW is active; plain-QMAP pipe setup is unsuitable\n",
		      stderr);
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct rmnet_ioctl_extended_s data;
	unsigned int required;
	int pipes, ingress_only, egress_only, fd, result = 1;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 3 || !*argv[1] || strlen(argv[1]) >= IFNAMSIZ)
		goto usage;
	pipes = strcmp(argv[2], "pipes") == 0;
	ingress_only = strcmp(argv[2], "ingress") == 0;
	egress_only = strcmp(argv[2], "egress") == 0;
	if (pipes || ingress_only || egress_only) {
		if (argc != 3)
			goto usage;
		required = (egress_only ? 0 : RMNET_IOCTL_FEAT_SET_INGRESS_DATA_FORMAT) |
			   (ingress_only ? 0 : RMNET_IOCTL_FEAT_SET_EGRESS_DATA_FORMAT);
	} else {
		if (argc != 5 || strcmp(argv[2], "notify-mux") ||
		    mux_request(&data, argv[3], argv[4]) < 0)
			goto usage;
		required = RMNET_IOCTL_FEAT_NOTIFY_MUX_CHANNEL;
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
	alarm(10);
	memset(&data, 0, sizeof(data));
	data.extended_ioctl = RMNET_IOCTL_GET_DRIVER_NAME;
	if (stock_ioctl(fd, argv[1], &data) < 0 ||
	    strncmp((char *)data.u.if_name, "rmnet_ipa0", IFNAMSIZ)) {
		fputs("refused: expected the measured rmnet_ipa0 driver\n", stderr);
		goto done;
	}
	memset(&data, 0, sizeof(data));
	data.extended_ioctl = RMNET_IOCTL_GET_SUPPORTED_FEATURES;
	if (stock_ioctl(fd, argv[1], &data) < 0)
		goto done;
	if ((data.u.data & required) != required) {
		fputs("refused: required ioctl features are not advertised\n", stderr);
		goto done;
	}
	if (pipes || ingress_only || egress_only) {
		if (require_plain_qmap_features(fd, argv[1]) < 0)
			goto done;
		if (!egress_only) {
			pipe_request(&data, 1);
			printf("INGRESS selector=%u flags=0x%x\n",
			       data.extended_ioctl, data.u.data);
			if (stock_ioctl(fd, argv[1], &data) < 0)
				goto done;
			puts("INGRESS accepted");
		}
		if (!ingress_only) {
			pipe_request(&data, 0);
			printf("EGRESS selector=%u flags=0x%x\n",
			       data.extended_ioctl, data.u.data);
			if (stock_ioctl(fd, argv[1], &data) < 0) {
				if (pipes)
					fputs("partial setup: ingress accepted, egress failed; do not retry blindly\n",
					      stderr);
				goto done;
			}
			puts("EGRESS accepted");
		}
	} else {
		if (mux_request(&data, argv[3], argv[4]) < 0)
			goto done;
		printf("MUX selector=%u id=%u name=%s\n", data.extended_ioctl,
		       data.u.rmnet_mux_val.mux_id, argv[4]);
		if (stock_ioctl(fd, argv[1], &data) < 0)
			goto done;
		puts("MUX accepted");
	}
	result = 0;
done:
	alarm(0);
	close(fd);
	return result;
usage:
	fprintf(stderr, "usage: %s <rmnet_ipa-if> pipes|ingress|egress|notify-mux <id> <name>\n",
		argv[0]);
	return 2;
}
