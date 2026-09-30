/* Capture trace_pipe without interpreting or modifying packet data.
 * Existing authorized rawdump window: 400 slots, 32 KiB each, starts at 1 MiB.
 * Logger owns slots 0..379; checkpoint index 15 (slot 395) is unused by
 * cellular-staged (0..14), ingress (16/17), and data-staged (18/19).
 * Userspace persistence is NOT guaranteed to run before an immediate panic.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define SLOT_SIZE 32768
#define SLOT_OFFSET ((off_t)(32 + 380 + 15) * SLOT_SIZE)
#define HISTORY_SIZE (SLOT_SIZE - 1024)
static volatile sig_atomic_t stopping;
static char history[HISTORY_SIZE];
static size_t used;
static uint64_t total, batches;

static void stop_capture(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static double now(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		return -1;
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int write_all(int fd, const void *data, size_t count)
{
	const char *p = data;
	while (count) {
		ssize_t n = write(fd, p, count);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		p += n;
		count -= (size_t)n;
	}
	return 0;
}

static int persist(int fd, const char *boot, const char *phase)
{
	char block[SLOT_SIZE] = { 0 };
	int n = snprintf(block, 1024,
		"IPA_TRACE slot=395 checkpoint=15 boot=%s phase=%s "
		"mono=%.6f total=%llu batches=%llu retained=%zu\n",
		boot, phase, now(), (unsigned long long)total,
		(unsigned long long)batches, used);
	ssize_t written;
	if (n < 0 || n >= 1024)
		return -1;
	memcpy(block + n, history, used);
	do {
		written = pwrite(fd, block, sizeof(block), SLOT_OFFSET);
	} while (written < 0 && errno == EINTR);
	if (written != (ssize_t)sizeof(block)) {
		if (written >= 0)
			errno = EIO;
		return -1;
	}
	return fsync(fd);
}

static int make_path(char *path, size_t size, const char *dir, const char *file)
{
	int n = snprintf(path, size, "%s/%s", dir, file);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int main(int argc, char **argv)
{
	char path[PATH_MAX], stop_path[PATH_MAX], boot[64], chunk[4096], *end;
	struct stat st;
	struct pollfd pollfd;
	FILE *file;
	long seconds;
	double deadline;
	int input = -1, output = -1, raw = -1, ready = -1, result = 1;
	int test_regular = argc == 7 && !strcmp(argv[6], "--test-regular");
	unsigned int dev_major, dev_minor;

	if ((argc != 6 && !test_regular) || !argv[5][0] || strlen(argv[5]) > 32 ||
	    strspn(argv[5], "abcdefghijklmnopqrstuvwxyz0123456789_-") !=
	    strlen(argv[5])) {
		fprintf(stderr, "usage: %s trace rawdump outdir seconds phase "
			"[--test-regular]\n", argv[0]);
		return 2;
	}
	errno = 0;
	seconds = strtol(argv[4], &end, 10);
	/* Observation bound only, not a modem parameter. */
	if (errno || *end || seconds < 1 || seconds > 180)
		return 2;
	file = fopen("/proc/sys/kernel/random/boot_id", "r");
	if (!file)
		goto done;
	if (!fgets(boot, sizeof(boot), file)) {
		fclose(file);
		goto done;
	}
	fclose(file);
	boot[strcspn(boot, "\n")] = 0;
	raw = open(argv[2], O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
	if (raw < 0 || fstat(raw, &st))
		goto done;
	if (test_regular) {
		if (!S_ISREG(st.st_mode) || st.st_size < SLOT_OFFSET + SLOT_SIZE)
			goto done;
	} else {
		if (!S_ISBLK(st.st_mode))
			goto done;
		file = fopen("/sys/block/sda/sda11/dev", "r");
		if (!file)
			goto done;
		int fields = fscanf(file, "%u:%u", &dev_major, &dev_minor);
		fclose(file);
		if (fields != 2 || st.st_rdev != makedev(dev_major, dev_minor))
			goto done;
	}
	if (make_path(path, sizeof(path), argv[3], "trace") ||
	    make_path(stop_path, sizeof(stop_path), argv[3], "stop"))
		goto done;
	output = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	input = open(argv[1], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (output < 0 || input < 0)
		goto done;
	signal(SIGINT, stop_capture);
	signal(SIGTERM, stop_capture);
	signal(SIGHUP, stop_capture);
	signal(SIGPIPE, SIG_IGN);
	deadline = now();
	if (deadline < 0 || persist(raw, boot, argv[5]))
		goto done;
	deadline += seconds;
	if (make_path(path, sizeof(path), argv[3], "ready"))
		goto done;
	ready = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (ready < 0)
		goto done;
	close(ready);
	ready = -1;
	pollfd.fd = input;
	pollfd.events = POLLIN;
	while (!stopping) {
		double current = now();
		if (current < 0)
			goto done;
		if (current >= deadline || !access(stop_path, F_OK))
			break;
		ssize_t n = read(input, chunk, sizeof(chunk));
		if (!n)
			break;
		if (n < 0) {
			if (errno != EAGAIN && errno != EINTR)
				goto done;
			/* Poll plus a 10 ms wakeup bound; no real-time guarantees. */
			if (poll(&pollfd, 1, 10) < 0 && errno != EINTR)
				goto done;
			continue;
		}
		size_t count = (size_t)n;
		if (used + count > sizeof(history)) {
			size_t discard = used + count - sizeof(history);
			memmove(history, history + discard, used - discard);
			used -= discard;
		}
		memcpy(history + used, chunk, count);
		used += count;
		total += count;
		batches++;
		if (write_all(output, chunk, count) ||
		    write_all(STDOUT_FILENO, chunk, count) ||
		    persist(raw, boot, argv[5]))
			goto done;
	}
	if (persist(raw, boot, argv[5]) || fsync(output))
		goto done;
	printf("TRACE_CAPTURE_FINISHED total=%llu batches=%llu phase=%s\n",
	       (unsigned long long)total, (unsigned long long)batches, argv[5]);
	result = 0;
done:
	if (result)
		perror("trace capture");
	if (ready >= 0)
		close(ready);
	if (input >= 0)
		close(input);
	if (output >= 0)
		close(output);
	if (raw >= 0)
		close(raw);
	return result;
}
