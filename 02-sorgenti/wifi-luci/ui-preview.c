/* ui-preview — rende le pagine della UI su file PPM, per verificarle sull'host.
 * Non tocca il device: include lo stesso sorgente e chiama le stesse funzioni. */
#include <stdio.h>
#define main ui_main_unused
#include "nx679j-ui.c"
#undef main

static void write_ppm(const char *path, u32 *m, u32 pitch_bytes) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%u %u\n255\n", W, H);
    for (u32 y = 0; y < H; y++) {
        u32 *row = (u32 *)((unsigned char *)m + (size_t)y * pitch_bytes);
        for (u32 x = 0; x < W; x++) {
            u32 p = row[x];
            unsigned char rgb[3] = { (p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *data = argc > 1 ? argv[1] : "/tmp/ui-sample.txt";
    g_quiet = 1;
    kv_load(data);
    printf("chiavi caricate: %d\n", kv_n);
    u32 *m = malloc((size_t)W * H * 4);
    if (!m) return 1;
    const char *names[3] = { "modem", "rete", "sistema" };
    for (int p = 0; p < 3; p++) {
        page = p;
        draw_page(m, W * 4);
        char path[64];
        snprintf(path, sizeof(path), "preview-%s.ppm", names[p]);
        write_ppm(path, m, W * 4);
        printf("scritto %s\n", path);
    }
    /* Verifica dell'interpretazione del tocco (senza device) */
    printf("\n--- hit_test ---\n");
    struct { int x, y; const char *desc; } pts[] = {
        { 540, 2200, "tab centro (RETE)" },
        { 100, 2200, "tab sinistra (MODEM)" },
        { 900, 2200, "tab destra (SISTEMA)" },
        { 540, 1950, "pulsante principale" },
        { 300, 1950, "meta' sinistra del pulsante" },
        { 900, 1950, "meta' destra del pulsante" },
        { 540, 1000, "zona dati (nessuna azione)" },
        { 540, 100,  "barra in alto (nessuna azione)" },
    };
    for (unsigned i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
        int id = hit_test(pts[i].x, pts[i].y);
        printf("  (%4d,%4d) -> %3d  %s\n", pts[i].x, pts[i].y, id, pts[i].desc);
    }
    free(m);
    return 0;
}
