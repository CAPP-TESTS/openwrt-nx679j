// nx679j-kiosktest — test percorso kiosk completo: master + mode-set (varianti) + flip colori.
// Rosso -> Verde -> Blu (hold s ciascuno), tutto in un processo (master unico).
// Logica mode/setcrtc replicata da nx679j-drmtest.c (le 6 varianti + best).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>

#define GETCRTC        0xc06864a1
#define SETCRTC        0xc06864a2
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define PAGE_FLIP      0xc01864b0
#define SET_MASTER     0x641e
#define PAGE_FLIP_EVENT 0x01

struct drm_mode_modeinfo { uint32_t clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; uint32_t vrefresh; uint32_t flags; uint32_t type; char name[32]; };
struct drm_mode_crtc { uint64_t set_connectors_ptr; uint32_t count_connectors; uint32_t crtc_id; uint32_t fb_id; uint32_t x, y; uint32_t gamma_size; uint32_t mode_valid; struct drm_mode_modeinfo mode; };
struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_mode_fb_cmd { uint32_t fb_id, width, height, pitch, bpp, depth; uint32_t handle; };
struct drm_mode_crtc_page_flip { uint32_t crtc_id, fb_id, flags, reserved; uint64_t user_data; };
typedef struct drm_mode_modeinfo modeinfo_t;

static int fd;
static uint32_t W = 1080, H = 2400;

int main(int argc, char **argv) {
    int giri = argc > 1 ? atoi(argv[1]) : 15;
    int hold = argc > 2 ? atoi(argv[2]) : 2;

    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }
    if (ioctl(fd, SET_MASTER)) printf("setmaster: errno=%d(%s)\n", errno, strerror(errno));
    else printf("SET_MASTER ok\n"); fflush(stdout);

    /* crtc validi (come drmtest) */
    uint32_t all_crtcs[16]; int n_crtcs = 0;
    int crtc_attivo = -1; modeinfo_t best; memset(&best, 0, sizeof(best));
    for (uint32_t ci = 0; ci < 256 && n_crtcs < 16; ci++) {
        struct drm_mode_crtc cur; memset(&cur, 0, sizeof(cur)); cur.crtc_id = ci;
        if (!ioctl(fd, GETCRTC, &cur)) {
            all_crtcs[n_crtcs++] = ci;
            if (cur.mode_valid && cur.mode.hdisplay) { crtc_attivo = (int)ci; best = cur.mode; }
        }
    }
    printf("crtc validi: %d [", n_crtcs);
    for (int k = 0; k < n_crtcs; k++) printf("%u ", all_crtcs[k]);
    printf("] attivo=%d\n", crtc_attivo); fflush(stdout);
    if (n_crtcs == 0) { printf("nessun crtc\n"); return 2; }

    /* 3 buffer dumb a colori pieni: 0=ROSSO 1=VERDE 2=BLU (XRGB8888) */
    uint32_t cols[3] = { 0x00FF0000, 0x0000FF00, 0x000000FF };
    struct { uint32_t fb_id, handle, *map, pitch; } buf[3];
    for (int i = 0; i < 3; i++) {
        struct drm_mode_create_dumb cd; memset(&cd, 0, sizeof(cd));
        cd.width = W; cd.height = H; cd.bpp = 32;
        if (ioctl(fd, CREATE_DUMB, &cd)) { perror("create dumb"); return 4; }
        struct drm_mode_map_dumb md; memset(&md, 0, sizeof(md)); md.handle = cd.handle;
        if (ioctl(fd, MAP_DUMB, &md)) { perror("map dumb"); return 5; }
        uint32_t *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
        if (m == MAP_FAILED) { perror("mmap"); return 6; }
        struct drm_mode_fb_cmd fc; memset(&fc, 0, sizeof(fc));
        fc.width = W; fc.height = H; fc.pitch = cd.pitch; fc.bpp = 32; fc.depth = 24; fc.handle = cd.handle;
        if (ioctl(fd, ADDFB, &fc)) { perror("addfb"); return 7; }
        for (uint32_t y = 0; y < H; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)m + (size_t)y * cd.pitch);
            for (uint32_t x = 0; x < W; x++) row[x] = cols[i];
        }
        __sync_synchronize();
        buf[i].fb_id = fc.fb_id; buf[i].handle = cd.handle; buf[i].map = m; buf[i].pitch = cd.pitch;
        printf("buf[%d] fb=%u pitch=%u\n", i, fc.fb_id, cd.pitch); fflush(stdout);
    }

    /* prova setcrtc: prima sul crtc attivo col suo mode, poi le varianti come drmtest */
    struct drm_mode_crtc set; modeinfo_t mv[8]; int nv = 0; int ok = 0; uint32_t crtc_usato = 0;
    if (crtc_attivo >= 0) { mv[nv++] = best; }
    { int wl[3] = {1080,1080,1080}; int hl[3] = {2340,2400,2340}; int zl[3] = {90,60,120};
      for (int v = 0; v < 3; v++) {
        modeinfo_t m; memset(&m, 0, sizeof(m));
        snprintf(m.name, 32, "%dx%dx%dcmd", wl[v], hl[v], zl[v]);
        m.hdisplay = wl[v]; m.hsync_start = wl[v]+20; m.hsync_end = wl[v]+22; m.htotal = wl[v]+42;
        m.vdisplay = hl[v]; m.vsync_start = hl[v]+40; m.vsync_end = hl[v]+42; m.vtotal = hl[v]+60;
        m.vrefresh = zl[v];
        m.clock = (uint32_t)(((unsigned long long)m.htotal * m.vtotal * zl[v]) / 1000);
        m.type = 8;
        mv[nv++] = m;
      }
    }
    for (int v = 0; v < nv && !ok; v++) {
        for (int k = 0; k < n_crtcs && !ok; k++) {
            memset(&set, 0, sizeof(set));
            set.crtc_id = all_crtcs[k]; set.fb_id = buf[0].fb_id; set.mode_valid = 1;
            set.mode = mv[v];
            if (!ioctl(fd, SETCRTC, &set)) { ok = 1; crtc_usato = all_crtcs[k]; printf("SETCRTC OK: var %d '%s' su crtc %u (fb=%u) -> ROSSO\n", v, mv[v].name, all_crtcs[k], buf[0].fb_id); }
        }
    }
    if (!ok) { printf("SETCRTC: nessuna variante accettata\n"); return 8; }
    fflush(stdout);

    /* flip loop: rosso -> verde -> blu */
    struct pollfd pfd = { fd, POLLIN, 0 };
    int errs = 0;
    for (int f = 0; f < giri; f++) {
        int b = (f + 1) % 3;
        struct drm_mode_crtc_page_flip pf; memset(&pf, 0, sizeof(pf));
        pf.crtc_id = crtc_usato; pf.fb_id = buf[b].fb_id; pf.flags = PAGE_FLIP_EVENT;
        if (ioctl(fd, PAGE_FLIP, &pf)) { printf("flip %d: errno=%d(%s)\n", f, errno, strerror(errno)); if (++errs > 3) break; }
        else {
            if (poll(&pfd, 1, 3000) > 0) { char ev[128]; if (read(fd, ev, sizeof(ev)) < 0) {} }
            printf("flip %d OK -> fb=%u (%s)\n", f, buf[b].fb_id, b==0?"ROSSO":b==1?"VERDE":"BLU"); fflush(stdout);
        }
        sleep(hold);
    }
    printf("FINE: fb %u resta attivo su crtc %u\n", buf[giri % 3].fb_id, crtc_usato);
    return 0;
}
