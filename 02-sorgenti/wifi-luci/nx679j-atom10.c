// nx679j-atom10 — isola la prop che dà EINVAL: prova combinazioni di oggetti/props via argv.
// Uso: atom10 <combo>  dove combo = c | n | p | cn | cp | np | cnp | p2
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
struct get_property { u64 values_ptr, enum_blob_ptr; u32 prop_id, flags; char name[32]; u32 count_values, count_enum_blobs; };
struct create_blob { u64 data; u32 length, blob_id; };
struct atomic_req { u32 flags, count_objs; u64 objs_ptr, count_props_ptr, props_ptr, prop_values_ptr, reserved, user_data; };

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
struct set_client_cap { u64 capability, value; };

static int fd; static u32 W=1080,H=2400;

static u32 find_prop_global(const char *name) {
    for (u32 id=1; id<500; id++) {
        struct get_property gp; memset(&gp,0,sizeof(gp)); gp.prop_id=id;
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return id;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *combo = argc>1?argv[1]:"cnp";
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd<0){ perror("open"); return 1; }
    if (ioctl(fd,SET_MASTER) && errno!=16) printf("setmaster errno=%d\n",errno);
    { struct set_client_cap c = { 3 /*ATOMIC*/, 1 }; if (ioctl(fd, SET_CLIENT_CAP, &c)) printf("cap ATOMIC errno=%d\n", errno); else printf("cap ATOMIC ok\n"); }
    { struct set_client_cap c = { 2 /*UNIVERSAL_PLANES*/, 1 }; if (ioctl(fd, SET_CLIENT_CAP, &c)) printf("cap UNIV errno=%d\n", errno); else printf("cap UNIV ok\n"); }

    int conn=-1; struct modeinfo modes[40]; struct modeinfo best; memset(&best,0,sizeof(best));
    for (u32 i=0;i<256 && conn<0;i++) {
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
    u32 crtc = crtcs[0];
    for (int k=0;k<ncr;k++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=crtcs[k]; if(!ioctl(fd,GETCRTC,&cc) && cc.mode_valid){ crtc=crtcs[k]; break; } }
    printf("conn=%d crtc=%u\n", conn, crtc);

    u32 conn_crtcid = find_prop_global("CRTC_ID");
    u32 ar_id       = find_prop_global("autorefresh");
    u32 modeid_id   = find_prop_global("MODE_ID");
    u32 active_id   = find_prop_global("ACTIVE");
    u32 p_fbid      = find_prop_global("FB_ID");
    u32 p_crtcid    = find_prop_global("CRTC_ID");
    u32 p_srcw      = find_prop_global("SRC_W");
    u32 p_srch      = find_prop_global("SRC_H");
    u32 p_srcx      = find_prop_global("SRC_X");
    u32 p_srcy      = find_prop_global("SRC_Y");
    u32 p_crtcw     = find_prop_global("CRTC_W");
    u32 p_crtch     = find_prop_global("CRTC_H");
    u32 p_crtcx     = find_prop_global("CRTC_X");
    u32 p_crtcy     = find_prop_global("CRTC_Y");
    printf("ids: connCRTC=%u ar=%u modeid=%u active=%u | fb=%u plCRTC=%u srcw=%u srch=%u srcx=%u srcy=%u crtcw=%u crtch=%u crtcx=%u crtcy=%u\n",
        conn_crtcid, ar_id, modeid_id, active_id, p_fbid, p_crtcid, p_srcw, p_srch, p_srcx, p_srcy, p_crtcw, p_crtch, p_crtcx, p_crtcy);

    /* fb */
    struct create_dumb cd; memset(&cd,0,sizeof(cd)); cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { printf("createdumb errno=%d\n",errno); return 4; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { printf("mapdumb errno=%d\n",errno); return 4; }
    unsigned char *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m==MAP_FAILED){ printf("mmap errno=%d\n",errno); return 4; }
    for (u32 y=0;y<H;y++){ u32 *row=(u32*)(m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=0x00FF0000; }
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { printf("addfb errno=%d\n",errno); return 4; }
    printf("fb=%u\n", fc.fb_id);

    struct create_blob cb; memset(&cb,0,sizeof(cb)); cb.data=(u64)(uintptr_t)&best; cb.length=sizeof(best);
    if (ioctl(fd, CREATE_BLOB, &cb)) { printf("createblob errno=%d\n",errno); return 5; }
    printf("blob=%u\n", cb.blob_id);

    u32 plane = 103;   /* il plane-1 del crtc-0 */

    /* costruisci gli oggetti/props secondo la combo */
    u32 objs[3]; u32 counts[3]; u32 props[40]; u64 vals[40]; int np=0, no=0;
    if (strstr(combo,"c")) {
        int c0=np; props[np]=modeid_id; vals[np++]=cb.blob_id;
        props[np]=active_id; vals[np++]=1;
        counts[no]=np-c0; objs[no++]=crtc;
    }
    if (strstr(combo,"n")) {
        int c0=np; props[np]=conn_crtcid; vals[np++]=crtc;
        if (ar_id) { props[np]=ar_id; vals[np++]=0; }
        counts[no]=np-c0; objs[no++]=conn;
    }
    if (strstr(combo,"p")) {
        int c0=np; props[np]=p_crtcid; vals[np++]=crtc;
        props[np]=p_fbid; vals[np++]=fc.fb_id;
        if (!strcmp(combo,"p2")) { /* solo fb senza rect */ }
        else {
            props[np]=p_srcw; vals[np++]=((u64)W)<<16;
            props[np]=p_srch; vals[np++]=((u64)H)<<16;
            props[np]=p_srcx; vals[np++]=0;
            props[np]=p_srcy; vals[np++]=0;
            props[np]=p_crtcw; vals[np++]=W;
            props[np]=p_crtch; vals[np++]=H;
            props[np]=p_crtcx; vals[np++]=0;
            props[np]=p_crtcy; vals[np++]=0;
        }
        counts[no]=np-c0; objs[no++]=plane;
    }
    printf("combo=%s: %d objs, %d props tot\n", combo, no, np);

    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=0x400|0x100; ar.count_objs=no;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0;
    int r = ioctl(fd, ATOMIC, &ar);
    printf("TEST_ONLY ret=%d errno=%d(%s)\n", r, errno, strerror(errno));
    if (!r) {
        ar.flags=0x400;
        errno=0;
        r = ioctl(fd, ATOMIC, &ar);
        printf("REAL ret=%d errno=%d(%s)\n", r, errno, strerror(errno));
    }
    /* non uscire subito: aspetta per la webcam */
    sleep(8);
    return r?6:0;
}
