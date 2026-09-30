/* netlink-watch3.c — NX679J: announce new netdevs to ModemManager + OpenWrt.
 *
 * Why: on this image the kernel does not deliver kobject uevents for
 * runtime-created netdevs (procd hotplug-call is never invoked; netifd
 * never creates its device object; MM never learns about the new port).
 * We listen on rtnetlink (always delivered) and:
 *   1) report the kernel event to ModemManager via mmcli (direct), and
 *   2) synthesize a kobject uevent to the netlink uevent group so that
 *      procd (hotplug scripts) and netifd (device tracking) see it too.
 *
 * v3: markers removed -- libqmi patch "probe-mux-ebusy" now verifies each
 * candidate mux with a real create+delete roundtrip, so busy/ghost muxes
 * are skipped by libqmi itself. Simple, no retry loops, no marker links.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

static int interesting(const char *ifn)
{
    return (!strncmp(ifn, "rmnet", 5) || !strncmp(ifn, "qmapmux", 7) ||
            !strncmp(ifn, "qmimux", 6) || !strncmp(ifn, "mbimmux", 7));
}

static void report_mm(const char *action, const char *ifn)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "mmcli --report-kernel-event=\"action=%s,name=%s,subsystem=net\" >/dev/null 2>&1",
             action, ifn);
    system(cmd);
}

static void inject_uevent(const char *action, const char *ifn)
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    struct sockaddr_nl dst;
    char payload[512];
    char buf[1024];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    int n;

    if (fd < 0) return;
    memset(&dst, 0, sizeof(dst));
    dst.nl_family = AF_NETLINK;
    dst.nl_pid = 1;            /* kernel */
    dst.nl_groups = 1;         /* udev/procd group */

    n = snprintf(payload, sizeof(payload) - 1,
                 "%s@/devices/virtual/net/%s\0ACTION=%s\0DEVPATH=/devices/virtual/net/%s\0SUBSYSTEM=net\0INTERFACE=%s\0",
                 action, ifn, action, ifn, ifn);

    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(n);
    nlh->nlmsg_type = 0;
    nlh->nlmsg_flags = 0;
    nlh->nlmsg_seq = 0;
    nlh->nlmsg_pid = 0;
    memcpy(NLMSG_DATA(nlh), payload, n);

    sendto(fd, buf, nlh->nlmsg_len, 0, (struct sockaddr *)&dst, sizeof(dst));
    close(fd);
}

int main(void)
{
    int fd;
    struct sockaddr_nl sa;
    char buf[8192];

    fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) { perror("socket"); return 1; }
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_LINK;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { perror("bind"); return 1; }

    setsid();
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        struct nlmsghdr *h;
        if (n <= 0) continue;
        for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, (unsigned int)n); h = NLMSG_NEXT(h, n)) {
            struct ifinfomsg *ifi;
            struct rtattr *a;
            int alen;
            const char *act;
            if (h->nlmsg_type != RTM_NEWLINK && h->nlmsg_type != RTM_DELLINK)
                continue;
            ifi = NLMSG_DATA(h);
            alen = h->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi));
            for (a = IFLA_RTA(ifi); RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
                if (a->rta_type == IFLA_IFNAME) {
                    char ifn[64];
                    memset(ifn, 0, sizeof(ifn));
                    snprintf(ifn, sizeof(ifn), "%s", (char *)RTA_DATA(a));
                    if (!interesting(ifn))
                        break;
                    act = (h->nlmsg_type == RTM_NEWLINK) ? "add" : "remove";
                    report_mm(act, ifn);
                    inject_uevent(act, ifn);
                    break;
                }
            }
        }
    }
    return 0;
}
