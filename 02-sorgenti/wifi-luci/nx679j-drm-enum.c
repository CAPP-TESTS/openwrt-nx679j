// nx679j-drm-enum — enumerazione DRM read-only (niente SETCRTC: sicuro).
// Stampa: connettori (tipo/id/stato/modi ESATTI), encoder, crtc, proprietà con nomi.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

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
struct drm_mode_card_res {
	u64 fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
	u32 count_fbs, count_crtcs, count_connectors, count_encoders;
	u32 min_width, max_width, min_height, max_height;
};
struct drm_mode_get_encoder { u32 encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones; };
struct drm_mode_crtc {
	u64 set_connectors_ptr;
	u32 count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
	struct drm_mode_modeinfo mode;
};
struct drm_mode_get_property {
	u64 values_ptr, enum_blob_ptr;
	u32 prop_id, flags;
	char name[32];
	u32 count_values, count_enum_blobs;
};
#define DRM_IOCTL_MODE_GETRESOURCES _IOWR('d',0xA0,struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCONNECTOR _IOWR('d',0xA7,struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETENCODER   _IOWR('d',0xA6,struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCRTC      _IOWR('d',0xA1,struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETPROPERTY  _IOWR('d',0xAA,struct drm_mode_get_property)

int main(void) {
	int fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) { perror("open"); return 1; }
	u32 conns[32]={0}, crtcs[32]={0}, encs[32]={0};
	struct drm_mode_card_res res; memset(&res,0,sizeof(res));
	res.connector_id_ptr=(u64)(uintptr_t)conns; res.count_connectors=32;
	res.crtc_id_ptr=(u64)(uintptr_t)crtcs; res.count_crtcs=32;
	res.encoder_id_ptr=(u64)(uintptr_t)encs; res.count_encoders=32;
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) perror("getresources");
	printf("== resources: conn=%u crtc=%u enc=%u ==\n", res.count_connectors, res.count_crtcs, res.count_encoders);
	for (u32 i=0;i<res.count_connectors && i<32;i++) printf("  conn[%u]=%u\n", i, conns[i]);
	for (u32 i=0;i<res.count_crtcs && i<32;i++) printf("  crtc[%u]=%u\n", i, crtcs[i]);
	for (u32 i=0;i<res.count_encoders && i<32;i++) printf("  enc[%u]=%u\n", i, encs[i]);

	for (u32 i=0;i<256;i++) {
		struct drm_mode_get_connector c; memset(&c,0,sizeof(c));
		c.connector_id=i;
		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR,&c)) continue;
		if (!c.connector_type && !c.connection) continue;
		printf("CONN %u: type=%u type_id=%u conn=%u enc=%u modes=%u props=%u\n",
		       i, c.connector_type, c.connector_type_id, c.connection, c.encoder_id, c.count_modes, c.count_props);
		if (c.connection == 1 && c.count_modes > 0) {
			struct drm_mode_modeinfo modes[48]; memset(modes,0,sizeof(modes));
			u32 props[64]; u64 pvals[64]; u32 ee[8];
			memset(props,0,sizeof(props)); memset(pvals,0,sizeof(pvals)); memset(ee,0,sizeof(ee));
			struct drm_mode_get_connector c2; memset(&c2,0,sizeof(c2));
			c2.connector_id=i;
			c2.modes_ptr=(u64)(uintptr_t)modes; c2.count_modes=48;
			c2.props_ptr=(u64)(uintptr_t)props; c2.count_props=64;
			c2.prop_values_ptr=(u64)(uintptr_t)pvals;
			c2.encoders_ptr=(u64)(uintptr_t)ee; c2.count_encoders=8;
			if (!ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR,&c2)) {
				printf("  MODI (%u):\n", c2.count_modes);
				for (u32 m=0;m<c2.count_modes && m<48;m++)
					printf("    m[%u] '%s' %ux%u@%u clk=%u ht=%u vt=%u fl=0x%x t=%u\n",
					       m, modes[m].name, modes[m].hdisplay, modes[m].vdisplay, modes[m].vrefresh,
					       modes[m].clock, modes[m].htotal, modes[m].vtotal, modes[m].flags, modes[m].type);
				printf("  PROPS id:");
				for (u32 p=0;p<c2.count_props && p<24;p++) {
					printf(" %u", props[p]);
					struct drm_mode_get_property pr; memset(&pr,0,sizeof(pr));
					pr.prop_id = props[p];
					if (!ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY,&pr))
						printf("(%s)", pr.name);
				}
				printf("\n");
			} else perror("  fetch completo");
		}
	}
	for (u32 e=0;e<256;e++) {
		struct drm_mode_get_encoder en; memset(&en,0,sizeof(en)); en.encoder_id=e;
		if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER,&en)) continue;
		if (!en.encoder_type) continue;
		printf("ENC %u: type=%u crtc=%u poss_crtcs=0x%x\n", e, en.encoder_type, en.crtc_id, en.possible_crtcs);
	}
	for (u32 ci=0;ci<256;ci++) {
		struct drm_mode_crtc cr; memset(&cr,0,sizeof(cr)); cr.crtc_id=ci;
		if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC,&cr)) continue;
		printf("CRTC %u: valid=%u fb=%u mode='%s' %ux%u\n", ci, cr.mode_valid, cr.fb_id, cr.mode.name, cr.mode.hdisplay, cr.mode.vdisplay);
	}
	printf("== fine enum ==\n");
	return 0;
}
