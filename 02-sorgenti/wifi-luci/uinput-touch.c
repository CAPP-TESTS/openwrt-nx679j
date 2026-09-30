/* uinput-touch — inietta un tocco sintetico nel kernel (via /dev/uinput).
 *
 * Serve a verificare da remoto il percorso tocco -> hit-test -> azione della UI
 * sul display, senza dita. Il device virtuale espone ABS_MT_POSITION_X/A-Y come
 * il touch reale, cosi' la UI lo seleziona con lo stesso criterio.
 *
 * uso: uinput-touch <x> <y> [--hold S] [--down MS]
 *   crea il device, aspetta S secondi (perche' la UI lo trovi con la sua
 *   scansione periodica), emette il tap, poi resta 1 s e esce.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define SCREEN_W 1080
#define SCREEN_H 2400
#define DEV_NAME "nx679j-synthetic-touch"

static int ufd = -1;
static int node_idx = -1;

/* /dev e' una directory statica nel ramdisk di questo sistema: il kernel NON
 * crea il nodo del device virtuale (non c'e' devtmpfs). Va creato a mano,
 * leggendo major:minor da sysfs. Senza questo, il device esiste nel kernel ma
 * nessuno puo' aprirlo. */
static void make_node(void) {
    for (int tries = 0; tries < 10; tries++) {
        for (int i = 0; i < 32; i++) {
            char p[128], buf[64];
            snprintf(p, sizeof(p), "/sys/class/input/event%d/device/name", i);
            int f = open(p, O_RDONLY);
            if (f < 0) continue;
            ssize_t n = read(f, buf, sizeof(buf) - 1);
            close(f);
            if (n <= 0) continue;
            buf[n] = 0;
            if (!strstr(buf, DEV_NAME)) continue;
            snprintf(p, sizeof(p), "/sys/class/input/event%d/dev", i);
            f = open(p, O_RDONLY);
            if (f < 0) continue;
            n = read(f, buf, sizeof(buf) - 1);
            close(f);
            if (n <= 0) continue;
            buf[n] = 0;
            int maj = 0, min = 0;
            if (sscanf(buf, "%d:%d", &maj, &min) != 2) continue;
            snprintf(p, sizeof(p), "/dev/input/event%d", i);
            unlink(p);
            if (mknod(p, S_IFCHR | 0666, makedev(maj, min))) { perror("mknod"); return; }
            printf("nodo creato: %s (%d:%d)\n", p, maj, min);
            fflush(stdout);
            node_idx = i;
            return;
        }
        usleep(200000);
    }
    fprintf(stderr, "device virtuale non comparso in sysfs\n");
}

static void emit(int type, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type; ev.code = code; ev.value = val;
    if (write(ufd, &ev, sizeof(ev)) < 0) { perror("write(ev)"); exit(1); }
}

static void sync_ev(void) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
    if (write(ufd, &ev, sizeof(ev)) < 0) { perror("write(SYN)"); exit(1); }
}

static void setup_device(void) {
    ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) { perror("open /dev/uinput"); exit(1); }
    if (ioctl(ufd, UI_SET_EVBIT, EV_ABS)) { perror("EV_ABS"); exit(1); }
    if (ioctl(ufd, UI_SET_EVBIT, EV_KEY)) { perror("EV_KEY"); exit(1); }
    if (ioctl(ufd, UI_SET_EVBIT, EV_SYN)) { perror("EV_SYN"); exit(1); }
    ioctl(ufd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(ufd, UI_SET_ABSBIT, ABS_X);
    ioctl(ufd, UI_SET_ABSBIT, ABS_Y);
    ioctl(ufd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "nx679j-synthetic-touch");
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor = 0x1234; uidev.id.product = 0x5678; uidev.id.version = 1;
    uidev.absmin[ABS_MT_SLOT] = 0;          uidev.absmax[ABS_MT_SLOT] = 9;
    uidev.absmin[ABS_MT_TRACKING_ID] = 0;   uidev.absmax[ABS_MT_TRACKING_ID] = 65535;
    uidev.absmin[ABS_MT_POSITION_X] = 0;    uidev.absmax[ABS_MT_POSITION_X] = SCREEN_W - 1;
    uidev.absmin[ABS_MT_POSITION_Y] = 0;    uidev.absmax[ABS_MT_POSITION_Y] = SCREEN_H - 1;
    uidev.absmin[ABS_X] = 0;                 uidev.absmax[ABS_X] = SCREEN_W - 1;
    uidev.absmin[ABS_Y] = 0;                 uidev.absmax[ABS_Y] = SCREEN_H - 1;
    if (write(ufd, &uidev, sizeof(uidev)) < 0) { perror("write(uidev)"); exit(1); }
    if (ioctl(ufd, UI_DEV_CREATE)) { perror("UI_DEV_CREATE"); exit(1); }
    make_node();
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s <x> <y> [--hold S] [--down MS]\n", argv[0]); return 2; }
    int x = atoi(argv[1]), y = atoi(argv[2]);
    int hold = 6, down = 120;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--down") && i + 1 < argc) down = atoi(argv[++i]);
    }

    setup_device();
    printf("device creato; attendo %d s perche' la UI lo trovi\n", hold);
    fflush(stdout);
    sleep(hold);

    printf("tap a (%d,%d), pressione %d ms\n", x, y, down);
    fflush(stdout);
    emit(EV_ABS, ABS_MT_SLOT, 0);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 1);
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    emit(EV_ABS, ABS_X, x);
    emit(EV_ABS, ABS_Y, y);
    emit(EV_KEY, BTN_TOUCH, 1);
    sync_ev();
    usleep(down * 1000);
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    emit(EV_KEY, BTN_TOUCH, 0);
    sync_ev();
    printf("tap emesso\n");
    fflush(stdout);

    sleep(1);
    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    if (node_idx >= 0) {          /* pulizia: il nodo non deve restare orfano */
        char p[40];
        snprintf(p, sizeof(p), "/dev/input/event%d", node_idx);
        unlink(p);
        printf("nodo rimosso: %s\n", p);
    }
    return 0;
}