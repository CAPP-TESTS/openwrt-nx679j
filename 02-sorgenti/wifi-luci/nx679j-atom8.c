// nx679j-atom8 — TEST CRITICO: 2 setcrtc nello STESSO processo (fb diverso). IL LOG VA SU RAWDUMp (slot 431)!
// Fasi: "2setcrtc" = setcrtc#1(ROSSO) -> sleep3 -> setcrtc#2(VERDE) -> hold
//        "dirty"    = setcrtc(ROSSO) -> dirtyfb#1 -> riempi VERDE -> dirtyfb#2 -> hold
//        "check"    = solo setcrtc(ROSSO) -> hold
// Il log sopravvive al reboot: dd if=/proc/1/root/dev/rd bs=32768 skip=$((32+431)) count=1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
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
#define DROP_MASTER    0x641f

#define SLOT 431
static int fd;
static u32 W = 1080, H = 2400;

/* log buffer + flush su rawdump a ogni riga */
static char logbuf[32768]; static size_t loglen = 0;
static void L(const char *fmt, ...) {
    char tmp[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n>0) {
        printf("%s\n", tmp); fflush(stdout);
        if (loglen + (size_t)n + 1 < sizeof(logbuf)) { memcpy(logbuf+loglen, tmp, n); loglen+=(size_t)n; logbuf[loglen++]='\n'; logbuf[loglen]=0; }
        int rd = open("/proc/1/root/dev/rd", O_WRONLY);
        if (rd>=0){ lseek(rd, (off_t)(32+SLOT)*32768, SEEK_SET); ssize_t w = write(rd, logbuf, loglen); (void)w; close(rd); }
    }
}

static unsigned char *last_buf = NULL; static u32 last_pitch = 0;

static u32 make_fb(u32 color, unsigned char **outbuf, u32 *outpitch) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd));
    cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { L("create dumb errno=%d", errno); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { L("map dumb errno=%d", errno); return 0; }
    unsigned char *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { L("mmap errno=%d", errno); return 0; }
    for (u32 y=0;y<H;y++) { u32 *row=(u32*)(m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { L("addfb errno=%d", errno); return 0; }
    L("fb=%u pitch=%u (color %06x)", fc.fb_id, cd.pitch, color);
    if(outbuf)*outbuf=m; if(outpitch)*outpitch=cd.pitch;
    return fc.fb_id;
}

static void fill(unsigned char *m, u32 pitch, u32 color) {
    for (u32 y=0;y<H;y++) { u32 *row=(u32*)(m+(size_t)y*pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
}

static u32 conn_id=-1, crtc_id=0; static struct modeinfo best; static u32 crtcs[16]; static int ncr=0;

static int setcrtc_now(u32 fbid) {
    for (int k=0;k<ncr;k++) {
        struct crtc set; memset(&set,0,sizeof(set));
        u32 cb[1]={ (u32)conn_id };
        set.set_connectors_ptr=(u64)(uintptr_t)cb; set.count_connectors=1;
        set.crtc_id=crtcs[k]; set.fb_id=fbid; set.mode_valid=1; set.mode=best;
        errno=0;
        if (!ioctl(fd, SETCRTC, &set)) { crtc_id=crtcs[k]; L("SETCRTC OK crtc=%u fb=%u", crtc_id, fbid); return 0; }
        L("  setcrtc crtc %u errno=%d(%s)", crtcs[k], errno, strerror(errno));
    }
    return -1;
}

int main(int argc, char **argv) {
    const char *fase = argc>1?argv[1]:"check";
    L("=== atom8 fase %s start (pid %d) ===", fase, getpid());

    fd = open("/dev/dri/card0", O_RDWR);
    if (fd<0){ L("open errno=%d", errno); return 1; }
    errno=0;
    if (ioctl(fd,SET_MASTER) && errno!=16) L("setmaster errno=%d", errno);
    L("master ok (errno_setmaster=%d)", errno);

    struct modeinfo modes[40];
    for (u32 i=0;i<256 && (int)conn_id<0;i++) {
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
    if((int)conn_id<0){ L("conn assente"); return 2; }
    L("conn=%u mode='%s' (%ux%u@%u)", conn_id, best.name, best.hdisplay, best.vdisplay, best.vrefresh);

    for (u32 ci=0;ci<256 && ncr<16;ci++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=ci; if(!ioctl(fd,GETCRTC,&cc)) crtcs[ncr++]=ci; }
    L("crtc trovati: %d", ncr);

    unsigned char *bR=NULL,*bG=NULL; u32 pR=0,pG=0;
    u32 fbR = make_fb(0x00FF0000, &bR, &pR);   /* ROSSO */
    if(!fbR) return 3;

    L("--- FASE 1: setcrtc fbR");
    if (setcrtc_now(fbR)) return 4;
    L("attesa 4s (guarda la webcam: ROSSO)");
    sleep(4);

    if (!strcmp(fase,"check")) { L("fine check (hold 5s)"); sleep(5); return 0; }

    if (!strcmp(fase,"2setcrtc")) {
        u32 fbG = make_fb(0x0000FF00, &bG, &pG);   /* VERDE */
        if(!fbG) return 5;
        L("--- FASE 2: setcrtc fbG=%u (STESSO processo)", fbG);
        if (setcrtc_now(fbG)) { L("setcrtc#2 FALLITO"); return 6; }
        L("setcrtc#2 accettato! attesa 6s (guarda la webcam: VERDE?)");
        sleep(6);
        L("fine 2setcrtc (hold 5s)");
        sleep(5);
        return 0;
    }

    if (!strcmp(fase,"dirty")) {
        L("--- FASE 2: dirtyfb#1 (senza modifiche)");
        struct dirty_cmd d; memset(&d,0,sizeof(d));
        static struct clip_rect clips[1] = { {0,0,1080,2400} };
        d.fb_id=fbR; d.num_clips=1; d.clips_ptr=(u64)(uintptr_t)clips;
        errno=0;
        int r1 = ioctl(fd, DIRTYFB, &d);
        L("dirtyfb#1 ret=%d errno=%d(%s)", r1, errno, strerror(errno));
        sleep(2);
        L("--- riempio VERDE + dirtyfb#2");
        fill(bR, pR, 0x0000FF00);
        errno=0;
        int r2 = ioctl(fd, DIRTYFB, &d);
        L("dirtyfb#2 ret=%d errno=%d(%s)", r2, errno, strerror(errno));
        L("attesa 6s (guarda la webcam: VERDE?)");
        sleep(6);
        L("fine dirty (hold 5s)");
        sleep(5);
        return 0;
    }

    L("fase sconosciuta");
    return 7;
}
