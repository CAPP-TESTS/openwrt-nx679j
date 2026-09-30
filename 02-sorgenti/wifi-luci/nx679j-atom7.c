// nx679j-atom7 — test a FASI SEPARATE con log persistente (/usr/lib/nx679j/modem/atom7.log).
// Fasi: a=setcrtc | b=+setplane | c=+pageflip(event) | d=+pageflip(no event)
// Ogni fase fa log PRIMA e DOPO l'operazione: se il device crasha, il log dice dove.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <stdarg.h>

typedef uint32_t u32; typedef uint64_t u64;

struct modeinfo { u32 clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; u32 vrefresh, flags, type; char name[32]; };
struct get_conn { u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr; u32 count_modes, count_props, count_encoders, encoder_id, connector_id, connector_type, connector_type_id, connection, mm_w, mm_h, subpixel, pad; };
struct crtc { u64 set_connectors_ptr; u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid; struct modeinfo mode; };
struct create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; };
struct map_dumb { u32 handle, pad; u64 offset; };
struct fb_cmd { u32 fb_id, width, height, pitch, bpp, depth, handle; };
struct set_plane { u32 plane_id, crtc_id, fb_id, crtc_x, crtc_y, crtc_w, crtc_h, src_x, src_y, src_w, src_h; };
struct page_flip { u32 crtc_id, fb_id, flags, reserved; u64 user_data; };

#define GETCRTC        0xc06864a1
#define SETCRTC        0xc06864a2
#define GETCONNECTOR   0xc05064a7
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define SETPLANE       0xc02c64b7
#define PAGE_FLIP      0xc01864b0
#define SET_MASTER     0x641e
#define PF_EVENT       0x01

#define LOG "/usr/lib/nx679j/modem/atom7.log"

static int fd;
static FILE *lf;
static u32 W = 1080, H = 2400;

static void L(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(lf, fmt, ap); va_end(ap);
    fprintf(lf, "\n"); fflush(lf);
}

static u32 make_fb(u32 color) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd));
    cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { L("create dumb errno=%d", errno); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { L("map dumb errno=%d", errno); return 0; }
    u32 *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { L("mmap errno=%d", errno); return 0; }
    for (u32 y=0;y<H;y++) { u32 *row=(u32*)((unsigned char *)m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { L("addfb errno=%d", errno); return 0; }
    L("fb creato = %u (pitch %u)", fc.fb_id, cd.pitch);
    return fc.fb_id;
}

int main(int argc, char **argv) {
    char fase = (argc>1 && argv[1][0]) ? argv[1][0] : 'a';
    int gap = argc>2?atoi(argv[2]):4;
    lf = fopen(LOG, "w");
    if (!lf) return 9;
    L("=== atom7 fase %c start (pid %d) ===", fase, getpid());

    fd = open("/dev/dri/card0", O_RDWR);
    if (fd<0){ L("open errno=%d", errno); return 1; }
    if (ioctl(fd,SET_MASTER) && errno!=16) L("setmaster errno=%d", errno);
    L("master ok");

    int conn_id=-1; struct modeinfo modes[40]; u32 nmodes=0; struct modeinfo best; memset(&best,0,sizeof(best));
    for (u32 i=0;i<256 && conn_id<0;i++) {
        struct get_conn c; memset(&c,0,sizeof(c)); c.connector_id=i;
        if (ioctl(fd,GETCONNECTOR,&c)) continue;
        if (c.connector_type!=16 || c.connection!=1) continue;
        struct get_conn c2; memset(&c2,0,sizeof(c2)); c2.connector_id=i;
        c2.modes_ptr=(u64)(uintptr_t)modes; c2.count_modes=40;
        if (ioctl(fd,GETCONNECTOR,&c2)) continue;
        conn_id=i; nmodes=c2.count_modes; if(nmodes>40)nmodes=40;
        best=modes[0];
        for (u32 m=0;m<nmodes;m++) if (modes[m].type&8) best=modes[m];
    }
    if(conn_id<0){ L("conn assente"); return 2; }
    L("conn=%d best='%s'", conn_id, best.name);

    u32 crtcs[16]; int ncr=0;
    for (u32 ci=0;ci<256 && ncr<16;ci++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=ci; if(!ioctl(fd,GETCRTC,&cc)) crtcs[ncr++]=ci; }
    u32 crtc_id=0;
    for (int k=0;k<ncr;k++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=crtcs[k]; if(!ioctl(fd,GETCRTC,&cc) && cc.mode_valid){ crtc_id=crtcs[k]; break; } }
    if(!crtc_id && ncr) crtc_id=crtcs[0];
    if(!crtc_id){ L("no crtc"); return 3; }
    L("crtc=%u", crtc_id);

    L("--- fb:");
    u32 fbR=make_fb(0x00FF0000), fbG=make_fb(0x0000FF00), fbB=make_fb(0x000000FF);
    if(!fbR||!fbG||!fbB){ L("fb fail"); return 4; }

    /* FASE A: setcrtc (fbR) */
    L("--- FASE A: setcrtc fbR=%u", fbR); fflush(lf);
    int ok=0;
    for (int k=0;k<ncr && !ok;k++) {
        struct crtc set; memset(&set,0,sizeof(set));
        u32 cb[1]={ (u32)conn_id };
        set.set_connectors_ptr=(u64)(uintptr_t)cb; set.count_connectors=1;
        set.crtc_id=crtcs[k]; set.fb_id=fbR; set.mode_valid=1; set.mode=best;
        L("setcrtc su crtc %u...", crtcs[k]); fflush(lf);
        errno=0;
        if (!ioctl(fd, SETCRTC, &set)) { ok=1; crtc_id=crtcs[k]; L("SETCRTC OK crtc=%u (atteso ROSSO)", crtc_id); }
        else L("setcrtc errno=%d(%s)", errno, strerror(errno));
    }
    if(!ok){ L("setcrtc fallito, stop"); return 5; }
    if (fase=='a') { L("fase a ok, hold 5s"); sleep(5); L("fine"); return 0; }
    sleep(gap);

    /* FASE B: setplane fbG */
    if (fase>='b') {
        L("--- FASE B: setplane fbG=%u sul plane primario", fbG); fflush(lf);
        /* trova il plane primario: scan plane 0-255, GETPLANE per id -> il primo con possible_crtcs che copre il nostro crtc.
           Semplice: usiamo l'euristica vista sul debugfs: plane con id piu' basso tra quelli del crtc.
           In pratica: proviamo i plane id noti della lista debugfs: 78,103,106,109,112,118... 
           Ma non abbiamo GETPLANE qui: usiamo direttamente il setplane su TUTTI i candidate finche' uno passa! */
        u32 candidates[20]; int nc=0;
        { /* generiamo i candidati: gli id tra 70 e 160 via GETPLANE non disponibile... usiamo bruting leggero */
          /* Getplane: struct {u32 plane_id, crtc_id, fb_id, possible_crtcs, gamma_size, count_format_types; u64 format_type_ptr;} */
          struct { u32 plane_id, crtc_id, fb_id, possible_crtcs, gamma_size, count_format_types; u64 format_type_ptr; } gp;
          for (u32 pi=1; pi<256 && nc<20; pi++) {
            memset(&gp,0,sizeof(gp)); gp.plane_id=pi;
            if (!ioctl(fd, 0xc02064b6 /*GETPLANE*/, &gp)) {
                candidates[nc++]=pi;
            }
          }
          L("plane candidati: %d", nc);
        }
        int done=0;
        for (int ci=0; ci<nc && !done; ci++) {
            struct set_plane sp; memset(&sp,0,sizeof(sp));
            sp.plane_id=candidates[ci]; sp.crtc_id=crtc_id; sp.fb_id=fbG;
            sp.crtc_x=0; sp.crtc_y=0; sp.crtc_w=W; sp.crtc_h=H;
            sp.src_x=0; sp.src_y=0; sp.src_w=W<<16; sp.src_h=H<<16;
            L("setplane plane=%u...", candidates[ci]); fflush(lf);
            errno=0;
            if (!ioctl(fd, SETPLANE, &sp)) { done=1; L("SETPLANE OK plane=%u (atteso VERDE)", candidates[ci]); }
            else L("  setplane plane=%u errno=%d(%s)", candidates[ci], errno, strerror(errno));
        }
        if(!done) L("setplane: nessun plane accettato");
        if (fase=='b') { L("fase b ok, hold 5s"); sleep(5); L("fine"); return 0; }
        sleep(gap);
    }

    /* FASE C: pageflip con evento verso fbB */
    if (fase>='c') {
        L("--- FASE C: pageflip fbB=%u (con evento)", fbB); fflush(lf);
        struct page_flip pf; memset(&pf,0,sizeof(pf));
        pf.crtc_id=crtc_id; pf.fb_id=fbB; pf.flags=PF_EVENT; pf.user_data=0x777;
        errno=0;
        if (ioctl(fd, PAGE_FLIP, &pf)) { L("pageflip FAIL errno=%d(%s)", errno, strerror(errno)); }
        else {
            L("pageflip accettato, attendo evento (2s)...");
            struct pollfd pfd = { fd, POLLIN, 0 };
            int pr = poll(&pfd, 1, 2000);
            if (pr > 0) { char buf[256]; int n = read(fd, buf, sizeof(buf)); L("EVENTO ricevuto (%d bytes): frame done ok!", n); }
            else L("nessun evento in 2s (pr=%d, errno=%d)", pr, errno);
        }
        if (fase=='c') { L("fase c ok, hold 5s"); sleep(5); L("fine"); return 0; }
        sleep(gap);
    }

    /* FASE D: pageflip senza evento */
    L("--- FASE D: pageflip senza evento"); fflush(lf);
    struct page_flip pf2; memset(&pf2,0,sizeof(pf2));
    pf2.crtc_id=crtc_id; pf2.fb_id=fbR; pf2.flags=0; pf2.user_data=0;
    errno=0;
    if (ioctl(fd, PAGE_FLIP, &pf2)) L("pageflip-noev FAIL errno=%d(%s)", errno, strerror(errno));
    else L("pageflip-noev accettato (atteso ROSSO)");
    L("hold 5s"); sleep(5);
    L("fine");
    return 0;
}
