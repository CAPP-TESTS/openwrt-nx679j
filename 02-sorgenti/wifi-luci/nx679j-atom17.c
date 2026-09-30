// nx679j-atom11 — IL TEST DECISIVO: commit completo ripetuto (cnp) con fb diverso.
// Basato su atom10 (che FUNZIONA: TEST_ONLY=0 REAL=0). Sequenza: commit ROSSO -> sleep -> commit VERDE -> sleep -> commit BLU -> hold.
// atom14: flip loop ATOMIC "plane-only" (la ricetta SDM: SetupAtomic setta solo
// PLANE_SET_FB_ID + PLANE_SET_CRTC per i frame successivi; crtc/conn SOLO al primo).
// Commit 1 = cnp completo. Poi N commit con SOLO il plane (FB_ID nuovo).
// atom17: FLIP LOOP REALISTICO — 4 fb in rotazione, N commit, gap 30ms.
// argv: [ncommits] [hold] [gap_us]. Log rawdump slot 442.
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
struct get_property { u64 values_ptr, enum_blob_ptr; u32 prop_id, flags; char name[32]; u32 count_values, count_enum_blobs; };
struct create_blob { u64 data; u32 length, blob_id; };
struct atomic_req { u32 flags, count_objs; u64 objs_ptr, count_props_ptr, props_ptr, prop_values_ptr, reserved, user_data; };
struct set_client_cap { u64 capability, value; };

#define GETCRTC        0xc06864a1
#define GETCONNECTOR   0xc05064a7
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define GETPROPERTY    0xc04064aa
#define CREATE_BLOB    0xc01064bd
#define ATOMIC         0xc03864bc
#define SET_MASTER     0x641e
#define SET_CLIENT_CAP 0x4010640d
#define PAGE_FLIP      0xb0
struct page_flip { u32 crtc_id, fb_id, flags, reserved; u64 user_data; };

#define SLOT 442
static int fd; static u32 W=1080,H=2400;
static char logbuf[32768]; static size_t loglen=0;
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

static u32 find_prop_global(const char *name) {
    for (u32 id=1; id<500; id++) {
        struct get_property gp; memset(&gp,0,sizeof(gp)); gp.prop_id=id;
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return id;
    }
    return 0;
}

static u32 make_fb(u32 color, u32 *out_pitch) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd)); cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { L("createdumb errno=%d", errno); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { L("mapdumb errno=%d", errno); return 0; }
    unsigned char *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m==MAP_FAILED){ L("mmap errno=%d", errno); return 0; }
    for (u32 y=0;y<H;y++){ u32 *row=(u32*)(m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { L("addfb errno=%d", errno); return 0; }
    if (out_pitch) *out_pitch=cd.pitch;
    L("fb=%u color=%06x", fc.fb_id, color);
    return fc.fb_id;
}

static u32 conn=-1, crtc=0, plane=103, modeid_id, active_id, conn_crtcid, ar_id;
static u32 p_crtcid, p_fbid, p_srcw, p_srch, p_srcx, p_srcy, p_crtcw, p_crtch, p_crtcx, p_crtcy;
static u32 ftm_id; static int use_posted=0;
static struct modeinfo best; static u32 blob_id;

static int commit_plane_only(u32 fbid) {
    u32 objs[1]; u32 counts[1]; u32 props[16]; u64 vals[16]; int np=0;
    props[np]=p_crtcid; vals[np++]=crtc;
    props[np]=p_fbid; vals[np++]=fbid;
    props[np]=p_srcw; vals[np++]=((u64)W)<<16;
    props[np]=p_srch; vals[np++]=((u64)H)<<16;
    props[np]=p_srcx; vals[np++]=0;
    props[np]=p_srcy; vals[np++]=0;
    props[np]=p_crtcw; vals[np++]=W;
    props[np]=p_crtch; vals[np++]=H;
    props[np]=p_crtcx; vals[np++]=0;
    props[np]=p_crtcy; vals[np++]=0;
    counts[0]=np; objs[0]=plane;
    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=0x400|0x01; ar.count_objs=1;   /* ALLOW_MODESET | PAGE_FLIP_EVENT */
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0;
    int r = ioctl(fd, ATOMIC, &ar);
    if (!r) {
        /* leggi l evento pageflip (struct drm_event_page_flip: 16B hdr-less... usiamo read di 32B) */
        char ev[64]; ssize_t n = read(fd, ev, sizeof(ev));
        L("plane-only %u: commit OK, evento read=%d", fbid, (int)n);
    }
    return r? -errno : 0;
}

static int commit_cnp(u32 fbid, int test_only, int full) {
    u32 objs[3]; u32 counts[3]; u32 props[32]; u64 vals[32]; int np=0, no=0;
    int c0;
    if (full == 2) {   /* full: MODE_ID + ACTIVE (primo commit) */
        c0=np; props[np]=modeid_id; vals[np++]=blob_id;
        props[np]=active_id; vals[np++]=1;
        counts[no]=np-c0; objs[no++]=crtc;
    } else if (full == 1) {  /* lazy: MODE_ID senza ACTIVE (successivi) */
        c0=np; props[np]=modeid_id; vals[np++]=blob_id;
        counts[no]=np-c0; objs[no++]=crtc;
    }
    c0=np; props[np]=conn_crtcid; vals[np++]=crtc;
    if (ar_id) { props[np]=ar_id; vals[np++]=0; }
    if (use_posted && ftm_id) { props[np]=ftm_id; vals[np++]=2; }
    counts[no]=np-c0; objs[no++]=conn;
    c0=np; props[np]=p_crtcid; vals[np++]=crtc;
    props[np]=p_fbid; vals[np++]=fbid;
    props[np]=p_srcw; vals[np++]=((u64)W)<<16;
    props[np]=p_srch; vals[np++]=((u64)H)<<16;
    props[np]=p_srcx; vals[np++]=0;
    props[np]=p_srcy; vals[np++]=0;
    props[np]=p_crtcw; vals[np++]=W;
    props[np]=p_crtch; vals[np++]=H;
    props[np]=p_crtcx; vals[np++]=0;
    props[np]=p_crtcy; vals[np++]=0;
    counts[no]=np-c0; objs[no++]=plane;

    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=(test_only? (0x400|0x100) : 0x400); ar.count_objs=no;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0;
    int r = ioctl(fd, ATOMIC, &ar);
    return r? -errno : 0;
}

int main(int argc, char **argv) {
    int nframes = argc>1?atoi(argv[1]):3;
    L("=== atom11 start (pid %d, nframes=%d) ===", getpid(), nframes);
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd<0){ L("open errno=%d", errno); return 1; }
    if (ioctl(fd,SET_MASTER) && errno!=16) L("setmaster errno=%d", errno);
    { struct set_client_cap c = { 3, 1 }; if (ioctl(fd, SET_CLIENT_CAP, &c)) L("cap FAIL errno=%d", errno); else L("cap ok"); }

    struct modeinfo modes[40];
    for (u32 i=0;i<256 && (int)conn<0;i++) {
        struct get_conn c; memset(&c,0,sizeof(c)); c.connector_id=i;
        if (ioctl(fd,GETCONNECTOR,&c)) continue;
        if (c.connector_type!=16 || c.connection!=1) continue;
        struct get_conn c2; memset(&c2,0,sizeof(c2)); c2.connector_id=i;
        c2.modes_ptr=(u64)(uintptr_t)modes; c2.count_modes=40;
        if (ioctl(fd,GETCONNECTOR,&c2)) continue;
        conn=i; best=modes[0];
        for (u32 m=0;m<c2.count_modes && m<40;m++) if (modes[m].type&8) best=modes[m];
    }
    u32 crtcs[16]; int ncr=0;
    for (u32 ci=0;ci<256 && ncr<16;ci++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=ci; if(!ioctl(fd,GETCRTC,&cc)) crtcs[ncr++]=ci; }
    crtc = crtcs[0];
    for (int k=0;k<ncr;k++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=crtcs[k]; if(!ioctl(fd,GETCRTC,&cc) && cc.mode_valid){ crtc=crtcs[k]; break; } }
    L("conn=%d crtc=%u plane=%u", (int)conn, crtc, plane);

    modeid_id = find_prop_global("MODE_ID"); active_id = find_prop_global("ACTIVE");
    conn_crtcid = find_prop_global("CRTC_ID"); ar_id = find_prop_global("autorefresh");
    ftm_id = find_prop_global("frame_trigger_mode");
    use_posted = (argc>2 && atoi(argv[2]));
    L("ftm=%u use_posted=%d", ftm_id, use_posted);
    p_crtcid = find_prop_global("CRTC_ID"); p_fbid = find_prop_global("FB_ID");
    p_srcw = find_prop_global("SRC_W"); p_srch = find_prop_global("SRC_H");
    p_srcx = find_prop_global("SRC_X"); p_srcy = find_prop_global("SRC_Y");
    p_crtcw = find_prop_global("CRTC_W"); p_crtch = find_prop_global("CRTC_H");
    p_crtcx = find_prop_global("CRTC_X"); p_crtcy = find_prop_global("CRTC_Y");
    L("ids ok (modeid=%u active=%u)", modeid_id, active_id);

    struct create_blob cb; memset(&cb,0,sizeof(cb)); cb.data=(u64)(uintptr_t)&best; cb.length=sizeof(best);
    if (ioctl(fd, CREATE_BLOB, &cb)) { L("blob errno=%d", errno); return 5; }
    blob_id = cb.blob_id;
    L("blob=%u", blob_id);

    u32 colors[8] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00, 0x00FF00FF, 0x0000FFFF, 0x00FFFFFF, 0x00808080 };
    int gap_us = argc>4?atoi(argv[4]):30000;
    L("gap_us=%d", gap_us);
    u32 pitches[64]; u32 fbs[64];
    int NF = 4;  /* 4 fb in rotazione (come un compositor) */
    if (nframes > NF) nframes = nframes;  /* nframes = numero commit */
    for (int i=0;i<NF;i++) { fbs[i] = make_fb(colors[i%8], &pitches[i]); if(!fbs[i]) return 4; }

    /* Primo: commit cnp completo (accende il crtc) */
    L("--- commit 1/%d (cnp): fb=%u color=%06x", nframes, fbs[0], colors[0]);
    int r = commit_cnp(fbs[0], 0, 2);
    if (r) { L("commit 1 (cnp) FALLITO errno=%d", -r); return 6; }
    L("commit 1 (cnp) OK");
    if (gap_us>0) usleep(gap_us);
    /* Loop: commit ATOMIC del solo plane con fb in rotazione (ricetta SDM) */
    int okc = 0;
    for (int i=1;i<nframes;i++) {
        u32 fb = fbs[i % NF];
        int pr = commit_plane_only(fb);
        if (pr) { L("plane-only %d/%d FALLITO (fb=%u) errno=%d", i+1, nframes, fb, -pr); return 7; }
        okc++;
        if ((i%10)==0 || i==nframes-1) L("flip %d/%d OK (fb=%u)", i, nframes-1, fb);
        if (gap_us>0) usleep(gap_us);
    }
    L("TUTTI %d FLIP OK", okc);
    int hold = argc>3?atoi(argv[3]):6;
    L("TUTTI I %d COMMIT OK — hold %ds (PROCESSO VIVO)", nframes, hold);
    for (int h=0; h<hold; h++) { sleep(1); if ((h%30)==29) L("vivo da %ds", h+1); }
    L("fine");
    return 0;
}
