/* qmi-trace.c - minimal AArch64 syscall tracer for one Android daemon.
 *
 * Purpose: observe the real modem transport without injecting a second
 * reader/writer.  Attaches with ptrace, traces existing qti threads, and
 * prints openat/close/read/write/ioctl/socket/connect/send/recv calls.
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
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __WALL
#define __WALL 0x40000000
#endif
#ifndef PTRACE_O_TRACESYSGOOD
#define PTRACE_O_TRACESYSGOOD 0x00000001
#endif

/* AArch64 Linux syscall numbers. */
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

#define MAX_THREADS 128
#define MAX_DUMP 160
static int trace_seconds = 12;

typedef struct {
	pid_t tid;
	int in_syscall;
	long nr;
	uint64_t args[6];
	int traced;
} thread_state;

static thread_state threads[MAX_THREADS];
static size_t nthreads;
static pid_t root_pid;

static thread_state *state_for(pid_t tid)
{
	size_t i;

	for (i = 0; i < nthreads; i++)
		if (threads[i].tid == tid)
			return &threads[i];
	if (nthreads >= MAX_THREADS)
		return NULL;
	threads[nthreads].tid = tid;
	threads[nthreads].in_syscall = 0;
	threads[nthreads].traced = 1;
	return &threads[nthreads++];
}

static int get_regs(pid_t tid, struct user_pt_regs *r)
{
	struct iovec iov;

	iov.iov_base = r;
	iov.iov_len = sizeof(*r);
	return ptrace(PTRACE_GETREGSET, tid, (void *)(uintptr_t)NT_PRSTATUS,
		      &iov);
}

static size_t peek_mem(pid_t tid, uint64_t addr, void *dst, size_t len)
{
	size_t done = 0;
	unsigned char *out = dst;

	while (done < len) {
		long word;
		size_t take = sizeof(word);

		if (take > len - done)
			take = len - done;
		errno = 0;
		word = ptrace(PTRACE_PEEKDATA, tid,
				      (void *)(uintptr_t)(addr + done), NULL);
		if (word == -1 && errno)
			break;
		memcpy(out + done, &word, take);
		done += take;
	}
	return done;
}

static void dump_bytes(pid_t tid, uint64_t addr, size_t len)
{
	unsigned char b[MAX_DUMP];
	size_t n, i;

	if (!addr || !len)
		return;
	if (len > sizeof(b))
		len = sizeof(b);
	n = peek_mem(tid, addr, b, len);
	printf(" bytes=%zu:", n);
	for (i = 0; i < n; i++)
		printf(" %02x", b[i]);
}

static void dump_string(pid_t tid, uint64_t addr)
{
	char b[160];
	size_t n;

	if (!addr)
		return;
	n = peek_mem(tid, addr, b, sizeof(b) - 1);
	b[n] = '\0';
	printf(" path=\"%s\"", b);
}

static int interesting(long nr)
{
	switch (nr) {
	case NR_OPENAT:
	case NR_CLOSE:
	case NR_READ:
	case NR_WRITE:
	case NR_READV:
	case NR_WRITEV:
	case NR_IOCTL:
	case NR_SOCKET:
	case NR_CONNECT:
	case NR_SENDTO:
	case NR_RECVFROM:
	case NR_SENDMSG:
	case NR_RECVMSG:
		return 1;
	default:
		return 0;
	}
}

static const char *sys_name(long nr)
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

static double trace_t0;

static double now_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void print_entry(pid_t tid, thread_state *s,
			const struct user_pt_regs *r)
{
	long nr = (long)r->regs[8];
	const uint64_t *a = (const uint64_t *)r->regs;

	s->nr = nr;
	memcpy(s->args, a, sizeof(s->args));
	if (!interesting(nr))
		return;
	printf("%.3f T%ld enter %s(%#lx, %#lx, %#lx, %#lx, %#lx, %#lx)",
	       now_sec() - trace_t0,
	       (long)tid, sys_name(nr), (unsigned long)a[0],
	       (unsigned long)a[1], (unsigned long)a[2],
	       (unsigned long)a[3], (unsigned long)a[4],
	       (unsigned long)a[5]);
	if (nr == NR_OPENAT)
		dump_string(tid, a[1]);
	if (nr == NR_WRITE || nr == NR_SENDTO)
		dump_bytes(tid, a[1], a[2]);
	if (nr == NR_SENDTO && a[4])
		printf(" addr:"), dump_bytes(tid, a[4], 12);
	if (nr == NR_IOCTL)
		printf(" ioctl_req=%#lx", (unsigned long)a[1]);
	printf("\n");
	fflush(stdout);
}

static void print_exit(pid_t tid, thread_state *s,
		       const struct user_pt_regs *r)
{
	long ret = (long)r->regs[0];

	if (!interesting(s->nr))
		return;
	printf("%.3f T%ld exit  %s -> %ld (%#lx)", now_sec() - trace_t0,
	       (long)tid, sys_name(s->nr),
	       ret, (unsigned long)r->regs[0]);
	if ((s->nr == NR_READ || s->nr == NR_RECVFROM) && ret > 0)
		dump_bytes(tid, s->args[1], (size_t)ret);
	printf("\n");
	fflush(stdout);
}

static int add_and_seize(pid_t tid)
{
	thread_state *s;

	if (ptrace(PTRACE_SEIZE, tid, NULL,
		   (void *)(uintptr_t)(PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACECLONE)) < 0)
		return -1;
	s = state_for(tid);
	if (!s)
		return -1;
	s->traced = 1;
	return 0;
}

static size_t enumerate_threads(pid_t pid)
{
	char path[64];
	DIR *d;
	struct dirent *de;

	snprintf(path, sizeof(path), "/proc/%ld/task", (long)pid);
	d = opendir(path);
	if (!d)
		return 0;
	while ((de = readdir(d)) != NULL) {
		char *end;
		long tid;

		if (de->d_name[0] == '.')
			continue;
		tid = strtol(de->d_name, &end, 10);
		if (*end || tid <= 0)
			continue;
		if (add_and_seize((pid_t)tid) == 0)
			printf("attached tid=%ld\n", tid);
	}
	closedir(d);
	return nthreads;
}

int main(int argc, char **argv)
{
	int status;
	long elapsed = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: %s PID [SECONDS]\n", argv[0]);
		return 2;
	}
	root_pid = (pid_t)strtol(argv[1], NULL, 10);
	if (argc > 2)
		trace_seconds = atoi(argv[2]);
	if (trace_seconds <= 0)
		trace_seconds = 12;
	if (root_pid <= 0)
		return 2;
	trace_t0 = now_sec();
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("tracing pid=%ld for %d seconds\n", (long)root_pid,
	       trace_seconds);
	if (!enumerate_threads(root_pid)) {
		fprintf(stderr, "no thread attached: %s\n", strerror(errno));
		return 1;
	}
	for (size_t i = 0; i < nthreads; i++)
		ptrace(PTRACE_INTERRUPT, threads[i].tid, NULL, NULL);

	while (elapsed < trace_seconds * 10) {
		pid_t tid = waitpid(-1, &status, __WALL | WNOHANG);
		if (tid == 0) {
			usleep(100000);
			elapsed++;
			continue;
		}
		if (tid < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (WIFEXITED(status) || WIFSIGNALED(status))
			continue;
		if (WIFSTOPPED(status)) {
			struct user_pt_regs r;
			thread_state *s = state_for(tid);
			int sig = WSTOPSIG(status);
			unsigned int event = (unsigned int)status >> 16;

			if (event == PTRACE_EVENT_CLONE) {
				unsigned long newtid = 0;

				ptrace(PTRACE_GETEVENTMSG, tid, NULL, &newtid);
				if (newtid > 0) {
					add_and_seize((pid_t)newtid);
					printf("attached new tid=%lu\n", newtid);
				}
				ptrace(PTRACE_SYSCALL, tid, NULL, NULL);
				continue;
			}
			if (s && get_regs(tid, &r) == 0) {
				if (sig == (SIGTRAP | 0x80)) {
					if (!s->in_syscall) {
						print_entry(tid, s, &r);
						s->in_syscall = 1;
					} else {
						print_exit(tid, s, &r);
						s->in_syscall = 0;
					}
				}
			}
			ptrace(PTRACE_SYSCALL, tid, NULL, NULL);
		}
	}
	for (size_t i = 0; i < nthreads; i++)
		ptrace(PTRACE_DETACH, threads[i].tid, NULL, NULL);
	printf("detached threads=%zu\n", nthreads);
	return 0;
}
