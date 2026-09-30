/*
 * qmi-probe.c — sonda QMI minimale per il modem Qualcomm su /dev/smd11.
 *
 * Perche' esiste: su Android il dialogo QMI col modem lo fa qmipriod/qcrilNrd.
 * Qui non c'e' nessun client, e il modem lasciato senza interlocutore porta giu'
 * la piattaforma dopo 1-2 minuti (misurato).  Questa sonda apre il canale e
 * chiede GET_VERSION_INFO al servizio CTL: se risponde, il canale e' vivo e si
 * puo' passare al bearer dati (WDS START_NETWORK_INTERFACE).
 *
 * Framing qmux (QMI over the legacy serial channel):
 *   0x00 | len(2, LE) | control(1)=0x00 | txn(1) | msgid(2, LE) | TLVs...
 * len conta da control in poi (control+txn+msgid+TLV).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define DEV_QMI  "/dev/smd11"
#define DEV_AT   "/dev/at_mdm0"

static int fd = -1;
static int txn = 1;

/* Crea il nodo se manca (questo kernel non ha devtmpfs). */
static void mknod_if_needed(const char *path, int maj, int min)
{
	char p[128];

	if (access(path, F_OK) == 0)
		return;
	snprintf(p, sizeof(p), "mknod %s c %d %d", path, maj, min);
	if (system(p) != 0)
		fprintf(stderr, "mknod %s fallito\n", path);
}

static int qmi_send(unsigned char *msg, int len)
{
	unsigned char buf[512];
	int n = 0;

	if (len > 400)
		return -1;
	buf[n++] = 0x00;		/* flags */
	buf[n++] = (unsigned char)(len & 0xff);
	buf[n++] = (unsigned char)(len >> 8);
	memcpy(buf + n, msg, (size_t)len);
	n += len;
	return write(fd, buf, (size_t)n) == n ? 0 : -1;
}

static int qmi_recv(unsigned char *out, int max)
{
	unsigned char hdr[3];
	int need, got = 0, r;

	/* leggo 3 byte di header */
	while (got < 3) {
		r = read(fd, hdr + got, (size_t)(3 - got));
		if (r <= 0)
			return -1;
		got += r;
	}
	need = hdr[1] | (hdr[2] << 8);
	if (need + 3 > max)
		return -1;
	got = 0;
	while (got < need) {
		r = read(fd, out + got, (size_t)(need - got));
		if (r <= 0)
			return -1;
		got += r;
	}
	return need;
}

static void dump(const char *what, unsigned char *b, int n)
{
	int i;

	printf("%s (%d byte):", what, n);
	for (i = 0; i < n; i++) {
		if (i % 16 == 0)
			printf("\n  ");
		printf("%02x ", b[i]);
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	unsigned char msg[512], rep[512];
	int n, i, len;
	unsigned int alarm_s = 6;

	if (argc > 1)
		alarm_s = (unsigned int)atoi(argv[1]);

	mknod_if_needed(DEV_QMI, 504, 5);
	mknod_if_needed(DEV_AT, 504, 0);

	fd = open(DEV_QMI, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", DEV_QMI, strerror(errno));
		return 1;
	}

	/* svuoto quel che c'e' gia' in coda */
	while (read(fd, rep, sizeof(rep)) > 0)
		;

	/* CTL GET_VERSION_INFO: svc 0 (header), msgid 0x0021, nessun TLV */
	n = 0;
	msg[n++] = 0x00;			/* control */
	msg[n++] = (unsigned char)txn++;
	msg[n++] = 0x21;			/* msgid lo: 0x0021 */
	msg[n++] = 0x00;			/* msgid hi */
	len = n;
	if (qmi_send(msg, len) != 0) {
		fprintf(stderr, "write: %s\n", strerror(errno));
		return 1;
	}
	printf("GET_VERSION_INFO inviato su %s (txn=%d)\n", DEV_QMI, txn - 1);

	alarm(alarm_s);
	for (i = 0; i < 3; i++) {
		n = qmi_recv(rep, sizeof(rep));
		if (n < 0) {
			printf("nessuna risposta (tentativo %d)\n", i + 1);
			continue;
		}
		dump("risposta", rep, n);
		/* control, txn, msgid lo, hi, result(2), poi TLV */
		if (n >= 6)
			printf("  msgid=0x%02x%02x txn=%u result=0x%02x%02x\n",
			       rep[3], rep[2], rep[1], rep[5], rep[4]);
	}
	alarm(0);
	close(fd);
	return 0;
}
