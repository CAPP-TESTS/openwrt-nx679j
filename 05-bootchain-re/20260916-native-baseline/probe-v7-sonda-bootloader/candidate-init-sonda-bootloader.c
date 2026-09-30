/*
 * NX679J slot-B boot-chain probe "sonda BOOTLOADER" -- /init as a STATIC ELF
 * (aarch64), PID 1.
 *
 * This is the RESTART2 variant of candidate-init-sonda.c. The single change:
 *
 *     reboot(LINUX_REBOOT_CMD_RESTART)                -> v7/v7b
 *     reboot(LINUX_REBOOT_CMD_RESTART2, "bootloader") -> this file
 *
 * Why the reason matters. With a plain RESTART the only evidence that this
 * program ran is that the machine reset -- and a reset from a hung kernel
 * (watchdog, power collapse) is indistinguishable from a reset this probe
 * asked for, whatever the A/B counter does. LINUX_REBOOT_CMD_RESTART2 passes a
 * reason string down the kernel's restart path to the platform, and on
 * Qualcomm platforms the reset reason is read back by the bootloader: the
 * reason "bootloader" is the documented way to make ABL come up in fastboot
 * mode instead of booting Android. Fastboot is visible FROM THE HOST (USB
 * 18d1:d00d) and needs no screen, no cooperation from the guest filesystem and
 * no A/B metadata, which on this unit is unreliable (slot B already carries
 * successful=1 and the per-slot state lives in the GPT attribute flags, not in
 * misc). So the outcome becomes: host sees fastboot => the probe ran.
 *
 * HYPOTHESIS, NOT FACT: that this unit's ABL honours the reason. The kernel
 * side is proven offline in QEMU; ABL is not observable without the phone.
 *
 * Everything else is deliberately identical to the v7/v7b probe:
 *   - static, no PT_INTERP, no DT_NEEDED, no dependency on UFS/USB/modules;
 *   - writes one marker line (mknod fallback for /dev/kmsg) before rebooting;
 *   - main() cannot return and never calls exit()/abort(): two nested infinite
 *     loops, proven on the disassembly of the shipped code;
 *   - the reason string is a static const char[] and the pointer passed to the
 *     syscall is checked against the actual bytes of the shipped ELF.
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

/*
 * The reset reason handed to the platform. Static const so that it lives in
 * .rodata as a named symbol and its address is a fixed adrp/add pair in the
 * disassembly of main() -- which is what the build and verify scripts check
 * against the bytes of the shipped binary.
 */
static const char reset_reason[] = "bootloader";

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
 * next channel. kmsg is the fixed dev_t 1:11, console 5:1.
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

	say(fd, "init reached userspace as pid 1",
	    "reboot reason=bootloader (LINUX_REBOOT_CMD_RESTART2)",
	    ++attempt, uptime_ms(), 0, 0);
	if (fd >= 0)
		say(fd, "marker channel", chan[0] == '/' ? chan : "none",
		    attempt, uptime_ms(), 0, 0);

	for (;;) {
		long rc;
		int err;
		struct timespec ts;

		say(fd, "calling reboot(2) LINUX_REBOOT_CMD_RESTART2",
		    "arg='bootloader' -> ABL is expected to come up in fastboot",
		    attempt, uptime_ms(), 0, 0);

		/*
		 * reboot(LINUX_REBOOT_CMD_RESTART2, "bootloader") via the raw
		 * syscall: argv = magic1, magic2, cmd, arg(string). The kernel
		 * copies the string out of our address space, logs
		 *   reboot: Restarting system with command 'bootloader'
		 * and hands it to the platform restart path; on success this
		 * never returns. Any return value is a failure and is logged
		 * with its errno before the retry.
		 */
		errno = 0;
		rc = syscall(SYS_reboot,
			     (int)LINUX_REBOOT_MAGIC1,
			     (int)LINUX_REBOOT_MAGIC2,
			     (int)LINUX_REBOOT_CMD_RESTART2,
			     (void *)reset_reason);
		err = errno;

		attempt++;
		say(fd, "reboot(2) returned", "restart2 did not happen", attempt,
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
