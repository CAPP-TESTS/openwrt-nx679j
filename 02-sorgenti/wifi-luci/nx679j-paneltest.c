// nx679j-paneltest — test contenuto dinamico: modes VERBATIM dal connettore + multi-setcrtc in-process.
// Fase 1: setcrtc con modes del connettore (verbatim). Fase 2: 2° setcrtc con fb nuovo (colori diversi).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

typedef uint32_t u32; typedef uint64_t u64;

struct modeinfo { u32 clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; u32 vrefresh, flags, type; char name[32]; };
struct card_res { u64 fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr; u32 count_fbs, count_crtcs, count_connectors, count_encoders, min_w, min_h, max_w, max_h; };
struct get_conn { u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr; u32 count_modes, count_props, count_encoders, encoder_id, connector_id, connector_type, connector_type_id, connection, mm_w, mm_h, subpixel, pad; };
struct crtc { u64 set_connectors_ptr; u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid; struct modeinfo mode; };
struct create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; };
struct map_dumb { u32 handle, pad; u64 offset; };
struct fb_cmd { u32 fb_id, width, height, pitch, bpp, depth, handle; };

#define GETRESOURCES  0xc04064a0
#define GETCRTC       0xc06864a1
#define SETCRTC       0xc06864a2
#define GETCONNECTOR  0xc05064a7
#define CREATE_DUMB   0xc02064b2
#define MAP_DUMB      0xc01064b3
#define ADDFB         0xc01c64ae
#define SET_MASTER    0x641e
#define DROP_MASTER   0x641f

static int fd;
static u32 W = 1080, H = 2400;

static u32 make_fb(const u32 cols[4]) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd));
    cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { perror("create dumb"); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { perror("map dumb"); return 0; }
    u32 *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { perror("mmap"); return 0; }
    for (u32 y=0;y<H;y++) { u32 b=y*4/H; u32 *row=(u32*)((unsigned char *)m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=cols[b]; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { perror("addfb"); return 0; }
    printf("fb nuovo = %u (pitch %u)\n", fc.fb_id, cd.pitch); fflush(stdout);
    return fc.fb_id;
}

int main(int argc, char **argv) {
    int hold = argc > 1 ? atoi(argv[1]) : 120;
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    if (ioctl(fd, SET_MASTER) && errno != 16) printf("setmaster errno=%d\n", errno);
    printf("master ok\n"); fflush(stdout);

    /* conn DSI + modes verbatim */
    int conn_id = -1; struct modeinfo modes[40]; u32 nmodes = 0;
    struct modeinfo best; memset(&best,0,sizeof(best));
    for (u32 i=0; i<256 && conn_id<0; i++) {
        struct get_conn c; memset(&c,0,sizeof(c)); c.connector_id=i;
        if (ioctl(fd, GETCONNECTOR, &c)) continue;
        if (c.connector_type != 16 || c.connection != 1) continue;
        struct get_conn c2; memset(&c2,0,sizeof(c2)); c2.connector_id=i;
        c2.modes_ptr=(u64)(uintptr_t)modes; c2.count_modes=40;
        if (ioctl(fd, GETCONNECTOR, &c2)) continue;
        conn_id=i; nmodes=c2.count_modes; if (nmodes>40) nmodes=40;
        best = modes[0];
        for (u32 m=0;m<nmodes;m++) { printf("mode[%u]: %s %ux%u@%u type=0x%x clk=%u ht=%u vt=%u\n", m, modes[m].name, modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh, modes[m].type, modes[m].clock, modes[m].htotal, modes[m].vtotal); if (modes[m].type & 8) best=modes[m]; }
    }
    if (conn_id < 0) { printf("conn assente\n"); return 2; }
    printf("conn=%d best='%s' %ux%u@%u\n", conn_id, best.name, best.hdisplay, best.vdisplay, best.vrefresh); fflush(stdout);

    /* crtc */
    u32 crtcs[16]; int n=0;
    for (u32 ci=0; ci<256 && n<16; ci++) { struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=ci; if (!ioctl(fd, GETCRTC, &cc)) crtcs[n++]=ci; }
    printf("crtc: "); for(int k=0;k<n;k++) printf("%u ",crtcs[k]); printf("\n"); fflush(stdout);

    /* FASE 1: setcrtc con mode verbatim del connettore + primo fb (ROSSO/VERDE/BLU/BIANCO) */
    u32 cols1[4] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF };
    u32 fb1 = make_fb(cols1);
    if (!fb1) return 3;
    int ok=0; u32 used_crtc=0;
    for (int k=0;k<n && !ok;k++) {
        struct crtc set; memset(&set,0,sizeof(set));
        u32 cb[1]={ (u32)conn_id };
        set.set_connectors_ptr=(u64)(uintptr_t)cb; set.count_connectors=1;
        set.crtc_id=crtcs[k]; set.fb_id=fb1; set.mode_valid=1; set.mode=best;
        if (!ioctl(fd, SETCRTC, &set)) { ok=1; used_crtc=crtcs[k]; printf("SETCRTC#1 OK fb=%u crtc=%u mode='%s'\n", fb1, used_crtc, best.name); fflush(stdout); }
        else printf("  crtc %u: errno=%d(%s)\n", crtcs[k], errno, strerror(errno));
    }
    if (!ok) { printf("SETCRTC#1 FALLITO\n"); return 4; }
    printf("attesa 20s (pannello dovrebbe mostrare R/V/B/W)...\n"); fflush(stdout);
    sleep(20);

    /* FASE 2: fb nuovo (GIALLO/CIANO/MAGENTA/NERO) + 2° setcrtc stesso mode, stesso crtc */
    u32 cols2[4] = { 0x00FFFF00, 0x0000FFFF, 0x00FF00FF, 0x00000000 };
    u32 fb2 = make_fb(cols2);
    if (!fb2) return 5;
    struct crtc set2; memset(&set2,0,sizeof(set2));
    u32 cb2[1]={ (u32)conn_id };
    set2.set_connectors_ptr=(u64)(uintptr_t)cb2; set2.count_connectors=1;
    set2.crtc_id=used_crtc; set2.fb_id=fb2; set2.mode_valid=1; set2.mode=best;
    if (ioctl(fd, SETCRTC, &set2)) { printf("SETCRTC#2 errno=%d(%s)\n", errno, strerror(errno)); }
    else { printf("SETCRTC#2 OK fb=%u -> pannello dovrebbe ora mostrare GIALLO/CIANO/MAGENTA/NERO\n", fb2); }
    fflush(stdout);
    sleep(hold);
    printf("fine\n");
    return 0;
}
