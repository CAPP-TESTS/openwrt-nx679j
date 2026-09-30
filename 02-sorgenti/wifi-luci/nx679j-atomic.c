// nx679j-atomic — accende il pannello via ATOMIC commit (come fa Android/HWC!)
// Legacy setcrtc crasha col VDTR6130; l'atomic no.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#ifndef DRM_IOCTL_MODE_CREATE_BLOB
#define DRM_IOCTL_MODE_CREATE_BLOB DRM_IOWR(0xBD, struct drm_mode_create_blob)
#endif
#ifndef DRM_IOCTL_MODE_OBJ_GETPROPERTIES
#define DRM_IOCTL_MODE_OBJ_GETPROPERTIES DRM_IOWR(0xB9, struct drm_mode_obj_get_properties)
#endif
#define DRM_MODE_OBJECT_CONNECTOR 0xc0c0c0c0
#define DRM_MODE_OBJECT_CRTC 0xcccccccc
#define DRM_MODE_OBJECT_PLANE 0xeeeeeeee

typedef uint64_t u64;
static int fd;

int main(int argc, char **argv) {
    const char *card = "/dev/dri/card0";
    int hold = argc > 1 ? atoi(argv[1]) : 300;
    fd = open(card, O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }

    // 1) resources
    struct drm_mode_card_res res; memset(&res, 0, sizeof(res));
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("getres"); return 2; }
    uint32_t *crtcs = calloc(res.count_crtcs, 4), *conns = calloc(res.count_connectors, 4);
    uint32_t *encs = calloc(res.count_encoders, 4), *fbs = calloc(res.count_fbs ? res.count_fbs : 1, 4);
    res.crtc_id_ptr = (u64)(uintptr_t)crtcs; res.connector_id_ptr = (u64)(uintptr_t)conns;
    res.encoder_id_ptr = (u64)(uintptr_t)encs; res.fb_id_ptr = (u64)(uintptr_t)fbs;
    ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res);
    printf("crtcs=%u conns=%u encs=%u\n", res.count_crtcs, res.count_connectors, res.count_encoders);

    // 2) trova il connettore DSI (type 16) e i suoi modi
    struct drm_mode_modeinfo modes[16]; uint64_t enc_ids[16], prop_ids[64], prop_vals[64];
    uint32_t conn_id = 0, nmodes = 0; uint64_t enc_id = 0;
    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector c; memset(&c, 0, sizeof(c));
        c.connector_id = conns[i];
        ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c);
        if (c.connector_type != 16) continue;
        conn_id = conns[i];
        memset(&c, 0, sizeof(c)); c.connector_id = conn_id;
        c.modes_ptr = (u64)(uintptr_t)modes; c.count_modes = 16;
        c.encoders_ptr = (u64)(uintptr_t)enc_ids; c.count_encoders = 16;
        c.props_ptr = (u64)(uintptr_t)prop_ids; c.prop_values_ptr = (u64)(uintptr_t)prop_vals; c.count_props = 64;
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) { perror("getconn"); return 3; }
        nmodes = c.count_modes; enc_id = c.encoder_id;
        printf("conn DSI %u: %u modi, est=%u enc=%llu\n", conn_id, nmodes, c.count_encoders, (unsigned long long)enc_id);
        for (uint32_t m = 0; m < nmodes; m++)
            printf("  mode[%u]: %s %ux%u@%u\n", m, modes[m].name, modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh);
        break;
    }
    if (!conn_id) { printf("conn DSI non trovato\n"); return 4; }

    // 3) trova il crtc dell'encoder
    uint32_t crtc_id = 0;
    for (uint32_t i = 0; i < res.count_encoders; i++) {
        struct drm_mode_get_encoder e; memset(&e, 0, sizeof(e));
        e.encoder_id = encs[i];
        ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e);
        if (e.encoder_id == (uint32_t)enc_id) { crtc_id = e.crtc_id; printf("encoder %u → crtc %u\n", encs[i], crtc_id); }
    }

    // 4) crea dumb buffer
    uint32_t m_idx = argc > 2 ? atoi(argv[2]) : 0;
    if (m_idx >= nmodes) m_idx = 0;
    uint32_t W = modes[m_idx].hdisplay, H = modes[m_idx].vdisplay;
    struct drm_mode_create_dumb d; memset(&d, 0, sizeof(d));
    d.width = W; d.height = H; d.bpp = 32; d.flags = 0;
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &d)) { perror("create dumb"); return 5; }
    struct drm_mode_fb_cmd fb; memset(&fb, 0, sizeof(fb));
    fb.width = d.width; fb.height = d.height; fb.bpp = 32; fb.depth = 24;
    fb.pitch = d.pitch; fb.handle = d.handle;
    if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb)) { perror("addfb"); return 6; }
    struct drm_mode_map_dumb md; memset(&md, 0, sizeof(md)); md.handle = d.handle;
    ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md);
    uint32_t *ptr = mmap(0, d.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (ptr == MAP_FAILED) { perror("mmap"); return 7; }
    printf("fb: %ux%u pitch=%u fb_id=%u\n", W, H, d.pitch, fb.fb_id);

    // disegna: 4 bande
    unsigned solid = argc > 3 ? atoi(argv[3]) : 0;
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint32_t col;
            if (solid == 1) col = 0xFFFF0000;
            else if (solid == 2) col = 0xFF00FF00;
            else if (solid == 3) col = 0xFF0000FF;
            else if (solid == 4) col = 0xFFFFFFFF;
            else if (y < H/4) col = 0xFFFF0000;
            else if (y < H/2) col = 0xFF00FF00;
            else if (y < 3*H/4) col = 0xFF0000FF;
            else col = 0xFFFFFFFF;
            ptr[(y * (d.pitch/4)) + x] = col;
        }
    printf("pattern disegnato\n");

    // 5) risolvi le property per nome: per CONN, CRTC, PLANE
    // helper: enumera tutte le prop di un oggetto
    #define GET_PROPS(objid, objtype, ids_arr, vals_arr, cnt) do { \
        struct drm_mode_obj_get_properties op; memset(&op, 0, sizeof(op)); \
        op.obj_id = objid; op.obj_type = objtype; \
        op.props_ptr = (u64)(uintptr_t)ids_arr; op.prop_values_ptr = (u64)(uintptr_t)vals_arr; op.count_props = cnt; \
        if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op)) { perror("objgetprops"); return 8; } \
        printf("GET_PROPS obj=%u count=%u\n", objid, op.count_props); \
    } while (0)

    uint32_t cprops[64], cvals[64];
    GET_PROPS(conn_id, 0xc0c0c0c0 /*connector*/, cprops, cvals, 64);

    // leggi i nomi
    char name[64];
    uint32_t prop_crtc_id_conn = 0;
    for (int i = 0; i < 64 && cprops[i]; i++) {
        struct drm_mode_get_property p; memset(&p, 0, sizeof(p));
        p.prop_id = cprops[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) { printf("conn GETPROP(%u) FAIL errno=%d\n", cprops[i], errno); continue; }
        memcpy(name, p.name, 32);
        if (!strcmp(name, "CRTC_ID")) { prop_crtc_id_conn = cprops[i]; printf(">>> trovato conn CRTC_ID = prop %u\n", cprops[i]); }
        printf("conn prop: %s (id=%u val=%u)\n", name, cprops[i], cvals[i]);
    }

    // crtc props
    uint32_t rprops[64], rvals[64];
    GET_PROPS(crtc_id, 0xcccccccc /*crtc*/, rprops, rvals, 64);
    uint32_t prop_mode_id = 0, prop_active = 0;
    for (int i = 0; i < 64 && rprops[i]; i++) {
        struct drm_mode_get_property p; memset(&p, 0, sizeof(p));
        p.prop_id = rprops[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) { printf("crtc GETPROP(%u) FAIL errno=%d\n", rprops[i], errno); continue; }
        memcpy(name, p.name, 32);
        if (!strcmp(name, "MODE_ID")) { prop_mode_id = rprops[i]; printf(">>> trovato crtc MODE_ID = prop %u\n", rprops[i]); }
        if (!strcmp(name, "ACTIVE")) { prop_active = rprops[i]; printf(">>> trovato crtc ACTIVE = prop %u\n", rprops[i]); }
        printf("crtc prop: %s (id=%u val=%u)\n", name, rprops[i], rvals[i]);
    }

    // plane resources
    struct drm_mode_get_plane_res pres; memset(&pres, 0, sizeof(pres));
    ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres);
    uint32_t *planes = calloc(pres.count_planes, 4);
    pres.plane_id_ptr = (u64)(uintptr_t)planes;
    ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres);
    // trova il plane PRIMARY del crtc: primo con prop "type" == 0 (DRM_PLANE_TYPE_PRIMARY)
    uint32_t plane_id = 0;
    for (uint32_t i = 0; i < pres.count_planes; i++) {
        struct drm_mode_get_plane pl; memset(&pl, 0, sizeof(pl));
        pl.plane_id = planes[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &pl)) continue;
        // leggi la prop type
        uint32_t lprops[64], lvals[64];
        struct drm_mode_obj_get_properties op; memset(&op, 0, sizeof(op));
        op.obj_id = planes[i]; op.obj_type = 0xeeeeeeee;
        op.props_ptr = (u64)(uintptr_t)lprops; op.prop_values_ptr = (u64)(uintptr_t)lvals; op.count_props = 64;
        if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op)) continue;
        for (int j = 0; j < 64 && lprops[j]; j++) {
            struct drm_mode_get_property pp; memset(&pp, 0, sizeof(pp));
            pp.prop_id = lprops[j];
            if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &pp)) continue;
            char pn[64]; memcpy(pn, pp.name, 32);
            if (!strcmp(pn, "type") && lvals[j] == 0) {  // PRIMARY
                plane_id = planes[i];
                printf("primary plane: %u (possible=%x)\n", plane_id, pl.possible_crtcs);
            }
        }
        if (plane_id) break;
    }
    if (!plane_id) { printf("NESSUN PRIMARY PLANE!\n"); return 8; }

    // plane props
    uint32_t pprops[64], pvals[64];
    GET_PROPS(plane_id, 0xeeeeeeee /*plane*/, pprops, pvals, 64);
    uint32_t prop_fb_id = 0, prop_crtc_id_pl = 0, prop_src_x = 0, prop_src_y = 0, prop_src_w = 0, prop_src_h = 0, prop_crtc_x = 0, prop_crtc_y = 0, prop_crtc_w = 0, prop_crtc_h = 0;
    for (int i = 0; i < 64 && pprops[i]; i++) {
        struct drm_mode_get_property p; memset(&p, 0, sizeof(p));
        p.prop_id = pprops[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) continue;
        memcpy(name, p.name, 32);
        if (!strcmp(name, "FB_ID")) prop_fb_id = pprops[i];
        if (!strcmp(name, "CRTC_ID")) prop_crtc_id_pl = pprops[i];
        if (!strcmp(name, "SRC_X")) prop_src_x = pprops[i];
        if (!strcmp(name, "SRC_Y")) prop_src_y = pprops[i];
        if (!strcmp(name, "SRC_W")) prop_src_w = pprops[i];
        if (!strcmp(name, "SRC_H")) prop_src_h = pprops[i];
        if (!strcmp(name, "CRTC_X")) prop_crtc_x = pprops[i];
        if (!strcmp(name, "CRTC_Y")) prop_crtc_y = pprops[i];
        if (!strcmp(name, "CRTC_W")) prop_crtc_w = pprops[i];
        if (!strcmp(name, "CRTC_H")) prop_crtc_h = pprops[i];
        printf("plane prop: %s (id=%u val=%u)\n", name, pprops[i], pvals[i]);
    }

    // 6) crea il blob del mode
    struct drm_mode_create_blob cb; memset(&cb, 0, sizeof(cb));
    cb.data = (u64)(uintptr_t)&modes[m_idx];
    cb.length = sizeof(struct drm_mode_modeinfo);
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_BLOB, &cb)) { perror("create blob"); return 9; }
    printf("mode blob: %u (mode %s)\n", cb.blob_id, modes[m_idx].name);
    printf("CHECK: conn_crtc_id=%u crtc_active=%u crtc_mode_id=%u\n", prop_crtc_id_conn, prop_active, prop_mode_id);
    if (!prop_crtc_id_conn || !prop_active || !prop_mode_id) { printf("MANCANO PROP CRITICHE!\n"); return 10; }

    // 7) atomic commit
    struct drm_mode_atomic at; memset(&at, 0, sizeof(at));
    uint32_t *o_ids = calloc(16, 4), *o_counts = calloc(16, 4);
    uint64_t *o_vals = calloc(16, 8);
    int n = 0;
    // connector: CRTC_ID = crtc
    o_ids[n]=conn_id; o_counts[n]=1; o_vals[n]= ((u64)prop_crtc_id_conn<<32) | crtc_id; n++;
    // crtc: ACTIVE=1, MODE_ID=blob
    o_ids[n]=crtc_id; o_counts[n]=2; o_vals[n]= ((u64)prop_active<<32) | 1; o_vals[n+1]= ((u64)prop_mode_id<<32) | cb.blob_id; n+=2;
    // plane: FB_ID, CRTC_ID, SRC_*, CRTC_*
    o_ids[n]=plane_id; o_counts[n]=10;
    o_vals[n]  = ((u64)prop_fb_id<<32) | fb.fb_id;
    o_vals[n+1]= ((u64)prop_crtc_id_pl<<32) | crtc_id;
    o_vals[n+2]= ((u64)prop_src_x<<32) | 0;
    o_vals[n+3]= ((u64)prop_src_y<<32) | 0;
    o_vals[n+4]= ((u64)prop_src_w<<32) | (W << 16);
    o_vals[n+5]= ((u64)prop_src_h<<32) | (H << 16);
    o_vals[n+6]= ((u64)prop_crtc_x<<32) | 0;
    o_vals[n+7]= ((u64)prop_crtc_y<<32) | 0;
    o_vals[n+8]= ((u64)prop_crtc_w<<32) | W;
    o_vals[n+9]= ((u64)prop_crtc_h<<32) | H;
    n+=10;

    at.count_objs = n;
    at.objs_ptr = (u64)(uintptr_t)o_ids;
    at.count_props_ptr = (u64)(uintptr_t)o_counts;
    at.props_ptr = (u64)(uintptr_t)o_vals;
    at.flags = 0x00000002 | 0x00000400; // ALLOW_MODESET | NONBLOCK (come Android HWC!)
    at.flags = 0x00000002 | 0x00000400; // ALLOW_MODESET | NONBLOCK (come Android HWC!) // ALLOW_MODESET | NONBLOCK (come Android/HWC!)
    if (ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &at)) { perror("atomic commit"); return 10; }
    printf("ATOMIC COMMIT OK!!! mode=%s fb=%u crtc=%u plane=%u\n", modes[m_idx].name, fb.fb_id, crtc_id, plane_id);
    fflush(stdout);

    // hold
    int elapsed = 0;
    while (elapsed < hold) { sleep(2); elapsed += 2; }
    printf("fine\n");
    return 0;
}
