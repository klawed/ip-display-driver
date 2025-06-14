/* IP Display Driver - Encoder Implementation
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include "ipdisp.h"

/* Streaming work function */
static void ipdisp_stream_work_func(struct work_struct *work)
{
    struct ipdisp_device *idev = container_of(work, struct ipdisp_device, 
                                             stream_work);
    size_t frame_size;
    int ret;
    
    if (!idev->streaming_enabled || !idev->framebuffer)
        return;
    
    /* Calculate frame size */
    frame_size = idev->width * idev->height * 4; /* RGBA32 */
    
    /* Lock framebuffer and send frame */
    mutex_lock(&idev->fb_lock);
    
    ret = ipdisp_network_send_frame(idev, idev->framebuffer, frame_size);
    if (ret > 0) {
        ipdisp_debug("Frame sent to %d clients\n", ret);
    }
    
    mutex_unlock(&idev->fb_lock);
}

/* Initialize encoder subsystem */
int ipdisp_encoder_init(struct ipdisp_device *idev)
{
    ipdisp_debug("Initializing encoder subsystem\n");
    
    /* Create workqueue for streaming */
    idev->stream_wq = alloc_workqueue("ipdisp-stream", 
                                     WQ_UNBOUND | WQ_HIGHPRI, 1);
    if (!idev->stream_wq) {
        ipdisp_err("Failed to create stream workqueue\n");
        return -ENOMEM;
    }
    
    /* Initialize work structure */
    INIT_WORK(&idev->stream_work, ipdisp_stream_work_func);
    
    idev->streaming_enabled = false;
    
    ipdisp_info("Encoder subsystem initialized\n");
    return 0;
}

/* Cleanup encoder subsystem */
void ipdisp_encoder_cleanup(struct ipdisp_device *idev)
{
    ipdisp_debug("Cleaning up encoder subsystem\n");
    
    /* Disable streaming */
    idev->streaming_enabled = false;
    
    /* Destroy workqueue */
    if (idev->stream_wq) {
        flush_workqueue(idev->stream_wq);
        destroy_workqueue(idev->stream_wq);
        idev->stream_wq = NULL;
    }
    
    ipdisp_info("Encoder subsystem cleaned up\n");
}

/* Queue frame for encoding/streaming */
void ipdisp_encoder_queue_frame(struct ipdisp_device *idev)
{
    if (idev->streaming_enabled && idev->stream_wq) {
        queue_work(idev->stream_wq, &idev->stream_work);
    }
}
