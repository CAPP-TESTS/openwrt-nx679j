
/* ---------- UI: pagine, layout, interpretazione del tocco ---------- */
enum { P_MODEM = 0, P_RETE = 1, P_SISTEMA = 2, P_NPAGES = 3 };

/* zone sensibili */
#define TAB_Y     2130
#define TAB_H     270
#define TAB_W     (1080 / P_NPAGES)
#define BTN_Y     1860
#define BTN_H     180
#define MARGIN    40
#define CONTENT_W (1080 - 2 * MARGIN)

#define A_TAB0    100
#define A_RECON   1
#define A_WIFI_ON 2
#define A_WIFI_OFF 3
#define A_SENS_UI 7    /* tasto NASCONDI/MOSTRA SIM */

/* Nasconde i dati sensibili della SIM su richiesta dell'utente (tasto
 * NASCONDI SIM). Si lasciano le ultime 3 cifre: bastano a riconoscere la
 * SIM senza esporla per intero su uno schermo sempre visibile. */
static int hide_sens = 0;
static const char *sens(const char *v) {
    static char sbuf[64];
    if (!hide_sens) return v;
    size_t l = strlen(v);
    if (l < 4) return "***";
    snprintf(sbuf, sizeof(sbuf), "***%s", v + l - 3);
    return sbuf;
}
#define A_REBOOT  4

static int reboot_armed = 0;
static int pressed = 0;      /* id premuto (per l'evidenziazione) */

/* ---- interfaccia al menu (ui-7): dichiarazioni anticipate ---- */
static void kbd_draw(u32 *, u32);
static void draw_topbar(u32 *, u32);
static void draw_pagebar(u32 *, u32);
static void draw_catbar(u32 *, u32);
static void menu_draw_page(u32 *, u32);
static int  hit_test_menu(int, int);
static void do_menu_action(int);
static void focus_ring_draw(u32 *, u32);   /* definita in ui-7 */

/* Il vecchio hit-test lineare e' sostituito da quello del menu: il chiamante
 * (ui-6) resta invariato. */
static int hit_test(int x, int y) { return hit_test_menu(x, y); }

static void do_action(int id) {
    /* Le azioni del menu (>= 30) le gestisce ui-7: li' c'e' il modello delle
     * pagine (liste, righe, scorrimento) che qui non esiste. */
    if (id >= 30) { do_menu_action(id); return; }
    switch (id) {
        case A_SENS_UI:
            hide_sens = !hide_sens;
            toast_set(hide_sens ? "Dati SIM nascosti" : "Dati SIM visibili");
            break;
        case A_RECON:
            run_bg("ubus call luci.nx679j-modem doReconnect");
            toast_set("Riconnessione richiesta...");
            break;
        case A_WIFI_ON:
            run_bg("wifi up");
            toast_set("Wi-Fi: accensione richiesta");
            break;
        case A_WIFI_OFF:
            run_bg("wifi down");
            toast_set("Wi-Fi: spegnimento richiesto");
            break;
        case A_REBOOT:
            if (!reboot_armed) { reboot_armed = 1; toast_until = now_ms() + 6000;
                                 snprintf(toast, sizeof(toast), "Tocca di nuovo RIAVVIA per confermare"); }
            else { toast_set("Riavvio in corso..."); run_bg("ubus call system reboot"); }
            break;
        default: break;
    }
}

/* etichetta a sinistra, valore a destra, su una riga */
static void row(u32 *m, u32 pitch, int y, const char *label, const char *val, u32 vcol) {
    draw_text(m, pitch, MARGIN, y, label, C_DIM, 3);
    draw_text_fit(m, pitch, 420, y, 1080 - 420 - MARGIN, val, vcol, 3);
    /* Anche una riga di sola lettura e' un punto di arresto della selezione: sul
     * pannello il valore puo' essere accorciato dai puntini, e attivarla lo fa
     * leggere intero nell'avviso. */
    char t[48];
    snprintf(t, sizeof(t), "%s: %s", label, val);
    focus_add(0, FK_ROW, MARGIN, y - 10, CONTENT_W, 74, t);
}

/* (le vecchie draw_top/draw_tabs sono state sostituite dalle barre del menu
 * in ui-7: qui restano solo i pezzi di pagina riusati) */

static void draw_btn(u32 *m, u32 pitch, int x, int w, const char *label, u32 col, int id) {
    u32 bg = (pressed == id) ? 0x00ffffffu : col;
    u32 fg = (pressed == id) ? col : 0x00001020u;
    box(m, pitch, x, BTN_Y, w, BTN_H, bg, 3, C_TEXT);
    int tw = text_w(label, 4);
    draw_text(m, pitch, x + (w - tw) / 2, BTN_Y + 52, label, fg, 4);
    focus_add(id, FK_BTN, x, BTN_Y, w, BTN_H, label);
}

/* QMI restituisce la banda come "eutran-3": in una riga di stato "B3" si legge
 * meglio e sta nella larghezza della colonna. */
static const char *bshort(const char *v) {
    if (!strncmp(v, "eutran-", 7)) return v + 7;
    return v;
}

static void draw_modem(u32 *m, u32 pitch) {
    const char *op = kv("modem.operator");
    draw_text(m, pitch, MARGIN, 300, *op ? op : "(nessun operatore)", C_TEXT, 5);

    char line[128];
    snprintf(line, sizeof(line), "%s - %s", kv("modem.registration"), kv("modem.tech"));
    draw_text(m, pitch, MARGIN, 400, line, C_OK, 3);   /* sotto l'operatore (300..380) */

    snprintf(line, sizeof(line), "%s  RSRP %s  RSRQ %s", kv("modem.rssi"), kv("modem.rsrp"), kv("modem.rsrq"));
    draw_text(m, pitch, MARGIN, 470, line, C_TEXT, 3);

    int y = 560;
    row(m, pitch, y, "IMEI",  sens(kv("modem.imei")), C_TEXT); y += 84;
    row(m, pitch, y, "Firmware", kv("modem.revision"), C_TEXT); y += 84;
    snprintf(line, sizeof(line), "1:%s  2:%s", kv("modem.slot1"), kv("modem.slot2"));
    row(m, pitch, y, "SIM", line, C_TEXT); y += 84;
    row(m, pitch, y, "ICCID", sens(kv("modem.iccid")), C_TEXT); y += 84;
    row(m, pitch, y, "IMSI",  sens(kv("modem.imsi")), C_TEXT); y += 84;
    row(m, pitch, y, "IPv4",  kv("modem.ip"), C_TEXT); y += 84;
    snprintf(line, sizeof(line), "route %s - sessioni %s", kv("modem.route"), kv("modem.session"));
    row(m, pitch, y, "Rete", line, C_TEXT); y += 84;

    const char *h = kv("modem.health");
    u32 hc = strncmp(h, "up", 2) ? C_ERR : C_OK;
    row(m, pitch, y, "Salute", h, hc);
    y += 84;
    /* Carrier aggregation (chiavi ca.*). Mostriamo lo STATO reale di ogni
     * portante: "deactivated" = la rete l'ha configurata ma non la sta usando.
     * Il contributo e' una STIMA dalla larghezza di banda, NON una misura:
     * l'aggregazione avviene dentro il modem e il kernel vede un solo flusso
     * aggregato su rmnet_data0. */
    snprintf(line, sizeof(line), "%s portanti, %s attive   aggregata %s MHz",
             kv("ca.totali"), kv("ca.attive"), kv("ca.dl"));
    row(m, pitch, y, "Aggreg.", line, C_TEXT); y += 84;
    snprintf(line, sizeof(line), "B%s  %s  PCI %s   %s MHz",
             bshort(kv("ca.pcc.band")), kv("ca.pcc.earfcn"), kv("ca.pcc.pci"), kv("ca.pcc.bw"));
    row(m, pitch, y, "PCC", line, C_OK); y += 84;
    if (*bshort(kv("ca.scc1.band"))) {
        snprintf(line, sizeof(line), "B%s  %s  PCI %s   %s MHz   %s",
                 bshort(kv("ca.scc1.band")), kv("ca.scc1.earfcn"), kv("ca.scc1.pci"),
                 kv("ca.scc1.bw"), kv("ca.scc1.state"));
        row(m, pitch, y, "SCC1", line, C_DIM); y += 84;
        snprintf(line, sizeof(line), "RSRP %s dBm    stima %s%% se attiva",
                 kv("ca.scc1.rsrp"), kv("ca.scc1.stima"));
        row(m, pitch, y, "SCC1", line, C_DIM);
    }

    int half = (CONTENT_W - 20) / 2;
    draw_btn(m, pitch, MARGIN, half, "RICONNETTI", C_ACCENT, A_RECON);
    draw_btn(m, pitch, MARGIN + half + 20, half,
             hide_sens ? "MOSTRA SIM" : "NASCONDI SIM", C_ACCENT, A_SENS_UI);
}

static void draw_rete(u32 *m, u32 pitch) {
    struct { const char *key; const char *name; } ifs[2] = {
        { "if.lan_wifi",  "Wi-Fi (LAN)" },
        { "if.wan_early", "Modem (WAN)" },
    };
    int y = 360;
    for (int i = 0; i < 2; i++) {
        const char *st = kv_part(ifs[i].key, 0);
        const char *dev = kv_part(ifs[i].key, 1);
        const char *ip = kv_part(ifs[i].key, 2);
        int up = !strcmp(st, "up");
        box(m, pitch, MARGIN, y, CONTENT_W, 430, C_PANEL, 2, up ? C_OK : C_ERR);
        draw_text(m, pitch, MARGIN + 30, y + 30, ifs[i].name, C_TEXT, 4);
        draw_text_right(m, pitch, 1080 - MARGIN - 30, y + 34, up ? "SU" : "GIU", up ? C_OK : C_ERR, 4);
        draw_text(m, pitch, MARGIN + 30, y + 140, "Dispositivo", C_DIM, 3);
        draw_text_fit(m, pitch, MARGIN + 30, y + 210, CONTENT_W - 60, dev, C_TEXT, 3);
        draw_text(m, pitch, MARGIN + 30, y + 290, "Indirizzo", C_DIM, 3);
        draw_text_fit(m, pitch, MARGIN + 30, y + 350, CONTENT_W - 60, ip, C_TEXT, 3);
        y += 480;
    }
    int half = (CONTENT_W - 20) / 2;
    draw_btn(m, pitch, MARGIN, half, "WI-FI SU", C_OK, A_WIFI_ON);
    draw_btn(m, pitch, MARGIN + half + 20, half, "WI-FI GIU", C_ERR, A_WIFI_OFF);
}


static void draw_toast(u32 *m, u32 pitch) {
    if (!toast[0] || now_ms() > toast_until) return;
    int w = text_w(toast, 3) + 60;
    if (w > 1000) w = 1000;
    box(m, pitch, (1080 - w) / 2, 1690, w, 110, C_WARN, 0, 0);
    draw_text_fit(m, pitch, (1080 - w) / 2 + 30, 1718, w - 60, toast, 0x00001020u, 3);
}

/* Composizione dello schermo: barre del menu (ui-7) + contenuto della pagina
 * corrente + eventuale avviso a comparsa. */
static void draw_page(u32 *m, u32 pitch) {
    /* Anti-residuo sul vetro: il pannello riscrive solo le zone che il driver
     * ritiene cambiate, quindi i pixel vecchi restano visibili sotto i nuovi
     * (e' quello che si vede come "testi sovrapposti"). Alternando lo sfondo
     * di 1 LSB a ogni ridisegno TUTTI i pixel del frame risultano cambiati:
     * il driver riscrive l'area intera e i residui spariscono.
     * La differenza e' 1/255 sul canale blu: invisibile. */
    static unsigned bg_flip = 0;
    fill_rect(m, pitch, 0, 0, W, H, C_BG ^ (bg_flip++ & 1u));
    draw_topbar(m, pitch);
    draw_pagebar(m, pitch);
    menu_draw_page(m, pitch);
    draw_toast(m, pitch);
    kbd_draw(m, pitch);       /* la tastiera copre tutto: e' modale */
    draw_catbar(m, pitch);
    /* L'evidenza va per ULTIMA: cosi' nessun elemento disegnato dopo la copre. */
    focus_ring_draw(m, pitch);
    __sync_synchronize();
}
