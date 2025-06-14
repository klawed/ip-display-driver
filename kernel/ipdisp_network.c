/* IP Display Driver - Network Implementation
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include "ipdisp.h"

/* Forward declarations */
static int ipdisp_network_send_display_info(struct ipdisp_device *idev,
                                           struct ipdisp_client *client);
static void ipdisp_network_cleanup_clients(struct ipdisp_device *idev);

/* Network thread function */
static int ipdisp_network_thread(void *data)
{
    struct ipdisp_device *idev = data;
    struct socket *sock;
    struct sockaddr_in addr;
    int ret;
    
    ipdisp_info("Network thread started on port %d\n", idev->port);
    
    while (!kthread_should_stop()) {
        if (!idev->listen_sock) {
            msleep(1000);
            continue;
        }
        
        /* Accept incoming connections */
        ret = kernel_accept(idev->listen_sock, &sock, O_NONBLOCK);
        if (ret < 0) {
            if (ret != -EAGAIN && ret != -EWOULDBLOCK) {
                ipdisp_debug("Accept failed: %d\n", ret);
            }
            msleep(100);
            continue;
        }
        
        /* Get client address */
        ret = kernel_getpeername(sock, (struct sockaddr *)&addr);
        if (ret < 0) {
            ipdisp_warn("Failed to get peer name: %d\n", ret);
            sock_release(sock);
            continue;
        }
        
        ipdisp_info("New client connected from %pI4:%d\n", 
                   &addr.sin_addr, ntohs(addr.sin_port));
        
        /* Add client to list */
        mutex_lock(&idev->clients_lock);
        
        /* Check if we have room for more clients */
        int client_count = 0;
        struct ipdisp_client *client;
        list_for_each_entry(client, &idev->clients, list) {
            if (client->active)
                client_count++;
        }
        
        if (client_count >= IPDISP_MAX_CLIENTS) {
            ipdisp_warn("Maximum clients reached, rejecting connection\n");
            sock_release(sock);
            mutex_unlock(&idev->clients_lock);
            continue;
        }
        
        /* Allocate new client */
        client = kzalloc(sizeof(*client), GFP_KERNEL);
        if (!client) {
            ipdisp_err("Failed to allocate client structure\n");
            sock_release(sock);
            mutex_unlock(&idev->clients_lock);
            continue;
        }
        
        client->sock = sock;
        client->addr = addr;
        client->active = true;
        mutex_init(&client->lock);
        list_add_tail(&client->list, &idev->clients);
        
        mutex_unlock(&idev->clients_lock);
        
        /* Send welcome message with display info */
        ipdisp_network_send_display_info(idev, client);
        
        /* Cleanup inactive clients periodically */
        ipdisp_network_cleanup_clients(idev);
    }
    
    ipdisp_info("Network thread stopped\n");
    return 0;
}

/* Send display information to client */
static int ipdisp_network_send_display_info(struct ipdisp_device *idev,
                                           struct ipdisp_client *client)
{
    struct ipdisp_packet_header header;
    struct kvec iov;
    struct msghdr msg;
    int ret;
    
    /* Prepare header */
    memset(&header, 0, sizeof(header));
    header.magic = cpu_to_be32(IPDISP_MAGIC);
    header.version = cpu_to_be32(IPDISP_VERSION);
    header.width = cpu_to_be32(idev->width);
    header.height = cpu_to_be32(idev->height);
    header.format = cpu_to_be32(IPDISP_FORMAT_RGBA32);
    header.timestamp = cpu_to_be64(ktime_get_ns());
    header.size = 0; /* No data payload for info packet */
    header.reserved = 0;
    
    /* Send header */
    iov.iov_base = &header;
    iov.iov_len = sizeof(header);
    
    memset(&msg, 0, sizeof(msg));
    msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
    
    mutex_lock(&client->lock);
    ret = kernel_sendmsg(client->sock, &msg, &iov, 1, sizeof(header));
    mutex_unlock(&client->lock);
    
    if (ret != sizeof(header)) {
        ipdisp_warn("Failed to send display info to client: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }
    
    return 0;
}

/* Remove inactive clients */
static void ipdisp_network_cleanup_clients(struct ipdisp_device *idev)
{
    struct ipdisp_client *client, *tmp;
    
    mutex_lock(&idev->clients_lock);
    
    list_for_each_entry_safe(client, tmp, &idev->clients, list) {
        if (!client->active) {
            ipdisp_debug("Removing inactive client\n");
            list_del(&client->list);
            if (client->sock)
                sock_release(client->sock);
            mutex_destroy(&client->lock);
            kfree(client);
        }
    }
    
    mutex_unlock(&idev->clients_lock);
}

/* Send frame data to all clients */
int ipdisp_network_send_frame(struct ipdisp_device *idev, 
                             const void *data, size_t size)
{
    struct ipdisp_client *client;
    struct ipdisp_packet_header header;
    struct kvec iov[2];
    struct msghdr msg;
    int ret, clients_sent = 0;
    
    if (list_empty(&idev->clients))
        return 0;
    
    /* Prepare header */
    memset(&header, 0, sizeof(header));
    header.magic = cpu_to_be32(IPDISP_MAGIC);
    header.version = cpu_to_be32(IPDISP_VERSION);
    header.width = cpu_to_be32(idev->width);
    header.height = cpu_to_be32(idev->height);
    header.format = cpu_to_be32(IPDISP_FORMAT_RGBA32);
    header.timestamp = cpu_to_be64(ktime_get_ns());
    header.size = cpu_to_be32(size);
    header.reserved = 0;
    
    /* Prepare message */
    iov[0].iov_base = &header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = (void *)data;
    iov[1].iov_len = size;
    
    memset(&msg, 0, sizeof(msg));
    msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
    
    /* Send to all active clients */
    mutex_lock(&idev->clients_lock);
    
    list_for_each_entry(client, &idev->clients, list) {
        if (!client->active)
            continue;
            
        mutex_lock(&client->lock);
        ret = kernel_sendmsg(client->sock, &msg, iov, 2, 
                           sizeof(header) + size);
        mutex_unlock(&client->lock);
        
        if (ret < 0) {
            ipdisp_debug("Failed to send frame to client: %d\n", ret);
            client->active = false; /* Mark for cleanup */
        } else if (ret != sizeof(header) + size) {
            ipdisp_debug("Partial send to client: %d/%zu\n", 
                        ret, sizeof(header) + size);
            client->active = false; /* Mark for cleanup */
        } else {
            clients_sent++;
        }
    }
    
    mutex_unlock(&idev->clients_lock);
    
    /* Schedule cleanup if needed */
    if (clients_sent == 0) {
        schedule_work(&idev->stream_work);
    }
    
    return clients_sent;
}

/* Initialize network subsystem */
int ipdisp_network_init(struct ipdisp_device *idev)
{
    struct socket *sock;
    struct sockaddr_in addr;
    int ret, opt = 1;
    
    ipdisp_debug("Initializing network subsystem\n");
    
    /* Create listening socket */
    ret = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (ret < 0) {
        ipdisp_err("Failed to create socket: %d\n", ret);
        return ret;
    }
    
    /* Set socket options */
    ret = kernel_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                           (char *)&opt, sizeof(opt));
    if (ret < 0) {
        ipdisp_warn("Failed to set SO_REUSEADDR: %d\n", ret);
    }
    
    /* Bind socket */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(idev->port);
    
    ret = kernel_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        ipdisp_err("Failed to bind socket to port %d: %d\n", idev->port, ret);
        sock_release(sock);
        return ret;
    }
    
    /* Start listening */
    ret = kernel_listen(sock, IPDISP_MAX_CLIENTS);
    if (ret < 0) {
        ipdisp_err("Failed to listen on socket: %d\n", ret);
        sock_release(sock);
        return ret;
    }
    
    idev->listen_sock = sock;
    
    /* Start network thread */
    idev->network_thread = kthread_run(ipdisp_network_thread, idev,
                                      "ipdisp-net");
    if (IS_ERR(idev->network_thread)) {
        ret = PTR_ERR(idev->network_thread);
        ipdisp_err("Failed to start network thread: %d\n", ret);
        sock_release(sock);
        idev->listen_sock = NULL;
        return ret;
    }
    
    ipdisp_info("Network subsystem initialized on port %d\n", idev->port);
    return 0;
}

/* Cleanup network subsystem */
void ipdisp_network_cleanup(struct ipdisp_device *idev)
{
    struct ipdisp_client *client, *tmp;
    
    ipdisp_debug("Cleaning up network subsystem\n");
    
    /* Stop network thread */
    if (idev->network_thread) {
        kthread_stop(idev->network_thread);
        idev->network_thread = NULL;
    }
    
    /* Close listening socket */
    if (idev->listen_sock) {
        sock_release(idev->listen_sock);
        idev->listen_sock = NULL;
    }
    
    /* Cleanup all clients */
    mutex_lock(&idev->clients_lock);
    
    list_for_each_entry_safe(client, tmp, &idev->clients, list) {
        ipdisp_debug("Closing client connection\n");
        list_del(&client->list);
        if (client->sock)
            sock_release(client->sock);
        mutex_destroy(&client->lock);
        kfree(client);
    }
    
    mutex_unlock(&idev->clients_lock);
    
    ipdisp_info("Network subsystem cleaned up\n");
}
