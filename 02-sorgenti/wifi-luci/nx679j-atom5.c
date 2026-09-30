// nx679j-atom5 — test percorso ATOMIC (come HAL Qualcomm/SDM su questa generazione).
// [1] commit completo: connector CRTC_ID (+autorefresh=0), crtc MODE_ID+ACTIVE, plane CRTC_ID+FB_ID+SRC/CRTC
// [2] loop: commit di SOLO plane FB_ID -> nuovi fb (VERDE, BLU) = il "flip" del percorso SDM.
// props[] e prop_values[] SEPARATI (atomic1-4 li impacchettavano = mai funzionato).
// Layout struct = kernel 5.10 del device (diverso da 6.x!). Test-only prima, poi commit reale.
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

/* kernel 5.10 esatto */
struct get_plane { u32 plane_id, crtc_id, fb_id, possible_crtcs, gamma_size, count_format_types; u64 format_type_ptr; };
struct get_plane_res { u64 plane_id_ptr; u32 count_planes; };
struct obj_get_props { u64 props_ptr, prop_values_ptr; u32 count_props, obj_id, obj_type; };
struct get_property { u64 values_ptr, enum_blob_ptr; u32 prop_id, flags; char name[32]; u32 count_values, count_enum_blobs; };
struct create_blob { u64 data; u32 length, blob_id; };
struct atomic_req { u32 flags, count_objs; u64 objs_ptr, count_props_ptr, props_ptr, prop_values_ptr, reserved, user_data; };

#define GETCRTC        0xc06864a1
#define GETCONNECTOR   0xc05064a7
#define GETPLANERES    0xc01064b5
#define GETPLANE       0xc02064b6
#define OBJ_GETPROPS   0xc02064b9
#define GETPROPERTY    0xc04064aa
#define CREATEBLOB     0xc01064bd
#define ATOMIC         0xc03864bc
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define SET_MASTER     0x641e

#define OBJ_PLANE      0xeeeeeeee
#define OBJ_CRTC       0xcccccccc
#define OBJ_CONN       0xc0c0c0c0
#define ALLOW_MODESET  0x0400
#define FLAG_TEST_ONLY 0x0100

static int fd;
static u32 W = 1080, H = 2400;

static u32 make_fb(u32 color) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd));
    cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { perror("create dumb"); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { perror("map dumb"); return 0; }
    u32 *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { perror("mmap"); return 0; }
    for (u32 y=0;y<H;y++) { u32 *row=(u32*)((unsigned char *)m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { perror("addfb"); return 0; }
    printf("  fb=%u pitch=%u\n", fc.fb_id, cd.pitch); fflush(stdout);
    return fc.fb_id;
}

static int get_props_count(u32 obj_id, u32 obj_type) {
    struct obj_get_props p; memset(&p,0,sizeof(p));
    p.count_props=0; p.obj_id=obj_id; p.obj_type=obj_type;
    if (ioctl(fd, OBJ_GETPROPS, &p)) { printf("  [getprops count fail errno=%d]\n", errno); return -1; }
    return (int)p.count_props;
}
static int get_props(u32 obj_id, u32 obj_type, u32 *props, u64 *vals, int maxn) {
    int n = get_props_count(obj_id, obj_type);
    if (n < 0) return -1;
    if (n > maxn) { printf("  [props %d > buf %d: tronco]\n", n, maxn); n = maxn; }
    struct obj_get_props p; memset(&p,0,sizeof(p));
    p.props_ptr=(u64)(uintptr_t)props; p.prop_values_ptr=(u64)(uintptr_t)vals;
    p.count_props=n; p.obj_id=obj_id; p.obj_type=obj_type;
    if (ioctl(fd, OBJ_GETPROPS, &p)) { printf("  [getprops fetch fail errno=%d]\n", errno); return -1; }
    return n;
}
static u32 prop_id(u32 obj_id, u32 obj_type, const char *name) {
    u32 props[64]; u64 vals[64];
    int n = get_props(obj_id, obj_type, props, vals, 64);
    for (int i=0;i<n;i++) {
        struct get_property gp; memset(&gp,0,sizeof(gp));
        gp.prop_id=props[i];
        if (ioctl(fd, GETPROPERTY, &gp)) { printf("  [getprop %u fail errno=%d]\n", props[i], errno); continue; }
        if (!strcmp(gp.name, name)) return props[i];
    }
    return 0;
}
static u64 prop_val(u32 obj_id, u32 obj_type, const char *name, int *found) {
    u32 props[64]; u64 vals[64];
    int n = get_props(obj_id, obj_type, props, vals, 64);
    for (int i=0;i<n;i++) {
        struct get_property gp; memset(&gp,0,sizeof(gp));
        gp.prop_id=props[i];
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) { if(found)*found=1; return vals[i]; }
    }
    if(found)*found=0; return 0;
}

static u32 find_prop_global(const char *name) {
    for (u32 id=1; id<600; id++) {
        struct get_property gp; memset(&gp,0,sizeof(gp));
        gp.prop_id=id;
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return id;
    }
    return 0;
}

static int do_atomic(u32 flags, u32 *objs, u32 *counts, u32 *props, u64 *vals, int n_objs) {
    struct atomic_req r; memset(&r,0,sizeof(r));
    r.flags=flags; r.count_objs=n_objs;
    r.objs_ptr=(u64)(uintptr_t)objs; r.count_props_ptr=(u64)(uintptr_t)counts;
    r.props_ptr=(u64)(uintptr_t)props; r.prop_values_ptr=(u64)(uintptr_t)vals;
    return ioctl(fd, ATOMIC, &r);
}

int main(int argc, char **argv) {
    int hold = argc>1?atoi(argv[1]):60;
    int use_posted = (argc>2 && !strcmp(argv[2],"posted"));
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd<0){ perror("open"); return 1; }
    if (ioctl(fd,SET_MASTER) && errno!=16) printf("setmaster errno=%d\n",errno);
    printf("master ok\n"); fflush(stdout);

    /* --- connector DSI + mode preferred --- */
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
    if(conn_id<0){ printf("conn assente\n"); return 2; }
    printf("conn=%d best='%s' %ux%u@%u\n", conn_id, best.name, best.hdisplay, best.vdisplay, best.vrefresh); fflush(stdout);

    /* props conn */
    printf("conn props count: %d\n", get_props_count(conn_id, OBJ_CONN)); fflush(stdout);
    int f=0; u64 ar_cur = prop_val(conn_id, OBJ_CONN, "autorefresh", &f);
    u32 ar_id = f ? find_prop_global("autorefresh") : 0;
    printf("autorefresh: esiste=%d id=%u val=%llu\n", f, ar_id, (unsigned long long)ar_cur); fflush(stdout);
    u32 ftm_id = find_prop_global("frame_trigger_mode");
    printf("frame_trigger_mode id=%u (posted=%d)\n", ftm_id, use_posted); fflush(stdout);
    u32 conn_crtcid = find_prop_global("CRTC_ID");

    /* --- crtc attivo (o primo) --- */
    u32 crtcs[16]; int ncr=0;
    for (u32 ci=0;ci<256 && ncr<16;ci++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=ci; if(!ioctl(fd,GETCRTC,&cc)) crtcs[ncr++]=ci; }
    u32 crtc_id=0;
    for (int k=0;k<ncr;k++){ struct crtc cc; memset(&cc,0,sizeof(cc)); cc.crtc_id=crtcs[k]; if(!ioctl(fd,GETCRTC,&cc) && cc.mode_valid){ crtc_id=crtcs[k]; printf("crtc attivo=%u fb=%u\n",crtc_id,cc.fb_id); break; } }
    if(!crtc_id && ncr){ crtc_id=crtcs[0]; printf("nessun crtc attivo; uso %u\n",crtc_id); }
    if(!crtc_id){ printf("no crtc\n"); return 3; }

    /* --- plane primario --- */
    u32 plane_ids[32]; struct get_plane_res pr; memset(&pr,0,sizeof(pr));
    pr.plane_id_ptr=(u64)(uintptr_t)plane_ids; pr.count_planes=32;
    if (ioctl(fd,GETPLANERES,&pr)) { perror("getplaneres"); return 4; }
    printf("planes: %u\n", pr.count_planes); fflush(stdout);
    u32 plane_id=0;
    for (u32 i=0;i<pr.count_planes && i<32;i++) {
        int pf=0; u64 typ = prop_val(plane_ids[i], OBJ_PLANE, "type", &pf);
        printf("  plane %u type=%llu%s\n", plane_ids[i], (unsigned long long)typ, (pf&&typ==1)?" [PRIMARY]":""); fflush(stdout);
        if (pf && typ==1) plane_id=plane_ids[i];
    }
    if(!plane_id && pr.count_planes) plane_id=plane_ids[0];
    if(argc>3) { plane_id=(u32)atoi(argv[3]); printf("OVERRIDE plane=%u\n", plane_id); }
    printf("plane scelto=%u\n", plane_id); fflush(stdout);
    /* DEBUG: dump prop del plane */
    { u32 pp[64]; u64 vv[64]; int n=get_props(plane_id,OBJ_PLANE,pp,vv,64);
      printf("plane %u: %d props\n", plane_id, n);
      for(int z=0;z<n && z<64;z++){ struct get_property gp; memset(&gp,0,sizeof(gp)); gp.prop_id=pp[z];
        if(ioctl(fd,GETPROPERTY,&gp)){ printf("  prop %u: ERR %d\n", pp[z], errno); continue; }
        printf("  %s=%llu (id %u)\n", gp.name, (unsigned long long)vv[z], pp[z]); } }
    if(!plane_id) return 5;

    /* prop ids del plane */
    u32 p_fbid = find_prop_global("FB_ID");
    u32 p_crtcid = find_prop_global("CRTC_ID");
    u32 p_srcw = find_prop_global("SRC_W");
    u32 p_srch = find_prop_global("SRC_H");
    u32 p_srcx = find_prop_global("SRC_X");
    u32 p_srcy = find_prop_global("SRC_Y");
    u32 p_crtcw = find_prop_global("CRTC_W");
    u32 p_crtch = find_prop_global("CRTC_H");
    u32 p_crtcx = find_prop_global("CRTC_X");
    u32 p_crtcy = find_prop_global("CRTC_Y");
    printf("PROP ID GLOBALI: FB_ID=%u CRTC_ID(plane)=%u SRC_W=%u SRC_H=%u SRC_X=%u SRC_Y=%u CRTC_W=%u CRTC_H=%u CRTC_X=%u CRTC_Y=%u\n", p_fbid,p_crtcid,p_srcw,p_srch,p_srcx,p_srcy,p_crtcw,p_crtch,p_crtcx,p_crtcy); fflush(stdout);

    /* crtc props */
    u32 c_modeid = find_prop_global("MODE_ID");
    u32 c_active = find_prop_global("ACTIVE");
    printf("PROP GLOBALI: MODE_ID=%u ACTIVE=%u conn_CRTC_ID=%u ar=%u ftm=%u\n", c_modeid,c_active,conn_crtcid,ar_id,ftm_id); fflush(stdout);

    /* --- blob mode --- */
    struct create_blob bl; memset(&bl,0,sizeof(bl));
    bl.data=(u64)(uintptr_t)&best; bl.length=sizeof(best);
    if (ioctl(fd,CREATEBLOB,&bl)) { perror("createblob"); return 6; }
    printf("blob mode id=%u\n", bl.blob_id); fflush(stdout);

    /* --- 3 fb colori pieni --- */
    printf("fb:\n");
    u32 fbR=make_fb(0x00FF0000), fbG=make_fb(0x0000FF00), fbB=make_fb(0x000000FF);
    if(!fbR||!fbG||!fbB) return 7;

    /* --- COMMIT 1: completo (test_only, poi reale) --- */
    u32 objs[3]  = { (u32)conn_id, crtc_id, plane_id };
    u32 counts[3]; u32 props[32]; u64 vals[32]; int np=0;
    int c0=np; props[np]=conn_crtcid; vals[np++]=crtc_id;
    if (ar_id) { props[np]=ar_id; vals[np++]=0; }
    if (ftm_id && use_posted) { props[np]=ftm_id; vals[np++]=2; }
    counts[0]=np-c0;
    int c1=np; props[np]=c_modeid; vals[np++]=bl.blob_id; props[np]=c_active; vals[np++]=1;
    counts[1]=np-c1;
    int c2=np;
    props[np]=p_crtcid; vals[np++]=crtc_id;
    props[np]=p_fbid;   vals[np++]=fbR;
    props[np]=p_srcw;   vals[np++]=((u64)W)<<16;
    props[np]=p_srch;   vals[np++]=((u64)H)<<16;
    props[np]=p_srcx;   vals[np++]=0;
    props[np]=p_srcy;   vals[np++]=0;
    props[np]=p_crtcw;  vals[np++]=W;
    props[np]=p_crtch;  vals[np++]=H;
    props[np]=p_crtcx;  vals[np++]=0;
    props[np]=p_crtcy;  vals[np++]=0;
    counts[2]=np-c2;

    printf("COMMIT1 test-only... "); fflush(stdout);
    if (do_atomic(ALLOW_MODESET|FLAG_TEST_ONLY, objs, counts, props, vals, 3)) printf("FAIL errno=%d(%s)\n",errno,strerror(errno));
    else printf("OK\n"); fflush(stdout);
    printf("COMMIT1 reale... "); fflush(stdout);
    if (do_atomic(ALLOW_MODESET, objs, counts, props, vals, 3)) { printf("FAIL errno=%d(%s)\n",errno,strerror(errno)); fflush(stdout); }
    else { printf("OK (pannello: ROSSO pieno)\n"); fflush(stdout); }
    sleep(5);

    /* --- COMMIT 2..N: SOLO plane FB_ID (percorso SDM) --- */
    u32 objs2[1] = { plane_id };
    u32 cnt2[1]  = { 1 };
    u32 pr2[1]; u64 v2[1];
    u32 seq[3] = { fbG, fbB, fbR };
    for (int i=0;i<3;i++) {
        pr2[0]=p_fbid; v2[0]=seq[i];
        printf("FLIP %d -> fb=%u: ", i+1, seq[i]); fflush(stdout);
        if (do_atomic(0, objs2, cnt2, pr2, v2, 1)) printf("FAIL errno=%d(%s)\n",errno,strerror(errno));
        else printf("OK\n");
        fflush(stdout);
        sleep(5);
    }
    printf("hold %ds...\n", hold); fflush(stdout);
    sleep(hold);
    printf("fine\n"); fflush(stdout);
    return 0;
}
