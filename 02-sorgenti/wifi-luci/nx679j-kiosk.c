// nx679j-kiosk.c — Kiosk minimo NX679J: flip loop 60fps + touch feedback.
// Formula verificata: SET_CLIENT_CAP(ATOMIC) + commit 1 cnp + commit plane-only con gap < 58ms.
// Log: rawdump slot 443.  Build: aarch64-openwrt-linux-musl-gcc -O2 -static -o nx679j-kiosk *.c
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
#include <sys/select.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int32_t s32;

struct modeinfo { u32 clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; u32 vrefresh, flags, type; char name[32]; };
struct get_conn { u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr; u32 count_modes, count_props, count_encoders, encoder_id, connector_id, connector_type, connector_type_id, connection, mm_w, mm_h, subpixel, pad; };
struct create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; };
struct map_dumb { u32 handle, pad; u64 offset; };
struct fb_cmd { u32 fb_id, width, height, pitch, bpp, depth, handle; };
struct get_property { u64 values_ptr, enum_blob_ptr; u32 prop_id, flags; char name[32]; u32 count_values, count_enum_blobs; };
struct create_blob { u64 data; u32 length, blob_id; };
struct atomic_req { u32 flags, count_objs; u64 objs_ptr, count_props_ptr, props_ptr, prop_values_ptr, reserved, user_data; };
struct set_client_cap { u64 capability, value; };
struct input_event { u64 sec, usec; u16 type, code; s32 value; };

#define GETCONNECTOR   0xc05064a7
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define GETPROPERTY    0xc04064aa
#define CREATE_BLOB    0xc01064bd
#define ATOMIC         0xc03864bc
#define SET_MASTER     0x641e
#define SET_CLIENT_CAP 0x4010640d
#define EV_ABS 3
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39

#define SLOT 443
#define W 1080
#define H 2400

static int fd = -1;
static int tfd = -1;

static void L(const char *fmt, ...) {
    char tmp[256]; va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    int rd = open("/proc/1/root/dev/rd", O_WRONLY);
    if (rd >= 0) {
        off_t off = (off_t)(32 + SLOT) * 32768;
        char buf[512]; int n = snprintf(buf, sizeof(buf), "%s\n", tmp);
        pwrite(rd, buf, n, off);
        close(rd);
    }
    printf("%s\n", tmp); fflush(stdout);
}

static u32 conn, crtc = 152, plane = 103, modeid_id, active_id, conn_crtcid, ar_id;
static u32 p_crtcid, p_fbid, p_srcw, p_srch, p_srcx, p_srcy, p_crtcw, p_crtch, p_crtcx, p_crtcy;
static struct modeinfo best;
static u32 blob_id;
static u64 fbs[4];
static u32 *maps[4];

static u32 findprop(u32 obj, const char *name) {
    for (u32 id = 1; id < 400; id++) {
        struct get_property gp; memset(&gp,0,sizeof(gp)); gp.prop_id = id;
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return id;
    }
    return 0;
}

static u32 make_fb(u32 color) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd)); cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { L("createdumb errno=%d", errno); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { L("mapdumb errno=%d", errno); return 0; }
    u32 *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m==MAP_FAILED){ L("mmap errno=%d", errno); return 0; }
    for (u64 i=0;i<(u64)W*H;i++) m[i]=color;
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { L("addfb errno=%d", errno); return 0; }
    maps[fc.fb_id] = 0; /* placeholder */
    /* salvo la mappa per indice fb tramite tabella: uso l'ordine di creazione */
    static int idx = 0;
    if (idx < 4) { maps[idx] = m; fbs[idx] = fc.fb_id; idx++; }
    return fc.fb_id;
}

static int commit_full(u32 fbid) {   /* commit cnp completo (primo) */
    u32 objs[3]; u32 counts[3]; u32 props[32]; u64 vals[32]; int np=0, no=0, c0;
    c0=np; props[np]=modeid_id; vals[np++]=blob_id;
    props[np]=active_id; vals[np++]=1; counts[no]=np-c0; objs[no++]=crtc;
    c0=np; props[np]=conn_crtcid; vals[np++]=crtc; if (ar_id) { props[np]=ar_id; vals[np++]=0; }
    counts[no]=np-c0; objs[no++]=conn;
    c0=np; props[np]=p_crtcid; vals[np++]=crtc; props[np]=p_fbid; vals[np++]=fbid;
    props[np]=p_srcw; vals[np++]=((u64)W)<<16; props[np]=p_srch; vals[np++]=((u64)H)<<16;
    props[np]=p_srcx; vals[np++]=0; props[np]=p_srcy; vals[np++]=0;
    props[np]=p_crtcw; vals[np++]=W; props[np]=p_crtch; vals[np++]=H;
    props[np]=p_crtcx; vals[np++]=0; props[np]=p_crtcy; vals[np++]=0;
    counts[no]=np-c0; objs[no++]=plane;
    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=0x400; ar.count_objs=no;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0; int r = ioctl(fd, ATOMIC, &ar);
    return r? -errno : 0;
}

static int commit_plane(u32 fbid) {  /* commit plane-only (flip) */
    u32 objs[1]; u32 counts[1]; u32 props[16]; u64 vals[16]; int np=0, c0;
    c0=np; props[np]=p_crtcid; vals[np++]=crtc; props[np]=p_fbid; vals[np++]=fbid;
    props[np]=p_srcw; vals[np++]=((u64)W)<<16; props[np]=p_srch; vals[np++]=((u64)H)<<16;
    props[np]=p_srcx; vals[np++]=0; props[np]=p_srcy; vals[np++]=0;
    props[np]=p_crtcw; vals[np++]=W; props[np]=p_crtch; vals[np++]=H;
    props[np]=p_crtcx; vals[np++]=0; props[np]=p_crtcy; vals[np++]=0;
    counts[0]=np-c0; objs[0]=plane;
    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=0x400; ar.count_objs=1;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0; int r = ioctl(fd, ATOMIC, &ar);
    return r? -errno : 0;
}

/* ---- touch: leggo eventi non bloccanti, aggiorno il mirino ---- */
static int t_x = 540, t_y = 600, t_id = -1, t_down = 0;
static void poll_touch(void) {
    if (tfd < 0) return;
    struct input_event ev;
    while (read(tfd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X) t_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y) t_y = ev.value;
            else if (ev.code == ABS_MT_TRACKING_ID) {
                t_id = ev.value;
                if (ev.value >= 0) { t_down = 1; L("touch DOWN x=%d y=%d", t_x, t_y); }
                else { if (t_down) L("touch UP x=%d y=%d", t_x, t_y); t_down = 0; }
            }
        }
    }
}

/* ---- disegno ---- */
static void draw_frame(u32 *m, int fr) {
    /* sfondo blu-notte con lieve gradiente orizzontale animato */
    u32 bg = 0x00101828 | ((u32)((fr*2) & 0x3f) << 16);
    for (u64 i=0;i<(u64)W*H;i++) m[i] = bg;
    /* rettangolo animato (barra 240x160) che scorre */
    int bx = (fr * 12) % (W - 240);
    int by = 300;
    for (int y=by; y<by+160 && y<H; y++)
        for (int x=bx; x<bx+240 && x<W; x++) m[(u64)y*W+x] = 0x00ff8020;
    /* mirino (quadrato 80x80) alla posizione del touch */
    int cx = t_x - 40, cy = t_y - 40;
    u32 cc = t_down ? 0x00ff2020 : 0x0020ff40;
    for (int y=cy; y<cy+80; y++) {
        if (y<0||y>=H) continue;
        for (int x=cx; x<cx+80; x++) { if (x<0||x>=W) continue; m[(u64)y*W+x] = cc; }
    }
    __sync_synchronize();
}

int main(int argc, char **argv) {
    int maxfr = argc>1?atoi(argv[1]):0;      /* 0 = infinito */
    L("=== kiosk start (pid %d, maxfr=%d) ===", getpid(), maxfr);
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { L("open card0 errno=%d", errno); return 1; }
    ioctl(fd, SET_MASTER, 0);
    struct set_client_cap cap = {3, 1};
    if (ioctl(fd, SET_CLIENT_CAP, &cap)) { L("setcap errno=%d", errno); return 2; }

    struct get_conn gc; u64 modes_ptr=0, props_ptr=0, pv_ptr=0;
    memset(&gc,0,sizeof(gc)); gc.encoders_ptr=(u64)(uintptr_t)&modes_ptr;
    gc.connector_id = 56;
    struct modeinfo modes[32]; struct get_conn gc2; memset(&gc2,0,sizeof(gc2));
    gc2.encoders_ptr=(u64)(uintptr_t)modes;
    /* uso la GETCONNECTOR completa */
    struct { u64 enc, modes, props, pv; u32 cm, cp, ce, eid, cid, ctype, ctid, cn, mmw, mmh, sub, pad; } full;
    memset(&full,0,sizeof(full));
    full.cid = 56; full.modes = (u64)(uintptr_t)modes; full.cm = 32;
    if (ioctl(fd, GETCONNECTOR, &full)) { L("getconn errno=%d", errno); return 1; }
    conn = 56;
    for (u32 i=0;i<full.cm;i++) { if (modes[i].vrefresh>=85 && modes[i].vrefresh<=95) { best=modes[i]; break; } }
    if (!best.hdisplay) best=modes[0];
    L("mode: %dx%d@%d", best.hdisplay, best.vdisplay, best.vrefresh);

    struct create_blob cb; cb.data=(u64)(uintptr_t)&best; cb.length=sizeof(best); cb.blob_id=0;
    if (ioctl(fd, CREATE_BLOB, &cb)) { L("blob errno=%d", errno); return 1; }
    blob_id = cb.blob_id;

    modeid_id = findprop(crtc, "MODE_ID"); active_id = findprop(crtc, "ACTIVE");
    conn_crtcid = findprop(conn, "CRTC_ID"); ar_id = findprop(conn, "autorefresh");
    p_crtcid = findprop(plane, "CRTC_ID"); p_fbid = findprop(plane, "FB_ID");
    p_srcw = findprop(plane, "SRC_W"); p_srch = findprop(plane, "SRC_H");
    p_srcx = findprop(plane, "SRC_X"); p_srcy = findprop(plane, "SRC_Y");
    p_crtcw = findprop(plane, "CRTC_W"); p_crtch = findprop(plane, "CRTC_H");
    p_crtcx = findprop(plane, "CRTC_X"); p_crtcy = findprop(plane, "CRTC_Y");
    L("props: mode=%u act=%u crtcid=%u fbid=%u srcw=%u", modeid_id, active_id, p_crtcid, p_fbid, p_srcw);
    if (!modeid_id || !p_fbid) { L("props mancanti!"); return 3; }

    if (!make_fb(0x00101828)) return 4;   /* fb0 */
    if (!make_fb(0x00101828)) return 4;   /* fb1 (doppio buffer) */
    L("fbs: %llu %llu", (unsigned long long)fbs[0], (unsigned long long)fbs[1]);

    int r = commit_full(fbs[0]);
    if (r) { L("commit full FALLITO errno=%d", -r); return 6; }
    L("commit full OK");

    tfd = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
    L("touch fd=%d", tfd);

    int fr = 0, idx = 0;
    for (;;) {
        poll_touch();
        idx = (idx+1) & 1;
        draw_frame(maps[idx], fr);
        int pr = commit_plane(fbs[idx]);
        if (pr) { L("flip %d FALLITO errno=%d", fr, -pr); return 7; }
        fr++;
        if ((fr % 300) == 0) L("frame %d ok (fb=%llu)", fr, (unsigned long long)fbs[idx]);
        if (maxfr && fr >= maxfr) { L("raggiunti %d frame, hold", fr); break; }
        usleep(16000);   /* ~60fps, ben sotto il power collapse (~58ms) */
    }
    /* hold: processo vivo */
    for (;;) sleep(3600);
    return 0;
}
