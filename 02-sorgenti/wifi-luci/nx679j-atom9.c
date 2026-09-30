// nx679j-atom9 — ATOMIC COMPLETO da crtc vergine: percorso SDM (primo commit tutte le prop, poi 2° commit solo FB_ID).
// Log persistente su rawdump slot 432. Guard v2 (con DROP_MASTER) attivo.
// Fasi: "atomic" = commit#1 completo (conn CRTC_ID+autorefresh=0, crtc MODE_ID+ACTIVE, plane CRTC_ID+FB_ID+rect) -> attesa -> commit#2 solo plane FB_ID.
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
struct obj_get_props { u64 props_ptr, prop_values_ptr; u32 count_props, obj_id, obj_type; };
struct get_property { u64 values_ptr, enum_blob_ptr; u32 prop_id, flags; char name[32]; u32 count_values, count_enum_blobs; };
struct create_blob { u64 data; u32 length, blob_id; };
struct atomic_req { u32 flags, count_objs; u64 objs_ptr, count_props_ptr, props_ptr, prop_values_ptr, reserved, user_data; };

#define GETCRTC        0xc06864a1
#define SETCRTC        0xc06864a2
#define GETCONNECTOR   0xc05064a7
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define OBJ_GETPROPS   0xc02064b9
#define GETPROPERTY    0xc04064aa
#define CREATE_BLOB    0xc01064bd
#define ATOMIC         0xc03864bc
#define SET_MASTER     0x641e
#define ALLOW_MODESET  0x400
#define FLAG_TEST_ONLY 0x100

#define SLOT 432
static int fd;
static u32 W = 1080, H = 2400;

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

static u32 prop_by_name(u32 obj_id, u32 obj_type, const char *name) {
    /* 1) OBJ_GETPROPS -> lista ids (count a 2 fasi); 2) per ognuno GETPROPERTY -> nome */
    struct obj_get_props og; memset(&og,0,sizeof(og));
    og.obj_id=obj_id; og.obj_type=obj_type;
    if (ioctl(fd, OBJ_GETPROPS, &og)) { L("  objgetprops count FAIL errno=%d", errno); return 0; }
    u32 n = og.count_props; if (!n || n>200) return 0;
    static u32 ids[200]; static u64 vals[200];
    memset(&og,0,sizeof(og)); og.obj_id=obj_id; og.obj_type=obj_type;
    og.props_ptr=(u64)(uintptr_t)ids; og.prop_values_ptr=(u64)(uintptr_t)vals; og.count_props=n;
    if (ioctl(fd, OBJ_GETPROPS, &og)) { L("  objgetprops fetch FAIL errno=%d", errno); return 0; }
    for (u32 i=0;i<n;i++) {
        struct get_property gp; memset(&gp,0,sizeof(gp)); gp.prop_id=ids[i];
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return ids[i];
    }
    return 0;
}

static u32 find_prop_global(const char *name) {
    for (u32 id=1; id<500; id++) {
        struct get_property gp; memset(&gp,0,sizeof(gp)); gp.prop_id=id;
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return id;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *fase = argc>1?argv[1]:"atomic";
    L("=== atom9 fase %s start (pid %d) ===", fase, getpid());
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd<0){ L("open errno=%d", errno); return 1; }
    errno=0;
    if (ioctl(fd,SET_MASTER) && errno!=16) L("setmaster errno=%d", errno);
    L("setmaster errno=%d (0=ok,16=busy)", errno);
    { struct set_client_cap { u64 capability, value; } c = { 3, 1 };
      if (ioctl(fd, 0x4010640d, &c)) L("cap ATOMIC FAIL errno=%d", errno); else L("cap ATOMIC ok"); }

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
    if(conn_id<0){ L("conn assente"); return 2; }
    L("conn=%d mode='%s'", conn_id, best.name);

    u32 crtcs[16]; int ncr=0;
    for (u32 ci=0;ci<256 && ncr<16;ci++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=ci; if(!ioctl(fd,GETCRTC,&cc)) crtcs[ncr++]=ci; }
    u32 crtc_id=crtcs[0];
    for (int k=0;k<ncr;k++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=crtcs[k]; if(!ioctl(fd,GETCRTC,&cc) && cc.mode_valid){ crtc_id=crtcs[k]; break; } }
    L("crtc=%u (di %d)", crtc_id, ncr);

    /* prop ids */
    u32 conn_crtcid  = find_prop_global("CRTC_ID");
    u32 ar_id        = find_prop_global("autorefresh");
    u32 crtc_modeid  = find_prop_global("MODE_ID");
    u32 crtc_active  = find_prop_global("ACTIVE");
    L("conn: CRTC_ID=%u autorefresh=%u | crtc: MODE_ID=%u ACTIVE=%u", conn_crtcid, ar_id, crtc_modeid, crtc_active);

    /* plane primario: scan 1..255, GETPLANE, scegli il primo con possible_crtcs valido e type=1 */
    struct { u32 plane_id, crtc_id, fb_id, possible_crtcs, gamma_size, count_format_types; u64 format_type_ptr; } gp;
    u32 plane_id=0, plane_crtcid=0, p_fbid=0, p_srcw=0, p_srch=0, p_srcx=0, p_srcy=0, p_crtcw=0, p_crtch=0, p_crtcx=0, p_crtcy=0;
    u32 plane_type=0;
    for (u32 pi=1; pi<256; pi++) {
        memset(&gp,0,sizeof(gp)); gp.plane_id=pi;
        if (ioctl(fd, 0xc02064b6 /*GETPLANE*/, &gp)) continue;
        u32 t = find_prop_global("type");
        u32 tv=0;
        if (t) { /* leggi il valore type: fetch props */
            struct obj_get_props og; static u32 ids[200]; static u64 vals[200];
            memset(&og,0,sizeof(og)); og.obj_id=pi; og.obj_type=0xeeeeeeee;
            og.props_ptr=(u64)(uintptr_t)ids; og.prop_values_ptr=(u64)(uintptr_t)vals; og.count_props=200;
            if (!ioctl(fd, OBJ_GETPROPS, &og)) {
                for (u32 i=0;i<og.count_props && i<200;i++) if (ids[i]==t) tv=(u32)vals[i];
            }
        }
        L("  plane %u: possible_crtcs=0x%x type=%u", pi, gp.possible_crtcs, tv);
        if (tv==1 && !plane_id) {
            plane_id=pi;
            p_fbid   = find_prop_global("FB_ID");
            plane_crtcid = find_prop_global("CRTC_ID");
            p_srcw = find_prop_global("SRC_W");
            p_srch = find_prop_global("SRC_H");
            p_srcx = find_prop_global("SRC_X");
            p_srcy = find_prop_global("SRC_Y");
            p_crtcw = find_prop_global("CRTC_W");
            p_crtch = find_prop_global("CRTC_H");
            p_crtcx = find_prop_global("CRTC_X");
            p_crtcy = find_prop_global("CRTC_Y");
            plane_type = tv;
        }
    }
    if (!plane_id) { plane_id=103; /* fallback: primo plane trovato */
        for (u32 pi=1; pi<256; pi++) { memset(&gp,0,sizeof(gp)); gp.plane_id=pi; if (!ioctl(fd, 0xc02064b6, &gp)) { plane_id=103; break; } }
        L("fallback plane=%u", plane_id);
        p_fbid   = find_prop_global("FB_ID");
        plane_crtcid = find_prop_global("CRTC_ID");
        p_srcw = find_prop_global("SRC_W");
        p_srch = find_prop_global("SRC_H");
        p_srcx = find_prop_global("SRC_X");
        p_srcy = find_prop_global("SRC_Y");
        p_crtcw = find_prop_global("CRTC_W");
        p_crtch = find_prop_global("CRTC_H");
        p_crtcx = find_prop_global("CRTC_X");
        p_crtcy = find_prop_global("CRTC_Y");
    }
    L("plane scelto=%u: FB_ID=%u CRTC_ID=%u SRC_W=%u SRC_H=%u", plane_id, p_fbid, plane_crtcid, p_srcw, p_srch);
    if (!p_fbid || !p_srcw) { L("prop del plane mancanti, stop"); return 3; }

    /* fb ROSSO */
    struct create_dumb cd; memset(&cd,0,sizeof(cd)); cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { L("createdumb errno=%d", errno); return 4; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { L("mapdumb errno=%d", errno); return 4; }
    unsigned char *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { L("mmap errno=%d", errno); return 4; }
    for (u32 y=0;y<H;y++) { u32 *row=(u32*)(m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=0x00FF0000; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { L("addfb errno=%d", errno); return 4; }
    L("fb ROSSO = %u", fc.fb_id);

    /* blob MODE_ID */
    struct create_blob cb; memset(&cb,0,sizeof(cb)); cb.data=(u64)(uintptr_t)&best; cb.length=sizeof(best);
    if (ioctl(fd, CREATE_BLOB, &cb)) { L("createblob errno=%d", errno); return 5; }
    L("blob mode = %u", cb.blob_id);

    /* === COMMIT #1 completo === */
    u32 objs[3]  = { (u32)conn_id, crtc_id, plane_id };
    u32 counts[3]; u32 props[32]; u64 vals[32]; int np=0;
    int c0=np; props[np]=conn_crtcid; vals[np++]=crtc_id;
    if (ar_id) { props[np]=ar_id; vals[np++]=0; }
    counts[0]=np-c0;
    int c1=np; props[np]=crtc_modeid; vals[np++]=cb.blob_id;
    props[np]=crtc_active; vals[np++]=1;
    counts[1]=np-c1;
    int c2=np; props[np]=plane_crtcid; vals[np++]=crtc_id;
    props[np]=p_fbid;   vals[np++]=fc.fb_id;
    props[np]=p_srcw;   vals[np++]=((u64)W)<<16;
    props[np]=p_srch;   vals[np++]=((u64)H)<<16;
    props[np]=p_srcx;   vals[np++]=0;
    props[np]=p_srcy;   vals[np++]=0;
    props[np]=p_crtcw;  vals[np++]=W;
    props[np]=p_crtch;  vals[np++]=H;
    props[np]=p_crtcx;  vals[np++]=0;
    props[np]=p_crtcy;  vals[np++]=0;
    counts[2]=np-c2;
    L("commit#1: objs=%u/%u/%u counts=%u/%u/%u props_tot=%d", objs[0],objs[1],objs[2], counts[0],counts[1],counts[2], np);

    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=ALLOW_MODESET|FLAG_TEST_ONLY; ar.count_objs=3;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0;
    int r = ioctl(fd, ATOMIC, &ar);
    L("TEST_ONLY ret=%d errno=%d(%s)", r, errno, strerror(errno));
    if (!r) {
        ar.flags=ALLOW_MODESET;
        errno=0;
        r = ioctl(fd, ATOMIC, &ar);
        L("COMMIT#1 ret=%d errno=%d(%s)", r, errno, strerror(errno));
    }
    if (r) { L("commit#1 fallito, stop"); return 6; }
    L("attesa 5s (webcam: ROSSO)");
    sleep(5);

    if (!strcmp(fase,"atomic")) { L("fine (hold 6s)"); sleep(6); return 0; }

    /* === COMMIT #2: solo FB_ID del plane, con SECONDO fb VERDE (percorso SDM) === */
    struct create_dumb cd2; memset(&cd2,0,sizeof(cd2)); cd2.width=W; cd2.height=H; cd2.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd2)) { L("createdumb2 errno=%d", errno); return 4; }
    struct map_dumb md2; memset(&md2,0,sizeof(md2)); md2.handle=cd2.handle;
    if (ioctl(fd, MAP_DUMB, &md2)) { L("mapdumb2 errno=%d", errno); return 4; }
    unsigned char *m2 = mmap(NULL, cd2.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md2.offset);
    if (m2 == MAP_FAILED) { L("mmap2 errno=%d", errno); return 4; }
    for (u32 y=0;y<H;y++) { u32 *row=(u32*)(m2+(size_t)y*cd2.pitch); for(u32 x=0;x<W;x++) row[x]=0x0000FF00; }
    __sync_synchronize();
    struct fb_cmd fc2; memset(&fc2,0,sizeof(fc2));
    fc2.width=W; fc2.height=H; fc2.pitch=cd2.pitch; fc2.bpp=32; fc2.depth=24; fc2.handle=cd2.handle;
    if (ioctl(fd, ADDFB, &fc2)) { L("addfb2 errno=%d", errno); return 4; }
    L("fb VERDE = %u", fc2.fb_id);
    u32 objs2[1] = { plane_id };
    u32 cnt2[1]  = { 1 };
    u32 pr2[1]   = { p_fbid };
    u64 v2[1]    = { fc2.fb_id };
    L("--- commit#2: solo FB_ID=fbG sul plane");
    struct atomic_req ar2; memset(&ar2,0,sizeof(ar2));
    ar2.flags=ALLOW_MODESET; ar2.count_objs=1;
    ar2.objs_ptr=(u64)(uintptr_t)objs2; ar2.count_props_ptr=(u64)(uintptr_t)cnt2;
    ar2.props_ptr=(u64)(uintptr_t)pr2; ar2.prop_values_ptr=(u64)(uintptr_t)v2;
    errno=0;
    r = ioctl(fd, ATOMIC, &ar2);
    L("COMMIT#2 ret=%d errno=%d(%s)", r, errno, strerror(errno));
    L("attesa 6s (webcam: VERDE?)");
    sleep(6);
    L("fine (hold 6s)");
    sleep(6);
    return 0;
}
