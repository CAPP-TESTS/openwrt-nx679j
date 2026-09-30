/* Stock rmnet_config.c: RTM_NEWLINK, kind=rmnet, LINK, MUX_ID(u16), FLAGS.
 * Creates only a link; never assigns an address or route or starts a bearer.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
/* Same values/layout in the stock kernel include/uapi/linux/if_link.h. */
_Static_assert(IFLA_RMNET_MUX_ID == 1 && IFLA_RMNET_FLAGS == 2, "stock attributes");
_Static_assert(RMNET_FLAGS_INGRESS_DEAGGREGATION == 1, "stock deaggregation flag");
_Static_assert(sizeof(struct ifla_rmnet_flags) == 8, "stock rmnet flags layout");

struct link_request {
	struct nlmsghdr h;
	struct ifinfomsg ifi;
	uint8_t attrs[256];
};

static int put_attr(struct link_request *r, unsigned type,
		    const void *value, size_t len)
{
	size_t off = NLMSG_ALIGN(r->h.nlmsg_len);
	size_t used = RTA_ALIGN(RTA_LENGTH(len));
	struct rtattr *a;
	if (off > sizeof(*r) || used > sizeof(*r) - off)
		return -1;
	a = (struct rtattr *)((uint8_t *)r + off);
	a->rta_type = (unsigned short)type;
	a->rta_len = (unsigned short)RTA_LENGTH(len);
	if (len)
		memcpy(RTA_DATA(a), value, len);
	r->h.nlmsg_len = (uint32_t)(off + used);
	return (int)off;
}

static int build_link(struct link_request *r, uint32_t parent,
		      const char *name, uint16_t mux)
{
	struct ifla_rmnet_flags flags = {
		.flags = RMNET_FLAGS_INGRESS_DEAGGREGATION,
		.mask = UINT32_MAX,
	};
	int info, data;
	if (!parent || !*name || strlen(name) >= IFNAMSIZ || mux > UINT8_MAX)
		return -1;
	memset(r, 0, sizeof(*r));
	r->h.nlmsg_len = NLMSG_LENGTH(sizeof(r->ifi));
	r->h.nlmsg_type = RTM_NEWLINK;
	r->h.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
	r->h.nlmsg_seq = 1;
	r->ifi.ifi_family = AF_UNSPEC;
	if (put_attr(r, IFLA_LINK, &parent, sizeof(parent)) < 0 ||
	    put_attr(r, IFLA_IFNAME, name, strlen(name) + 1) < 0)
		return -1;
	info = put_attr(r, IFLA_LINKINFO | NLA_F_NESTED, NULL, 0);
	if (info < 0 || put_attr(r, IFLA_INFO_KIND, "rmnet", sizeof("rmnet")) < 0)
		return -1;
	data = put_attr(r, IFLA_INFO_DATA | NLA_F_NESTED, NULL, 0);
	if (data < 0 || put_attr(r, IFLA_RMNET_MUX_ID, &mux, sizeof(mux)) < 0 ||
	    put_attr(r, IFLA_RMNET_FLAGS, &flags, sizeof(flags)) < 0)
		return -1;
	((struct rtattr *)((uint8_t *)r + data))->rta_len =
		(unsigned short)(r->h.nlmsg_len - (unsigned)data);
	((struct rtattr *)((uint8_t *)r + info))->rta_len =
		(unsigned short)(r->h.nlmsg_len - (unsigned)info);
	return 0;
}

static int create_link(struct link_request *r)
{
	struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
	struct timeval timeout = { .tv_sec = 5 };
	int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	int result = 1;
	if (fd < 0) {
		perror("netlink socket");
		return 1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
	    sendto(fd, r, r->h.nlmsg_len, 0, (struct sockaddr *)&kernel,
		   sizeof(kernel)) != (ssize_t)r->h.nlmsg_len) {
		perror("netlink send");
		goto done;
	}
	for (;;) {
		uint8_t buf[4096];
		struct sockaddr_nl peer = { 0 };
		struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
		struct msghdr msg = {
			.msg_name = &peer, .msg_namelen = sizeof(peer),
			.msg_iov = &iov, .msg_iovlen = 1,
		};
		struct nlmsghdr *h;
		ssize_t n = recvmsg(fd, &msg, 0);
		int remaining;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("netlink ACK");
			goto done;
		}
		if (!n || msg.msg_flags & MSG_TRUNC || peer.nl_pid != 0) {
			fputs("invalid netlink ACK datagram\n", stderr);
			goto done;
		}
		remaining = (int)n;
		for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, remaining);
		     h = NLMSG_NEXT(h, remaining)) {
			struct nlmsgerr *err;
			if (h->nlmsg_seq != r->h.nlmsg_seq)
				continue;
			if (h->nlmsg_type != NLMSG_ERROR ||
			    h->nlmsg_len < NLMSG_LENGTH(sizeof(*err))) {
				fputs("unexpected netlink ACK\n", stderr);
				goto done;
			}
			err = NLMSG_DATA(h);
			if (err->error) {
				fprintf(stderr, "RTM_NEWLINK: %s (%d)\n",
					strerror(-err->error), err->error);
				goto done;
			}
			result = 0;
			goto done;
		}
	}
done:
	close(fd);
	return result;
}

int main(int argc, char **argv)
{
	struct link_request request;
	unsigned int parent;
	unsigned long mux;
	char *end;
	if (argc != 4)
		goto usage;
	errno = 0;
	mux = strtoul(argv[3], &end, 0);
	if (!*argv[3] || *argv[3] == '-' || *end || errno || mux > UINT8_MAX)
		goto usage;
	parent = if_nametoindex(argv[1]);
	if (!parent || build_link(&request, parent, argv[2], (uint16_t)mux) < 0) {
		fputs("invalid parent, name, or mux\n", stderr);
		return 1;
	}
	if (if_nametoindex(argv[2])) {
		fputs("refused: target interface already exists\n", stderr);
		return 1;
	}
	if (create_link(&request))
		return 1;
	printf("CREATED name=%s parent=%s(%u) mux=%lu flags=0x%x\n",
	       argv[2], argv[1], parent, mux, RMNET_FLAGS_INGRESS_DEAGGREGATION);
	return 0;
usage:
	fprintf(stderr, "usage: %s <parent> <name> <mux-id 0..255>\n", argv[0]);
	return 2;
}
