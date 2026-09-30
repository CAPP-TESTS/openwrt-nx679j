/*
 * qmi-dial.c — dialogo QMI col modem (SM8450, canale GLINK smd11).
 *
 * FASI:
 *   1) mknod dei nodi (questo kernel non ha devtmpfs) e apertura canale;
 *   2) CTL GET_VERSION_INFO  (0x0021, service 0x00): prova che il modem risponde;
 *   3) WDS GET_CLIENT_ID     (0x0022, service 0x01, TLV 0x01=service type WDS):
 *      il modem assegna un client id per il servizio dati;
 *   4) WDS START_NETWORK_INTERFACE (0x0020): attiva la sessione dati.
 *
 * Framing QMUX (header corto, service id nei 5 bit bassi del primo byte):
 *   [0x80|svc] [len_lo] [len_hi] [control=0] [txn] [msgid_lo] [msgid_hi] [TLV...]
 * len = byte dopo i due di lunghezza (control+txn+msgid+TLV).
 *
 * TLV: [type] [len_lo] [len_hi] [valore...]
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define DEV_QMI  "/dev/smdcntl8"
#define DEV_AT   "/dev/at_mdm0"

#define SVC_CTL  0x00
#define SVC_WDS  0x01

#define CTL_GET_VERSION_INFO 0x0021
#define WDS_GET_CLIENT_ID    0x0022
#define WDS_START_NETWORK    0x0020

static int fd = -1;
static int txn = 1;

static void mknod_if_needed(const char *path, int maj, int min)
{
	char p[128];

	if (access(path, F_OK) == 0)
		return;
	snprintf(p, sizeof(p), "mknod %s c %d %d", path, maj, min);
	if (system(p) != 0)
		fprintf(stderr, "mknod %s fallito\n", path);
}

/* manda un messaggio e aspetta la risposta; ritorna la lunghezza o -1 */
static int qmi_txn(unsigned char svc, unsigned short msgid,
		   const unsigned char *tlv, int tlvlen, unsigned char *rep, int max)
{
	unsigned char buf[1024];
	int n = 0, body, r, got, need;

	/* header corto: service nei 5 bit bassi */
	buf[n++] = (unsigned char)(0x80 | (svc & 0x1f));
	body = 1 + 1 + 2 + tlvlen;	/* control + txn + msgid + TLV */
	buf[n++] = (unsigned char)(body & 0xff);
	buf[n++] = (unsigned char)(body >> 8);
	buf[n++] = 0x00;		/* control */
	buf[n++] = (unsigned char)(txn++ & 0xff);
	buf[n++] = (unsigned char)(msgid & 0xff);
	buf[n++] = (unsigned char)(msgid >> 8);
	if (tlvlen) {
		memcpy(buf + n, tlv, (size_t)tlvlen);
		n += tlvlen;
	}
	if (write(fd, buf, (size_t)n) != n) {
		fprintf(stderr, "write: %s\n", strerror(errno));
		return -1;
	}
	/* risposta: 3 byte di header (svc|len) + body */
	got = 0;
	while (got < 3) {
		r = read(fd, rep + got, (size_t)(3 - got));
		if (r <= 0)
			return -1;
		got += r;
	}
	need = rep[1] | (rep[2] << 8);
	if (need + 3 > max)
		return -1;
	got = 0;
	/* il body arriva dopo i 3 byte: leggo in coda a rep */
	while (got < need) {
		r = read(fd, rep + 3 + got, (size_t)(need - got));
		if (r <= 0)
			return -1;
		got += r;
	}
	return need + 3;
}

static void dump(const char *what, const unsigned char *b, int n)
{
	int i;

	printf("%s [%d]:", what, n);
	for (i = 0; i < n; i++)
		printf(" %02x", b[i]);
	printf("\n");
}

/* cerca un TLV e stampa il primo byte del valore */
static int find_tlv(const unsigned char *rep, int n, unsigned char type)
{
	int i = 7;			/* dopo svc,len(2),ctrl,txn,msgid(2) */

	while (i + 3 <= n) {
		int t = rep[i];
		int l = rep[i + 1] | (rep[i + 2] << 8);

		if (t == type)
			return (i + 3 < n) ? rep[i + 3] : -1;
		i += 3 + l;
	}
	return -1;
}

int main(void)
{
	unsigned char rep[1024], tlv[64];
	int n, client = -1, i;

	mknod_if_needed(DEV_QMI, 504, 2);

	mknod_if_needed(DEV_AT, 504, 0);

	fd = open(DEV_QMI, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", DEV_QMI, strerror(errno));
		return 1;
	}
	while (read(fd, rep, sizeof(rep)) > 0)
		;

	/* 1) CTL GET_VERSION_INFO */
	n = qmi_txn(SVC_CTL, CTL_GET_VERSION_INFO, NULL, 0, rep, sizeof(rep));
	if (n < 0) {
		printf("CTL GET_VERSION: nessuna risposta (il modem parla QMI?)\n");
		return 2;
	}
	dump("CTL GET_VERSION", rep, n);
	printf("  result=%02x%02x\n", rep[5], rep[4]);

	/* 2) WDS GET_CLIENT_ID: TLV 0x01 = service type (0x01 = WDS) */
	tlv[0] = 0x01;
	tlv[1] = 0x01;
	tlv[2] = 0x00;
	tlv[3] = SVC_WDS;
	n = qmi_txn(SVC_WDS, WDS_GET_CLIENT_ID, tlv, 4, rep, sizeof(rep));
	if (n < 0) {
		printf("WDS GET_CLIENT_ID: nessuna risposta\n");
		return 2;
	}
	dump("WDS GET_CLIENT_ID", rep, n);
	client = find_tlv(rep, n, 0x01);	/* TLV 0x01 = client id */
	printf("  client id = %d (result=%02x%02x)\n", client, rep[5], rep[4]);

	/* 3) WDS START_NETWORK_INTERFACE con APN vuoto (default della SIM) */
	tlv[0] = 0x10;			/* APN */
	tlv[1] = 0x00;
	tlv[2] = 0x00;
	n = qmi_txn(SVC_WDS, WDS_START_NETWORK, tlv, 3, rep, sizeof(rep));
	if (n < 0) {
		printf("WDS START: nessuna risposta\n");
		return 2;
	}
	dump("WDS START_NETWORK", rep, n);
	printf("  call id = %d (result=%02x%02x)\n", find_tlv(rep, n, 0x01),
	       rep[5], rep[4]);

	/* stampo anche gli altri TLV per capire cosa offre il modem */
	for (i = 7; i + 3 <= n; ) {
		int t = rep[i], l = rep[i + 1] | (rep[i + 2] << 8);

		printf("  TLV 0x%02x len=%d:", t, l);
		{
			int k;
			for (k = 0; k < l && k < 24; k++)
				printf(" %02x", rep[i + 3 + k]);
		}
		printf("\n");
		i += 3 + l;
	}
	close(fd);
	return 0;
}
