/* IP Display Driver - Main Header
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#ifndef IPDISP_H
#define IPDISP_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <net/sock.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mode_config.h>

/* Module information */
#define DRIVER_NAME "ipdisp"
#define DRIVER_DESC "IP Display Driver"
#define DRIVER_DATE "20241214"
#define DRIVER_MAJOR 1
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

/* Default configuration */
#define IPDISP_DEFAULT_WIDTH 1920
#define IPDISP_DEFAULT_HEIGHT 1080
#define IPDISP_DEFAULT_PORT 8080
#define IPDISP_MAX_CLIENTS 4
#define IPDISP_BUFFER_SIZE (1920 * 1080 * 4) /* RGBA32 */

/* Network protocol */
#define IPDISP_MAGIC 0x49504453  /* "IPDS" */
#define IPDISP_VERSION 1
#define IPDISP_HEADER_SIZE 32

/* Frame formats */
enum ipdisp_format {
    IPDISP_FORMAT_RGBA32 = 0,
    IPDISP_FORMAT_RGB24,
    IPDISP_FORMAT_H264,
    IPDISP_FORMAT_H265,
};

/* Network packet header */
struct ipdisp_packet_header {
    u32 magic;      /* Magic number */
    u32 version;    /* Protocol version */
    u32 width;      /* Frame width */
    u32 height;     /* Frame height */
    u32 format;     /* Frame format */
    u64 timestamp;  /* Frame timestamp */
    u32 size;       /* Data size */
    u32 reserved;   /* Reserved for future use */
} __packed;

/* Client connection */
struct ipdisp_client {
    struct socket *sock;
    struct sockaddr_in addr;
    struct list_head list;
    bool active;
    struct mutex lock;
};

/* Main device structure */
struct ipdisp_device {
    struct drm_device drm;
    struct platform_device *pdev;
    
    /* Display properties */
    u32 width;
    u32 height;
    u32 pitch;
    
    /* Frame buffer */
    void *framebuffer;
    dma_addr_t fb_dma_addr;
    size_t fb_size;
    struct mutex fb_lock;
    
    /* Network */
    struct socket *listen_sock;
    u16 port;
    struct task_struct *network_thread;
    struct list_head clients;
    struct mutex clients_lock;
    
    /* Encoder/streaming */
    struct workqueue_struct *stream_wq;
    struct work_struct stream_work;
    bool streaming_enabled;
    
    /* DRM components */
    struct drm_simple_display_pipe pipe;
    struct drm_connector connector;
    struct drm_encoder encoder;
};

/* Function prototypes */

/* DRM functions */
int ipdisp_drm_init(struct ipdisp_device *idev);
void ipdisp_drm_cleanup(struct ipdisp_device *idev);
void ipdisp_drm_update_frame(struct ipdisp_device *idev);

/* Network functions */
int ipdisp_network_init(struct ipdisp_device *idev);
void ipdisp_network_cleanup(struct ipdisp_device *idev);
int ipdisp_network_send_frame(struct ipdisp_device *idev, 
                             const void *data, size_t size);

/* Encoder functions */
int ipdisp_encoder_init(struct ipdisp_device *idev);
void ipdisp_encoder_cleanup(struct ipdisp_device *idev);
void ipdisp_encoder_queue_frame(struct ipdisp_device *idev);

/* Utility macros */
#define ipdisp_dev(dev) container_of(dev, struct ipdisp_device, drm)
#define to_ipdisp_device(x) container_of(x, struct ipdisp_device, drm)

/* Debug macros */
#ifdef DEBUG
#define ipdisp_debug(fmt, ...) \
    pr_debug(DRIVER_NAME ": " fmt, ##__VA_ARGS__)
#else
#define ipdisp_debug(fmt, ...) do { } while (0)
#endif

#define ipdisp_info(fmt, ...) \
    pr_info(DRIVER_NAME ": " fmt, ##__VA_ARGS__)

#define ipdisp_warn(fmt, ...) \
    pr_warn(DRIVER_NAME ": " fmt, ##__VA_ARGS__)

#define ipdisp_err(fmt, ...) \
    pr_err(DRIVER_NAME ": " fmt, ##__VA_ARGS__)

#endif /* IPDISP_H */
