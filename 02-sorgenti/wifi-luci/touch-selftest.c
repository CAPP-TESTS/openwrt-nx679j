/* touch-selftest — verifica la logica del tocco della UI SENZA toccare il DRM.
 *
 * Include lo stesso sorgente della UI e usa le stesse funzioni (touch_scan,
 * poll_touch, hit_test), ma non apre /dev/dri: si puo' eseguire mentre la UI
 * e' viva sul display, per provare il percorso tocco -> hit-test con un tocco
 * iniettato via uinput.
 */
#include <stdio.h>
#define main ui_main_unused
#include "nx679j-ui.c"
#undef main

int main(void) {
    printf("selftest: scansione device touch\n");
    touch_scan();
    printf("device attivi: %d\n", n_tfd);
    fflush(stdout);
    if (n_tfd == 0) { printf("nessun touch trovato\n"); return 1; }

    for (int i = 0; i < 600; i++) {          /* ~60 s a 10 Hz */
        poll_touch();
        if (t_ev_down) {
            t_ev_down = 0;
            printf("DOWN  x=%d y=%d -> hit_test=%d\n", t_down_x, t_down_y, hit_test(t_down_x, t_down_y));
            fflush(stdout);
        }
        if (t_ev_up) {
            t_ev_up = 0;
            printf("UP    x=%d y=%d -> hit_test=%d\n", t_x, t_y, hit_test(t_x, t_y));
            fflush(stdout);
        }
        usleep(100000);
    }
    printf("selftest: fine\n");
    return 0;
}