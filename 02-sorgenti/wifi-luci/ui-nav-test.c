/* Collaudo da PC della navigazione a tre tasti (VOL su/giu' + POWER).
 *
 * Si compila la STESSA concatenazione che va sul device, con main() rinominato
 * (-Dmain=ui_real_main): qui si chiamano direttamente le funzioni di ingresso
 * vere (key_event, poll_key_timeout) e si guarda la lista degli elementi
 * selezionabili che il DISEGNO ha raccolto. Cosi' il collaudo non duplica la
 * geometria: prova il codice che gira davvero.
 *
 * Il DRM non si tocca: draw_page() scrive su un buffer qualunque.
 *
 * Si compila con -Dmain=ui_real_main, cosi' il main() della UI (che apre
 * /dev/dri) resta definito ma non viene chiamato: qui sotto si riapre il nome
 * main per il collaudo. */
#undef main

static u32 *tb;
static u32 tpit;
static int fails = 0;
static int max_items = 0;

static void chk(int cond, const char *what) {
    if (cond) printf("ok   %s\n", what);
    else { printf("FAIL %s\n", what); fails++; }
}

static void frame(void) {
    toast[0] = 0; toast_until = 0;          /* l'avviso coprirebbe l'evidenza */
    g_focus_on = 1; focus_reset();
    draw_page(tb, tpit);
    g_focus_on = 0;
    if (g_focus_n > max_items) max_items = g_focus_n;
}

static void ev_key(int code, int val) {
    struct input_event e;
    memset(&e, 0, sizeof(e));
    e.type = EV_KEY; e.code = (uint16_t)code; e.value = val;
    key_event(&e);
}
static void vol(int code, int n) { for (int i = 0; i < n; i++) ev_key(code, 1); }
/* Un CLICK singolo: e' lo standby (v173) e scatta alla scadenza della finestra
 * POWER_DBL_MS, perche' due click ravvicinati sono un'ATTIVAZIONE. */
static void pwr_click(void) {
    ev_key(_KEY_POWER, 1); ev_key(_KEY_POWER, 0);
    usleep(POWER_DBL_MS * 1000 + 120000);
    poll_key_timeout();
}
/* DUE click ravvicinati = la conferma del vecchio schema (attiva l'elemento). */
static void pwr_double(void) {
    ev_key(_KEY_POWER, 1); ev_key(_KEY_POWER, 0);
    usleep(60000);
    ev_key(_KEY_POWER, 1); ev_key(_KEY_POWER, 0);
}
static void pwr_long(void) { ev_key(_KEY_POWER, 1); usleep(1600000); poll_key_timeout(); }

static int sel_index(void) {
    for (int i = 0; i < g_focus_n; i++) if (g_focus[i].key == g_sel_key) return i;
    return -1;
}
static const char *sel_txt(void) { int i = sel_index(); return i < 0 ? "(nessuno)" : g_focus[i].txt; }

static int has_id(int id) {
    for (int i = 0; i < g_focus_n; i++) if (g_focus[i].kind == FK_BTN && g_focus[i].id == id) return 1;
    return 0;
}
static int count_txt(const char *t) {
    int c = 0;
    for (int i = 0; i < g_focus_n; i++) if (!strcmp(g_focus[i].txt, t)) c++;
    return c;
}
/* Quanti pixel hanno il colore dell'evidenza (C_WARN): se l'evidenza non si
 * disegna, il conteggio e' zero e il collaudo lo dice. */
static int warn_px(void) {
    int c = 0;
    for (u32 y = 0; y < H; y++) {
        u32 *row = (u32 *)((unsigned char *)tb + (size_t)y * tpit);
        for (u32 x = 0; x < W; x++) if (row[x] == C_WARN) c++;
    }
    return c;
}
static void show_items(void) {
    for (int i = 0; i < g_focus_n; i++)
        printf("     %3d key=%5d %s id=%4d %s (%d,%d %dx%d)\n", i, g_focus[i].key,
               g_focus[i].kind == FK_BTN ? "BTN" : "ROW", g_focus[i].id, g_focus[i].txt,
               g_focus[i].x, g_focus[i].y, g_focus[i].w, g_focus[i].h);
}
/* L'evidenza e' davvero ATTORNO all'elemento selezionato? Si guardano i quattro
 * punti medi della cornice (banda di 6 px attorno al rettangolo), non il
 * conteggio dei pixel: cosi' il controllo non si accontenta di una riga colorata
 * in un punto qualunque dello schermo. */
static int px_is(u32 x, u32 y, u32 col) {
    if (x >= W || y >= H) return 0;
    u32 *row = (u32 *)((unsigned char *)tb + (size_t)y * tpit);
    return row[x] == col;
}
static int ring_ok(int x, int y, int w, int h) {
    return px_is((u32)(x - 3), (u32)(y + h / 2), C_WARN) &&
           px_is((u32)(x + w + 3), (u32)(y + h / 2), C_WARN) &&
           px_is((u32)(x + w / 2), (u32)(y - 3), C_WARN) &&
           px_is((u32)(x + w / 2), (u32)(y + h + 3), C_WARN);
}
static void dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (u32 y = 0; y < H; y++) {
        u32 *row = (u32 *)((unsigned char *)tb + (size_t)y * tpit);
        for (u32 x = 0; x < W; x++) { u32 v = row[x]; fputc(v >> 16 & 255, f); fputc(v >> 8 & 255, f); fputc(v & 255, f); }
    }
    fclose(f);
    printf("     (scritto %s)\n", path);
}

int main(int argc, char **argv) {
    const char *data = argc > 1 ? argv[1] : "/tmp/ui-data.txt";
    tb = malloc((size_t)W * H * 4);
    tpit = W * 4;
    memset(tb, 0, (size_t)W * H * 4);

    kv_load(data);
    printf("== dati: %s (%d chiavi), categoria iniziale %s / %s\n", data, kv_n, cat_names[cat], cat_pages[cat][page_sel[cat]]);

    /* ---- 0. orologio: il gate di key_scan() confronta now_ms() con 0 ----
     * Il collaudo non chiama key_scan() (nessun pmic_pwrkey sul PC), ma il bug
     * che teneva chiusi i tasti sul device stava proprio qui: con un orologio
     * wall-clock troncato a int, now_ms() e' NEGATIVO e ogni scadenza
     * inizializzata a 0 (t_key_scan_next, t_wake_ignore) smette di funzionare.
     * Questi due controlli sarebbero FALLITI con gettimeofday. */
    {
        int a = now_ms(); usleep(2000); int b = now_ms();
        printf("== orologio: now_ms() = %d, dopo 2 ms = %d\n", a, b);
        chk(a > 0, "now_ms() e' positivo: il gate di key_scan() (scadenza a 0) non si blocca");
        chk(b >= a && b - a < 1000, "now_ms() e' monotono e misura in millisecondi");
    }

    /* ---- 1. l'evidenza esiste dal primo frame, sulla pagina iniziale ---- */
    frame();
    printf("== pagina iniziale: %d elementi\n", g_focus_n);
    show_items();
    chk(g_focus_n > 0, "la pagina iniziale ha elementi selezionabili");
    chk(sel_index() >= 0, "una selezione e' agganciata senza premere nulla");
    chk(warn_px() > 0, "l'evidenza (cornice C_WARN) e' disegnata");
    chk(has_id(A_RECON), "il tasto RICONNETTI (modem) e' selezionabile");
    chk(has_id(A_SENS_UI), "il tasto NASCONDI/MOSTRA SIM e' selezionabile");

    /* ---- 1b. la cornice sta ATTORNO all'elemento selezionato e lo segue ---- */
    {
        int i = sel_index();
        int x0 = g_focus[i].x, y0 = g_focus[i].y, w0 = g_focus[i].w, h0 = g_focus[i].h;
        chk(ring_ok(x0, y0, w0, h0), "la cornice avvolge l'elemento selezionato (4 lati verificati a pixel)");
        vol(_KEY_VOLUMEDOWN, 1); frame();
        int j = sel_index();
        /* Il bordo INFERIORE del vecchio elemento non e' piu' colorato: e' il
         * punto che la cornice del vicino non puo' coprire (sarebbe un falso
         * negativo se guardassi un bordo laterale, adiacente al nuovo). */
        chk(!px_is((u32)(x0 + w0 / 2), (u32)(y0 + h0 + 3), C_WARN), "dopo una pressione la cornice ha lasciato il vecchio elemento");
        chk(ring_ok(g_focus[j].x, g_focus[j].y, g_focus[j].w, g_focus[j].h), "la cornice e' sul nuovo elemento");
        vol(_KEY_VOLUMEUP, 1); frame();
        chk(sel_index() == i, "VOL su riporta esattamente sull'elemento di prima");
        for (int k = 0; k < g_focus_n + 2; k++) { if (sel_index() == 0) break; vol(_KEY_VOLUMEUP, 1); }
        chk(sel_index() == 0, "VOL su avvolge in cima (elemento 0)");
    }

    /* ---- 2. NASCONDI/MOSTRA SIM raggiunto con VOL, attivato con POWER ---- */
    {
        int idx = -1, steps = 0;
        for (int i = 0; i < g_focus_n; i++) if (g_focus[i].kind == FK_BTN && g_focus[i].id == A_SENS_UI) idx = i;
        int here = sel_index();
        steps = (idx - here + g_focus_n) % g_focus_n;
        for (int i = 0; i < steps; i++) vol(_KEY_VOLUMEDOWN, 1);
        chk(has_id(A_SENS_UI) && sel_index() == idx, "VOL giu' porta la selezione su NASCONDI SIM");
        printf("     selezione: %s (dopo %d pressioni)\n", sel_txt(), steps);
        pwr_double(); frame();
        chk(hide_sens == 1, "POWER attiva il tasto: i dati SIM sono NASCOSTI");
        chk(count_txt("MOSTRA SIM") == 1, "l'etichetta del tasto e' diventata MOSTRA SIM");
        idx = -1;
        for (int i = 0; i < g_focus_n; i++) if (g_focus[i].kind == FK_BTN && g_focus[i].id == A_SENS_UI) idx = i;
        here = sel_index();
        steps = (idx - here + g_focus_n) % g_focus_n;
        if (steps) vol(_KEY_VOLUMEDOWN, steps);
        chk(sel_index() == idx, "la selezione e' rimasta agganciata al tasto giusto dopo il cambio pagina/etichetta");
        pwr_double(); frame();
        chk(hide_sens == 0, "riattivandolo i dati SIM tornano VISIBILI (tasto reversibile)");
        dump_ppm("/tmp/nav-modem.ppm");
    }

    /* ---- 3. ogni pagina di ogni categoria e' navigabile ---- */
    {
        int worst = 0, worst_cat = 0, worst_page = 0, empty = 0;
        for (cat = 0; cat < CAT_N; cat++) {
            for (int p = 0; p < cat_npages[cat]; p++) {
                page_sel[cat] = p;
                g_sel_key = 0;
                frame();
                int n = g_focus_n;
                if (n < cat_npages[cat] + CAT_N) { printf("FAIL pagina %s/%s: solo %d elementi\n", cat_names[cat], cat_pages[cat][p], n); fails++; }
                if (n == 0) empty++;
                if (n > worst) { worst = n; worst_cat = cat; worst_page = p; }
                for (int i = 0; i < n; i++) {
                    if (g_focus[i].x < 0 || g_focus[i].y < 0 || g_focus[i].x + g_focus[i].w > (int)W || g_focus[i].y + g_focus[i].h > (int)H) {
                        printf("FAIL elemento fuori schermo su %s/%s: id=%d %d,%d %dx%d\n",
                               cat_names[cat], cat_pages[cat][p], g_focus[i].id,
                               g_focus[i].x, g_focus[i].y, g_focus[i].w, g_focus[i].h);
                        fails++;
                    }
                }
                /* giro completo: n pressioni riportano sulla stessa selezione */
                int before = g_sel_key;
                vol(_KEY_VOLUMEDOWN, n);
                if (g_sel_key != before) { printf("FAIL giro incompleto su %s/%s\n", cat_names[cat], cat_pages[cat][p]); fails++; }
                /* i punti di arresto non devono saltare nessun elemento visibile */
                int seen = 0;
                int *marks = calloc(n, sizeof(int));
                g_sel_key = 0;
                for (int k = 0; k < n; k++) {
                    vol(_KEY_VOLUMEDOWN, 1);
                    int si = sel_index();
                    if (si < 0 || si >= n) { printf("FAIL selezione fuori elenco su %s/%s\n", cat_names[cat], cat_pages[cat][p]); fails++; break; }
                    if (marks[si]) break;
                    marks[si] = 1; seen++;
                }
                free(marks);
                if (seen != n) { printf("FAIL elementi non raggiungibili su %s/%s: %d di %d\n", cat_names[cat], cat_pages[cat][p], seen, n); fails++; }
            }
        }
        printf("== pagina piu' densa: %s/%s con %d elementi (FOCUS_MAX=%d)\n",
               cat_names[worst_cat], cat_pages[worst_cat][worst_page], worst, FOCUS_MAX);
        chk(empty == 0, "nessuna pagina senza elementi selezionabili");
        chk(worst < FOCUS_MAX, "la lista resta dentro FOCUS_MAX");
    }

    /* ---- 4. la pagina Wi-Fi: tasti che il tocco NON raggiunge ---- */
    cat = CAT_RETE; page_sel[cat] = 1; g_sel_key = 0; frame();
    chk(has_id(A_WIFI_ON) && has_id(A_WIFI_OFF), "WI-FI SU/GIU sono selezionabili anche se l'hit-test del tocco non li conosce");

    /* ---- 5. campo editabile: POWER apre la tastiera, i tasti la usano ---- */
    cat = CAT_SISTEMA; page_sel[cat] = 4; g_sel_key = 0; frame();
    {
        int mods = count_txt("MOD");
        printf("== pagina Modifica: %d campi con tasto MOD\n", mods);
        chk(mods == 9, "i 9 campi editabili hanno il loro tasto MOD");
        while (!(g_focus[sel_index()].kind == FK_BTN && g_focus[sel_index()].id >= A_ROW0)) vol(_KEY_VOLUMEDOWN, 1);
        printf("     campo selezionato: %s\n", sel_txt());
        pwr_double(); frame();
        chk(kbd_open == 1, "POWER sul campo apre la tastiera");
        chk(!strcmp(kbd_key, "hostname"), "la tastiera punta al campo giusto (hostname)");
        /* modale: solo i tasti della tastiera sono selezionabili */
        {
            int vis = 0, out = 0;
            for (int i = 0; i < g_focus_n; i++) { if (focus_visible(i)) vis++; else out++; }
            printf("== tastiera aperta: %d tasti selezionabili, %d elementi coperti\n", vis, out);
            chk(vis >= 45, "tutti i tasti della tastiera sono raggiungibili");
            chk(out > 0, "gli elementi dietro la tastiera sono esclusi (modale)");
        }
        dump_ppm("/tmp/nav-kbd.ppm");
        /* scrive una lettera: si cerca il tasto 'z' e lo si attiva */
        {
            int before = kbd_len;
            int target = KB_CH + 3 * 10 + 0;              /* riga 3, prima colonna: 'z' */
            while (!(g_focus[sel_index()].kind == FK_BTN && g_focus[sel_index()].id == target)) vol(_KEY_VOLUMEDOWN, 1);
            chk(!strcmp(sel_txt(), "z"), "la selezione arriva sul tasto 'z' della tastiera");
            pwr_double(); frame();
            chk(kbd_len == before + 1 && kbd_buf[kbd_len - 1] == 'z', "POWER scrive il carattere selezionato");
        }
        /* POWER lungo: chiude la tastiera senza applicare */
        dump_ppm("/tmp/nav-kbd-sel.ppm");
        pwr_long();
        chk(kbd_open == 0, "POWER lungo (1.5 s) chiude la tastiera");
    }

    /* ---- 6. POWER lungo su una pagina interna: torna alla prima ---- */
    cat = CAT_MODEM; page_sel[cat] = 2; g_sel_key = 0; frame();
    pwr_long();
    chk(page_sel[cat] == 0, "POWER lungo su una pagina interna torna alla prima pagina");

    /* ---- 7. ciclo completo di modifica: apri, cancella, scrivi, APPLICA ---- */
    cat = CAT_SISTEMA; page_sel[cat] = 4; g_sel_key = 0; frame();
    {
        while (!(g_focus[sel_index()].kind == FK_BTN && g_focus[sel_index()].id >= A_ROW0)) vol(_KEY_VOLUMEDOWN, 1);
        pwr_double(); frame();
        chk(kbd_open == 1 && !strcmp(kbd_key, "hostname"), "secondo ciclo: tastiera su hostname");

        /* CANCELLA: si svuota il campo */
        for (int k = 0; k < 60 && !(g_focus[sel_index()].kind == FK_BTN && g_focus[sel_index()].id == KB_PULISCI); k++) vol(_KEY_VOLUMEDOWN, 1);
        chk(g_focus[sel_index()].id == KB_PULISCI, "la selezione raggiunge PULISCI");
        pwr_double(); frame();
        chk(kbd_len == 0, "PULISCI svuota il campo");

        /* scrive "ok" muovendo la selezione su 'o' e 'k' */
        const char *want = "ok";
        for (int i = 0; i < 2; i++) {
            int steps = 0;
            while (steps++ < 60) {
                int si = sel_index();
                if (g_focus[si].kind == FK_BTN && g_focus[si].id >= KB_CH && g_focus[si].txt[0] == want[i] && g_focus[si].txt[1] == 0) break;
                vol(_KEY_VOLUMEDOWN, 1);
            }
            printf("     tasto '%c' raggiunto in %d passi\n", want[i], steps);
            pwr_double(); frame();
        }
        chk(kbd_len == 2 && !strcmp(kbd_buf, "ok"), "POWER sui due tasti scrive \"ok\" nel campo");

        /* APPLICA: e' l'unico modo di salvare senza tocco */
        for (int k = 0; k < 60 && !(g_focus[sel_index()].kind == FK_BTN && g_focus[sel_index()].id == KB_OK); k++) vol(_KEY_VOLUMEDOWN, 1);
        chk(g_focus[sel_index()].id == KB_OK, "la selezione raggiunge APPLICA");
        pwr_double();
        chk(strstr(toast, "applicato") != NULL, "APPLICA applica il valore (avviso di conferma)");
        printf("     avviso: %s\n", toast);
        frame();
        chk(kbd_open == 0, "APPLICA chiude la tastiera");
    }

    /* ---- 8. ripetizione del volume: liste lunghe senza 77 pressioni ---- */
    cat = CAT_SISTEMA; page_sel[cat] = 1; g_sel_key = 0; frame();
    {
        int before = sel_index();
        int t0 = now_ms();
        while (now_ms() - t0 < 60) ev_key(_KEY_VOLUMEDOWN, 2);   /* raffica di ripetizioni */
        int n1 = (sel_index() - before + g_focus_n) % g_focus_n;
        printf("== raffica di ripetizioni per 60 ms: %d voci saltate (limite 120 ms -> al massimo 1)\n", n1);
        chk(n1 <= 1, "la ripetizione e' limitata: una raffica non fa correre via la selezione");
        usleep(150000);
        ev_key(_KEY_VOLUMEDOWN, 2);
        int n2 = (sel_index() - before + g_focus_n) % g_focus_n;
        chk(n2 == n1 + 1, "dopo il limite la ripetizione avanza di una voce");
    }

    /* ---- 9. giro completo del menu con i SOLI tasti (4 categorie, tutte le pagine) ---- */
    {
        int steps_ok = 0, pages_ok = 0;
        for (int c = 0; c < CAT_N; c++) {
            /* raggiunge il tasto della categoria c e lo attiva */
            for (int k = 0; k < 200; k++) {
                int si = sel_index();
                if (g_focus[si].kind == FK_BTN && g_focus[si].id == A_CAT0 + c) break;
                vol(_KEY_VOLUMEDOWN, 1);
            }
            pwr_double(); frame();
            if (cat == c) steps_ok++;
            for (int p = 0; p < cat_npages[c]; p++) {
                for (int k = 0; k < 200; k++) {
                    int si = sel_index();
                    if (g_focus[si].kind == FK_BTN && g_focus[si].id == A_PAGE0 + p) break;
                    vol(_KEY_VOLUMEDOWN, 1);
                }
                pwr_double(); frame();
                if (cat == c && page_sel[c] == p) pages_ok++;
                else printf("FAIL pagina %s/%s non aperta con i tasti (cat=%d page=%d)\n", cat_names[c], cat_pages[c][p], cat, page_sel[c]);
            }
        }
        printf("== giro con i soli tasti: %d/%d categorie, %d pagine aperte\n", steps_ok, CAT_N, pages_ok);
        chk(steps_ok == CAT_N, "le 4 categorie si aprono con POWER sulla propria linguetta");
        chk(pages_ok == 23, "tutte le 23 pagine si aprono con POWER sulla linguetta");
    }

    /* ---- 10. STANDBY del pannello (v176): NERO + luminosita' 0, LINK VIVO ----
     * Sul PC non c'e' /dev/dri: g_drm_dry=1 fa fingere al percorso DRM quello che
     * sul device fa il kernel, cosi' si prova la MACCHINA A STATI e le righe di
     * log senza toccare il DRM. La regola v176 e' l'OPPOSTO di quella vecchia:
     * allo standby si committa UN FRAME NERO e NON si tocca il display - niente
     * DPMS, niente unprepare, e il loop NON salta piu' il commit (committa nero,
     * ed e' questo che tiene esercitato il link DSI). I quattro fatti che lo
     * provano: g_sb_black (frame nero), g_bl_zero (brightness a 0), g_bl_restore
     * (rimessa al valore salvato) e dpms_calls (deve restare 0). */
    {
        int ffd = -1;
        g_drm_dry = 1;
        standby = 0; pwr_pending = 0; pwr_down = 0; pwr_fired = 0;
        keyfd = open("/dev/null", O_RDONLY);      /* finti nodi dei tasti */
        keyfd_v = open("/dev/null", O_RDONLY);
        loglen = 0;                               /* si guardano solo le righe dello standby */
        /* Luminosita' "di partenza" nota e contatori azzerati: cosi' si vede che
         * l'entrata scrive 0 e il risveglio rimette PROPRIO questo valore. */
        bl_saved = 753; bl_tries = 0;
        g_sb_black = 0; g_bl_zero = 0; g_bl_restore = 0; dpms_calls = 0;
        chk(flip_step(0, &ffd) != 1, "a schermo acceso il loop NON salta il commit (va al DRM)");
        pwr_click();
        chk(standby == 1, "POWER corto: lo schermo si SPEGNE (standby ON)");
        chk(g_sb_black == 1, "standby: viene committato UN FRAME NERO (e' il nuovo 'schermo spento')");
        chk(g_bl_zero == 1, "standby: la luminosita' va a 0 (su AMOLED = pixel spenti)");
        chk(flip_step(0, &ffd) != 1, "in standby il loop NON salta il commit: committa nero (link vivo)");
        chk(strstr(logbuf, "standby: schermo nero (link vivo)") != NULL,
            "log: riga 'standby: schermo nero (link vivo)'");
        chk(strstr(logbuf, "luminosita' a 0") != NULL, "log: riga 'standby: luminosita' a 0'");
        chk(dpms_calls == 0, "lo standby NON tocca la proprieta' DPMS (dpms_calls = 0)");
        { struct stat st;
          chk(keyfd >= 0 && keyfd_v >= 0 && !fstat(keyfd, &st) && !fstat(keyfd_v, &st),
              "in standby i nodi dei tasti restano APERTI (POWER puo' svegliare)"); }
        bl_tries = 0;
        pwr_click();
        chk(standby == 0, "POWER corto a schermo nero: RISVEGLIO");
        chk(strstr(logbuf, "standby: schermo riacceso") != NULL, "log: riga 'standby: schermo riacceso'");
        chk(g_bl_restore == 1 && bl_saved == 753 && bl_tries == 1,
            "il risveglio rimette la luminosita' AL VALORE SALVATO (753)");
        chk(strstr(logbuf, "risveglio: luminosita' rimessa a 753") != NULL,
            "log: riga 'risveglio: luminosita' rimessa a 753'");
        chk(dpms_calls == 0, "anche il risveglio non tocca il DPMS: nessun unprepare, nessun teardown");
        close(keyfd); close(keyfd_v); keyfd = -1; keyfd_v = -1;

        /* POWER lungo: resta "indietro" e non tocca lo standby */
        cat = CAT_MODEM; page_sel[cat] = 2; g_sel_key = 0; frame();
        pwr_long();
        chk(page_sel[cat] == 0 && standby == 0, "POWER lungo resta 'indietro' e non tocca lo standby");

        /* doppio click: ATTIVA (la navigazione a tre tasti resta quella di prima) */
        cat = CAT_MODEM; page_sel[cat] = 0; g_sel_key = 0; frame();
        {
            int idx = -1;
            for (int i = 0; i < g_focus_n; i++) if (g_focus[i].kind == FK_BTN && g_focus[i].id == A_SENS_UI) idx = i;
            int here = sel_index(), steps = (idx - here + g_focus_n) % g_focus_n;
            if (steps) vol(_KEY_VOLUMEDOWN, steps);
            int was = hide_sens;
            pwr_double(); frame();
            chk(hide_sens != was, "POWER doppio: ATTIVA il tasto (navigazione intatta)");
            chk(standby == 0, "il doppio click non spegne lo schermo");
            pwr_double(); frame();
            chk(hide_sens == was, "secondo doppio click: il tasto torna come era");
        }

        /* FAIL-SAFE: lo standby non falla MAI. Se il frame nero non si committa
         * (g_drm_dry=2 = commit fallito) lo schermo resta nero lo stesso, la
         * luminosita' va a 0 lo stesso, e il click successivo risveglia. */
        g_drm_dry = 2; loglen = 0;
        g_bl_zero = 0;
        pwr_click();
        chk(standby == 1 && g_bl_zero == 1, "frame nero non committato: si resta neri, luminosita' a 0");
        chk(strstr(logbuf, "standby: frame nero non committato") != NULL,
            "log: riga 'standby: frame nero non committato (resta nero comunque)'");
        chk(strstr(logbuf, "standby: schermo nero (link vivo)") != NULL,
            "log: lo standby si dichiara comunque nero (nessun fallimento silenzioso)");
        g_drm_dry = 1;
        pwr_click();
        chk(standby == 0, "dopo un frame nero non committato un nuovo click risveglia");
        g_drm_dry = 0;
    }

    /* ---- 11. REGRESSIONE (v174, tenuta nel v176): il click che RISVEGLIA non
     * rientra in standby ----
     * MISURATO sul device (display-late.log):
     *   standby: schermo nero
     *   POWER corto: risveglio
     *   standby: schermo riacceso
     *   standby: schermo nero      <-- lo STESSO click fisico, di nuovo standby
     * Il nodo del tasto consegna DUE esemplari (press+release) di un click solo:
     * il secondo, trovando lo schermo gia' acceso, armava la finestra del click
     * e 350 ms dopo lo schermo tornava nero. Qui si manda la stessa sequenza: il
     * risveglio deve CONSUMARE il click (stato azzerato) e la guardia del
     * risveglio (POWER_WAKE_GUARD_MS) deve impedire il riarmo. */
    {
        int ffd = -1;
        g_drm_dry = 1;
        /* Stato noto: nessuna guardia in corso (t_wake_ignore e' un istante di
         * CLOCK_MONOTONIC: il collaudo lo azzera invece di dormirci sopra). */
        standby = 0; pwr_pending = 0; pwr_pend_t0 = 0; pwr_down = 0; pwr_fired = 0; t_wake_ignore = 0;
        pwr_click();
        chk(standby == 1, "regressione: premessa - il primo click spegne il pannello");
        loglen = 0;
        ev_key(_KEY_POWER, 1); ev_key(_KEY_POWER, 0);        /* click che sveglia */
        chk(standby == 0, "regressione: il click a pannello spento RISVEGLIA");
        ev_key(_KEY_POWER, 1); ev_key(_KEY_POWER, 0);        /* secondo esemplare */
        usleep(POWER_DBL_MS * 1000 + 120000);                /* oltre la finestra del click */
        poll_key_timeout();                                  /* il giro di loop che decide */
        chk(standby == 0, "il click del risveglio NON rientra in standby (schermo resta ON)");
        chk(pwr_pending == 0 && pwr_pend_t0 == 0, "dopo il risveglio lo stato del click e' azzerato");
        chk(strstr(logbuf, "standby: schermo riacceso") != NULL, "log: riga 'standby: schermo riacceso'");
        chk(strstr(logbuf, "standby: schermo nero") == NULL,
            "log: NESSUNA riga 'standby: schermo nero' dopo il risveglio (era il difetto)");
        chk(strstr(logbuf, "POWER corto: ignorato") != NULL,
            "log: il secondo esemplare del click e' dichiarato ignorato (guardia)");
        chk(flip_step(0, &ffd) != 1, "dopo il risveglio il loop riprende a committare (pannello acceso)");

        /* Un click gia' IN ATTESA non deve sopravvivere al risveglio: lo standby
         * lo puo' accendere anche il comando di servizio (/tmp/ui-standby). */
        standby = 0; pwr_pending = 0; pwr_pend_t0 = 0; pwr_down = 0; pwr_fired = 0; t_wake_ignore = 0;
        standby_enter();
        chk(standby == 1, "regressione: premessa - pannello spento dal comando di servizio");
        pwr_pending = 1; pwr_pend_t0 = now_ms() - POWER_DBL_MS - 50;   /* click mai consumato */
        standby_exit();                                                /* risveglio dal comando di servizio */
        chk(standby == 0 && pwr_pending == 0, "il risveglio azzera un click rimasto in attesa");
        poll_key_timeout();
        chk(standby == 0, "il click in attesa non rispegne il pannello dopo il risveglio (guardia)");

        /* La guardia non e' permanente: scaduta, il tasto torna a fare standby. */
        usleep((POWER_WAKE_GUARD_MS + 80) * 1000);
        pwr_click();
        chk(standby == 1, "scaduta la guardia un nuovo click torna a spegnere");
        pwr_click();
        chk(standby == 0, "e il click successivo risveglia di nuovo (guardia non permanente)");
        g_drm_dry = 0;
    }

    printf("== elementi massimi raccolti in un frame: %d\n", max_items);
    printf("== %s: %d controlli falliti\n", fails ? "FALLITO" : "PASSATO", fails);
    return fails ? 1 : 0;
}
