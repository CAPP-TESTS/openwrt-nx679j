/* Offline netlink encoding tests: no socket or interface is created. */
#define main rmnet_link_main
#include "rmnet-link.c"
#undef main
#include <assert.h>

static struct rtattr *get_attr(void *data, int len, unsigned int type)
{
	struct rtattr *a;
	for (a = data; RTA_OK(a, len); a = RTA_NEXT(a, len))
		if ((a->rta_type & NLA_TYPE_MASK) == type)
			return a;
	return NULL;
}

int main(void)
{
	struct link_request r;
	struct rtattr *a, *info, *data;
	uint32_t parent;
	uint16_t mux;
	struct ifla_rmnet_flags flags;
	int len;

	assert(build_link(&r, 17, "rmnet_data0", 1) == 0);
	assert(r.h.nlmsg_type == RTM_NEWLINK);
	assert(r.h.nlmsg_flags == (NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL));
	len = IFLA_PAYLOAD(&r.h);
	a = get_attr(IFLA_RTA(&r.ifi), len, IFLA_LINK);
	assert(a && RTA_PAYLOAD(a) == sizeof(parent));
	memcpy(&parent, RTA_DATA(a), sizeof(parent));
	assert(parent == 17);
	info = get_attr(IFLA_RTA(&r.ifi), len, IFLA_LINKINFO);
	assert(info && info->rta_type & NLA_F_NESTED);
	a = get_attr(RTA_DATA(info), RTA_PAYLOAD(info), IFLA_INFO_KIND);
	assert(a && RTA_PAYLOAD(a) == sizeof("rmnet"));
	assert(memcmp(RTA_DATA(a), "rmnet", sizeof("rmnet")) == 0);
	data = get_attr(RTA_DATA(info), RTA_PAYLOAD(info), IFLA_INFO_DATA);
	assert(data && data->rta_type & NLA_F_NESTED);
	a = get_attr(RTA_DATA(data), RTA_PAYLOAD(data), IFLA_RMNET_MUX_ID);
	assert(a && RTA_PAYLOAD(a) == sizeof(mux));
	memcpy(&mux, RTA_DATA(a), sizeof(mux));
	assert(mux == 1);
	a = get_attr(RTA_DATA(data), RTA_PAYLOAD(data), IFLA_RMNET_FLAGS);
	assert(a && RTA_PAYLOAD(a) == sizeof(flags));
	memcpy(&flags, RTA_DATA(a), sizeof(flags));
	assert(flags.flags == RMNET_FLAGS_INGRESS_DEAGGREGATION && flags.mask == UINT32_MAX);
	assert(build_link(&r, 0, "rmnet_data0", 1) < 0);
	assert(build_link(&r, 17, "abcdefghijklmnop", 1) < 0);
	assert(build_link(&r, 17, "rmnet_data0", 256) < 0);
	puts("PASS: nested rmnet netlink attributes, flags, mux and bounds");
	return 0;
}
