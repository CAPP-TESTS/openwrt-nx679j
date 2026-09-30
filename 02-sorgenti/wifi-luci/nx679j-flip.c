// nx679j-flip — PAGE FLIP legacy su SDE vendor (senza mode-set!)
// Il display è già attivo dal cont-splash; il flip cambia solo il buffer di scanout.
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

#define GETRESOURCES   0xc04064a0
#define GETCRTC        0xc06864a1
#define SETCRTC        0xc06864a2
#define GETCONNECTOR   0xc05064a7
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define DESTROY_DUMB   0xc02064b4
#define PAGE_FLIP      0xc01864b0

struct drm_mode_card_res { uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr; uint32_t count_fbs, count_crtcs, count_connectors, count_encoders; uint32_t min_w, min_h, max_w, max_h; };
struct drm_mode_crtc { uint64_t set_connectors_ptr; uint32_t count_connectors; uint32_t crtc_id; uint32_t fb_id; uint32_t x, y; uint32_t gamma_size; uint32_t mode_valid; struct { uint32_t clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; uint32_t vrefresh; uint32_t flags; uint32_t type; char name[32]; } mode; };
struct drm_mode_get_connector { uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr; uint32_t count_modes, count_props, count_encoders; uint32_t encoder_id, connector_id, connector_type, connector_type_id; uint32_t connection, mm_w, mm_h, subpixel, pad; };
struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_mode_fb_cmd { uint32_t fb_id, width, height, pitch, bpp, depth; uint32_t handle; };
struct drm_mode_crtc_page_flip { uint32_t crtc_id, fb_id, flags, reserved; uint64_t user_data; };

#define DRM_MODE_PAGE_FLIP_EVENT 0x01

static int fd;
static uint32_t W = 1080, H = 2400;

static int try_crtc_state(uint32_t c) {
    struct drm_mode_crtc cr; memset(&cr, 0, sizeof(cr)); cr.crtc_id = c;
    if (ioctl(fd, GETCRTC, &cr)) return -1;
    printf("crtc %u: fb=%u mode_valid=%d %ux%u (%s) @%u\n", c, cr.fb_id, cr.mode_valid, cr.mode.hdisplay, cr.mode.vdisplay, cr.mode.name, cr.mode.vrefresh);
    return cr.mode_valid ? (int)c : -1;
}

int main(int argc, char **argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 300;
    int nbuf = argc > 2 ? atoi(argv[2]) : 3;

    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    // ponytail: crtc noti per questo pannello (vdtr6130) — niente GETRESOURCES
    uint32_t crtcs[] = {152, 214, 223, 232, 241, 153, 215, 224, 233, 242};
    int active = -1;
    for (unsigned i = 0; i < sizeof(crtcs)/sizeof(crtcs[0]); i++) {
        int r = try_crtc_state(crtcs[i]);
        if (r >= 0) active = r;
    }
    if (active < 0) { printf("NESSUN crtc attivo con mode — niente flip\n"); return 3; }
    printf("crtc attivo: %d\n", active);

    // crea i buffer dumb
    struct { uint32_t fb_id, handle; uint32_t *map; uint32_t pitch; } buf[8];
    if (nbuf > 8) nbuf = 8;
    for (int i = 0; i < nbuf; i++) {
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
        buf[i].fb_id = fc.fb_id; buf[i].handle = cd.handle; buf[i].map = m; buf[i].pitch = cd.pitch;
        printf("buf[%d]: fb=%u pitch=%u mapped ok\n", i, fc.fb_id, cd.pitch);
    }

    // pattern: buf0 = rosso alto/verde basso, poi li animiamo
    for (int i = 0; i < nbuf; i++) {
        uint32_t *m = buf[i].map; int pitch = buf[i].pitch/4;
        for (int y = 0; y < (int)H; y++) {
            uint32_t col = (y < (int)H/2) ? 0xFFFF0000 : 0xFF00FF00;
            if (i == 1) col = 0xFF0000FF;          // blu pieno
            if (i == 2) col = 0xFFFFFFFF;          // bianco
            for (int x = 0; x < (int)W; x++) m[y*pitch + x] = col;
        }
        // barra di avanzamento in alto: N colonne bianche in base al buffer
        int barw = (W * (i+1)) / nbuf;
        for (int y = 8; y < 80; y++)
            for (int x = 0; x < barw; x++) m[y*pitch + x] = 0xFFFF8000;
    }
    __sync_synchronize();

    // primo flip
    struct drm_mode_crtc_page_flip pf; memset(&pf, 0, sizeof(pf));
    pf.crtc_id = active; pf.fb_id = buf[0].fb_id; pf.flags = DRM_MODE_PAGE_FLIP_EVENT;
    int rc = ioctl(fd, PAGE_FLIP, &pf);
    printf("flip0 fb=%u -> rc=%d errno=%d(%s)\n", buf[0].fb_id, rc, errno, strerror(errno));
    if (rc) return 8;

    // attesa evento
    char ev[64]; struct pollfd pfd = { fd, POLLIN, 0 };
    int pr = poll(&pfd, 1, 2000);
    if (pr > 0) { int n = read(fd, ev, sizeof(ev)); printf("evento flip: %d byte\n", n); }
    else printf("evento flip: TIMEOUT (TE muto?)\n");

    // loop flips: anima la barra
    for (int f = 0; f < frames; f++) {
        int b = f % nbuf;
        // anima: barra crescente sul buffer
        uint32_t *m = buf[b].map; int pitch = buf[b].pitch/4;
        int barw = ((f * 20) % W);
        for (int y = 100; y < 200; y++)
            for (int x = 0; x < W; x++) m[y*pitch + x] = (x < barw) ? 0xFF00FFFF : 0xFF202020;
        __sync_synchronize();
        struct drm_mode_crtc_page_flip p2; memset(&p2, 0, sizeof(p2));
        p2.crtc_id = active; p2.fb_id = buf[b].fb_id; p2.flags = DRM_MODE_PAGE_FLIP_EVENT;
        pf = p2;
        if (ioctl(fd, PAGE_FLIP, &pf)) { printf("flip %d rc errno=%d(%s)\n", f, errno, strerror(errno)); break; }
        if (poll(&pfd, 1, 3000) > 0) read(fd, ev, sizeof(ev));
        if ((f % 60) == 0) printf("flip %d ok fb=%u\n", f, buf[b].fb_id);
        usleep(16000);
    }
    printf("FINE flip (device vivo). fb rimasti attivi: %u\n", buf[(frames-1) % nbuf].fb_id);
    return 0;
}
