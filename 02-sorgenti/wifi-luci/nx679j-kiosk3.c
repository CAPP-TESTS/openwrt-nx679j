// nx679j-atom11 — IL TEST DECISIVO: commit completo ripetuto (cnp) con fb diverso.
// Basato su atom10 (che FUNZIONA: TEST_ONLY=0 REAL=0). Sequenza: commit ROSSO -> sleep -> commit VERDE -> sleep -> commit BLU -> hold.
// atom14: flip loop ATOMIC "plane-only" (la ricetta SDM: SetupAtomic setta solo
// PLANE_SET_FB_ID + PLANE_SET_CRTC per i frame successivi; crtc/conn SOLO al primo).
// Commit 1 = cnp completo. Poi N commit con SOLO il plane (FB_ID nuovo).
// atom18-kiosk: flip loop infinito + animazione incrementale + touch (event0).
// Log rawdump slot 443.
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
#include <poll.h>
#include <sys/time.h>

static int now_ms(void) { struct timeval tv; gettimeofday(&tv,NULL); return (int)(tv.tv_sec*1000 + tv.tv_usec/1000); }

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

#define SLOT 443
struct input_event { u64 sec, usec; uint16_t type, code; int32_t value; };
#define EV_ABS 3
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
static u32 *kmaps[8]; static u32 kfbs[8]; static int kidx = 0;
static int tfd = -1;
static int t_x = 540, t_y = 600, t_down = 0;
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
    if (kidx < 8) { kmaps[kidx] = (u32*)m; }
    for (u32 y=0;y<H;y++){ u32 *row=(u32*)(m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { L("addfb errno=%d", errno); return 0; }
    if (out_pitch) *out_pitch=cd.pitch;
    if (kidx < 8) { kfbs[kidx] = fc.fb_id; kidx++; }
    L("fb=%u color=%06x", fc.fb_id, color);
    return fc.fb_id;
}

static u32 conn=-1, crtc=0, plane=103, modeid_id, active_id, conn_crtcid, ar_id;
static u32 p_crtcid, p_fbid, p_srcw, p_srch, p_srcx, p_srcy, p_crtcw, p_crtch, p_crtcx, p_crtcy;
static u32 ftm_id; static int use_posted=0;
static struct modeinfo best; static u32 blob_id;

/* FASE 6 (design Astra): flip NONBLOCK + OUT_FENCE_PTR, un commit in volo,
 * completamento verificato con poll() time-bounded. Nessun ALLOW_MODESET. */
static u32 out_fence_id = 0;   /* CRTC prop OUT_FENCE_PTR */

static int commit_plane_only(u32 fbid, int *fence_out) {
    u32 objs[2]; u32 counts[2]; u32 props[24]; u64 vals[24]; int np=0, no=0, c0;
    int fence_fd = -1;
    if (out_fence_id) {
        c0=np; props[np]=out_fence_id; vals[np++]=(u64)(uintptr_t)&fence_fd;
        counts[no]=np-c0; objs[no++]=crtc;
    }
    c0=np;
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
    counts[no]=np-c0; objs[no++]=plane;

    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=0x200;                 /* NONBLOCK: l'ioctl non attende l'hw */
    ar.count_objs=no;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0;
    int r = ioctl(fd, ATOMIC, &ar);
    if (fence_out) *fence_out = fence_fd;
    else if (fence_fd >= 0) close(fence_fd);
    return r? -errno : 0;
}

/* 0 = completato, 1 = timeout, -1 = errore */
static int wait_fence(int ffd, int ms) {
    if (ffd < 0) return 1;
    struct pollfd p; p.fd=ffd; p.events=POLLIN; p.revents=0;
    int pr = poll(&p, 1, ms);
    close(ffd);
    if (pr > 0) return 0;
    if (pr == 0) return 1;
    return -1;
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


static void poll_touch(void) {
    if (tfd < 0) return;
    struct input_event ev;
    while (read(tfd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X) t_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y) t_y = ev.value;
            else if (ev.code == ABS_MT_TRACKING_ID) {
                if (ev.value >= 0) { t_down = 1; L("touch DOWN x=%d y=%d", t_x, t_y); }
                else { t_down = 0; L("touch UP x=%d y=%d", t_x, t_y); }
            }
        }
    }
}

static u32 K_BG = 0x00101828;
static u32 *k_buf[2];       /* i due buffer (mmap) */
static u32 k_fb[2];         /* i fb id */
static u32 k_pitch[2];      /* pitch in bytes returned by CREATE_DUMB */
static int prev_bx[2] = {-1,-1}, prev_cx[2] = {-1,-1}, prev_cy[2] = {-1,-1};

static void fill_rect(u32 *m, u32 pitch_bytes, int x0, int y0, int w, int h, u32 col) {
    unsigned char *base = (unsigned char *)m;
    for (int y=y0; y<y0+h; y++) {
        if (y<0||(u32)y>=H) continue;
        u32 *row = (u32 *)(base + (size_t)y * pitch_bytes);
        for (int x=x0; x<x0+w; x++) { if (x<0||(u32)x>=W) continue; row[x] = col; }
    }
    __sync_synchronize();
}

static void draw_inc(u32 *m, u32 pitch_bytes, int fr, int bi) {
    int bx = (fr * 36) % (W - 240);
    if (prev_bx[bi] >= 0) fill_rect(m, pitch_bytes, prev_bx[bi], 300, 240, 160, K_BG);
    fill_rect(m, pitch_bytes, bx, 300, 240, 160, 0x00ff8020);
    prev_bx[bi] = bx;
    if (prev_cx[bi] >= 0) fill_rect(m, pitch_bytes, prev_cx[bi]-40, prev_cy[bi]-40, 80, 80, K_BG);
    fill_rect(m, pitch_bytes, t_x-40, t_y-40, 80, 80, t_down ? 0x00ff2020 : 0x0020ff40);
    prev_cx[bi] = t_x; prev_cy[bi] = t_y;
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
    out_fence_id = find_prop_global("OUT_FENCE_PTR");
    L("ids ok (modeid=%u active=%u outfence=%u)", modeid_id, active_id, out_fence_id);

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

    /* Validazione (Astra): TEST_ONLY del commit completo, NON modifica lo stato */
    L("--- TEST_ONLY cnp: rc=%d (0 = validato)", commit_cnp(fbs[0], 1, 2));

    /* Primo: commit cnp completo (accende il crtc) */
    L("--- commit 1/%d (cnp): fb=%u color=%06x", nframes, fbs[0], colors[0]);
    int r = commit_cnp(fbs[0], 0, 2);
    if (r) { L("commit 1 (cnp) FALLITO errno=%d", -r); return 6; }
    L("commit 1 (cnp) OK");
    if (gap_us>0) usleep(gap_us);
    /* KIOSK: loop infinito con animazione + touch */
    k_buf[0]=kmaps[0]; k_fb[0]=kfbs[0];
    k_buf[1]=kmaps[1]; k_fb[1]=kfbs[1];
    k_pitch[0]=pitches[0]; k_pitch[1]=pitches[1];
    for (int b=0;b<2;b++) {
        size_t visible_bytes = (size_t)W * sizeof(u32);
        if (k_pitch[b] < visible_bytes || k_pitch[b] % sizeof(u32)) {
            L("invalid pitch fb=%u pitch=%u visible=%zu", k_fb[b], k_pitch[b], visible_bytes);
            return 8;
        }
        for (u32 y=0;y<H;y++) {
            unsigned char *row_bytes = (unsigned char *)k_buf[b] + (size_t)y * k_pitch[b];
            u32 *row = (u32 *)row_bytes;
            for (u32 x=0;x<W;x++) row[x]=K_BG;
            memset(row_bytes + visible_bytes, 0, k_pitch[b] - visible_bytes);
        }
    }
    /* prev arrays gia inizializzati */
    draw_inc(k_buf[1], k_pitch[1], 0, 1);
    tfd = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
    L("touch fd=%d", tfd);
    /* FASE 6: flip NONBLOCK + fence con timeout, un commit in volo, mai idle >58 ms */
    int cur = 0, fr = 0, to = 0, to_tot = 0;
    int t0 = now_ms(), tlast = t0, gap_max = 0;
    L("loop NONBLOCK avviato");
    for (;;) {
        poll_touch();
        int nxt = 1-cur;
        draw_inc(k_buf[nxt], k_pitch[nxt], fr, nxt);
        int ffd = -1;
        int pr = commit_plane_only(k_fb[nxt], &ffd);
        if (pr) {
            L("flip %d IOCTL FALLITO errno=%d -> STOP (contenimento: non insisto)", fr, -pr);
            break;
        }
        int w = wait_fence(ffd, 100);
        if (w == 1) {
            to++; to_tot++;
            L("flip %d: FENCE TIMEOUT (%d consecutivi)", fr, to);
            if (to >= 5) { L("STOP: 5 fence timeout consecutivi (contenimento Astra)"); break; }
        } else if (w < 0) {
            L("flip %d: poll errno=%d", fr, errno);
        } else {
            to = 0;
        }
        cur = nxt; fr++;
        { int tn = now_ms(); int g = tn - tlast; if (g > gap_max) gap_max = g; tlast = tn; }
        if ((fr % 300) == 0) {
            int dt = now_ms() - t0;
            L("TELEMETRIA frame=%d fps=%.1f gap_max_ms=%d fence_timeout_tot=%d",
              fr, dt>0 ? (fr*1000.0/dt) : 0.0, gap_max, to_tot);
        }
        usleep(16000);
    }
    L("=== kiosk STOPPED dopo %d frame (processo vivo, display NON piu' aggiornato) ===", fr);
    for (;;) sleep(60);
    return 0;
}
