/*
 * Single stock IPA initialization trigger, not a firmware modification.
 * ABI: ipa3_write in refs/ipa-lineage20/ipa.c and ipa3-write-stock.asm.
 * Device numbers come only from the running kernel's IPA class device.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int main(void)
{
	FILE *f;
	struct stat st;
	unsigned int maj, min;
	dev_t device;
	int fd;
	ssize_t n;

	setvbuf(stdout, NULL, _IOLBF, 0);
	f = fopen("/sys/class/ipa/ipa/dev", "r");
	if (!f) {
		perror("IPA sysfs");
		return 1;
	}
	if (fscanf(f, "%u:%u", &maj, &min) != 2) {
		fprintf(stderr, "Invalid IPA sysfs device number\n");
		fclose(f);
		return 1;
	}
	fclose(f);
	device = makedev(maj, min);
	if (lstat("/dev/ipa", &st) < 0) {
		if (errno != ENOENT || mknod("/dev/ipa", S_IFCHR | 0600, device) < 0) {
			perror("IPA node");
			return 1;
		}
		if (lstat("/dev/ipa", &st) < 0) {
			perror("lstat");
			return 1;
		}
	}
	if (!S_ISCHR(st.st_mode) || st.st_rdev != device) {
		fprintf(stderr, "Refusing unexpected IPA device\n");
		return 1;
	}
	/* The stock open/write callbacks are synchronous; bound userspace waiting. */
	alarm(5);
	fd = open("/dev/ipa", O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		perror("open IPA");
		return 1;
	}
	if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode) || st.st_rdev != device) {
		fprintf(stderr, "IPA descriptor verification failed\n");
		close(fd);
		return 1;
	}
	printf("IPA_WRITE device=%u:%u payload=ASCII_1 length=1\n", maj, min);
	n = write(fd, "1", 1);
	if (n < 0)
		perror("write IPA");
	else
		printf("IPA_WRITE returned=%zd\n", n);
	close(fd);
	alarm(0);
	return n == 1 ? 0 : 1;
}
