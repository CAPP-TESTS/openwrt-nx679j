/* nx679j-ui — interfaccia grafica sul display del telefono per controllare OpenWrt.
 *
 * Impalcatura DRM ripresa da nx679j-kiosk3.c (provata sul pannello): ioctl
 * grezzi, dumb buffer, commit atomici NONBLOCK con OUT_FENCE_PTR, un commit in
 * volo, fence con poll(). Il pannello di questo telefono collassa se resta
 * senza commit per piu' di ~58 ms: il loop non si ferma MAI per fare I/O —
 * i dati arrivano da un file che un demone separato tiene aggiornato.
 *
 * Uso: nx679j-ui [--dati /tmp/ui-data.txt]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <sys/wait.h>

/* Orologio MONOTONO (uptime), NON wall-clock.
 * MISURATO sul device: con gettimeofday nel 2026 il valore e' ~1.79e12 ms e il
 * cast a int lo tronca a un numero NEGATIVO (-654699432). Ogni scadenza
 * inizializzata a 0 smetteva percio' di funzionare: key_scan() usciva subito
 * (t_key_scan_next = 0, tn < 0 -> return) e non apriva MAI pmic_pwrkey /
 * pmic_resin, e l'anti-tocco-fantasma (now_ms() < t_wake_ignore, t_wake_ignore
 * = 0) restava sempre vero. CLOCK_MONOTONIC parte da 0 a ogni boot, non dipende
 * dall'ora di sistema (niente salti da NTP) e resta positivo per 24 giorni;
 * tutte le altre scadenze del file usano differenze, quindi restano corrette. */
static int now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }

typedef uint32_t u32; typedef uint64_t u64;

struct modeinfo { u32 clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; u32 vrefresh, flags, type; char name[32]; };
struct get_conn { u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr; u32 count_modes, count_props, count_encoders, encoder_id, connector_id, connector_type, connector_type_id, connection, mm_w, mm_h, subpixel, pad; };
struct crtc { u64 set_connectors_ptr; u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid; struct modeinfo mode; };
struct create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; };
struct map_dumb { u32 handle, pad; u64 offset; };
struct fb_cmd { u32 fb_id, width, height, pitch, bpp, depth, handle; };
struct get_property { u64 values_ptr, enum_blob_ptr; u32 prop_id, flags; char name[32]; u32 count_values, count_enum_blobs; };
struct create_blob { u64 data; u32 length, blob_id; };
struct atomic_req { u32 flags, count_objs; u64 objs_ptr, count_props_ptr, props_ptr, prop_values_ptr, reserved, user_data; };
struct set_client_cap { u64 capability, value; };
struct page_flip { u32 crtc_id, fb_id, flags, reserved; u64 user_data; };

#define GETCRTC        0xc06864a1
#define GETCONNECTOR   0xc05064a7
#define CREATE_DUMB    0xc02064b2
#define MAP_DUMB       0xc01064b3
#define ADDFB          0xc01c64ae
#define GETPROPERTY    0xc04064aa
#define CREATE_BLOB    0xc01064bd
#define ATOMIC         0xc03864bc
#define SET_MASTER     0x641e
#define SET_CLIENT_CAP 0x4010640d

#define SLOT 443                 /* slot rawdump per il log (come kiosk3) */
struct input_event { u64 sec, usec; uint16_t type, code; int32_t value; };
#define EV_ABS 3
#define EV_KEY 1
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
#define EV_SYN 0
#define SYN_REPORT 0
/* bitmask delle capacita' di un device input (usato per riconoscere un touch) */
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)

/* Proprieta' di un oggetto DRM (serve a verificare a chi appartiene un nome:
 * lo stesso nome puo' esistere su piu' oggetti e un commit atomico che usa
 * la proprieta' sbagliata viene rifiutato IN BLOCCO, cioe' display nero). */
#define OBJ_GETPROPS   0xc02064b9
#define OBJ_TYPE_CRTC  0xcccccccc
struct obj_props { u64 props_ptr, values_ptr; u32 count_props, obj_id, obj_type; };

static int fd = -1;
static u32 W = 1080, H = 2400;
static char logbuf[32768]; static size_t loglen = 0;
static int g_quiet = 0;

static void L(const char *fmt, ...) {
    char tmp[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        printf("%s\n", tmp); fflush(stdout);
        if (loglen + (size_t)n + 1 < sizeof(logbuf)) { memcpy(logbuf + loglen, tmp, (size_t)n); loglen += (size_t)n; logbuf[loglen++] = '\n'; logbuf[loglen] = 0; }
        if (!g_quiet) {
            int rd = open("/proc/1/root/dev/rd", O_WRONLY);
            if (rd >= 0) { lseek(rd, (off_t)(32 + SLOT) * 32768, SEEK_SET); ssize_t w = write(rd, logbuf, loglen); (void)w; close(rd); }
        }
    }
}

/* ---------- livello DRM (ripreso da nx679j-kiosk3.c, provato sul pannello) ---------- */

static u32 find_prop_global(const char *name) {
    for (u32 id=1; id<500; id++) {
        struct get_property gp; memset(&gp,0,sizeof(gp)); gp.prop_id=id;
        if (ioctl(fd, GETPROPERTY, &gp)) continue;
        if (!strcmp(gp.name, name)) return id;
    }
    return 0;
}

static u32 *kmaps[4]; static u32 kfbs[4]; static int kidx = 0;

static u32 make_fb(u32 color, u32 *out_pitch) {
    struct create_dumb cd; memset(&cd,0,sizeof(cd)); cd.width=W; cd.height=H; cd.bpp=32;
    if (ioctl(fd, CREATE_DUMB, &cd)) { L("createdumb errno=%d", errno); return 0; }
    struct map_dumb md; memset(&md,0,sizeof(md)); md.handle=cd.handle;
    if (ioctl(fd, MAP_DUMB, &md)) { L("mapdumb errno=%d", errno); return 0; }
    unsigned char *m = mmap(NULL, cd.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (m==MAP_FAILED){ L("mmap errno=%d", errno); return 0; }
    if (kidx < 4) kmaps[kidx] = (u32*)m;
    for (u32 y=0;y<H;y++){ u32 *row=(u32*)(m+(size_t)y*cd.pitch); for(u32 x=0;x<W;x++) row[x]=color; }
    __sync_synchronize();
    struct fb_cmd fc; memset(&fc,0,sizeof(fc));
    fc.width=W; fc.height=H; fc.pitch=cd.pitch; fc.bpp=32; fc.depth=24; fc.handle=cd.handle;
    if (ioctl(fd, ADDFB, &fc)) { L("addfb errno=%d", errno); return 0; }
    if (out_pitch) *out_pitch=cd.pitch;
    if (kidx < 4) { kfbs[kidx] = fc.fb_id; kidx++; }
    return fc.fb_id;
}

static u32 conn=(u32)-1, crtc=0, plane=103, modeid_id, active_id, conn_crtcid, ar_id;
static u32 p_crtcid, p_fbid, p_srcw, p_srch, p_srcx, p_srcy, p_crtcw, p_crtch, p_crtcx, p_crtcy;
static u32 ftm_id; static int use_posted=0;
static struct modeinfo best; static u32 blob_id;

/* flip NONBLOCK + OUT_FENCE_PTR, un commit in volo, mai idle >58 ms */
static u32 out_fence_id = 0;
static u32 idle_pc_id = 0;      /* proprieta' CRTC idle_pc_state (verificata appartenere al CRTC) */
static int g_idle_pc_off = 0;   /* 1 = chiedi idle_pc_disable (flag file /tmp/ui-idle-pc-disable) */

/* Prop_on_crtc: la proprieta' appartiene al CRTC? Restituisce il valore attuale
 * se si', altrimenti -1. Serve PRIMA di scrivere: lo stesso nome puo' esistere
 * su piu' oggetti e un commit atomico con la proprieta' sbagliata viene
 * rifiutato in blocco. */
static int prop_on_crtc(u32 pid) {
    u32 pids[64]; u64 pvals[64];
    struct obj_props op; memset(&op, 0, sizeof(op));
    op.props_ptr = (u64)(uintptr_t)pids; op.values_ptr = (u64)(uintptr_t)pvals;
    op.count_props = 64; op.obj_id = crtc; op.obj_type = OBJ_TYPE_CRTC;
    if (ioctl(fd, OBJ_GETPROPS, &op)) return -1;
    for (u32 i = 0; i < op.count_props && i < 64; i++)
        if (pids[i] == pid) return (int)pvals[i];
    return -1;
}

static int commit_plane_only(u32 fbid, int *fence_out) {
    u32 objs[2]; u32 counts[2]; u32 props[24]; u64 vals[24]; int np=0, no=0, c0;
    int fence_fd = -1;
    /* Il valore nel driver e' appiccicoso: smettere di scriverlo NON riporta il
     * comportamento di default. Quindi lo si scrive SEMPRE in modo esplicito:
     * 2=idle_pc_disable (flag attivo) oppure 0=idle_pc_none (default di boot).
     * Cosi' il flag file commuta davvero fra i due comportamenti, a caldo. */
    if (idle_pc_id) {
        c0=np;
        if (out_fence_id) { props[np]=out_fence_id; vals[np++]=(u64)(uintptr_t)&fence_fd; }
        props[np]=idle_pc_id; vals[np++]= g_idle_pc_off ? 2 : 0;
        counts[no]=np-c0; objs[no++]=crtc;
    } else if (out_fence_id) {
        c0=np; props[np]=out_fence_id; vals[np++]=(u64)(uintptr_t)&fence_fd;
        counts[no]=np-c0; objs[no++]=crtc;
    }
    c0=np;
    props[np]=p_crtcid; vals[np++]=crtc;
    props[np]=p_fbid; vals[np++]=fbid;
    props[np]=p_srcw; vals[np++]=((u64)W)<<16;
    props[np]=p_srch; vals[np++]=((u64)H)<<16;
    props[np]=p_srcx; vals[np++]=0;
    props[np]=p_srcy; vals[np++]=0;
    props[np]=p_crtcw; vals[np++]=W;
    props[np]=p_crtch; vals[np++]=H;
    props[np]=p_crtcx; vals[np++]=0;
    props[np]=p_crtcy; vals[np++]=0;
    counts[no]=np-c0; objs[no++]=plane;

    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=0x200;                 /* NONBLOCK */
    ar.count_objs=no;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0;
    int r = ioctl(fd, ATOMIC, &ar);
    if (fence_out) *fence_out = fence_fd;
    else if (fence_fd >= 0) close(fence_fd);
    return r? -errno : 0;
}

static int wait_fence(int ffd, int ms) {
    if (ffd < 0) return 1;
    struct pollfd p; p.fd=ffd; p.events=POLLIN; p.revents=0;
    int pr = poll(&p, 1, ms);
    close(ffd);
    if (pr > 0) return 0;
    if (pr == 0) return 1;
    return -1;
}

static int commit_cnp(u32 fbid, int test_only, int full) {
    u32 objs[3]; u32 counts[3]; u32 props[32]; u64 vals[32]; int np=0, no=0;
    int c0;
    if (full == 2) {
        c0=np; props[np]=modeid_id; vals[np++]=blob_id;
        props[np]=active_id; vals[np++]=1;
        /* La protezione va messa ANCHE nel primo commit: il modeset riconfigura
         * l'encoder e questo e' il momento piu' delicato (tipicamente dopo un
         * rimpiazzo a caldo, quando il pannello ha appena passato un gap). */
        if (idle_pc_id) { props[np]=idle_pc_id; vals[np++]= g_idle_pc_off ? 2 : 0; }
        counts[no]=np-c0; objs[no++]=crtc;
    } else if (full == 1) {
        c0=np; props[np]=modeid_id; vals[np++]=blob_id;
        if (idle_pc_id) { props[np]=idle_pc_id; vals[np++]= g_idle_pc_off ? 2 : 0; }
        counts[no]=np-c0; objs[no++]=crtc;
    }
    c0=np; props[np]=conn_crtcid; vals[np++]=crtc;
    if (ar_id) { props[np]=ar_id; vals[np++]=0; }
    if (use_posted && ftm_id) { props[np]=ftm_id; vals[np++]=2; }
    counts[no]=np-c0; objs[no++]=conn;
    c0=np; props[np]=p_crtcid; vals[np++]=crtc;
    props[np]=p_fbid; vals[np++]=fbid;
    props[np]=p_srcw; vals[np++]=((u64)W)<<16;
    props[np]=p_srch; vals[np++]=((u64)H)<<16;
    props[np]=p_srcx; vals[np++]=0;
    props[np]=p_srcy; vals[np++]=0;
    props[np]=p_crtcw; vals[np++]=W;
    props[np]=p_crtch; vals[np++]=H;
    props[np]=p_crtcx; vals[np++]=0;
    props[np]=p_crtcy; vals[np++]=0;
    counts[no]=np-c0; objs[no++]=plane;

    struct atomic_req ar; memset(&ar,0,sizeof(ar));
    ar.flags=(test_only? (0x400|0x100) : 0x400); ar.count_objs=no;
    ar.objs_ptr=(u64)(uintptr_t)objs; ar.count_props_ptr=(u64)(uintptr_t)counts;
    ar.props_ptr=(u64)(uintptr_t)props; ar.prop_values_ptr=(u64)(uintptr_t)vals;
    errno=0;
    int r = ioctl(fd, ATOMIC, &ar);
    return r? -errno : 0;
}

/* ---------- input touch ----------
 * Il touch reale e' event0, ma il numero del nodo non e' garantito fra boot:
 * si aprono TUTTI i device che espongono ABS_MT_POSITION_X, con riscansione
 * periodica. Cosi' (a) il tocco sopravvive a un cambio di numerazione, e
 * (b) la UI vede anche un device iniettato via uinput, che serve a verificare
 * da remoto il percorso tocco->hit-test->azione. */
#define MAX_TFD 8
static int tfds[MAX_TFD];
static int tfd_idx[MAX_TFD];
static int n_tfd = 0;
static int t_x = 540, t_y = 1200, t_down = 0;
static int t_down_x = 0, t_down_y = 0;
static int t_ev_down = 0, t_ev_up = 0;   /* eventi pendenti da consumare nel loop */
static int pending_down = 0, pending_up = 0;  /* prenotati, risolti a fine frame */

static int abs_has_mt_x(int f) {
    unsigned long bits[8];
    memset(bits, 0, sizeof(bits));
    if (ioctl(f, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) return 0;
    int bit = ABS_MT_POSITION_X;
    return (int)((bits[bit / (int)(8 * sizeof(long))] >> (bit % (int)(8 * sizeof(long)))) & 1UL);
}

static void touch_scan(void) {
    for (int i = 0; i < 32; i++) {
        int have = 0;
        for (int k = 0; k < n_tfd; k++) if (tfd_idx[k] == i) have = 1;
        if (have) continue;
        char p[40];
        snprintf(p, sizeof(p), "/dev/input/event%d", i);
        int f = open(p, O_RDONLY | O_NONBLOCK);
        if (f < 0) continue;
        if (!abs_has_mt_x(f)) { close(f); continue; }
        if (n_tfd >= MAX_TFD) { close(f); continue; }
        tfds[n_tfd] = f; tfd_idx[n_tfd] = i; n_tfd++;
        L("touch: %s aperto (fd=%d, %d device attivi)", p, f, n_tfd);
    }
}

static void touch_drop(int k) {
    close(tfds[k]);
    n_tfd--;
    tfds[k] = tfds[n_tfd];
    tfd_idx[k] = tfd_idx[n_tfd];
    L("touch: device rimosso (restano %d)", n_tfd);
}

static void poll_touch(void) {
    struct input_event ev;
    for (int k = 0; k < n_tfd; k++) {
        ssize_t r;
        while ((r = read(tfds[k], &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_MT_POSITION_X) t_x = ev.value;
                else if (ev.code == ABS_MT_POSITION_Y) t_y = ev.value;
                else if (ev.code == ABS_MT_TRACKING_ID) {
                    /* NON si risolve qui l'inizio del tocco: in questo frame le
                     * POSITION arrivano DOPO il TRACKING_ID, quindi le coordinate
                     * sarebbero quelle del tocco precedente. Si prenota e si
                     * risolve a fine frame (SYN_REPORT), quando sono definitive. */
                    if (ev.value >= 0) pending_down = 1;
                    else pending_up = 1;
                }
            } else if (ev.type == EV_SYN) {
                if (pending_down) { pending_down = 0; t_down = 1; t_down_x = t_x; t_down_y = t_y; t_ev_down = 1; }
                if (pending_up) { pending_up = 0; t_down = 0; t_ev_up = 1; }
            }
        }
        if (r < 0 && (errno == ENODEV || errno == EBADF)) { touch_drop(k); k--; }
    }
}

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

/* ---------- dati: file key=value prodotto da nx679j-ui-fetch ---------- */
/* 512 chiavi: la superficie completa (servizi, log, processi, rotte, lease,
 * impostazioni) da sola supera le 150 righe. */
#define MAXKV 512

static char kv_key[MAXKV][48];
static char kv_val[MAXKV][160];
static int kv_n = 0;
static long kv_mtime = 0;

static int kv_load(const char *path) {   /* ritorna 1 se i dati sono cambiati */
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (st.st_mtime == kv_mtime) return 0;         /* niente di nuovo: non rileggo */
    kv_mtime = st.st_mtime;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int n = 0;
    char line[320];
    while (fgets(line, sizeof(line), f) && n < MAXKV) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        snprintf(kv_key[n], sizeof(kv_key[0]), "%.*s", (int)sizeof(kv_key[0]) - 1, line);
        char *v = eq + 1;
        size_t l = strlen(v);
        while (l && (v[l-1] == '\n' || v[l-1] == '\r')) v[--l] = 0;
        snprintf(kv_val[n], sizeof(kv_val[0]), "%.*s", (int)sizeof(kv_val[0]) - 1, v);
        n++;
    }
    fclose(f);
    kv_n = n;
    L("dati aggiornati: %d chiavi", kv_n);
    return 1;   /* dati nuovi: il chiamante deve ridisegnare */
}

static const char *kv(const char *key) {
    for (int i = 0; i < kv_n; i++) if (!strcmp(kv_key[i], key)) return kv_val[i];
    return "";
}

/* "value|extra1|extra2" -> porzione idx (0 = primo). Buffer statico a rotazione. */
static const char *kv_part(const char *key, int idx) {
    static char bufs[4][160];
    static int rr = 0;
    char *out = bufs[rr = (rr + 1) & 3];
    const char *s = kv(key);
    int cur = 0; size_t o = 0;
    for (const char *p = s; ; p++) {
        if (*p == '|' || *p == 0) {
            if (cur == idx) break;
            cur++; o = 0;
            if (*p == 0) { out[0] = 0; return out; }
            continue;
        }
        if (cur == idx && o < sizeof(bufs[0]) - 1) out[o++] = *p;
    }
    out[o] = 0;
    return out;
}

/* ---------- azioni: mai bloccanti (il loop dei commit non si ferma) ---------- */
static pid_t bg_pid = -1;

static void run_bg(const char *cmd) {
    if (bg_pid > 0) { int st; if (waitpid(bg_pid, &st, WNOHANG) == 0) return; }  /* una per volta */
    pid_t p = fork();
    if (p == 0) {
        setsid();
        int d = open("/dev/null", O_RDWR);
        if (d >= 0) { dup2(d, 0); dup2(d, 1); dup2(d, 2); if (d > 2) close(d); }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (p > 0) bg_pid = p;
}

static char toast[96] = "";
static int toast_until = 0;

static void toast_set(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(toast, sizeof(toast), fmt, ap);
    va_end(ap);
    toast_until = now_ms() + 4000;
    L("avviso: %s", toast);
}

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
