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
