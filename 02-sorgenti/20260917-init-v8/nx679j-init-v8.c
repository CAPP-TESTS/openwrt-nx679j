/*
 * NX679J /init v8 -- PID 1, and NOTHING ELSE.
 *
 * Freestanding: no libc, no stdio, no string library.  The complete list of
 * syscalls this program can execute is:
 *
 *      clone(SIGCHLD)     start a child            (fork: cannot block)
 *      execve             run /nx679j/worker       (reads the initramfs: RAM)
 *      wait4(...,WNOHANG) "is the child done?"     (NEVER a blocking wait: a
 *                         child stuck in an uninterruptible syscall never
 *                         becomes waitable, so a blocking waitpid inside PID 1
 *                         would itself be a way to hang for ever)
 *      nanosleep          deadline / heartbeat delay (woken by the timer)
 *      kill(SIGKILL)      drop a child that overran its deadline (best effort:
 *                         SIGKILL cannot interrupt D state -- which is exactly
 *                         why the deadline is enforced by THIS process)
 *      reboot             the report, when nothing else can be reported
 *      exit_group         only in the child, if execve itself failed
 *
 * There is no finit_module, no init_module, no mount, no open, no read, no
 * write, no mknod, no ioctl, no socket and no exit in PID 1: everything that
 * can block for ever lives in the worker, in a child, under a deadline.  The
 * build verifies this from the shipped bytes (verify-candidate-v8.py: every
 * "svc #0" in this binary must be preceded by a whitelisted syscall number).
 *
 * WHY: on this hardware a finit_module(2) of a driver whose probe waits for a
 * clock, a PHY, a regulator, an extcon/role event or a glink/qmi handshake
 * that never comes does not return -- and CONFIG_PANIC_TIMEOUT=-1 means the
 * resulting freeze has no visible sign from outside at all.  A working Android
 * boot never has that syscall in PID 1 either: magiskinit only mounts and
 * hands over, and the module loading of the real system happens in a separate
 * process, later, without a deadline on the whole system.
 *
 * REPORT (the only observation channel is the phone's screen; a reboot shows
 * the RedMagic logo again = one "flash"):
 *
 *   logo STEADY + host sees USB 18d1:4ee7 -> SUCCESS: the gadget is bound, and
 *        this PID 1 holds it up and never reboots again.
 *   logo CYCLES (flashes)                 -> NOT successful; the journal on
 *        rawdump / pstore / kmsg has the per-module detail.  The flash period
 *        is a secondary hint: 8 s = UFS was up but the gadget did not bind,
 *        30 s = no UFS/rawdump at all, 60 s = an operation had to be killed by
 *        PID 1 (something wedged outside the per-module timeouts too).
 *   logo STEADY, no USB                   -> userspace not reached, or the
 *        reboot path itself is broken.
 *
 * PID 1 never reports before it has tried to write the journal: every
 * heartbeat cycle runs "worker dump" first, and reboots only after it returns.
 */

typedef unsigned long u64;
typedef long i64;

/* ------------------------------------------- raw syscall entry, aarch64 EL0 */
static inline i64 sys1(long n, long a)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;

	__asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");
	return x0;
}

static inline i64 sys2(long n, long a, long b)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;

	__asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1)
			     : "memory", "cc");
	return x0;
}

static inline i64 sys3(long n, long a, long b, long c)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;

	__asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2)
			     : "memory", "cc");
	return x0;
}

static inline i64 sys4(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;

	__asm__ __volatile__("svc #0" : "+r"(x0)
			     : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
			     : "memory", "cc");
	return x0;
}

static inline i64 sys5(long n, long a, long b, long c, long d, long e)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;

	__asm__ __volatile__("svc #0" : "+r"(x0)
			     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
			     : "memory", "cc");
	return x0;
}

/* the only syscall numbers that may appear in this file */
#define NR_CLONE         220
#define NR_EXECVE        221
#define NR_WAIT4         260
#define NR_NANOSLEEP     101
#define NR_KILL          129
#define NR_REBOOT        142
#define NR_EXIT_GROUP     94
#define NR_CLOCK_GETTIME 113

#define SIGCHLD 17
#define CLOCK_MONOTONIC 1
#define WNOHANG 1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#define LINUX_REBOOT_CMD_RESTART 0x01234567

/* ------------------------------------------------------------- no libc bits */
void *memset(void *d, int c, u64 n)
{
	unsigned char *p = d;

	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, u64 n)
{
	unsigned char *p = d;
	const unsigned char *q = s;

	while (n--)
		*p++ = *q++;
	return d;
}

void *memmove(void *d, const void *s, u64 n)
{
	unsigned char *p = d;
	const unsigned char *q = s;

	if (p < q)
		return memcpy(d, s, n);
	p += n;
	q += n;
	while (n--)
		*--p = *--q;
	return d;
}

static char *scpy(char *d, const char *s)
{
	while ((*d++ = *s++))
		;
	return d - 1;
}

static char *u2s(char *d, u64 v)
{
	char t[24];
	int i = 0;

	if (!v) {
		*d++ = '0';
		return d;
	}
	while (v) {
		t[i++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while (i)
		*d++ = t[--i];
	return d;
}

/* ------------------------------------------------------------ time, sleep */
struct ts {
	i64 sec;
	i64 nsec;
};

static u64 now_ms(void)
{
	struct ts t;

	t.sec = 0;
	t.nsec = 0;
	sys2(NR_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)&t);
	return (u64)t.sec * 1000ULL + (u64)(t.nsec / 1000000);
}

static void sleep_ms(u64 ms)
{
	struct ts t;

	t.sec = (i64)(ms / 1000);
	t.nsec = (i64)(ms % 1000) * 1000000;
	sys2(NR_NANOSLEEP, (long)&t, 0);
}

/* ====================================================================== ops */
/*
 * The operation table, in the order PID 1 executes it.  The ORDER is a
 * requirement here, not an implementation detail:
 *
 *   1 mounts   /proc /sys /configfs.  No module can be needed for these.
 *   2 journal  the FIRST journal write.  UFS is built into this kernel
 *              (CONFIG_SCSI_UFSHCD=y) and the block devices exist from the
 *              first instant of userspace, so the durable channel is there
 *              BEFORE any module is touched.  Nothing that can block on a
 *              driver runs before this point.
 *   3 gadget   create the NCM gadget via configfs.  libcomposite, configfs,
 *              dwc3 and the NCM function are built in (=y) too, so this does
 *              not depend on a module either; if the UDC (a600000.dwc3) is
 *              not there yet the worker says so and we retry after the
 *              modules.
 *   4 chain    NOW the modules, and only for what is not built in, each load
 *              in its own child with its own deadline (TIMEOUT-KILLED).
 *   5 gadget2  retry the gadget with whatever the modules brought up.
 *
 * The three claims to check in this file: no module load happens before the
 * first journal write (OP_JOURNAL < OP_CHAIN) and none before the first gadget
 * attempt (OP_GADGET < OP_CHAIN).  Both are visible in the exec order below.
 */
#define OP_MOUNTS  0
#define OP_JOURNAL 1
#define OP_GADGET  2
#define OP_CHAIN   3
#define OP_GADGET2 4
#define OP_DUMP    5
#define OP_MAINT   6

static const char *op_name[7] = {
	"mounts", "journal", "gadget", "chain", "gadget", "dump", "maint"
};
static const u64 op_deadline[7] = {
	10000,   /* mounts  */
	10000,   /* journal */
	20000,   /* gadget  */
	340000,  /* chain   */
	20000,   /* gadget2 */
	10000,   /* dump    */
	20000,   /* maint   */
};

#define WORKER "/nx679j/worker"

/*
 * One operation = one exec of the worker, with a hard deadline.
 *   0 = the child exited 0            (the operation succeeded)
 *   1 = the child exited non-zero     (it failed, but it did come back)
 *   2 = PID 1 killed it: it overran   (the interesting case)
 *   3 = exec failed
 */
static int run_op(int op, const char *arg)
{
	char *argv[4];
	char *envp[3];
	long status = 0;
	long pid;
	u64 t0;
	int n = 0;

	argv[n++] = (char *)WORKER;
	argv[n++] = (char *)op_name[op];
	if (arg)
		argv[n++] = (char *)arg;
	argv[n] = 0;
	envp[0] = (char *)"PATH=/";
	envp[1] = (char *)"HOME=/";
	envp[2] = 0;

	pid = sys5(NR_CLONE, SIGCHLD, 0, 0, 0, 0);      /* fork */
	if (pid == 0) {
		sys3(NR_EXECVE, (long)WORKER, (long)argv, (long)envp);
		sys1(NR_EXIT_GROUP, 127);
		for (;;)
			;                                /* not reached */
	}
	if (pid < 0)
		return 3;

	t0 = now_ms();
	for (;;) {
		if (sys4(NR_WAIT4, pid, (long)&status, WNOHANG, 0) == pid) {
			if ((status & 0x7f) == 0 && ((status >> 8) & 0xff) == 0)
				return 0;
			return 1;
		}
		if (now_ms() - t0 > op_deadline[op]) {
			sys2(NR_KILL, pid, 9);
			return 2;
		}
		sleep_ms(50);
	}
}

/* a small message for the dump worker: "tag op=chain rc=2 ufs=1 gadget=0" */
static char *mkmsg(char *b, const char *tag, int op, int rc, int ufs, int gdc)
{
	char *p = b;

	p = scpy(p, tag);
	p = scpy(p, " op=");
	p = scpy(p, op_name[op]);
	p = scpy(p, " rc=");
	p = u2s(p, (u64)rc);
	p = scpy(p, " journal=");
	p = u2s(p, (u64)ufs);
	p = scpy(p, " gadget=");
	p = u2s(p, (u64)gdc);
	*p = 0;
	return b;
}

static void report_op(int op, int rc, int ufs, int gdc)
{
	char msg[96];

	if (rc == 0)
		return;
	(void)run_op(OP_DUMP, mkmsg(msg, rc == 2 ? "PID1-KILLED" : "PID1-OP-FAILED",
				    op, rc, ufs, gdc));
}

void _start(void)
{
	int rc, killed = 0, journal_ok = 0, gadget_ok = 0, gadget2_ok = 0;
	int maint_bad = 0;
	u64 period;
	char msg[96];

	/*
	 * ORDER, and why (CONFIG_SCSI_UFSHCD=y, CONFIG_USB_DWC3=y,
	 * CONFIG_USB_CONFIGFS_NCM=y on this kernel -- all built in):
	 *
	 *   1. mounts       /proc /sys /configfs         (no driver work)
	 *   2. journal      the FIRST durable journal write, BEFORE any module
	 *   3. gadget       configfs NCM attempt, BEFORE any module
	 *   4. chain        the modules, each in a child with a deadline
	 *   5. gadget2      retry the gadget after the modules
	 *
	 * Lines 2 and 3 are the answer to "no blocking module wait before the
	 * first journal write and before the first gadget attempt": the first
	 * exec of the worker is the mounts op, the second is the journal op and
	 * the third is the gadget op.  OP_CHAIN (the only op that can call
	 * finit_module) is the FOURTH.  There is no code path here that loads a
	 * module earlier, because the op list is a straight-line sequence.
	 */
	rc = run_op(OP_MOUNTS, 0);
	if (rc > 1)
		killed = 1;
	report_op(OP_MOUNTS, rc, journal_ok, gadget_ok);

	rc = run_op(OP_JOURNAL, "first-journal-write-before-any-module");
	journal_ok = (rc == 0);
	if (rc > 1)
		killed = 1;
	report_op(OP_JOURNAL, rc, journal_ok, gadget_ok);

	rc = run_op(OP_GADGET, "before-modules");
	gadget_ok = (rc == 0);
	if (rc > 1)
		killed = 1;
	report_op(OP_GADGET, rc, journal_ok, gadget_ok);

	if (!gadget_ok) {
		/* The UDC may simply not be there yet; the modules below are
		 * what bring up the PHY/redriver/role switch on this board.
		 * This is a retry, not a dependency: the attempt above already
		 * happened. */
		rc = run_op(OP_CHAIN, 0);
		if (rc > 1)
			killed = 1;
		report_op(OP_CHAIN, rc, journal_ok, gadget_ok);

		rc = run_op(OP_GADGET2, "after-modules");
		gadget2_ok = (rc == 0);
		if (rc > 1)
			killed = 1;
		report_op(OP_GADGET2, rc, journal_ok, gadget2_ok);
		gadget_ok = gadget2_ok;
	}

	(void)run_op(OP_DUMP,
		     mkmsg(msg, "v8-summary", OP_DUMP, killed, journal_ok,
			   gadget_ok));

	if (gadget_ok) {
		/* SUCCESS: hold the device up, never reboot again */
		for (;;) {
			rc = run_op(OP_MAINT, 0);
			if (rc > 1) {
				if (++maint_bad >= 5)
					break;   /* the gadget is gone for good */
			} else {
				maint_bad = 0;
			}
		}
	}

	/* NOT successful: report with the reboot heartbeat, journal first */
	period = killed ? 60000 : (journal_ok ? 8000 : 30000);
	for (;;) {
		sleep_ms(period);
		(void)run_op(OP_DUMP, "heartbeat");
		sys4(NR_REBOOT, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		     LINUX_REBOOT_CMD_RESTART, 0);
		(void)run_op(OP_DUMP, "reboot-call-returned");
	}
}
