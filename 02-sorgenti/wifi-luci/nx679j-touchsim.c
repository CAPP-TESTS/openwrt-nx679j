// nx679j-touchsim — SELF-TEST completo della catena touch→disegno.
// Un solo processo (un solo client DRM!): setcrtc + uinput + inject + read + paint.
// Crea un touchscreen VIRTUALE (uinput), inietta una X gigante, LEGGE i propri
// eventi dal device virtuale e disegna la X sul framebuffer. Prova che la catena
// input->eventi->disegno funziona end-to-end. Il touch FISICO resta da provare.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <linux/uinput.h>
#include <linux/input.h>

/* ---- mini DRM (come touchpaint) ---- */
#define DRM_IOCTL_MODE_GETRESOURCES  _IOR('d',0xA0,struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCONNECTOR  _IOWR('d',0xA7,struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETENCODER    _IOWR('d',0xA6,struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCRTC       _IOWR('d',0xA1,struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC       _IOWR('d',0xA2,struct drm_mode_crtc)
#define DRM_IOCTL_MODE_CREATE_DUMB   _IOWR('d',0xB2,struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB      _IOWR('d',0xB3,struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_ADDFB         _IOWR('d',0xAE,struct drm_mode_fb_cmd)
#define DRM_MODE_CONNECTED 1
#define DRM_MODE_TYPE_PREFERRED (1<<3)

struct drm_mode_modeinfo {
	unsigned int clock; unsigned short hdisplay,hsync_start,hsync_end,htotal,hskew;
	unsigned short vdisplay,vsync_start,vsync_end,vtotal,vscan; unsigned int vrefresh;
	unsigned int flags; unsigned int type; char name[32];
};
struct drm_mode_get_connector {
	unsigned long long encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
	unsigned int count_modes, count_props, count_encoders;
	unsigned int encoder_id, connector_id, connector_type;
	unsigned int connector_type_id, connection, mm_width, mm_height, subpixel;
	unsigned int padding;
};
struct drm_mode_get_encoder { unsigned int encoder_id,encoder_type; unsigned int crtc_id; unsigned int possible_crtcs,possible_clones; };
struct drm_mode_crtc { unsigned long long set_connectors_ptr; unsigned int count_connectors; unsigned int crtc_id; unsigned int fb_id; unsigned int x,y; unsigned int gamma_size; unsigned int mode_valid; struct drm_mode_modeinfo mode; };
struct drm_mode_create_dumb { unsigned int height,width,bpp,flags; unsigned int handle,pitch; unsigned long long size; };
struct drm_mode_map_dumb { unsigned int handle; unsigned int pad; unsigned long long offset; };
struct drm_mode_fb_cmd { unsigned int fb_id,width,height,pitch,bpp,depth,handle; };
struct drm_mode_card_res { unsigned long long fb_id_ptr,crtc_id_ptr,connector_id_ptr,encoder_id_ptr; unsigned int count_fbs,count_crtcs,count_connectors,count_encoders; unsigned int min_w,max_w,min_h,max_h; };

#define W 1080
#define H 2400

static unsigned int *fb_mem;
static int fb_fd;
static unsigned int pitch_px;

static void paint(int x, int y, unsigned int col, int sz)
{
	for (int dy = -sz; dy <= sz; dy++) {
		for (int dx = -sz; dx <= sz; dx++) {
			int px = x + dx, py = y + dy;
			if (px >= 0 && px < W && py >= 0 && py < H)
				fb_mem[(unsigned long)py * pitch_px + px] = col;
		}
	}
}
static void line(int x0,int y0,int x1,int y1,unsigned int col)
{
	int dx = abs(x1-x0), sx = x0<x1?1:-1;
	int dy = -abs(y1-y0), sy = y0<y1?1:-1;
	int err = dx+dy;
	while (1) { paint(x0,y0,col,2);
		if (x0==x1 && y0==y1) break;
		int e2 = 2*err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; } }
}

int main(void)
{
	/* ===== 1) DRM: setcrtc su crtc 152 ===== */
	fb_fd = open("/dev/dri/card0", O_RDWR);
	if (fb_fd < 0) { perror("open card0"); return 1; }

	struct drm_mode_card_res res; memset(&res,0,sizeof(res));
	unsigned int crtcs[16]; res.crtc_id_ptr = (unsigned long long)(unsigned long)crtcs; res.count_crtcs = 16;
	if (ioctl(fb_fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("getresources"); return 2; }
	int crtc_id = 152;
	for (unsigned i = 0; i < res.count_crtcs && i < 16; i++) printf("crtc[%u]=%u ", i, crtcs[i]);
	printf("\n");

	unsigned int conn_id = 0;
	struct drm_mode_modeinfo modes[16];
	memset(modes, 0, sizeof(modes));
	for (unsigned int ci = 0; ci < 256; ci++) {
		struct drm_mode_get_connector cn; memset(&cn,0,sizeof(cn)); cn.connector_id = ci;
		if (ioctl(fb_fd, DRM_IOCTL_MODE_GETCONNECTOR, &cn)) continue;
		if (cn.connection != 1 || cn.count_modes == 0) continue;
		struct drm_mode_modeinfo mm[16]; memset(mm,0,sizeof(mm));
		unsigned int pp[40]; unsigned long long pv[40]; unsigned int ee[8];
		memset(pp,0,sizeof(pp)); memset(pv,0,sizeof(pv)); memset(ee,0,sizeof(ee));
		struct drm_mode_get_connector c2; memset(&c2,0,sizeof(c2));
		c2.connector_id = ci;
		c2.modes_ptr = (unsigned long long)(unsigned long)mm; c2.count_modes = 16;
		c2.props_ptr = (unsigned long long)(unsigned long)pp; c2.count_props = 40;
		c2.prop_values_ptr = (unsigned long long)(unsigned long)pv;
		c2.encoders_ptr = (unsigned long long)(unsigned long)ee; c2.count_encoders = 8;
		if (ioctl(fb_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c2)) continue;
		printf("CONN %u: type=%u modes=%u enc=%u conn=%u\n", ci, c2.connector_type, c2.count_modes, c2.encoder_id, c2.connection);
		if (c2.connector_type == 16 && c2.connection == 1) { conn_id = ci; memcpy(modes, mm, sizeof(modes)); break; }
	}
	if (!conn_id) { printf("nessun conn DSI\n"); return 2; }
	printf("uso conn %u, mode[0]=%s\n", conn_id, modes[0].name);

	struct drm_mode_create_dumb dumb; memset(&dumb,0,sizeof(dumb));
	dumb.width = W; dumb.height = H; dumb.bpp = 32;
	if (ioctl(fb_fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb)) { perror("dumb"); return 3; }
	struct drm_mode_fb_cmd fbc; memset(&fbc,0,sizeof(fbc));
	fbc.width = W; fbc.height = H; fbc.pitch = dumb.pitch; fbc.bpp = 32; fbc.depth = 24; fbc.handle = dumb.handle;
	if (ioctl(fb_fd, DRM_IOCTL_MODE_ADDFB, &fbc)) { perror("addfb"); return 3; }
	struct drm_mode_map_dumb map; memset(&map,0,sizeof(map)); map.handle = dumb.handle;
	if (ioctl(fb_fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) { perror("mapdumb"); return 3; }
	fb_mem = mmap(NULL, dumb.size, PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, map.offset);
	if (fb_mem == MAP_FAILED) { perror("mmap"); return 3; }
	pitch_px = dumb.pitch / 4;

	/* riempi di NERO */
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			fb_mem[(unsigned long)y*pitch_px + x] = 0x00000000;

	/* ===== 2) uinput: touchscreen virtuale (PRIMA del setcrtc: il primo frame = la X!) ===== */
	int ufd = open("/dev/uinput", O_WRONLY|O_NONBLOCK);
	if (ufd < 0) { perror("open uinput"); return 5; }
	ioctl(ufd, UI_SET_EVBIT, EV_ABS);
	ioctl(ufd, UI_SET_EVBIT, EV_KEY);
	ioctl(ufd, UI_SET_EVBIT, EV_SYN);
	ioctl(ufd, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(ufd, UI_SET_ABSBIT, ABS_MT_SLOT);
	ioctl(ufd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
	ioctl(ufd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
	ioctl(ufd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
	struct uinput_abs_setup abs; memset(&abs,0,sizeof(abs));
	abs.code = ABS_MT_POSITION_X; abs.absinfo.minimum = 0; abs.absinfo.maximum = W-1;
	ioctl(ufd, UI_ABS_SETUP, &abs);
	abs.code = ABS_MT_POSITION_Y; abs.absinfo.minimum = 0; abs.absinfo.maximum = H-1;
	ioctl(ufd, UI_ABS_SETUP, &abs);
	struct uinput_setup usetup; memset(&usetup,0,sizeof(usetup));
	usetup.id.bustype = BUS_VIRTUAL; usetup.id.vendor = 0x1; usetup.id.product = 0x1;
	strcpy(usetup.name, "nx679j-simts");
	if (ioctl(ufd, UI_DEV_SETUP, &usetup)) { perror("devsetup"); return 5; }
	if (ioctl(ufd, UI_DEV_CREATE)) { perror("devcreate"); return 5; }
	printf("uinput device creato, cerco l'event node...\n"); fflush(stdout);
	sleep(3);

	/* trova l'event node del device virtuale */
	char evpath[64] = ""; 
	DIR *d = opendir("/sys/class/input");
	struct dirent *de;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5)) continue;
		char p[128], buf[64];
		snprintf(p, sizeof(p), "/sys/class/input/%s/device/name", de->d_name);
		int f = open(p, O_RDONLY);
		if (f < 0) continue;
		int n = read(f, buf, sizeof(buf)-1); close(f);
		if (n > 0) { buf[n] = 0;
			if (strstr(buf, "nx679j-simts")) { snprintf(evpath, sizeof(evpath), "/dev/input/%s", de->d_name); break; } }
	}
	closedir(d);
	if (!evpath[0]) { printf("event node del sim non trovato\n"); return 6; }
	printf("event node: %s\n", evpath); fflush(stdout);
	int efd = open(evpath, O_RDONLY|O_NONBLOCK);
	if (efd < 0) {
		/* il nodo non esiste (devtmpfs non lo crea): mknod event[N] = c 13 (64+N) */
		int n = atoi(evpath + strlen("/dev/input/event"));
		if (!mknod(evpath, S_IFCHR | 0666, makedev(13, 64 + n))) {
			printf("mknod %s (13,%d) ok\n", evpath, 64 + n); fflush(stdout);
			efd = open(evpath, O_RDONLY|O_NONBLOCK);
		}
	}
	if (efd < 0) { perror("open evnode"); return 6; }

	/* ===== 3) inject: disegna una X gigante ===== */
	printf("inietto la X (10 segmenti)...\n"); fflush(stdout);
	int nev = 0;
	int segs[10][4] = {
		{80,80, 1000,2320}, {1000,80, 80,2320},           /* X grande */
		{80,80, 1000,80}, {1000,80, 1000,2320},           /* bordo */
		{1000,2320, 80,2320}, {80,2320, 80,80},
		{540,200, 540,2200}, {200,1200, 880,1200},        /* croce */
		{300,500, 800,1900}, {800,500, 300,1900}          /* X interna */
	};
	for (int s = 0; s < 10; s++) {
		int x0=segs[s][0], y0=segs[s][1], x1=segs[s][2], y1=segs[s][3];
		struct input_event ev;
		memset(&ev,0,sizeof(ev)); ev.type = EV_ABS; ev.code = ABS_MT_TRACKING_ID; ev.value = s+1;
		write(ufd, &ev, sizeof(ev));
		memset(&ev,0,sizeof(ev)); ev.type = EV_KEY; ev.code = BTN_TOUCH; ev.value = 1;
		write(ufd, &ev, sizeof(ev));
		int steps = 120;
		for (int i = 0; i <= steps; i++) {
			int x = x0 + (x1-x0)*i/steps, y = y0 + (y1-y0)*i/steps;
			memset(&ev,0,sizeof(ev)); ev.type = EV_ABS; ev.code = ABS_MT_POSITION_X; ev.value = x;
			write(ufd, &ev, sizeof(ev));
			memset(&ev,0,sizeof(ev)); ev.type = EV_ABS; ev.code = ABS_MT_POSITION_Y; ev.value = y;
			write(ufd, &ev, sizeof(ev));
			memset(&ev,0,sizeof(ev)); ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
			write(ufd, &ev, sizeof(ev));
			/* leggi i MIEI eventi e disegna! */
			struct input_event re;
			while (read(efd, &re, sizeof(re)) == sizeof(re)) {
				nev++;
				if (re.type == EV_ABS && (re.code == ABS_MT_POSITION_X || re.code == ABS_MT_POSITION_Y)) {
					static int lx = -1, ly = -1;
					if (re.code == ABS_MT_POSITION_X) lx = re.value;
					else ly = re.value;
					if (lx >= 0 && ly >= 0) { paint(lx, ly, 0x00FFFFFF, 2); lx = ly = -1; }
				}
			}
			usleep(3000);
		}
		memset(&ev,0,sizeof(ev)); ev.type = EV_KEY; ev.code = BTN_TOUCH; ev.value = 0;
		write(ufd, &ev, sizeof(ev));
		memset(&ev,0,sizeof(ev)); ev.type = EV_ABS; ev.code = ABS_MT_TRACKING_ID; ev.value = -1;
		write(ufd, &ev, sizeof(ev));
		memset(&ev,0,sizeof(ev)); ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
		write(ufd, &ev, sizeof(ev));
	}
	printf("iniezione finita: %d eventi letti (round-trip input OK)\n", nev);

	/* ===== 4) SETCRTC: adesso il primo frame pushato = LA X ===== */
	struct drm_mode_crtc set; memset(&set,0,sizeof(set));
	unsigned int cbuf[1] = { conn_id };
	set.set_connectors_ptr = (unsigned long long)(unsigned long)cbuf; set.count_connectors = 1;
	set.fb_id = fbc.fb_id; set.crtc_id = crtc_id;
	set.mode = modes[0]; set.mode_valid = 1;
	printf("SETCRTC: %s %ux%u@%u\n", modes[0].name, modes[0].hdisplay, modes[0].vdisplay, modes[0].vrefresh); fflush(stdout);
	if (ioctl(fb_fd, DRM_IOCTL_MODE_SETCRTC, &set)) { perror("setcrtc"); return 4; }
	printf("SETCRTC OK — LA X DOVREBBE ESSERE A SCHERMO!\n"); fflush(stdout);

	/* hold con refresh: flip periodico (tiene la pipeline viva) */
	int k = 0;
	while (1) { sleep(2); k++;
		if (k % 15 == 0) printf("hold %ds\n", k*2); fflush(stdout); }
	return 0;
}
