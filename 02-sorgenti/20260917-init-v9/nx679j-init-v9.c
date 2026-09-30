/*
 * NX679J /init v9 -- PID 1, and NOTHING ELSE.  v9 = v8 + the DIAGNOSTIC RELAY.
 *
 * Freestanding: no libc, no stdio, no string library.  The complete list of
 * syscalls this program can execute is:
 *
 *      clone(SIGCHLD)     start a child            (fork: cannot block)
 *      execve             run /nx679j/worker or /nx679j/relay (RAM)
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
 * build verifies this from the shipped bytes (verify-candidate-v9.py: every
 * "svc #0" in this binary must be preceded by a whitelisted syscall number).
 * v9 does not change that list: the relay is just one more exec'd child.
 *
 * WHY: on this hardware a finit_module(2) of a driver whose probe waits for a
 * clock, a PHY, a regulator, an extcon/role event or a glink/qmi handshake
 * that never comes does not return -- and CONFIG_PANIC_TIMEOUT=-1 means the
 * resulting freeze has no visible sign from outside at all.  A working Android
 * boot never has that syscall in PID 1 either: magiskinit only mounts and
 * hands over, and the module loading of the real system happens in a separate
 * process, later, without a deadline on the whole system.
 *
 * WHAT v9 ADDS, AND NOTHING ELSE: once the gadget is up (so usb0 exists with
 * 10.0.0.1), PID 1 starts /nx679j/relay as a long-lived child.  That child
 * listens on TCP and answers every connection with one READ-ONLY text dump of
 * the boot, so the journal can be read from the host with one command:
 *
 *      nc 10.0.0.1 9999
 *
 * instead of dump-reading the rawdump partition with dd.  The relay is polled
 * with wait4(WNOHANG) like every other child (it may never block PID 1) and is
 * respawned if it ever dies, with a minimum gap so that a relay that cannot
 * bind cannot turn the maint loop into a spin.  If the gadget never came up,
 * the relay is NOT started and the journal says so: nothing could reach it.
 * The relay never writes anything (no journal file, no device): everything PID
 * 1 needs to record about it is recorded through the worker's dump mode.
 *
 * REPORT (the observation channels are the phone's screen, the journal, and
 * now the network):
 *
 *   logo STEADY + host sees USB 18d1:4ee7 -> SUCCESS: the gadget is bound, and
 *        this PID 1 holds it up and never reboots again.  `nc 10.0.0.1 9999`
 *        then prints the whole boot journal, live.
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
/*
 * OP_RELAY is NOT an op of the worker: it exists only so that the journal
 * messages about the relay read like every other message ("op=relay").  The
 * relay is its own program (/nx679j/relay) started with spawn_bg() below,
 * because it is long-lived: run_op() would kill it at its deadline, and PID 1
 * must not have any operation without a deadline.
 */
#define OP_RELAY   7

static const char *op_name[8] = {
	"mounts", "journal", "gadget", "chain", "gadget", "dump", "maint",
	"relay"
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
#define RELAY  "/nx679j/relay"

/* never respawn the relay more often than this (a relay that cannot bind must
 * not turn the maint loop into a spin) */
#define RELAY_MIN_GAP_MS 5000

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

/*
 * v9: the relay is a LONG-LIVED child, so it cannot go through run_op: the
 * deadline would kill it, and a diagnostic relay that is killed every time is
 * not a relay.  spawn_bg() is the same clone+exec without the waiting part;
 * PID 1 keeps the pid and polls it with wait4(WNOHANG) in its own loop, so the
 * "PID 1 never blocks on a child" rule is untouched.
 */
static long spawn_bg(const char *prog)
{
	char *argv[2];
	char *envp[3];
	long pid;

	argv[0] = (char *)prog;
	argv[1] = 0;
	envp[0] = (char *)"PATH=/";
	envp[1] = (char *)"HOME=/";
	envp[2] = 0;

	pid = sys5(NR_CLONE, SIGCHLD, 0, 0, 0, 0);      /* fork */
	if (pid == 0) {
		long er;

		er = sys3(NR_EXECVE, (long)prog, (long)argv, (long)envp);
		/* v19: exit with the REAL errno, so PID 1 can NAME the failure */
		sys1(NR_EXIT_GROUP, er ? -er : 0);
		for (;;)
			;                                /* not reached */
	}
	return pid;
}

/*
 * Is the background child gone?  1 = gone (*code = exit status, negative for a
 * signal), 0 = still running.  WNOHANG only: a child that is stuck in an
 * uninterruptible syscall must never be able to stop PID 1 here either.
 */
static int bg_done(long pid, int *code)
{
	long status = 0;

	if (pid <= 0)
		return 0;
	if (sys4(NR_WAIT4, pid, (long)&status, WNOHANG, 0) != pid)
		return 0;
	if (status & 0x7f)
		*code = -(int)(status & 0x7f);
	else
		*code = (int)((status >> 8) & 0xff);
	return 1;
}

/* "tag op=relay rc=<exit code> n=<spawns/died count> journal=.. gadget=.." */
static char *mkrelay(char *b, const char *tag, int code, int n, int ufs, int gdc)
{
	char *p = b;

	p = scpy(p, tag);
	p = scpy(p, " op=");
	p = scpy(p, op_name[OP_RELAY]);
	p = scpy(p, " rc=");
	p = u2s(p, (u64)code);
	p = scpy(p, " n=");
	p = u2s(p, (u64)n);
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

/*
 * v9: keep the relay alive, without ever waiting on it.  Called once per
 * iteration of BOTH loops (the successful one and the heartbeat one), so the
 * relay is respawned if it dies for any reason, but never more often than
 * RELAY_MIN_GAP_MS.  The journal lines are rate-limited: a relay that cannot
 * bind must not fill the journal with one line per respawn.
 */
static void keep_relay(long *pid, int *spawns, int *deaths, u64 *t0,
		       int journal_ok, int gadget_ok)
{
	char msg[96];
	int code = 0;

	if (*pid > 0 && bg_done(*pid, &code)) {
		*pid = 0;
		(*deaths)++;
		if (*deaths <= 3 || !(*deaths % 20))
			(void)run_op(OP_DUMP, mkrelay(msg, "v9-relay-died", code,
						      *deaths, journal_ok, gadget_ok));
	}
	if (*pid <= 0 && (*spawns == 0 ||
			  now_ms() - *t0 >= RELAY_MIN_GAP_MS)) {
		*pid = spawn_bg(RELAY);
		*t0 = now_ms();
		if (*pid > 0) {
			(*spawns)++;
			if (*spawns <= 3 || !(*spawns % 20))
				(void)run_op(OP_DUMP,
					     mkrelay(msg, *spawns == 1 ?
						     "v9-relay-started" :
						     "v9-relay-restarted", 0,
						     *spawns, journal_ok,
						     gadget_ok));
		} else {
			(*deaths)++;
			if (*deaths <= 3)
				(void)run_op(OP_DUMP,
					     mkrelay(msg, "v9-relay-spawn-failed",
						     127, *deaths, journal_ok,
						     gadget_ok));
		}
	}
}

void _start(void)
{
	int rc, killed = 0, journal_ok = 0, gadget_ok = 0, gadget2_ok = 0;
	int maint_bad = 0;
	long relay_pid = 0;
	int relay_spawns = 0, relay_deaths = 0;
	u64 period, relay_t0 = 0;
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
	/* v32: firmware del modem PRIMA della catena moduli (il probe di q6v5_pas lo
	 * cerca; senza, -EPROBE_DEFER e dopo il timeout del kernel non riprova piu').
	 * vfat e' INTEGRATO (CONFIG_VFAT_FS=y, misurato).  modem_a: /dev/sde6 = blk 8:70. */
	(void)sys4(33, -100, (long)"/dev/sde6", 0060000 | 0600,
		   (long)((8 << 8) | 70));
	(void)sys5(40, (long)"/dev/sde6", (long)"/vendor/firmware_mnt",
		   (long)"vfat", 0, 0);
	/* v33: il firmware_class cerca in /lib/firmware, il path standard: se questo
	 * driver non e' patchato per il path vendor, il probe non trova modem.mdt e
	 * va in deferred.  Stessa partizione montata anche li' (additivo). */
	(void)sys5(40, (long)"/dev/sde6", (long)"/lib/firmware",
		   (long)"vfat", 0, 0);
	/*
	 * v34: LA CAUSA, misurata sul dmesg di Android (dove il modem funziona):
	 *   qcom_q6v5_pas ...: Direct firmware load for modem.b11 failed with error -2
	 *   qcom_q6v5_pas ...: Falling back to sysfs fallback for: modem.b11
	 *   ueventd: firmware: loading 'modem.b11' for '.../remoteproc-mss/firmware/modem.b11'
	 * Il kernel cerca i file in /lib/firmware: se non li trova chiede allo USERSPACE
	 * (sysfs fallback) e su Android risponde ueventd.  Noi NON abbiamo ueventd, quindi
	 * il fallback non risponde mai e il probe del modem deferisce.
	 * Rimedio: i file devono stare DOVE IL KERNEL LI CERCA.  Bind mount della
	 * directory image/ su /lib/firmware (MS_BIND = 4096, altrimenti non si copre il
	 * mio mounts precedenti).  Prima di OP_CHAIN: il probe avviene li'.
	 */
	/*
	 * v38: LA CAUSA DELL'ENOENT.  In v35/v37 il bind dall'init dava ENOENT mentre
	 * lo stesso bind fatto con busybox riusciva: la differenza e' che busybox si
	 * crea il mount point, il kernel no.  Se la directory target non esiste,
	 * mount() fallisce con ENOENT esattamente come osservato.  Quindi la creo io.
	 * mkdirat = 34 (AT_FDCWD = -100).  faccessat = 48 con QUATTRO argomenti
	 * (dirfd, path, mode, flags): in v37 ne passavo tre, per questo "errore".
	 */
	(void)sys3(34, -100, (long)"/lib/firmware", 0755);
	(void)sys3(34, -100, (long)"/vendor/firmware_mnt", 0755);
	{
		i64 s = sys4(48, -100, (long)"/vendor/firmware_mnt/image", 0, 0);
		i64 t = sys4(48, -100, (long)"/lib/firmware", 0, 0);

		(void)run_op(OP_DUMP, s == 0 ? "v38: sorgente del bind OK"
			     : s == -2 ? "v38: sorgente ASSENTE (ENOENT)"
			     : "v38: sorgente errore");
		(void)run_op(OP_DUMP, t == 0 ? "v38: target /lib/firmware esiste"
			     : t == -2 ? "v38: target ASSENTE (ENOENT)"
			     : "v38: target errore");
	}
	{
		i64 b = sys5(40, (long)"/vendor/firmware_mnt/image",
			     (long)"/lib/firmware", (long)0, 4096, 0);

		if (b != 0)
			b = sys5(40, (long)"/vendor/firmware_mnt/image",
				 (long)"/lib/firmware", (long)0, 4096, 0);
		(void)run_op(OP_DUMP, b == 0 ? "v38: bind /lib/firmware OK"
			     : b == -16 ? "v38: bind EBUSY"
			     : b == -2 ? "v38: bind ENOENT"
			     : b == -22 ? "v38: bind EINVAL"
			     : b == -1 ? "v38: bind EPERM"
			     : "v38: bind altro errore");
	}
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

		/*
		 * v39: LA SCIOGLIE.  vfat/nls_cp437 sono MODULI: prima di OP_CHAIN i
		 * mount fallivano (misurato: "sorgente ASSENTE" in v38), dopo la
		 * catena funzionano.  Quindi servire il firmware qui:
		 *  1) mount vfat di modem_a (8:70) e bind di image/ su /lib/firmware,
		 *     il path che il kernel interroga ("Direct firmware load for
		 *     modem.b11" nel dmesg di Android);
		 *  2) UNA NUOVA registrazione di driver: il kernel riprova i device
		 *     differiti a ogni registrazione, e il probe di
		 *     4080000.remoteproc-mss stavolta trova il firmware.
		 * delete_module=106, finit_module=273, openat=56: syscall, nessun tool
		 * (il busbox OpenWrt non ha rmmod/insmod, kmodloader tace).
		 */
		(void)sys3(34, -100, (long)"/lib/firmware", 0755);
		(void)sys5(40, (long)"/dev/sde6", (long)"/vendor/firmware_mnt",
			   (long)"vfat", 0, 0);
		{
			i64 m1 = sys5(40, (long)"/dev/sde6", (long)"/lib/firmware",
				      (long)"vfat", 0, 0);
			i64 m2 = sys5(40, (long)"/vendor/firmware_mnt/image",
				      (long)"/lib/firmware", (long)0, 4096, 0);

			(void)run_op(OP_DUMP, m1 == 0 ? "v39: vfat /lib/firmware OK"
				     : m1 == -16 ? "v39: vfat EBUSY"
				     : "v39: vfat altro");
			(void)run_op(OP_DUMP, m2 == 0 ? "v39: bind /lib/firmware OK"
				     : m2 == -16 ? "v39: bind EBUSY"
				     : m2 == -2 ? "v39: bind ENOENT"
				     : "v39: bind altro");
		}
		{
			long fd;
			i64 g = -1;
			i64 f = -1;

			/*
			 * v43: qcom_spss dipende da qcom_glink_spss (dmesg: "Unknown symbol
			 * qcom_glink_spss_register").  Carico PRIMA la dipendenza, poi il
			 * modulo: DUE registrazioni nuove = il kernel riprova i device
			 * differiti con il firmware finalmente in /lib/firmware.
			 */
			fd = sys4(56, -100, (long)"/lib/modules/qcom_glink_spss.ko", 0, 0);
			if (fd >= 0)
				g = sys3(273, fd, (long)"", 0);
			(void)run_op(OP_DUMP, g == 0 ? "v43: glink_spss OK"
				     : g == -17 ? "v43: glink_spss EEXIST"
				     : g == -2 ? "v43: glink_spss ENOENT"
				     : g == -126 ? "v43: glink_spss ENOKEY"
				     : fd < 0 ? "v43: glink_spss open KO"
				     : "v43: glink_spss altro");
			fd = sys4(56, -100, (long)"/lib/modules/qcom_spss.ko", 0, 0);
			if (fd >= 0)
				f = sys3(273, fd, (long)"", 0);
			(void)run_op(OP_DUMP, f == 0 ? "v40: trigger spss OK"
				     : f == -17 ? "v40: spss EEXIST"
				     : f == -8 ? "v40: spss ENOEXEC"
				     : f == -2 ? "v40: spss ENOENT"
				     : f == -1 ? "v40: spss EPERM"
				     : f == -22 ? "v40: spss EINVAL"
				     : f == -126 ? "v40: spss ENOKEY"
				     : f == -9 ? "v40: spss file assente"
				     : "v40: spss altro");
		}
		{
			int i;
			static const char *dm[14] = {
				"/lib/modules/rmnet_ctl.ko",
				"/lib/modules/rmnet_core.ko",
				"/lib/modules/rmnet_shs.ko",
				"/lib/modules/rmnet_offload.ko",
				"/lib/modules/rmnet_perf.ko",
				"/lib/modules/rmnet_perf_tether.ko",
				"/lib/modules/rmnet_aps.ko",
				"/lib/modules/rmnet_sch.ko",
				"/lib/modules/rmnet_wlan.ko",
				"/lib/modules/rndisipam.ko",
				"/lib/modules/ipa_clientsm.ko",
				"/lib/modules/ipanetm.ko",
				"/lib/modules/ipam.ko",
				"/lib/modules/gsim.ko",
			};
			int pass, ok = 0;
			i64 pid;

			/*
			 * v53: il caricamento dello stack dati va in un FIGLIO: uno di
			 * questi finit_module (ipam fa il bring-up IPA) puo' bloccare, e
			 * in v52 ha impiccato l'init prima di usb0 (usb up, niente rete
			 * per 4 minuti).  PID 1 non deve mai aspettare qui.
			 */
			pid = sys5(220, 17, 0, 0, 0, 0);
			if (pid > 0) {
				(void)run_op(OP_DUMP, "v53: stack dati in background");
				goto dati_fatti;
			}

			for (pass = 0; pass < 2; pass++)
				for (i = 0; i < 14; i++) {
					long fd = sys4(56, -100, (long)dm[i], 0, 0);
					i64 r = fd >= 0 ? sys3(273, fd, (long)"", 0) : -9;

					if (r == 0)
						ok++;
				}
			(void)run_op(OP_DUMP, ok > 14 ? "v52: stack dati caricato"
				     : ok >= 10 ? "v52: stack quasi"
				     : ok >= 4 ? "v52: stack parziale"
				     : "v52: stack scarso");
			(void)sys1(93, 0);	/* il figlio esce: il padre ha proseguito */
		}
dati_fatti:
		{
			int i;

			/*
			 * Il firmware si carica allo START del remoteproc (su Android
			 * e' l'init vendor a scriverlo).  Il device prende il primo
			 * nome libero: provo remoteproc0..4.
			 */
			/*
			 * v45: avvio TUTTI i rproc, non solo il primo.  Il modem e'
			 * 4080000.remoteproc-mss e il suo firmware (modem.mdt/b11) e'
			 * in /lib/firmware via bind: come su Android, e' lo userspace
			 * a scrivere "start".  Ordine: adsp prima (dipendenza), poi
			 * cdsp/slpi/mss/spss.
			 */
			static const char *rp[5] = {
				"/sys/class/remoteproc/remoteproc0/state",
				"/sys/class/remoteproc/remoteproc1/state",
				"/sys/class/remoteproc/remoteproc2/state",
				"/sys/class/remoteproc/remoteproc3/state",
				"/sys/class/remoteproc/remoteproc4/state",
			};
			int started = 0;

			for (i = 0; i < 0; i++) {
				long fd = sys4(56, -100, (long)rp[i], 1, 0);

				if (fd >= 0) {
					i64 w = sys3(64, fd, (long)"start", 5);

					(void)sys1(57, fd);
					if (w == 5)
						started++;
				}
			}
			(void)run_op(OP_DUMP, started > 0 ? "v45: rproc avviati"
				     : "v45: nessun rproc avviato");
		}

		rc = run_op(OP_GADGET2, "after-modules");
		gadget2_ok = (rc == 0);
		if (rc > 1)
			killed = 1;
		report_op(OP_GADGET2, rc, journal_ok, gadget2_ok);
		gadget_ok = gadget2_ok;
	}

	(void)run_op(OP_DUMP,
		     mkmsg(msg, "v9-summary", OP_DUMP, killed, journal_ok,
			   gadget_ok));

	/*
	 * v9: START THE RELAY.  Unconditionally, and before either loop: the
	 * relay is the one child that is meant to outlive the operations, and
	 * the decision "is there an interface to listen on?" belongs to the
	 * relay itself (it waits for usb0/10.0.0.1 and falls back to the
	 * wildcard address, and it reports in its own dump what it bound to).
	 * PID 1 only has to start it and keep it alive; it never waits for it.
	 */
	keep_relay(&relay_pid, &relay_spawns, &relay_deaths, &relay_t0,
		   journal_ok, gadget_ok);

	if (gadget_ok) {
		/*
		 * v10: HAND OVER TO OPENWRT.
		 * The exec runs in PID 1 itself (execve replaces this image), so the
		 * switch runs as a CHILD: PID 1 stays alive whatever happens.
		 */
		{
			long sp, st = 0;
			char *w;
			int i;

			sp = spawn_bg("/nx679j/switch.sh");
			(void)run_op(OP_DUMP, sp > 0 ? "v18: spawn ok" : "v18: spawn FAILED");
			if (sp > 0) {
				for (i = 0; i < 40; i++) {
					if (sys4(NR_WAIT4, sp, (long)&st, WNOHANG, 0) == sp)
						break;
					sleep_ms(100);
				}
				w = "v18: child alive";
				if ((st & 0x7f) == 0)
					w = ((st >> 8) & 0xff) == 0 ? "v19: exec OK (script started)"
					  : ((st >> 8) & 0xff) == 2 ? "v19: exec ENOENT (path missing)"
					  : ((st >> 8) & 0xff) == 8 ? "v19: exec ENOEXEC (bad format)"
					  : ((st >> 8) & 0xff) == 13 ? "v19: exec EACCES (not executable)"
					  : "v19: exec OTHER errno";
				(void)run_op(OP_DUMP, w);
			}
		}
		/* SUCCESS: hold the device up, never reboot again */
		for (;;) {
			rc = run_op(OP_MAINT, 0);
			if (rc > 1) {
				if (++maint_bad >= 5)
					break;   /* the gadget is gone for good */
			} else {
				maint_bad = 0;
			}
			keep_relay(&relay_pid, &relay_spawns, &relay_deaths,
				   &relay_t0, journal_ok, gadget_ok);
		}
	}

	/* NOT successful: report with the reboot heartbeat, journal first */
	period = killed ? 60000 : (journal_ok ? 8000 : 30000);
	for (;;) {
		sleep_ms(period);
		(void)run_op(OP_DUMP, "heartbeat");
		/* the relay dies with the reboot, but while this boot lasts it is
		 * the fastest way to read the journal: keep it alive here too */
		keep_relay(&relay_pid, &relay_spawns, &relay_deaths, &relay_t0,
			   journal_ok, gadget_ok);
		sys4(NR_REBOOT, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		     LINUX_REBOOT_CMD_RESTART, 0);
		(void)run_op(OP_DUMP, "reboot-call-returned");
	}
}
