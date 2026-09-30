// nx679j-panelon — accende il pannello in modo "pulito":
// 1) salva lo stato, 2) DISABLE su tutti i crtc (pulisce lo stato ABL),
// 3) SET del modo esatto (fetched dal kernel) su ogni crtc con fb nuovo,
// 4) dopo, logga lo stato. NIENTE input loop (il killer).
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

struct drm_mode_modeinfo {
	u32 clock;
	u16 hdisplay, hsync_start, hsync_end, htotal, hskew;
	u16 vdisplay, vsync_start, vsync_end, vtotal, vscan;
	u32 vrefresh, flags, type;
	char name[32];
};
struct drm_mode_get_connector {
	u64 encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
	u32 count_modes, count_props, count_encoders;
	u32 encoder_id, connector_id, connector_type, connector_type_id;
	u32 connection, mm_width, mm_height, subpixel, pad;
};
struct drm_mode_create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; };
struct drm_mode_map_dumb { u32 handle, pad; u64 offset; };
struct drm_mode_fb_cmd { u32 fb_id, width, height, pitch, bpp, depth; u32 handle; };
struct drm_mode_crtc {
	u64 set_connectors_ptr;
	u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
	struct drm_mode_modeinfo mode;
};
#define DRM_IOCTL_MODE_GETCONNECTOR _IOWR('d',0xA7,struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_CREATE_DUMB _IOWR('d',0xB2,struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB _IOWR('d',0xB3,struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_ADDFB _IOWR('d',0xAE,struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_SETCRTC _IOWR('d',0xA2,struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETCRTC _IOWR('d',0xA1,struct drm_mode_crtc)
struct drm_clip_rect { unsigned short x1, y1, x2, y2; };
struct drm_mode_fb_dirty_cmd { u32 fb_id, flags, color, num_clips; u64 clips_ptr; };
#define DRM_IOCTL_MODE_DIRTYFB _IOWR('d',0xB1,struct drm_mode_fb_dirty_cmd)

int main(int argc, char** argv) {
	int hold = (argc > 1) ? atoi(argv[1]) : 60;
	int midx = (argc > 2) ? atoi(argv[2]) : 0;
	int solid = (argc > 3) ? atoi(argv[3]) : 0; /* 1=rosso,2=verde,3=blu,4=bianco */
	int disable_first = (argc > 4) ? atoi(argv[4]) : 1;
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) { perror("open card0"); return 1; }

	/* trova conn DSI (type 16) e i suoi modes */
	int conn_id = -1; struct drm_mode_modeinfo modes[8]; int nmodes = 0;
	for (u32 i = 0; i < 128 && conn_id < 0; i++) {
		struct drm_mode_get_connector c; memset(&c, 0, sizeof(c));
		c.connector_id = i;
		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
		if (c.connector_type != 16 || c.connection != 1) continue;
		u32 props[64]; u64 vals[64]; u32 encs[8];
		struct drm_mode_get_connector c2; memset(&c2, 0, sizeof(c2));
		c2.connector_id = i;
		c2.modes_ptr = (u64)(uintptr_t)modes; c2.count_modes = 8;
		c2.props_ptr = (u64)(uintptr_t)props; c2.count_props = 64;
		c2.prop_values_ptr = (u64)(uintptr_t)vals;
		c2.encoders_ptr = (u64)(uintptr_t)encs; c2.count_encoders = 8;
		if (!ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c2)) {
			conn_id = i; nmodes = c2.count_modes;
			printf("DSI conn %d: %d modi\n", conn_id, nmodes);
			for (int m = 0; m < nmodes; m++)
				printf("  mode[%d]: %s %ux%u@%u clk=%u ht=%u vt=%u\n", m,
					modes[m].name, modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh,
					modes[m].clock, modes[m].htotal, modes[m].vtotal);
		}
	}
	if (conn_id < 0) { printf("conn DSI assente\n"); return 2; }

	/* dumb + fb */
	struct drm_mode_create_dumb d; memset(&d, 0, sizeof(d));
	d.width = 1080; d.height = 2400; d.bpp = 32;
	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &d)) { perror("dumb"); return 3; }
	struct drm_mode_fb_cmd fb; memset(&fb, 0, sizeof(fb));
	fb.width = d.width; fb.height = d.height; fb.pitch = d.pitch;
	fb.bpp = 32; fb.depth = 24; fb.handle = d.handle;
	if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb)) { perror("addfb"); return 4; }
	struct drm_mode_map_dumb md; memset(&md, 0, sizeof(md)); md.handle = d.handle;
	ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md);
	u32 *ptr = mmap(0, d.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, md.offset);
	if (ptr == MAP_FAILED) { perror("mmap"); return 5; }
	/* disegna: 4 bande colore brillanti + bordo */
	for (int y = 0; y < 2400; y++)
		for (int x = 0; x < 1080; x++) {
			u32 col;
			if (solid == 1) col = 0xFFFF0000;        /* tutto rosso */
			else if (solid == 2) col = 0xFF00FF00;   /* tutto verde */
			else if (solid == 3) col = 0xFF0000FF;   /* tutto blu */
			else if (solid == 4) col = 0xFFFFFFFF;   /* tutto bianco */
			else if (solid == 5) col = (y < 40) ? 0xFFFF0000 : 0xFF00FF00; /* TEST: striscia rossa in alto + verde */
			else if (solid == 6) col = ((y / 20) % 2) ? 0xFFFFFFFF : 0xFF000000; /* barre orizz 20px */
			else if (y < 600) col = 0xFFFF0000;
			else if (y < 1200) col = 0xFF00FF00;
			else if (y < 1800) col = 0xFF0000FF;
			else col = 0xFFFFFFFF;
			ptr[(y * (d.pitch/4)) + x] = col;
		}
	printf("pattern disegnato (4 bande + bordo nero)\n");

	u32 all_crtcs[] = {152, 214, 223, 232, 241, 0};
	/* FASE 1: disable su tutti (pulisce stato ABL) — salvo se disable_first==0 */
	if (disable_first) {
		for (int k = 0; all_crtcs[k]; k++) {
			struct drm_mode_crtc dis; memset(&dis, 0, sizeof(dis));
			dis.crtc_id = all_crtcs[k];
			int r = ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &dis);
			printf("disable crtc %u: rc=%d errno=%d\n", all_crtcs[k], r, errno);
		}
	} else printf("(salto la fase disable: re-commit diretto)\n");
	/* FASE 2: set su ogni crtc col modo fetched[0] (60cmd) */
	int ok = 0; u32 ok_crtc = 0;
	for (int k = 0; all_crtcs[k] && !ok; k++) {
		u32 cbuf[1] = { (u32)conn_id };
		struct drm_mode_crtc s; memset(&s, 0, sizeof(s));
		s.set_connectors_ptr = (u64)(uintptr_t)cbuf; s.count_connectors = 1;
		s.crtc_id = all_crtcs[k]; s.fb_id = fb.fb_id;
		s.mode = modes[midx < nmodes ? midx : 0]; s.mode_valid = 1;
		int r = ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &s);
		printf("SET su crtc %u: rc=%d errno=%d\n", all_crtcs[k], r, errno);
		if (!r) { ok = 1; ok_crtc = all_crtcs[k]; }
	}
	if (ok) printf("PANEL ON su crtc %u (hold %ds)\n", ok_crtc, hold);
	if (ok) {
		struct drm_clip_rect clip = { 0, 0, 1080, 2340 };
		struct drm_mode_fb_dirty_cmd dty; memset(&dty, 0, sizeof(dty));
		dty.fb_id = fb.fb_id; dty.num_clips = 1;
		dty.clips_ptr = (u64)(uintptr_t)&clip;
		int r = ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &dty);
		printf("dirtyfb full-frame: rc=%d errno=%d\n", r, errno);
	} else { printf("NESSUN crtc accetta\n"); return 6; }
	fflush(stdout);
	int elapsed = 0, ncommit = 0;
	while (elapsed < hold) {
		int sl = (hold - elapsed > 2) ? 2 : (hold - elapsed);
		sleep(sl); elapsed += sl;
		if (ok) {
			/* commit VERO ripetuto: ogni SETCRTC = un kickoff del frame */
			u32 cbuf[1] = { (u32)conn_id };
			struct drm_mode_crtc s2; memset(&s2, 0, sizeof(s2));
			s2.set_connectors_ptr = (u64)(uintptr_t)cbuf; s2.count_connectors = 1;
			s2.crtc_id = ok_crtc; s2.fb_id = fb.fb_id;
			s2.mode = modes[midx < nmodes ? midx : 0]; s2.mode_valid = 1;
			int r2 = ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &s2);
			ncommit++;
			if (ncommit <= 3)
				printf("recommit #%d: rc=%d errno=%d\n", ncommit, r2, errno);
		}
	}
	printf("commits totali: %d\n", ncommit);
	return 0;
}
