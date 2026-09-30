
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
