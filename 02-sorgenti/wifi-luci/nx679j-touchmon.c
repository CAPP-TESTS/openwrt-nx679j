// nx679j-touchmon — legge gli eventi del touch (SOLO lettura, zero DRM!)
// Uso: ./touchmon [secondi] — stampa eventi + coordinate ABS.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <linux/input.h>

int main(int argc, char **argv)
{
    int secs = (argc > 1) ? atoi(argv[1]) : 60;
    const char *dev = (argc > 2) ? argv[2] : "/dev/input/event0";

    int fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { perror("open event0"); return 1; }

    printf("touchmon: %s per %ds | aspetto tocchi...\n", dev, secs);
    fflush(stdout);

    time_t t0 = time(NULL);
    int n = 0, x = 0, y = 0, down = 0;
    while (time(NULL) - t0 < secs) {
        struct input_event ev;
        ssize_t r = read(fd, &ev, sizeof(ev));
        if (r == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) x = ev.value;
                if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) y = ev.value;
                if (ev.code == ABS_MT_TRACKING_ID && ev.value >= 0) down = 1;
                if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1) {
                    printf("TOUCH #%d: (%d, %d)\n", ++n, x, y); fflush(stdout);
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                if (ev.value == 1) down = 1;
                else if (ev.value == 0) down = 0;
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                if (down && (x || y)) {
                    printf("move: (%d, %d)\n", x, y); fflush(stdout);
                    x = y = 0;
                }
            }
        } else if (r < 0 && errno != EAGAIN) {
            perror("read");
            break;
        }
        usleep(20000);
    }
    printf("touchmon: fine, %d tocchi rilevati\n", n);
    fflush(stdout);
    return 0;
}
