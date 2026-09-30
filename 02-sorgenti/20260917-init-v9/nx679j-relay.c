/*
 * NX679J /nx679j/relay -- the READ-ONLY diagnostic relay of v9.
 *
 * WHAT IT IS: a child of PID 1 that listens on TCP, on the address of the
 * gadget's network interface (10.0.0.1 on usb0), and answers every connection
 * with ONE text snapshot of the boot.  The only client that is needed is:
 *
 *      nc 10.0.0.1 9999
 *
 * (or telnet, or any browser, or `socat - TCP:10.0.0.1:9999`).  Nothing is
 * read FROM the client: the dump is written to the socket immediately on
 * accept, so the answer cannot depend on what the client sends.  That is the
 * whole protocol, and it is deliberate -- a request language would be another
 * thing that can be wrong on a device we cannot debug.
 *
 * WHY READ-ONLY IS A PROPERTY AND NOT A PROMISE: this program never opens a
 * file for writing.  Every open(2) here is O_RDONLY; the only descriptors it
 * ever writes to are the accepted socket and the boot console (fd 1/2).  It
 * does not load modules, does not mount, does not create device nodes, does
 * not touch the network configuration (it only READS the interface state with
 * SIOCGIFADDR/SIOCGIFFLAGS/SIOCGIFMTU), and it never writes to the rawdump
 * partition or to the journal file.  verify-candidate-v9.py checks the
 * ship time from the bytes; localtest-relay.py checks it at run time from the
 * real syscalls the shipped binary makes (qemu-aarch64 -strace): no
 * open(...,O_WRONLY|O_CREAT), no mount, no finit_module, no mknod, no unlink,
 * no rename, no write to anything but the socket and the console.
 *
 * WHY IT CANNOT WEDGE THE BOOT: it runs in its own process, started by PID 1
 * with clone+execve like every other worker, and PID 1 never does a blocking
 * wait on it (wait4 WNOHANG only).  Inside itself, each client is served by a
 * forked grandchild with alarm(CLIENT_DEADLINE_S): a client that connects and
 * stops reading is killed on that alarm (SIGALRM interrupts the blocked
 * write), so one rude client cannot stop the next one from being served.  The
 * listen socket is never closed by a client interaction.
 *
 * SIZE, AND WHY IT IS BOUNDED: every file it reads is capped (FILE_MAX) and
 * the journal keeps the LAST JOURNAL_MAX bytes (a rolling window in a static
 * buffer, no allocation at all).  A boot whose journal is 200 MB therefore
 * still produces one bounded, printable answer.
 *
 * TEST INDIRECTION (the same style as the worker's V8_MODDIR/V8_JOURNAL):
 * V9_RELAY_ROOT prefixes every path it reads, V9_RELAY_IF/V9_RELAY_IP describe
 * the interface to use, V9_RELAY_WAIT is the seconds to wait for it.  The
 * shipped boot uses none of them: with an empty environment it reads /proc,
 * /sys and /nx679j-journal and binds 10.0.0.1:9999 on usb0.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VERSION        "nx679j-relay-v9"
#define DEFAULT_PORT   9999
#define DEFAULT_IF     "usb0"
#define DEFAULT_IP     "10.0.0.1"
#define DEFAULT_WAIT   30          /* seconds to wait for the interface     */

#define CLIENT_DEADLINE_S 30       /* one client may not hold a child longer */
#define FILE_MAX   (64 * 1024)     /* per-file cap                          */
#define JOURNAL_MAX (512 * 1024)   /* journal window (kept in .bss, no malloc) */
#define LIST_MAX   250             /* names listed per directory            */

/* ------------------------------------------------------------------ state */
static int g_c = -1;                     /* the accepted socket, or -1      */
static unsigned long long g_sent;        /* bytes written to THIS client    */
static const char *g_ifname = DEFAULT_IF;
static const char *g_want_ip = DEFAULT_IP;
static int g_port = DEFAULT_PORT;
static int g_listen_ok;
static int g_bind_all;                   /* 1 = had to fall back to 0.0.0.0 */
static char g_bind_ip[64] = "?";
static char g_if_ip[64] = "-";
static int g_if_index, g_if_present;
static int g_want_present;               /* is the wanted address anywhere?  */
static char g_want_on[64] = "-";         /* the interface that has it        */
static char g_nonlocal[16] = "?";        /* net.ipv4.ip_nonlocal_bind        */
static unsigned long long g_started_ms, g_now_ms;
static unsigned long g_clients;          /* clients accepted (all children)  */
static unsigned g_children_alive;
static int g_client_deadline_s = CLIENT_DEADLINE_S;   /* per-client alarm    */

static const char *g_root = "";

static unsigned long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000L;
}

static void sleep_ms(unsigned ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

/*
 * EVERY path this program reads goes through here (test indirection).
 *
 * The buffer is ALWAYS filled and the caller may use either the returned
 * pointer or the buffer.  Writing only in the "root is set" case was a real
 * bug: on the device (no root prefix) every caller that used the buffer read
 * an uninitialized stack string, so /proc and /sys looked absent.
 */
static const char *P(const char *path, char *buf, size_t n)
{
	if (g_root && *g_root)
		snprintf(buf, n, "%s%s", g_root, path);
	else
		snprintf(buf, n, "%s", path);
	return buf;
}

/* ------------------------------------------------------------------- output
 * Everything that goes to the client goes through emit(); a short write of a
 * blocking socket does not happen for the sizes involved here, but the loop
 * keeps the code honest anyway.
 */
static void emit(const char *b, size_t n)
{
	size_t off = 0;

	while (off < n) {
		ssize_t w = write(g_c, b + off, n - off);

		if (w <= 0) {
			if (errno == EINTR)
				continue;
			return;                 /* the client is gone */
		}
		off += (size_t)w;
	}
	g_sent += n;
}

static void outf(const char *fmt, ...)
{
	char b[8192];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (n <= 0)
		return;
	if (n > (int)sizeof(b))
		n = (int)sizeof(b);
	emit(b, (size_t)n);
}

/* keep the answer printable whatever the file contained (a sysfs attribute
 * can contain anything; a reader must not get binary noise in a text dump) */
static void emit_sanitized(const char *b, size_t n, int indent)
{
	char line[256];
	size_t i = 0, o = 0;

	while (i < n) {
		char c = b[i];

		if (o == 0 && indent) {
			line[o++] = ' ';
			line[o++] = '|';
			line[o++] = ' ';
		}
		if (c == '\n') {
			line[o++] = '\n';
			emit(line, o);
			o = 0;
			i++;
			if (o == 0 && indent && i == n)
				break;
			continue;
		}
		if (c == '\t' || (c >= 0x20 && c < 0x7f))
			line[o++] = c;
		else
			line[o++] = '.';
		i++;
		if (o >= sizeof(line) - 4) {
			line[o++] = '\n';                 /* wrap long lines */
			emit(line, o);
			o = 0;
		}
	}
	if (o) {
		line[o++] = '\n';
		emit(line, o);
	}
}

/* ------------------------------------------------------------- file reading */
struct blob {
	char *buf;
	size_t cap;
	size_t len;
	unsigned long long total;
	unsigned long long fnv_all;     /* FNV-1a 64 of EVERY byte of the file  */
	int truncated;
};

#define FNV_BASIS 14*****************ULL
#define FNV_PRIME 1099511628211ULL

static void blob_read(const char *path, struct blob *b)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	b->len = 0;
	b->total = 0;
	b->fnv_all = FNV_BASIS;
	b->truncated = 0;
	if (fd < 0)
		return;
	for (;;) {
		ssize_t r;
		ssize_t k;

		if (b->len == b->cap) {
			/* keep the NEWEST bytes: half the window is dropped */
			memmove(b->buf, b->buf + b->cap / 2, b->cap - b->cap / 2);
			b->len = b->cap - b->cap / 2;
			b->truncated = 1;
		}
		r = read(fd, b->buf + b->len, b->cap - b->len);
		if (r <= 0)
			break;
		/* the hash covers the WHOLE file, so it can be compared with a
		 * dd of the rawdump partition; the buffer only ever holds the
		 * newest window of it */
		for (k = 0; k < r; k++) {
			b->fnv_all ^= (unsigned char)b->buf[b->len + (size_t)k];
			b->fnv_all *= FNV_PRIME;
		}
		b->len += (size_t)r;
		b->total += (unsigned long long)r;
	}
	close(fd);
}

#define FNV(b, n) ({                                                  \
	unsigned long long _h = FNV_BASIS;                            \
	size_t _i;                                                    \
	for (_i = 0; _i < (size_t)(n); _i++) {                        \
		_h ^= (unsigned char)(b)[_i];                         \
		_h *= FNV_PRIME;                                      \
	}                                                             \
	_h;                                                           \
})

static int exists(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

/* "file <path>: N bytes" + its text, or "(absent)" -- never silent */
static void section_file(const char *path, const char *label, size_t cap, int indent)
{
	char p[512];
	static char buf[FILE_MAX + 8];
	struct blob b = { .buf = buf };

	P(path, p, sizeof(p));
	b.cap = cap > sizeof(buf) ? sizeof(buf) : cap;
	if (!exists(p)) {
		outf("  %-28s : (absent)\n", label);
		return;
	}
	blob_read(p, &b);
	outf("  %-28s : %llu bytes%s\n", label, b.total,
	     b.truncated ? " (TRUNCATED, newest kept)" : "");
	if (!b.len)
		return;
	emit_sanitized(b.buf, b.len, indent);
}

/* list a directory's names, sorted, capped -- or the reason it is empty */
static void section_dir(const char *path, const char *label, int max)
{
	char p[512];
	DIR *d;
	struct dirent *e;
	static char names[LIST_MAX + 64][80];
	int n = 0, i, j;

	P(path, p, sizeof(p));
	d = opendir(p);
	if (!d) {
		outf("  %-28s : (absent: %s)\n", label, strerror(errno));
		return;
	}
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || n >= max)
			continue;
		snprintf(names[n], sizeof(names[0]), "%.79s", e->d_name);
		n++;
	}
	closedir(d);
	for (i = 0; i < n; i++)                 /* small n: insertion sort */
		for (j = i + 1; j < n; j++)
			if (strcmp(names[j], names[i]) < 0) {
				char t[80];

				memcpy(t, names[i], sizeof(t));
				memcpy(names[i], names[j], sizeof(t));
				memcpy(names[j], t, sizeof(t));
			}
	outf("  %-28s : %d entries\n", label, n);
	for (i = 0; i < n; i++)
		outf("    %s\n", names[i]);
}

/* count *.ko under a modules dir, and list the first few */
static void section_modules_dir(const char *path)
{
	char p[512];
	DIR *d;
	struct dirent *e;
	static char names[64][80];
	int n = 0, i, j;

	P(path, p, sizeof(p));
	d = opendir(p);
	if (!d) {
		outf("  %-28s : (absent)\n", path);
		return;
	}
	while ((e = readdir(d))) {
		size_t L = strlen(e->d_name);

		if (L < 4 || strcmp(e->d_name + L - 3, ".ko") || n >= 64)
			continue;
		snprintf(names[n], sizeof(names[0]), "%.79s", e->d_name);
		n++;
	}
	closedir(d);
	for (i = 0; i < n; i++)                 /* small n: insertion sort */
		for (j = i + 1; j < n; j++)
			if (strcmp(names[j], names[i]) < 0) {
				char t[80];

				memcpy(t, names[i], sizeof(t));
				memcpy(names[i], names[j], sizeof(t));
				memcpy(names[j], t, sizeof(t));
			}
	outf("  %-28s : %d .ko files\n", path, n);
	for (i = 0; i < n && i < 16; i++)
		outf("    %s\n", names[i]);
}

/* --------------------------------------------------------------- interface */
static int if_flags(const char *ifname, short *flags)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	int rc = -1;

	if (s < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
		*flags = ifr.ifr_flags;
		rc = 0;
	}
	close(s);
	return rc;
}

/* READ-ONLY interface introspection: the relay never sets an address */
static int if_ipv4(const char *ifname, char *out, size_t n)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	int rc = 0;

	if (s < 0)
		return 0;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(s, SIOCGIFADDR, &ifr) == 0) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

		snprintf(out, n, "%s", inet_ntoa(sin->sin_addr));
		rc = 1;
	}
	close(s);
	return rc;
}

static int if_netmask(const char *ifname, char *out, size_t n)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	int rc = 0;

	if (s < 0)
		return 0;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(s, SIOCGIFNETMASK, &ifr) == 0) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_netmask;

		snprintf(out, n, "%s", inet_ntoa(sin->sin_addr));
		rc = 1;
	}
	close(s);
	return rc;
}

static int if_mtu(const char *ifname)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	int rc = -1;

	if (s < 0)
		return -1;
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (ioctl(s, SIOCGIFMTU, &ifr) == 0)
		rc = ifr.ifr_mtu;
	close(s);
	return rc;
}

/* ----------------------------------------------------------------- sections */
static void sec_header(void)
{
	unsigned long long up_s = (g_now_ms - g_started_ms) / 1000;

	outf("####################################################################\n");
	outf("# nx679j v9 DIAGNOSTIC RELAY - READ-ONLY SNAPSHOT\n");
	outf("# %s\n", VERSION);
	outf("# This program never opens a file for writing and never changes the\n");
	outf("# device: it only READS /proc, /sys and the boot journal, and writes\n");
	outf("# this text to the socket.  Nothing the client sends is looked at.\n");
	outf("####################################################################\n");
	outf("[relay] version=%s port=%d bind=%s bind_all=%d listen_ok=%d\n",
	     VERSION, g_port, g_bind_ip, g_bind_all, g_listen_ok);
	outf("[relay] iface=%s if_index=%d present=%d ipv4=%s wanted_ip=%s\n",
	     g_ifname, g_if_index, g_if_present, g_if_ip, g_want_ip);
	outf("[relay] wanted_ip=%s on_an_interface=%s(%s) nonlocal_bind=%s\n",
	     g_want_ip, g_want_present ? "yes" : "no", g_want_on, g_nonlocal);
outf("[relay] clients_accepted=%lu serving_children=%u uptime_s=%llu "
     "since_start_s=%llu root=%s\n",
     g_clients, g_children_alive, g_now_ms / 1000, up_s,
     (g_root && *g_root) ? g_root : "(/)");
outf("[relay] client_deadline_s=%d file_cap=%d journal_window=%d "
     "(one client is killed on that deadline, so a reader that stops "
     "reading cannot hold the relay)\n",
     g_client_deadline_s, FILE_MAX, JOURNAL_MAX);
	outf("[relay] the journal is in section [8]; a copy of the same text is "
	     "on the boot console\n");
}

static void sec_kernel(void)
{
	struct utsname u;

	outf("\n[1] KERNEL\n");
	if (!uname(&u))
		outf("  uname                        : %s %s %s %s\n", u.sysname,
		     u.release, u.version, u.machine);
	section_file("/proc/version", "version", 4096, 1);
	section_file("/proc/cmdline", "cmdline", 4096, 1);
	section_file("/proc/uptime", "uptime", 4096, 1);
	section_file("/proc/loadavg", "loadavg", 4096, 1);
	section_file("/proc/meminfo", "meminfo (capped)", 2048, 1);
	section_file("/proc/devices", "devices", 4096, 1);
}

static void sec_network(void)
{
	char p[512];
	DIR *d;
	struct dirent *e;
	int n = 0;

	outf("\n[2] NETWORK INTERFACES (read with ioctl: nothing is configured "
	     "here)\n");
	P("/sys/class/net", p, sizeof(p));
	d = opendir(p);
	if (!d) {
		outf("  /sys/class/net               : (absent)\n");
		return;
	}
	while ((e = readdir(d)) && n < 16) {
		char addr[64] = "-", mask[64] = "-", st[64] = "-";
		short fl = 0;
		int mtu;

		/* /sys/class/net is a symlink farm: readdir gives the names,
		 * the addresses come from ioctl on the same name */
		if (e->d_name[0] == '.')
			continue;
		n++;
		if (!if_ipv4(e->d_name, addr, sizeof(addr)))
			snprintf(addr, sizeof(addr), "-");
		if (!if_netmask(e->d_name, mask, sizeof(mask)))
			snprintf(mask, sizeof(mask), "-");
		mtu = if_mtu(e->d_name);
		if (if_flags(e->d_name, &fl))
			fl = 0;
		{
			char q[512], q2[512];
			char cbuf[64];

			snprintf(q, sizeof(q), "/sys/class/net/%s/operstate",
				 e->d_name);
			P(q, q2, sizeof(q2));
			if (exists(q2)) {
				int fd = open(q2, O_RDONLY | O_CLOEXEC);

				if (fd >= 0) {
					ssize_t r = read(fd, cbuf, sizeof(cbuf) - 1);

					close(fd);
					if (r > 0) {
						cbuf[r] = 0;
						if (strchr(cbuf, '\n'))
							*strchr(cbuf, '\n') = 0;
						snprintf(st, sizeof(st), "%s", cbuf);
					}
				}
			}
		}
		outf("  %-10s addr=%-15s mask=%-15s mtu=%d flags=0x%04x "
		     "(up=%d running=%d) operstate=%s\n",
		     e->d_name, addr, mask, mtu, (unsigned short)fl,
		     !!(fl & IFF_UP), !!(fl & IFF_RUNNING), st);
	}
	closedir(d);
	outf("  interfaces seen: %d\n", n);
}

static void sec_usb(void)
{
	char p[512];
	DIR *d;
	struct dirent *e;
	int n = 0;

	outf("\n[3] USB GADGET\n");
	P("/sys/class/udc", p, sizeof(p));
	d = opendir(p);
	if (!d) {
		outf("  /sys/class/udc               : (absent)\n");
	} else {
		while ((e = readdir(d))) {
			char q[512], v[128];
			int fd;

			if (e->d_name[0] == '.')
				continue;
			n++;
			outf("  udc %s\n", e->d_name);
			for (int k = 0; k < 3; k++) {
				static const char *attrs[3] = {"state", "current_speed",
							       "function"};
				char pv[512];

				snprintf(q, sizeof(q), "/sys/class/udc/%s/%s",
					 e->d_name, attrs[k]);
				P(q, pv, sizeof(pv));
				v[0] = 0;
				fd = open(pv, O_RDONLY | O_CLOEXEC);
				if (fd >= 0) {
					ssize_t r = read(fd, v, sizeof(v) - 1);

					close(fd);
					if (r > 0) {
						v[r] = 0;
						if (strchr(v, '\n'))
							*strchr(v, '\n') = 0;
					} else {
						v[0] = 0;
					}
				}
				outf("      %-14s : %s\n", attrs[k],
				     v[0] ? v : "(unreadable)");
			}
		}
		closedir(d);
	}
	outf("  udcs: %d\n", n);
	section_dir("/sys/class/usb_role", "usb_role nodes", 16);
	section_file("/sys/kernel/config/usb_gadget/g1/UDC", "gadget g1 UDC", 256, 1);
	section_file("/sys/kernel/config/usb_gadget/g1/idVendor", "g1 idVendor", 256, 1);
	section_file("/sys/kernel/config/usb_gadget/g1/idProduct", "g1 idProduct", 256, 1);
	section_dir("/sys/kernel/config/usb_gadget/g1/functions", "g1 functions", 16);
	section_dir("/sys/kernel/config/usb_gadget/g1/configs", "g1 configs", 16);
	section_file("/sys/kernel/config/usb_gadget/g1/strings/0x409/product",
		     "g1 product string", 256, 1);
}

static void sec_storage(void)
{
	char p[512];
	DIR *d;
	struct dirent *e;
	int n = 0;

	outf("\n[4] STORAGE / MOUNTS / RAWDUMP\n");
	P("/sys/class/block", p, sizeof(p));
	d = opendir(p);
	if (!d) {
		outf("  /sys/class/block           : (absent)\n");
	} else {
		while ((e = readdir(d)) && n < 40) {
			char q[512], q2[512], sz[64], dv[64];
			int fd;

			if (e->d_name[0] == '.')
				continue;
			sz[0] = dv[0] = 0;
			snprintf(q, sizeof(q), "/sys/class/block/%s/size", e->d_name);
			P(q, q2, sizeof(q2));
			fd = open(q2, O_RDONLY | O_CLOEXEC);
			if (fd >= 0) {
				ssize_t r = read(fd, sz, sizeof(sz) - 1);

				close(fd);
				if (r > 0) {
					sz[r] = 0;
					if (strchr(sz, '\n'))
						*strchr(sz, '\n') = 0;
				}
			}
			snprintf(q, sizeof(q), "/sys/class/block/%s/dev", e->d_name);
			P(q, q2, sizeof(q2));
			fd = open(q2, O_RDONLY | O_CLOEXEC);
			if (fd >= 0) {
				ssize_t r = read(fd, dv, sizeof(dv) - 1);

				close(fd);
				if (r > 0) {
					dv[r] = 0;
					if (strchr(dv, '\n'))
						*strchr(dv, '\n') = 0;
				}
			}
			outf("  %-14s size=%-12s sectors dev=%s\n", e->d_name,
			     sz[0] ? sz : "?", dv[0] ? dv : "?");
			n++;
		}
		closedir(d);
	}
	outf("  block devices listed: %d\n", n);
	section_dir("/dev/block/by-name", "/dev/block/by-name", 40);
	section_file("/proc/mounts", "mounts", 4096, 1);
	outf("  %-28s : %s\n", "/sys/.../1d84000.ufshc",
	     exists("/sys/devices/platform/soc/1d84000.ufshc") ? "present" : "ABSENT");
	outf("  %-28s : %s\n", "/dev/rd (rawdump node)",
	     exists("/dev/rd") ? "present" : "absent");
	outf("  %-28s : %s\n", "/dev/kmsg", exists("/dev/kmsg") ? "present" : "absent");
	outf("  %-28s : %s\n", "/dev/pmsg0", exists("/dev/pmsg0") ? "present" : "absent");
	outf("  %-28s : %s\n", "/dev/console", exists("/dev/console") ? "present" : "absent");
}

static void sec_pstore(void)
{
	outf("\n[5] PSTORE / RAMOOPS\n");
	section_dir("/sys/fs/pstore", "/sys/fs/pstore", 16);
}

static void sec_modules(void)
{
	outf("\n[6] MODULES LOADED (/sys/module)\n");
	section_dir("/sys/module", "/sys/module (loaded)", LIST_MAX);
	outf("\n[7] MODULE FILES (/lib/modules)\n");
	section_modules_dir("/lib/modules");
}

/* read a file and print its first `maxlines` lines as a readable text block */
static void section_lines(const char *path, const char *label, int maxlines)
{
	static char buf[16 * 1024];
	char p[512];
	ssize_t r;
	size_t len = 0, i, start = 0;
	int fd, n = 0;

	P(path, p, sizeof(p));
	fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		outf("  %-28s : (absent: %s)\n", label, strerror(errno));
		return;
	}
	while (len < sizeof(buf) - 1 &&
	       (r = read(fd, buf + len, sizeof(buf) - 1 - len)) > 0)
		len += (size_t)r;
	close(fd);
	buf[len] = 0;
	outf("  %-28s : %llu bytes\n", label, (unsigned long long)len);
	for (i = 0; i <= len && n < maxlines; i++) {
		if (i == len || buf[i] == '\n') {
			char save = buf[i];

			buf[i] = 0;
			outf("    %s\n", buf + start);
			if (!save)
				break;
			n++;
			start = i + 1;
		}
	}
}

/* where does this program actually live?  the answer is not always the
 * obvious one: PID 1 starts it, so its mount namespace and PID 1's must be the
 * same, and this section is how a reader can CHECK that instead of assuming
 * it. */
static void sec_self(void)
{
	char p[512];
	struct stat st;

	outf("\n[9] RELAY SELF-CHECK (the world this program is running in)\n");
	outf("  pid/ppid                    : %d / %d\n", (int)getpid(), (int)getppid());
	if (!stat("/", &st))
		outf("  stat /                      : dev=%llu ino=%llu\n",
		     (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
	snprintf(p, sizeof(p), "%s/proc", g_root);
	outf("  stat /proc                  : %s\n",
	     stat(p, &st) ? strerror(errno) : "present");
	snprintf(p, sizeof(p), "%s/sys", g_root);
	outf("  stat /sys                   : %s\n",
	     stat(p, &st) ? strerror(errno) : "present");
	{
		char t[128];
		ssize_t rl;

		P("/proc/self/ns/mnt", p, sizeof(p));
		rl = readlink(p, t, sizeof(t) - 1);
		outf("  /proc/self/ns/mnt           : %s\n",
		     rl > 0 ? (t[rl] = 0, t) : strerror(errno));
		P("/proc/1/ns/mnt", p, sizeof(p));
		rl = readlink(p, t, sizeof(t) - 1);
		outf("  /proc/1/ns/mnt (PID 1)      : %s\n",
		     rl > 0 ? (t[rl] = 0, t) : strerror(errno));
	}
	outf("  ---- /proc/self/mountinfo, first 30 lines ----\n");
	section_lines("/proc/self/mountinfo", "/proc/self/mountinfo", 30);
	outf("  ---- raw errno for a fixed list of paths (stat / open) ----\n");
	{
		static const char *probe[] = {
			"/", "/proc", "/proc/version", "/proc/self",
			"/proc/self/mountinfo", "/proc/1/ns/mnt", "/sys",
			"/sys/class/net", "/sys/kernel/config", "/dev/kmsg",
			"/nx679j-journal", "/nx679j", "/nx679j/relay", "/init",
		};
		unsigned k;

		for (k = 0; k < sizeof(probe) / sizeof(probe[0]); k++) {
			int se, oe, fd;
			struct stat st;

			P(probe[k], p, sizeof(p));
			se = stat(p, &st) ? errno : 0;
			fd = open(p, O_RDONLY | O_CLOEXEC);
			oe = fd < 0 ? errno : 0;
			if (fd >= 0)
				close(fd);
			outf("  %-27s stat=%-3d(%s) open=%-3d(%s)%s\n", probe[k],
			     se, se ? strerror(se) : "ok",
			     oe, oe ? strerror(oe) : "ok",
			     se == 0 ? (S_ISDIR(st.st_mode) ? " dir" : " file") : "");
		}
	}
	outf("  ---- the journal file itself ----\n");
	section_lines("/nx679j-journal", "/nx679j-journal (first 2 lines)", 2);
}

/* does ANY interface in this namespace carry this IPv4 address? */
static int if_any_has(const char *want, char *who, size_t n)
{
	DIR *d;
	struct dirent *e;
	char p[512];
	int found = 0;
	char tmp[64];

	P("/sys/class/net", p, sizeof(p));
	d = opendir(p);
	if (!d)
		return 0;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		if (if_ipv4(e->d_name, tmp, sizeof(tmp)) && !strcmp(tmp, want)) {
			snprintf(who, n, "%.63s", e->d_name);
			found = 1;
			break;
		}
	}
	closedir(d);
	return found;
}

/* the kernel's answer for an fd, so the relay can report what it GOT and not
 * what it asked for */
static void bound_from_kernel(int fd)
{
	struct sockaddr_in sa;
	socklen_t len = sizeof(sa);

	memset(&sa, 0, sizeof(sa));
	if (getsockname(fd, (struct sockaddr *)&sa, &len) != 0) {
		snprintf(g_bind_ip, sizeof(g_bind_ip), "unknown(%s)",
			 strerror(errno));
		return;
	}
	if (sa.sin_addr.s_addr == htonl(INADDR_ANY)) {
		snprintf(g_bind_ip, sizeof(g_bind_ip), "0.0.0.0");
		g_bind_all = 1;
		return;
	}
	snprintf(g_bind_ip, sizeof(g_bind_ip), "%s", inet_ntoa(sa.sin_addr));
}

static void console_ifaces(void)
{
	char p[512], line[600], a[64];
	DIR *d;
	struct dirent *e;
	size_t off = 0;
	int n = 0;

	P("/sys/class/net", p, sizeof(p));
	d = opendir(p);
	if (!d) {
		dprintf(1, "%s: no /sys/class/net: cannot list interfaces\n",
			VERSION);
		return;
	}
	line[0] = 0;
	while ((e = readdir(d)) && off + 80 < sizeof(line)) {
		if (e->d_name[0] == '.')
			continue;
		if (!if_ipv4(e->d_name, a, sizeof(a)))
			snprintf(a, sizeof(a), "-");
		off += (size_t)snprintf(line + off, sizeof(line) - off, "%s=%s ",
				       e->d_name, a);
		n++;
	}
	closedir(d);
	dprintf(1, "%s: interfaces in this namespace (%d): %s\n", VERSION, n,
		line);
}

static void sec_journal(void)
{
	static char jbuf[JOURNAL_MAX];
	struct blob b = { .buf = jbuf, .cap = sizeof(jbuf) };
	char pj[512];
	size_t start = 0;

	blob_read(P("/nx679j-journal", pj, sizeof(pj)), &b);
	if (!b.total) {
		outf("\n[8] THE BOOT JOURNAL /nx679j-journal\n");
		outf("  (empty or absent: the journal lives in RAM (/nx679j-journal) "
		     "and is pushed to\n  the rawdump partition by the worker's "
		     "dump step)\n");
		return;
	}
	if (b.truncated) {
		/* the window starts in the middle of a line: drop that partial
		 * line so every line below is a complete journal line */
		char *nl = memchr(b.buf, '\n', b.len);

		if (nl)
			start = (size_t)(nl - b.buf) + 1;
	}
	outf("\n[8] THE BOOT JOURNAL /nx679j-journal\n");
	outf("  journal_file_bytes=%llu window_bytes=%llu\n", b.total,
	     (unsigned long long)(b.len - start));
	outf("  hash_fnv1a64_full=0x%016llx hash_fnv1a64_window=0x%016llx%s\n",
	     b.fnv_all, FNV(b.buf + start, b.len - start),
	     b.truncated ? "  (TRUNCATED: the NEWEST complete lines are shown;\n"
			   "  hash_fnv1a64_window is over exactly the lines below, "
			   "hash_fnv1a64_full is over the whole file)" : "");
	outf("  ----8<---- nx679j-journal ----8<----\n");
	emit_sanitized(b.buf + start, b.len - start, 0);
	outf("  ---->8---- end of journal ---->8----\n");
}

static void serve(int c, const struct sockaddr_in *peer)
{
	char ip[64];

	snprintf(ip, sizeof(ip), "%s", inet_ntoa(peer->sin_addr));
	g_c = c;
	g_sent = 0;
	g_now_ms = now_ms();
	sec_header();
	outf("[client] peer=%s:%u\n", ip, (unsigned)ntohs(peer->sin_port));
	sec_kernel();
	sec_network();
	sec_usb();
	sec_storage();
	sec_pstore();
	sec_modules();
	sec_journal();
	sec_self();
	outf("\n[10] END OF DUMP - %llu bytes, %s.  Reconnect (`nc %s %d`) for a "
	     "new snapshot.\n", g_sent, VERSION, g_bind_ip, g_port);
}

int main(int argc, char **argv)
{
	const char *e;
	int wait_s = DEFAULT_WAIT, i = 0;
	int lfd = -1;
	struct sockaddr_in sa;

	e = getenv("V9_RELAY_ROOT");
	if (e)
		g_root = e;
	if ((e = getenv("V9_RELAY_IF")) && *e)
		g_ifname = e;
	if ((e = getenv("V9_RELAY_IP")) && *e)
		g_want_ip = e;
	if ((e = getenv("V9_RELAY_WAIT")) && *e)
		wait_s = atoi(e);
	if ((e = getenv("V9_RELAY_DEADLINE")) && *e && atoi(e) > 0)
		g_client_deadline_s = atoi(e);
	if (argc > 1 && atoi(argv[1]) > 0)
		g_port = atoi(argv[1]);

	g_started_ms = now_ms();
	signal(SIGPIPE, SIG_IGN);

	/* ---- wait (bounded) for the interface the gadget creates ----------- */
	/*
	 * The wait only makes sense while the interface EXISTS but its address
	 * is not there yet (the gadget op sets the address and it can lag by a
	 * fraction of a second).  If the interface does not exist at all there
	 * is nothing to wait for: bind the wildcard immediately, because the
	 * wildcard covers 10.0.0.1:PORT the moment that address appears, and
	 * waiting would only delay the relay by the whole timeout.
	 */
	g_if_index = (int)if_nametoindex(g_ifname);
	while (g_if_index && i < wait_s) {
		if (if_ipv4(g_ifname, g_if_ip, sizeof(g_if_ip)) &&
		    !strcmp(g_if_ip, g_want_ip))
			break;
		sleep_ms(1000);
		i++;
	}
	g_if_present = g_if_index ? 1 : 0;
	if (!g_if_present || !if_ipv4(g_ifname, g_if_ip, sizeof(g_if_ip)))
		snprintf(g_if_ip, sizeof(g_if_ip), "-");

	/* ---- bind, ONCE, to the gadget's address (fallback reported) ------- */
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)g_port);
	if (!inet_pton(AF_INET, g_want_ip, &sa.sin_addr))
		sa.sin_addr.s_addr = htonl(INADDR_ANY);
	/*
	 * WHICH ADDRESS DO WE BIND?
	 *
	 * The wanted address first -- but ONLY if this namespace really has it on
	 * an interface.  A kernel can accept a bind to an address it does not have
	 * (the device kernel does: QEMU guest A printed "wanted 10.0.0.1 is NOT on
	 * any interface ... bound 10.0.0.1" with nonlocal_bind=0), and a listener
	 * on an address no interface carries is a listener nobody can reach while
	 * the dump claims otherwise.  So: no address on an interface -> wildcard,
	 * which covers the wanted address the moment it appears.
	 */
	g_want_present = if_any_has(g_want_ip, g_want_on, sizeof(g_want_on));
	{
		static char nb[16];
		struct blob b = { .buf = nb, .cap = sizeof(nb) };
		char pnb[512];

		blob_read(P("/proc/sys/net/ipv4/ip_nonlocal_bind", pnb,
			    sizeof(pnb)), &b);
		if (b.len) {
			size_t k;

			nb[b.len] = 0;
			for (k = 0; k < b.len; k++)
				if (nb[k] == '\n' || nb[k] == '\r')
					nb[k] = 0;
			snprintf(g_nonlocal, sizeof(g_nonlocal), "%.15s", nb);
		} else {
			snprintf(g_nonlocal, sizeof(g_nonlocal), "-");
		}
	}
	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		snprintf(g_bind_ip, sizeof(g_bind_ip), "none");
		dprintf(1, "%s: socket rc=-%d (%s)\n", VERSION, errno, strerror(errno));
		return 3;
	}
	{
		int one = 1;

		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	}
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)g_port);
	if (g_want_present && inet_pton(AF_INET, g_want_ip, &sa.sin_addr) &&
	    bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
		/* got the address the gadget owns: this is the phone's normal case */
	} else {
		int e1 = errno;

		/*
		 * Either the address is not here yet or the bind was refused.
		 * Do NOT configure anything: this program is read-only.  Bind
		 * the wildcard address instead and SAY SO, in the dump and on
		 * the console.
		 */
		if (!g_want_present)
			dprintf(1, "%s: %s is NOT on any interface in this "
				"namespace: binding the wildcard instead\n",
				VERSION, g_want_ip);
		else
			dprintf(1, "%s: bind %s:%d refused (%s): binding the "
				"wildcard instead\n", VERSION, g_want_ip, g_port,
				strerror(e1));
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = htons((unsigned short)g_port);
		sa.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
			snprintf(g_bind_ip, sizeof(g_bind_ip), "none");
			dprintf(1, "%s: bind %s:%d rc=-%d (%s); wildcard rc=-%d "
				   "(%s)\n", VERSION, g_want_ip, g_port, e1,
				strerror(e1), errno, strerror(errno));
			close(lfd);
			return 4;
		}
		g_bind_all = 1;
		snprintf(g_bind_ip, sizeof(g_bind_ip), "0.0.0.0");
	}
	/*
	 * Whatever we asked for, the KERNEL's answer is what goes into the dump:
	 * a diagnostic that repeats the request instead of the result is how a
	 * reader gets misled.
	 */
	bound_from_kernel(lfd);
	if (listen(lfd, 16) != 0) {
		dprintf(1, "%s: listen rc=-%d (%s)\n", VERSION, errno, strerror(errno));
		close(lfd);
		return 5;
	}
	g_listen_ok = 1;
	console_ifaces();
	dprintf(1, "%s: wanted %s is %s this namespace (nonlocal_bind=%s); "
		"bound %s:%d\n", VERSION, g_want_ip,
		g_want_present ? "ON an interface in" : "NOT on any interface in",
		g_nonlocal, g_bind_ip, g_port);

	dprintf(1, "%s: READ-ONLY relay up: nc %s %d  (iface %s ip %s, "
		   "wildcard_fallback=%d)\n", VERSION, g_bind_ip, g_port,
		g_ifname, g_if_ip, g_bind_all);
	dprintf(1, "%s: it never opens a file for writing; the answer is a "
		   "snapshot of /proc, /sys and /nx679j-journal\n", VERSION);

	/* ---- serve, for ever (PID 1 respawns us if we ever die) ------------ */
	for (;;) {
		struct sockaddr_in peer;
		socklen_t plen = sizeof(peer);
		int c = accept(lfd, (struct sockaddr *)&peer, &plen);

		if (c < 0) {
			if (errno == EINTR) {
				/* a child finished */
			} else {
				sleep_ms(200);
				continue;
			}
		} else {
			pid_t pid;

			g_clients++;
			pid = fork();
			if (pid == 0) {
				close(lfd);
				/* SIGPIPE is ignored so that a client that closes
				 * early cannot kill this child with a signal we did
				 * not choose: the alarm below is the ONE deadline */
				signal(SIGPIPE, SIG_IGN);
				signal(SIGALRM, SIG_DFL);   /* hard client deadline */
				alarm(g_client_deadline_s);
				serve(c, &peer);
				close(c);
				_exit(0);
			}
			if (pid < 0) {
				dprintf(1, "%s: fork rc=-%d\n", VERSION, errno);
				close(c);
			} else {
				close(c);              /* the child owns it now */
				g_children_alive++;
			}
		}
		/* reap finished clients; never block (PID 1's rule, kept here too) */
		for (;;) {
			int st;

			if (waitpid(-1, &st, WNOHANG) <= 0)
				break;
			if (g_children_alive)
				g_children_alive--;
		}
	}
}
