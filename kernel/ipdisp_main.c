/* IP Display Driver - Main Module
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include "ipdisp.h"

/* Module parameters */
static unsigned int width = IPDISP_DEFAULT_WIDTH;
static unsigned int height = IPDISP_DEFAULT_HEIGHT;
static unsigned int port = IPDISP_DEFAULT_PORT;
static char *codec = "raw";

module_param(width, uint, 0444);
MODULE_PARM_DESC(width, "Display width (default: 1920)");

module_param(height, uint, 0444);
MODULE_PARM_DESC(height, "Display height (default: 1080)");

module_param(port, uint, 0444);
MODULE_PARM_DESC(port, "Network port (default: 8080)");

module_param(codec, charp, 0444);
MODULE_PARM_DESC(codec, "Video codec: raw, h264, h265 (default: raw)");

/* Global device instance */
static struct ipdisp_device *ipdisp_global_dev;

/* Platform device release function */
static void ipdisp_platform_release(struct device *dev)
{
    /* Nothing to do here - managed by DRM */
}

/* Platform device */
static struct platform_device ipdisp_platform_device = {
    .name = DRIVER_NAME,
    .id = -1,
    .dev = {
        .release = ipdisp_platform_release,
    },
};

/* DRM driver operations */
static const struct drm_driver ipdisp_drm_driver = {
    .driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
    .fops = &ipdisp_fops,
    .name = DRIVER_NAME,
    .desc = DRIVER_DESC,
    .date = DRIVER_DATE,
    .major = DRIVER_MAJOR,
    .minor = DRIVER_MINOR,
    .patchlevel = DRIVER_PATCHLEVEL,
};

/* File operations for DRM device */
DEFINE_DRM_GEM_FOPS(ipdisp_fops);

/* Device initialization */
static int ipdisp_device_init(struct ipdisp_device *idev)
{
    int ret;
    
    /* Initialize device structure */
    idev->width = width;
    idev->height = height;
    idev->port = port;
    idev->pitch = width * 4; /* RGBA32 */
    idev->fb_size = idev->pitch * height;
    
    /* Initialize mutexes */
    mutex_init(&idev->fb_lock);
    mutex_init(&idev->clients_lock);
    
    /* Initialize client list */
    INIT_LIST_HEAD(&idev->clients);
    
    /* Allocate framebuffer */
    idev->framebuffer = dma_alloc_coherent(&idev->pdev->dev, 
                                          idev->fb_size,
                                          &idev->fb_dma_addr, 
                                          GFP_KERNEL);
    if (!idev->framebuffer) {
        ipdisp_err("Failed to allocate framebuffer\n");
        return -ENOMEM;
    }
    
    /* Clear framebuffer */
    memset(idev->framebuffer, 0, idev->fb_size);
    
    ipdisp_info("Allocated %zu bytes for %dx%d framebuffer\n",
                idev->fb_size, idev->width, idev->height);
    
    /* Initialize DRM subsystem */
    ret = ipdisp_drm_init(idev);
    if (ret) {
        ipdisp_err("Failed to initialize DRM: %d\n", ret);
        goto err_drm;
    }
    
    /* Initialize network subsystem */
    ret = ipdisp_network_init(idev);
    if (ret) {
        ipdisp_err("Failed to initialize network: %d\n", ret);
        goto err_network;
    }
    
    /* Initialize encoder subsystem */
    ret = ipdisp_encoder_init(idev);
    if (ret) {
        ipdisp_err("Failed to initialize encoder: %d\n", ret);
        goto err_encoder;
    }
    
    ipdisp_info("Device initialized successfully\n");
    return 0;
    
err_encoder:
    ipdisp_network_cleanup(idev);
err_network:
    ipdisp_drm_cleanup(idev);
err_drm:
    dma_free_coherent(&idev->pdev->dev, idev->fb_size,
                     idev->framebuffer, idev->fb_dma_addr);
    return ret;
}

/* Device cleanup */
static void ipdisp_device_cleanup(struct ipdisp_device *idev)
{
    if (!idev)
        return;
        
    ipdisp_info("Cleaning up device\n");
    
    /* Cleanup subsystems */
    ipdisp_encoder_cleanup(idev);
    ipdisp_network_cleanup(idev);
    ipdisp_drm_cleanup(idev);
    
    /* Free framebuffer */
    if (idev->framebuffer) {
        dma_free_coherent(&idev->pdev->dev, idev->fb_size,
                         idev->framebuffer, idev->fb_dma_addr);
    }
    
    /* Cleanup mutexes */
    mutex_destroy(&idev->fb_lock);
    mutex_destroy(&idev->clients_lock);
}

/* Platform device probe */
static int ipdisp_probe(struct platform_device *pdev)
{
    struct ipdisp_device *idev;
    int ret;
    
    ipdisp_info("Probing IP display driver\n");
    
    /* Allocate device structure */
    idev = devm_drm_dev_alloc(&pdev->dev, &ipdisp_drm_driver,
                             struct ipdisp_device, drm);
    if (IS_ERR(idev)) {
        ret = PTR_ERR(idev);
        ipdisp_err("Failed to allocate DRM device: %d\n", ret);
        return ret;
    }
    
    idev->pdev = pdev;
    platform_set_drvdata(pdev, idev);
    
    /* Initialize device */
    ret = ipdisp_device_init(idev);
    if (ret) {
        ipdisp_err("Failed to initialize device: %d\n", ret);
        return ret;
    }
    
    /* Register DRM device */
    ret = drm_dev_register(&idev->drm, 0);
    if (ret) {
        ipdisp_err("Failed to register DRM device: %d\n", ret);
        goto err_register;
    }
    
    ipdisp_global_dev = idev;
    
    ipdisp_info("IP Display driver loaded successfully\n");
    ipdisp_info("Resolution: %dx%d, Port: %d, Codec: %s\n",
                width, height, port, codec);
    
    return 0;
    
err_register:
    ipdisp_device_cleanup(idev);
    return ret;
}

/* Platform device remove */
static int ipdisp_remove(struct platform_device *pdev)
{
    struct ipdisp_device *idev = platform_get_drvdata(pdev);
    
    ipdisp_info("Removing IP display driver\n");
    
    if (idev) {
        drm_dev_unregister(&idev->drm);
        ipdisp_device_cleanup(idev);
        ipdisp_global_dev = NULL;
    }
    
    return 0;
}

/* Platform driver */
static struct platform_driver ipdisp_platform_driver = {
    .probe = ipdisp_probe,
    .remove = ipdisp_remove,
    .driver = {
        .name = DRIVER_NAME,
    },
};

/* Module initialization */
static int __init ipdisp_init(void)
{
    int ret;
    
    ipdisp_info("Loading IP Display Driver v%d.%d.%d\n",
                DRIVER_MAJOR, DRIVER_MINOR, DRIVER_PATCHLEVEL);
    
    /* Validate parameters */
    if (width < 640 || width > 7680) {
        ipdisp_err("Invalid width: %d (must be 640-7680)\n", width);
        return -EINVAL;
    }
    
    if (height < 480 || height > 4320) {
        ipdisp_err("Invalid height: %d (must be 480-4320)\n", height);
        return -EINVAL;
    }
    
    if (port < 1024 || port > 65535) {
        ipdisp_err("Invalid port: %d (must be 1024-65535)\n", port);
        return -EINVAL;
    }
    
    /* Register platform device */
    ret = platform_device_register(&ipdisp_platform_device);
    if (ret) {
        ipdisp_err("Failed to register platform device: %d\n", ret);
        return ret;
    }
    
    /* Register platform driver */
    ret = platform_driver_register(&ipdisp_platform_driver);
    if (ret) {
        ipdisp_err("Failed to register platform driver: %d\n", ret);
        platform_device_unregister(&ipdisp_platform_device);
        return ret;
    }
    
    return 0;
}

/* Module cleanup */
static void __exit ipdisp_exit(void)
{
    ipdisp_info("Unloading IP Display Driver\n");
    
    platform_driver_unregister(&ipdisp_platform_driver);
    platform_device_unregister(&ipdisp_platform_device);
}

module_init(ipdisp_init);
module_exit(ipdisp_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("IP Display Driver Project");
MODULE_VERSION("1.0.0");
