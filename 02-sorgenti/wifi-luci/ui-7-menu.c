
/* ---------- menu: categorie, pagine, liste scorrevoli con azioni per riga ----------
 *
 * La struttura e' a due livelli: 4 categorie in basso (bersagli grandi per il
 * dito), una riga di pagine sotto la barra del titolo, il contenuto sotto.
 * Le liste (servizi, processi, log, rotte...) sono generate DAI DATI: ogni riga
 * e' una chiave `prefisso.N` e i tasti a destra eseguono un'azione su quella
 * riga (es. start/stop del servizio il cui nome e' il valore della chiave).
 */
enum { CAT_STATO = 0, CAT_RETE, CAT_MODEM, CAT_SISTEMA, CAT_N };
static const char *cat_names[CAT_N] = { "STATO", "RETE", "MODEM", "SISTEMA" };
#define MAXPAGES 8
static int cat = CAT_MODEM;
static int page_sel[CAT_N] = { 0, 0, 0, 0 };
static int scroll_sel[CAT_N][MAXPAGES];

/* Le pagine di ogni categoria: nomi (per la barra) e indice del tipo. */
static const char *pages_stato[]  = { "Panor", "Log sist", "Log kern", "Processi", "Rotte", "Lease", "Mount" };
static const char *pages_rete[]   = { "Iface", "Wi-Fi", "Firewall", "Diag", "Lease", "Hostname" };
static const char *pages_modem[]  = { "Stato", "SIM", "Sessioni" };
static const char *pages_sistema[] = { "Servizi", "Setup", "Backup", "Riavvio", "Modifica", "Crontab", "Fuso" };
static const char **cat_pages[CAT_N] = { pages_stato, pages_rete, pages_modem, pages_sistema };
static const int cat_npages[CAT_N] = { 7, 6, 3, 7 };

/* ---------- pagine: ognuna disegna il contenuto della categoria corrente ---------- */

/* Definizioni anticipate: le pagine qui sotto usano costanti e funzioni che
 * nel file compaiono piu' avanti. Le guardie evitano la doppia definizione. */
#ifndef A_SCROLL_UP
#define A_SCROLL_UP 220
#define A_SCROLL_DN 221
#define A_PAGE0     200
#define A_CAT0      400
#define A_ROW0      500
#define A_RENEW_WAN 30
#define A_BACKUP    31
#define A_PING_GW   32
#define A_PING1     33
#define A_PING2     34
#define A_FW_RELOAD 35
#endif
/* Quante righe di lista entrano nel contenuto (stesse che usa draw_list). */
#ifndef ROW_H
#define ROW_H 92
#define LIST_Y 300
#define LIST_ROWS 18
#endif
static int  dirty_req;
static void draw_btn_at(u32 *, u32, int, int, int, int, const char *, int, int);

/* Pulsanti di pagina: barra standard sopra quella di scorrimento, con la STESSA
 * geometria usata dal disegno e dall'hit-test. Prima erano posizionati a mano
 * dentro le pagine e l'hit-test non li conosceva: non rispondevano al tocco. */
#define PB_Y 1870
#define PB_H 120
static void page_btn(u32 *m, u32 pitch, int i, int n, const char *label, int id) {
    int w = (1000 - 20 * (n - 1)) / n;
    draw_btn_at(m, pitch, 40 + i * (w + 20), PB_Y, w, PB_H, label, id, 3);
}
static int page_btn_hit(int x, int y, int n) {
    if (y < BTN_Y || y >= BTN_Y + BTN_H) return -1;   /* stessa zona di draw_btn */
    int w = (1000 - 20 * (n - 1)) / n;
    int i = (x - 40) / (w + 20);
    return (x >= 40 && i >= 0 && i < n) ? i : -1;
}

static void draw_btn_at(u32 *, u32, int, int, int, int, const char *, int, int);
static int  list_rows(const char *, char [][160], int);
static void draw_list(u32 *, u32, const char *, const char *, const char *const *);

static void page_overview(u32 *m, u32 pitch) {
    int y = 300;
    row(m, pitch, y, "Versione", kv("set.upgrade"), C_TEXT); y += 96;
    row(m, pitch, y, "Ora", kv("set.time"), C_TEXT); y += 96;
    row(m, pitch, y, "Uptime", kv("sys.uptime"), C_TEXT); y += 96;
    row(m, pitch, y, "Carico 1/5/15", kv("sys.load"), C_TEXT); y += 96;
    row(m, pitch, y, "Memoria", kv("sys.mem"), C_TEXT); y += 96;
    row(m, pitch, y, "Temperatura", kv("sys.temp"), C_TEXT); y += 96;
    row(m, pitch, y, "WAN", kv("net.if.wan_early"), C_TEXT); y += 96;
    row(m, pitch, y, "LAN/Wi-Fi", kv("net.if.lan_wifi"), C_TEXT); y += 96;
    row(m, pitch, y, "Client Wi-Fi", kv("wifi.clients"), C_TEXT); y += 96;
    row(m, pitch, y, "DHCP", kv("set.dhcp_from"), C_TEXT); y += 96;
    row(m, pitch, y, "Modem", kv("modem.health"), C_TEXT);
}

static void page_ifaces(u32 *m, u32 pitch) {
    static const char *const b[] = { "SU", "GIU", NULL };
    draw_list(m, pitch, "Interfacce", "net.if", b);
    page_btn(m, pitch, 0, 1, "RINNOVA WAN", A_RENEW_WAN);
}

static void page_services(u32 *m, u32 pitch) {
    static const char *const b[] = { "AVV", "FER", "ON", "OFF", NULL };
    draw_list(m, pitch, "Servizi", "svc", b);
    draw_text(m, pitch, 40, 2140, "AVV=avvia  FER=ferma  ON=abilita al boot  OFF=disabilita", C_DIM, 2);
}

static void page_settings(u32 *m, u32 pitch) {
    int y = 300;
    row(m, pitch, y, "Hostname", kv("set.hostname"), C_TEXT); y += 96;
    row(m, pitch, y, "Fuso", kv("set.tzname"), C_TEXT); y += 96;
    row(m, pitch, y, "NTP", kv("set.ntp"), C_TEXT); y += 96;
    row(m, pitch, y, "LAN", kv("set.lan_ip"), C_TEXT); y += 96;
    row(m, pitch, y, "DHCP da", kv("set.dhcp_from"), C_TEXT); y += 96;
    row(m, pitch, y, "DHCP n.", kv("set.dhcp_limit"), C_TEXT); y += 96;
    row(m, pitch, y, "Lease", kv("set.dhcp_leasetime"), C_TEXT); y += 96;
    row(m, pitch, y, "Dominio", kv("set.domain"), C_TEXT); y += 96;
    row(m, pitch, y, "Host statici", kv("set.hosts"), C_TEXT); y += 96;
    row(m, pitch, y, "Root pw", kv("set.rootpw"), C_TEXT); y += 96;
    row(m, pitch, y, "Crontab", kv("set.crontab"), C_TEXT); y += 96;
    row(m, pitch, y, "Regole fw", kv("set.fw"), C_TEXT);
    draw_text(m, pitch, 40, 2100, "La modifica di questi valori richiede la tastiera (in arrivo)", C_DIM, 2);
}

static void page_backup(u32 *m, u32 pitch) {
    int y = 300;
    row(m, pitch, y, "Ultimo backup", kv("bkp.name"), C_TEXT); y += 96;
    row(m, pitch, y, "Dimensione", kv("bkp.size"), C_TEXT); y += 96;
    draw_text(m, pitch, 40, 560, "Crea un archivio della configurazione in /tmp.", C_DIM, 2);
    draw_text(m, pitch, 40, 610, "Per scaricarlo serve la GUI web (System > Backup).", C_DIM, 2);
    page_btn(m, pitch, 0, 1, "CREA BACKUP", A_BACKUP);
}

static void page_diag(u32 *m, u32 pitch) {
    draw_text(m, pitch, 40, 300, "Ping (3 pacchetti) verso:", C_DIM, 3);
    draw_btn_at(m, pitch, 40, 380, 320, 120, "GATEWAY", A_PING_GW, 3);
    draw_btn_at(m, pitch, 380, 380, 320, 120, "1.1.1.1", A_PING1, 3);
    draw_btn_at(m, pitch, 720, 380, 320, 120, "8.8.8.8", A_PING2, 3);
    static char rows[16][160];
    int n = list_rows("diag", rows, 16);
    int y = 560;
    for (int i = 0; i < n && i < 12; i++) { draw_text_fit(m, pitch, 40, y, 1000, rows[i], C_TEXT, 2); y += 60; }
    if (!n) draw_text(m, pitch, 40, 560, "(nessun test eseguito)", C_DIM, 3);
}

 /* ---------- Azioni ----------
 * 1..99    : azioni fisse (riconnessione, reboot, wifi, ...)
 * 100..119 : tab di categoria
 * 200..209 : pagine della categoria corrente
 * 220..229 : scorrimento liste
 * 300..    : tasti di riga — id = 300 + riga*4 + quale  (riga = indice assoluto
 *            nella lista, cioe' 0-based sul file dati, non sulla schermata)
 */
 #define A_SCROLL_UP 220
 #define A_SCROLL_DN 221
 #define A_PAGE0     200
 /* Categorie e tasti di riga stanno ALTI: 100 e' A_TAB0 del vecchio menu a tre
  * tab, ancora gestito da do_action() in ui-5. */
 #define A_CAT0      400
 #define A_ROW0      500
 #define A_RENEW_WAN 30
 #define A_BACKUP    31
 #define A_PING_GW   32
 #define A_PING1     33
 #define A_PING2     34
 #define A_FW_RELOAD 35

/* Dichiarazioni anticipate: le pagine e le barre si richiamano a vicenda. */
static void draw_list(u32 *, u32, const char *, const char *, const char *const *);
static void page_overview(u32 *, u32);
static void page_ifaces(u32 *, u32);
static void page_fw(u32 *, u32);
static void page_diag(u32 *, u32);
static void page_services(u32 *, u32);
static void page_settings(u32 *, u32);
static void page_backup(u32 *, u32);
static void page_edit(u32 *, u32);
static void page_cron(u32 *, u32);
static void page_sh(u32 *, u32);
static void page_dn(u32 *, u32);
static void page_tz(u32 *, u32);
static void draw_reboot_page(u32 *, u32);

/* Firewall: stato del servizio e comando di ricarica. La stessa superficie
 * della GUI web (Network > Firewall), senza le regole: quelle restano a UCI. */
/* Campi modificabili con la tastiera a schermo. Ogni riga ha un tasto MOD che
 * apre la tastiera sul valore corrente. */
static void page_edit(u32 *m, u32 pitch) {
    static const char *const b[] = { "MOD", NULL };
    draw_list(m, pitch, "Modifica", "ed", b);
    draw_text(m, pitch, 40, 2120, "SSID/chiave: applicare fa ripartire il Wi-Fi.", C_WARN, 2);
    draw_text(m, pitch, 40, 2160, "La password si scrive e si applica (non si legge).", C_DIM, 2);
}

/* Fuso orario: elenco curato, un tasto USA per riga. Il valore della riga porta
 * gia' zona E stringa POSIX: applicare e' una sola azione, nessuna tastiera. */
static void page_tz(u32 *m, u32 pitch) {
    static const char *const b[] = { "USA", NULL };
    draw_list(m, pitch, "Fuso orario", "tz", b);
    draw_text(m, pitch, 40, 2130, "Elenco curato: le zone rare restano in LuCI.", C_DIM, 2);
}

/* Hostname statici: righe + DEL, piu' AGGIUNGI ("nome ip"). */
static void page_dn(u32 *m, u32 pitch) {
    static const char *const b[] = { "DEL", NULL };
    draw_list(m, pitch, "Hostname statici", "dn", b);
    draw_btn_at(m, pitch, 40, 1870, 480, 120, "AGGIUNGI", 38, 3);
    draw_text(m, pitch, 40, 2130, "AGGIUNGI: scrivi  nome ip   (dnsmasq risponde a quel nome).", C_DIM, 2);
}

/* Lease statici: righe con DEL, piu' AGGIUNGI ("nome ip [mac]"). */
static void page_sh(u32 *m, u32 pitch) {
    static const char *const b[] = { "DEL", NULL };
    draw_list(m, pitch, "Lease statici", "sh", b);
    draw_btn_at(m, pitch, 40, 1870, 480, 120, "AGGIUNGI", 37, 3);
    draw_text(m, pitch, 40, 2130, "AGGIUNGI: scrivi  nome ip [mac]  (mac facoltativo).", C_DIM, 2);
}

/* Attivita' pianificate: righe del crontab, con MOD (modifica la riga) e DEL.
 * Le modifiche si applicano SUBITO al file (niente bozza da salvare): ogni
 * tasto scrive /etc/crontabs/root e riavvia cron. */
static void page_cron(u32 *m, u32 pitch) {
    static const char *const b[] = { "MOD", "DEL", NULL };
    draw_list(m, pitch, "Attivita' pianificate", "cr", b);
    draw_btn_at(m, pitch, 40, 1870, 480, 120, "AGGIUNGI", 36, 3);
    draw_text(m, pitch, 40, 2130, "MOD modifica la riga, DEL la cancella, AGGIUNGI ne crea una.", C_DIM, 2);
}

static void page_fw(u32 *m, u32 pitch) {
    int y = 300;
    row(m, pitch, y, "Servizio attivo", kv("set.fw"), C_TEXT); y += 96;
    row(m, pitch, y, "Zone", kv("fw.zones"), C_TEXT); y += 96;
    row(m, pitch, y, "Regole", kv("fw.rules"), C_TEXT); y += 96;
    row(m, pitch, y, "Redirect", kv("fw.redirect"), C_TEXT); y += 96;
    row(m, pitch, y, "Inoltro (WAN)", kv("fw.forward"), C_TEXT); y += 96;
    draw_text(m, pitch, 40, 900, "Regole e zone si modificano da LuCI; qui lo stato e la ricarica.", C_DIM, 2);
    page_btn(m, pitch, 0, 1, "RICARICA", A_FW_RELOAD);
}

/* Riavvio: due tocchi per confermare (un tocco solo non deve riavviare). */
static void draw_reboot_page(u32 *m, u32 pitch) {
    int y = 300;
    row(m, pitch, y, "Uptime", kv("sys.uptime"), C_TEXT); y += 96;
    row(m, pitch, y, "WAN", kv("net.if.wan_early"), C_TEXT); y += 96;
    row(m, pitch, y, "Modem", kv("modem.health"), C_TEXT);
    draw_text(m, pitch, 40, 800, reboot_armed ? "Tocca di nuovo per confermare." : "Il riavvio chiude la sessione dati per ~2 minuti.",
              reboot_armed ? C_ERR : C_DIM, 3);
    draw_btn_at(m, pitch, 40, 1900, 1000, 150, reboot_armed ? "CONFERMA RIAVVIO" : "RIAVVIA", A_REBOOT, 4);
}

/* Tasto con evidenza di selezione (per tab di categoria e riga pagine). */
static void draw_tab_at(u32 *m, u32 pitch, int x, int y, int w, int h,
                        const char *label, int id, int sel) {
    /* id serve al riscontro visivo della pressione; sel all'evidenza di selezione */
    u32 bg = (pressed == id) ? 0x00ffffffu : (sel ? C_ACCENT : C_PANEL2);
    u32 fg = (sel || pressed == id) ? C_PANEL2 : C_TEXT;
    box(m, pitch, x, y, w, h, bg, 2, sel ? C_TEXT : C_DIM);
    int sc = 3, tw = text_w(label, sc);
    if (tw > w - 12) { sc = 2; tw = text_w(label, sc); }
    draw_text(m, pitch, x + (w - tw) / 2, y + (h - FONT_H * sc) / 2, label, fg, sc);
    focus_add(id, FK_BTN, x, y, w, h, label);
}

/* Barra superiore: nome del sistema, ora, categoria e pagina. */
static void draw_topbar(u32 *m, u32 pitch) {
    draw_text(m, pitch, 40, 34, "OpenWrt", C_TEXT, 4);
    char t[64];
    snprintf(t, sizeof(t), "%s %s", kv("set.date"), kv("set.time"));
    draw_text_right(m, pitch, 1040, 44, t, C_DIM, 3);
    /* Categoria e nome pagina NON si scrivono piu' quassu' (decisione utente).
     * Erano testo blu a scala 3 (48 px: y 108..156) che finiva a ridosso della
     * barra dei tab (y 170), e i residui delle etichette vecchie restavano sul
     * vetro sopra i tasti. L'informazione e' gia' data dal tasto evidenziato
     * nella barra dei tab: meno testo, nessuna collisione possibile. */
}

/* Riga dei tasti pagina della categoria corrente. */
static void draw_pagebar(u32 *m, u32 pitch) {
    /* UNA riga sola: con due righe la seconda (fino a y=334) copriva la prima
     * riga di contenuto (che parte a y=280). Con 7 pagine: 136 px per tab. */
    int n = cat_npages[cat];
    int w = (1000 - 8 * (n - 1)) / n;
    if (w > 240) w = 240;
    for (int i = 0; i < n; i++)
        draw_tab_at(m, pitch, 40 + i * (w + 8), 170, w, 88, cat_pages[cat][i], A_PAGE0 + i, i == page_sel[cat]);
}

/* Barra inferiore: le 4 categorie, bersagli grandi per il dito. */
static void draw_catbar(u32 *m, u32 pitch) {
    int w = 1000 / CAT_N;
    for (int i = 0; i < CAT_N; i++)
        draw_tab_at(m, pitch, 40 + i * w, 2210, w - 8, 150, cat_names[i], A_CAT0 + i, i == cat);
}

/* Quale pagina disegnare per la categoria corrente. */
static void menu_draw_page(u32 *m, u32 pitch) {
    int p = page_sel[cat];
    switch (cat) {
    case CAT_STATO:
        switch (p) {
        case 0: page_overview(m, pitch); break;
        case 1: draw_list(m, pitch, "Log sistema", "log", NULL); break;
        case 2: draw_list(m, pitch, "Log kernel", "klog", NULL); break;
        case 3: draw_list(m, pitch, "Processi", "proc", NULL); break;
        case 4: draw_list(m, pitch, "Rotte", "route", NULL); break;
        case 5: draw_list(m, pitch, "Lease DHCP", "lease", NULL); break;
        default: draw_list(m, pitch, "Mount", "mnt", NULL); break;
        }
        break;
    case CAT_RETE:
        switch (p) {
        case 0: page_ifaces(m, pitch); break;
        case 1: draw_rete(m, pitch); break;
        case 4: page_sh(m, pitch); break;
        case 5: page_dn(m, pitch); break;
        case 6: page_tz(m, pitch); break;
        case 2: page_fw(m, pitch); break;
        default: page_diag(m, pitch); break;
        }
        break;
    case CAT_MODEM:
        draw_modem(m, pitch);
        break;
    default:
        switch (p) {
        case 0: page_services(m, pitch); break;
        case 1: page_settings(m, pitch); break;
        case 2: page_backup(m, pitch); break;
        case 3: draw_reboot_page(m, pitch); break;
        case 4: page_edit(m, pitch); break;
        case 5: page_cron(m, pitch); break;
        default: draw_reboot_page(m, pitch); break;
        }
        break;
    }
}

/* ---------- tasti per riga: quali pagine ne hanno ---------- */

static int page_nbtn(void) {
    if (cat == CAT_RETE && page_sel[cat] == 0) return 2;
    if (cat == CAT_RETE && page_sel[cat] == 4) return 1;
    if (cat == CAT_RETE && page_sel[cat] == 5) return 1;
    if (cat == CAT_SISTEMA && page_sel[cat] == 0) return 4;
    if (cat == CAT_SISTEMA && page_sel[cat] == 4) return 1;
    if (cat == CAT_SISTEMA && page_sel[cat] == 5) return 2;
    if (cat == CAT_SISTEMA && page_sel[cat] == 6) return 1;
    return 0;
}

/* Quante righe entrano e da quale si parte (stessa formula di draw_list). */
static int list_geom(int *maxrows, int *start) {
    *maxrows = (PB_Y - 20 - LIST_Y) / ROW_H;
    if (*maxrows > LIST_ROWS) *maxrows = LIST_ROWS;
    int sc = scroll_sel[cat][page_sel[cat]];
    int n = 0;
    *start = sc * (*maxrows);
    return n;
}

/* ---------- hit-test del menu ---------- */
static int hit_test_menu(int x, int y) {
    if (kbd_open) return kbd_hit(x, y);   /* la tastiera e' modale: prende tutto */
    /* barra categorie (in basso) */
    if (y >= 2210 && y < 2360) {
        int w = 1000 / CAT_N;
        if (x >= 40) {
            int i = (x - 40) / w;
            if (i >= 0 && i < CAT_N) return A_CAT0 + i;
        }
        return 0;
    }
    /* riga dei tasti pagina */
    int n = cat_npages[cat];
    int w = (1000 - 8 * (n - 1)) / n;
    if (w > 240) w = 240;
    if (y >= 170 && y < 258) {                 /* stessa formula del disegno */
        int c = (x - 40) / (w + 8);
        if (x >= 40 && c >= 0 && c < n) return A_PAGE0 + c;
        return 0;
    }
    /* scorrimento */
    /* Pulsanti di pagina: barra standard (vedi page_btn). Vanno riconosciuti
     * QUI, con la stessa geometria del disegno. */
    if (cat == CAT_SISTEMA && page_sel[cat] == 2) { if (page_btn_hit(x, y, 1) == 0) return A_BACKUP; }
    if (cat == CAT_SISTEMA && page_sel[cat] == 5) { if (page_btn_hit(x, y, 2) == 0) return 36; }
    if (cat == CAT_MODEM) {   /* i tasti della pagina modem non erano MAI stati collegati */
        if (page_btn_hit(x, y, 2) == 0) return A_RECON;      /* RICONNETTI */
        if (page_btn_hit(x, y, 2) == 1) return A_SENS_UI;    /* NASCONDI/MOSTRA SIM */
    }
    if (cat == CAT_RETE) {
        int pp = page_sel[cat];
        if (pp == 0) { if (page_btn_hit(x, y, 1) == 0) return A_RENEW_WAN; }
        if (pp == 2) { if (page_btn_hit(x, y, 1) == 0) return A_FW_RELOAD; }
        if (pp == 4) { if (page_btn_hit(x, y, 1) == 0) return 37; }
        if (pp == 5) { if (page_btn_hit(x, y, 1) == 0) return 38; }
        if (pp == 3 && y >= 380 && y < 500) {      /* diagnostica: tasti in area contenuto */
            if (x >= 40 && x < 360) return A_PING_GW;
            if (x >= 380 && x < 700) return A_PING1;
            if (x >= 720 && x < 1040) return A_PING2;
        }
    }
    if (y >= 2030 && y < 2126) {
        if (x >= 700 && x < 860) return A_SCROLL_UP;
        if (x >= 880 && x < 1040) return A_SCROLL_DN;
    }
    /* tasti di riga */
    int nb = page_nbtn();
    if (nb && y >= LIST_Y && y < 2020) {
        int maxrows, start;
        list_geom(&maxrows, &start);
        int i = (y - LIST_Y) / ROW_H;
        if (i >= 0 && i < maxrows) {
            int bw = 112;
            for (int b = 0; b < nb; b++) {
                int bx = 1040 - (nb - b) * (bw + 10) + 4;
                if (x >= bx && x < bx + bw) return A_ROW0 + (start + i) * 4 + b;
            }
        }
    }
    return 0;
}

/* ---------- azioni del menu ---------- */
static const char *page_prefix(void) {
    if (cat == CAT_STATO) {
        switch (page_sel[cat]) {
        case 1: return "log";
        case 2: return "klog";
        case 3: return "proc";
        case 4: return "route";
        case 5: return "lease";
        case 6: return "mnt";
        }
        return NULL;
    }
    if (cat == CAT_RETE && page_sel[cat] == 0) return "net.if";
    if (cat == CAT_RETE && page_sel[cat] == 4) return "sh";
    if (cat == CAT_RETE && page_sel[cat] == 5) return "dn";
    if (cat == CAT_SISTEMA && page_sel[cat] == 0) return "svc";
    if (cat == CAT_SISTEMA && page_sel[cat] == 4) return "ed";
    if (cat == CAT_SISTEMA && page_sel[cat] == 5) return "cr";
    if (cat == CAT_SISTEMA && page_sel[cat] == 6) return "tz";
    return NULL;
}

static void do_menu_action(int id) {
    if (kbd_key_action(id)) return;       /* i tasti della tastiera hanno la precedenza */
    if (id == 36) { kbd_open_for("crnew", "nuova riga crontab", "", 0); return; }
    if (id == 37) { kbd_open_for("shnew", "nome ip [mac]", "", 0); return; }
    if (id == 38) { kbd_open_for("dnnew", "nome ip", "", 0); return; }
    if (id >= A_CAT0 && id < A_CAT0 + CAT_N) { cat = id - A_CAT0; dirty_req = 1; return; }
    if (id >= A_PAGE0 && id < A_PAGE0 + 16) {
        int i = id - A_PAGE0;
        if (i < cat_npages[cat]) page_sel[cat] = i;
        dirty_req = 1;
        return;
    }
    if (id == A_SCROLL_UP || id == A_SCROLL_DN) {
        int *s = &scroll_sel[cat][page_sel[cat]];
        if (id == A_SCROLL_UP) { if (*s > 0) (*s)--; } else (*s)++;
        dirty_req = 1;
        return;
    }
    if (id == A_RENEW_WAN) { run_bg("ubus call network.interface.wan_early renew"); toast_set("Rinnovo WAN richiesto"); dirty_req = 1; return; }
    if (id == A_FW_RELOAD) { run_bg("fw4 reload"); toast_set("Firewall: ricarica"); dirty_req = 1; return; }
    if (id == A_BACKUP)    { run_bg("sysupgrade -b /tmp/backup-ui.tar.gz; ls -l /tmp/backup-ui.tar.gz > /tmp/ui-backup.txt"); toast_set("Backup in corso…"); dirty_req = 1; return; }
    if (id == A_PING_GW)   { run_bg("ping -c3 -W2 $(ip route | awk '/default/{print $3; exit}') > /tmp/ui-ping.txt 2>&1"); toast_set("Ping gateway…"); dirty_req = 1; return; }
    if (id == A_PING1)     { run_bg("ping -c3 -W2 1.1.1.1 > /tmp/ui-ping.txt 2>&1"); toast_set("Ping 1.1.1.1…"); dirty_req = 1; return; }
    if (id == A_PING2)     { run_bg("ping -c3 -W2 8.8.8.8 > /tmp/ui-ping.txt 2>&1"); toast_set("Ping 8.8.8.8…"); dirty_req = 1; return; }

    if (id >= A_ROW0) {
        int row = (id - A_ROW0) / 4, b = (id - A_ROW0) % 4;
        const char *pre = page_prefix();
        if (!pre) return;
        static char rows[64][160];
        int n = list_rows(pre, rows, 64);
        if (row >= n) return;
        /* Il primo campo (fino allo SPAZIO) e' il nome dell'oggetto su cui
         * agire: i valori del gatherer hanno il nome in testa ("wpad ON auto",
         * "lan_wifi up phy0-ap0 192.168.77.1/24"). */
        char name[48];
        const char *p = rows[row];
        int i = 0;
        while (*p && *p != ' ' && i < 47) name[i++] = *p++;
        name[i] = 0;
        char cmd[220];
        if (cat == CAT_RETE && page_sel[cat] == 0) {
            snprintf(cmd, sizeof(cmd), "ubus call network.interface.%s %s", name, b == 0 ? "up" : "down");
            run_bg(cmd);
            toast_set(b == 0 ? "Interfaccia su: %s" : "Interfaccia giu: %s", name);
        } else if (cat == CAT_SISTEMA && page_sel[cat] == 0) {
            if (b == 0 || b == 1)
                snprintf(cmd, sizeof(cmd), "ubus call rc init '{\"name\":\"%s\",\"action\":\"%s\"}'", name, b == 0 ? "start" : "stop");
            else
                snprintf(cmd, sizeof(cmd), "/etc/init.d/%s %s", name, b == 2 ? "enable" : "disable");
            run_bg(cmd);
            toast_set(b == 0 ? "Avvio: %s" : b == 1 ? "Fermo: %s" : b == 2 ? "Abilitato: %s" : "Disabilitato: %s", name);
        } else if (cat == CAT_RETE && page_sel[cat] == 5 && b == 0) {
            snprintf(cmd, sizeof(cmd), "uci -q delete dhcp.%s && uci commit dhcp && /etc/init.d/dnsmasq restart", name);
            run_bg(cmd);
            toast_set("hostname %s cancellato", name);
            dirty_req = 1;
            return;
        } else if (cat == CAT_RETE && page_sel[cat] == 4 && b == 0) {
            /* cancella il lease statico: l'id sezione e' il primo campo */
            snprintf(cmd, sizeof(cmd), "uci -q delete dhcp.%s && uci commit dhcp && /etc/init.d/dnsmasq restart", name);
            run_bg(cmd);
            toast_set("lease %s cancellato", name);
            dirty_req = 1;
            return;
        } else if (cat == CAT_SISTEMA && page_sel[cat] == 6 && b == 0) {
            /* valore = "<zona> <tzstring>": si applicano entrambi insieme */
            const char *sp = strchr(rows[row], ' ');
            if (!sp) return;
            char zone[64]; snprintf(zone, sizeof(zone), "%.63s", name);
            snprintf(cmd, sizeof(cmd),
                "uci set system.@system[0].zonename='%s' && uci set system.@system[0].timezone='%s' && uci commit system && /etc/init.d/sysntpd restart",
                zone, sp + 1);
            run_bg(cmd);
            toast_set("fuso: %s", zone);
            dirty_req = 1;
            return;
        } else if (cat == CAT_SISTEMA && page_sel[cat] == 5) {
            /* riga del crontab: il primo token e' il numero di riga del file */
            int ln = atoi(rows[row]);
            const char *sp = strchr(rows[row], ' ');
            if (b == 0) {                      /* MOD */
                char cur[100] = "";
                if (sp) snprintf(cur, sizeof(cur), "%.*s", 90, sp + 1);
                char key[16]; snprintf(key, sizeof(key), "cr%d", ln);
                kbd_open_for(key, "riga crontab", cur, 0);
            } else {                            /* DEL */
                snprintf(cmd, sizeof(cmd),
                    "awk -v n=%d 'NR!=n' /etc/crontabs/root > /tmp/cr.$$ && mv /tmp/cr.$$ /etc/crontabs/root && /etc/init.d/cron restart", ln);
                run_bg(cmd);
                toast_set("riga %d cancellata", ln);
            }
            dirty_req = 1;
            return;
        } else if (cat == CAT_SISTEMA && page_sel[cat] == 4 && b == 0) {
            /* Apre la tastiera sul campo: il valore corrente e' il resto della riga. */
            char cur[100] = "";
            const char *sp = strchr(rows[row], ' ');
            if (sp) snprintf(cur, sizeof(cur), "%.*s", 90, sp + 1);
            if (!strcmp(name, "rootpw")) cur[0] = 0;   /* non si legge: si imposta */
            kbd_open_for(name, name, cur, !strcmp(name, "key") || !strcmp(name, "rootpw"));
            return;
        }
        dirty_req = 1;
    }
}

static void draw_btn_at(u32 *m, u32 pitch, int x, int y, int w, int h,
                        const char *label, int id, int sc) {
    u32 bg = (pressed == id) ? 0x00ffffffu : C_PANEL2;
    u32 fg = (pressed == id) ? C_PANEL2 : C_TEXT;
    box(m, pitch, x, y, w, h, bg, 2, C_ACCENT);
    int tw = text_w(label, sc);
    if (tw > w - 16) sc = 2, tw = text_w(label, sc);
    draw_text(m, pitch, x + (w - tw) / 2, y + (h - FONT_H * sc) / 2, label, fg, sc);
    focus_add(id, FK_BTN, x, y, w, h, label);
}

/* Quante righe di lista entrano nel contenuto. */
#define ROW_H 92
#define LIST_Y 300
#define LIST_ROWS 18

/* Righe visibili di una lista: le chiavi `prefix.N` in ordine di N. */
static int list_rows(const char *prefix, char out[][160], int max) {
    /* Si accettano ENTRAMBE le forme di chiave:
     *   <prefisso>.<numero>   (log.1, proc.1, route.3 — liste posizionali)
     *   <prefisso>.<nome>     (svc.wpad, net.if.lan_wifi — oggetti con nome)
     * Guardare solo la prima forma lasciava vuote le liste di servizi e
     * interfacce: bug trovato guardando l'anteprima, non sul pannello. */
    int n = 0, pl = (int)strlen(prefix);
    for (int i = 0; i < kv_n && n < max; i++) {
        if (!strncmp(kv_key[i], prefix, pl) && kv_key[i][pl] == '.') {
            snprintf(out[n], 160, "%.*s", 159, kv_val[i]);
            n++;
        }
    }
    return n;
}

/* Disegna una lista scorrevole. btns: etichette dei tasti per riga (max 4,
 * NULL = fine). L'id del tasto e' A_ROW0 + riga*4 + indice: cosi' il tocco sa
 * su QUALE riga e QUALE tasto si e' agito. */
static void draw_list(u32 *m, u32 pitch, const char *title, const char *prefix,
                      const char *const *btns) {
    static char rows[64][160];
    int n = list_rows(prefix, rows, 64);
    int nb = 0;
    while (nb < 4 && btns && btns[nb]) nb++;
    int *sc = &scroll_sel[cat][page_sel[cat]];
    (void)title;   /* il titolo non si disegna piu' (lo dice la barra dei tab) */

    char sub[48];
    snprintf(sub, sizeof(sub), "%d voci", n);
    draw_text(m, pitch, 470, 100, sub, C_DIM, 2);  /* riga del titolo: zona libera per costruzione */

    if (!n) { draw_text(m, pitch, 40, LIST_Y, "(nessun dato)", C_DIM, 3); return; }

    int maxrows = (PB_Y - 20 - LIST_Y) / ROW_H;
    if (maxrows > LIST_ROWS) maxrows = LIST_ROWS;
    int pages = (n + maxrows - 1) / maxrows;
    if (*sc >= pages) *sc = pages - 1;
    if (*sc < 0) *sc = 0;
    int start = (*sc) * maxrows;

    int bw = 112, bh = ROW_H - 26;
    int labw = 1032 - nb * (bw + 10) - 20;
    for (int i = 0; i < maxrows && start + i < n; i++) {
        int y = LIST_Y + i * ROW_H;
        fill_rect(m, pitch, 24, y, 1032, ROW_H - 6, (i & 1) ? C_PANEL : C_PANEL2);
        draw_text_fit(m, pitch, 40, y + 22, labw, rows[start + i], C_TEXT, 2);
        /* Senza tasti per riga e' la RIGA il punto di arresto della selezione:
         * l'evidenza dice su quale riga si e', e attivarla la fa leggere
         * intera nell'avviso (sul pannello puo' finire con i puntini). Quando i
         * tasti per riga ci sono, i punti di arresto sono i tasti. */
        if (!nb) focus_add(0, FK_ROW, 24, y, 1032, ROW_H - 6, rows[start + i]);
        for (int b = 0; b < nb; b++) {
            int bx = 1040 - (nb - b) * (bw + 10) + 4;
            draw_btn_at(m, pitch, bx, y + 10, bw, bh, btns[b], A_ROW0 + (start + i) * 4 + b, 2);
        }
    }
    if (pages > 1) {
        char p[24];
        snprintf(p, sizeof(p), "%d/%d", *sc + 1, pages);
        draw_text(m, pitch, 40, 2060, p, C_DIM, 3);
        draw_btn_at(m, pitch, 700, 2030, 160, 96, "SU", A_SCROLL_UP, 3);
        draw_btn_at(m, pitch, 880, 2030, 160, 96, "GIU", A_SCROLL_DN, 3);
    }
}

/* ---------- selezione: bilanciere volume + tasto laterale ----------
 * Il touch di questo telefono e' morto, quindi la UI si guida con tre soli
 * tasti: VOLUME SU/GIU' spostano la selezione, POWER corto attiva, POWER lungo
 * (>= 1.5 s) torna indietro. Si tiene la CHIAVE dell'elemento selezionato e non
 * la sua posizione nell'elenco: quando un tasto attivato cambia pagina la
 * posizione non significa piu' niente, mentre la chiave dice subito se
 * l'elemento esiste ancora (e se non esiste si riparte dalla testa). */
static int g_sel_key = 0;

/* Questo elemento e' selezionabile adesso? Con la tastiera aperta il fuoco sta
 * SOLO nella tastiera: e' modale, come lo e' per il tocco (kbd_hit). */
static int focus_visible(int i) {
    if (!kbd_open) return 1;
    if (g_focus[i].kind != FK_BTN) return 0;
    int id = g_focus[i].id;
    return (id >= KB_CH && id < KB_CH + 40) || (id >= KB_CANC && id <= KB_PULISCI);
}

/* Indice dell'elemento selezionato. */
static int focus_here(void) {
    if (!g_focus_n) return -1;
    if (g_sel_key)
        for (int i = 0; i < g_focus_n; i++)
            if (g_focus[i].key == g_sel_key && focus_visible(i)) return i;
    /* Nessuna selezione (primo giro) o selezione sparita (pagina cambiata): si
     * riparte dal primo elemento visibile. A tastiera aperta il primo disegnato
     * e' la X di annulla: si preferisce un tasto carattere, cosi' la prima
     * pressione non butta via la modifica. */
    if (kbd_open)
        for (int i = 0; i < g_focus_n; i++)
            if (focus_visible(i) && g_focus[i].id >= KB_CH && g_focus[i].id < KB_CH + 40) return i;
    for (int i = 0; i < g_focus_n; i++) if (focus_visible(i)) return i;
    return -1;
}

/* Sposta la selezione fra gli elementi visibili, in tondo. */
static void focus_move(int dir) {
    int i = focus_here();
    if (i < 0) return;
    g_sel_key = g_focus[i].key;          /* aggancia la selezione, se non c'e' */
    int moved = 0;
    for (int k = 0; k < g_focus_n; k++) {
        i += dir;
        if (i < 0) i = g_focus_n - 1;
        if (i >= g_focus_n) i = 0;
        if (focus_visible(i)) { moved = 1; break; }
    }
    if (!moved) return;                  /* nessun altro elemento visibile */
    g_sel_key = g_focus[i].key;
    dirty_req = 1;
    L("selezione %d di %d: %s", i + 1, g_focus_n, g_focus[i].txt);
}

/* POWER corto: attiva l'elemento selezionato. Stessa via del tocco
 * (do_action), quindi tasti, tab, categorie e righe fanno ESATTAMENTE quello
 * che facevano al dito - compreso aprire la tastiera sui campi. */
static void focus_activate(void) {
    int i = focus_here();
    if (i < 0) { toast_set("nessun elemento selezionato"); dirty_req = 1; return; }
    g_sel_key = g_focus[i].key;
    if (g_focus[i].kind == FK_ROW) { toast_set("%s", g_focus[i].txt); dirty_req = 1; return; }
    L("POWER corto: attiva '%s' (id=%d)", g_focus[i].txt, g_focus[i].id);
    do_action(g_focus[i].id);
    dirty_req = 1;
}

/* POWER lungo: si torna indietro di un livello. Prima la tastiera (modale),
 * poi la pagina della categoria. */
static void focus_back(void) {
    if (kbd_open) { kbd_key_action(KB_ANNULLA); return; }
    if (page_sel[cat] != 0) {
        page_sel[cat] = 0;
        g_sel_key = 0;                   /* pagina nuova: si riparte dalla testa */
        toast_set("indietro: %s", cat_pages[cat][0]);
        dirty_req = 1;
        return;
    }
    toast_set("sei gia' in cima (%s)", cat_names[cat]);
    dirty_req = 1;
}

/* Cornice dell'elemento selezionato: si chiama in fondo a draw_page, quindi su
 * ENTRAMBI i buffer. Per questo non deve cambiare stato in modo non
 * deterministico: a parita' di lista e di tastiera aperta/chiusa i due buffer
 * devono mostrare la stessa evidenza (altrimenti per un frame se ne vedono
 * due). L'unica scrittura e' l'aggancio della selezione al primo elemento,
 * che e' idempotente. */
static void focus_ring_draw(u32 *m, u32 pitch) {
    int i = focus_here();
    if (i < 0) return;
    if (!g_sel_key) g_sel_key = g_focus[i].key;   /* l'evidenza si vede dal primo frame */
    focus_ring(m, pitch, g_focus[i].x, g_focus[i].y, g_focus[i].w, g_focus[i].h);
}
