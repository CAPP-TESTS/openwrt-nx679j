/*
 * qmi-qrtr.c - Minimal QMI-over-QRTR client for NX679J (SM8450) modem bring-up.
 *
 * Transport model (verified against the stock 5.10 kernel sources):
 *   - Userspace talks QMI to the modem over AF_QIPCRTR (QRTR / IPC Router).
 *   - The in-kernel name service (net/qrtr/ns.c) tracks all servers announced
 *     by every node (modem included) and answers NEW_LOOKUP from local clients.
 *   - QRTR ctrl messages are exchanged with the local node at QRTR_PORT_CTRL.
 *   - QMI wire format on the QRTR socket: struct qmi_header {u8 type; u16 txn;
 *     u16 msg_id; u16 msg_len;} __packed, followed by TLVs.
 *
 * Commands:
 *   list                          - enumerate all QRTR services (like qrtr-lookup)
 *   lookup <service> [instance]   - filtered service lookup
 *   ctl-version                   - QMI CTL 0x0021 GET_VERSION_INFO
 *   wds-status                    - WDS 0x0022 GET_PACKET_SERVICE_STATUS
 *   wds-get-settings [timeout_s]  - WDS 0x002D GET_CURRENT_SETTINGS (decoded)
 *   wds-start <apn> [ipfam]       - WDS 0x0020 START_NETWORK (ipfam: 4/6/8)
 *   wds-stop <pkt_handle>         - WDS 0x0021 STOP_NETWORK
 *   raw <service> <hexbytes>      - send a raw QMI frame, print raw reply
 *
 * Build: aarch64-linux-gnu-gcc -static -Os -Wall -Wextra -Werror qmi-qrtr.c -o qmi-qrtr
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

struct sockaddr_qrtr {
	unsigned short sq_family;
	unsigned int sq_node;
	unsigned int sq_port;
};

#define QRTR_PORT_CTRL 0xfffffffeu

#define QRTR_TYPE_HELLO      2
#define QRTR_TYPE_BYE        3
#define QRTR_TYPE_NEW_SERVER 4
#define QRTR_TYPE_DEL_SERVER 5
#define QRTR_TYPE_NEW_LOOKUP 10

struct qrtr_ctrl_pkt {
	uint32_t cmd;
	union {
		struct { uint32_t service, instance, node, port; } server;
		struct { uint32_t node, port; } client;
	};
} __attribute__((packed));

struct qmi_hdr {
	uint8_t  type;
	uint16_t txn;
	uint16_t msg_id;
	uint16_t msg_len;
} __attribute__((packed));

/* QMI message type byte */
#define QMI_REQ  0
#define QMI_RESP 2
#define QMI_IND  4

static uint16_t g_txn = 1;
static uint16_t next_txn(void)
{
	uint16_t txn = g_txn;
	g_txn = txn == 255 ? 1 : txn + 1;
	return txn;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void hexdump(const char *tag, const uint8_t *d, size_t n)
{
	size_t i;
	printf("%s (%zu bytes):", tag, n);
	for (i = 0; i < n; i++) {
		if (i % 16 == 0)
			printf("\n  ");
		printf("%02x ", d[i]);
	}
	printf("\n");
}

/* ---- QRTR ---- */

static int qrtr_open(struct sockaddr_qrtr *local)
{
	int fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
	socklen_t l = sizeof(*local);

	if (fd < 0) {
		perror("socket(AF_QIPCRTR)");
		return -1;
	}
	if (getsockname(fd, (struct sockaddr *)local, &l) < 0) {
		perror("getsockname");
		close(fd);
		return -1;
	}
	printf("[qrtr] local node=%u port=%u\n", local->sq_node, local->sq_port);
	return fd;
}

static int qrtr_sendto(int fd, unsigned node, unsigned port,
		       const void *data, size_t len)
{
	struct sockaddr_qrtr dst = { AF_QIPCRTR, node, port };

	return sendto(fd, data, len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

/* recv one datagram with timeout; returns length, 0 on timeout, -1 error */
static int qrtr_recv(int fd, struct sockaddr_qrtr *from, uint8_t *buf, size_t max,
		     int timeout_ms)
{
	struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
	fd_set rf;
	socklen_t sl = sizeof(*from);
	int r;

	FD_ZERO(&rf);
	FD_SET(fd, &rf);
	r = select(fd + 1, &rf, NULL, NULL, &tv);
	if (r <= 0)
		return r; /* 0 timeout, -1 error */
	return (int)recvfrom(fd, buf, max, 0, (struct sockaddr *)from, &sl);
}

struct srv_info {
	unsigned service, instance_field, node, port;
};

/* Register a NEW_LOOKUP and collect replies. service=instance=0 lists all. */
static int qrtr_lookup(int fd, const struct sockaddr_qrtr *local,
		       unsigned service, unsigned instance_field,
		       struct srv_info *out, int maxout, int quiet)
{
	struct qrtr_ctrl_pkt pkt;
	struct timespec ts;
	int n = 0;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	pkt.server.service = service;
	pkt.server.instance = instance_field;

	if (qrtr_sendto(fd, local->sq_node, QRTR_PORT_CTRL, &pkt, sizeof(pkt)) < 0) {
		perror("sendto(NEW_LOOKUP)");
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC, &ts);

	for (;;) {
		uint8_t buf[4096];
		struct sockaddr_qrtr from;
		int k = qrtr_recv(fd, &from, buf, sizeof(buf), 1000);

		if (k == 0) { /* 1s of silence: done */
			break;
		}
		if (k < 0) {
			if (errno == EINTR)
				continue;
			perror("recvfrom");
			break;
		}
		if (from.sq_port != QRTR_PORT_CTRL || (size_t)k < sizeof(pkt))
			continue; /* not an NS message */
		{
			struct qrtr_ctrl_pkt *p = (struct qrtr_ctrl_pkt *)buf;

			if (p->cmd == QRTR_TYPE_NEW_SERVER) {
				if (!p->server.service && !p->server.node && !p->server.port)
					break; /* end-of-listing marker */
				if (n < maxout) {
					out[n].service        = p->server.service;
					out[n].instance_field = p->server.instance;
					out[n].node           = p->server.node;
					out[n].port           = p->server.port;
				}
				n++;
				if (!quiet)
					printf("[ns] service=%u version=%u instance=%u node=%u port=%u\n",
					       p->server.service,
					       p->server.instance & 0xff,
					       p->server.instance >> 8,
					       p->server.node, p->server.port);
			} else if (p->cmd == QRTR_TYPE_DEL_SERVER) {
				if (!quiet)
					printf("[ns] (gone) service=%u node=%u port=%u\n",
					       p->server.service, p->server.node, p->server.port);
			}
		}
	}
	return n;
}

#ifndef QRTR_TYPE_HELLO
#define QRTR_TYPE_HELLO 2
#endif

/* Send a HELLO control message to a node's control port (0xffffffff = broadcast). */
static int qrtr_hello(int fd, unsigned node)
{
	struct qrtr_ctrl_pkt pkt;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_HELLO;
	return qrtr_sendto(fd, node, QRTR_PORT_CTRL, &pkt, sizeof(pkt));
}

/* Poll the NS listing once per second; print totals and WDS/CTL presence. */
static int cmd_watch(int fd, const struct sockaddr_qrtr *local, int secs)
{
	struct srv_info res[256];
	int i;

	for (i = 0; i < secs; i++) {
		int n = qrtr_lookup(fd, local, 0, 0, res, 256, 1);
		int j, wds = -1, ctl = -1, node0 = 0;

		for (j = 0; j < n; j++) {
			if (res[j].service == 1 && res[j].node != local->sq_node)
				wds = (int)res[j].port;
			if (res[j].service == 0)
				ctl = (int)res[j].port;
			if (res[j].node == 0)
				node0++;
		}
		printf("[watch t=%2d] services=%3d modem(node0)=%3d wds_port=%d ctl_port=%d\n",
		       i, n, node0, wds, ctl);
		fflush(stdout);
		sleep(1);
	}
	return 0;
}

static int find_service(int fd, const struct sockaddr_qrtr *local,
			unsigned service, struct srv_info *out)
{
	struct srv_info res[32];
	int n = qrtr_lookup(fd, local, service, 0, res, 32, 0);
	int i;

	for (i = 0; i < n && i < 32; i++) {
		if (res[i].service == service) {
			*out = res[i];
			return 0;
		}
	}
	fprintf(stderr, "[ns] service %u not found (lookup returned %d entries)\n", service, n);
	return -1;
}

/* QRTR encodes version in the low byte and instance in the upper 24 bits. */
static int find_service_exact(int fd, const struct sockaddr_qrtr *local,
			      unsigned service, unsigned version,
			      unsigned instance, struct srv_info *out)
{
	struct srv_info res[32];
	unsigned field = (instance << 8) | version;
	int n = qrtr_lookup(fd, local, service, field, res, 32, 0);
	int i, found = 0;

	if (n < 0 || n > 32)
		return -1;
	for (i = 0; i < n; i++) {
		if (res[i].service == service && res[i].instance_field == field) {
			*out = res[i];
			found++;
		}
	}
	if (found != 1) {
		fprintf(stderr, "[ns] need one service=%u version=%u instance=%u; found=%d\n",
			service, version, instance, found);
		return -1;
	}
	return 0;
}

/* ---- QMI ---- */

static size_t qmi_tlv_put(uint8_t *buf, size_t off, uint8_t type,
			  const void *data, uint16_t len)
{
	buf[off++] = type;
	buf[off++] = (uint8_t)(len & 0xff);
	buf[off++] = (uint8_t)(len >> 8);
	if (len) {
		memcpy(buf + off, data, len);
		off += len;
	}
	return off;
}

static size_t qmi_build(uint8_t *buf, uint16_t txn, uint16_t msg_id,
			const uint8_t *tlvs, size_t tlvs_len)
{
	buf[0] = QMI_REQ;
	buf[1] = (uint8_t)(txn & 0xff);
	buf[2] = (uint8_t)(txn >> 8);
	buf[3] = (uint8_t)(msg_id & 0xff);
	buf[4] = (uint8_t)(msg_id >> 8);
	buf[5] = (uint8_t)(tlvs_len & 0xff);
	buf[6] = (uint8_t)(tlvs_len >> 8);
	if (tlvs_len)
		memcpy(buf + 7, tlvs, tlvs_len);
	return 7 + tlvs_len;
}

static int qmi_response_matches(const uint8_t *resp, size_t len,
				uint16_t txn, uint16_t msg_id)
{
	return len >= sizeof(struct qmi_hdr) && resp[0] == QMI_RESP &&
	       rd16(resp + 1) == txn && rd16(resp + 3) == msg_id &&
	       rd16(resp + 5) == len - sizeof(struct qmi_hdr);
}

static const uint8_t *qmi_find_tlv(const uint8_t *msg, size_t len,
				 uint8_t type, uint16_t *value_len)
{
	const uint8_t *found = NULL;
	uint16_t found_len = 0;
	size_t off = sizeof(struct qmi_hdr);

	*value_len = 0;
	if (len < off || rd16(msg + 5) != len - off)
		return NULL;
	while (off < len) {
		uint16_t n;
		if (len - off < 3)
			return NULL;
		n = rd16(msg + off + 1);
		if ((size_t)n > len - off - 3)
			return NULL;
		if (msg[off] == type) {
			if (found)
				return NULL;
			found = msg + off + 3;
			found_len = n;
		}
		off += 3 + n;
	}
	*value_len = found_len;
	return found;
}

static int qmi_success(const uint8_t *msg, size_t len)
{
	uint16_t n;
	const uint8_t *v = qmi_find_tlv(msg, len, 0x02, &n);
	return v && n == 4 && rd16(v) == 0 && rd16(v + 2) == 0;
}

static int64_t monotonic_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int qmi_txn(int fd, const struct srv_info *dst, const uint8_t *msg,
		   size_t len, uint8_t *resp, size_t resmax, int timeout_ms)
{
	struct sockaddr_qrtr from;
	uint16_t txn, msg_id;
	int64_t deadline;

	if (len < sizeof(struct qmi_hdr) || rd16(msg + 5) != len - 7) {
		errno = EINVAL;
		return -1;
	}
	/* Callers may alias request and response; save identity before receiving. */
	txn = rd16(msg + 1);
	msg_id = rd16(msg + 3);
	deadline = monotonic_ms() + timeout_ms;

	if (qrtr_sendto(fd, dst->node, dst->port, msg, len) < 0) {
		perror("sendto(qmi)");
		return -1;
	}
	for (;;) {
		int64_t remaining = deadline - monotonic_ms();
		int k;
		if (remaining <= 0)
			return 0;
		k = qrtr_recv(fd, &from, resp, resmax, (int)remaining);
		if (k < 0 && errno == EINTR)
			continue;

		if (k <= 0)
			return k;
		if (from.sq_node != dst->node || from.sq_port != dst->port)
			continue;
		if (qmi_response_matches(resp, (size_t)k, txn, msg_id))
			return k;
	}
}

static void qmi_dump(const uint8_t *resp, size_t len)
{
	size_t off = sizeof(struct qmi_hdr);

	hexdump("[qmi] raw", resp, len);
	if (len < sizeof(struct qmi_hdr))
		return;
	{
		struct qmi_hdr *h = (struct qmi_hdr *)resp;

		printf("[qmi] type=%u txn=%u msg_id=0x%04x msg_len=%u\n",
		       h->type, h->txn, h->msg_id, h->msg_len);
	}
	while (off + 3 <= len) {
		uint8_t t = resp[off];
		uint16_t l = rd16(resp + off + 1);
		const uint8_t *v = resp + off + 3;

		printf("  TLV 0x%02x len=%u", t, l);
		if (off + 3 + l > len) {
			printf("  [truncated]\n");
			break;
		}
		if (l == 4)
			printf("  u32=%u (0x%08x)", rd32(v), rd32(v));
		else if (l == 2)
			printf("  u16=%u", rd16(v));
		else if (l == 1)
			printf("  u8=%u", v[0]);
		if (l && l <= 32 && t != 0x01) {
			unsigned i;
			int printable = 1;

			for (i = 0; i < l; i++)
				if (v[i] && (v[i] < 32 || v[i] > 126))
					printable = 0;
			if (printable) {
				printf("  str=\"");
				for (i = 0; i < l; i++)
					putchar(v[i]);
				printf("\"");
			}
		}
		printf("\n");
		off += 3 + l;
	}
}

/* libqmi's WDS fields are LE u32; ModemManager qmi_inet4_ntop converts to BE. */
static void format_ipv4(char out[16], const uint8_t *v)
{
	uint32_t address = rd32(v);
	snprintf(out, 16, "%u.%u.%u.%u", address >> 24,
		 (address >> 16) & 255, (address >> 8) & 255, address & 255);
}
static void print_ipv4(const char *name, const uint8_t *v)
{
	char address[16];
	format_ipv4(address, v);
	printf("  %s: %s (as-le32 value 0x%08x)\n", name, address, rd32(v));
}

/* ---- WDS helpers ---- */

struct wds_conn {
	struct srv_info srv;
	int have;
};

static int wds_target(int fd, const struct sockaddr_qrtr *local, struct wds_conn *w)
{
	if (!w->have) {
		if (find_service(fd, local, 1, &w->srv) < 0)
			return -1;
		w->have = 1;
	}
	return 0;
}

/* Requested-settings bitmask (libqmi QmiWdsRequestedSettings order) */
#define RQ_PROFILE_ID   (1u << 0)
#define RQ_PROFILE_NAME (1u << 1)
#define RQ_PDP_TYPE     (1u << 2)
#define RQ_APN_NAME     (1u << 3)
#define RQ_DNS_ADDRESS  (1u << 4)
#define RQ_IP_ADDRESS   (1u << 8)
#define RQ_GATEWAY_INFO (1u << 9)
#define RQ_MTU          (1u << 13)
#define RQ_IP_FAMILY    (1u << 15)

static int cmd_wds_get_settings(int fd, const struct sockaddr_qrtr *local,
				int wait_s)
{
	struct wds_conn w = { .have = 0 };
	uint8_t tlvs[16], *resp;
	size_t tl = 0, len;
	uint32_t mask = RQ_PROFILE_ID | RQ_PROFILE_NAME | RQ_PDP_TYPE | RQ_APN_NAME |
			RQ_DNS_ADDRESS | RQ_IP_ADDRESS | RQ_GATEWAY_INFO | RQ_MTU | RQ_IP_FAMILY;
	uint8_t m[4];
	int attempt, got = 0;

	if (wds_target(fd, local, &w) < 0)
		return 1;
	printf("[wds] service at node=%u port=%u\n", w.srv.node, w.srv.port);

	m[0] = (uint8_t)(mask & 0xff);
	m[1] = (uint8_t)(mask >> 8);
	m[2] = (uint8_t)(mask >> 16);
	m[3] = (uint8_t)(mask >> 24);
	tl = qmi_tlv_put(tlvs, tl, 0x10, m, 4);

	resp = malloc(65536);
	if (!resp)
		return 1;

	for (attempt = 0; attempt < (wait_s > 0 ? wait_s : 1); attempt++) {
		uint16_t txn = next_txn();
		size_t off;
		int k;

		len = qmi_build((uint8_t *)resp + 32768, txn, 0x002D, tlvs, tl);
		k = qmi_txn(fd, &w.srv, (uint8_t *)resp + 32768, len, resp, 32768, 3000);
		if (k <= 0) {
			fprintf(stderr, "[wds] no response (attempt %d)\n", attempt + 1);
			if (wait_s > 1)
				sleep(1);
			continue;
		}
		qmi_dump(resp, (size_t)k);
		if (!qmi_success(resp, (size_t)k)) {
			if (wait_s > 1)
				sleep(1);
			continue;
		}
		off = sizeof(struct qmi_hdr);
		while (off + 3 <= (size_t)k) {
			uint8_t t = resp[off];
			uint16_t l = rd16(resp + off + 1);
			const uint8_t *v = resp + off + 3;

			if (off + 3 + l > (size_t)k)
				break;
			if (t == 0x1E && l >= 4) { print_ipv4("IPv4 addr", v); got = 1; }
			else if (t == 0x20 && l >= 4) print_ipv4("IPv4 gateway", v);
			else if (t == 0x15 && l >= 4) print_ipv4("DNS1", v);
			else if (t == 0x16 && l >= 4) print_ipv4("DNS2", v);
			else if (t == 0x21 && l >= 4) print_ipv4("IPv4 netmask", v);
			else if (t == 0x29 && l >= 4) printf("  MTU: %u\n", rd32(v));
			else if (t == 0x2B && l >= 1) printf("  IP family: %u\n", v[0]);
			else if (t == 0x14 && l >= 1) {
				printf("  APN: %.*s\n", l, v);
			} else if (t == 0x02 && l >= 4) {
				printf("  result=%u error=%u\n", rd16(v), rd16(v + 2));
			}
			off += 3 + l;
		}
		if (got)
			break;
		if (wait_s > 1)
			sleep(1);
	}
	free(resp);
	return got ? 0 : 1;
}

static int cmd_wds_start(int fd, const struct sockaddr_qrtr *local,
			 const char *apn, int ipfam)
{
	struct wds_conn w = { .have = 0 };
	uint8_t tlvs[300], resp[4096];
	size_t tl = 0, len;
	uint8_t fam = (uint8_t)ipfam;
	uint16_t txn = next_txn();
	int k;

	if (wds_target(fd, local, &w) < 0)
		return 1;
	printf("[wds] service at node=%u port=%u\n", w.srv.node, w.srv.port);

	tl = qmi_tlv_put(tlvs, tl, 0x14, apn, (uint16_t)strlen(apn));
	tl = qmi_tlv_put(tlvs, tl, 0x19, &fam, 1);

	len = qmi_build(resp, txn, 0x0020, tlvs, tl);
	k = qmi_txn(fd, &w.srv, resp, len, resp, sizeof(resp), 5000);
	if (k <= 0) {
		fprintf(stderr, "[wds-start] no response\n");
		return 1;
	}
	hexdump("[wds-start] raw", resp, (size_t)k);
	if ((size_t)k >= sizeof(struct qmi_hdr)) {
		struct qmi_hdr *h = (struct qmi_hdr *)resp;
		size_t off = sizeof(struct qmi_hdr);

		printf("[wds-start] type=%u txn=%u msg_id=0x%04x\n", h->type, h->txn, h->msg_id);
		while (off + 3 <= (size_t)k) {
			uint8_t t = resp[off];
			uint16_t l = rd16(resp + off + 1);
			const uint8_t *v = resp + off + 3;

			if (off + 3 + l > (size_t)k)
				break;
			if (t == 0x02 && l >= 4)
				printf("  result=%u error=%u\n", rd16(v), rd16(v + 2));
			else if (t == 0x01 && l >= 4)
				printf("  packet data handle: %u\n", rd32(v));
			else if (t == 0x10 && l >= 2)
				printf("  call end reason: %u\n", rd16(v));
			else
				printf("  TLV 0x%02x len=%u\n", t, l);
			off += 3 + l;
		}
	}
	return 0;
}

static int cmd_wds_status(int fd, const struct sockaddr_qrtr *local)
{
	struct wds_conn w = { .have = 0 };
	uint8_t resp[4096];
	size_t len;
	uint16_t txn = next_txn();
	int k;

	if (wds_target(fd, local, &w) < 0)
		return 1;
	len = qmi_build(resp, txn, 0x0022, NULL, 0);
	k = qmi_txn(fd, &w.srv, resp, len, resp, sizeof(resp), 3000);
	if (k <= 0) {
		fprintf(stderr, "[wds-status] no response\n");
		return 1;
	}
	qmi_dump(resp, (size_t)k);
	return 0;
}

static int cmd_ctl_version(int fd, const struct sockaddr_qrtr *local)
{
	struct srv_info srv;
	uint8_t resp[4096];
	size_t len;
	uint16_t txn = next_txn();
	int k;

	if (find_service(fd, local, 0, &srv) < 0)
		return 1;
	len = qmi_build(resp, txn, 0x0021, NULL, 0);
	k = qmi_txn(fd, &srv, resp, len, resp, sizeof(resp), 3000);
	if (k <= 0) {
		fprintf(stderr, "[ctl] no response\n");
		return 1;
	}
	qmi_dump(resp, (size_t)k);
	return 0;
}

static void wr32(uint8_t *v, uint32_t n)
{
	v[0] = (uint8_t)n;
	v[1] = (uint8_t)(n >> 8);
	v[2] = (uint8_t)(n >> 16);
	v[3] = (uint8_t)(n >> 24);
}

/* refs/qmi-service-wds.json: Bind Mux Data Port (0x00a2). */
static size_t wds_bind_tlvs(uint8_t *tlvs, uint32_t type,
			    uint32_t endpoint, uint8_t mux)
{
	uint8_t info[8];
	size_t n;
	wr32(info, type);
	wr32(info + 4, endpoint);
	n = qmi_tlv_put(tlvs, 0, 0x10, info, sizeof(info));
	return qmi_tlv_put(tlvs, n, 0x11, &mux, sizeof(mux));
}

static int qmi_request_ok(int fd, const struct srv_info *srv, uint16_t id,
			  const uint8_t *tlvs, size_t tl, uint8_t *resp,
			  size_t cap, int timeout_ms)
{
	uint8_t request[1024];
	size_t len;
	int n;
	if (tl > sizeof(request) - sizeof(struct qmi_hdr))
		return -1;
	len = qmi_build(request, next_txn(), id, tlvs, tl);
	hexdump("[request]", request, len);
	n = qmi_txn(fd, srv, request, len, resp, cap, timeout_ms);
	if (n <= 0) {
		fprintf(stderr, "[session] no reply for 0x%04x\n", id);
		return -1;
	}
	qmi_dump(resp, (size_t)n);
	if (!qmi_success(resp, (size_t)n)) {
		fprintf(stderr, "[session] QMI failure for 0x%04x\n", id);
		return -1;
	}
	return n;
}

/* NX679J Android capture 2026-09-20, service 2 at node 0/port 0x59:
 * txn 22 and 31: 0x002e, TLV 0x01=ONLINE, TLV 0x10=5 zero bytes.
 * Both have matching result/error=0 responses and mode 1 -> 0 readback.
 * qcril_qmi_nas.cpp:18080-18090 in the pinned public mirror initializes
 * e911_pending_info as valid, not pending; the old public IDL lacks it.
 * Only this observed normal (non-emergency) encoding is implemented.
 */
static size_t dms_online_observed_tlvs(uint8_t *tlvs)
{
	const uint8_t online = 0, metadata[5] = { 0 };
	size_t n = qmi_tlv_put(tlvs, 0, 0x01, &online, sizeof(online));
	return qmi_tlv_put(tlvs, n, 0x10, metadata, sizeof(metadata));
}

static int cmd_dms_online_observed(int fd, const struct sockaddr_qrtr *local)
{
	struct srv_info srv;
	uint8_t tlvs[12], resp[256];
	const uint8_t *mode;
	uint16_t len;
	int n;
	int64_t deadline;
	size_t tl;

	/* Diagnostic ceiling, including discovery and readback; no setter retries. */
	alarm(60);
	if (find_service_exact(fd, local, 2, 1, 0, &srv) < 0)
		return 1;
	n = qmi_request_ok(fd, &srv, 0x002d, NULL, 0,
			   resp, sizeof(resp), 4000);
	if (n < 0)
		return 1;
	mode = qmi_find_tlv(resp, (size_t)n, 0x01, &len);
	if (!mode || len != 1 || (mode[0] != 0 && mode[0] != 5)) {
		fputs("[dms] refused: expected healthy Android 0 or OpenWrt 5\n", stderr);
		return 1;
	}
	printf("[dms] BEFORE mode=%u; sending observed ONLINE request once\n", mode[0]);
	tl = dms_online_observed_tlvs(tlvs);
	if (qmi_request_ok(fd, &srv, 0x002e, tlvs, tl,
			   resp, sizeof(resp), 10000) < 0)
		return 1;
	puts("[dms] SET_ACCEPTED result=0 error=0");
	deadline = monotonic_ms() + 30000;
	do {
		n = qmi_request_ok(fd, &srv, 0x002d, NULL, 0,
				   resp, sizeof(resp), 4000);
		if (n < 0)
			return 1;
		mode = qmi_find_tlv(resp, (size_t)n, 0x01, &len);
		if (!mode || len != 1)
			return 1;
		printf("[dms] READBACK mode=%u\n", mode[0]);
		if (mode[0] == 0) {
			puts("[dms] ONLINE_VERIFIED; not a bearer or Internet proof");
			alarm(0);
			return 0;
		}
		sleep(2);
	} while (monotonic_ms() < deadline);
	fputs("[dms] no ONLINE readback within observation; no retry\n", stderr);
	return 1;
}

/* Stock qcom_sysmon.c: SSCTL v2, instance 18 (MSS), Get Failure Reason. */
static int cmd_ssctl_reason(int fd, const struct sockaddr_qrtr *local)
{
	struct srv_info srv;
	uint8_t resp[256];
	const uint8_t *v;
	uint16_t len;
	int n;

	if (find_service_exact(fd, local, 43, 2, 18, &srv) < 0)
		return 1;
	n = qmi_request_ok(fd, &srv, 0x0022, NULL, 0, resp, sizeof(resp), 5000);
	if (n < 0)
		return 1;
	v = qmi_find_tlv(resp, (size_t)n, 0x10, &len);
	if (!v) {
		puts("[ssctl] no failure-reason TLV reported");
		return 0;
	}
	if (len < 1 || v[0] > 90 || len != 1u + v[0]) {
		fputs("[ssctl] invalid failure-reason length\n", stderr);
		return 1;
	}
	printf("[ssctl] failure_reason=%.*s\n", v[0], v + 1);
	return 0;
}

/* Stock pdr_internal.h: notifier 0x42, Register Listener 0x20.
 * Observe the returned current state, then unregister on the same client.
 * This does not start/restart a domain or send an SSR event.
 */
static size_t servreg_listener_tlvs(uint8_t *tlvs, const char *path, uint8_t enable)
{
	size_t n = qmi_tlv_put(tlvs, 0, 0x01, &enable, 1);
	return qmi_tlv_put(tlvs, n, 0x02, path, (uint16_t)strlen(path));
}

static int cmd_servreg_state(int fd, const struct sockaddr_qrtr *local,
			    uint32_t instance, const char *path)
{
	struct srv_info srv;
	uint8_t tlvs[71], resp[256];
	const uint8_t *v;
	uint16_t len;
	size_t tl;
	int n, result = 1;

	/* SERVREG_NAME_LENGTH from the stock include/linux/soc/qcom/pdr.h. */
	if (!*path || strlen(path) > 64)
		return 1;
	if (find_service_exact(fd, local, 66, 1, instance, &srv) < 0)
		return 1;
	tl = servreg_listener_tlvs(tlvs, path, 1);
	n = qmi_request_ok(fd, &srv, 0x0020, tlvs, tl, resp, sizeof(resp), 5000);
	if (n < 0)
		return 1;
	v = qmi_find_tlv(resp, (size_t)n, 0x10, &len);
	if (v && len == 4) {
		uint32_t state = rd32(v);
		printf("[servreg] path=%s state=0x%08x (%s)\n", path, state,
		       state == 0x1fffffffu ? "UP" :
		       state == 0x0fffffffu ? "DOWN" :
		       state == 0x2fffffffu ? "EARLY_DOWN" :
		       state == 0x7fffffffu ? "UNINIT" : "unknown");
		result = 0;
	}
	tl = servreg_listener_tlvs(tlvs, path, 0);
	if (qmi_request_ok(fd, &srv, 0x0020, tlvs, tl,
			   resp, sizeof(resp), 5000) < 0)
		result = 1;
	return result;
}

static volatile sig_atomic_t session_running = 1;
static void session_signal(int sig)
{
	(void)sig;
	session_running = 0;
}

static int parse_u32(const char *text, uint32_t *out)
{
	char *end;
	unsigned long n;
	errno = 0;
	n = strtoul(text, &end, 0);
	if (!*text || *text == '-' || *end || errno || n > UINT32_MAX)
		return -1;
	*out = (uint32_t)n;
	return 0;
}

/* libqmi b7913df8: one DPM hardware port, four LE u32 fields.
 * RX/TX are from the modem's perspective (mm-port-qmi.c dpm_open_port).
 */
static size_t dpm_open_tlvs(uint8_t *tlvs, uint32_t type, uint32_t endpoint,
			   uint32_t rx, uint32_t tx)
{
	uint8_t port[17] = { 1 };
	wr32(port + 1, type);
	wr32(port + 5, endpoint);
	wr32(port + 9, rx);
	wr32(port + 13, tx);
	return qmi_tlv_put(tlvs, 0, 0x11, port, sizeof(port));
}

/* libqmi qmi-service-wda.json: Get Data Format endpoint TLV (not Set). */
static size_t wda_get_tlvs(uint8_t *tlvs, uint32_t type, uint32_t endpoint)
{
	uint8_t info[8];
	wr32(info, type);
	wr32(info + 4, endpoint);
	return qmi_tlv_put(tlvs, 0, 0x10, info, sizeof(info));
}

static int cmd_wda_get(int fd, const struct sockaddr_qrtr *local,
		       uint32_t type, uint32_t endpoint)
{
	struct srv_info srv;
	uint8_t tlvs[11], resp[4096];
	size_t tl = wda_get_tlvs(tlvs, type, endpoint);

	/* Exact service/version/instance observed in the live QRTR listing. */
	if (find_service_exact(fd, local, 26, 1, 0, &srv) < 0)
		return 1;
	return qmi_request_ok(fd, &srv, 0x0021, tlvs, tl,
			      resp, sizeof(resp), 10000) < 0;
}

/* libqmi qmi-enums-wda.h: RAW_IP=2, QMAP=5 (no checksum extensions).
 * qmi-service-wda.json: Set Data Format endpoint TLV is 0x17.
 */
static size_t wda_qmap_tlvs(uint8_t *tlvs, uint32_t type, uint32_t endpoint)
{
	uint8_t info[8], value[4], qos = 0;
	size_t n;

	n = qmi_tlv_put(tlvs, 0, 0x10, &qos, 1);
	wr32(value, 2);
	n = qmi_tlv_put(tlvs, n, 0x11, value, 4);
	wr32(value, 5);
	n = qmi_tlv_put(tlvs, n, 0x12, value, 4);
	n = qmi_tlv_put(tlvs, n, 0x13, value, 4);
	wr32(info, type);
	wr32(info + 4, endpoint);
	return qmi_tlv_put(tlvs, n, 0x17, info, sizeof(info));
}

static int wda_is_plain_qmap(const uint8_t *resp, size_t n)
{
	const uint8_t *v;
	uint16_t len;
	unsigned int type;

	v = qmi_find_tlv(resp, n, 0x10, &len);
	if (!v || len != 1 || v[0] != 0)
		return 0;
	for (type = 0x11; type <= 0x13; type++) {
		v = qmi_find_tlv(resp, n, (uint8_t)type, &len);
		if (!v || len != 4 || rd32(v) != (type == 0x11 ? 2u : 5u))
			return 0;
	}
	return 1;
}

static int cmd_wda_qmap(int fd, const struct sockaddr_qrtr *local,
			uint32_t type, uint32_t endpoint)
{
	struct srv_info srv;
	uint8_t tlvs[36], resp[4096];
	size_t tl = wda_qmap_tlvs(tlvs, type, endpoint);
	int n;

	if (find_service_exact(fd, local, 26, 1, 0, &srv) < 0)
		return 1;
	n = qmi_request_ok(fd, &srv, 0x0020, tlvs, tl,
			   resp, sizeof(resp), 10000);
	if (n < 0)
		return 1;
	if (!wda_is_plain_qmap(resp, (size_t)n)) {
		fputs("[wda] returned format is not verified plain QMAP; stop\n", stderr);
		return 1;
	}
	puts("[wda] SET_VERIFIED raw-IP=2 UL-QMAP=5 DL-QMAP=5 QoS=0");
	tl = wda_get_tlvs(tlvs, type, endpoint);
	n = qmi_request_ok(fd, &srv, 0x0021, tlvs, tl,
			   resp, sizeof(resp), 10000);
	if (n < 0 || !wda_is_plain_qmap(resp, (size_t)n)) {
		fputs("[wda] Get does not confirm the selected format; stop\n", stderr);
		return 1;
	}
	puts("[wda] VERIFIED raw-IP=2 UL-QMAP=5 DL-QMAP=5 QoS=0");
	return 0;
}

static int cmd_dpm_session(int fd, const struct sockaddr_qrtr *local,
			   uint32_t type, uint32_t endpoint,
			   uint32_t rx, uint32_t tx, uint32_t hold_s)
{
	struct srv_info srv;
	uint8_t tlvs[20], resp[4096];
	size_t tl = dpm_open_tlvs(tlvs, type, endpoint, rx, tx);
	int64_t end_at;
	int result = 0;

	if (find_service_exact(fd, local, 47, 1, 0, &srv) < 0)
		return 1;
	session_running = 1;
	signal(SIGTERM, session_signal);
	signal(SIGINT, session_signal);
	if (qmi_request_ok(fd, &srv, 0x0020, tlvs, tl,
			   resp, sizeof(resp), 10000) < 0)
		return 1;
	printf("[dpm] OPENED endpoint=%u:%u rx=%u tx=%u pid=%ld hold=%u\n",
	       type, endpoint, rx, tx, (long)getpid(), hold_s);
	end_at = monotonic_ms() + (int64_t)hold_s * 1000;
	while (session_running && monotonic_ms() < end_at) {
		struct sockaddr_qrtr from;
		int n = qrtr_recv(fd, &from, resp, sizeof(resp), 1000);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("DPM receive");
			result = 1;
			break;
		}
		if (n >= (int)sizeof(struct qmi_hdr) &&
		    from.sq_node == srv.node && from.sq_port == srv.port)
			qmi_dump(resp, (size_t)n);
	}
	/* libqmi Close Port (0x21) has no input; retain ownership until cleanup. */
	puts("[dpm] CLOSING ports on this client");
	if (qmi_request_ok(fd, &srv, 0x0021, NULL, 0,
			   resp, sizeof(resp), 10000) < 0)
		result = 1;
	return result;
}

static int cmd_wds_session(int fd, const struct sockaddr_qrtr *local,
			   const char *apn, uint32_t endpoint_type,
			   uint32_t endpoint_id, uint8_t mux, uint32_t hold_s)
{
	struct srv_info dms;
	struct wds_conn w = { .have = 0 };
	uint8_t tlvs[300], resp[8192], family = 4, handle_wire[4];
	const uint8_t *v;
	uint16_t vlen;
	uint32_t handle;
	size_t tl;
	int n, result = 1;
	int64_t end_at;

	if (!*apn || strlen(apn) > sizeof(tlvs) - 7)
		return 1;
	if (find_service(fd, local, 2, &dms) < 0)
		return 1;
	n = qmi_request_ok(fd, &dms, 0x002d, NULL, 0, resp, sizeof(resp), 4000);
	if (n < 0)
		return 1;
	v = qmi_find_tlv(resp, (size_t)n, 0x01, &vlen);
	if (!v || vlen != 1 || v[0] != 0) {
		fprintf(stderr, "[session] refused: DMS is not ONLINE\n");
		return 1;
	}
	if (wds_target(fd, local, &w) < 0)
		return 1;
	tl = wds_bind_tlvs(tlvs, endpoint_type, endpoint_id, mux);
	if (qmi_request_ok(fd, &w.srv, 0x00a2, tlvs, tl,
			   resp, sizeof(resp), 10000) < 0)
		return 1;
	tl = qmi_tlv_put(tlvs, 0, 0x01, &family, 1);
	/* ModemManager treats Set IP Family as optional; Start carries it too. */
	if (qmi_request_ok(fd, &w.srv, 0x004d, tlvs, tl,
			   resp, sizeof(resp), 10000) < 0)
		fprintf(stderr, "[session] continuing with explicit IPv4 Start preference\n");
	tl = qmi_tlv_put(tlvs, 0, 0x14, apn, (uint16_t)strlen(apn));
	tl = qmi_tlv_put(tlvs, tl, 0x19, &family, 1);
	n = qmi_request_ok(fd, &w.srv, 0x0020, tlvs, tl,
			   resp, sizeof(resp), 90000);
	if (n < 0)
		return 1;
	v = qmi_find_tlv(resp, (size_t)n, 0x01, &vlen);
	if (!v || vlen != sizeof(handle_wire)) {
		fprintf(stderr, "[session] missing packet data handle\n");
		return 1;
	}
	memcpy(handle_wire, v, sizeof(handle_wire));
	handle = rd32(handle_wire);
	printf("[session] handle=%u endpoint=%u:%u mux=%u pid=%ld\n",
	       handle, endpoint_type, endpoint_id, mux, (long)getpid());
	if (cmd_wds_get_settings(fd, local, 5) != 0)
		goto stop;
	session_running = 1;
	signal(SIGTERM, session_signal);
	signal(SIGINT, session_signal);
	end_at = monotonic_ms() + (int64_t)hold_s * 1000;
	printf("[session] HOLDING seconds=%u; same QRTR client remains open\n", hold_s);
	result = 0;
	while (session_running && monotonic_ms() < end_at) {
		struct sockaddr_qrtr from;
		n = qrtr_recv(fd, &from, resp, sizeof(resp), 1000);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("session receive");
			result = 1;
			break;
		}
		if (n < (int)sizeof(struct qmi_hdr) ||
		    from.sq_node != w.srv.node || from.sq_port != w.srv.port)
			continue;
		qmi_dump(resp, (size_t)n);
		if (resp[0] == QMI_IND && rd16(resp + 3) == 0x0022) {
			v = qmi_find_tlv(resp, (size_t)n, 0x01, &vlen);
			/* QmiWdsConnectionStatus: disconnected=1. */
			if (v && vlen >= 1 && v[0] == 1) {
				fprintf(stderr, "[session] network disconnected\n");
				result = 1;
				break;
			}
		}
	}
stop:
	tl = qmi_tlv_put(tlvs, 0, 0x01, handle_wire, sizeof(handle_wire));
	printf("[session] stopping handle=%u\n", handle);
	if (qmi_request_ok(fd, &w.srv, 0x0021, tlvs, tl,
			   resp, sizeof(resp), 10000) < 0)
		result = 1;
	return result;
}

static int hex2bin(const char *hex, uint8_t *out, size_t max)
{
	size_t n = 0;

	while (*hex && n < max) {
		unsigned v;

		while (*hex == ' ' || *hex == ':')
			hex++;
		if (!*hex)
			break;
		if (sscanf(hex, "%2x", &v) != 1)
			return -1;
		out[n++] = (uint8_t)v;
		hex += 2;
	}
	return (int)n;
}

static int cmd_raw(int fd, const struct sockaddr_qrtr *local,
		   unsigned service, const char *hex)
{
	struct srv_info srv;
	uint8_t msg[1024], resp[8192];
	int n, k;

	if (find_service(fd, local, service, &srv) < 0)
		return 1;
	n = hex2bin(hex, msg, sizeof(msg));
	if (n <= 0) {
		fprintf(stderr, "bad hex\n");
		return 1;
	}
	k = qmi_txn(fd, &srv, msg, (size_t)n, resp, sizeof(resp), 4000);
	if (k <= 0) {
		fprintf(stderr, "[raw] no response\n");
		return 1;
	}
	qmi_dump(resp, (size_t)k);
	return 0;
}

/*
 * rawseq: send several raw QMI messages to the same service from a single
 * socket/port (same QMI "client"), so requests that depend on client state
 * see a consistent client.
 */
static int cmd_rawseq(int fd, const struct sockaddr_qrtr *local,
		      unsigned service, char **hexes, int nhex)
{
	struct srv_info srv;
	uint8_t msg[1024], resp[8192];
	int i, n, k;

	if (find_service(fd, local, service, &srv) < 0)
		return 1;

	for (i = 0; i < nhex; i++) {
		n = hex2bin(hexes[i], msg, sizeof(msg));
		if (n <= 0) {
			fprintf(stderr, "bad hex #%d\n", i);
			return 1;
		}
		fprintf(stderr, "[rawseq %d] -> %s\n", i, hexes[i]);
		k = qmi_txn(fd, &srv, msg, (size_t)n, resp, sizeof(resp), 4000);
		if (k <= 0) {
			fprintf(stderr, "[rawseq %d] no response\n", i);
			continue;
		}
		qmi_dump(resp, (size_t)k);
		usleep(300000);
	}
	return 0;
}

/*
 * playback: replay a file of QMI requests extracted from a qcril trace.
 * File lines: "<group-hex> <service> <hex-message>"
 * Each group gets its own QRTR socket, mirroring the original client split.
 */
struct pb_grp {
	unsigned gid;
	int fd;
	struct sockaddr_qrtr local;
};

struct pb_svc {
	unsigned svc;
	struct srv_info srv;
	int valid;
};

static int cmd_playback(int fd, const struct sockaddr_qrtr *local, const char *path)
{
	static struct pb_grp grps[64];
	static struct pb_svc svcs[128];
	int ngrp = 0, nsvc = 0;
	FILE *f;
	char line[512];
	int sent = 0, answered = 0, failed = 0;

	(void)fd;
	(void)local;

	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
		return 1;
	}

	while (fgets(line, sizeof(line), f)) {
		char hexbuf[512];
		unsigned gid, svc;
		uint8_t msg[1024], resp[8192];
		struct pb_grp *g = NULL;
		struct pb_svc *s = NULL;
		struct srv_info srv;
		int i, n, k;

		if (sscanf(line, "%x %u %511s", &gid, &svc, hexbuf) != 3)
			continue;

		/* group socket */
		for (i = 0; i < ngrp; i++)
			if (grps[i].gid == gid) {
				g = &grps[i];
				break;
			}
		if (!g) {
			if (ngrp >= 64) {
				fprintf(stderr, "[pb] too many groups\n");
				break;
			}
			g = &grps[ngrp++];
			g->gid = gid;
			g->fd = qrtr_open(&g->local);
			if (g->fd < 0) {
				fprintf(stderr, "[pb] socket fail for group %x\n", gid);
				g->fd = -1;
			}
		}
		if (g->fd < 0)
			continue;

		/* service resolution cache */
		for (i = 0; i < nsvc; i++)
			if (svcs[i].svc == svc && svcs[i].valid) {
				s = &svcs[i];
				break;
			}
		if (!s) {
			if (nsvc >= 128)
				continue;
			s = &svcs[nsvc++];
			s->svc = svc;
			s->valid = 0;
			if (find_service(g->fd, &g->local, svc, &s->srv) == 0)
				s->valid = 1;
		}
		if (!s->valid) {
			fprintf(stderr, "[pb] svc=%u not found, skip msgid=%s\n", svc, hexbuf + 6);
			failed++;
			continue;
		}
		srv = s->srv;

		n = hex2bin(hexbuf, msg, sizeof(msg));
		if (n <= 0)
			continue;
		sent++;
		k = qmi_txn(g->fd, &srv, msg, (size_t)n, resp, sizeof(resp), 500);
		if (k > 0) {
			unsigned mid = n >= 8 ? (unsigned)msg[3] | ((unsigned)msg[4] << 8) : 0;
			answered++;
			fprintf(stderr, "[pb] %03d grp=%x svc=%u msgid=0x%04x -> resp %d bytes\n",
				sent, gid, svc, mid, k);
		} else {
			unsigned mid = n >= 8 ? (unsigned)msg[3] | ((unsigned)msg[4] << 8) : 0;
			fprintf(stderr, "[pb] %03d grp=%x svc=%u msgid=0x%04x -> no response\n",
				sent, gid, svc, mid);
		}
		usleep(80000);
	}
	fclose(f);
	fprintf(stderr, "[pb] done: %d sent, %d answered, %d skipped\n", sent, answered, failed);
	return 0;
}

int main(int argc, char **argv)
{
	struct sockaddr_qrtr local;
	int fd;
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 2) {
		fprintf(stderr, "usage: %s list|lookup <svc> [inst]|ctl-version|wds-status|"
				"wds-get-settings [secs]|wds-start <apn> [ipfam]|"
				"wds-session <apn> <ep-type> <ep-id> <mux> <hold_s>|"
				"dpm-session <ep-type> <ep-id> <rx> <tx> <hold_s>|"
				"wda-get <ep-type> <ep-id>|"
				"wda-qmap <ep-type> <ep-id>|"
				"dms-online-observed|"
				"ssctl-reason|servreg-state <instance> <domain-path>|"
				"raw <svc> <hex>|hello [node]|watch [secs]\n", argv[0]);
		return 2;
	}
	fd = qrtr_open(&local);
	if (fd < 0)
		return 1;

	if (!strcmp(argv[1], "list")) {
		qrtr_lookup(fd, &local, 0, 0, NULL, 0, 0);
	} else if (!strcmp(argv[1], "lookup") && argc >= 3) {
		unsigned svc = (unsigned)strtoul(argv[2], NULL, 0);
		unsigned inst = argc >= 4 ? (unsigned)strtoul(argv[3], NULL, 0) : 0;

		qrtr_lookup(fd, &local, svc, inst, NULL, 0, 0);
	} else if (!strcmp(argv[1], "ctl-version")) {
		return cmd_ctl_version(fd, &local);
	} else if (!strcmp(argv[1], "wds-status")) {
		return cmd_wds_status(fd, &local);
	} else if (!strcmp(argv[1], "dms-online-observed") && argc == 2) {
		return cmd_dms_online_observed(fd, &local);
	} else if (!strcmp(argv[1], "ssctl-reason") && argc == 2) {
		return cmd_ssctl_reason(fd, &local);
	} else if (!strcmp(argv[1], "servreg-state") && argc == 4) {
		uint32_t instance;
		if (parse_u32(argv[2], &instance) || instance > 0xffffffu)
			return 2;
		return cmd_servreg_state(fd, &local, instance, argv[3]);
	} else if ((!strcmp(argv[1], "wda-get") ||
		    !strcmp(argv[1], "wda-qmap")) && argc == 4) {
		uint32_t type, endpoint;
		if (parse_u32(argv[2], &type) || type < 1 || type > 6 ||
		    parse_u32(argv[3], &endpoint))
			return 2;
		return !strcmp(argv[1], "wda-get") ?
			cmd_wda_get(fd, &local, type, endpoint) :
			cmd_wda_qmap(fd, &local, type, endpoint);
	} else if (!strcmp(argv[1], "dpm-session") && argc == 7) {
		uint32_t type, endpoint, rx, tx, hold;
		if (parse_u32(argv[2], &type) || type < 1 || type > 6 ||
		    parse_u32(argv[3], &endpoint) ||
		    parse_u32(argv[4], &rx) || parse_u32(argv[5], &tx) ||
		    parse_u32(argv[6], &hold) || !hold || hold > 86400)
			return 2;
		return cmd_dpm_session(fd, &local, type, endpoint, rx, tx, hold);
	} else if (!strcmp(argv[1], "wds-get-settings")) {
		int wait = argc >= 3 ? atoi(argv[2]) : 1;

		return cmd_wds_get_settings(fd, &local, wait);
	} else if (!strcmp(argv[1], "wds-start") && argc >= 3) {
		int fam = argc >= 4 ? atoi(argv[3]) : 4;

		return cmd_wds_start(fd, &local, argv[2], fam);
	} else if (!strcmp(argv[1], "wds-session") && argc == 7) {
		uint32_t type, endpoint, mux, hold;
		if (parse_u32(argv[3], &type) || type < 1 || type > 6 ||
		    parse_u32(argv[4], &endpoint) ||
		    parse_u32(argv[5], &mux) || mux > UINT8_MAX ||
		    parse_u32(argv[6], &hold) || !hold || hold > 86400) {
			fprintf(stderr, "invalid endpoint/mux/hold (hold: 1..86400 s)\n");
			return 2;
		}
		return cmd_wds_session(fd, &local, argv[2], type, endpoint,
				       (uint8_t)mux, hold);
	} else if (!strcmp(argv[1], "raw") && argc >= 4) {
		return cmd_raw(fd, &local, (unsigned)strtoul(argv[2], NULL, 0), argv[3]);
	} else if (!strcmp(argv[1], "rawseq") && argc >= 4) {
		return cmd_rawseq(fd, &local, (unsigned)strtoul(argv[2], NULL, 0),
				  &argv[3], argc - 3);
	} else if (!strcmp(argv[1], "hello")) {
		unsigned int node = (argc > 2) ? (unsigned int)strtoul(argv[2], NULL, 0) : 0xffffffffu;

		if (qrtr_hello(fd, node) < 0) {
			perror("hello");
			return 1;
		}
		fprintf(stderr, "hello sent to node=0x%x\n", node);
		return 0;
	} else if (!strcmp(argv[1], "playback") && argc >= 3) {
		return cmd_playback(fd, &local, argv[2]);
	} else if (!strcmp(argv[1], "watch")) {
		int secs = (argc > 2) ? atoi(argv[2]) : 30;

		return cmd_watch(fd, &local, secs);
	} else {
		fprintf(stderr, "unknown command\n");
		return 2;
	}
	close(fd);
	return 0;
}
