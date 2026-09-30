/* qmi-trace-v2.c - ptrace probe that can trigger a dormant qti path.
 * Usage: qmi-trace-v2 PID [seconds] [SIGUSR1|SIGUSR2|0]
 */
#define _GNU_SOURCE
#include <asm/ptrace.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/elf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif
#ifndef PTRACE_O_TRACESYSGOOD
#define PTRACE_O_TRACESYSGOOD 1
#endif
#ifndef PTRACE_O_TRACECLONE
#define PTRACE_O_TRACECLONE 0x00000008
#endif
#ifndef PTRACE_O_TRACEEXIT
#define PTRACE_O_TRACEEXIT 0x00000040
#endif
#ifndef PTRACE_EVENT_CLONE
#define PTRACE_EVENT_CLONE 3
#endif
#ifndef PTRACE_GETEVENTMSG
#define PTRACE_GETEVENTMSG 0x4201
#endif
#define MAX_THREADS 128
#define MAX_DUMP 128
#define NR_OPENAT 56
#define NR_CLOSE 57
#define NR_READ 63
#define NR_WRITE 64
#define NR_READV 65
#define NR_WRITEV 66
#define NR_IOCTL 29
#define NR_SOCKET 198
#define NR_CONNECT 203
#define NR_SENDTO 206
#define NR_RECVFROM 207
#define NR_SENDMSG 211
#define NR_RECVMSG 212

typedef struct {
	pid_t tid;
	int in_syscall;
	long nr;
	uint64_t args[6];
} thread_state;
static thread_state st[MAX_THREADS];
static size_t nst;

static thread_state *find_state(pid_t tid)
{
	size_t i;
	for (i = 0; i < nst; i++)
		if (st[i].tid == tid)
			return &st[i];
	if (nst == MAX_THREADS)
		return NULL;
	st[nst].tid = tid;
	return &st[nst++];
}

static int regs_get(pid_t tid, struct user_pt_regs *r)
{
	struct iovec iov = { .iov_base = r, .iov_len = sizeof(*r) };
	return ptrace(PTRACE_GETREGSET, tid, (void *)(uintptr_t)NT_PRSTATUS,
			      &iov);
}

static size_t mem_read(pid_t tid, uint64_t addr, void *dst, size_t len)
{
	size_t done = 0;
	unsigned char *p = dst;
	while (done < len) {
		long v;
		size_t take = sizeof(v);
		if (take > len - done)
			take = len - done;
		errno = 0;
		v = ptrace(PTRACE_PEEKDATA, tid,
				   (void *)(uintptr_t)(addr + done), NULL);
		if (v == -1 && errno)
			break;
		memcpy(p + done, &v, take);
		done += take;
	}
	return done;
}

static void dump_data(pid_t tid, uint64_t addr, size_t len)
{
	unsigned char b[MAX_DUMP];
	size_t n, i;
	if (!addr || !len)
		return;
	if (len > sizeof(b))
		len = sizeof(b);
	n = mem_read(tid, addr, b, len);
	printf(" bytes=%zu:", n);
	for (i = 0; i < n; i++)
		printf(" %02x", b[i]);
}

static void dump_path(pid_t tid, uint64_t addr)
{
	char b[160];
	size_t n;
	if (!addr)
		return;
	n = mem_read(tid, addr, b, sizeof(b) - 1);
	b[n] = '\0';
	printf(" path=\"%s\"", b);
}

struct traced_iovec {
	uint64_t base;
	uint64_t len;
};

struct traced_msghdr {
	uint64_t name;
	uint64_t namelen;
	uint64_t iov;
	uint64_t iovlen;
	uint64_t control;
	uint64_t controllen;
	uint64_t flags;
};

static void dump_iov(pid_t tid, uint64_t iov_addr, uint64_t iov_len,
			     size_t result_len)
{
	struct traced_iovec v;
	size_t left = result_len;
	if (!iov_addr || !iov_len || !result_len)
		return;
	if (iov_len > 16)
		iov_len = 16;
	if (mem_read(tid, iov_addr, &v, sizeof(v)) != sizeof(v))
		return;
	if (v.len < left)
		left = (size_t)v.len;
	printf(" iov=%#lx/%#lx", (unsigned long)v.base,
	       (unsigned long)v.len);
	dump_data(tid, v.base, left);
}

static void dump_msg(pid_t tid, uint64_t addr, size_t result_len)
{
	struct traced_msghdr h;
	if (!addr || mem_read(tid, addr, &h, sizeof(h)) != sizeof(h))
		return;
	dump_iov(tid, h.iov, h.iovlen, result_len);
}

static void dump_sockaddr(pid_t tid, uint64_t addr, uint64_t len)
{
	unsigned char b[128];
	size_t n = (size_t)len;
	if (!addr || !n)
		return;
	if (n > sizeof(b))
		n = sizeof(b);
	if (mem_read(tid, addr, b, n) != n)
		return;
	dump_data(tid, (uint64_t)(uintptr_t)b, 0);
	printf(" sockaddr_len=%zu:", n);
	for (size_t i = 0; i < n; i++)
		printf(" %02x", b[i]);
}

static const char *name(long nr)
{
	switch (nr) {
	case NR_OPENAT: return "openat";
	case NR_CLOSE: return "close";
	case NR_READ: return "read";
	case NR_WRITE: return "write";
	case NR_READV: return "readv";
	case NR_WRITEV: return "writev";
	case NR_IOCTL: return "ioctl";
	case NR_SOCKET: return "socket";
	case NR_CONNECT: return "connect";
	case NR_SENDTO: return "sendto";
	case NR_RECVFROM: return "recvfrom";
	case NR_SENDMSG: return "sendmsg";
	case NR_RECVMSG: return "recvmsg";
	default: return "syscall";
	}
}

static int wanted(long nr)
{
	return nr == NR_OPENAT || nr == NR_CLOSE || nr == NR_READ ||
		nr == NR_WRITE || nr == NR_READV || nr == NR_WRITEV ||
		nr == NR_IOCTL || nr == NR_SOCKET || nr == NR_CONNECT ||
		nr == NR_SENDTO || nr == NR_RECVFROM || nr == NR_SENDMSG ||
		nr == NR_RECVMSG;
}

static void enter_print(pid_t tid, thread_state *s,
			const struct user_pt_regs *r)
{
	const uint64_t *a = (const uint64_t *)r->regs;
	long nr = (long)r->regs[8];
	s->nr = nr;
	memcpy(s->args, a, sizeof(s->args));
	if (!wanted(nr))
		return;
	printf("T%ld enter %s(%#lx,%#lx,%#lx,%#lx,%#lx,%#lx)",
		       (long)tid, name(nr), (unsigned long)a[0],
		       (unsigned long)a[1], (unsigned long)a[2],
		       (unsigned long)a[3], (unsigned long)a[4],
		       (unsigned long)a[5]);
	if (nr == NR_OPENAT)
		dump_path(tid, a[1]);
	if (nr == NR_WRITE || nr == NR_SENDTO)
		dump_data(tid, a[1], a[2]);
	if (nr == NR_WRITEV)
		dump_iov(tid, a[1], a[2], (size_t)-1);
	if (nr == NR_SENDMSG)
		dump_msg(tid, a[1], (size_t)-1);
	if (nr == NR_CONNECT)
		dump_sockaddr(tid, a[1], a[2]);
	if (nr == NR_IOCTL)
		printf(" ioctl_req=%#lx", (unsigned long)a[1]);
	printf("\n");
}

static void exit_print(pid_t tid, thread_state *s,
		       const struct user_pt_regs *r)
{
	long ret = (long)r->regs[0];
	if (!wanted(s->nr))
		return;
	printf("T%ld exit %s -> %ld (%#lx)", (long)tid, name(s->nr), ret,
	       (unsigned long)r->regs[0]);
	if ((s->nr == NR_READ || s->nr == NR_RECVFROM) && ret > 0)
		dump_data(tid, s->args[1], (size_t)ret);
	if (s->nr == NR_READV && ret > 0)
		dump_iov(tid, s->args[1], s->args[2], (size_t)ret);
	if (s->nr == NR_RECVMSG && ret > 0)
		dump_msg(tid, s->args[1], (size_t)ret);
	printf("\n");
}

static int attach(pid_t tid)
{
	if (ptrace(PTRACE_SEIZE, tid, NULL,
			   (void *)(uintptr_t)(PTRACE_O_TRACESYSGOOD |
						PTRACE_O_TRACECLONE |
						PTRACE_O_TRACEEXIT)) < 0)
		return -1;
	return find_state(tid) ? 0 : -1;
}

static void attach_all(pid_t pid)
{
	char path[64];
	DIR *d;
	struct dirent *de;
	snprintf(path, sizeof(path), "/proc/%ld/task", (long)pid);
	d = opendir(path);
	if (!d)
		return;
	while ((de = readdir(d)) != NULL) {
		char *end;
		long tid;
		if (de->d_name[0] == '.')
			continue;
		tid = strtol(de->d_name, &end, 10);
		if (*end || tid <= 0 || attach((pid_t)tid))
			continue;
		printf("attached tid=%ld\n", tid);
	}
	closedir(d);
}

int main(int argc, char **argv)
{
	pid_t pid, tid;
	int seconds = 20;
	int trigger = 0;
	int status;
	int ticks = 0;
	if (argc < 2 || argc > 4) {
		fprintf(stderr, "usage: %s PID [seconds] [SIGUSR1|SIGUSR2|0]\n", argv[0]);
		return 2;
	}
	pid = (pid_t)strtol(argv[1], NULL, 10);
	if (argc >= 3)
		seconds = atoi(argv[2]);
	if (argc == 4)
		trigger = atoi(argv[3]);
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("tracing pid=%ld for %d seconds\n", (long)pid, seconds);
	attach_all(pid);
	for (size_t i = 0; i < nst; i++)
		ptrace(PTRACE_INTERRUPT, st[i].tid, NULL, NULL);
	if (trigger == SIGUSR1 || trigger == SIGUSR2) {
		sleep(1);
		if (kill(pid, trigger))
			perror("kill trigger");
		printf("sent signal %d\n", trigger);
	}
	while (ticks < seconds * 10) {
		tid = waitpid(-1, &status, __WALL | WNOHANG);
		if (tid == 0) {
			usleep(100000);
			ticks++;
			continue;
		}
		if (tid < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (!WIFSTOPPED(status))
			continue;
		if (WSTOPSIG(status) == SIGTRAP &&
		    ((unsigned)status >> 16) == PTRACE_EVENT_CLONE) {
			unsigned long newtid = 0;
			if (ptrace(PTRACE_GETEVENTMSG, tid, NULL, &newtid) == 0) {
				printf("T%ld clone -> %lu\n", (long)tid, newtid);
				find_state((pid_t)newtid);
				ptrace(PTRACE_SYSCALL, (pid_t)newtid, NULL, NULL);
			}
		} else if (WSTOPSIG(status) == (SIGTRAP | 0x80)) {
			struct user_pt_regs r;
			thread_state *s = find_state(tid);
			if (s && regs_get(tid, &r) == 0) {
				if (!s->in_syscall) {
					enter_print(tid, s, &r);
					s->in_syscall = 1;
				} else {
					exit_print(tid, s, &r);
					s->in_syscall = 0;
				}
			}
		}
		ptrace(PTRACE_SYSCALL, tid, NULL, NULL);
	}
	for (size_t i = 0; i < nst; i++)
		ptrace(PTRACE_DETACH, st[i].tid, NULL, NULL);
	printf("detached threads=%zu\n", nst);
	return 0;
}
