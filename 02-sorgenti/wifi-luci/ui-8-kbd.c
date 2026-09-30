static void draw_btn_at(u32 *, u32, int, int, int, int, const char *, int, int);
static int dirty_req;   /* definita in ui-7: qui serve per segnalare il ridisegno */
/* ---------- tastiera a schermo ----------
 * serve a modificare i campi di testo dal display (hostname, SSID, chiave
 * Wi-Fi, password). 4 righe da 10 tasti + barra comandi. */
#define KB_Y     1420
#define KB_KH    150
#define KB_CW    108
#define KB_BAR   (KB_Y + 4 * KB_KH + 10)

static int  kbd_open = 0;
static char kbd_buf[128];
static int  kbd_len = 0;
static int  kbd_mask = 0;
static char kbd_key[48];      /* quale campo si sta modificando */
static char kbd_title[64];

static const char *kb_rows[4] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl#",
    "zxcvbnm@_.-"
};
/* Etichette e id della barra comandi: dichiarati QUI perche' kbd_draw li usa. */
static const char *kb_bar_lab[4] = { "SPAZIO", "CANCELLA", "PULISCI", "APPLICA" };
static const int   kb_bar_id[4]  = { 801, 800, 804, 802 };   /* SPAZ, CANC, PULISCI, OK */

/* Id dei tasti: 700+indice per i caratteri, 800.. per i comandi. */
#define KB_CH    700
#define KB_CANC  800
#define KB_SPAZ  801
#define KB_OK    802
#define KB_ANNULLA 803
#define KB_PULISCI 804

static void kbd_draw(u32 *m, u32 pitch) {
    if (!kbd_open) return;
    fill_rect(m, pitch, 0, KB_Y - 210, 1080, 2400 - (KB_Y - 210), C_PANEL);
    draw_text(m, pitch, 40, KB_Y - 190, kbd_title, C_ACCENT, 3);
    /* valore in corso: se e' una password si mostrano asterischi */
    char show[80];
    int k = 0;
    for (int i = 0; i < kbd_len && k < 40; i++) show[k++] = kbd_mask ? '*' : kbd_buf[i];
    show[k] = 0;
    draw_text(m, pitch, 40, KB_Y - 130, show, C_TEXT, 4);
    draw_btn_at(m, pitch, 900, KB_Y - 200, 140, 100, "X", KB_ANNULLA, 4);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 10; c++) {
            char lab[2] = { kb_rows[r][c], 0 };
            draw_btn_at(m, pitch, c * KB_CW + 2, KB_Y + r * KB_KH + 2, KB_CW - 4, KB_KH - 6,
                        lab, KB_CH + r * 10 + c, 5);
        }
    int bw = (1040 - 3 * 12) / 4;
    for (int i = 0; i < 4; i++)
        draw_btn_at(m, pitch, 40 + i * (bw + 12), KB_BAR, bw, 120,
                    kb_bar_lab[i], kb_bar_id[i], 3);
}

static int kbd_hit(int x, int y) {
    if (!kbd_open) return 0;
    if (y >= KB_Y && y < KB_Y + 4 * KB_KH) {
        int r = (y - KB_Y) / KB_KH, c = x / KB_CW;
        return (r >= 0 && r < 4 && c >= 0 && c < 10) ? KB_CH + r * 10 + c : 0;
    }
    if (y >= KB_BAR && y < KB_BAR + 120) {
        int bw = (1040 - 3 * 12) / 4;
        int i = (x - 40) / (bw + 12);
        return (x >= 40 && i >= 0 && i < 4) ? kb_bar_id[i] : 0;
    }
    if (y >= KB_Y - 210 && x >= 900 && x < 1040 && y < KB_Y - 100) return KB_ANNULLA;
    return 0;
}

static void kbd_open_for(const char *key, const char *title, const char *cur, int mask) {
    snprintf(kbd_key, sizeof(kbd_key), "%s", key);
    snprintf(kbd_title, sizeof(kbd_title), "%s", title);
    snprintf(kbd_buf, sizeof(kbd_buf), "%s", cur ? cur : "");
    kbd_len = (int)strlen(kbd_buf);
    kbd_mask = mask;
    kbd_open = 1;
    dirty_req = 1;
}

/* Applica il valore modificato. Il testo finisce in una riga di shell: si
 * ripuliscono apici, backtick, $ e backslash (nessun carattere di controllo).
 * I campi vuoti annullano invece di scrivere un valore vuoto nel config. */
static void kbd_apply(void) {
    char v[340], clean[128];
    int k = 0;
    for (int i = 0; i < kbd_len && k < 100; i++) {
        char ch = kbd_buf[i];
        if (ch == '\'' || ch == '"' || ch == '`' || ch == '$' || ch == '\\' || ch < 32) continue;
        clean[k++] = ch;
    }
    clean[k] = 0;
    if (!clean[0]) { toast_set("valore vuoto: annullato"); kbd_open = 0; dirty_req = 1; return; }
    if (!strcmp(kbd_key, "hostname"))
        snprintf(v, sizeof(v), "uci set system.@system[0].hostname='%s' && uci commit system", clean);
    else if (!strcmp(kbd_key, "ssid"))
        snprintf(v, sizeof(v), "uci set wireless.default_radio0.ssid='%s' && uci commit wireless && wifi reload", clean);
    else if (!strcmp(kbd_key, "key"))
        snprintf(v, sizeof(v), "uci set wireless.default_radio0.key='%s' && uci commit wireless && wifi reload", clean);
    else if (!strcmp(kbd_key, "rootpw"))
        snprintf(v, sizeof(v), "ubus call luci setPassword '{\"username\":\"root\",\"password\":\"%s\"}'", clean);
    /* Campi di rete: si accettano solo cifre, punti, spazi e virgole. Un valore
     * malformato qui cambierebbe l'indirizzo della LAN o il DHCP: meglio
     * rifiutarlo che scriverlo. */
    else if (!strcmp(kbd_key, "lanip") || !strcmp(kbd_key, "netmask") ||
             !strcmp(kbd_key, "dhcp_start") || !strcmp(kbd_key, "dhcp_limit") ||
             !strcmp(kbd_key, "dns")) {
        int okv = 0;
        char num[96];
        int j = 0;
        for (int i = 0; clean[i] && j < 90; i++) {
            char ch = clean[i];
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == ' ' || ch == ',') { num[j++] = ch; if (ch >= '0' && ch <= '9') okv = 1; }
        }
        num[j] = 0;
        if (!okv) { toast_set("valore non valido: annullato"); kbd_open = 0; dirty_req = 1; return; }
        if (!strcmp(kbd_key, "lanip"))
            snprintf(v, sizeof(v), "uci set network.lan_wifi.ipaddr='%s' && uci commit network && ifup lan_wifi", num);
        else if (!strcmp(kbd_key, "netmask"))
            snprintf(v, sizeof(v), "uci set network.lan_wifi.netmask='%s' && uci commit network && ifup lan_wifi", num);
        else if (!strcmp(kbd_key, "dhcp_start"))
            snprintf(v, sizeof(v), "uci set dhcp.lan.start='%s' && uci commit dhcp && /etc/init.d/dnsmasq restart", num);
        else if (!strcmp(kbd_key, "dhcp_limit"))
            snprintf(v, sizeof(v), "uci set dhcp.lan.limit='%s' && uci commit dhcp && /etc/init.d/dnsmasq restart", num);
        else {
            char num2[96];
            snprintf(num2, sizeof(num2), "%s", num);
            char *p = num2; while (*p) { if (*p == ',' || *p == ' ') *p = ' '; p++; }
            snprintf(v, sizeof(v), "uci -q delete dhcp.@dnsmasq[0].server; for s in %s; do uci add_list dhcp.@dnsmasq[0].server=$s; done; uci commit dhcp && /etc/init.d/dnsmasq restart", num2);
        }
    }
    /* Riga del crontab: "cr<N>" sostituisce la riga N, "crnew" aggiunge in fondo.
     * Il testo e' gia' ripulito dagli apici, quindi sta dentro '...' senza rischi. */
    else if (!strcmp(kbd_key, "dnnew")) {
        char ok[96]; int j = 0, hasip = 0;
        for (int i = 0; clean[i] && j < 90; i++) {
            char ch = clean[i];
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                ch == '.' || ch == '-' || ch == ' ') { ok[j++] = ch; if (ch == '.') hasip = 1; }
        }
        ok[j] = 0;
        if (!hasip) { toast_set("servono nome e indirizzo"); kbd_open = 0; dirty_req = 1; return; }
        snprintf(v, sizeof(v),
            "set -- %s; uci add dhcp domain >/dev/null && uci set dhcp.@domain[-1].name=\"$1\" && uci set dhcp.@domain[-1].ip=\"$2\" && uci commit dhcp && /etc/init.d/dnsmasq restart", ok);
    }
    else if (!strcmp(kbd_key, "shnew")) {
        /* "nome ip [mac]": si accettano lettere, cifre, punti, due punti e trattini */
        char ok[96]; int j = 0, hasip = 0;
        for (int i = 0; clean[i] && j < 90; i++) {
            char ch = clean[i];
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                ch == '.' || ch == ':' || ch == '-' || ch == ' ') { ok[j++] = ch; if (ch == '.') hasip = 1; }
        }
        ok[j] = 0;
        if (!hasip) { toast_set("servono nome e indirizzo"); kbd_open = 0; dirty_req = 1; return; }
        snprintf(v, sizeof(v),
            "set -- %s; uci add dhcp host >/dev/null && uci set dhcp.@host[-1].name=\"$1\" && uci set dhcp.@host[-1].ip=\"$2\" && { [ -n \"$3\" ] && uci set dhcp.@host[-1].mac=\"$3\" || true; } && uci commit dhcp && /etc/init.d/dnsmasq restart", ok);
    }
    else if (!strncmp(kbd_key, "cr", 2)) {
        if (!strcmp(kbd_key, "crnew"))
            snprintf(v, sizeof(v), "printf '%%s\\n' '%s' >> /etc/crontabs/root && /etc/init.d/cron restart", clean);
        else
            snprintf(v, sizeof(v),
                "awk -v n=%d -v t='%s' 'NR==n{print t; next}{print}' /etc/crontabs/root > /tmp/cr.$$ && mv /tmp/cr.$$ /etc/crontabs/root && /etc/init.d/cron restart",
                atoi(kbd_key + 2), clean);
    }
    else { kbd_open = 0; dirty_req = 1; return; }
    run_bg(v);
    toast_set("%s: applicato", kbd_title);
    kbd_open = 0;
    dirty_req = 1;
}

/* Tasto della tastiera. Ritorna 1 se l'id era suo. */
static int kbd_key_action(int id) {
    if (!kbd_open) return 0;
    if (id >= KB_CH && id < KB_CH + 40) {
        char ch = kb_rows[(id - KB_CH) / 10][(id - KB_CH) % 10];
        if (kbd_len < (int)sizeof(kbd_buf) - 2) { kbd_buf[kbd_len++] = ch; kbd_buf[kbd_len] = 0; }
        dirty_req = 1; return 1;
    }
    if (id == KB_SPAZ) {
        if (kbd_len < (int)sizeof(kbd_buf) - 2) { kbd_buf[kbd_len++] = ' '; kbd_buf[kbd_len] = 0; }
        dirty_req = 1; return 1;
    }
    if (id == KB_CANC)    { if (kbd_len) kbd_buf[--kbd_len] = 0; dirty_req = 1; return 1; }
    if (id == KB_PULISCI) { kbd_len = 0; kbd_buf[0] = 0; dirty_req = 1; return 1; }
    if (id == KB_OK)      { kbd_apply(); return 1; }
    if (id == KB_ANNULLA) { kbd_open = 0; toast_set("modifica annullata"); dirty_req = 1; return 1; }
    return 0;
}
