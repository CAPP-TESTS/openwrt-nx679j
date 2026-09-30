/* Local-only strace smoke target: one new thread and a UNIX socketpair.
 * No QRTR, modem device, network interface, external peer or firmware access.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static void pause_seconds(time_t seconds)
{
	struct timespec remaining = { .tv_sec = seconds };
	while (nanosleep(&remaining, &remaining) && errno == EINTR)
		;
}

static void *exchange(void *unused)
{
	static const char payload[] = "trace-probe-v1";
	char received[sizeof(payload)] = { 0 };
	struct iovec out = { .iov_base = (void *)payload, .iov_len = sizeof(payload) };
	struct iovec in = { .iov_base = received, .iov_len = sizeof(received) };
	struct msghdr tx = { .msg_iov = &out, .msg_iovlen = 1 };
	struct msghdr rx = { .msg_iov = &in, .msg_iovlen = 1 };
	int pair[2], failed;

	(void)unused;
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair)) {
		perror("socketpair");
		return (void *)1;
	}
	failed = sendmsg(pair[0], &tx, 0) != (ssize_t)sizeof(payload) ||
		 recvmsg(pair[1], &rx, 0) != (ssize_t)sizeof(received) ||
		 memcmp(payload, received, sizeof(payload)) != 0;
	close(pair[0]);
	close(pair[1]);
	if (failed)
		return (void *)1;
	puts("EXCHANGE_OK");
	return NULL;
}

int main(void)
{
	pthread_t thread;
	void *result;

	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("READY pid=%ld\n", (long)getpid());
	/* Attach while blocked; create the worker only after the attach window. */
	pause_seconds(3);
	if (pthread_create(&thread, NULL, exchange, NULL) ||
	    pthread_join(thread, &result) || result)
		return 1;
	/* Leave time to detach and check TracerPid before normal process exit. */
	pause_seconds(4);
	puts("FIXTURE_PASS");
	return 0;
}
