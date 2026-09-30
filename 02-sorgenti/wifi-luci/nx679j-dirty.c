// nx679j-dirty — test DIRTYFB: setcrtc (rosso) -> dirtyfb -> buffer diventa VERDE -> dirtyfb -> hold.
// dirtyfb = DRM_IOCTL_MODE_DIRTYFB (0xB1) -> msm_framebuffer_dirty -> drm_atomic_helper_dirtyfb.
// Documentato per pannelli cmd-mode manual-update. Non crasha il percorso legacy.
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
struct get_conn { u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr; u32 count_modes, count_props, count_encoders, encoder_id, connector_id, connector_type, connector_type_id, connection, mm_w, mm_h, subpixel, pad; };
struct crtc { u64 set_connectors_ptr; u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid; struct modeinfo mode; };
struct create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; };
struct map_dumb { u32 handle, pad; u64 offset; };
struct fb_cmd { u32 fb_id, width, height, pitch, bpp, depth, handle; };
struct dirty_cmd { u32 fb_id, flags, color, num_clips; u64 clips_ptr; };
struct clip_rect { short x1, y1, x2, y2; };

#define GETCRTC        0xc06864a1
#define SETCRTC        0xc06864a2
#define GETCONNECTOR   0xc05064a7
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define DIRTYFB        0xc01864b1
#define SET_MASTER     0x641e

static int fd;
static u32 W = 1080, H = 2400;
static unsigned char *membuf = NULL; static u32 membuf_pitch = 0, membuf_fb = 0;

static u32 make_fb(void) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd));
    cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { perror("create dumb"); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { perror("map dumb"); return 0; }
    u32 *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { perror("mmap"); return 0; }
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { perror("addfb"); return 0; }
    printf("fb=%u pitch=%u\n", fc.fb_id, cd.pitch); fflush(stdout);
    membuf = (unsigned char *)m; membuf_pitch = cd.pitch; membuf_fb = fc.fb_id;
    return fc.fb_id;
}

static void fill(u32 color) {
    for (u32 y=0;y<H;y++) { u32 *row=(u32*)(membuf+(size_t)y*membuf_pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
}

static int dirtyfb(u32 fb) {
    struct dirty_cmd d; memset(&d,0,sizeof(d));
    static struct clip_rect clips[1] = { {0,0,1080,2400} };
    d.fb_id=fb; d.flags=0; d.color=0; d.num_clips=1; d.clips_ptr=(u64)(uintptr_t)clips;
    errno=0;
    int r = ioctl(fd, DIRTYFB, &d);
    printf("dirtyfb fb=%u -> ret=%d errno=%d(%s)\n", fb, r, errno, strerror(errno));
    fflush(stdout);
    return r;
}

int main(int argc, char **argv) {
    int gap = argc>1?atoi(argv[1]):5;
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd<0){ perror("open"); return 1; }
    if (ioctl(fd,SET_MASTER) && errno!=16) printf("setmaster errno=%d\n",errno);
    printf("master ok\n"); fflush(stdout);

    int conn_id=-1; struct modeinfo modes[40]; struct modeinfo best; memset(&best,0,sizeof(best));
    for (u32 i=0;i<256 && conn_id<0;i++) {
        struct get_conn c; memset(&c,0,sizeof(c)); c.connector_id=i;
        if (ioctl(fd,GETCONNECTOR,&c)) continue;
        if (c.connector_type!=16 || c.connection!=1) continue;
        struct get_conn c2; memset(&c2,0,sizeof(c2)); c2.connector_id=i;
        c2.modes_ptr=(u64)(uintptr_t)modes; c2.count_modes=40;
        if (ioctl(fd,GETCONNECTOR,&c2)) continue;
        conn_id=i; u32 nm=c2.count_modes; if(nm>40)nm=40;
        best=modes[0];
        for (u32 m=0;m<nm;m++) if (modes[m].type&8) best=modes[m];
    }
    if(conn_id<0){ printf("conn assente\n"); return 2; }

    u32 crtcs[16]; int ncr=0;
    for (u32 ci=0;ci<256 && ncr<16;ci++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=ci; if(!ioctl(fd,GETCRTC,&cc)) crtcs[ncr++]=ci; }

    /* fb + riempi ROSSO + setcrtc */
    u32 fb = make_fb();
    if(!fb) return 3;
    fill(0x00FF0000);   /* ROSSO */
    int ok=0;
    for (int k=0;k<ncr && !ok;k++) {
        struct crtc set; memset(&set,0,sizeof(set));
        u32 cb[1]={ (u32)conn_id };
        set.set_connectors_ptr=(u64)(uintptr_t)cb; set.count_connectors=1;
        set.crtc_id=crtcs[k]; set.fb_id=fb; set.mode_valid=1; set.mode=best;
        if (!ioctl(fd, SETCRTC, &set)) { ok=1; printf("SETCRTC OK crtc=%u (ROSSO)\n", crtcs[k]); }
        else printf("  crtc %u errno=%d\n", crtcs[k], errno);
    }
    if(!ok) return 4;
    fflush(stdout);
    sleep(gap);

    /* dirtyfb #1 (nessun cambiamento) */
    printf("--- dirtyfb #1 (senza modifiche):\n");
    dirtyfb(fb);
    sleep(2);

    /* riempi VERDE + dirtyfb #2 */
    printf("--- riempio VERDE + dirtyfb #2:\n");
    fill(0x0000FF00);   /* VERDE */
    dirtyfb(fb);
    sleep(gap);

    /* riempi BLU + dirtyfb #3 */
    printf("--- riempio BLU + dirtyfb #3:\n");
    fill(0x000000FF);   /* BLU */
    dirtyfb(fb);
    sleep(gap);

    /* riempi BIANCO + dirtyfb #4 */
    printf("--- riempio BIANCO + dirtyfb #4:\n");
    fill(0x00FFFFFF);
    dirtyfb(fb);
    printf("fine (hold 8s)\n"); fflush(stdout);
    sleep(8);
    return 0;
}
