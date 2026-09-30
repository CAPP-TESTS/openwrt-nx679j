/* nl-send.c — NX679J: send a raw hex netlink message (for RE comparisons).
 * usage: nl-send <hexstring>
 * Sends the buffer verbatim to NETLINK_ROUTE (kernel), waits for ACK,
 * prints the ACK error code and the resulting interface state.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int hex2bin(const char *hex, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (hex[0] && hex[1] && n < max) {
        unsigned v;
        if (sscanf(hex, "%2x", &v) != 1) return -1;
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return (int)n;
}

int main(int argc, char **argv)
{
    uint8_t buf[4096];
    int len, fd, ret = 1;
    struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
    struct timeval tv = { .tv_sec = 5 };

    if (argc != 2) { fprintf(stderr, "usage: %s <hex>\n", argv[0]); return 2; }
    len = hex2bin(argv[1], buf, sizeof(buf));
    if (len <= 0) { fprintf(stderr, "bad hex\n"); return 2; }

    fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) { perror("socket"); return 1; }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (sendto(fd, buf, len, 0, (struct sockaddr *)&kernel, sizeof(kernel)) != len) {
        perror("sendto");
        goto done;
    }
    for (;;) {
        uint8_t rbuf[4096];
        struct sockaddr_nl peer = {0};
        struct iovec iov = { .iov_base = rbuf, .iov_len = sizeof(rbuf) };
        struct msghdr msg = { .msg_name = &peer, .msg_namelen = sizeof(peer),
                              .msg_iov = &iov, .msg_iovlen = 1 };
        ssize_t n = recvmsg(fd, &msg, 0);
        struct nlmsghdr *h;
        int remaining;
        if (n < 0) { perror("recv"); goto done; }
        if (!n || (msg.msg_flags & MSG_TRUNC) || peer.nl_pid != 0) {
            fputs("invalid ACK datagram\n", stderr);
            goto done;
        }
        remaining = (int)n;
        for (h = (struct nlmsghdr *)rbuf; NLMSG_OK(h, remaining); h = NLMSG_NEXT(h, remaining)) {
            struct nlmsgerr *err;
            if (h->nlmsg_type != NLMSG_ERROR || h->nlmsg_len < NLMSG_LENGTH(sizeof(*err))) {
                fputs("unexpected ACK\n", stderr);
                continue;
            }
            err = NLMSG_DATA(h);
            printf("ACK seq=%u error=%d (%s)\n", h->nlmsg_seq, err->error,
                   err->error ? strerror(-err->error) : "ok");
            ret = 0;
            goto done;
        }
    }
done:
    close(fd);
    return ret;
}
