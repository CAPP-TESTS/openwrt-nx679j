
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
