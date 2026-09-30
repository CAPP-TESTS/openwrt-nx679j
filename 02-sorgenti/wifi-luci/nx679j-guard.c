// nx679j-guard v2 — fd di guardia con DROP_MASTER: previene lastclose ma NON ruba il master.
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#define DROP_MASTER 0x641f
int main(void) {
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    if (ioctl(fd, DROP_MASTER) && errno != 22) fprintf(stderr, "dropmaster errno=%d\n", errno);
    fprintf(stderr, "guard: fd=%d aperto, master ceduto\n", fd);
    for (;;) pause();
}
