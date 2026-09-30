/* No socket or ioctl is executed by these offline UAPI layout tests. */
#define main rmnet_cli_main
#define socket mock_socket
#define ioctl mock_ioctl
#define close mock_close
#define alarm mock_alarm
#define if_nametoindex mock_if_nametoindex
#include "rmnet-config.c"
#undef main
#undef socket
#undef ioctl
#undef close
#undef alarm
#undef if_nametoindex
#include <assert.h>
#include <stdarg.h>

static unsigned int selectors[2];
static unsigned int selector_count;

int mock_socket(int domain, int type, int protocol)
{
	assert(domain == AF_INET && type == SOCK_DGRAM && protocol == 0);
	return 42;
}

int mock_close(int fd)
{
	assert(fd == 42);
	return 0;
}

unsigned int mock_alarm(unsigned int seconds)
{
	assert(seconds == 0 || seconds == 10);
	return 0;
}

unsigned int mock_if_nametoindex(const char *name)
{
	assert(strcmp(name, "rmnet_ipa0") == 0);
	return 1;
}

int mock_ioctl(int fd, unsigned long request, ...)
{
	struct ifreq *ifr;
	struct rmnet_ioctl_extended_s *data;
	va_list args;

	assert(fd == 42);
	va_start(args, request);
	ifr = va_arg(args, struct ifreq *);
	va_end(args);
	assert(strcmp(ifr->ifr_name, "rmnet_ipa0") == 0);
	if (request == SIOCETHTOOL) {
		struct ethtool_gfeatures *features = (void *)ifr->ifr_data;
		assert(features->cmd == ETHTOOL_GFEATURES && features->size == 2);
		features->features[0].active = 0x4000;
		return 0;
	}
	assert(request == RMNET_IOCTL_EXTENDED);
	data = (void *)ifr->ifr_data;
	switch (data->extended_ioctl) {
	case RMNET_IOCTL_GET_DRIVER_NAME:
		strcpy((char *)data->u.if_name, "rmnet_ipa0");
		break;
	case RMNET_IOCTL_GET_SUPPORTED_FEATURES:
		data->u.data = 7;
		break;
	case RMNET_IOCTL_SET_INGRESS_DATA_FORMAT:
	case RMNET_IOCTL_SET_EGRESS_DATA_FORMAT:
		assert(selector_count < 2);
		selectors[selector_count++] = data->extended_ioctl;
		if (data->extended_ioctl == RMNET_IOCTL_SET_INGRESS_DATA_FORMAT) {
			assert(data->u.data == 46u);
			assert(data->u.ingress_format.__data == 46u);
			assert(data->u.ingress_format.agg_size == 8192u);
			assert(data->u.ingress_format.agg_count == 0u);
		} else {
			assert(data->u.data == 10u);
		}
		break;
	default:
		assert(!"unexpected ioctl");
	}
	return 0;
}

int main(void)
{
	struct rmnet_ioctl_extended_s data;
	unsigned char *wire = (unsigned char *)&data;
	size_t i;

	pipe_request(&data, 1);
	assert(data.extended_ioctl == 7 && data.u.data == 46);
	assert(data.u.ingress_format.agg_size == 8192);
	assert(data.u.ingress_format.agg_count == 0);
	assert(wire[0] == 7 && wire[4] == 46);
	assert(wire[8] == 0 && wire[9] == 32 && wire[10] == 0 && wire[11] == 0);
	for (i = 8; i < sizeof(data); i++)
		if (i < 12)
			continue;
		else
			assert(wire[i] == 0);
	pipe_request(&data, 0);
	assert(data.extended_ioctl == 6 && data.u.data == 10);
	assert(mux_request(&data, "255", "rmnet_data0") == 0);
	assert(data.extended_ioctl == 5 && data.u.rmnet_mux_val.mux_id == 255);
	assert(strcmp((char *)data.u.rmnet_mux_val.vchannel_name, "rmnet_data0") == 0);
	assert(mux_request(&data, "256", "rmnet_data0") < 0);
	assert(mux_request(&data, "-1", "rmnet_data0") < 0);
	assert(mux_request(&data, "1junk", "rmnet_data0") < 0);
	assert(mux_request(&data, "", "rmnet_data0") < 0);
	assert(mux_request(&data, "1", "abcdefghijklmnop") < 0);
	assert(mux_request(&data, "1", "") < 0);
	{
		char *args[] = { "rmnet-config", "rmnet_ipa0", "pipes", NULL };
		selector_count = 0;
		assert(rmnet_cli_main(3, args) == 0);
		assert(selector_count == 2 && selectors[0] == 7 && selectors[1] == 6);
		args[2] = "ingress";
		selector_count = 0;
		assert(rmnet_cli_main(3, args) == 0);
		assert(selector_count == 1 && selectors[0] == 7);
		args[2] = "egress";
		selector_count = 0;
		assert(rmnet_cli_main(3, args) == 0);
		assert(selector_count == 1 && selectors[0] == 6);
	}
	puts("PASS: stock rmnet layouts, ingress AGG_DATA 8192/0, bounded mux/name");
	puts("PASS: mocked combined/ingress-only/egress-only ioctl sequences");
	return 0;
}
