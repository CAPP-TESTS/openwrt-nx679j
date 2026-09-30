
/* ---------- disegno (CPU, su dumb buffer) ---------- */
#include "ui-font8x16.h"

/* Palette: scuro, alto contrasto sul pannello del telefono. */
#define C_BG      0x000f1620u
#define C_PANEL   0x001a2432u
#define C_PANEL2  0x00223042u
#define C_TEXT    0x00e8f0f8u
#define C_DIM     0x0090a4b8u
#define C_ACCENT  0x0030a8ffu
#define C_OK      0x0030d060u
#define C_WARN    0x00ffb020u
#define C_ERR     0x00ff4040u
#define C_BAR     0x00007a3cu

static void fill_rect(u32 *m, u32 pitch_bytes, int x0, int y0, int w, int h, u32 col) {
    unsigned char *base = (unsigned char *)m;
    int x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)W) x1 = (int)W;
    if (y1 > (int)H) y1 = (int)H;
    for (int y = y0; y < y1; y++) {
        u32 *row = (u32 *)(base + (size_t)y * pitch_bytes);
        for (int x = x0; x < x1; x++) row[x] = col;
    }
}

static void box(u32 *m, u32 pitch, int x, int y, int w, int h, u32 fill, int bw, u32 bc) {
    fill_rect(m, pitch, x, y, w, h, fill);
    if (bw > 0) {
        fill_rect(m, pitch, x, y, w, bw, bc);
        fill_rect(m, pitch, x, y + h - bw, w, bw, bc);
        fill_rect(m, pitch, x, y, bw, h, bc);
        fill_rect(m, pitch, x + w - bw, y, bw, h, bc);
    }
}

static int text_w(const char *s, int scale) { return (int)strlen(s) * FONT_W * scale; }

/* Disegna testo con il font 8x16. scale = ingrandimento (3 -> 24x48 px).
 * I caratteri fuori da [32,126] diventano spazi. */
static void draw_text(u32 *m, u32 pitch_bytes, int x, int y, const char *s, u32 col, int scale) {
    unsigned char *base = (unsigned char *)m;
    int cx = x;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned c = *p;
        if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
        const unsigned char *g = font8x16 + (c - FONT_FIRST) * 16;
        for (int r = 0; r < FONT_H; r++) {
            unsigned bits = g[r];
            if (!bits) continue;
            int py = y + r * scale;
            if (py < 0 || py >= (int)H) continue;
            for (int b = 0; b < FONT_W; b++) {
                if (!(bits & (0x80u >> b))) continue;
                int px = cx + b * scale;
                for (int dy = 0; dy < scale; dy++) {
                    int yy = py + dy;
                    if (yy < 0 || yy >= (int)H) continue;
                    u32 *row = (u32 *)(base + (size_t)yy * pitch_bytes);
                    for (int dx = 0; dx < scale; dx++) {
                        int xx = px + dx;
                        if (xx >= 0 && xx < (int)W) row[xx] = col;
                    }
                }
            }
        }
        cx += FONT_W * scale;
    }
}

static void draw_text_right(u32 *m, u32 pitch, int xr, int y, const char *s, u32 col, int scale) {
    draw_text(m, pitch, xr - text_w(s, scale), y, s, col, scale);
}

/* Testo che entra nello spazio dato: rimpicciolisce fino a 2 (sotto non sarebbe
 * leggibile sul telefono), poi taglia con i puntini. */
static void draw_text_fit(u32 *m, u32 pitch, int x, int y, int maxw, const char *s, u32 col, int scale) {
    int sc = scale;
    while (sc > 2 && text_w(s, sc) > maxw) sc--;
    if (text_w(s, sc) <= maxw) { draw_text(m, pitch, x, y, s, col, sc); return; }
    char buf[64];
    size_t n = strlen(s);
    if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
    memcpy(buf, s, n); buf[n] = 0;
    while (n > 1 && text_w(buf, sc) > maxw) buf[--n] = 0;
    /* tre puntini finali */
    if (n > 3) { buf[n - 1] = '.'; buf[n - 2] = '.'; buf[n - 3] = '.'; }
    draw_text(m, pitch, x, y, buf, col, sc);
}

/* ---------- elementi selezionabili (navigazione con i tasti volume) ----------
 * Il touch di questo telefono e' morto: l'unico ingresso sono il tasto laterale
 * (pmic_pwrkey = POWER) e il bilanciere volume (pmic_resin). La lista degli
 * elementi selezionabili NON si scrive a mano: la riempiono le funzioni di
 * disegno mentre disegnano. Due formule parallele (una per disegnare, una per
 * selezionare) divergono al primo ritocco del layout e l'evidenza finirebbe
 * dove non c'e' niente. Si raccoglie SOLO sul primo dei due buffer: il secondo
 * disegna lo stesso contenuto e raddoppierebbe la lista. */
struct focus_item {
    int  key;        /* identita' stabile fra due frame (>0 = tasto, <0 = riga) */
    int  kind;       /* FK_BTN = attiva l'id, FK_ROW = solo evidenza + testo */
    int  id;         /* azione da eseguire (solo per FK_BTN) */
    int  x, y, w, h; /* rettangolo su cui si disegna l'evidenza */
    char txt[48];    /* etichetta: finisce nel log e nell'avviso */
};
#define FK_BTN 0
#define FK_ROW 1
#define FOCUS_MAX 320

static struct focus_item g_focus[FOCUS_MAX];
static int g_focus_n = 0;
static int g_focus_on = 0;    /* 1 = il prossimo disegno raccoglie */
static int g_focus_row = 0;   /* contatore delle righe (chiavi negative) */

static void focus_reset(void) { g_focus_n = 0; g_focus_row = 0; }

static void focus_add(int id, int kind, int x, int y, int w, int h, const char *txt) {
    if (!g_focus_on || g_focus_n >= FOCUS_MAX) return;
    struct focus_item *it = &g_focus[g_focus_n++];
    it->kind = kind;
    it->id = id;
    it->x = x; it->y = y; it->w = w; it->h = h;
    /* Una riga di lista non ha un id: la sua chiave e' un negativo progressivo
     * (l'ordine del disegno e' stabile fra due frame). */
    it->key = (kind == FK_BTN) ? id : -(++g_focus_row);
    snprintf(it->txt, sizeof(it->txt), "%s", txt ? txt : "");
}

/* Cornice di selezione. Si disegna in FONDO a draw_page, dopo la tastiera:
 * un elemento disegnato piu' tardi coprirebbe la cornice dei precedenti (ed e'
 * quello che succedeva disegnandola tasto per tasto). Sta ATTORNO al
 * rettangolo, mai sopra: l'etichetta resta leggibile. */
static void focus_ring(u32 *m, u32 pitch, int x, int y, int w, int h) {
    const int b = 6;
    fill_rect(m, pitch, x - b, y - b, w + 2 * b, b, C_WARN);
    fill_rect(m, pitch, x - b, y + h, w + 2 * b, b, C_WARN);
    fill_rect(m, pitch, x - b, y, b, h, C_WARN);
    fill_rect(m, pitch, x + w, y, b, h, C_WARN);
}
