// nx679j-drmtest — accende il pannello DSI via ioctl DRM diretti (senza libdrm).
// Uso: nx679j-drmtest [secondi]  (default 60)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

typedef uint32_t u32; typedef uint64_t u64;

/* --- drm_mode.h minimali --- */
#define DRM_IOCTL_BASE 'd'
struct drm_mode_card_res {
	u64 fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
	u32 count_fbs, count_crtcs, count_connectors, count_encoders;
	u32 min_width, max_width, min_height, max_height;
};
struct drm_mode_get_connector {
	u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
	u32 count_modes, count_props, count_encoders;
	u32 encoder_id, connector_id, connector_type;
	u32 connector_type_id, connection, mm_width, mm_height, subpixel;
	u32 padding;
};
struct drm_mode_modeinfo {   /* layout UAPI kernel: clock PRIMA, name in fondo! */
	u32 clock;
	uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
	u32 vrefresh, flags, type;
	char name[32];
};
struct drm_mode_get_encoder { u32 encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones; };
struct drm_mode_crtc {
	u64 set_connectors_ptr;
	u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
	struct drm_mode_modeinfo mode;
};
struct drm_mode_create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; };
struct drm_mode_map_dumb { u32 handle, pad; u64 offset; };
struct drm_mode_fb_cmd { u32 fb_id, width, height, pitch, bpp, depth, handle; };

#define DRM_IOCTL_MODE_GETRESOURCES  _IOR('d',0xA0,struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCONNECTOR  _IOWR('d',0xA7,struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETENCODER    _IOWR('d',0xA6,struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCRTC       _IOWR('d',0xA1,struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC       _IOWR('d',0xA2,struct drm_mode_crtc)
#define DRM_IOCTL_MODE_CREATE_DUMB   _IOWR('d',0xB2,struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB      _IOWR('d',0xB3,struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_ADDFB         _IOWR('d',0xAE,struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_DESTROY_DUMB  _IOWR('d',0xB4,struct drm_mode_create_dumb)

#define DRM_MODE_CONNECTED 1
#define DRM_MODE_TYPE_PREFERRED (1<<3)

/* ---- touch painter helpers ---- */
static u32 *FB; static int FW, FH, FPITCH;
struct input_event { long tv_sec, tv_usec; uint16_t type, code; int32_t value; };
#define DRM_IOCTL_MODE_DIRTYFB _IOWR('d',0xB1,struct drm_mode_fb_dirty_cmd)
struct drm_mode_fb_dirty_cmd { u32 fb_id, flags, color, num_clips; u64 clips_ptr; };
static void putpx(int x,int y,u32 c){ if(x<0||y<0||x>=FW||y>=FH)return; FB[(y*(FPITCH/4))+x]=c; }
static void blob(int cx,int cy,u32 c,int r){ for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++) if(dx*dx+dy*dy<=r*r) putpx(cx+dx,cy+dy,c); }
static void line(int x0,int y0,int x1,int y1,u32 c){ int dx=abs(x1-x0), dy=abs(y1-y0), sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx-dy; for(int k=0;k<4000;k++){ blob(x0,y0,c,3); if(x0==x1&&y0==y1)break; int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;} } }

int main(int argc, char **argv) {
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) { perror("open card0"); return 1; }
	int hold = (argc > 1) ? atoi(argv[1]) : 60;

	struct drm_mode_card_res res; memset(&res, 0, sizeof(res));
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("getresources"); return 1; }
	printf("resources: %u conn, %u crtc (%ux%u..%ux%u)\n", res.count_connectors, res.count_crtcs,
	       res.min_width, res.min_height, res.max_width, res.max_height);

	u32 conns[16]={0}, crtcs[16]={0}, encs[16]={0};
	res.connector_id_ptr = (u64)(uintptr_t)conns; res.count_connectors = 16;
	res.crtc_id_ptr = (u64)(uintptr_t)crtcs; res.count_crtcs = 16;
	res.encoder_id_ptr = (u64)(uintptr_t)encs; res.count_encoders = 16;
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("getresources2"); return 1; }
	printf("connectors: %u, crtcs: %u\n", res.count_connectors, res.count_crtcs);

	int conn_id = -1; int need_crtc_mode = 0; int enc_id = 0;
	struct drm_mode_modeinfo fmodes[8]; int nfmodes = 0;
	struct drm_mode_modeinfo best; memset(&best,0,sizeof(best));
	/* Scan 0..255: la DSI e' il connector type=15. Gli array di GETRESOURCES
	 * non sono affidabili su questo kernel vendor (non riempie gli id). */
	for (u32 i = 0; i < 256 && conn_id < 0; i++) {
		struct drm_mode_get_connector c; memset(&c, 0, sizeof(c));
		c.connector_id = i;
		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
		if (c.connector_type != 16) continue;   /* 16=DSI (15 era VIRTUAL!) */
		printf("DSI conn id=%u: connected=%u modes=%u encoder=%u\n", i, c.connection, c.count_modes, c.encoder_id);
		if (c.connection != 1) continue;
		conn_id = i; enc_id = c.encoder_id;
		struct drm_mode_modeinfo modes[40]; memset(modes,0,sizeof(modes));
		u32 props[80]; u64 propvals[80]; u32 encs2[8];
		memset(props,0,sizeof(props)); memset(propvals,0,sizeof(propvals)); memset(encs2,0,sizeof(encs2));
		int rc = -1;
		{
			/* il kernel copia modes E props E encoders: TUTTI i pointer devono essere validi */
			struct drm_mode_get_connector c2; memset(&c2, 0, sizeof(c2));
			c2.connector_id = i;
			c2.modes_ptr = (u64)(uintptr_t)modes; c2.count_modes = 40;
			c2.props_ptr = (u64)(uintptr_t)props; c2.count_props = 80;
			c2.prop_values_ptr = (u64)(uintptr_t)propvals;
			c2.encoders_ptr = (u64)(uintptr_t)encs2; c2.count_encoders = 8;
			rc = ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c2);
			printf("  full fetch rc=%d errno=%d modes=%u props=%u enc=%u\n", rc, errno, c2.count_modes, c2.count_props, c2.count_encoders);
			if (!rc) c = c2;
		}
		if (!rc && c.count_modes >= 1) {
			for (u32 m = 0; m < c.count_modes && m < 40; m++)
				printf("  mode[%u]: %s %ux%u@%u\n", m, modes[m].name, modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh);
			best = modes[0];
			for (u32 m = 0; m < c.count_modes && m < 40; m++) if (modes[m].type & DRM_MODE_TYPE_PREFERRED) { best = modes[m]; break; }
			for (u32 m = 0; m < c.count_modes && m < 8; m++) fmodes[nfmodes++] = modes[m];
		} else {
			printf("  (modes non leggibili rc=%d, uso il CRTC corrente)\n", rc);
			need_crtc_mode = 1;
		}
	}
	if (conn_id < 0) { fprintf(stderr, "nessun connettore connected\n"); return 2; }

	int crtc_id = -1;

	u32 all_crtcs[16]; int n_crtcs = 0;
	for (u32 ci = 0; ci < 256 && n_crtcs < 16; ci++) {
		struct drm_mode_crtc cur; memset(&cur, 0, sizeof(cur)); cur.crtc_id = ci;
		if (!ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &cur)) all_crtcs[n_crtcs++] = ci;
	}
	{
		int n_enc = 0;
		for (u32 ei = 0; ei < 256 && n_enc < 12; ei++) {
			struct drm_mode_get_encoder e; memset(&e, 0, sizeof(e)); e.encoder_id = ei;
			if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e)) continue;
			printf("encoder %u: type=%u crtc=%u possible_crtcs=0x%x possible_clones=0x%x\n", ei, e.encoder_type, e.crtc_id, e.possible_crtcs, e.possible_clones);
			n_enc++;
		}
	}
	printf("crtc validi: %d [", n_crtcs);
	for (int k = 0; k < n_crtcs; k++) printf("%u ", all_crtcs[k]);
	printf("]\n"); fflush(stdout);
	/* modalita' R6130 1080x2400@90 dal device tree (il bootloader non configura
	 * i CRTC e gli array DRM di questo kernel vendor non sono leggibili). */
	struct drm_mode_modeinfo r6130; memset(&r6130, 0, sizeof(r6130));
	strcpy(r6130.name, "1080x2340x90cmd");
	r6130.clock = 242352;
	r6130.hdisplay = 1080; r6130.hsync_start = 1100; r6130.hsync_end = 1102; r6130.htotal = 1122;
	r6130.vdisplay = 2340; r6130.vsync_start = 2380; r6130.vsync_end = 2382; r6130.vtotal = 2400;
	r6130.vrefresh = 90; r6130.type = 8;  /* PREFERRED */
	if (need_crtc_mode || !best.hdisplay) {
		for (u32 ci = 0; ci < 16 && crtc_id < 0; ci++) {
			struct drm_mode_crtc cur; memset(&cur, 0, sizeof(cur)); cur.crtc_id = ci;
			if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &cur)) continue;
			if (!cur.mode_valid || !cur.mode.hdisplay) continue;
			best = cur.mode; crtc_id = ci;
			printf("modo dal CRTC %u: %s %ux%u@%u (fb attuale %u)\n", ci, best.name, best.hdisplay, best.vdisplay, best.vrefresh, cur.fb_id);
		}
		if (crtc_id < 0) {
			/* nessun crtc configurato: prendi il primo crtc che risponde e usa il
			 * modo R6130 dal device tree */
			for (u32 ci = 0; ci < 256 && crtc_id < 0; ci++) {
				struct drm_mode_crtc cur; memset(&cur, 0, sizeof(cur)); cur.crtc_id = ci;
				int rc = ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &cur);
				if (ci < 8 || !rc) printf("crtc %u: rc=%d errno=%d\n", ci, rc, errno);
				if (!rc) { crtc_id = ci; best = r6130;
					printf("crtc %u pronto -> modo R6130 dal DT\n", ci); }
			}
		}
		if (crtc_id < 0) { fprintf(stderr, "nessun CRTC utilizzabile\n"); return 8; }
	} else {
		struct drm_mode_get_encoder enc; memset(&enc, 0, sizeof(enc)); enc.encoder_id = enc_id;
		ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc);
		crtc_id = (int)enc.crtc_id;
		if (crtc_id < 0) { for (u32 ci = 0; ci < 16 && crtc_id < 0; ci++) { struct drm_mode_crtc cur; memset(&cur,0,sizeof(cur)); cur.crtc_id = ci; if (!ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &cur) && cur.mode_valid) crtc_id = ci; } }
	}
	printf("scelto: conn %d, crtc %d, mode %s %ux%u\n", conn_id, crtc_id, best.name, best.hdisplay, best.vdisplay);

	struct drm_mode_create_dumb dumb; memset(&dumb, 0, sizeof(dumb));
	dumb.width = 1080; dumb.height = 2400; dumb.bpp = 32;
	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb)) { perror("create_dumb"); return 3; }
	printf("dumb: handle=%u pitch=%u size=%llu\n", dumb.handle, dumb.pitch, (unsigned long long)dumb.size); fflush(stdout);
	struct drm_mode_fb_cmd fb; memset(&fb, 0, sizeof(fb));
	fb.width = dumb.width; fb.height = dumb.height; fb.pitch = dumb.pitch; fb.bpp = 32; fb.depth = 24; fb.handle = dumb.handle;
	printf("addfb...\n"); fflush(stdout);
	if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb)) { perror("addfb"); return 4; }
	printf("addfb ok fb_id=%u\n", fb.fb_id); fflush(stdout);
	struct drm_mode_map_dumb map; memset(&map, 0, sizeof(map)); map.handle = dumb.handle;
	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) { perror("map_dumb"); return 5; }
	printf("map ok offset=%llu, mmap...\n", (unsigned long long)map.offset); fflush(stdout);
	uint8_t *ptr = mmap(0, dumb.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, map.offset);
	if (ptr == MAP_FAILED) { perror("mmap"); return 6; }
	printf("mmap ok, fill...\n"); fflush(stdout);
	{
		u32 *px; u32 W = dumb.width, H = dumb.height;
		for (u32 y = 0; y < H; y++) {
			u32 band = y * 4 / H;
			u32 col = (band==0)?0x00FF0000:(band==1)?0x0000FF00:(band==2)?0x000000FF:0x00FFFFFF;
			px = (u32 *)(ptr + y * dumb.pitch);
			for (u32 x = 0; x < W; x++) px[x] = col;
		}
	}
	printf("fill done, setcrtc...\n"); fflush(stdout);
	struct drm_mode_crtc set; memset(&set, 0, sizeof(set));
	set.crtc_id = (u32)crtc_id;
	struct drm_mode_modeinfo mv[12]; int nv = 0;
	for (int q = 0; q < nfmodes && nv < 11; q++) mv[nv++] = fmodes[q];
	int wl[6] = {1080,1080,1080,1080,1080,1080};
	int hl[6] = {2340,2400,2340,2400,2340,2400};
	int zl[6] = {90,90,60,60,120,120};
	for (int v = 0; v < 6; v++) {
		struct drm_mode_modeinfo *m = &mv[nv]; memset(m, 0, sizeof(*m));
		snprintf(m->name, 32, "%dx%dx%dcmd", wl[v], hl[v], zl[v]);
		m->hdisplay = wl[v]; m->hsync_start = wl[v]+20; m->hsync_end = wl[v]+22; m->htotal = wl[v]+42;
		m->vdisplay = hl[v]; m->vsync_start = hl[v]+40; m->vsync_end = hl[v]+42; m->vtotal = hl[v]+60;
		m->vrefresh = zl[v];
		m->clock = (u32)(((unsigned long long)m->htotal * m->vtotal * zl[v]) / 1000);
		m->type = 8;
		nv++;
	}
	mv[nv++] = best;
	int ok = 0;
	for (int v = 0; v < nv && !ok; v++) {
		u32 connbuf[1] = { (u32)conn_id };
		set.set_connectors_ptr = (u64)(uintptr_t)connbuf; set.count_connectors = 1;
		set.fb_id = fb.fb_id; set.mode = mv[v]; set.mode_valid = 1;
		for (int k = 0; k < n_crtcs && !ok; k++) {
			set.crtc_id = all_crtcs[k];
			if (!ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &set)) { ok = 1; crtc_id = all_crtcs[k]; best = mv[v]; }
			else if (v == 0) printf("  crtc %u: errno=%d\n", all_crtcs[k], errno);
		}
		printf("variante %d %s: %s\n", v, mv[v].name, ok ? "OK!" : "rifiutata"); fflush(stdout);
	}
	if (!ok) { printf("NESSUNA variante accettata: serve ATOMIC\n"); fflush(stdout); return 7; }
	set.crtc_id = (u32)crtc_id;
	printf("SETCRTC OK: %s %ux%u@%u crtc %d\n", best.name, best.hdisplay, best.vdisplay, best.vrefresh, crtc_id); fflush(stdout);
	/* sfondo scuro + painter */
	FB = (u32*)ptr; FW = dumb.width; FH = dumb.height; FPITCH = dumb.pitch;
	for (int y=0;y<FH;y++) for (int x=0;x<FW;x++) FB[y*(FPITCH/4)+x] = 0x00101828;
	struct drm_mode_fb_dirty_cmd dfc; memset(&dfc,0,sizeof(dfc)); dfc.fb_id = fb.fb_id;
	ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &dfc);
	printf("TOUCH PAINT: disegna col dito! (input event0)\n"); fflush(stdout);
	int ifd = open("/dev/input/event0", O_RDONLY|O_NONBLOCK);
	if (ifd < 0) { perror("open input event0"); return 8; }
	int x=-1,y=-1,down=0,lx=-1,ly=-1; int npunti=0;
	for (;;) {
		struct input_event ev;
		while (read(ifd,&ev,sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if (ev.type == 3) { if (ev.code == 53 || ev.code == 0) x = ev.value; if (ev.code == 54 || ev.code == 1) y = ev.value; }
			else if (ev.type == 1 && ev.code == 330) down = ev.value;
			else if (ev.type == 0 && ev.code == 0) {
				if (down && x >= 0 && y >= 0) {
					u32 col = 0x00FF3030 + ((npunti*131) & 0xFF)*(1u<<8); /* tinta variabile */
					blob(x,y,col,6);
					if (lx >= 0) line(lx,ly,x,y,col);
					lx = x; ly = y; npunti++;
					ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &dfc);
				}
				if (!down) { lx = ly = -1; }
			}
		}
		usleep(3000);
	}
	return 0;
}
