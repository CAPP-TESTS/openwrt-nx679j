/* uinput-key — inietta tasti virtuali (volume su/giu') nel kernel via /dev/uinput.
 *
 * Serve a verificare da remoto il percorso tasto -> azione della UI sul display,
 * senza poter premere i tasti fisici. Il device virtuale si chiama ESATTAMENTE
 * 'pmic_resin' e espone EV_KEY con KEY_VOLUMEDOWN (114) e KEY_VOLUMEUP (115),
 * cosi' la UI (che cerca i device per nome in /sys/class/input/eventN/device/name)
 * lo apre come farebbe con il tasto volume reale.
 *
 * ATTENZIONE: sul NX679J esiste GIA' un device reale chiamato 'pmic_resin'
 * (event1). Quindi la ricerca per nome trova anche quello: il tool registra gli
 * eventN che combaciano PRIMA di UI_DEV_CREATE e, dopo la creazione, usa solo
 * l'eventN NUOVO (quello del device virtuale) per mknod. Non tocca il nodo del
 * device reale.
 *
 * uso: uinput-key [--hold S] [--tail D] [--gap MS]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define DEV_NAME "pmic_resin"
#define MAXEV 64

static int ufd = -1;
static int node_idx = -1;
static int pre_match[MAXEV];
static int pre_n = 0;

static int name_of(int i, char *buf, size_t sz) {
    char p[128];
    snprintf(p, sizeof(p), "/sys/class/input/event%d/device/name", i);
    int f = open(p, O_RDONLY);
    if (f < 0) return -1;
    ssize_t n = read(f, buf, sz - 1);
    close(f);
    if (n <= 0) return -1;
    buf[n] = 0;
    if (buf[n - 1] == '\n') buf[n - 1] = 0;
    return 0;
}

static int is_pre(int i) {
    for (int k = 0; k < pre_n; k++) if (pre_match[k] == i) return 1;
    return 0;
}

/* elenca in sysfs i device il cui nome contiene DEV_NAME; se before=1 li registra
 * come "preesistenti" (il device reale), altrimenti ne ritorna il nuovo. */
static int scan(const char *tag, int record_pre) {
    int found = -1;
    printf("--- scan %s ---\n", tag);
    for (int i = 0; i < MAXEV; i++) {
        char nm[128], dv[64], p[128];
        if (name_of(i, nm, sizeof(nm)) < 0) continue;
        if (!strstr(nm, DEV_NAME)) continue;
        snprintf(p, sizeof(p), "/sys/class/input/event%d/dev", i);
        int f = open(p, O_RDONLY);
        dv[0] = 0;
        if (f >= 0) { ssize_t n = read(f, dv, sizeof(dv) - 1); close(f); if (n > 0) { dv[n] = 0; char *nl = strchr(dv, '\n'); if (nl) *nl = 0; } }
        if (record_pre) {
            if (pre_n < MAXEV) pre_match[pre_n++] = i;
            printf("match preesistente: event%d name='%s' dev=%s\n", i, nm, dv);
        } else {
            printf("match nuovo:        event%d name='%s' dev=%s\n", i, nm, dv);
            if (!is_pre(i)) found = i;
        }
    }
    fflush(stdout);
    return found;
}

/* elenca gli eventN aperti dalla UI (fd del processo nx679j-ui) */
static void ui_fds(const char *tag) {
    FILE *pf = popen("pidof nx679j-ui", "r");
    if (!pf) return;
    char pid[32] = {0};
    if (!fgets(pid, sizeof(pid), pf)) { pclose(pf); printf("ui_fds(%s): nx679j-ui non trovato\n", tag); return; }
    pclose(pf);
    for (char *c = pid; *c; c++) if (*c == '\n') *c = 0;
    char dirp[64];
    snprintf(dirp, sizeof(dirp), "/proc/%s/fd", pid);
    DIR *d = opendir(dirp);
    if (!d) { printf("ui_fds(%s): %s non apribile\n", tag, dirp); return; }
    char line[256] = {0};
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char fp[128], tgt[256];
        snprintf(fp, sizeof(fp), "%s/%s", dirp, e->d_name);
        ssize_t n = readlink(fp, tgt, sizeof(tgt) - 1);
        if (n <= 0) continue;
        tgt[n] = 0;
        if (!strstr(tgt, "input")) continue;
        size_t l = strlen(line);
        snprintf(line + l, sizeof(line) - l, "fd%s->%s ", e->d_name, tgt);
    }
    closedir(d);
    printf("ui_fds(%s) pid=%s: %s\n", tag, pid, line);
    fflush(stdout);
}

/* /dev e' una directory statica nel ramdisk di questo sistema: il kernel NON
 * crea il nodo del device virtuale (non c'e' devtmpfs). Va creato a mano. */
static void make_node(int idx) {
    if (idx < 0) { fprintf(stderr, "device virtuale non trovato in sysfs\n"); return; }
    char p[128], buf[64];
    snprintf(p, sizeof(p), "/sys/class/input/event%d/dev", idx);
    int f = open(p, O_RDONLY);
    if (f < 0) { perror("open dev"); return; }
    ssize_t n = read(f, buf, sizeof(buf) - 1);
    close(f);
    if (n <= 0) { fprintf(stderr, "dev non leggibile\n"); return; }
    buf[n] = 0;
    int maj = 0, min = 0;
    if (sscanf(buf, "%d:%d", &maj, &min) != 2) { fprintf(stderr, "dev non parsabile: %s\n", buf); return; }
    snprintf(p, sizeof(p), "/dev/input/event%d", idx);
    unlink(p);
    if (mknod(p, S_IFCHR | 0666, makedev(maj, min))) { perror("mknod"); return; }
    printf("nodo creato: %s (%d:%d) per il device virtuale\n", p, maj, min);
    fflush(stdout);
    node_idx = idx;
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
    if (ioctl(ufd, UI_SET_EVBIT, EV_KEY)) { perror("EV_KEY"); exit(1); }
    if (ioctl(ufd, UI_SET_EVBIT, EV_SYN)) { perror("EV_SYN"); exit(1); }
    if (ioctl(ufd, UI_SET_KEYBIT, KEY_VOLUMEDOWN)) { perror("KEY_VOLUMEDOWN"); exit(1); }
    if (ioctl(ufd, UI_SET_KEYBIT, KEY_VOLUMEUP)) { perror("KEY_VOLUMEUP"); exit(1); }

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", DEV_NAME);
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor = 0x1234; uidev.id.product = 0x5679; uidev.id.version = 1;
    if (write(ufd, &uidev, sizeof(uidev)) < 0) { perror("write(uidev)"); exit(1); }
}

static void key(int code, const char *name, int gap) {
    printf("emit %s (%d) press\n", name, code);
    fflush(stdout);
    emit(EV_KEY, code, 1);
    sync_ev();
    usleep(60000);
    printf("emit %s (%d) release\n", name, code);
    fflush(stdout);
    emit(EV_KEY, code, 0);
    sync_ev();
    usleep(gap * 1000);
}

int main(int argc, char **argv) {
    int hold = 6, tail = 6, gap = 300;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tail") && i + 1 < argc) tail = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gap") && i + 1 < argc) gap = atoi(argv[++i]);
    }

    scan("PRIMA della creazione (preesistenti)", 1);
    ui_fds("prima");

    if (ufd < 0) { }                       /* no-op: setup sotto */
    setup_device();
    if (ioctl(ufd, UI_DEV_CREATE)) { perror("UI_DEV_CREATE"); exit(1); }
    printf("device '%s' creato (EV_KEY: KEY_VOLUMEDOWN=114, KEY_VOLUMEUP=115)\n", DEV_NAME);
    fflush(stdout);
    usleep(300000);

    int idx = scan("DOPO la creazione", 0);
    make_node(idx);
    printf("attendo %d s perche' la UI lo trovi con la scansione periodica\n", hold);
    fflush(stdout);
    sleep(hold);
    ui_fds("con device virtuale vivo");

    key(KEY_VOLUMEDOWN, "KEY_VOLUMEDOWN", gap);
    key(KEY_VOLUMEUP, "KEY_VOLUMEUP", gap);
    printf("eventi emessi; attendo %d s prima di distruggere il device\n", tail);
    fflush(stdout);
    sleep(tail);
    ui_fds("dopo gli eventi");

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    printf("device distrutto\n");
    fflush(stdout);
    if (node_idx >= 0) {          /* pulizia: il nodo non deve restare orfano */
        char p[40];
        snprintf(p, sizeof(p), "/dev/input/event%d", node_idx);
        unlink(p);
        printf("nodo rimosso: %s\n", p);
    }
    return 0;
}
