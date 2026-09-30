/* drm-probe — legge (solo lettura) una proprieta' DRM e i suoi valori enumerati.
 *
 * Serve a rispondere a una domanda precisa: il driver espone idle_pc_state, e
 * quali valori accetta? Non scrive nulla e non prende il master: aprire card0
 * mentre un altro client (la UI) e' master e' sicuro; il pericolo documentato
 * su questo device e' aprire la card quando NESSUN master esiste.
 *
 * uso: drm-probe [nome_proprieta]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>

typedef uint64_t u64; typedef uint32_t u32;

#define GETPROPERTY    0xc04064aa
#define OBJ_GETPROPS   0xc02064b9

struct get_property { u64 values_ptr, enum_blob_ptr; u32 prop_id, flags; char name[32]; u32 count_values, count_enum_blobs; };
struct prop_enum { u64 value; char name[32]; };
struct obj_props { u64 props_ptr, values_ptr; u32 count_props, obj_id, obj_type; };

#define OBJ_CRTC 0xcccccccc
#define OBJ_CONN 0xc0c0c0c0

#define DRM_MODE_PROP_EXTENDED_TYPE 0x0000ffc0
#define DRM_MODE_PROP_LEGACY_TYPE   0x0000000f
#define DRM_MODE_PROP_RANGE         0x00000002
#define DRM_MODE_PROP_IMMUTABLE     0x00000004
#define DRM_MODE_PROP_ENUM          0x00000008
#define DRM_MODE_PROP_BITMASK       0x00000010
#define DRM_MODE_PROP_BLOB          0x00000020

static int fd = -1;

static u32 find_prop(const char *name) {
    for (u32 id = 1; id < 500; id++) {
        struct get_property gp; memset(&gp, 0, sizeof(gp)); gp.prop_id = id;
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return id;
    }
    return 0;
}

static void dump_prop(const char *name) {
    struct get_property gp; memset(&gp, 0, sizeof(gp));
    u32 id = find_prop(name);
    if (!id) { printf("%-22s ASSENTE\n", name); return; }
    gp.prop_id = id;
    if (ioctl(fd, GETPROPERTY, &gp)) { printf("%-22s errore ioctl errno=%d\n", name, errno); return; }
    printf("%-22s id=%u flags=0x%08x  ", name, id, gp.flags);
    u32 legacy = gp.flags & DRM_MODE_PROP_LEGACY_TYPE;
    if (legacy & DRM_MODE_PROP_ENUM) printf("tipo=ENUM\n"); 
    else if (legacy & DRM_MODE_PROP_BITMASK) printf("tipo=BITMASK\n");
    else if (legacy & DRM_MODE_PROP_RANGE) printf("tipo=RANGE\n");
    else if (legacy & DRM_MODE_PROP_BLOB) printf("tipo=BLOB\n");
    else printf("tipo=0x%x\n", legacy);
    if (legacy & DRM_MODE_PROP_RANGE) {
        u64 vals[2] = {0, 0};
        struct get_property g2; memset(&g2, 0, sizeof(g2));
        g2.prop_id = id; g2.values_ptr = (u64)(uintptr_t)vals; g2.count_values = 2;
        if (!ioctl(fd, GETPROPERTY, &g2)) printf("                         RANGE min=%llu max=%llu\n",
            (unsigned long long)vals[0], (unsigned long long)vals[1]);
    }
    if (legacy & (DRM_MODE_PROP_ENUM | DRM_MODE_PROP_BITMASK)) {
        struct prop_enum ents[32];
        memset(ents, 0, sizeof(ents));
        struct get_property g3; memset(&g3, 0, sizeof(g3));
        g3.prop_id = id; g3.enum_blob_ptr = (u64)(uintptr_t)ents; g3.count_enum_blobs = 32;
        if (!ioctl(fd, GETPROPERTY, &g3)) {
            for (u32 i = 0; i < g3.count_enum_blobs && i < 32; i++)
                printf("                         valore %llu = \"%s\"\n",
                       (unsigned long long)ents[i].value, ents[i].name);
        }
    }
}

int main(int argc, char **argv) {
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { perror("open card0"); return 1; }
    printf("card0 aperto (nessun SET_MASTER: solo lettura)\n");

    /* Quali proprieta' appartengono DAVVERO al CRTC, con il valore attuale.
     * Serve prima di scrivere: un nome puo' esistere su piu' oggetti e un commit
     * atomico con la proprieta' sbagliata viene rifiutato in blocco. */
    u32 crtc_id = 152;
    if (argc > 1 && !strcmp(argv[1], "--crtc")) crtc_id = (u32)atoi(argv[2]);
    u32 pids[64]; u64 pvals[64];
    memset(pids, 0, sizeof(pids)); memset(pvals, 0, sizeof(pvals));
    struct obj_props op; memset(&op, 0, sizeof(op));
    op.props_ptr = (u64)(uintptr_t)pids; op.values_ptr = (u64)(uintptr_t)pvals;
    op.count_props = 64; op.obj_id = crtc_id; op.obj_type = OBJ_CRTC;
    if (ioctl(fd, OBJ_GETPROPS, &op)) {
        printf("OBJ_GETPROPS crtc=%u fallito errno=%d\n", crtc_id, errno);
    } else {
        printf("\nproprieta' del CRTC %u (%u):\n", crtc_id, op.count_props);
        for (u32 i = 0; i < op.count_props && i < 64; i++) {
            struct get_property gp; memset(&gp, 0, sizeof(gp)); gp.prop_id = pids[i];
            const char *nm = "?";
            if (!ioctl(fd, GETPROPERTY, &gp)) nm = gp.name;
            printf("   %-22s id=%-4u valore=%llu\n", nm, pids[i], (unsigned long long)pvals[i]);
        }
    }
    printf("\n");
    if (argc > 1 && !strcmp(argv[1], "--crtc")) { close(fd); return 0; }
    if (argc > 1) { for (int i = 1; i < argc; i++) dump_prop(argv[i]); }
    else {
        dump_prop("idle_pc_state");
        dump_prop("autorefresh");
        dump_prop("frame_trigger_mode");
        dump_prop("tearcheck_enable");
    }
    close(fd);
    return 0;
}