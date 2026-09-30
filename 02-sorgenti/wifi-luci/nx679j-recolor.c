// nx679j-recolor — test aggiornamento contenuto: setcrtc con un fb NUOVO (colori diversi).
// Se il pannello mostra i nuovi colori => il percorso "setcrtc ripetuto" funziona (kiosk a refresh).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define GETCRTC        0xc06864a1
#define SETCRTC        0xc06864a2
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae

struct drm_mode_modeinfo { uint32_t clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; uint32_t vrefresh; uint32_t flags; uint32_t type; char name[32]; };
struct drm_mode_crtc { uint64_t set_connectors_ptr; uint32_t count_connectors; uint32_t crtc_id; uint32_t fb_id; uint32_t x, y; uint32_t gamma_size; uint32_t mode_valid; struct drm_mode_modeinfo mode; };
struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_mode_fb_cmd { uint32_t fb_id, width, height, pitch, bpp, depth; uint32_t handle; };

int main(int argc, char **argv) {
    int hold = argc > 1 ? atoi(argv[1]) : 60;
    uint32_t W = 1080, H = 2400;
    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }
    if (ioctl(fd, 0x641e)) printf("setmaster: errno=%d(%s)\n", errno, strerror(errno));
    else printf("SET_MASTER ok\n");
    fflush(stdout);

    /* crtc attivo (dal drmtest) */
    uint32_t crtcs[] = {152, 214, 223, 232, 241, 153, 215, 224, 233, 242};
    int active = -1; struct drm_mode_modeinfo best; memset(&best, 0, sizeof(best));
    for (unsigned i = 0; i < sizeof(crtcs)/sizeof(crtcs[0]); i++) {
        struct drm_mode_crtc cr; memset(&cr, 0, sizeof(cr)); cr.crtc_id = crtcs[i];
        if (!ioctl(fd, GETCRTC, &cr) && cr.mode_valid && cr.mode.hdisplay) { active = (int)crtcs[i]; best = cr.mode; }
    }
    /* trova il connettore DSI connected (type=16) */
    int conn_id = -1;
    for (uint32_t i = 0; i < 256 && conn_id < 0; i++) {
        struct { uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr; uint32_t count_modes, count_props, count_encoders; uint32_t encoder_id, connector_id, connector_type, connector_type_id; uint32_t connection, mm_w, mm_h, subpixel, pad; } c;
        memset(&c, 0, sizeof(c)); c.connector_id = i;
        if (ioctl(fd, 0xc05064a7, &c)) continue;
        if (c.connector_type == 16 && c.connection == 1) conn_id = i;
    }
    printf("conn_id=%d\n", conn_id); fflush(stdout);

    if (active < 0) {
        /* nessun crtc attivo: mode-set con le varianti (come drmtest) */
        printf("nessun crtc attivo: faccio mode-set\n"); fflush(stdout);
        uint32_t all_crtcs[16]; int n_crtcs = 0;
        for (uint32_t ci = 0; ci < 256 && n_crtcs < 16; ci++) {
            struct drm_mode_crtc cur; memset(&cur, 0, sizeof(cur)); cur.crtc_id = ci;
            if (!ioctl(fd, GETCRTC, &cur)) all_crtcs[n_crtcs++] = ci;
        }
        /* crea il buffer DENTRO, prima del setcrtc */
        struct drm_mode_create_dumb cd; memset(&cd, 0, sizeof(cd));
        cd.width = W; cd.height = H; cd.bpp = 32;
        if (ioctl(fd, CREATE_DUMB, &cd)) { perror("create dumb"); return 3; }
        struct drm_mode_map_dumb md; memset(&md, 0, sizeof(md)); md.handle = cd.handle;
        if (ioctl(fd, MAP_DUMB, &md)) { perror("map dumb"); return 4; }
        uint32_t *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
        if (m == MAP_FAILED) { perror("mmap"); return 5; }
        uint32_t cols[4] = { 0x00FFFF00, 0x0000FFFF, 0x00FF00FF, 0x00000000 };
        for (uint32_t y = 0; y < H; y++) {
            uint32_t band = y * 4 / H;
            uint32_t *row = (uint32_t *)((uint8_t *)m + (size_t)y * cd.pitch);
            for (uint32_t x = 0; x < W; x++) row[x] = cols[band];
        }
        __sync_synchronize();
        struct drm_mode_fb_cmd fc; memset(&fc, 0, sizeof(fc));
        fc.width = W; fc.height = H; fc.pitch = cd.pitch; fc.bpp = 32; fc.depth = 24; fc.handle = cd.handle;
        if (ioctl(fd, ADDFB, &fc)) { perror("addfb"); return 6; }
        /* varianti mode + setcrtc su ogni crtc */
        struct drm_mode_modeinfo mv[7]; int nv = 0;
        { int wl[6]={1080,1080,1080,1080,1080,1080}; int hl[6]={2400,2400,2400,2400,2400,2400}; int zl[6]={90,120,60,165,144,90};
          for (int v=0; v<6; v++) { struct drm_mode_modeinfo mm; memset(&mm,0,sizeof(mm));
            snprintf(mm.name,32,"%dx%dx%dcmd",wl[v],hl[v],zl[v]);
            mm.hdisplay=wl[v]; mm.hsync_start=wl[v]+20; mm.hsync_end=wl[v]+22; mm.htotal=wl[v]+42;
            mm.vdisplay=hl[v]; mm.vsync_start=hl[v]+40; mm.vsync_end=hl[v]+42; mm.vtotal=hl[v]+60;
            mm.vrefresh=zl[v]; mm.clock=(uint32_t)(((unsigned long long)mm.htotal*mm.vtotal*zl[v])/1000); mm.type=8;
            mv[nv++]=mm; } }
        int ok=0;
        for (int v=0; v<nv && !ok; v++) for (int k=0; k<n_crtcs && !ok; k++) {
            struct drm_mode_crtc set; memset(&set,0,sizeof(set));
            uint32_t connbuf2[1] = { (uint32_t)(conn_id >= 0 ? conn_id : 56) };
            set.set_connectors_ptr = (uint64_t)(uintptr_t)connbuf2; set.count_connectors = 1;
            set.crtc_id=all_crtcs[k]; set.fb_id=fc.fb_id; set.mode_valid=1; set.mode=mv[v];
            if (!ioctl(fd, SETCRTC, &set)) { ok=1; active=all_crtcs[k]; best=mv[v];
                printf("mode-set OK: var %d '%s' crtc %u fb=%u\n", v, mv[v].name, all_crtcs[k], fc.fb_id); fflush(stdout); }
            else if (v==0) printf("  crtc %u var%d: errno=%d(%s)\n", all_crtcs[k], v, errno, strerror(errno));        }
        if (!ok) { printf("mode-set FALLITO\n"); return 7; }
        printf("pannello dovrebbe mostrare GIALLO/CIANO/MAGENTA/NERO\n"); fflush(stdout);
        sleep(hold);
        return 0;
    }
    printf("crtc attivo %d mode %s\n", active, best.name); fflush(stdout);

    /* nuovo buffer: 4 bande GIALLO/CIANO/MAGENTA/NERO */
    struct drm_mode_create_dumb cd; memset(&cd, 0, sizeof(cd));
    cd.width = W; cd.height = H; cd.bpp = 32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { perror("create dumb"); return 3; }
    struct drm_mode_map_dumb md; memset(&md, 0, sizeof(md)); md.handle = cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { perror("map dumb"); return 4; }
    uint32_t *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { perror("mmap"); return 5; }
    uint32_t cols[4] = { 0x00FFFF00 /*giallo*/, 0x0000FFFF /*ciano*/, 0x00FF00FF /*magenta*/, 0x00000000 /*nero*/ };
    for (uint32_t y = 0; y < H; y++) {
        uint32_t band = y * 4 / H;
        uint32_t *row = (uint32_t *)((uint8_t *)m + (size_t)y * cd.pitch);
        for (uint32_t x = 0; x < W; x++) row[x] = cols[band];
    }
    __sync_synchronize();
    struct drm_mode_fb_cmd fc; memset(&fc, 0, sizeof(fc));
    fc.width = W; fc.height = H; fc.pitch = cd.pitch; fc.bpp = 32; fc.depth = 24; fc.handle = cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { perror("addfb"); return 6; }
    printf("nuovo fb=%u pronto\n", fc.fb_id); fflush(stdout);

    /* setcrtc con il nuovo fb */
    struct drm_mode_crtc set; memset(&set, 0, sizeof(set));
    set.crtc_id = (uint32_t)active; set.fb_id = fc.fb_id; set.mode_valid = 1; set.mode = best;
    if (ioctl(fd, SETCRTC, &set)) { printf("SETCRTC errno=%d(%s)\n", errno, strerror(errno)); return 7; }
    printf("SETCRTC OK con fb=%u -> pannello dovrebbe mostrare GIALLO/CIANO/MAGENTA/NERO\n", fc.fb_id); fflush(stdout);
    sleep(hold);
    printf("fine (fb %u resta)\n", fc.fb_id);
    return 0;
}
