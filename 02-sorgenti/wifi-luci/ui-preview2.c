/* ui-preview2 — rende le pagine del menu SU FILE, per guardarle senza rischi.
 * Usa le stesse funzioni del binario per il device. */
#include <stdio.h>
#define main ui_main_unused
#include "nx679j-ui.c"
#undef main

static u32 pbuf[1080 * 2400];

static void ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n1080 2400\n255\n");
    for (int i = 0; i < 1080 * 2400; i++) {
        u32 p = pbuf[i];
        fputc((p >> 16) & 0xff, f);
        fputc((p >> 8) & 0xff, f);
        fputc(p & 0xff, f);
    }
    fclose(f);
    printf("%s ok\n", path);
}

int main(int argc, char **argv) {
    kv_load(argc > 1 ? argv[1] : "ui-sample.txt");
    printf("chiavi=%d\n", kv_n);
    const char *names[CAT_N] = { "stato", "rete", "modem", "sistema" };
    for (int c = 0; c < CAT_N; c++) {
        cat = c;
        page_sel[c] = 0;
        draw_page(pbuf, 1080 * 4);
        char p[64];
        snprintf(p, sizeof(p), "prev-%s-0.ppm", names[c]);
        ppm(p);
    }
    /* le pagine piu' dense: servizi (4 tasti per riga), interfacce, log */
    cat = CAT_SISTEMA; page_sel[CAT_SISTEMA] = 0; draw_page(pbuf, 1080 * 4); ppm("prev-servizi.ppm");
    cat = CAT_RETE;    page_sel[CAT_RETE] = 0;    draw_page(pbuf, 1080 * 4); ppm("prev-iface.ppm");
    cat = CAT_STATO;   page_sel[CAT_STATO] = 1;   draw_page(pbuf, 1080 * 4); ppm("prev-log.ppm");
    cat = CAT_SISTEMA; page_sel[CAT_SISTEMA] = 1; draw_page(pbuf, 1080 * 4); ppm("prev-impostazioni.ppm");
    /* hit-test: tab categoria, riga pagine, tasto di riga (4o tasto dei servizi) */
    cat = CAT_SISTEMA; page_sel[CAT_SISTEMA] = 0;
    printf("cat= %d | pagina= %d | tasto_riga= %d | fuori= %d\n",
           hit_test(150, 2300), hit_test(300, 200), hit_test(1000, 330 + 92 + 40), hit_test(500, 1500));
    return 0;
}
