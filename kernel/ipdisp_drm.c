/* IP Display Driver - DRM Implementation
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include "ipdisp.h"

/* Display modes */
static const struct drm_display_mode default_modes[] = {
    /* 1920x1080@60 */
    { DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
               2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
               DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
    /* 1680x1050@60 */
    { DRM_MODE("1680x1050", DRM_MODE_TYPE_DRIVER, 146250, 1680, 1784,
               1960, 2240, 0, 1050, 1053, 1059, 1089, 0,
               DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC) },
    /* 1280x1024@60 */
    { DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 108000, 1280, 1328,
               1440, 1688, 0, 1024, 1025, 1028, 1066, 0,
               DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
    /* 1024x768@60 */
    { DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
               1184, 1344, 0, 768, 771, 777, 806, 0,
               DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
};

/* Connector helper functions */
static int ipdisp_connector_get_modes(struct drm_connector *connector)
{
    struct ipdisp_device *idev = to_ipdisp_device(connector->dev);
    struct drm_display_mode *mode;
    int i, count = 0;
    
    ipdisp_debug("Getting connector modes\n");
    
    /* Add default modes */
    for (i = 0; i < ARRAY_SIZE(default_modes); i++) {
        mode = drm_mode_duplicate(connector->dev, &default_modes[i]);
        if (mode) {
            /* Mark current resolution as preferred */
            if (mode->hdisplay == idev->width && 
                mode->vdisplay == idev->height) {
                mode->type |= DRM_MODE_TYPE_PREFERRED;
            }
            drm_mode_probed_add(connector, mode);
            count++;
        }
    }
    
    /* Add custom mode if not in defaults */
    bool found = false;
    for (i = 0; i < ARRAY_SIZE(default_modes); i++) {
        if (default_modes[i].hdisplay == idev->width &&
            default_modes[i].vdisplay == idev->height) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        mode = drm_cvt_mode(connector->dev, idev->width, idev->height,
                           60, false, false, false);
        if (mode) {
            mode->type |= DRM_MODE_TYPE_PREFERRED;
            drm_mode_probed_add(connector, mode);
            count++;
        }
    }
    
    ipdisp_debug("Added %d modes\n", count);
    return count;
}

static enum drm_mode_status 
ipdisp_connector_mode_valid(struct drm_connector *connector,
                           struct drm_display_mode *mode)
{
    struct ipdisp_device *idev = to_ipdisp_device(connector->dev);
    
    ipdisp_debug("Validating mode %dx%d\n", mode->hdisplay, mode->vdisplay);
    
    /* Check if mode is supported */
    if (mode->hdisplay > 7680 || mode->vdisplay > 4320) {
        ipdisp_debug("Mode too large\n");
        return MODE_BAD;
    }
    
    if (mode->hdisplay < 640 || mode->vdisplay < 480) {
        ipdisp_debug("Mode too small\n");
        return MODE_BAD;
    }
    
    /* Allow current configured mode */
    if (mode->hdisplay == idev->width && mode->vdisplay == idev->height) {
        return MODE_OK;
    }
    
    return MODE_OK;
}

static const struct drm_connector_helper_funcs ipdisp_connector_helper_funcs = {
    .get_modes = ipdisp_connector_get_modes,
    .mode_valid = ipdisp_connector_mode_valid,
};

static enum drm_connector_status 
ipdisp_connector_detect(struct drm_connector *connector, bool force)
{
    ipdisp_debug("Connector detect\n");
    /* Always report connected */
    return connector_status_connected;
}

static const struct drm_connector_funcs ipdisp_connector_funcs = {
    .detect = ipdisp_connector_detect,
    .fill_modes = drm_helper_probe_single_connector_modes,
    .destroy = drm_connector_cleanup,
    .reset = drm_atomic_helper_connector_reset,
    .atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
    .atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* Simple display pipe functions */
static void ipdisp_pipe_enable(struct drm_simple_display_pipe *pipe,
                              struct drm_crtc_state *crtc_state,
                              struct drm_plane_state *plane_state)
{
    struct ipdisp_device *idev = to_ipdisp_device(pipe->crtc.dev);
    
    ipdisp_debug("Enabling display pipe\n");
    
    /* Enable streaming */
    idev->streaming_enabled = true;
    
    /* Queue initial frame */
    if (idev->stream_wq)
        queue_work(idev->stream_wq, &idev->stream_work);
}

static void ipdisp_pipe_disable(struct drm_simple_display_pipe *pipe)
{
    struct ipdisp_device *idev = to_ipdisp_device(pipe->crtc.dev);
    
    ipdisp_debug("Disabling display pipe\n");
    
    /* Disable streaming */
    idev->streaming_enabled = false;
}

static void ipdisp_pipe_update(struct drm_simple_display_pipe *pipe,
                              struct drm_plane_state *old_state)
{
    struct ipdisp_device *idev = to_ipdisp_device(pipe->crtc.dev);
    struct drm_plane_state *state = pipe->plane.state;
    struct drm_framebuffer *fb = state->fb;
    struct drm_gem_object *gem_obj;
    struct drm_gem_dma_object *dma_obj;
    void *src_addr;
    
    if (!fb || !idev->streaming_enabled)
        return;
        
    ipdisp_debug("Updating display pipe\n");
    
    gem_obj = fb->obj[0];
    dma_obj = to_drm_gem_dma_obj(gem_obj);
    src_addr = dma_obj->vaddr;
    
    if (!src_addr) {
        ipdisp_warn("No source address for framebuffer\n");
        return;
    }
    
    /* Copy framebuffer data */
    mutex_lock(&idev->fb_lock);
    
    if (fb->format->format == DRM_FORMAT_XRGB8888 ||
        fb->format->format == DRM_FORMAT_ARGB8888) {
        /* Direct copy for matching format */
        size_t copy_size = min_t(size_t, idev->fb_size, 
                                fb->height * fb->pitches[0]);
        memcpy(idev->framebuffer, src_addr, copy_size);
    } else {
        ipdisp_warn("Unsupported framebuffer format: %s\n",
                   drm_get_format_name(fb->format->format, NULL));
    }
    
    mutex_unlock(&idev->fb_lock);
    
    /* Queue frame for streaming */
    if (idev->stream_wq)
        queue_work(idev->stream_wq, &idev->stream_work);
}

static const struct drm_simple_display_pipe_funcs ipdisp_pipe_funcs = {
    .enable = ipdisp_pipe_enable,
    .disable = ipdisp_pipe_disable,
    .update = ipdisp_pipe_update,
};

/* Supported formats */
static const u32 ipdisp_formats[] = {
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ARGB8888,
};

/* Mode config functions */
static const struct drm_mode_config_funcs ipdisp_mode_config_funcs = {
    .fb_create = drm_gem_fb_create,
    .atomic_check = drm_atomic_helper_check,
    .atomic_commit = drm_atomic_helper_commit,
};

/* Initialize DRM subsystem */
int ipdisp_drm_init(struct ipdisp_device *idev)
{
    struct drm_device *drm = &idev->drm;
    int ret;
    
    ipdisp_debug("Initializing DRM subsystem\n");
    
    /* Initialize mode configuration */
    ret = drmm_mode_config_init(drm);
    if (ret) {
        ipdisp_err("Failed to initialize mode config: %d\n", ret);
        return ret;
    }
    
    /* Set mode config properties */
    drm->mode_config.min_width = 640;
    drm->mode_config.max_width = 7680;
    drm->mode_config.min_height = 480;
    drm->mode_config.max_height = 4320;
    drm->mode_config.preferred_depth = 32;
    drm->mode_config.funcs = &ipdisp_mode_config_funcs;
    
    /* Initialize connector */
    ret = drm_connector_init(drm, &idev->connector, &ipdisp_connector_funcs,
                            DRM_MODE_CONNECTOR_Virtual);
    if (ret) {
        ipdisp_err("Failed to initialize connector: %d\n", ret);
        return ret;
    }
    
    drm_connector_helper_add(&idev->connector, &ipdisp_connector_helper_funcs);
    
    /* Initialize simple display pipe */
    ret = drm_simple_display_pipe_init(drm, &idev->pipe, &ipdisp_pipe_funcs,
                                      ipdisp_formats, ARRAY_SIZE(ipdisp_formats),
                                      NULL, &idev->connector);
    if (ret) {
        ipdisp_err("Failed to initialize display pipe: %d\n", ret);
        drm_connector_cleanup(&idev->connector);
        return ret;
    }
    
    /* Enable vblank */
    ret = drm_vblank_init(drm, 1);
    if (ret) {
        ipdisp_err("Failed to initialize vblank: %d\n", ret);
        goto err_vblank;
    }
    
    /* Reset mode configuration */
    drm_mode_config_reset(drm);
    
    ipdisp_info("DRM subsystem initialized successfully\n");
    return 0;
    
err_vblank:
    drm_connector_cleanup(&idev->connector);
    return ret;
}

/* Cleanup DRM subsystem */
void ipdisp_drm_cleanup(struct ipdisp_device *idev)
{
    ipdisp_debug("Cleaning up DRM subsystem\n");
    
    /* DRM managed resources will be cleaned up automatically */
}

/* Update frame notification */
void ipdisp_drm_update_frame(struct ipdisp_device *idev)
{
    if (idev->streaming_enabled && idev->stream_wq) {
        queue_work(idev->stream_wq, &idev->stream_work);
    }
}
