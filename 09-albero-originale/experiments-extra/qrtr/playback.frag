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
