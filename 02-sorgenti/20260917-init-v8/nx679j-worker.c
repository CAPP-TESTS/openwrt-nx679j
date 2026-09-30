/*
 * NX679J /nx679j/worker -- the ONLY program that is allowed to do risky work.
 *
 * Every mode of this program is started by /init (PID 1) as a CHILD, with a
 * deadline enforced by PID 1.  Nothing in here may ever run inside PID 1: a
 * finit_module(2) that enters a driver probe waiting for a clock, a regulator,
 * a PHY, an extcon/role event or a glink/qmi handshake that is not coming will
 * never return, and on this kernel (CONFIG_PANIC_TIMEOUT=-1) a task stuck in a
 * syscall is indistinguishable from a panic: no screen change, no USB, no
 * journal, no reboot.
 *
 * Mode:  worker mounts            mount proc/sys/configfs/pstore, read the
 *                                 device facts, list /lib/modules
 *        worker chain             load modules.load (the set the working
 *                                 system itself loads) + the extra closure
 *                                 needed for the USB gadget.  EVERY single
 *                                 finit_module(2) runs in its own GRANDCHILD
 *                                 with its own explicit timeout: on expiry the
 *                                 grandchild is killed and the module is
 *                                 journalled as TIMEOUT-KILLED, then the walk
 *                                 continues -- the driver is lost, the phone
 *                                 is not.
 *        worker ufs               wait for the UFS host node, find the rawdump
 *                                 block device, mknod it, write the journal
 *        worker gadget            configfs NCM gadget + bind the real UDC +
 *                                 usb0 10.0.0.1/24 (the goal)
 *        worker maint             periodic re-assertion in success mode
 *        worker dump <why>        push the journal to every channel that can
 *                                 survive a reset (dev/kmsg at KERN_EMERG,
 *                                 ramoops pmsg, the rawdump partition)
 *
 * The journal is a FILE appended to incrementally, one line per event, BEFORE
 * and AFTER every risky syscall ("ATTEMPT ..." / result).  If this process (or
 * a grandchild) is left stuck in a driver probe for ever, the last line in the
 * journal names the exact module and the exact syscall, and PID 1 records
 * TIMEOUT-KILLED next to it.  RAM-only, so it is pushed out to the persistent
 * channels after every module.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>

#define TAG        "nx679j"
#define JOURNAL    "/nx679j-journal"
#define MODDIR     "/lib/modules"

/* Test-only indirection: the shipped binary uses the real paths, because the
 * environment is empty in PID 1's child.  It exists so the chain logic can be
 * exercised locally (qemu-aarch64 user mode) with a fake module directory and
 * a writable journal, without touching the device. */
static const char *journal_path(void)
{
	const char *e = getenv("V8_JOURNAL");

	return (e && *e) ? e : JOURNAL;
}

static const char *moddir_path(void)
{
	const char *e = getenv("V8_MODDIR");

	return (e && *e) ? e : MODDIR;
}
#define MAX_MODS   420
#define MAX_DEPS   40
#define NAME_LEN   48
#define PATH_LEN   320
#define MOD_TIMEOUT_MS 15000     /* one module may not exceed this, ever     */
#define CHAIN_BUDGET_MS 300000   /* and the whole walk may not exceed this   */
#define RD_SECTORS "524288"      /* 256 MiB, unique in this GPT              */
#define UFS_NODE   "/sys/devices/platform/soc/1d84000.ufshc"

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

/* ------------------------------------------------------------------ journal */
static int fd_j = -1;
static char jtail[8192];        /* last lines, for the dump               */
static unsigned jtail_len;
static int rd_major = -1, rd_minor = -1, fd_kmsg = -1, fd_pmsg = -1;

static void jraw(const char *s, size_t n)
{
	if (fd_j >= 0) {
		size_t off = 0;

		while (off < n) {
			ssize_t w = write(fd_j, s + off, n - off);

			if (w <= 0)
				break;
			off += (size_t)w;
		}
	}
	if (n > sizeof(jtail)) {
		s += n - sizeof(jtail);
		n = sizeof(jtail);
	}
	if (jtail_len + n > sizeof(jtail)) {
		memmove(jtail, jtail + (jtail_len + n - sizeof(jtail)),
			sizeof(jtail) - n);
		jtail_len = sizeof(jtail) - n;
	}
	memcpy(jtail + jtail_len, s, n);
	jtail_len += (unsigned)n;
}

static void jnote(const char *fmt, ...)
{
	char b[512];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b) - 2, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n > (int)sizeof(b) - 2)
		n = (int)sizeof(b) - 2;
	b[n++] = '\n';
	jraw(b, (size_t)n);
}

static void journal_open(void)
{
	if (fd_j >= 0)
		return;
	fd_j = open(journal_path(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd_j < 0 && errno == ENOENT && !strcmp(journal_path(), JOURNAL)) {
		mkdir("/nx679j", 0755);
		fd_j = open(journal_path(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	}
}

/* ---------------------------------------------------------------- plumbing */
static int wf(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t w;
	size_t len = strlen(val);

	if (fd < 0)
		return -errno;
	w = write(fd, val, len);
	close(fd);
	return w == (ssize_t)len ? 0 : -EIO;
}

static int rf(const char *path, char *out, size_t len)
{
	int fd, n;

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
	{
		char *nl = strchr(out, '\n');

		if (nl)
			*nl = '\0';
	}
	return n;
}

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

/* ------------------------------------------------------------- dump of the journal
 * Every channel here can block for ever on this hardware (a dead UFS link turns
 * one write() into an unbounded wait), which is exactly why only a child does it.
 */
static int open_kmsg(void)
{
	if (access("/dev/kmsg", F_OK))
		(void)mknod("/dev/kmsg", S_IFCHR | 0644, makedev(1, 11));
	return open("/dev/kmsg", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
}

static int pmsg_major(void)
{
	static char buf[8192];
	char *p = buf;

	if (rfall("/proc/devices", buf, sizeof(buf)) < 0)
		return -1;
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

static int open_pmsg(void)
{
	int maj = pmsg_major();

	if (maj < 0)
		return -1;
	if (access("/dev/pmsg0", F_OK))
		(void)mknod("/dev/pmsg0", S_IFCHR | 0600, makedev(maj, 0));
	return open("/dev/pmsg0", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
}

static int find_rawdump(void)
{
	DIR *d;
	struct dirent *e;
	/* Fast path first: UFS is built into this kernel (CONFIG_SCSI_UFSHCD=y),
	 * so /dev/block/by-name may already be usable before any module is
	 * loaded -- which is exactly the point: the journal must be writable
	 * without waiting for ufs_qcom. */
	static const char *byname[] = {
		"/dev/block/by-name/rawdump",
		"/dev/block/by-name/raw_dump",
		"/dev/block/by-name/RAWDUMP",
	};
	int i;

	for (i = 0; i < (int)(sizeof(byname) / sizeof(byname[0])); i++) {
		struct stat st;

		if (!stat(byname[i], &st)) {
			unsigned maj = (unsigned)major(st.st_rdev);
			unsigned min = (unsigned)minor(st.st_rdev);

			jnote("rawdump: %s is %u:%u (by-name, no module needed)",
			      byname[i], maj, min);
			(void)unlink("/dev/rd");
			if (mknod("/dev/rd", S_IFBLK | 0600, st.st_rdev))
				jnote("rawdump: mknod rc=-%d", errno);
			rd_major = (int)maj;
			rd_minor = (int)min;
			return 1;
		}
	}

	d = opendir("/sys/class/block");
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
		jnote("rawdump: partition %s major:minor=%u:%u", e->d_name, maj, min);
		(void)unlink("/dev/rd");
		if (mknod("/dev/rd", S_IFBLK | 0600, makedev(maj, min)))
			jnote("rawdump: mknod rc=-%d", errno);
		rd_major = (int)maj;
		rd_minor = (int)min;
		closedir(d);
		return 1;
	}
	closedir(d);
	return 0;
}

static void dump_now(const char *why)
{
	static const char pf[3] = { '<', '1', '>' };
	int n = 0;

	jnote("%s: ---- dump (%s) ----", TAG, why);
	if (fd_kmsg < 0)
		fd_kmsg = open_kmsg();
	if (fd_pmsg < 0)
		fd_pmsg = open_pmsg();
	if (fd_kmsg < 0)
		jnote("%s: kmsg unavailable", TAG);
	else
		n++;
	if (fd_pmsg < 0)
		jnote("%s: pmsg unavailable (no ramoops this boot)", TAG);
	else
		n++;
	if (rd_major < 0)
		(void)find_rawdump();
	if (!n && rd_major < 0)      /* fd 1 still gets the mirror below */
		jnote("%s: no kmsg/pmsg/rawdump channel this boot", TAG);
	{
		/* The ONLY channel that always exists is fd 1: the kernel hands
		 * /dev/console to the init process and this child inherits it.
		 * Everything else (kmsg, pmsg, rawdump) is best effort on top.
		 * The whole journal FILE is mirrored, not this process's RAM
		 * tail: the per-module children are separate processes, so their
		 * "ATTEMPT finit_module" / "MOD ..." / "TIMEOUT-KILLED" lines
		 * exist only in the file. */
		static char kb[262144 + 128];
		static char hd[192];
		int jf, h;
		unsigned blen = 0;

		jf = open(journal_path(), O_RDONLY | O_CLOEXEC);
		if (jf >= 0) {
			ssize_t got = read(jf, kb + 128, sizeof(kb) - 129);

			close(jf);
			if (got > 0)
				blen = (unsigned)got;
		}
		if (!blen) {
			unsigned l = jtail_len > 4096 ? 4096 : jtail_len;

			memcpy(kb + 128, jtail + (jtail_len - l), l);
			blen = l;
		}
		h = snprintf(hd, sizeof(hd),
			     "<1>%s: journal mirror: %u bytes (file %s, kmsg=%d "
			     "pmsg=%d rd=%d)\n", TAG, blen,
			     fd_j >= 0 ? "open" : "closed", fd_kmsg, fd_pmsg,
			     rd_major);
		if (h < 0 || h > (int)sizeof(hd))
			h = 0;
		(void)!write(1, hd, (size_t)h);          /* /dev/console */
		/* the CONTENT must go to fd 1 too: that is the channel that is
		 * observed to print.  A multi-line write to /dev/kmsg did not
		 * reach the console in the QEMU run of 2026-09-17 (only the
		 * header did), so the console copy is not left to kmsg. */
		(void)!write(1, kb + 128, blen);
		if (fd_kmsg >= 0) {
			(void)!write(fd_kmsg, hd, (size_t)h);
			(void)!write(fd_kmsg, kb + 128, blen);
		}
		if (fd_pmsg >= 0)
			(void)!write(fd_pmsg, kb + 128, blen);
		if (rd_major >= 0) {
			int fd = open("/dev/rd", O_WRONLY | O_CLOEXEC);

			if (fd < 0)
				jnote("%s: rawdump open rc=-%d", TAG, errno);
			else {
				unsigned off;

				for (off = 0; off < blen; off += 4096) {
					char b[4096];
					unsigned l = blen - off;

					if (l > sizeof(b))
						l = sizeof(b);
					memset(b, 0, sizeof(b));
					memcpy(b, kb + 128 + off, l);
					if (lseek(fd, (off_t)off, SEEK_SET) < 0)
						break;
					if (write(fd, b, sizeof(b)) !=
					    (ssize_t)sizeof(b))
						break;
				}
				close(fd);
				jnote("%s: rawdump: %u bytes at offset 0 of "
				      "/dev/rd", TAG, blen);
			}
		}
	}
}

/* --------------------------------------------------------------- modules */
struct modent {
	char name[NAME_LEN];
	short dep[MAX_DEPS];
	unsigned char ndep;
	unsigned char bootset;
	unsigned char used;
};

static struct modent mods[MAX_MODS];
static int n_mods;
static short chain[MAX_MODS];
static int n_chain;

static void canon(char *dst, size_t dlen, const char *src)
{
	const char *b = strrchr(src, '/');
	size_t i = 0;

	b = b ? b + 1 : src;
	while (*b && i + 1 < dlen) {
		char c = *b++;

		if (c == '-' || c == '.')
			c = '_';
		if (c >= 'A' && c <= 'Z')
			c += 32;
		dst[i++] = c;
	}
	dst[i] = '\0';
	if (i > 3 && !strcmp(dst + i - 3, "_ko"))
		dst[i - 3] = '\0';
}

static int mod_find(const char *name, int add)
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

#define MAX_FILES 640
static char f_name[MAX_FILES][NAME_LEN];
static char f_path[MAX_FILES][PATH_LEN];
static int n_files;

static void index_dir(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char c[NAME_LEN];
		size_t L = strlen(e->d_name);
		int i, slot = -1;

		if (e->d_name[0] == '.' || L < 4 || strcmp(e->d_name + L - 3, ".ko"))
			continue;
		canon(c, sizeof(c), e->d_name);
		for (i = 0; i < n_files; i++)
			if (!strcmp(f_name[i], c)) {
				slot = i;
				break;
			}
		if (slot < 0 && n_files < MAX_FILES)
			slot = n_files++;
		if (slot < 0)
			continue;
		snprintf(f_name[slot], NAME_LEN, "%s", c);
		snprintf(f_path[slot], PATH_LEN, "%s/%s", dir, e->d_name);
	}
	closedir(d);
}

static void index_modules(void)
{
	struct utsname u;
	char v[PATH_LEN];

	n_files = 0;
	/* our own ramdisk copies first, then the vendor ramdisk's flat set */
	if (!uname(&u)) {
		snprintf(v, sizeof(v), "%s/%s", moddir_path(), u.release);
		index_dir(v);
	}
	index_dir(moddir_path());
}

static const char *mod_file(const char *name)
{
	int i;

	for (i = 0; i < n_files; i++)
		if (!strcmp(f_name[i], name) && !access(f_path[i], R_OK))
			return f_path[i];
	return NULL;
}

static void parse_dep(const char *path)
{
	static char buf[262144];
	char *p = buf;

	if (rfall(path, buf, sizeof(buf)) < 0)
		return;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *colon = strchr(p, ':');

		if (nl)
			*nl = '\0';
		if (colon) {
			int idx;

			*colon = '\0';
			idx = mod_find(p, 1);
			if (idx >= 0) {
				char *d = colon + 1;

				while (*d && mods[idx].ndep < MAX_DEPS) {
					char *sp;
					int di;

					while (*d == ' ' || *d == '\t')
						d++;
					if (!*d)
						break;
					sp = strchr(d, ' ');
					if (sp)
						*sp = '\0';
					di = mod_find(d, 1);
					if (di >= 0)
						mods[idx].dep[mods[idx].ndep++] = (short)di;
					d = sp ? sp + 1 : d + strlen(d);
				}
			}
		}
		p = nl ? nl + 1 : NULL;
	}
}

static void parse_softdep(const char *path)
{
	static char buf[16384];
	char *p = buf;

	if (rfall(path, buf, sizeof(buf)) < 0)
		return;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *t = p;

		if (nl)
			*nl = '\0';
		while (*t == ' ' || *t == '\t')
			t++;
		if (!strncmp(t, "softdep", 7)) {
			char *save = NULL;
			char *tok = strtok_r(t + 7, " \t", &save);
			int idx = tok ? mod_find(tok, 1) : -1;
			int is_pre = 0;

			while ((tok = strtok_r(NULL, " \t", &save))) {
				int ti;

				if (!strcmp(tok, "pre:")) {
					is_pre = 1;
					continue;
				}
				if (!strcmp(tok, "post:")) {
					is_pre = 0;
					continue;
				}
				ti = mod_find(tok, 1);
				if (is_pre && idx >= 0 && ti >= 0 && mods[idx].ndep < MAX_DEPS)
					mods[idx].dep[mods[idx].ndep++] = (short)ti;
			}
		}
		p = nl ? nl + 1 : NULL;
	}
}

static void parse_list(const char *path, int as_bootset)
{
	static char buf[131072];
	char *p = buf;

	if (rfall(path, buf, sizeof(buf)) < 0)
		return;
	while (p && *p) {
		char *nl = strchr(p, '\n');
		char *t = p;
		int idx;

		if (nl)
			*nl = '\0';
		while (*t == ' ' || *t == '\t')
			t++;
		if (*t && *t != '#') {
			idx = mod_find(t, 1);
			if (idx >= 0 && as_bootset)
				mods[idx].bootset = 1;
		}
		p = nl ? nl + 1 : NULL;
	}
}

static void dfs(int idx, int depth)
{
	int i;

	if (idx < 0 || idx >= n_mods || depth > 24 || mods[idx].used)
		return;
	mods[idx].used = 1;
	for (i = 0; i < mods[idx].ndep; i++)
		dfs(mods[idx].dep[i], depth + 1);
	if (n_chain < MAX_MODS)
		chain[n_chain++] = (short)idx;
}

static const char *SEED_USB[] = {
	"dwc3_msm", "phy_msm_ssusb_qmp", "phy_msm_snps_hs", "phy_generic",
	"eud", "ssusb_redriver_nb7vpq904m", "ucsi_glink", "altmode_glink",
	"pmic_glink", "pdr_interface", "qmi_helpers", "qcom_glink",
	"qcom_glink_smem", "qcom_smd", "rproc_qcom_common", "fsa4480_i2c",
};

/*
 * ORDER: exactly the device's own modules.load first (the set the WORKING
 * system loads on every boot, in dependency order), then the extra closure the
 * USB gadget needs.  modules.load.recovery's 329 names are never used: 231 of
 * them are recovery-only, and among them are drivers that wait for firmware /
 * a remote processor (cnss2, icnss2, cdsprm, mhi_*) that is not running here.
 */
static void build_chain(void)
{
	int i;
	int have_dep = 0;
	char p[PATH_LEN];

	n_chain = 0;
	snprintf(p, sizeof(p), "%s/modules.dep", moddir_path());
	have_dep = !access(p, R_OK);
	if (have_dep) {
		parse_dep(p);
		snprintf(p, sizeof(p), "%s/modules.softdep", moddir_path());
		parse_softdep(p);
		snprintf(p, sizeof(p), "%s/modules.load", moddir_path());
		parse_list(p, 1);
		jnote("%s: metadata: %d names, boot-set=%d names", TAG, n_mods,
		      (int)0);
		jnote("%s: order = modules.load first, then the gadget closure", TAG);
		for (i = 0; i < n_mods; i++)
			if (mods[i].bootset)
				dfs(i, 0);
	} else {
		/* our own ramdisk only: the pre-sorted fallback list */
		snprintf(p, sizeof(p), "%s/fallback.order", moddir_path());
		if (rfall(p, (void *)0, 0) < 0)
			parse_list(p, 1);
		for (i = 0; i < n_mods; i++)
			if (mods[i].bootset)
				dfs(i, 0);
		if (!n_chain) {
			/* last resort: whatever .ko we shipped */
			jnote("%s: no metadata at all, loading every .ko we ship",
			      TAG);
			for (i = 0; i < n_files; i++)
				chain[n_chain++] = (short)mod_find(f_name[i], 1);
		}
	}
	for (i = 0; i < (int)(sizeof(SEED_USB) / sizeof(SEED_USB[0])); i++)
		dfs(mod_find(SEED_USB[i], 1), 0);
}

/* one finit_module(2) in its own grandchild, with its own hard timeout */
static int load_guarded(struct modent *m)
{
	char p[PATH_LEN];
	const char *path = mod_file(m->name);
	pid_t pid;
	unsigned long long t0;
	int st = 0;

	snprintf(p, sizeof(p), "/sys/module/%s", m->name);
	if (access(p, F_OK) == 0) {
		jnote("%s: MOD %-34s already-loaded", TAG, m->name);
		return 0;
	}
	if (!path) {
		jnote("%s: MOD %-34s MISSING (no .ko in /lib/modules)", TAG, m->name);
		return -1;
	}
	pid = fork();
	if (pid == 0) {
		int fd, rc;

		journal_open();
		jnote("%s: ATTEMPT finit_module %s", TAG, path);
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			jnote("%s: MOD %-34s open rc=-%d", TAG, m->name, errno);
			_exit(3);
		}
		rc = (int)syscall(SYS_finit_module, fd, "", 0);
		if (rc) {
			int e = errno;

			jnote("%s: MOD %-34s finit_module rc=-%d errno=%d (%s)", TAG,
			      m->name, e, e,
			      e == EEXIST ? "already loaded" :
			      e == ENOEXEC ? "bad format" :
			      e == EINVAL ? "unknown symbol / bad parameter" :
			      e == ENODEV ? "no such device" :
			      e == ENOENT ? "dependency not loaded" :
			      e == EPERM ? "signature/permission" : "other");
			close(fd);
			_exit(1);
		}
		jnote("%s: MOD %-34s loaded rc=0", TAG, m->name);
		close(fd);
		_exit(0);
	}
	if (pid < 0) {
		jnote("%s: MOD %-34s fork failed rc=-%d", TAG, m->name, errno);
		return -1;
	}
	t0 = now_ms();
	for (;;) {
		if (waitpid(pid, &st, WNOHANG) == pid) {
			if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
				return 0;
			return -1;
		}
		if (now_ms() - t0 > MOD_TIMEOUT_MS) {
			/* it is stuck inside a driver probe: lose the driver */
			kill(pid, 9);
			jnote("%s: MOD %-34s TIMEOUT-KILLED after %u ms "
			      "(stuck in finit_module, see ATTEMPT line above)",
			      TAG, m->name, MOD_TIMEOUT_MS);
			return -1;
		}
		sleep_ms(50);
	}
}

/* ======================================================================== */
/*  modes                                                                   */
/* ======================================================================== */
static int mode_mounts(void)
{
	struct utsname u;
	char buf[512];
	int i;
	struct {
		const char *src, *tgt, *type;
	} m[] = {
		{ "proc", "/proc", "proc" },
		{ "sysfs", "/sys", "sysfs" },
		{ "configfs", "/sys/kernel/config", "configfs" },
		{ "pstore", "/sys/fs/pstore", "pstore" },
	};

	journal_open();
	jnote("%s: mounts: start (uid=%d pid=%d)", TAG, getuid(), getpid());
	for (i = 0; i < 4; i++) {
		int rc;

		jnote("%s: ATTEMPT mount -t %s %s", TAG, m[i].type, m[i].tgt);
		(void)mkdir_p(m[i].tgt);
		rc = mount(m[i].src, m[i].tgt, m[i].type, 0, NULL);
		jnote("%s: mount %-9s rc=%d%s", TAG, m[i].type, rc,
		      rc ? (errno == EBUSY ? " (busy, fine)" :
			    errno == ENODEV ? " (not configured)" : "") : "");
	}
	if (rf("/proc/cmdline", buf, sizeof(buf)) >= 0)
		jnote("%s: cmdline: %.400s", TAG, buf);
	if (!uname(&u))
		jnote("%s: kernel %s %s", TAG, u.release, u.machine);
	if (rfall("/proc/devices", buf, sizeof(buf)) >= 0)
		jnote("%s: /proc/devices: pmsg=%s", TAG,
		      strstr(buf, "pmsg") ? "yes" : "no");
	jnote("%s: configfs usb_gadget=%s", TAG,
	      access("/sys/kernel/config/usb_gadget", F_OK) ? "absent" : "present");
	index_modules();
	jnote("%s: %d .ko files visible in /lib/modules", TAG, n_files);
	jnote("%s: mounts: done", TAG);
	return 0;
}

static int mode_chain(void)
{
	unsigned long long t0 = now_ms();
	int i, ok = 0, fail = 0, killed = 0, skipped = 0;
	unsigned long long budget_main = CHAIN_BUDGET_MS;

	journal_open();
	jnote("%s: chain: start", TAG);
	index_modules();
	build_chain();
	jnote("%s: chain: %d modules to try, %d .ko visible", TAG, n_chain, n_files);
	(void)budget_main;
	for (i = 0; i < n_chain; i++) {
		struct modent *m = &mods[chain[i]];
		int rc;

		if (now_ms() - t0 > CHAIN_BUDGET_MS) {
			skipped = n_chain - i;
			jnote("%s: chain: BUDGET EXCEEDED (%u ms), skipping %d "
			      "remaining modules", TAG, CHAIN_BUDGET_MS, skipped);
			break;
		}
		/* one module, one child, one deadline; a failure is recorded in
		 * the journal by the child (rc/errno) or by load_guarded
		 * (TIMEOUT-KILLED), and the walk goes on either way */
		if (load_guarded(m) == 0)
			ok++;
		/* push the journal out after EVERY module: if the next one wedges
		 * the syscall, everything before it is already on a channel that
		 * survives a reset */
		if (!(i % 8) || i == n_chain - 1)
			dump_now("chain progress");
	}
	for (i = 0; i < n_chain; i++) {
		char p[PATH_LEN];

		snprintf(p, sizeof(p), "/sys/module/%s", mods[chain[i]].name);
		if (access(p, F_OK) == 0)
			continue;
	}
	/* count what the journal says, it is the honest record */
	{
		char *p = jtail;

		while ((p = strstr(p, " TIMEOUT-KILLED"))) {
			killed++;
			p++;
		}
		fail = n_chain - ok - killed - skipped;
		if (fail < 0)
			fail = 0;
	}
	jnote("%s: chain: done in %llu ms: ok=%d timeout-killed=%d "
	      "failed/missing=%d skipped=%d", TAG, now_ms() - t0, ok, killed,
	      fail, skipped);
	dump_now("end of chain");
	return 0;
}

/*
 * The FIRST journal write of a boot, and the reason it exists: UFS is built
 * into this kernel (CONFIG_SCSI_UFSHCD=y), so the block device is there from
 * the first instant of userspace and the journal can be made durable BEFORE
 * any module is loaded.  The previous images waited for ufs_qcom to load
 * before writing anything, which is why they left no trace when they hung.
 *
 * This mode does not fork, does not load, and does not wait for anything: it
 * looks for the rawdump partition (by-name first, then by size), mknod's it,
 * and dumps.  Exit 0 = the durable channel worked, 4 = it did not (and the
 * journal is then only in the console/kmsg/pmsg).
 */
static int mode_journal(void)
{
	int ok;

	journal_open();
	jnote("%s: FIRST JOURNAL WRITE, no module loaded yet "
	      "(device tree nodes UFS=%s USB=%s)",
	      TAG, access("/sys/devices/platform/soc/1d84000.ufshc", F_OK) ?
	      "absent" : "present",
	      access("/sys/class/udc", F_OK) ? "absent" : "present");
	jnote("%s: /dev/block/by-name present: %s", TAG,
	      access("/dev/block/by-name", F_OK) ? "no" : "yes");
	jnote("%s: /dev present: kmsg=%s pmsg0=%s console=%s", TAG,
	      access("/dev/kmsg", F_OK) ? "no" : "yes",
	      access("/dev/pmsg0", F_OK) ? "no" : "yes",
	      access("/dev/console", F_OK) ? "no" : "yes");
	ok = find_rawdump();
	dump_now(ok ? "first journal write: rawdump channel ready"
		    : "first journal write: NO rawdump partition found");
	jnote("%s: journal is durable on rawdump: %s", TAG, ok ? "yes" : "no");
	return ok ? 0 : 4;
}

static int mode_ufs(void)
{
	int i, have = 0;

	journal_open();
	jnote("%s: ufs: start (%s)", TAG, UFS_NODE);
	for (i = 0; i < 20; i++) {
		if (access(UFS_NODE, F_OK) == 0) {
			have = 1;
			break;
		}
		jnote("%s: ATTEMPT wait for the UFS host node (%d/20)", TAG, i + 1);
		sleep_ms(1000);
	}
	jnote("%s: ufs: host node %s", TAG, have ? "present" : "STILL ABSENT");
	if (!find_rawdump()) {
		char *p = jtail;

		/* list what block devices do exist, for the record */
		jnote("%s: ufs: no %s-sector partition yet; block devices seen:",
		      TAG, RD_SECTORS);
		{
			DIR *d = opendir("/sys/class/block");
			struct dirent *e;
			int n = 0;

			if (d) {
				while ((e = readdir(d)) && n < 20) {
					char q[256], sz[64];

					if (e->d_name[0] == '.')
						continue;
					snprintf(q, sizeof(q),
						 "/sys/class/block/%s/partition", e->d_name);
					if (access(q, F_OK))
						continue;
					snprintf(q, sizeof(q), "/sys/class/block/%s/size",
						 e->d_name);
					if (rf(q, sz, sizeof(sz)) < 0)
						continue;
					jnote("%s:   %s size=%s", TAG, e->d_name, sz);
					n++;
				}
				closedir(d);
			}
		}
		(void)p;
	}
	dump_now("ufs step");
	/* 0 only if we really have the rawdump partition and could mknod it:
	 * PID 1 uses this exit status to pick the heartbeat period (and to
	 * decide whether the failure class is "no UFS"). */
	return (have && rd_major >= 0) ? 0 : 4;
}

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

static void net_up(const char *ifname, const char *ip, const char *mask)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	struct sockaddr_in *sin;

	if (s < 0) {
		jnote("%s: net socket rc=-%d", TAG, errno);
		return;
	}
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	sin->sin_family = AF_INET;
	inet_pton(AF_INET, ip, &sin->sin_addr);
	(void)ioctl(s, SIOCSIFADDR, &ifr);
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	sin = (struct sockaddr_in *)&ifr.ifr_netmask;
	sin->sin_family = AF_INET;
	inet_pton(AF_INET, mask, &sin->sin_addr);
	(void)ioctl(s, SIOCSIFNETMASK, &ifr);
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	if (!ioctl(s, SIOCGIFFLAGS, &ifr)) {
		ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
		(void)ioctl(s, SIOCSIFFLAGS, &ifr);
	}
	close(s);
	jnote("%s: net %s %s/%s up", TAG, ifname, ip, mask);
}

static void poke_role(void)
{
	DIR *d = opendir("/sys/class/usb_role");
	struct dirent *e;
	int n = 0;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char p[256];

		if (e->d_name[0] == '.')
			continue;
		snprintf(p, sizeof(p), "/sys/class/usb_role/%s/role", e->d_name);
		jnote("%s: ATTEMPT usb_role %s -> device", TAG, e->d_name);
		if (wf(p, "device") == 0)
			n++;
	}
	closedir(d);
	jnote("%s: usb_role nodes set to device: %d", TAG, n);
}

#define GDIR "/sys/kernel/config/usb_gadget/g1"

static int gadget_cycle(int wait_s)
{
	char udc[128] = "", cur[128] = "", path[256];
	int i, rc_bind = -1;
	struct stat st;

	for (i = 0; i < wait_s; i++) {
		jnote("%s: ATTEMPT wait for a real UDC (%d/%d)", TAG, i + 1, wait_s);
		if (first_udc(udc, sizeof(udc)))
			break;
		sleep_ms(1000);
	}
	if (!first_udc(udc, sizeof(udc))) {
		jnote("%s: gadget: NO UDC after %d s (dwc3_msm not up?)", TAG, wait_s);
		return -1;
	}
	jnote("%s: gadget: UDC=%s", TAG, udc);
	(void)mkdir_p(GDIR);
	(void)mkdir_p(GDIR "/strings/0x409");
	(void)mkdir_p(GDIR "/configs/c.1");
	(void)mkdir_p(GDIR "/configs/c.1/strings/0x409");
	(void)mkdir_p(GDIR "/functions/ncm.usb0");
	(void)wf(GDIR "/idVendor", "0x18d1");
	(void)wf(GDIR "/idProduct", "0x4ee7");
	(void)wf(GDIR "/bcdUSB", "0x0200");
	(void)wf(GDIR "/bcdDevice", "0x0100");
	(void)wf(GDIR "/strings/0x409/manufacturer", "OpenWrt");
	(void)wf(GDIR "/strings/0x409/product", "NX679J");
	(void)wf(GDIR "/strings/0x409/serialnumber", "nx679j-v8");
	(void)wf(GDIR "/configs/c.1/strings/0x409/configuration", "NCM");
	(void)wf(GDIR "/configs/c.1/MaxPower", "250");
	snprintf(path, sizeof(path), GDIR "/configs/c.1/ncm.usb0");
	if (lstat(path, &st))
		(void)symlink(GDIR "/functions/ncm.usb0", path);

	if (rf(GDIR "/UDC", cur, sizeof(cur)) < 0)
		cur[0] = '\0';
	if (strcmp(cur, udc)) {
		jnote("%s: ATTEMPT bind UDC %s", TAG, udc);
		rc_bind = wf(GDIR "/UDC", udc);
		jnote("%s: gadget: bind %s rc=%d%s", TAG, udc, rc_bind,
		      rc_bind ? strerror(errno) : "");
	} else {
		rc_bind = 0;
	}
	sleep_ms(2000);
	snprintf(path, sizeof(path), "/sys/class/udc/%s/state", udc);
	jnote("%s: gadget: state=%s", TAG,
	      rf(path, cur, sizeof(cur)) >= 0 ? cur : "unreadable");
	snprintf(path, sizeof(path), "/sys/class/udc/%s/current_speed", udc);
	jnote("%s: gadget: current_speed=%s", TAG,
	      rf(path, cur, sizeof(cur)) >= 0 ? cur : "unreadable");
	net_up("usb0", "10.0.0.1", "255.255.255.0");
	return rc_bind ? -1 : 0;
}

static int mode_gadget(void)
{
	int rc;

	journal_open();
	jnote("%s: gadget: start", TAG);
	poke_role();
	rc = gadget_cycle(12);
	jnote("%s: gadget: rc=%d", TAG, rc);
	dump_now("end of gadget step");
	return rc ? 1 : 0;
}

static int mode_maint(void)
{
	journal_open();
	jnote("%s: maint: tick", TAG);
	poke_role();
	(void)gadget_cycle(2);
	dump_now("maint");
	return 0;
}

static int mode_dump(const char *why)
{
	journal_open();
	jnote("%s: %s", TAG, why ? why : "dump requested");
	dump_now(why ? why : "requested");
	return 0;
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "dump";

	if (!strcmp(mode, "mounts"))
		return mode_mounts();
	if (!strcmp(mode, "journal"))
		return mode_journal();
	if (!strcmp(mode, "chain"))
		return mode_chain();
	if (!strcmp(mode, "ufs"))
		return mode_ufs();
	if (!strcmp(mode, "gadget"))
		return mode_gadget();
	if (!strcmp(mode, "maint"))
		return mode_maint();
	if (!strcmp(mode, "dump"))
		return mode_dump(argc > 2 ? argv[2] : NULL);
	journal_open();
	jnote("%s: unknown mode '%s'", TAG, mode);
	return 2;
}
