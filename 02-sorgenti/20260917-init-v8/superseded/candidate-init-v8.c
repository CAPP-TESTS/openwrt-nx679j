/*
 * NX679J slot-B init, v8 -- /init as a STATIC ELF (aarch64), PID 1.
 *
 * WHY v8 EXISTS (all measured, 2026-09-17)
 *   * v7 (probe): our ramdisk, our container, our cmdline, /init = a program
 *     that writes one marker and calls reboot(2) in a loop -> the phone enters
 *     a RESTART CYCLE (~10 s).  Therefore the packaging, the cmdline, the DTB,
 *     the ABL handoff and "the kernel runs our /init as PID 1" are all PROVEN.
 *   * v5 (shell + busybox) and v6 (static ELF + module chain): frozen RedMagic
 *     logo, no USB, rawdump journal all zeros, pstore empty, no panic, A/B
 *     counter untouched.  PID 1 blocked inside a syscall is indistinguishable
 *     from a panic because CONFIG_PANIC_TIMEOUT=-1.
 *
 * THE DEFECT COMMON TO v5 AND v6 (offline analysis, this experiment):
 *   PID 1 ITSELF performs every operation that can block for ever, and the
 *   evidence is only written at the very END of the sequence:
 *     - finit_module(2)/init_module(2) on a driver whose probe() waits for
 *       hardware that is not up: the kernel runs the module's init -- and, for
 *       a platform driver, the driver's probe() -- SYNCHRONOUSLY INSIDE OUR
 *       SYSCALL (module_platform_driver -> __platform_driver_register ->
 *       driver_register -> really_probe -> drv->probe, all in the caller's
 *       context unless the driver opted into asynchronous probing).  If that
 *       probe waits for a clock, a regulator, a PHY, an extcon/role-switch
 *       event, a glink/qmi handshake with an ADSP/PMIC that is not running, or
 *       for the UFS link to come up, PID 1 never returns from the syscall.
 *     - mount(2) of a pseudo-filesystem whose ->get_tree touches hardware
 *     - read(2)/write(2) on /sys, /proc or a block device that does not answer
 *       (a dead UFS link turns a single write() into an unbounded wait)
 *     - and, in v6, a 900-second gadget wait (gadget_cycle(900,5)) placed in
 *       the MIDDLE of the module chain, before the journal is ever flushed:
 *       >=15 minutes in which nothing at all is written anywhere.
 *   In QEMU every one of those is instantaneous and always succeeds: there is
 *   no UFS link to train, no ADSP to handshake with, no PHY to lock, no clock
 *   tree to wait for, and the modules' probes are never even called (the QEMU
 *   device tree has no matching compatibles).  That is why the failure is
 *   hardware-only.
 *
 * v8's INVERSION: PID 1 never performs a risky operation, ever.
 *   PID 1 (the SUPERVISOR) is allowed exactly these syscalls:
 *       nanosleep, clock_gettime, wait4(...WNOHANG), fork, kill(SIGKILL),
 *       mmap of one anonymous shared block, mknod/dup2 for /dev/null at most,
 *       and reboot(2) -- its reporting channel.
 *   Everything else -- mounts, reads, finit_module, block-device I/O, configfs
 *   writes, sockets/ioctls -- happens in a CHILD process that answers for one
 *   step and can hang without taking PID 1 down with it.
 *   PID 1 enforces a per-step deadline itself: it polls with wait4(WNOHANG)
 *   (a BLOCKING waitpid would itself be a way to hang for ever: a child stuck
 *   in an uninterruptible syscall never becomes waitable) and compares
 *   CLOCK_MONOTONIC against the deadline.
 *   Signals cannot help: SIGKILL cannot interrupt a task in D state, so the
 *   only enforcement available is the supervisor's own deadline plus reboot(2).
 *
 * WHAT THE CHILD REPORTS (even when it is stuck for ever)
 *   The child publishes progress into a MAP_SHARED anonymous block before every
 *   risky operation ("phase", monotonic timestamp, the exact next syscall and
 *   its argument).  The supervisor reads that block with plain loads: no
 *   syscall, no lock, no I/O.  So even a child frozen inside a driver probe
 *   tells the supervisor exactly which operation hung.
 *
 * THE REPORTING CHANNEL (the only observation channel is the phone's screen)
 *   A reboot = the RedMagic logo reappearing = "one flash".  The supervisor
 *   walks six steps; the time at which the FIRST flash happens and the period
 *   of the flashes afterwards encode what happened:
 *
 *     no flash ever, logo steady        -> userspace was not reached, or the
 *                                          reboot path itself is unusable.
 *     no flash ever + USB 18d1:4ee7     -> SUCCESS: every step completed, the
 *                                          UDC is bound; the supervisor holds
 *                                          the gadget up (no reboot, so the
 *                                          device stays usable).
 *     flash every ~9 s                  -> all six steps were attempted, no
 *                                          step exceeded its deadline, but the
 *                                          gadget is NOT up.
 *     flash every 12+6*(k-1) s, first
 *     flash at ~(10+15*(k-1)) s         -> step k exceeded its deadline: step k
 *                                          is the one that can hang for ever.
 *                                          k = 1 mounts/reads, 2 boot-set
 *                                          modules, 3 USB chain, 4 UFS chain,
 *                                          5 UFS wait + journal dump, 6 gadget
 *                                          bind.
 *   The technical detail (which module, which rc, the kernel's own view) is
 *   written by CHILD writers to every channel that might survive a reset:
 *   /dev/kmsg at KERN_EMERG (the pstore console backend records it if ramoops
 *   is registered), /dev/pmsg0 (ramoops pmsg, readable at /sys/fs/pstore after
 *   the next boot) and rawdump (the 256 MiB block partition, readable from
 *   Android).  None of those writes is ever done by PID 1.
 *
 * HARD CONSTRAINTS (kernel requirements, not style)
 *   * PID 1 must never die: main() ends in an infinite loop and contains no
 *     return instruction; verified on the shipped disassembly by the build.
 *   * PID 1 must never block: verified on the shipped disassembly, which must
 *     show that main()'s whole call graph -- libc included -- contains no
 *     blocking call (no finit_module, mount, read, write, open of a device,
 *     socket, ioctl, or blocking wait).  See verify-candidate-v8.py.
 *
 * Single-variable experiment: this image is v7's container + v7's ramdisk
 * content, with ONLY /init replaced -- the same content that is measured to
 * reach userspace and self-restart.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

#define TAG "nx679j-v8"

/* ------------------------------------------------------------------ config */
#define RING_SIZE       (64 * 1024)
#define SHM_SIZE        (sizeof(struct shared) + RING_SIZE)
#define DETAIL_SIZE     96
#define MAX_MODS        400
#define MAX_DEPS        40
#define MAX_SOFT        12
#define MAX_BLOCK       400
#define NAME_LEN        48
#define PATH_LEN        320
#define REPORT_BASE_S   12      /* stall report period, step 1                */
#define REPORT_STEP_S   6       /* added per step index (stall = 12+6*(k-1))  */
#define REPORT_NOSTALL_S 9      /* period when no step stalled                */
#define HOLD_TICK_S     5       /* maintenance child period in success mode   */

/* per-step deadlines, milliseconds: 10, 25, 40, 55, 70, 85 s */
#define STEP1_MS  10000
#define STEP2_MS  25000
#define STEP3_MS  40000
#define STEP4_MS  55000
#define STEP5_MS  70000
#define STEP6_MS  85000

struct shared {
	volatile unsigned step;          /* current step, 1..NSTEP; 0 = idle   */
	volatile unsigned phase;         /* 1 started, 2 done, 3 failed, 4 stalled */
	volatile long     rc;            /* last return code / -errno          */
	volatile unsigned long long t_ms;/* monotonic ms of the last update    */
	volatile unsigned len;           /* bytes of log used                  */
	volatile unsigned nstall;        /* number of stalls seen              */
	volatile unsigned stall_step;    /* first step that exceeded its deadline */
	volatile unsigned gadget_up;     /* 1 if the UDC is bound and verified */
	char detail[DETAIL_SIZE];        /* "next thing being attempted"       */
	char logbuf[RING_SIZE];          /* the journal (children append)      */
};

static struct shared *S;             /* PID 1's view, MAP_SHARED           */
static int fd_null = -1;             /* /dev/null, for the children        */

/* ======================================================================== */
/*  journal: plain memory, callable from PID 1 and from children            */
/* ======================================================================== */
static void jput(const char *b, size_t n)
{
	unsigned len = S->len;
	unsigned room;

	if (n > RING_SIZE / 2)
		n = RING_SIZE / 2;
	if (len + n > RING_SIZE) {           /* drop the oldest half, keep going */
		unsigned keep = RING_SIZE / 2;

		memmove(S->logbuf, S->logbuf + (len - keep), keep);
		S->len = keep;
		len = keep;
	}
	room = RING_SIZE - len;
	if (n > room)
		n = room;
	memcpy(S->logbuf + len, b, n);
	S->len = len + (unsigned)n;
}

static void jnote(const char *fmt, ...)
{
	char b[384];
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
	jput(b, (size_t)n);
}

/* progress publication: visible to PID 1 even if the child never comes back */
static void publish(unsigned step, unsigned phase, long rc, const char *fmt, ...)
{
	char b[DETAIL_SIZE];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	S->step = step;
	S->rc = rc;
	S->t_ms = now_ms();
	memcpy(S->detail, b, sizeof(S->detail) - 1);
	S->detail[sizeof(S->detail) - 1] = '\0';
	__sync_synchronize();
	S->phase = phase;                    /* written last: it is the flag */
}

static unsigned long long now_ms(void)
{
	struct timespec ts;

	if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (unsigned long long)ts.tv_sec * 1000ULL
	     + (unsigned long long)(ts.tv_nsec / 1000000L);
}

static void child_sleep_ms(unsigned ms)
{
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000);
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	while (syscall(SYS_nanosleep, &ts, &ts) == -1 && errno == EINTR)
		;
}

/* ======================================================================== */
/*  child-only helpers: files, mounts, modules, devices                     */
/* ======================================================================== */
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

static int mount_pseudo(const char *src, const char *tgt, const char *type,
			int step)
{
	int rc;

	publish(step, 1, 0, "mount -t %s %s", type, tgt);
	(void)mkdir_p(tgt);
	rc = mount(src, tgt, type, 0, NULL);
	if (rc && errno == EBUSY)
		rc = 0;
	jnote("mount %-10s -> %-18s rc=%d%s", type, tgt, rc,
	      rc ? " (continuing)" : "");
	return rc;
}

/* --------------------------------------------------------------- modules */
struct modent {
	char name[NAME_LEN];
	short dep[MAX_DEPS];
	unsigned char ndep;
	unsigned char block;          /* 2,3,4 = which step loads it */
	unsigned char bootset;        /* in the device's own modules.load */
	unsigned char done;
};

static struct modent mods[MAX_MODS];
static int n_mods;

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

/*
 * The real file for a canonical name.  The device ships 327 FLAT .ko files
 * with mixed spellings (nvmem_qcom-spmi-sdam.ko), so the directory is listed
 * instead of guessing the spelling.
 */
#define MAX_FILES 512
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
				slot = i;          /* flat vendor copy wins */
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
	index_dir("/lib/modules");
	if (!uname(&u)) {
		snprintf(v, sizeof(v), "/lib/modules/%s", u.release);
		index_dir(v);
	}
}

static const char *mod_file(const char *canon_name)
{
	int i;

	for (i = 0; i < n_files; i++)
		if (!strcmp(f_name[i], canon_name) && !access(f_path[i], R_OK))
			return f_path[i];
	return NULL;
}

static int already_loaded(const char *canon_name)
{
	char p[PATH_LEN];

	snprintf(p, sizeof(p), "/sys/module/%s", canon_name);
	return access(p, F_OK) == 0;
}

/* one finit_module(2); the whole point of v8 is that PID 1 never runs this */
static int load_one(struct modent *m, int step)
{
	const char *path;
	int fd, rc;

	if (already_loaded(m->name)) {
		jnote("  %-34s already in /sys/module, not reloading", m->name);
		return 0;
	}
	path = mod_file(m->name);
	if (!path) {
		jnote("  %-34s no .ko in /lib/modules", m->name);
		return -1;
	}
	publish(step, 1, 0, "finit_module %s", path);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		jnote("  %-34s open rc=-%d", m->name, errno);
		return -1;
	}
	rc = (int)syscall(SYS_finit_module, fd, "", 0);
	if (rc) {
		int e = errno;

		jnote("  %-34s finit_module rc=-%d (%s)", m->name, e,
		      e == EEXIST ? "already loaded" :
		      e == ENOEXEC ? "bad format" :
		      e == EINVAL ? "unknown symbol/bad params" :
		      e == ENODEV ? "no such device" :
		      e == EPERM ? "permission" : "see errno");
		close(fd);
		return -1;
	}
	jnote("  %-34s loaded", m->name);
	close(fd);
	return 0;
}

/* modules.dep: "path/x.ko: path/y.ko ..." */
static void parse_dep(const char *path)
{
	static char buf[131072];
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

/* modules.softdep: "softdep <mod> pre: a b" / "... post: c d" */
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
				if (!strcmp(tok, "pre:")) {
					is_pre = 1;
					continue;
				}
				if (!strcmp(tok, "post:")) {
					is_pre = 0;
					continue;
				}
				if (is_pre && idx >= 0) {
					int ti = mod_find(tok, 1);

					if (ti >= 0 && mods[idx].ndep < MAX_DEPS)
						mods[idx].dep[mods[idx].ndep++] = (short)ti;
				}
			}
		}
		p = nl ? nl + 1 : NULL;
	}
}

/* one module name per line -> mods[] with bootset=1 (the device's boot list) */
static void parse_bootset(const char *path)
{
	static char buf[65536];
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
			if (idx >= 0)
				mods[idx].bootset = 1;
		}
		p = nl ? nl + 1 : NULL;
	}
}

/*
 * THE CHAIN: the closure of the two goals (USB gadget, UFS) over the device's
 * own modules.dep + modules.softdep.  modules.load.recovery's 329 names are
 * deliberately NOT used: the ~250 modules that only a recovery boot needs
 * (kgsl, audio, camera, sensors, wifi...) are irrelevant to the goal and every
 * one of them is another chance to block for ever inside a probe.
 */
static const char *SEED_USB[] = {
	"dwc3_msm", "phy_msm_ssusb_qmp", "phy_msm_snps_hs", "phy_generic",
	"eud", "ssusb_redriver_nb7vpq904m", "ucsi_glink", "altmode_glink",
	"pmic_glink", "pdr_interface", "qmi_helpers", "qcom_glink",
	"qcom_glink_smem", "qcom_smd", "rproc_qcom_common", "qcom_ipc_logging",
	"smem", "qcom_hwspinlock", "clk_qcom", "proxy_consumer",
	"debug_regulator", "gdsc_regulator", "minidump", "qrtr", "fsa4480_i2c",
};
static const char *SEED_UFS[] = {
	"ufs_qcom", "phy_qcom_ufs", "phy_qcom_ufs_qmp_v4_waipio",
	"phy_qcom_ufs_qmp_v4_lahaina", "phy_qcom_ufs_qmp_v4_diwali",
	"phy_qcom_ufs_qmp_v4_cape", "ufshcd_crypto_qti", "crypto_qti_common",
	"crypto_qti_hwkm",
};

static short chain[MAX_MODS];
static int n_chain;
static unsigned char in_usb[MAX_MODS];

static void dfs(int idx, int depth, int tag)
{
	int i;

	if (idx < 0 || idx >= n_mods || depth > 24 || mods[idx].done)
		return;
	mods[idx].done = 1;                     /* reuse: "already ordered" */
	for (i = 0; i < mods[idx].ndep; i++)
		dfs(mods[idx].dep[i], depth + 1, tag);
	if (tag == 2)
		in_usb[idx] = 1;
	if (tag) {
		chain[n_chain++] = (short)idx;
	} else {
		/* fallback order: everything we shipped, in the given order */
	}
}

static void build_chain_device(const char *moddir)
{
	char p[PATH_LEN];
	int i;

	snprintf(p, sizeof(p), "%s/modules.dep", moddir);
	if (access(p, R_OK))
		return;
	parse_dep(p);
	snprintf(p, sizeof(p), "%s/modules.softdep", moddir);
	parse_softdep(p);
	snprintf(p, sizeof(p), "%s/modules.load", moddir);
	parse_bootset(p);

	n_chain = 0;
	for (i = 0; i < (int)(sizeof(SEED_USB) / sizeof(SEED_USB[0])); i++)
		dfs(mod_find(SEED_USB[i], 1), 0, 2);
	for (i = 0; i < (int)(sizeof(SEED_UFS) / sizeof(SEED_UFS[0])); i++)
		dfs(mod_find(SEED_UFS[i], 1), 0, 1);
	for (i = 0; i < n_mods; i++)
		mods[i].done = 0;
}

/* fallback (no device metadata: exactly the QEMU situation): split the
 * already ordered list we ship into three contiguous chunks */
static void build_chain_fallback(void)
{
	static char buf[32768];
	static char order[64];
	char *p = buf;
	int i;

	snprintf(order, sizeof(order), "/lib/modules/fallback.order");
	if (rfall(order, buf, sizeof(buf)) < 0) {
		jnote("no /lib/modules/fallback.order either: nothing to load");
		return;
	}
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
			if (idx >= 0)
				chain[n_chain++] = (short)idx;
		}
		p = nl ? nl + 1 : NULL;
	}
	for (i = 0; i < n_chain; i++) {
		if (i < n_chain / 3)
			mods[chain[i]].block = 2;
		else if (i < 2 * (n_chain / 3))
			mods[chain[i]].block = 3;
		else
			mods[chain[i]].block = 4;
	}
}

/*
 * Assign each chain member to step 2, 3 or 4:
 *   2 = it is in the device's own modules.load, i.e. a module Android itself
 *       loads in the boot ramdisk, before any remote processor is running;
 *   3 = it belongs to the USB gadget closure;
 *   4 = the rest (the UFS closure).
 * A forward pass then pushes every module to at least the block of its
 * dependencies, so a step never has to load a module whose deps are not in
 * yet (the chain is in dependency order).
 */
static void assign_blocks(int device_mode)
{
	int i, j;

	if (device_mode) {
		for (i = 0; i < n_chain; i++) {
			int idx = chain[i];

			if (mods[idx].bootset)
				mods[idx].block = 2;
			else if (in_usb[idx] || device_mode == 0)
				mods[idx].block = 3;
			else
				mods[idx].block = 4;
		}
	}
	for (i = 0; i < n_chain; i++) {
		int idx = chain[i];

		for (j = 0; j < mods[idx].ndep; j++) {
			int d = mods[idx].dep[j];

			if (d >= 0 && mods[d].block > mods[idx].block)
				mods[idx].block = mods[d].block;
		}
	}
}

static void load_block(int block, int step, int *n_ok, int *n_fail)
{
	int i;

	for (i = 0; i < n_chain; i++) {
		int idx = chain[i];

		if (mods[idx].block != block || mods[idx].done == 2)
			continue;
		mods[idx].done = 2;
		if (load_one(&mods[idx], step) == 0)
			(*n_ok)++;
		else
			(*n_fail)++;
	}
}

/* ------------------------------------------------------------ rawdump I/O */
#define RD_SECTORS "524288"          /* 256 MiB, unique in this GPT */
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
		jnote("rawdump: %s major:minor=%u:%u", e->d_name, maj, min);
		unlink("/dev/rd");
		if (mknod("/dev/rd", S_IFBLK | 0600, makedev(maj, min)))
			jnote("rawdump mknod rc=-%d", errno);
		rd_major = (int)maj;
		rd_minor = (int)min;
		found = 1;
		break;
	}
	closedir(d);
	return found;
}

/* ------------------------------------------------------------ child writers */
static int open_kmsg(void)
{
	int fd;

	if (access("/dev/kmsg", F_OK))
		(void)mknod("/dev/kmsg", S_IFCHR | 0644, makedev(1, 11));
	fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
	return fd < 0 ? -1 : fd;
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
	int fd;

	if (maj < 0)
		return -1;
	(void)unlink("/dev/pmsg0");
	if (mknod("/dev/pmsg0", S_IFCHR | 0600, makedev(maj, 0)))
		jnote("pmsg0 mknod %d:0 rc=-%d", maj, errno);
	fd = open("/dev/pmsg0", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
	return fd < 0 ? -1 : fd;
}

/*
 * Dump the shared journal to every channel that might survive a reset.
 * MUST run in a child: every one of these write()s can block for ever on
 * this hardware, which is exactly why PID 1 is not allowed to do it.
 */
static void dump_journal(int step, const char *why)
{
	int fds[3];
	const char *names[3] = { "kmsg", "pmsg", "rawdump" };
	int i, nfd = 0;
	unsigned len;

	publish(step, 1, 0, "journal dump (%s)", why);
	fds[nfd] = open_kmsg();
	if (fds[nfd] >= 0) {
		jnote("channel kmsg open");
		nfd++;
	} else {
		jnote("channel kmsg unavailable");
	}
	fds[nfd] = open_pmsg();
	if (fds[nfd] >= 0) {
		jnote("channel pmsg open (ramoops)");
		nfd++;
	} else {
		jnote("channel pmsg unavailable (no ramoops this boot)");
	}

	__sync_synchronize();
	len = S->len;
	S->len = RING_SIZE;                  /* fresh ring for the next phase */
	(void)names;
	for (i = 0; i < nfd; i++) {
		unsigned off = 0;

		while (off < len) {
			unsigned n = len - off;
			ssize_t w;

			if (n > 1024)
				n = 1024;
			w = write(fds[i], S->logbuf + off, n);
			if (w <= 0)
				break;
			off += (unsigned)w;
		}
		close(fds[i]);
	}

	/* the block partition: survives everything, readable from Android */
	publish(step, 1, 0, "rawdump write (%s)", why);
	if (rd_major < 0 && !find_rawdump())
		jnote("rawdump: no block device yet (ufs_qcom in?)");
	else {
		int fd = open("/dev/rd", O_WRONLY | O_CLOEXEC);

		if (fd < 0) {
			jnote("rawdump open rc=-%d", errno);
		} else {
			unsigned off;

			for (off = 0; off < len; off += 4096) {
				char b[4096];
				unsigned n = len - off;
				ssize_t w;

				if (n > sizeof(b))
					n = sizeof(b);
				memset(b, 0, sizeof(b));
				memcpy(b, S->logbuf + off, n);
				if (lseek(fd, (off_t)off, SEEK_SET) < 0)
					break;
				w = write(fd, b, sizeof(b));
				if (w != (ssize_t)sizeof(b)) {
					jnote("rawdump write off=%u rc=%d", off, (int)w);
					break;
				}
			}
			close(fd);
			jnote("rawdump: journal written (%u bytes)", len);
		}
	}
	/* restore the ring so the next phase keeps a coherent journal */
	if (len < RING_SIZE / 2) {
		memmove(S->logbuf + len, S->logbuf, len);
		S->len = len * 2;
	}
}

/* ------------------------------------------------------------------ gadget */
#define GDIR "/sys/kernel/config/usb_gadget/g1"
static char udc_cur[128];

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

static void poke_role(int step)
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
		publish(step, 1, 0, "usb_role %s -> device", e->d_name);
		if (wf(p, "device") == 0)
			n++;
	}
	closedir(d);
	jnote("usb_role nodes set to device: %d", n);
}

static void net_up(const char *ifname, const char *ip, const char *mask)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	struct sockaddr_in *sin;
	int rc_addr = -1, rc_mask = -1, rc_up = -1;

	if (s < 0) {
		jnote("net socket rc=-%d", errno);
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
	jnote("net %s addr rc=%d mask rc=%d up rc=%d", ifname, rc_addr, rc_mask,
	      rc_up);
}

/* returns 0 only when the UDC really is bound and readable back */
static int gadget_cycle(int step, int wait_s)
{
	char udc[128], path[256], cur[128] = "";
	int i, rc_bind = -1;
	struct stat st;

	for (i = 0; i < wait_s; i++) {
		publish(step, 1, 0, "wait for a real UDC (%ds)", wait_s);
		if (first_udc(udc, sizeof(udc)))
			break;
		child_sleep_ms(1000);
	}
	if (!first_udc(udc, sizeof(udc))) {
		jnote("gadget: no UDC after %ds (dwc3_msm not up?)", wait_s);
		return -1;
	}
	snprintf(udc_cur, sizeof(udc_cur), "%s", udc);
	jnote("gadget: UDC=%s", udc);

	publish(step, 1, 0, "configfs layout under %s", GDIR);
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

	snprintf(path, sizeof(path), GDIR "/UDC");
	if (rf(path, cur, sizeof(cur)) < 0)
		cur[0] = '\0';
	if (strcmp(cur, udc)) {
		publish(step, 1, 0, "bind UDC %s", udc);
		rc_bind = wf(path, udc);
		jnote("gadget: bind %s rc=%d", udc, rc_bind);
	} else {
		rc_bind = 0;
	}
	child_sleep_ms(2000);
	snprintf(path, sizeof(path), "/sys/class/udc/%s/state", udc);
	if (rf(path, cur, sizeof(cur)) >= 0)
		jnote("gadget: udc state=%s", cur);
	else
		jnote("gadget: udc state unreadable");
	snprintf(path, sizeof(path), "/sys/class/udc/%s/current_speed", udc);
	if (rf(path, cur, sizeof(cur)) >= 0)
		jnote("gadget: current_speed=%s", cur);
	net_up("usb0", "10.0.0.1", "255.255.255.0");
	return rc_bind ? -1 : 0;
}

/* ======================================================================== */
/*  steps: every one of them runs in a child, with a deadline               */
/* ======================================================================== */
#define NSTEP 6

enum { S_MOUNTS = 1, S_BOOTMODS, S_USBMODS, S_UFSMODS, S_UFSJOURNAL, S_GADGET };

static const char *STEP_NAME[NSTEP + 1] = {
	"", "mounts+reads", "boot-set modules", "USB chain modules",
	"UFS chain modules", "UFS wait + journal dump", "gadget bind",
};
static const unsigned STEP_DEADLINE_MS[NSTEP + 1] = {
	0, STEP1_MS, STEP2_MS, STEP3_MS, STEP4_MS, STEP5_MS, STEP6_MS,
};

static int device_mode;              /* 1 = the device's metadata is present */

static int run_step(int step)
{
	char buf[512];
	struct utsname u;
	int ok = 0, fail = 0;

	switch (step) {
	case S_MOUNTS:
		(void)mount_pseudo("proc", "/proc", "proc", step);
		(void)mount_pseudo("sysfs", "/sys", "sysfs", step);
		(void)mount_pseudo("configfs", "/sys/kernel/config", "configfs", step);
		(void)mount_pseudo("pstore", "/sys/fs/pstore", "pstore", step);
		publish(step, 1, 0, "read /proc/cmdline");
		if (rf("/proc/cmdline", buf, sizeof(buf)) >= 0)
			jnote("cmdline: %.300s", buf);
		publish(step, 1, 0, "read /proc/devices");
		if (rfall("/proc/devices", buf, sizeof(buf)) >= 0) {
			char *p = strstr(buf, "pmsg");

			jnote("pmsg listed in /proc/devices: %s", p ? "yes" : "no");
		}
		publish(step, 1, 0, "read uname");
		if (!uname(&u))
			jnote("kernel %s %s", u.release, u.machine);
		publish(step, 1, 0, "list /lib/modules");
		index_modules();
		jnote("init v8: %d .ko files visible in /lib/modules", n_files);
		snprintf(buf, sizeof(buf), "/sys/kernel/config/usb_gadget");
		jnote("configfs usb_gadget present=%d", access(buf, F_OK) == 0);
		break;

	case S_BOOTMODS:
	case S_USBMODS:
	case S_UFSMODS:
		publish(step, 1, 0, "plan the module chain");
		if (device_mode) {
			assign_blocks(1);
		} else {
			int i;

			for (i = 0; i < n_chain; i++)
				mods[chain[i]].block = 0;
			build_chain_fallback();
		}
		jnote("step %d (%s): loading block %d of the %d-module chain",
		      step, STEP_NAME[step], step, n_chain);
		load_block(step, step, &ok, &fail);
		jnote("step %d: loaded=%d failed=%d", step, ok, fail);
		break;

	case S_UFSJOURNAL:
		{
			int i = 0;

			for (i = 0; i < 20; i++) {
				publish(step, 1, 0, "wait for a block device (%d/%d)",
					i + 1, 20);
				if (find_rawdump())
					break;
				child_sleep_ms(1000);
			}
			if (rd_major < 0)
				jnote("no rawdump device found in 20 s");
			dump_journal(step, "end of the UFS step");
		}
		break;

	case S_GADGET:
		poke_role(step);
		ok = gadget_cycle(step, 10);
		jnote("step 6 gadget_cycle rc=%d", ok);
		if (ok == 0)
			S->gadget_up = 1;
		break;
	}
	return 0;
}

/* ======================================================================== */
/*  PID 1: the supervisor.  No risky syscall below, by construction.        */
/* ======================================================================== */

/*
 * Start a child that answers for one step.  The child gets /dev/null on its
 * stdio (its own logging goes to kmsg/pmsg/rawdump explicitly) so that a
 * console that nobody drains can never hold it.
 */
static pid_t spawn_step(int step)
{
	pid_t pid = fork();

	if (pid < 0) {
		jnote("fork failed rc=-%d", errno);
		return -1;
	}
	if (pid == 0) {
		if (fd_null >= 0) {
			dup2(fd_null, 0);
			dup2(fd_null, 1);
			dup2(fd_null, 2);
		}
		run_step(step);
		S->phase = 2;
		S->rc = 0;
		_exit(0);
	}
	return pid;
}

/* report mode: the flash period is the code for the step that stalled */
static void report_forever(unsigned stall_step)
{
	for (;;) {
		child_sleep_ms(REPORT_BASE_S * 1000
			       + (stall_step ? (int)(stall_step - 1) * REPORT_STEP_S * 1000
					     : 0));
		syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
			LINUX_REBOOT_CMD_RESTART, (void *)NULL);
	}
}

/* success: hold the gadget up and never reboot (the device stays usable) */
static void hold_gadget(void)
{
	for (;;) {
		pid_t pid;
		int st;
		unsigned long long t0 = now_ms();

		pid = fork();
		if (pid == 0) {
			if (fd_null >= 0) {
				dup2(fd_null, 0);
				dup2(fd_null, 1);
				dup2(fd_null, 2);
			}
			publish(S_GADGET, 1, 0, "maintenance");
			poke_role(S_GADGET);
			(void)gadget_cycle(S_GADGET, 2);
			dump_journal(S_GADGET, "periodic");
			_exit(0);
		}
		/* bounded: never a blocking wait, even in hold mode */
		while (now_ms() - t0 < (unsigned long long)HOLD_TICK_S * 1000) {
			st = 0;
			if (pid > 0 && syscall(SYS_wait4, pid, &st, WNOHANG, NULL) == pid)
				pid = -1;
			child_sleep_ms(200);
		}
		if (pid > 0)
			syscall(SYS_kill, pid, 9);   /* stuck child: leave it */
	}
}

int main(void)
{
	int step;
	unsigned nstall = 0, stall_step = 0;
	char p[PATH_LEN];

	/* one anonymous shared block, inherited by every child */
	S = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE,
		 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (S == MAP_FAILED)
		for (;;) {
			struct timespec ts = { 1, 0 };

			/* not even /dev/null: park, then keep trying to report */
			syscall(SYS_nanosleep, &ts, NULL);
			syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
				LINUX_REBOOT_CMD_RESTART, (void *)NULL);
		}
	S->step = 0;
	S->phase = 0;
	S->len = 0;
	S->stall_step = 0;
	S->gadget_up = 0;
	jnote(TAG ": supervisor started (pid should be 1)");

	/* /dev/null for the children; the only open() PID 1 ever does */
	(void)mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3));
	fd_null = open("/dev/null", O_RDWR | O_CLOEXEC);

	/* the module chain is planned by a child too (it reads the metadata) */
	{
		pid_t pid = spawn_step(S_MOUNTS);
		int st;
		unsigned long long t0 = now_ms();

		while (now_ms() - t0 < STEP_DEADLINE_MS[S_MOUNTS]) {
			if (pid > 0 && syscall(SYS_wait4, pid, &st, WNOHANG, NULL) == pid) {
				pid = -1;
				break;
			}
			child_sleep_ms(50);
		}
		if (pid > 0) {
			/* step 1 stalled: report and let the next boot retry */
			nstall = 1;
			stall_step = S_MOUNTS;
			S->stall_step = S_MOUNTS;
			S->nstall = 1;
			jnote(TAG ": STALL step=1 (%s) after %u ms",
			      STEP_NAME[S_MOUNTS], STEP_DEADLINE_MS[S_MOUNTS]);
			pid = spawn_step(S_UFSJOURNAL);       /* best-effort writer */
			t0 = now_ms();
			while (now_ms() - t0 < 6000) {
				if (pid > 0 &&
				    syscall(SYS_wait4, pid, &st, WNOHANG, NULL) == pid)
					break;
				child_sleep_ms(100);
			}
			if (pid > 0)
				syscall(SYS_kill, pid, 9);
			report_forever(S_MOUNTS);
		}
	}

	/* plan the chain once, in the parent's address space: the children
	 * inherit the plan through the fork, so it is computed exactly once */
	if (!access("/lib/modules/modules.dep", R_OK)) {
		device_mode = 1;
		build_chain_device("/lib/modules");
	} else if (!access("/lib/modules", F_OK)) {
		struct utsname u;

		if (!uname(&u)) {
			snprintf(p, sizeof(p), "/lib/modules/%s/modules.dep", u.release);
			if (!access(p, R_OK)) {
				device_mode = 1;
				build_chain_device("/lib/modules");
			}
		}
	}
	if (device_mode) {
		assign_blocks(1);
		jnote(TAG ": device metadata found: chain=%d modules "
		      "(boot-set=%d)",
		      n_chain, mods[0].block);
	} else {
		int i;

		build_chain_fallback();
		for (i = 0; i < n_chain; i++)
			(void)i;
		jnote(TAG ": no device metadata: fallback chain=%d modules", n_chain);
	}

	for (step = S_BOOTMODS; step <= NSTEP; step++) {
		pid_t pid;
		int st, exited = 0;
		unsigned long long t0 = now_ms();

		jnote(TAG ": step %d/%d %s (deadline %u ms)", step, NSTEP,
		      STEP_NAME[step], STEP_DEADLINE_MS[step]);
		pid = spawn_step(step);
		if (pid > 0) {
			while (now_ms() - t0 < STEP_DEADLINE_MS[step]) {
				if (syscall(SYS_wait4, pid, &st, WNOHANG, NULL) == pid) {
					exited = 1;
					break;
				}
				child_sleep_ms(50);
			}
		}
		if (exited) {
			jnote(TAG ": step %d done (child rc=%d)", step,
			      WIFEXITED(st) ? WEXITSTATUS(st) : -1);
			continue;
		}
		/* the child is still there: that step is the one that can hang */
		nstall++;
		if (!stall_step)
			stall_step = (unsigned)step;
		S->nstall = nstall;
		S->stall_step = stall_step;
		jnote(TAG ": STALL step=%d (%s) exceeded %u ms; last attempt: \"%s\"",
		      step, STEP_NAME[step], STEP_DEADLINE_MS[step], S->detail);
		syscall(SYS_kill, pid, 9);            /* best effort, cannot help */
		/* write the detail somewhere durable, from ANOTHER child */
		{
			pid_t w = spawn_step(S_UFSJOURNAL);
			unsigned long long tw = now_ms();

			while (now_ms() - tw < 6000) {
				if (w > 0 &&
				    syscall(SYS_wait4, w, &st, WNOHANG, NULL) == w)
					break;
				child_sleep_ms(100);
			}
			if (w > 0)
				syscall(SYS_kill, w, 9);
		}
		break;                                 /* first stall is the answer */
	}

	if (!S->gadget_up) {
		jnote(TAG ": no gadget: reporting with a %us period",
		      REPORT_NOSTALL_S);
		{
			pid_t w = spawn_step(S_UFSJOURNAL);   /* one more dump try */
			unsigned long long tw = now_ms();
			int st = 0;

			while (now_ms() - tw < 6000) {
				if (w > 0 &&
				    syscall(SYS_wait4, w, &st, WNOHANG, NULL) == w)
					break;
				child_sleep_ms(100);
			}
			if (w > 0)
				syscall(SYS_kill, w, 9);
		}
		jnote(TAG ": FRAMING reboot loop: no step stalled%s",
		      nstall ? ", but a step already did" : "");
		report_forever(0);
	}

	jnote(TAG ": gadget up: holding, no more reboots");
	hold_gadget();

	/* unreachable: neither loop above returns, and main has no ret */
	for (;;)
		child_sleep_ms(1000);
}
