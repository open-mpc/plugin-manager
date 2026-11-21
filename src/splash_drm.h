/* The few DRM/KMS kernel interfaces src/splash.c uses, declared here (as force-shadow does) so the build needs no
 * libdrm or kernel headers. Copied from include/uapi/drm/drm.h and drm_mode.h; stable kernel ABI. The sizes are
 * checked below, and tests/run.sh compares the ioctl numbers with the values from the kernel headers. */
#include <stdint.h>
#include <sys/ioctl.h>

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh, flags, type;
    char name[32];
};
struct drm_mode_card_res {
    uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
    uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
    uint32_t min_width, max_width, min_height, max_height;
};
struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
    struct drm_mode_modeinfo mode;
};
struct drm_mode_get_encoder { uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones; };
struct drm_mode_get_connector {
    uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
    uint32_t count_modes, count_props, count_encoders;
    uint32_t encoder_id, connector_id, connector_type, connector_type_id;
    uint32_t connection, mm_width, mm_height, subpixel, pad;
};
struct drm_mode_fb_cmd { uint32_t fb_id, width, height, pitch, bpp, depth, handle; };
struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_mode_destroy_dumb { uint32_t handle; };

#define DRM_MODE_TYPE_PREFERRED (1 << 3)
#define DRM_IOCTL_SET_MASTER         _IO('d', 0x1e)
#define DRM_IOCTL_DROP_MASTER        _IO('d', 0x1f)
#define DRM_IOCTL_MODE_GETRESOURCES  _IOWR('d', 0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_SETCRTC       _IOWR('d', 0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER    _IOWR('d', 0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR  _IOWR('d', 0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_ADDFB         _IOWR('d', 0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB          _IOWR('d', 0xAF, unsigned int)
#define DRM_IOCTL_MODE_CREATE_DUMB   _IOWR('d', 0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB      _IOWR('d', 0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB  _IOWR('d', 0xB4, struct drm_mode_destroy_dumb)

_Static_assert(sizeof(struct drm_mode_modeinfo) == 68, "modeinfo");
_Static_assert(sizeof(struct drm_mode_card_res) == 64, "card_res");
_Static_assert(sizeof(struct drm_mode_crtc) == 104, "crtc");
_Static_assert(sizeof(struct drm_mode_get_connector) == 80, "get_connector");
_Static_assert(sizeof(struct drm_mode_create_dumb) == 32, "create_dumb");
_Static_assert(sizeof(struct drm_mode_map_dumb) == 16, "map_dumb");
