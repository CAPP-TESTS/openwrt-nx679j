
/* ---------- main: init DRM (come kiosk3) + loop ---------- */
/* ---------- standby del pannello + tasto laterale ----------
 * DESIGN v176 - come fa lo STOCK ANDROID: lo schermo si "spegne" con un FRAME
 * NERO e la luminosita' a 0, MAI con un teardown del display.
 * Dalla ricerca sui sorgenti (4 filoni paralleli, 25/09):
 *   1. lo stock non unprepara il display allo screen-off: logga
 *      "SetDisplayState: Set state = 0 ... teardown = 0" e tiene il link vivo;
 *   2. il ciclo DPMS-off (unprepare) + DPMS-on NON riporta in vita il link su
 *      questo albero vendor: resta "wait_for_idle: -110 / wr_ptr_irq wait
 *      failed" e il pannello e' nero anche con dpms=On;
 *   3. su AMOLED il nero (pixel a 0) col backlight a 0 e' indistinguibile da un
 *      pannello spento - e il link resta esercitato.
 * Quindi:
 *   standby  = UN frame nero committato (percorso plane-only normale) +
 *              /sys/class/backlight/panel0-backlight/brightness = 0;
 *   risveglio = luminosita' salvata riscritta + ridisegno PIENO.
 * Niente DPMS, niente unprepare, niente stop dei commit: il loop continua a
 * girare e a committare nero (economico, e tiene il link esercitato). Il battito
 * "standby: loop vivo (nero)" ogni 10 s dice dall'esterno che e' vivo.
 */
#define _KEY_POWER      116   /* tasto laterale    (linux/input-event-codes.h) */
#define _KEY_VOLUMEUP   115   /* bilanciere volume  (pmic_resin su questo device) */
#define _KEY_VOLUMEDOWN 114

static u32 g_fb[2] = { 0, 0 };
static int g_cur = 0;
static int standby = 0;
static long t_wake_ignore = 0;
/* Guardia dopo il RISVEGLIO, piu' lunga della finestra del click (POWER_DBL_MS,
 * definita piu' sotto): il click che ha svegliato il pannello e' CONSUMATO dal
 * risveglio e entro questa finestra nessun evento puo' riarmare lo standby
 * (vedi standby_exit e key_event). Vale anche per il gas del tocco. */
#define POWER_WAKE_GUARD_MS 600

/* ---------- VECCHIA VIA DPMS: INERTE, NON USATA DALLO STANDBY (v176) ----------
 * Qui stava lo spegnimento via proprieta' DPMS del connettore (il kernel
 * costruiva il commit: drm_atomic_connector_commit_dpms, DPMS Off = 3, tipo
 * oggetto connettore = 0xc0c0c0c0). MISURATO sul device il 25/09: quel commit
 * unprepara il display e il ciclo DPMS-off -> DPMS-on NON lo riporta in vita -
 * il DSI muore con "wait_for_idle: -110 / wr_ptr_irq wait failed" e il pannello
 * resta nero. Lo stock non lo fa mai: allo screen-off logga
 * "SetDisplayState: Set state = 0 ... teardown = 0" e tiene il link vivo.
 * Il codice resta QUI, inerte e commentato, perche' dice cosa non si deve fare:
 * `dpms_calls` e' il contatore che il collaudo su PC legge - deve restare 0.
 * Numeri dal sorgente del kernel: drm.h OBJ_SETPROPERTY = DRM_IOWR(0xBA, ...). */
struct obj_setprop { u64 value; u32 prop_id; u32 obj_id; u32 obj_type; };
#define OBJ_SETPROP _IOWR('d', 0xBA, struct obj_setprop)

static u32 dpms_id = 0;
static int dpms_calls = 0;      /* quante volte la via DPMS e' stata toccata (deve restare 0) */

static int __attribute__((unused)) dpms_set(u64 v) {
    dpms_calls++;
    if (!dpms_id) dpms_id = find_prop_global("DPMS");
    if (!dpms_id) return -1;
    struct obj_setprop p; memset(&p, 0, sizeof(p));
    p.value = v; p.prop_id = dpms_id; p.obj_id = conn; p.obj_type = 0xc0c0c0c0;
    errno = 0;
    return ioctl(fd, OBJ_SETPROP, &p) ? -errno : 0;
}

/* Vecchio ripiego dello spegnimento (commit a mano con i tre oggetti: piano
 * staccato, schermo non attivo, connettore staccato). INERTE dal v176: lo
 * standby non spegne piu' niente. Tenuto perche' documenta l'errore EINVAL
 * (errno 22) visto il 25/09 e la regola del kernel "either both CRTC and FB
 * must be set, or neither". */
static int __attribute__((unused)) commit_off(void) {
    u32 objs[3]; u32 counts[3]; u32 props[8]; u64 vals[8]; int np = 0, no = 0, c0;
    /* I TRE pezzi di uno spegnimento atomico corretto: piano staccato, schermo
     * non attivo, connettore staccato. Senza il terzo il kernel rifiuta con
     * EINVAL (errno 22) - errore visto e corretto il 2026-09-25. */
    c0 = np; props[np] = active_id; vals[np++] = 0;
    counts[no] = np - c0; objs[no++] = crtc;
    c0 = np; props[np] = conn_crtcid; vals[np++] = 0;
    counts[no] = np - c0; objs[no++] = conn;
    /* Il piano va staccato DAL TUTTO: crtc_id E fb_id a zero. Il kernel
     * rifiuta "FB senza CRTC" con EINVAL (drm_atomic_plane_check: "either both
     * CRTC and FB must be set, or neither"): lasciando il fb si ottiene proprio
     * quello stato - errore visto e corretto il 2026-09-25 leggendo il sorgente. */
    c0 = np; props[np] = p_crtcid; vals[np++] = 0;
    props[np] = p_fbid; vals[np++] = 0;
    counts[no] = np - c0; objs[no++] = plane;
    struct atomic_req ar; memset(&ar, 0, sizeof(ar));
    ar.flags = 0x400; ar.count_objs = no;                   /* ALLOW_MODESET */
    ar.objs_ptr = (u64)(uintptr_t)objs; ar.count_props_ptr = (u64)(uintptr_t)counts;
    ar.props_ptr = (u64)(uintptr_t)props; ar.prop_values_ptr = (u64)(uintptr_t)vals;
    errno = 0;
    int r = ioctl(fd, ATOMIC, &ar);
    return r ? -errno : 0;
}

/* ---------- STANDBY DEL PANNELLO: la storia, in breve ----------
 * v173. L'innesto del 25/09 uccideva la UI: il commit di spegnimento falliva con
 * EINVAL **ma aveva gia' spento il pannello** (CRTC a meta'), il codice credeva
 * di aver fallito, il loop continuava a committare su un CRTC inattivo e li' il
 * driver vendor non completa MAI: ciclo fermo per sempre (stato D), con lui
 * morti tasto e touch, letti nello stesso loop. Da allora il flag `standby` sale
 * PRIMA del tentativo e il ciclo non si e' piu' fermato.
 * v175. Risveglio via DPMS del connettore + riscrittura della luminosita'.
 * MISURATO sul device: il commit DPMS-off unprepara il display, il DPMS-on NON lo
 * riporta in vita ("wait_for_idle: -110 / wr_ptr_irq wait failed", pannello nero
 * con dpms=On). E' il ciclo che questo albero vendor non sa fare.
 * v176 (ATTUALE). Niente piu' teardown: si fa come lo stock Android, che allo
 * screen-off logga "SetDisplayState: Set state = 0 ... teardown = 0". Lo schermo
 * si spegne con UN FRAME NERO e la luminosita' a 0, il display resta preparato e
 * il link continua a essere esercitato dai commit del loop. Su AMOLED il nero e'
 * pixel spenti: indistinguibile da un pannello off, senza rischiare il link. */
static int g_drm_dry = 0;        /* 0 = parla col DRM; 1 = finge ok; 2 = finge errore
                                  * (solo per il collaudo su PC: li' /dev/dri non esiste) */
/* Stato dei tasti POWER, dichiarato QUI perche' standby_enter() deve poter
 * annullare un click in attesa quando lo standby lo accende un altro comando. */
static int pwr_down = 0, pwr_fired = 0, pwr_t0 = 0;
static int pwr_pending = 0, pwr_pend_t0 = 0;   /* rilascio in attesa: click o doppio click? */
static int pwr_guard = 0;      /* la pressione in CORSO e' nata nella guardia del risveglio:
                                * e' il secondo esemplare del click che ha appena svegliato
                                * lo schermo (MISURATO sul device: il nodo del tasto ne
                                * consegna due) e il suo rilascio non deve armare lo standby. */

/* ---------- FRAME NERO: lo "spegnimento" su AMOLED (v176) ----------
 * Due buffer da riempire, non uno: il loop alterna i due fb a ogni flip, quindi
 * se uno restasse col disegno vecchio il primo flip lo rimetterebbe a schermo. */
static u32 g_pitch[2] = { 0, 0 };   /* riempito dal main: serve a scrivere il nero */
static int g_sb_black = 0;          /* 1 = un frame nero e' stato preparato/committato */

/* 0 = nero in QUALUNQUE ordine di canali: nessun colore da indovinare. */
static void sb_fill_black(void) {
    for (int b = 0; b < 2; b++) {
        if (!kmaps[b] || !g_pitch[b]) continue;
        unsigned char *base = (unsigned char *)kmaps[b];
        for (int y = 0; y < (int)H; y++) {
            u32 *row = (u32 *)(base + (size_t)y * g_pitch[b]);
            for (int x = 0; x < (int)W; x++) row[x] = 0;
        }
    }
}

/* UN frame nero committato col percorso NORMALE (plane-only, lo stesso del
 * loop): nessun DPMS, nessun unprepare, nessuna richiesta di disattivazione.
 * 0 = committato, -1 = non committato (il pannello resta comunque nero). */
static int sb_black_frame(void) {
    sb_fill_black();
    if (g_drm_dry) { g_sb_black = 1; return g_drm_dry == 2 ? -1 : 0; }
    if (!g_fb[g_cur]) return -1;
    int f = -1;
    if (commit_plane_only(g_fb[g_cur], &f)) return -1;
    wait_fence(f, 40);          /* il frame nero si aspetta: e' il primo dei tanti */
    g_sb_black = 1;
    return 0;
}


/* ---------- LUMINOSITA' DEL PANNELLO (v175) ----------
 * MISURATO sul device il 25/09 (dmesg A/B, avvio contro risveglio).
 * Abilitazione all'avvio (93.15 s), che FUNZIONA:
 *   dsi_display_set_mode -> nubia_read_panel_type -> nubia_check_flash_demura
 *   -> sde_backlight_device_update_status -> dsi_panel_set_backlight lvl:1252
 * Risveglio (164.84 s), che lascia il pannello NERO:
 *   dsi_display_set_mode -> panel_power_on (vddio/vci) -> sde_backlight_device_update_status
 *   ...e 23 ms dopo panel_power_off, e `dsi_panel_set_backlight` NON arriva MAI.
 * Dal 203 s il link e' morto: sde_encoder_phys_cmd_wait_for_tx_complete
 * "failed wait_for_idle: -110" (ETIMEDOUT) + wr_ptr_irq wait failed, mentre il
 * contatore dei frame avanza (il commit non arriva mai al pannello).
 * Quindi: la via che spegne (DPMS del connettore) non veniva usata per accendere,
 * e la luminosita' - un comando DSI - non veniva rimessa. Tutte e due le cose
 * stanno QUI, in due passi separati e leggibili. */
static const char *bl_path = "/sys/class/backlight/panel0-backlight/brightness";
static long bl_saved = -1;      /* livello letto PRIMA di azzerarlo */
static int bl_tries = 0;        /* riscritture della luminosita' (contato per il collaudo) */
static int g_bl_zero = 0;       /* quante volte si e' scritto 0 (collaudo: standby ON) */
static int g_bl_restore = 0;    /* quante volte si e' rimessa la salvata (collaudo: risveglio) */

static long bl_read(void) {
    if (g_drm_dry) return -1;
    FILE *f = fopen(bl_path, "r");
    if (!f) return -1;
    char b[32] = { 0 };
    if (!fgets(b, sizeof(b) - 1, f)) { fclose(f); return -1; }
    fclose(f);
    return atol(b);
}

/* Scrittura best-effort: un fallimento non annulla ne' lo standby ne' il
 * risveglio (il log lo dice e si prosegue). Livello 0 = pixel AMOLED spenti. */
static int bl_set(long v) {
    if (v == 0) g_bl_zero++;
    if (g_drm_dry) return 0;
    FILE *f = fopen(bl_path, "w");
    if (!f) return -1;
    fprintf(f, "%ld", v);
    if (fclose(f)) return -1;
    return 0;
}

static int bl_restore(void) {
    if (bl_saved < 0) return -1;
    bl_tries++; g_bl_restore++;
    return bl_set(bl_saved);
}

/* ---------- ENTRATA IN STANDBY (v176) ----------
 * Tre passi, nell'ordine: salva la luminosita', committa UN frame nero, azzera
 * la luminosita'. Niente DPMS, niente unprepare, niente stop dei commit. */
static void standby_enter(void) {
    if (standby) return;
    /* Il livello si legge QUI, PRIMA di azzerarlo: dopo non e' piu' quello
     * giusto da rimettere al risveglio. */
    if (!g_drm_dry) { long b = bl_read(); if (b >= 0) { bl_saved = b; L("standby: luminosita' salvata = %ld", bl_saved); } }
    /* 1. UN frame nero sul percorso normale: e' questo che tiene il link DSI
     *    esercitato e, su AMOLED, spegne i pixel (nero = pixel spenti). */
    if (sb_black_frame() == 0) L("standby: frame nero committato (link vivo)");
    else L("standby: frame nero non committato (resta nero comunque)");
    standby = 1;                     /* da qui in poi il loop disegna NERO, e continua */
    pwr_pending = 0;                 /* un click rimasto in attesa non deve risvegliare */
    /* 2. Luminosita' a 0. Best-effort: se la scrittura non riesce si prosegue -
     *    lo standby non fallisce mai per il backlight. */
    if (bl_set(0) == 0) L("standby: luminosita' a 0");
    else L("standby: luminosita' non azzerata (errno=%d) - prosegue", errno);
    L("standby: schermo nero (link vivo)");
}

static void standby_exit(void) {
    if (!standby) return;
    standby = 0; dirty_req = 1;      /* ridisegno PIENO al prossimo giro del loop */
    /* Prima la luminosita', poi il disegno: il frame che torna a schermo non deve
     * fare un lampo a luminosita' 0 (o restare invisibile). */
    if (bl_restore() == 0) L("risveglio: luminosita' rimessa a %ld (scrittura %d)", bl_saved, bl_tries);
    else L("risveglio: luminosita' non riscritta (saved=%ld) - best-effort", bl_saved);
    /* IL CLICK CHE HA SVEGLIATO E' CONSUMATO DAL RISVEGLIO: il suo unico effetto
     * e' stato questo. Servono tutte e due le cose, altrimenti lo schermo torna
     * nero da solo 350 ms dopo:
     *   (1) si AZZERA lo stato del tasto - un click gia' in attesa non deve
     *       scadere e rispegnere (standby_enter() da qui in poi non arriva);
     *   (2) per POWER_WAKE_GUARD_MS non si arma piu' niente - questo device
     *       consegna DUE esemplari (press+release) di un click solo: il secondo,
     *       trovando il pannello gia' acceso, apriva una nuova attesa. La guardia
     *       copre anche il gas del tasto (t_wake_ignore: stesso orologio, stesso
     *       scopo) ed e' piu' lunga della finestra del click (POWER_DBL_MS),
     *       cioe' della durata entro cui il secondo esemplare arriva. */
    t_wake_ignore = now_ms() + POWER_WAKE_GUARD_MS;
    pwr_pending = 0; pwr_pend_t0 = 0; pwr_down = 0; pwr_fired = 0; pwr_guard = 0;
    L("standby: schermo riacceso");
    L("standby: risveglio, click consumato (nessun riarmo per %d ms)", POWER_WAKE_GUARD_MS);
}

/* Il passo di commit del loop. UNICO punto che tocca il DRM. In standby NON si
 * salta piu': il loop continua a committare frame NERI, che e' esattamente cio'
 * che tiene vivo il link DSI (era il teardown a ucciderlo). 0 = inviato e
 * riuscito, <0 = errore. */
static int flip_step(u32 fbid, int *ffd) {
    return commit_plane_only(fbid, ffd);
}

/* ---------- ingresso: due dispositivi, cercati PER NOME ----------
 * I numeri eventN cambiano a ogni avvio (dipendono da quando i moduli vendor
 * vengono caricati): la ricerca e' per nome, come e' sempre stata. I nomi sono
 * due perche' il tasto laterale (POWER) e il bilanciere volume stanno su nodi
 * diversi: pmic_pwrkey e pmic_resin. Se il bilanciere risultasse esposto sullo
 * STESSO nodo del tasto laterale, i suoi codici arrivano comunque: la lettura
 * non dipende da chi dei due li manda. */
static int keyfd = -1;      /* pmic_pwrkey  -> POWER (tasto laterale) */
static int keyfd_v = -1;    /* pmic_resin   -> VOLUME SU/GIU' */
static int t_key_scan_next = 0;

static void key_scan(void) {
    if (keyfd >= 0 && keyfd_v >= 0) return;      /* trovati tutti e due: basta */
    int tn = now_ms();
    if (tn < t_key_scan_next) return;            /* il giro costa 32 open: uno ogni 5 s */
    t_key_scan_next = tn + 5000;
    for (int i = 0; i < 32; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/sys/class/input/event%d/device/name", i);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        char nm[64] = {0};
        if (!fgets(nm, sizeof(nm), f)) nm[0] = 0;
        fclose(f);
        int *slot = NULL;
        const char *who = "";
        if (keyfd < 0 && strstr(nm, "pmic_pwrkey"))       { slot = &keyfd;   who = "pwrkey/POWER"; }
        else if (keyfd_v < 0 && strstr(nm, "pmic_resin")) { slot = &keyfd_v; who = "resin/volume"; }
        if (!slot) continue;
        snprintf(p, sizeof(p), "/dev/input/event%d", i);
        *slot = open(p, O_RDONLY | O_NONBLOCK);
        if (*slot >= 0) L("tasto: %s aperto (%s, fd=%d)", p, who, *slot);
        else L("tasto: %s non apribile errno=%d", p, errno);
    }
}

/* ---------- pressione lunga del tasto laterale ----------
 * 1.5 s = "indietro". Il rilascio puo' non arrivare (o arrivare molto dopo) se
 * il loop e' occupato in un commit: per questo la scadenza si guarda ANCHE a
 * eventi finiti, non solo alla ricezione del rilascio. Senza quel controllo un
 * evento di rilascio perso lascerebbe il tasto "giu'" per sempre e la pressione
 * successiva verrebbe letta come lunga. */
#define POWER_LONG_MS 1500
#define POWER_DBL_MS   350   /* finestra del doppio click che ATTIVA (vedi key_event) */
static int t_vol_last = 0;      /* ultima mossa da ripetizione (limite 120 ms) */

static void poll_key_timeout(void) {
    /* pressione lunga = indietro. Il click in attesa decade: era una lunga. */
    if (pwr_down && !pwr_fired && now_ms() - pwr_t0 >= POWER_LONG_MS) {
        pwr_fired = 1; pwr_pending = 0;
        L("POWER lungo (%d ms): indietro", POWER_LONG_MS);
        focus_back();
    }
    /* Un click singolo e' scaduto: e' lo standby (schermo spento/acceso). Si
     * aspetta la finestra perche' due click ravvicinati sono un'ATTIVAZIONE:
     * cosi' il tasto laterale fa tutte e due le cose e la navigazione a tre
     * tasti resta quella che l'utente conosce. */
    if (pwr_pending && !pwr_down && now_ms() - pwr_pend_t0 >= POWER_DBL_MS) {
        int guard = now_ms() < t_wake_ignore;
        pwr_pending = 0; pwr_pend_t0 = 0;
        /* Dentro la guardia il click si CONSUMA e non fa nulla: se restasse in
         * attesa, riarmerebbe lo standby appena la guardia scade (schermo nero
         * poco dopo il risveglio, senza che l'utente tocchi piu' niente). */
        if (guard) L("POWER corto: consumato dalla guardia del risveglio");
        else if (standby) standby_exit(); else standby_enter();
    }
}

/* Il verso del bilanciere non si puo' verificare da qui (il pannello non e'
 * raggiungibile): se sul device risultasse invertito, /tmp/ui-swap-vol lo
 * scambia SENZA ricompilare, cosi' si corregge da SSH in un riavvio. */
static int vol_swap(void) { return access("/tmp/ui-swap-vol", F_OK) == 0; }

static void key_event(const struct input_event *ev) {
    if (ev->type != EV_KEY) return;
    if (ev->code == _KEY_POWER) {
        if (ev->value == 1) {
            /* Secondo click dentro la finestra = ATTIVAZIONE (era la conferma del
             * vecchio schema). Sta sulla PRESSIONE, non sul rilascio, perche' il
             * rilascio del secondo click armerebbe di nuovo lo standby. */
            if (pwr_pending) {
                pwr_pending = 0; pwr_fired = 1;
                L("POWER doppio: attiva");
                focus_activate();
            } else {
                pwr_fired = 0;
            }
            /* Una pressione nata dentro la guardia del risveglio appartiene al
             * click che ha svegliato lo schermo (secondo esemplare): il suo
             * rilascio NON deve armare lo standby. Il flag si tiene per tutta la
             * pressione: se la guardia scade mentre il dito e' ancora giu', il
             * rilascio tardivo non deve contare come un click nuovo. La pressione
             * LUNGA invece resta valida anche dentro la guardia (un dito tenuto
             * giu' 1.5 s vuole "indietro" - vedi poll_key_timeout). */
            pwr_guard = now_ms() < t_wake_ignore;
            pwr_down = 1; pwr_t0 = now_ms();
        } else if (ev->value == 0) {
            if (pwr_down && !pwr_fired) {
                if (standby) {
                    /* Pannello spento: nessuna attesa, il click sveglia subito -
                     * e non puo' attivare nulla che non si veda. standby_exit()
                     * azzera lo stato del tasto: il click e' CONSUMATO. */
                    L("POWER corto: risveglio");
                    standby_exit();
                } else if (pwr_guard || now_ms() < t_wake_ignore) {
                    /* Guardia del risveglio: e' il secondo esemplare del click
                     * appena consumato (o la sua coda). Non si arma niente. */
                    L("POWER corto: ignorato (guardia del risveglio)");
                } else {
                    pwr_pending = 1; pwr_pend_t0 = now_ms();   /* standby fra 350 ms se non arriva il secondo */
                }
            }
            pwr_down = 0; pwr_fired = 0; pwr_guard = 0;
        }
        return;
    }
    /* Volume: pressione E ripetizione (il kernel la manda se il tasto manda
     * autorepeat). Senza ripetizione una pagina da 77 voci si attraversa a 77
     * pressioni. Si limita a una mossa ogni 120 ms: anche una ripetizione
     * veloce resta controllabile invece di correre via. */
    if (ev->value != 1 && ev->value != 2) return;
    int tn2 = now_ms();
    if (ev->value == 2 && tn2 - t_vol_last < 120) return;
    t_vol_last = tn2;
    int dir = 0;
    if (ev->code == _KEY_VOLUMEUP)        dir = vol_swap() ? +1 : -1;
    else if (ev->code == _KEY_VOLUMEDOWN) dir = vol_swap() ? -1 : +1;
    if (!dir) { L("tasto: codice %d sconosciuto (ignorato)", ev->code); return; }
    focus_move(dir);
}

static void poll_key(void) {
    struct input_event ev;
    key_scan();
    int fds[2] = { keyfd, keyfd_v };
    for (int k = 0; k < 2; k++) {
        if (fds[k] < 0) continue;
        while (read(fds[k], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) key_event(&ev);
    }
    poll_key_timeout();
}

int main(int argc, char **argv) {
    const char *datapath = "/tmp/ui-data.txt";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dati") && i + 1 < argc) datapath = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) g_quiet = 1;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    L("=== nx679j-ui avvio (pid %d, dati %s) ===", getpid(), datapath);
    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) { L("open errno=%d", errno); return 1; }
    if (ioctl(fd, SET_MASTER) && errno != 16) L("setmaster errno=%d", errno);
    { struct set_client_cap c = { 3, 1 }; if (ioctl(fd, SET_CLIENT_CAP, &c)) L("cap FAIL errno=%d", errno); else L("cap ok"); }

    struct modeinfo modes[40];
    for (u32 i = 0; i < 256 && (int)conn < 0; i++) {
        struct get_conn c; memset(&c, 0, sizeof(c)); c.connector_id = i;
        if (ioctl(fd, GETCONNECTOR, &c)) continue;
        if (c.connector_type != 16 || c.connection != 1) continue;
        struct get_conn c2; memset(&c2, 0, sizeof(c2)); c2.connector_id = i;
        c2.modes_ptr = (u64)(uintptr_t)modes; c2.count_modes = 40;
        if (ioctl(fd, GETCONNECTOR, &c2)) continue;
        conn = i; best = modes[0];
        for (u32 m = 0; m < c2.count_modes && m < 40; m++) if (modes[m].type & 8) best = modes[m];
    }
    if ((int)conn < 0) { L("nessun connettore DSI collegato"); return 2; }
    u32 crtcs[16]; int ncr = 0;
    for (u32 ci = 0; ci < 256 && ncr < 16; ci++) { struct crtc cc; memset(&cc, 0, sizeof(cc)); cc.crtc_id = ci; if (!ioctl(fd, GETCRTC, &cc)) crtcs[ncr++] = ci; }
    crtc = crtcs[0];
    for (int k = 0; k < ncr; k++) { struct crtc cc; memset(&cc, 0, sizeof(cc)); cc.crtc_id = crtcs[k]; if (!ioctl(fd, GETCRTC, &cc) && cc.mode_valid) { crtc = crtcs[k]; break; } }
    L("conn=%d crtc=%u plane=%u", (int)conn, crtc, plane);

    modeid_id = find_prop_global("MODE_ID"); active_id = find_prop_global("ACTIVE");
    conn_crtcid = find_prop_global("CRTC_ID"); ar_id = find_prop_global("autorefresh");
    p_crtcid = find_prop_global("CRTC_ID"); p_fbid = find_prop_global("FB_ID");
    p_srcw = find_prop_global("SRC_W"); p_srch = find_prop_global("SRC_H");
    p_srcx = find_prop_global("SRC_X"); p_srcy = find_prop_global("SRC_Y");
    p_crtcw = find_prop_global("CRTC_W"); p_crtch = find_prop_global("CRTC_H");
    p_crtcx = find_prop_global("CRTC_X"); p_crtcy = find_prop_global("CRTC_Y");
    out_fence_id = find_prop_global("OUT_FENCE_PTR");
    /* Diagnostica + preparazione: il collasso in idle del link DSI e' la ragione
     * per cui questo client deve committare di continuo. Il driver espone
     * idle_pc_state (ENUM: 0=none, 1=enable, 2=disable) sul CRTC: si usa SOLO se
     * la verifica di appartenenza passa, altrimenti non si tocca nulla. */
    { idle_pc_id = find_prop_global("idle_pc_state");
      int cur = idle_pc_id ? prop_on_crtc(idle_pc_id) : -1;
      L("idle_pc_state: id=%u sul CRTC=%s valore=%d", idle_pc_id, cur >= 0 ? "SI" : "NO", cur);
      if (cur < 0) idle_pc_id = 0;
      L("autorefresh id=%u, frame_trigger_mode id=%u", ar_id, ftm_id); }
    /* Il flag va letto QUI, prima di qualunque commit: il primo commit e' un
     * modeset e la proprieta' del driver e' appiccicosa. Leggendolo piu' tardi
     * (com'era) il primo commit scriveva idle_pc_state=0 e disattivava la
     * protezione nel momento piu' delicato: pannello morto a 300 fence in
     * ritardo durante uno scambio a caldo. */
    g_idle_pc_off = (access("/tmp/ui-idle-pc-disable", F_OK) == 0);
    L("idle_pc_disable: %d (letto PRIMA del primo commit)", g_idle_pc_off);
    L("ids (modeid=%u active=%u outfence=%u)", modeid_id, active_id, out_fence_id);

    struct create_blob cb; memset(&cb, 0, sizeof(cb));
    cb.data = (u64)(uintptr_t)&best; cb.length = sizeof(best);
    if (ioctl(fd, CREATE_BLOB, &cb)) { L("blob errno=%d", errno); return 5; }
    blob_id = cb.blob_id;

    u32 pitches[4];
    u32 fb0 = make_fb(C_BG, &pitches[0]);
    u32 fb1 = make_fb(C_BG, &pitches[1]);
    if (!fb0 || !fb1) return 4;

    L("--- TEST_ONLY cnp: rc=%d (0 = validato)", commit_cnp(fb0, 1, 2));
    int r = commit_cnp(fb0, 0, 2);
    if (r) { L("commit cnp FALLITO errno=%d", -r); return 6; }
    L("commit cnp OK");

    u32 *buf[2] = { kmaps[0], kmaps[1] };
    u32 fbs[2] = { fb0, fb1 };
    g_fb[0] = fb0; g_fb[1] = fb1;
    u32 pitch[2] = { pitches[0], pitches[1] };
    g_pitch[0] = pitch[0]; g_pitch[1] = pitch[1];   /* servono a scrivere il nero in standby */
    for (int b = 0; b < 2; b++) {
        size_t visible = (size_t)W * sizeof(u32);
        if (pitch[b] < visible || pitch[b] % sizeof(u32)) { L("pitch non valido fb=%u", fbs[b]); return 8; }
    }
    touch_scan();
    L("touch: %d device attivi all'avvio", n_tfd);

    kv_load(datapath);
    /* Come si guida la UI senza tocco, detto UNA volta all'avvio: il touch di
     * questo telefono e' morto, e senza questa riga la strada (tre tasti) non si
     * scopre guardando lo schermo. */
    toast_set("VOL su/giu = seleziona  POWER = schermo on/off  POWER x2 = attiva  POWER lungo = indietro");
    int dirty = 1;
    int cur = 0, fr = 0, to = 0, to_tot = 0, toast_was = 0;
    int t0 = now_ms(), tlast = t0, gap_max = 0;
    int t_sb_log = t0;      /* ultima riga "loop vivo" in standby */
    int t_scan_next = t0 + 5000;
    int t_flag_next = t0 + 3000;
    L("loop avviato");
    for (;;) {
        poll_touch();
        poll_key();
        /* Comando di servizio: toccare /tmp/ui-standby commuta lo standby senza
         * il tasto fisico. Serve a provare la sequenza DRM da remoto (l'utente
         * tiene il telefono a distanza, SSH: senza questo l'unica via sarebbe
         * premere il tasto). */
        if (access("/tmp/ui-standby", F_OK) == 0) {
            unlink("/tmp/ui-standby");
            if (standby) standby_exit(); else standby_enter();
        }
        if (now_ms() < t_wake_ignore) { /* anti-tocco-fantasma dopo il risveglio */
            pending_down = pending_up = t_ev_down = t_ev_up = 0; t_down = 0;
        }
        if (dirty_req) { dirty = 1; dirty_req = 0; }   /* azione del menu: ridisegna subito */
        if (now_ms() > t_scan_next) { touch_scan(); t_scan_next = now_ms() + 5000; }
        if (now_ms() > t_flag_next) {
            /* Flag file, non argomento: cosi' si prova on/off senza riflashare. */
            int want = (access("/tmp/ui-idle-pc-disable", F_OK) == 0);
            if (want != g_idle_pc_off) {
                g_idle_pc_off = want;
                dirty = 1;
                L("idle_pc_disable -> %d (%s)", want,
                  want ? "collasso in idle disattivato: il pannello non dipende piu' dai commit" :
                         "default: il pannello richiede commit continui");
            }
            t_flag_next = now_ms() + 5000;
        }
        if (t_ev_down) {
            t_ev_down = 0;
            pressed = hit_test(t_down_x, t_down_y);
            if (pressed) dirty = 1;
        }
        if (t_ev_up) {
            t_ev_up = 0;
            int id = hit_test(t_x, t_y);
            if (id && id == pressed) do_action(id);
            /* Niente ramo "tab" qui: con il menu a categorie ogni id passa da
             * do_action(), che smista (>=30) a do_menu_action(). La vecchia
             * scorciatoia id-A_TAB0 intercettava gli id del menu e li buttava
             * in `page`, per cui nessun tasto del menu rispondeva al tocco. */
            if (pressed) { pressed = 0; dirty = 1; }
        }
        if (kv_load(datapath)) dirty = 1;   /* dati nuovi -> ridisegno: senza questo lo schermo resta fermo */
        if (dirty) {   /* in standby si ridisegna NERO, ma SI committa (link vivo) */
            if (standby) {
                /* Il nero E' il "pannello spento". La pagina non si tocca: nessun
                 * draw_page, cosi' al risveglio torna da sola (dirty_req). */
                sb_fill_black();
            } else {
            /* La lista degli elementi selezionabili si raccoglie mentre si
             * disegna il PRIMO dei due buffer: cosi' coincide con cio' che si
             * vede (nessuna geometria duplicata) e non viene contata due volte. */
            g_focus_on = 1; focus_reset();
            draw_page(buf[0], pitch[0]);
            g_focus_on = 0;
            draw_page(buf[1], pitch[1]);
            }
            dirty = 0;
            /* Occhio di ricambio: se esiste /tmp/ui-dump, la UI scrive su file
             * il frame appena disegnato (PPM). Serve a GUARDARE cio' che la UI
             * disegna sul device, senza webcam. Si cancella il flag: si fa una
             * volta sola per richiesta. A schermo nero non si esporta niente:
             * sarebbe un PPM tutto nero e non direbbe nulla sulla pagina. */
            if (!standby && access("/tmp/ui-dump", F_OK) == 0) {
                FILE *f = fopen("/tmp/ui-dump.ppm", "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", W, H);
                    unsigned char *b = (unsigned char *)buf[0];
                    for (int y = 0; y < (int)H; y++) {
                        u32 *row = (u32 *)(b + (size_t)y * pitch[0]);
                        for (int x = 0; x < (int)W; x++) { u32 v = row[x]; fputc(v >> 16 & 255, f); fputc(v >> 8 & 255, f); fputc(v & 255, f); }
                    }
                    fclose(f);
                }
                unlink("/tmp/ui-dump");
            }
        }
        if (toast_was && now_ms() > toast_until) { toast_was = 0; reboot_armed = 0; dirty = 1; }
        if (toast[0] && now_ms() <= toast_until) toast_was = 1;

        /* UNICO punto del loop che tocca il DRM. In standby il commit NON si
         * salta: si commettono frame NERI, e sono loro a tenere vivo il link DSI.
         * (Il 25/09 il fermo dei commit per lo spegnimento e' stato la strada che
         * ha portato al link morto: qui non si ferma piu' niente.) */
        int nxt = 1 - cur;
        int ffd = -1;
        int pr = flip_step(fbs[nxt], &ffd);
        if (pr > 0) {
            /* Ramo non piu' usato dallo standby (v176): se compare, il commit e'
             * stato saltato da qualcos'altro. Si dice e si va avanti. */
            L("flip %d saltato", fr);
        } else if (pr) {
            L("flip %d FALLITO errno=%d -> STOP", fr, -pr); break;
        } else {
            int w = wait_fence(ffd, 40);
            if (w == 1) {
                /* Fence in ritardo: NON e' un errore del driver (l'ioctl e' riuscito) e
                 * fermarsi qui significherebbe lasciare il pannello senza commit, cioe'
                 * ucciderlo. Si continua e si conta. Il timeout e' 40 ms e non 100:
                 * il link collassa dopo 58 ms di inattivita', quindi aspettarne 100
                 * significherebbe superare da soli la soglia. */
                to++; to_tot++;
                if (to <= 3 || (to % 50) == 0) L("flip %d: fence in ritardo (%d consecutivi, tot %d)", fr, to, to_tot);
            } else if (w < 0) {
                L("flip %d: poll errno=%d", fr, errno);
            } else {
                to = 0;
            }
            cur = nxt; fr++;
            g_cur = nxt;   /* il fb a schermo: lo standby ci scrive il nero direttamente */
            { int tn = now_ms(); int g = tn - tlast; if (g > gap_max) gap_max = g; tlast = tn; }
            if ((fr % 600) == 0) {
                int dt = now_ms() - t0;
                L("TELEMETRIA frame=%d fps=%.1f gap_max_ms=%d fence_timeout=%d chiavi=%d pagina=%s/%s",
                  fr, dt > 0 ? (fr * 1000.0 / dt) : 0.0, gap_max, to_tot, kv_n,
                  cat_names[cat], cat_pages[cat][page_sel[cat]]);
            }
        }
        /* BATTITO in standby: una riga ogni 10 s dice dall'esterno che il loop e'
         * vivo, che le chiavi sono aperte e che si sta committendo nero. */
        if (standby && now_ms() - t_sb_log > 10000) {
            t_sb_log = now_ms();
            L("standby: loop vivo (nero) (frame %d, tasti %d/%d aperti)", fr, keyfd, keyfd_v);
        }
        usleep(16000);
    }
    L("=== ui STOPPED dopo %d frame (processo vivo, display fermo) ===", fr);
    for (;;) sleep(60);
    return 0;
}
