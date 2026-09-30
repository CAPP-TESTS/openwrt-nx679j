/*
 * Read-only RTM_GETLINK dump. Attribute IDs/layouts come from Linux UAPI.
 * No interface is created or changed. Unknown rmnet attributes are kept as hex.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static struct rtattr *attr_find(struct rtattr *a, int len, unsigned type)
{
	for (; RTA_OK(a, len); a = RTA_NEXT(a, len))
		if ((a->rta_type & NLA_TYPE_MASK) == type)
			return a;
	return NULL;
}

static void print_link(struct nlmsghdr *h)
{
	struct ifinfomsg *ifi = NLMSG_DATA(h);
	int len = IFLA_PAYLOAD(h);
	struct rtattr *attrs = IFLA_RTA(ifi);
	struct rtattr *name = attr_find(attrs, len, IFLA_IFNAME);
	struct rtattr *link = attr_find(attrs, len, IFLA_LINK);
	struct rtattr *info = attr_find(attrs, len, IFLA_LINKINFO);
	struct rtattr *kind = NULL, *data = NULL;
	uint32_t parent = 0;
	int is_rmnet = 0;

	if (link && RTA_PAYLOAD(link) == sizeof(parent))
		memcpy(&parent, RTA_DATA(link), sizeof(parent));
	printf("ifindex=%d name=%.*s parent=%u flags=0x%x", ifi->ifi_index,
	       name ? (int)RTA_PAYLOAD(name) : 0,
	       name ? (char *)RTA_DATA(name) : "", parent, ifi->ifi_flags);
	if (info) {
		kind = attr_find(RTA_DATA(info), RTA_PAYLOAD(info), IFLA_INFO_KIND);
		data = attr_find(RTA_DATA(info), RTA_PAYLOAD(info), IFLA_INFO_DATA);
	}
	if (kind) {
		printf(" kind=%.*s", (int)RTA_PAYLOAD(kind), (char *)RTA_DATA(kind));
		is_rmnet = RTA_PAYLOAD(kind) == sizeof("rmnet") &&
			   memcmp(RTA_DATA(kind), "rmnet", sizeof("rmnet")) == 0;
	}
	if (is_rmnet && data) {
		struct rtattr *mux = attr_find(RTA_DATA(data), RTA_PAYLOAD(data),
					      IFLA_RMNET_MUX_ID);
		if (mux && RTA_PAYLOAD(mux) == sizeof(uint16_t)) {
			uint16_t id;
			memcpy(&id, RTA_DATA(mux), sizeof(id));
			printf(" mux_id=%u", id);
		}
	}
	putchar('\n');
	if (is_rmnet && data) {
		struct rtattr *a = RTA_DATA(data);
		int remaining = RTA_PAYLOAD(data);
		for (; RTA_OK(a, remaining); a = RTA_NEXT(a, remaining)) {
			const unsigned char *v = RTA_DATA(a);
			unsigned int i;
			printf("  rmnet_attr=%u bytes=", a->rta_type & NLA_TYPE_MASK);
			for (i = 0; i < RTA_PAYLOAD(a); i++)
				printf("%02x", v[i]);
			putchar('\n');
		}
	}
}

int main(void)
{
	struct {
		struct nlmsghdr h;
		struct ifinfomsg ifi;
	} req = {
		.h = {
			.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
			.nlmsg_type = RTM_GETLINK,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
			.nlmsg_seq = 1,
		},
		.ifi.ifi_family = AF_UNSPEC,
	};
	struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
	struct timeval timeout = { .tv_sec = 5 };
	int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (fd < 0) {
		perror("netlink socket");
		return 1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
	    sendto(fd, &req, req.h.nlmsg_len, 0,
		   (struct sockaddr *)&kernel, sizeof(kernel)) < 0) {
		perror("netlink request");
		return 1;
	}
	for (;;) {
		char buf[65536];
		struct sockaddr_nl peer;
		struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
		struct msghdr msg = {
			.msg_name = &peer, .msg_namelen = sizeof(peer),
			.msg_iov = &iov, .msg_iovlen = 1,
		};
		struct nlmsghdr *h;
		int remaining;
		ssize_t n = recvmsg(fd, &msg, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("netlink receive");
			return 1;
		}
		if (!n || (msg.msg_flags & MSG_TRUNC) || peer.nl_pid != 0) {
			fprintf(stderr, "Invalid or truncated netlink dump\n");
			return 1;
		}
		remaining = (int)n;
		for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, remaining);
		     h = NLMSG_NEXT(h, remaining)) {
			if (h->nlmsg_seq != req.h.nlmsg_seq)
				continue;
			if (h->nlmsg_flags & NLM_F_DUMP_INTR) {
				fprintf(stderr, "Netlink dump interrupted\n");
				return 1;
			}
			if (h->nlmsg_type == NLMSG_DONE) {
				close(fd);
				return 0;
			}
			if (h->nlmsg_type == NLMSG_ERROR) {
				fprintf(stderr, "Netlink error response\n");
				return 1;
			}
			if (h->nlmsg_type == RTM_NEWLINK &&
			    h->nlmsg_len >= NLMSG_LENGTH(sizeof(struct ifinfomsg)))
				print_link(h);
		}
	}
}
