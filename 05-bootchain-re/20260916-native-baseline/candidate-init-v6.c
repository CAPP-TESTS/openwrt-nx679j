/*
 * NX679J slot-B gadget probe, v6 -- /init as a STATIC ELF binary, PID 1.
 *
 * WHY v6 EXISTS (measured on the phone, 2026-09-17):
 *   v4 (stock kernel + 12 MB OpenWrt ramdisk + 327 modules) -> nothing: no USB
 *   enumeration in 140 s, breadcrumb journal on rawdump still all zeros.
 *   v5 (same kernel, 1.1 MB ramdisk, busybox + musl + kmodloader + a hand-built
 *   list of 40 modules + the same init script) -> nothing: no USB in 150 s.
 *   The known-good Magisk image on the same slot enumerated Android in 26 s,
 *   and a control run proved ABL boots slot B from an unsigned image.
 *   The missing piece was found in the device's own vendor ramdisk (which the
 *   boot chain unpacks into the same rootfs as our ramdisk): it ships 327 flat
 *   modules plus the authoritative modules.load / modules.load.recovery /
 *   modules.dep / modules.softdep, and its softdep file requires
 *       softdep smem     pre: qcom_hwspinlock
 *       softdep dwc3_msm pre: phy-generic phy-msm-snps-hs phy-msm-ssusb-qmp eud
 *   none of which the v5 list contained: smem.ko and dwc3_msm.ko could not
 *   resolve their symbols, so nothing came up and nothing was ever journalled.
 *
 * v6 therefore removes every userspace dependency that could kill PID 1
 * silently AND stops guessing the module order:
 *   - /init is a statically linked ELF (aarch64) with NO interpreter and NO
 *     DT_NEEDED: no shell, no busybox, no musl, no kmodloader.
 *   - the module chain is read from the device's own metadata at runtime
 *     (modules.dep hard deps + modules.softdep pre:/post:) and loaded with
 *     finit_module(2) in topological order; the 40-name list is only the
 *     fallback used when that metadata is absent (e.g. under QEMU).
 *   - the USB gadget (configfs NCM, libcomposite and usb_f_ncm are BUILT IN) is
 *     attempted the instant dwc3_msm and its softdeps are in, and retried
 *     forever afterwards. The UDC the live phone reports is a600000.dwc3.
 *   - the breadcrumb journal is a 64 KiB in-memory ring dumped to rawdump as
 *     soon as UFS appears, with one line per module: name, rc and the error
 *     text, which is the evidence that is read back from Android afterwards.
 *   - PID 1 NEVER returns from main() and NEVER calls exit(): every failure is
 *     logged and retried forever, so "no USB on the host" can no longer be
 *     explained by "init exited and the kernel panicked".
 *
 * Read the result like this:
 *   host sees 18d1:4ee7  -> kernel reached userspace, the module chain worked
 *                           and the gadget is up: the OpenWrt side is unblocked.
 *   no USB at all        -> read the rawdump journal: it lists every module with
 *                           its rc, so the first failing link is named there.
 *
 * Channels (all guarded, all logged with their return code):
 *   1. /dev/pmsg0   ramoops pmsg, readable from Android at /sys/fs/pstore
 *                   (the real major is resolved from /proc/devices: pmsg uses a
 *                   DYNAMIC major, 252 on the live phone is not a constant)
 *   2. /dev/kmsg    kernel log ring, which PStore console also captures
 *   3. /dev/console serial console, opened O_NONBLOCK so it can never block PID 1
 *   4. rawdump      the 256 MiB partition, once ufs_qcom/its deps are in
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>

#define TAG "nx679j-v6"

/* ------------------------------------------------------------------ channels */
static int fd_kmsg = -1, fd_pmsg = -1, fd_console = -1;
static int rc_kmsg, rc_pmsg, rc_console;    /* open() return codes */
static unsigned long n_log;

static void put_fd(int fd, const char *b, size_t n)
{
	if (fd >= 0)
		(void)!write(fd, b, n);          /* never let a channel stop us */
}

static void logmsg(const char *fmt, ...)
{
	char buf[512];
	int n, left = (int)sizeof(buf) - 1;
	va_list ap;

	n = snprintf(buf, sizeof(buf), TAG ": ");
	if (n < 0)
		return;
	left -= n;
	va_start(ap, fmt);
	n += vsnprintf(buf + n, (size_t)(left > 0 ? left : 0), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n > (int)sizeof(buf) - 2)
		n = (int)sizeof(buf) - 2;
	buf[n++] = '\n';

	put_fd(fd_kmsg, buf, (size_t)n);
	put_fd(fd_pmsg, buf, (size_t)n);
	put_fd(fd_console, buf, (size_t)n);
	n_log++;
}

/* ------------------------------------------------------------------ files */
/* write a string to a path; returns 0 on success, -errno otherwise */
static int wf(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t w;
	size_t len = strlen(val);

	if (fd < 0)
		return -errno;
	w = write(fd, val, len);
	close(fd);
	if (w != (ssize_t)len)
		return w < 0 ? -errno : -EIO;
	return 0;
}

/* read up to len-1 bytes, NUL-terminate and strip the trailing newline;
 * returns the number of bytes read, or -errno */
static int rf(const char *path, char *out, size_t len)
{
	int fd, n;
	char *nl;

	if (!len)
		return -EINVAL;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = (int)read(fd, out, len - 1);
	close(fd);
	if (n < 0)
		return -errno;
	out[n] = '\0';
	nl = strchr(out, '\n');
	if (nl)
		*nl = '\0';
	return n;
}

/* whole-file read, newlines kept (for /proc/devices); -errno on failure */
static int rfall(const char *path, char *out, size_t len)
{
	int fd, n;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = (int)read(fd, out, len - 1);
	close(fd);
	if (n < 0)
		return -errno;
	out[n] = '\0';
	return n;
}

static int mkdir_p(const char *path)
{
	char tmp[256];
	char *p;

	if (strlen(path) >= sizeof(tmp))
		return -ENAMETOOLONG;
	strcpy(tmp, path);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) && errno != EEXIST)
			return -errno;
		*p = '/';
	}
	if (mkdir(tmp, 0755) && errno != EEXIST)
		return -errno;
	return 0;
}

static void sleep_s(int secs)
{
	struct timespec ts = { secs, 0 };

	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

/* ------------------------------------------------------------------ journal */
/* A 16 KiB in-memory ring of log lines. No filesystem involved: v6 keeps no
 * tmpfs so that /tmp can never be the thing that failed.
 *
 * The whole ring is dumped to rawdump (oldest first, 4 KiB at a time, from
 * offset 0): the chain of 327 module lines plus the boot log fits in 64 KiB,
 * and a single-sector dump would cut off the head of the chain. It is only
 * re-dumped when the ring has grown, so the partition is not rewritten for
 * nothing. */
#define JRING (64 * 1024)
static char jring[JRING];
static size_t jlen;
static size_t jflushed;          /* bytes already written to rawdump */

static void journal_add(const char *line)
{
	size_t n = strlen(line) + 1;

	if (n > JRING / 2)
		n = JRING / 2;
	if (jlen + n > JRING) {                 /* drop the oldest half */
		size_t keep = JRING / 2;

		memmove(jring, jring + (jlen - keep), keep);
		jlen = keep;
	}
	memcpy(jring + jlen, line, n - 1);
	jring[jlen + n - 1] = '\n';
	jlen += n;
}

/* logmsg + journal in one call is what most of this program wants */
static void note(const char *fmt, ...)
{
	char buf[400];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	logmsg("%s", buf);
	journal_add(buf);
}

/* ------------------------------------------------------------------ usb gadget */
#define GDIR "/sys/kernel/config/usb_gadget/g1"
static char udc_cur[128];                  /* UDC we are bound to, "" if none */

/* first real UDC in /sys/class/udc, skipping the always-present dummy_udc.0
 * (binding to the dummy proves nothing and would hide the real controller) */
static int first_udc(char *out, size_t len)
{
	DIR *d = opendir("/sys/class/udc");
	struct dirent *e;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		if (!strncmp(e->d_name, "dummy_udc", 9))
			continue;
		snprintf(out, len, "%s", e->d_name);
		closedir(d);
		return 1;
	}
	closedir(d);
	return 0;
}

/* every role-switch node that exists is poked towards "device": if the DWC3
 * came up in host/none role, the configfs bind fails with -ENODEV and the host
 * sees nothing. UNPROVEN on this hardware (no phone run yet). */
static int poke_role(void)
{
	DIR *d = opendir("/sys/class/usb_role");
	struct dirent *e;
	int n = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char p[256];
		char got[64] = "?";

		if (e->d_name[0] == '.')
			continue;
		snprintf(p, sizeof(p), "/sys/class/usb_role/%s/role",
			 e->d_name);
		if (wf(p, "device"))
			continue;
		rf(p, got, sizeof(got));
		note("role %s -> %s", p, got);
		n++;
	}
	closedir(d);
	return n;
}

/* ioctl route to "ip link set usb0 up" + "ip addr add 10.0.0.1/24": the v6
 * ramdisk has no `ip` binary and no shell. */
static void net_up(const char *ifname, const char *ip, const char *mask)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	struct sockaddr_in *sin;
	int rc_addr, rc_mask, rc_up;

	if (s < 0) {
		note("net socket rc=%d", -errno);
		return;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	sin->sin_family = AF_INET;
	inet_pton(AF_INET, ip, &sin->sin_addr);
	rc_addr = ioctl(s, SIOCSIFADDR, &ifr);

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	sin = (struct sockaddr_in *)&ifr.ifr_netmask;
	sin->sin_family = AF_INET;
	inet_pton(AF_INET, mask, &sin->sin_addr);
	rc_mask = ioctl(s, SIOCSIFNETMASK, &ifr);

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	rc_up = ioctl(s, SIOCGIFFLAGS, &ifr);
	if (!rc_up) {
		ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
		rc_up = ioctl(s, SIOCSIFFLAGS, &ifr);
	}
	close(s);

	{
		char p[128], mac[32] = "?";
		int rc_mac;

		snprintf(p, sizeof(p), "/sys/class/net/%s/address", ifname);
		rc_mac = rf(p, mac, sizeof(mac));
		note("net %s addr rc=%d mask rc=%d up rc=%d mac=%s rc_mac=%d",
		     ifname, rc_addr, rc_mask, rc_up, mac, rc_mac);
	}
}

/*
 * One gadget cycle. wait_s > 0 waits that long for a UDC before giving up.
 * Returns 0 if a gadget was bound, -1 if there was no UDC, -2 if the bind
 * failed. Never aborts: every error is logged and left behind.
 */
static int gadget_cycle(int cyc, int wait_s)
{
	char udc[128], path[256], cur[128] = "";
	int i, rc_bind = -1, same;
	struct stat st;
	int rc_state_read;

	for (i = 0; i < wait_s; i++) {
		if (first_udc(udc, sizeof(udc)))
			break;
		sleep_s(1);
	}
	if (!first_udc(udc, sizeof(udc))) {
		note("cycle=%d udc=NONE after %ds", cyc, wait_s);
		return -1;
	}

	if (strcmp(udc, udc_cur)) {
		note("cycle=%d udc=%s (was \"%s\")", cyc, udc, udc_cur);
		snprintf(udc_cur, sizeof(udc_cur), "%s", udc);
	}

	/* configfs layout: the dirs are made once, re-making them is harmless */
	mkdir_p(GDIR);
	mkdir_p(GDIR "/strings/0x409");
	mkdir_p(GDIR "/configs/c.1");
	mkdir_p(GDIR "/configs/c.1/strings/0x409");
	mkdir_p(GDIR "/functions/ncm.usb0");

	wf(GDIR "/idVendor", "0x18d1");
	wf(GDIR "/idProduct", "0x4ee7");
	wf(GDIR "/bcdUSB", "0x0200");
	wf(GDIR "/bcdDevice", "0x0100");
	wf(GDIR "/strings/0x409/manufacturer", "OpenWrt");
	wf(GDIR "/strings/0x409/product", "NX679J");
	wf(GDIR "/strings/0x409/serialnumber", "nx679j-v6");
	wf(GDIR "/configs/c.1/strings/0x409/configuration", "NCM");
	wf(GDIR "/configs/c.1/MaxPower", "250");

	snprintf(path, sizeof(path), GDIR "/configs/c.1/ncm.usb0");
	if (lstat(path, &st)) {
		if (symlink(GDIR "/functions/ncm.usb0", path))
			note("cycle=%d symlink %s rc=-%d", cyc, path, errno);
	}

	snprintf(path, sizeof(path), GDIR "/UDC");
	if (rf(path, cur, sizeof(cur)) < 0)
		cur[0] = '\0';
	same = !strcmp(cur, udc);
	if (!same) {
		rc_bind = wf(path, udc);
		note("cycle=%d gadget bind udc=%s rc=%d", cyc, udc, rc_bind);
	} else {
		note("cycle=%d gadget already bound udc=%s", cyc, udc);
		rc_bind = 0;
	}

	sleep_s(2);

	snprintf(path, sizeof(path), "/sys/class/udc/%s/state", udc);
	rc_state_read = rf(path, cur, sizeof(cur));
	note("cycle=%d udc state=%s rc=%d", cyc,
	     rc_state_read < 0 ? "?" : cur, rc_state_read);

	snprintf(path, sizeof(path), "/sys/class/udc/%s/current_speed", udc);
	rc_state_read = rf(path, cur, sizeof(cur));
	note("cycle=%d udc current_speed=%s rc=%d", cyc,
	     rc_state_read < 0 ? "?" : cur, rc_state_read);

	net_up("usb0", "10.0.0.1", "255.255.255.0");

	return rc_bind ? -2 : 0;
}

/* ------------------------------------------------------------------ modules */
/*
 * MODULE PLAN (v6, measured on the device):
 *
 * On hardware the boot chain gives us BOTH ramdisks: ours (boot_b) plus the
 * device's own vendor ramdisk (vendor_boot_b -> vendor_ramdisk00), which is
 * unpacked into the same rootfs. That vendor ramdisk carries 327 flat .ko
 * files in /lib/modules plus the AUTHORITATIVE metadata:
 *     modules.load (98) modules.load.recovery (329) modules.dep (327)
 *     modules.softdep (17) modules.alias modules.blocklist
 * and its softdep file says:
 *     softdep smem      pre: qcom_hwspinlock
 *     softdep dwc3_msm  pre: phy-generic phy-msm-snps-hs phy-msm-ssusb-qmp eud
 * The v1..v5 init loaded a hand-built list of 40 names and never loaded
 * qcom_hwspinlock / phy-generic / eud, so smem.ko and dwc3_msm.ko could not
 * resolve their symbols: no USB and no UFS. This loader instead reads the
 * device's own files and honours both kinds of dependency:
 *   - modules.dep     hard deps  (must be in, they carry the symbols)
 *   - modules.softdep pre:/post: (load before / after)
 * kmodloader is deliberately NOT used: it would drag musl + a second ELF
 * loader back into the ramdisk, which is exactly what v6 removed. The
 * topological walk below is the same algorithm Android's own first-stage
 * loader runs, done with finit_module(2) and init_module(2) only.
 */
#define MAX_MODS    400
#define MAX_DEPS    40
#define MAX_SOFT    12
#define NAME_LEN    48
#define PATH_LEN    320

struct modent {
	char name[NAME_LEN];            /* canonical: no path, no .ko, '-' -> '_' */
	short deps[MAX_DEPS];
	unsigned char ndeps;
	short pre[MAX_SOFT];
	unsigned char npre;
	short post[MAX_SOFT];
	unsigned char npost;
	unsigned char blocked;
	unsigned char state;            /* 0 todo, 1 visiting, 2 emitted */
};

static struct modent mods[MAX_MODS];
static int n_mods;
static short want[MAX_MODS];
static int n_want;
static unsigned long m_ok, m_fail, m_skip, m_missing;
static int dwc3_seen;

/* canonical module name: basename, drop .ko, '-' -> '_', lower case */
static void canon(char *dst, size_t dlen, const char *src)
{
	const char *b = strrchr(src, '/');
	size_t i = 0;

	b = b ? b + 1 : src;
	while (*b && i + 1 < dlen) {
		char c = *b++;

		if (c == '-' || c == '.')
			c = '_';
		dst[i++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
	}
	dst[i] = '\0';
	/* strip a trailing "_ko" that came from a ".ko" suffix */
	i = strlen(dst);
	if (i > 3 && !strcmp(dst + i - 3, "_ko"))
		dst[i - 3] = '\0';
}

static int mod_index(const char *name, int add)
{
	char c[NAME_LEN];
	int i;

	canon(c, sizeof(c), name);
	for (i = 0; i < n_mods; i++)
		if (!strcmp(mods[i].name, c))
			return i;
	if (!add || n_mods >= MAX_MODS)
		return -1;
	i = n_mods++;
	memset(&mods[i], 0, sizeof(mods[i]));
	snprintf(mods[i].name, sizeof(mods[i].name), "%s", c);
	return i;
}

static void mod_want(int idx)
{
	if (idx < 0 || n_want >= MAX_MODS)
		return;
	want[n_want++] = (short)idx;
}

/* real file index: canonical module name -> the .ko that actually exists.
 * Mixed spellings exist in this vendor set (nvmem_qcom-spmi-sdam.ko), so the
 * path is looked up in the real directory listing instead of being guessed. */
static char mpath_name[MAX_MODS][NAME_LEN];
static char mpath_full[MAX_MODS][PATH_LEN];
static int mpath_n;

static void index_dir(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char c[NAME_LEN];
		size_t L = strlen(e->d_name);
		int i, free_slot = -1;

		if (e->d_name[0] == '.' || L < 4 || strcmp(e->d_name + L - 3, ".ko"))
			continue;
		canon(c, sizeof(c), e->d_name);
		for (i = 0; i < mpath_n; i++) {
			if (!strcmp(mpath_name[i], c))
				break;
		}
		if (i < mpath_n)
			free_slot = i;         /* flat vendor copy wins over ours */
		else if (mpath_n < MAX_MODS)
			free_slot = mpath_n++;
		if (free_slot < 0)
			continue;
		snprintf(mpath_name[free_slot], NAME_LEN, "%s", c);
		snprintf(mpath_full[free_slot], PATH_LEN, "%s/%s", dir, e->d_name);
	}
	closedir(d);
}

static void scan_module_files(void)
{
	struct utsname u;
	char v[PATH_LEN];

	mpath_n = 0;
	index_dir("/lib/modules");                       /* device: flat layout */
	if (!uname(&u)) {
		snprintf(v, sizeof(v), "/lib/modules/%s", u.release);
		index_dir(v);                            /* ours: versioned dir */
	}
	note("modload indexed %d .ko files on disk", mpath_n);
}

static const char *indexed_path(const char *name)
{
	char c[NAME_LEN];
	int i;

	canon(c, sizeof(c), name);
	for (i = 0; i < mpath_n; i++)
		if (!strcmp(mpath_name[i], c) && !access(mpath_full[i], R_OK))
			return mpath_full[i];
	return NULL;
}

/* find the .ko for a module: device flat layout first, then ours */
static int mod_path(const char *name, char *out, size_t olen)
{
	static const char *dirs[] = { "/lib/modules/", NULL };
	struct utsname u;
	char v[PATH_LEN];
	const char *cands[3];
	char hyp[NAME_LEN], und[NAME_LEN];
	const char *hit;
	int di, ci;

	hit = indexed_path(name);
	if (hit) {
		snprintf(out, olen, "%s", hit);
		return 0;
	}

	if (!uname(&u)) {
		snprintf(v, sizeof(v), "/lib/modules/%s/", u.release);
		dirs[1] = v;
	}
	/* the .ko on disk may use hyphens or underscores */
	snprintf(hyp, sizeof(hyp), "%s", name);
	for (ci = 0; hyp[ci]; ci++)
		if (hyp[ci] == '_')
			hyp[ci] = '-';
	snprintf(und, sizeof(und), "%s", name);
	for (ci = 0; und[ci]; ci++)
		if (und[ci] == '-')
			und[ci] = '_';
	cands[0] = name;
	cands[1] = hyp;
	cands[2] = und;

	for (di = 0; di < 2; di++) {
		if (!dirs[di])
			continue;
		for (ci = 0; ci < 3; ci++) {
			snprintf(out, olen, "%s%s.ko", dirs[di], cands[ci]);
			if (!access(out, R_OK))
				return 0;
		}
	}
	out[0] = '\0';
	return -1;
}

static const char *errtext(int e)
{
	switch (e) {
	case 1:  return "EPERM";
	case 2:  return "ENOENT file/symbol missing";
	case 6:  return "ENXIO";
	case 8:  return "ENOEXEC bad format";
	case 11: return "EAGAIN";
	case 12: return "ENOMEM";
	case 16: return "EBUSY";
	case 17: return "EEXIST already loaded";
	case 19: return "ENODEV";
	case 22: return "EINVAL bad params/unknown symbol";
	case 28: return "ENOSPC";
	case 30: return "EROFS";
	case 38: return "ENOSYS no finit_module, tried init_module";
	default: return "errno";
	}
}

static int already_loaded(const char *name)
{
	char p[PATH_LEN], c[NAME_LEN];

	canon(c, sizeof(c), name);
	snprintf(p, sizeof(p), "/sys/module/%s", c);
	return access(p, F_OK) == 0;
}

/* load one module; never fatal, every outcome is journalled */
static int mod_load(const char *name)
{
	char path[PATH_LEN];
	int fd, rc;
	void *buf = NULL;

	if (already_loaded(name)) {
		m_skip++;
		note("modload %s skipped already-loaded", name);
		return 1;
	}
	if (mod_path(name, path, sizeof(path))) {
		m_missing++;
		note("modload %s rc=-2 no .ko in /lib/modules", name);
		return -1;
	}
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		m_missing++;
		note("modload %s open %s rc=-%d", name, path, errno);
		return -1;
	}
	rc = (int)syscall(SYS_finit_module, fd, "", 0);
	if (rc && errno == ENOSYS) {
		/* ancient/patched kernel: fall back to init_module(2) */
		struct stat st;
		ssize_t got;

		if (!fstat(fd, &st) && st.st_size > 0 && st.st_size < 16 * 1024 * 1024) {
			buf = malloc((size_t)st.st_size);
			if (buf) {
				lseek(fd, 0, SEEK_SET);
				got = read(fd, buf, (size_t)st.st_size);
				if (got == st.st_size)
					rc = (int)syscall(SYS_init_module, buf,
							  (unsigned long)got, "");
				free(buf);
			}
		}
	}
	if (rc) {
		m_fail++;
		note("modload %s rc=-%d (%s)", name, errno, errtext(errno));
		close(fd);
		return -1;
	}
	m_ok++;
	note("modload %s rc=0 %s", name, path);
	close(fd);
	return 0;
}

static void emit(int idx, int depth);

static void emit_ref(short idx, int depth)
{
	if (idx >= 0)
		emit(idx, depth + 1);
}

/* depth-first: deps, then softdep pre:, then the module, then post: */
static void emit(int idx, int depth)
{
	int i;

	if (idx < 0 || idx >= n_mods || depth > 12)
		return;
	if (mods[idx].state == 2)
		return;
	if (mods[idx].state == 1) {
		note("modload %s: dependency cycle, not recursing", mods[idx].name);
		return;
	}
	mods[idx].state = 1;
	if (mods[idx].blocked)
		note("modload %s is in modules.blocklist (loading anyway)", mods[idx].name);
	for (i = 0; i < mods[idx].ndeps; i++)
		emit_ref(mods[idx].deps[i], depth);
	for (i = 0; i < mods[idx].npre; i++)
		emit_ref(mods[idx].pre[i], depth);
	if (!strcmp(mods[idx].name, "dwc3_msm"))
		dwc3_seen = 1;
	mod_load(mods[idx].name);
	mods[idx].state = 2;
	/*
	 * As soon as dwc3_msm is in, the UDC either exists or never will: try
	 * the gadget NOW instead of waiting for the end of the chain.
	 */
	if (dwc3_seen && !strcmp(mods[idx].name, "dwc3_msm")) {
		note("dwc3_msm loaded -> first gadget attempt");
		gadget_cycle(900, 5);
	}
	for (i = 0; i < mods[idx].npost; i++)
		emit_ref(mods[idx].post[i], depth);
}

/* modules.dep: "/lib/modules/x.ko: /lib/modules/y.ko ..." */
static void parse_dep_file(const char *path)
{
	static char buf[65536];
	char *p;
	int rc = rfall(path, buf, sizeof(buf));

	if (rc < 0) {
		note("modload modules.dep unreadable rc=%d", rc);
		return;
	}
	p = buf;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *colon = strchr(p, ':');
		int idx;

		if (nl)
			*nl = '\0';
		if (colon) {
			*colon = '\0';
			idx = mod_index(p, 1);
			if (idx >= 0) {
				char *d = colon + 1;

				while (*d && mods[idx].ndeps < MAX_DEPS) {
					int di;
					char *sp;

					while (*d == ' ' || *d == '	')
						d++;
					if (!*d)
						break;
					sp = strchr(d, ' ');
					if (sp)
						*sp = '\0';
					di = mod_index(d, 1);
					if (di >= 0)
						mods[idx].deps[mods[idx].ndeps++] = (short)di;
					d = sp ? sp + 1 : d + strlen(d);
				}
			}
		}
		p = nl ? nl + 1 : NULL;
	}
}

/* modules.softdep: "softdep <mod> pre: a b" / "... post: c d" */
static void parse_softdep_file(const char *path)
{
	static char buf[16384];
	char *p;
	int rc = rfall(path, buf, sizeof(buf));

	if (rc < 0)
		return;
	p = buf;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *t;

		if (nl)
			*nl = '\0';
		t = p;
		while (*t == ' ' || *t == '	')
			t++;
		if (!strncmp(t, "softdep", 7)) {
			char *save = NULL;
			char *tok = strtok_r(t + 7, " 	", &save);
			int idx = tok ? mod_index(tok, 1) : -1;

			while ((tok = strtok_r(NULL, " 	", &save))) {
				int is_pre = !strcmp(tok, "pre:");
				int is_post = !strcmp(tok, "post:");

				if (!is_pre && !is_post)
					continue;
				while ((tok = strtok_r(NULL, " 	", &save))) {
					int ti;

					if (!strcmp(tok, "pre:") ||
					    !strcmp(tok, "post:"))
						break;
					ti = mod_index(tok, 1);
					if (ti < 0 || idx < 0)
						continue;
					if (is_pre && mods[idx].npre < MAX_SOFT)
						mods[idx].pre[mods[idx].npre++] = (short)ti;
					if (is_post && mods[idx].npost < MAX_SOFT)
						mods[idx].post[mods[idx].npost++] = (short)ti;
				}
			}
		}
		p = nl ? nl + 1 : NULL;
	}
}

static void parse_blocklist(const char *path)
{
	static char buf[8192];
	char *p;
	int rc = rfall(path, buf, sizeof(buf));

	if (rc < 0)
		return;
	p = buf;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *t = p;
		int idx;

		if (nl)
			*nl = '\0';
		while (*t == ' ' || *t == '	')
			t++;
		if (!strncmp(t, "blocklist ", 10) || !strncmp(t, "blacklist ", 10)) {
			idx = mod_index(t + 10, 1);
			if (idx >= 0)
				mods[idx].blocked = 1;
		}
		p = nl ? nl + 1 : NULL;
	}
}

/* want list from a load file, one module name per line */
static int parse_want_file(const char *path)
{
	static char buf[32768];
	char *p;
	int rc = rfall(path, buf, sizeof(buf));
	int n = 0;

	if (rc < 0)
		return -1;
	p = buf;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *t = p;
		int idx;

		if (nl)
			*nl = '\0';
		while (*t == ' ' || *t == '	')
			t++;
		if (*t && *t != '#') {
			idx = mod_index(t, 1);
			if (idx >= 0) {
				mod_want(idx);
				n++;
			}
		}
		p = nl ? nl + 1 : NULL;
	}
	return n;
}

/* the modules that make the two success signals possible at all */
static const char *GOALS[] = {
	"qcom_hwspinlock.ko", "smem.ko", "qcom_ipc_logging.ko",
	"minidump.ko", "qcom_glink.ko", "qcom_glink_smem.ko", "qcom_smd.ko",
	"rproc_qcom_common.ko", "qmi_helpers.ko", "pdr_interface.ko",
	"pmic_glink.ko", "ucsi_glink.ko", "debug-regulator.ko",
	"proxy-consumer.ko", "gdsc-regulator.ko", "clk-qcom.ko",
	"ssusb-redriver-nb7vpq904m.ko", "phy-generic.ko", "eud.ko",
	"phy-msm-snps-hs.ko", "phy-msm-ssusb-qmp.ko", "dwc3-msm.ko",
	"phy-qcom-ufs.ko", "phy-qcom-ufs-qmp-v4-waipio.ko", "ufs-qcom.ko",
	"ufshcd-crypto-qti.ko", "crypto-qti-common.ko", "crypto-qti-hwkm.ko",
};

/*
 * Primary path: the device's own metadata is present (it always is on
 * hardware, because ABL/unpacking hands us vendor_ramdisk00 as well).
 */
static int load_chain_device(void)
{
	int i, n_load, n_rec;

	if (access("/lib/modules/modules.dep", R_OK)) {
		note("modload: no /lib/modules/modules.dep");
		return -1;
	}
	scan_module_files();
	parse_dep_file("/lib/modules/modules.dep");
	parse_softdep_file("/lib/modules/modules.softdep");
	parse_blocklist("/lib/modules/modules.blocklist");
	n_load = parse_want_file("/lib/modules/modules.load");
	n_rec = parse_want_file("/lib/modules/modules.load.recovery");
	note("modload source=device modules.load=%d recovery=%d parsed deps=%d "
	     "softdep pre=%d post=%d", n_load, n_rec, n_mods,
	     (int)mods[mod_index("dwc3_msm", 1)].npre,
	     (int)mods[mod_index("dwc3_msm", 1)].npost);
	/* the goal chain is appended explicitly: nothing above guarantees it */
	for (i = 0; i < (int)(sizeof(GOALS) / sizeof(GOALS[0])); i++)
		mod_want(mod_index(GOALS[i], 1));
	note("modload want=%d entries (device lists + %d goal names)",
	     n_want, (int)(sizeof(GOALS) / sizeof(GOALS[0])));
	for (i = 0; i < n_want; i++)
		emit(want[i], 0);
	return 0;
}

/*
 * Fallback path: no device metadata (exactly the QEMU situation). Our own
 * ramdisk carries /lib/modules/fallback.order, already sorted respecting
 * deps and softdeps at build time, plus the .ko files themselves.
 */
static int load_chain_fallback(void)
{
	char order[64];
	char *p;
	static char buf[16384];
	int rc;
	char c[NAME_LEN];

	snprintf(order, sizeof(order), "/lib/modules/fallback.order");
	rc = rfall(order, buf, sizeof(buf));
	scan_module_files();
	if (rc < 0) {
		note("modload: no %s either (nothing to load, not fatal)", order);
		return -1;
	}
	p = buf;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *t = p;

		if (nl)
			*nl = '\0';
		while (*t == ' ' || *t == '	')
			t++;
		if (*t && *t != '#') {
			canon(c, sizeof(c), t);
			if (!strcmp(c, "dwc3_msm"))
				dwc3_seen = 1;
			mod_load(c);
			if (dwc3_seen && !strcmp(c, "dwc3_msm")) {
				/* same rule as the primary path: the gadget is only
				 * attempted once dwc3_msm and its softdeps are in */
				note("dwc3_msm in -> gadget attempt now (fallback path)");
				gadget_cycle(901, 5);
			}
		}
		p = nl ? nl + 1 : NULL;
	}
	note("modload source=fallback order=%s ok=%lu fail=%lu skip=%lu "
	     "missing=%lu", order, m_ok, m_fail, m_skip, m_missing);
	return m_ok;
}

static void load_modules(void)
{
	if (load_chain_device() < 0)
		load_chain_fallback();
	note("modload summary loaded=%lu skipped=%lu failed=%lu missing=%lu "
	     "dwc3_seen=%d", m_ok, m_skip, m_fail, m_missing, dwc3_seen);
}

/* ------------------------------------------------------------------ rawdump */
/*
 * rawdump is identified by size alone (524288 sectors of 512 B = 256 MiB,
 * unique in this GPT); there is no ueventd, so no by-name symlink. Without
 * ufs_qcom (a module) no block device exists at all and this logs "none".
 */
#define RD_SECTORS "524288"
static int rd_major = -1, rd_minor = -1;

static int find_rawdump(void)
{
	DIR *d = opendir("/sys/class/block");
	struct dirent *e;
	int found = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char p[300], sz[64], dev[64];
		unsigned maj, min;

		if (e->d_name[0] == '.')
			continue;
		snprintf(p, sizeof(p), "/sys/class/block/%s/partition", e->d_name);
		if (access(p, F_OK))
			continue;
		snprintf(p, sizeof(p), "/sys/class/block/%s/size", e->d_name);
		if (rf(p, sz, sizeof(sz)) < 0)
			continue;
		if (strcmp(sz, RD_SECTORS))
			continue;
		snprintf(p, sizeof(p), "/sys/class/block/%s/dev", e->d_name);
		if (rf(p, dev, sizeof(dev)) < 0)
			continue;
		if (sscanf(dev, "%u:%u", &maj, &min) != 2)
			continue;
		note("rawdump device=%s major:minor=%u:%u", e->d_name, maj, min);
		unlink("/dev/rd");
		if (mknod("/dev/rd", S_IFBLK | 0600, makedev(maj, min)))
			note("rawdump mknod rc=-%d", errno);
		rd_major = (int)maj;
		rd_minor = (int)min;
		found = 1;
		break;
	}
	closedir(d);
	return found;
}

/* dump the whole ring to rawdump, oldest first, 4 KiB at a time */
static void journal_flush(void)
{
	int fd;
	size_t off;

	if (jlen == jflushed)          /* nothing new */
		return;
	if (rd_major < 0 && !find_rawdump())
		return;
	fd = open("/dev/rd", O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		note("journal open /dev/rd rc=-%d", errno);
		return;
	}
	for (off = 0; off < jlen; off += 4096) {
		char buf[4096];
		size_t n = jlen - off;

		if (n > sizeof(buf))
			n = sizeof(buf);
		memset(buf, 0, sizeof(buf));
		memcpy(buf, jring + off, n);
		if (lseek(fd, (off_t)off, SEEK_SET) < 0) {
			note("journal lseek off=%zu rc=-%d", off, errno);
			break;
		}
		if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
			note("journal write off=%zu rc=-%d", off, errno);
			break;
		}
	}
	close(fd);
	jflushed = jlen;
	note("journal dumped offset 0..%zu to rawdump", jlen);
}

/* ------------------------------------------------------------------ mounts */
static void do_mount(const char *src, const char *tgt, const char *type)
{
	int rc;

	mkdir_p(tgt);
	rc = mount(src, tgt, type, 0, NULL);
	/* EBUSY means it is already mounted: that is a success for us */
	if (rc && errno != EBUSY)
		note("mount %s on %s rc=-%d", type, tgt, errno);
}

static void uptime_str(char *out, size_t len)
{
	char buf[64];

	if (rf("/proc/uptime", buf, sizeof(buf)) < 0) {
		snprintf(out, len, "?");
		return;
	}
	snprintf(out, len, "%s", buf);
}

/* ------------------------------------------------------------------ pmsg */
/*
 * /dev/pmsg0 is NOT a fixed major. fs/pstore/pmsg.c does
 * register_chrdev(0, "pmsg", ...): the kernel hands out a dynamic major (252
 * on the live phone, something else on another boot) and lists it in
 * /proc/devices under the name "pmsg". v5 hardcoded 252:0, which can silently
 * open a *different* driver and lose the only breadcrumb channel that
 * survives a reset. So: resolve the real major, re-make the node, open that.
 * If the kernel does not list a pmsg major (no ramoops this boot) the channel
 * is declared unavailable instead of guessed.
 */
static int pmsg_major(void)
{
	static char buf[8192];
	char *p;
	int rc = rfall("/proc/devices", buf, sizeof(buf));

	if (rc < 0)
		return -1;
	p = buf;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *name = strchr(p, ' ');

		if (nl)
			*nl = '\0';
		if (name) {
			while (*name == ' ')
				name++;
			if (!strcmp(name, "pmsg")) {
				int maj = atoi(p);

				if (maj > 0)
					return maj;
			}
		}
		p = nl ? nl + 1 : NULL;
	}
	return -1;
}

static void open_pmsg(void)
{
	int maj = pmsg_major();

	if (maj < 0) {
		note("pmsg0 unavailable (no pmsg major in /proc/devices: no ramoops this boot)");
		return;
	}
	unlink("/dev/pmsg0");
	if (mknod("/dev/pmsg0", S_IFCHR | 0600, makedev(maj, 0)))
		note("pmsg0 mknod %d:0 rc=-%d", maj, errno);
	fd_pmsg = open("/dev/pmsg0", O_WRONLY | O_CLOEXEC);
	rc_pmsg = fd_pmsg < 0 ? -errno : 0;
	note("pmsg0 major=%d source=/proc/devices open rc=%d", maj, rc_pmsg);
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv)
{
	char buf[512], up[64];
	int cyc = 0, wait_first = 30, i;

	/*
	 * Test-only knobs, inert for the real thing: the kernel starts PID 1
	 * with no useful argv, and these are only honoured when we are NOT PID 1
	 * (i.e. when a user-mode emulator runs this binary). Shipped behaviour
	 * can therefore not be changed from the command line.
	 */
	for (i = 1; i < argc && getpid() != 1; i++) {
		if (!strncmp(argv[i], "--wait=", 7))
			wait_first = atoi(argv[i] + 7);
	}

	/* stage 0: channels first, so everything after is visible somewhere.
	 * /dev/console is opened O_NONBLOCK on purpose: a serial console with
	 * nobody draining its TX FIFO must never be able to block PID 1. */
	fd_kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
	rc_kmsg = fd_kmsg < 0 ? -errno : 0;
	fd_console = open("/dev/console", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
	rc_console = fd_console < 0 ? -errno : 0;
	/* /dev/pmsg0 is opened later: its major has to be read from /proc/devices
	 * first, and /proc is only mounted in stage 1 */

	/* If not a single channel node could be opened, fall back to fd 1 (which
	 * the kernel points at /dev/console for PID 1 anyway) so that a smoke
	 * test in a user-mode emulator is not blind. */
	if (fd_kmsg < 0 && fd_console < 0) {
		fd_kmsg = fd_console = 1;
		logmsg("channels: neither /dev/kmsg nor /dev/console opened, using fd 1");
	}

	/*
	 * Emitted only now, i.e. after the fallback, so that this first line is
	 * guaranteed to land somewhere: it is the proof that /init ran at all.
	 */
	logmsg("init v6 entered pid=%d channels kmsg=%d console=%d",
	       (int)getpid(), rc_kmsg, rc_console);

	/* stage 1: pseudo filesystems (configfs is what the gadget needs) */
	do_mount("proc", "/proc", "proc");
	do_mount("sysfs", "/sys", "sysfs");
	do_mount("configfs", "/sys/kernel/config", "configfs");
	do_mount("pstore", "/sys/fs/pstore", "pstore");

	/*
	 * The pmsg breadcrumb channel can only be resolved once /proc is up, so
	 * it comes right after the mounts and before the gadget attempt.
	 */
	open_pmsg();

	{
		struct utsname u;

		if (!uname(&u))
			note("kernel=%s %s", u.release, u.machine);
	}
	if (rf("/proc/cmdline", buf, sizeof(buf)) >= 0)
		note("cmdline=%s", buf);
	{
		int rc_conf = access("/sys/kernel/config/usb_gadget", F_OK);

		note("configfs usb_gadget dir present=%d rc=%d",
		     rc_conf ? 0 : 1, rc_conf);
	}

	/*
	 * stage 2: THE MODULE CHAIN, FIRST. Nothing else is done before it, and
	 * the gadget is attempted the moment dwc3_msm (with its softdeps) is
	 * in - see emit(). Reading the device's own modules.load / modules.dep
	 * / modules.softdep instead of a hand-built list is the whole point of
	 * v6: the v5 list was missing the softdep providers, so dwc3_msm.ko
	 * and ufs_qcom.ko could never resolve their symbols.
	 */
	note("stage2 module chain (device metadata if present, else fallback list)");
	load_modules();
	journal_flush();

	/*
	 * stage 3: one long first gadget attempt. If dwc3_msm just loaded, the
	 * UDC appears within a moment; if it was already in, this is the only
	 * chance to bind before the retry loop starts.
	 */
	note("stage3 gadget attempt (wait=%ds for a real UDC)", wait_first);
	{
		int r = gadget_cycle(0, wait_first);

		note("stage3 result=%d", r);
	}
	journal_flush();

	/* stage 4: rawdump journal, if any block device exists at all */
	if (!find_rawdump())
		note("rawdump none (no block device yet: is ufs_qcom in?)");
	journal_flush();

	/*
	 * stage 5: hold PID 1 forever. There is no exit() in this program and
	 * main never returns: a bare-metal "no USB" can therefore never be
	 * explained by "init exited and the kernel panicked".
	 */
	for (;;) {
		cyc++;
		uptime_str(up, sizeof(up));
		note("cycle=%d uptime=%s logs=%lu", cyc, up, n_log);
		poke_role();
		gadget_cycle(cyc, 5);
		journal_flush();
		sleep_s(10);
	}

	/* unreachable; here so that "main returned" is not even expressible */
	for (;;)
		pause();
	return 0;
}
