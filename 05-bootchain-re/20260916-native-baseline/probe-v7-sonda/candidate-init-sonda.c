/*
 * NX679J slot-B boot-chain probe "sonda", images v7/v7b -- /init as a STATIC
 * ELF (aarch64), PID 1.
 *
 * ONE QUESTION, ONE ANSWER: does the kernel reach userspace when the ramdisk is
 * ours?  This init is a deliberate INVERSION of v6: it loads no module, touches
 * no UFS, no USB, no gadget, parses nothing, and never tries to finish a boot.
 * It writes one marker line and then asks the kernel to RESTART, forever, once
 * every ~10 s. That is the whole program.
 *
 * Why a self-restart answers the question without any guesswork:
 *   - Measured, 2026-09-17: after every v5/v6 boot attempt the phone stayed
 *     frozen (RedMagic logo, no USB, no journal on rawdump, empty pstore) and
 *     the ABL A/B counter was untouched: slot-retry-count:b stayed 7 and an ABL
 *     pass after a hung boot never happened. A hung kernel reboots nobody.
 *   - If this probe is executed, the phone restarts BY ITSELF, repeatedly, with
 *     nothing but the boot logo on screen and the host USB port dropping and
 *     re-enumerating: no user action needed, no driver needed, no network
 *     needed.
 *   - If the probe is NOT executed (kernel stuck before /init, or /init cannot
 *     be executed), the phone behaves exactly like v5/v6: frozen, no reset,
 *     counter unchanged.
 *   - The two outcomes cannot be confused, and the probe needs no cooperation
 *     from UFS, USB or any module.
 *
 * Evidence written before each reboot (best effort, never fatal, never blocking):
 *   /dev/kmsg     primary channel. Created with mknod(2) if absent, because the
 *                 Android ramdisk used by v7b ships NO device nodes (the live
 *                 kernel has no devtmpfs: Android's own init is what normally
 *                 creates /dev/kmsg) and this probe does not mount anything.
 *                 The line carries the "<3>" priority so that it passes the
 *                 default console loglevel and is captured by the PStore
 *                 console buffer, readable from Android at /sys/fs/pstore.
 *   /dev/console  fallback, opened O_NONBLOCK so PID 1 can never be blocked by
 *                 a console that nobody is reading.
 *   fd 2 / fd 1   last resort: the kernel wires PID 1's stdio to /dev/console.
 *
 * Hard constraint (kernel requirement, not a style choice): PID 1 must never
 * die. main() ends in two nested infinite loops, so no path reaches the C
 * runtime epilogue and no exit/_exit/abort call is reachable -- verified by
 * disassembling main() and by grepping this source (see build-probe-v7.py).
 *
 * Same toolchain as v6: aarch64-linux-gnu-gcc -static -Os -s (see the build
 * script for the exact command line and the ELF checks).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

/* Marker that the report and the test procedure grep for. */
#define MARK  "nx679j-sonda"
#define SLEEP_SECONDS 10
#define SLEEP_TEXT "10s before the next restart attempt"

/* ---- tiny, dependency-free helpers ------------------------------------- */

/* decimal u64 -> tail of buf, returns pointer to the first digit */
static char *fmt_u64(unsigned long long v, char *end)
{
	*--end = '\0';
	do {
		*--end = (char)('0' + (v % 10));
		v /= 10;
	} while (v);
	return end;
}

/* append s to *p, keeping room for the NUL */
static void put(char **p, const char *s)
{
	while (*s)
		*(*p)++ = *s++;
}

static int write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t n = write(fd, buf, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

/*
 * Create the node if the ramdisk does not ship one. mknod(2) on the initramfs
 * rootfs always works for PID 1; if it fails (existing node, read-only fs, no
 * CAP_MKNOD) the following open() simply returns the reason and we move to the
 * next channel. tty_major/tty_minor are the fixed dev_t values of these two
 * kernel devices (kmsg = 1:11, console = 5:1).
 */
static void ensure_node(const char *path, unsigned int major, unsigned int minor,
			mode_t mode)
{
	if (access(path, F_OK) == 0)
		return;
	(void)mkdir("/dev", 0755);	/* ignore EEXIST / ENOENT */
	(void)mknod(path, S_IFCHR | mode, makedev(major, minor));
}

/* Open the first usable evidence channel; -1 means "no channel at all". */
static int open_channel(const char **what)
{
	int fd;

	ensure_node("/dev/kmsg", 1, 11, 0644);
	fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		*what = "/dev/kmsg";
		return fd;
	}

	ensure_node("/dev/console", 5, 1, 0600);
	fd = open("/dev/console", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd >= 0) {
		*what = "/dev/console";
		return fd;
	}

	/* PID 1's stdio: the kernel opens /dev/console for us when it can. */
	if (write(2, "", 0) == 0) {
		*what = "fd2";
		return 2;
	}
	if (write(1, "", 0) == 0) {
		*what = "fd1";
		return 1;
	}
	*what = "none";
	return -1;
}

/*
 * One marker line, always the same shape so the report can grep it:
 *   <3>nx679j-sonda: <event>[ <detail>] attempt=<n> uptime_ms=<m> [rc=<r> errno=<e>]
 */
static void say(int fd, const char *event, const char *detail,
		unsigned long long attempt, unsigned long long uptime_ms,
		long rc, int err)
{
	char buf[256];
	char *p = buf;
	char num[24];

	put(&p, "<3>" MARK ": ");
	put(&p, event);
	if (detail && detail[0]) {
		put(&p, " ");
		put(&p, detail);
	}
	put(&p, " attempt=");
	put(&p, fmt_u64(attempt, num + sizeof(num)));
	put(&p, " uptime_ms=");
	put(&p, fmt_u64(uptime_ms, num + sizeof(num)));
	if (rc != 0 || err != 0) {
		put(&p, " rc=");
		put(&p, fmt_u64((unsigned long long)rc, num + sizeof(num)));
		put(&p, " errno=");
		put(&p, fmt_u64((unsigned long long)err, num + sizeof(num)));
	}
	*(p++) = '\n';

	if (fd >= 0)
		(void)write_all(fd, buf, (size_t)(p - buf));
}

static unsigned long long uptime_ms(void)
{
	struct timespec ts;

	if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (unsigned long long)ts.tv_sec * 1000ULL
	     + (unsigned long long)(ts.tv_nsec / 1000000L);
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
	const char *chan = "none";
	unsigned long long attempt = 0;
	int fd = open_channel(&chan);

	say(fd, "init reached userspace as pid 1", "", ++attempt, uptime_ms(), 0, 0);
	if (fd >= 0)
		say(fd, "marker channel", chan[0] == '/' ? chan : "none",
		    attempt, uptime_ms(), 0, 0);

	for (;;) {
		long rc;
		int err;
		struct timespec ts;

		say(fd, "calling reboot(2) LINUX_REBOOT_CMD_RESTART", "", attempt,
		    uptime_ms(), 0, 0);

		/*
		 * reboot(LINUX_REBOOT_CMD_RESTART) via the raw syscall, exactly as
		 * specified: argv = magic1, magic2, cmd, arg(NULL). On success this
		 * never returns: the machine restarts. Any return value is a failure
		 * and is logged with its errno before the retry.
		 */
		errno = 0;
		rc = syscall(SYS_reboot,
			     (int)LINUX_REBOOT_MAGIC1,
			     (int)LINUX_REBOOT_MAGIC2,
			     (int)LINUX_REBOOT_CMD_RESTART,
			     (void *)NULL);
		err = errno;

		attempt++;
		say(fd, "reboot(2) returned", "restart did not happen", attempt,
		    uptime_ms(), rc, err);

		ts.tv_sec = SLEEP_SECONDS;
		ts.tv_nsec = 0;
		(void)syscall(SYS_nanosleep, &ts, (void *)NULL);
		say(fd, "sleeping", SLEEP_TEXT, attempt, uptime_ms(), 0, 0);
	}

	/*
	 * Unreachable: the for(;;) above has no exit path. Belt and braces, so
	 * that even a future edit of the loop cannot let PID 1 fall off the end
	 * of main() and let the C runtime call exit(3): park for ever.
	 */
	for (;;) {
		struct timespec ts = { 3600, 0 };

		(void)syscall(SYS_nanosleep, &ts, (void *)NULL);
	}
}
