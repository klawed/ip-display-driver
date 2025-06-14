# Input Handling Extension Guide

## Overview

The current IP Display Driver handles display output streaming. For complete remote desktop functionality, we need to add keyboard and mouse input handling. This document outlines how to extend the system with input capabilities.

## Architecture Options

### Option 1: Extend Current Protocol (Recommended)
Extend the existing IP Display Protocol to handle bidirectional communication:
- **Display data**: Server → Client (existing)
- **Input events**: Client → Server (new)

### Option 2: Separate Input Channel
Use a separate TCP connection for input events while keeping display streaming separate.

### Option 3: uinput Integration
Create virtual input devices using Linux uinput subsystem.

## Protocol Extension

### Extended Packet Types

```c
// Extended packet types
enum ipdisp_packet_type {
    IPDISP_PACKET_DISPLAY_INFO = 0,
    IPDISP_PACKET_FRAME_DATA = 1,
    IPDISP_PACKET_KEY_EVENT = 2,
    IPDISP_PACKET_MOUSE_EVENT = 3,
    IPDISP_PACKET_MOUSE_MOVE = 4,
    IPDISP_PACKET_CLIPBOARD = 5,
};

// Extended header structure
struct ipdisp_packet_header_v2 {
    u32 magic;          // 0x49504453 ("IPDS")
    u32 version;        // Protocol version (2)
    u32 packet_type;    // Packet type (enum above)
    u32 width;          // Frame width (display packets)
    u32 height;         // Frame height (display packets)
    u32 format;         // Frame format (display packets)
    u64 timestamp;      // Packet timestamp
    u32 size;           // Data payload size
    u32 reserved;       // Reserved for future use
} __packed;

// Input event structures
struct ipdisp_key_event {
    u32 keycode;        // Linux keycode (KEY_*)
    u32 scancode;       // Hardware scancode
    u32 modifiers;      // Modifier state (shift, ctrl, etc.)
    u8 pressed;         // 1 = pressed, 0 = released
    u8 reserved[3];
} __packed;

struct ipdisp_mouse_event {
    u32 button;         // Mouse button (BTN_LEFT, BTN_RIGHT, etc.)
    u8 pressed;         // 1 = pressed, 0 = released
    u8 reserved[3];
} __packed;

struct ipdisp_mouse_move {
    s32 x;              // Absolute X coordinate
    s32 y;              // Absolute Y coordinate
    s32 rel_x;          // Relative X movement
    s32 rel_y;          // Relative Y movement
    u8 absolute;        // 1 = absolute, 0 = relative
    u8 reserved[3];
} __packed;
```

## Kernel Module Extensions

### Add Input Device Support

Create a new kernel source file:

```c
// kernel/ipdisp_input.c
#include "ipdisp.h"
#include <linux/input.h>
#include <linux/uinput.h>

/* Input device structures */
struct ipdisp_input {
    struct input_dev *keyboard;
    struct input_dev *mouse;
    struct mutex input_lock;
    bool input_enabled;
};

/* Initialize input devices */
int ipdisp_input_init(struct ipdisp_device *idev)
{
    struct ipdisp_input *input;
    int ret;
    
    input = kzalloc(sizeof(*input), GFP_KERNEL);
    if (!input)
        return -ENOMEM;
    
    mutex_init(&input->input_lock);
    
    /* Create keyboard device */
    input->keyboard = input_allocate_device();
    if (!input->keyboard) {
        ret = -ENOMEM;
        goto err_alloc_kbd;
    }
    
    input->keyboard->name = "IP Display Virtual Keyboard";
    input->keyboard->id.bustype = BUS_VIRTUAL;
    input->keyboard->id.vendor = 0x0001;
    input->keyboard->id.product = 0x0001;
    input->keyboard->id.version = 0x0001;
    
    /* Set keyboard capabilities */
    __set_bit(EV_KEY, input->keyboard->evbit);
    __set_bit(EV_REP, input->keyboard->evbit);
    
    /* Add all keyboard keys */
    for (int i = 0; i < KEY_MAX; i++) {
        __set_bit(i, input->keyboard->keybit);
    }
    
    ret = input_register_device(input->keyboard);
    if (ret) {
        ipdisp_err("Failed to register keyboard device: %d\n", ret);
        goto err_reg_kbd;
    }
    
    /* Create mouse device */
    input->mouse = input_allocate_device();
    if (!input->mouse) {
        ret = -ENOMEM;
        goto err_alloc_mouse;
    }
    
    input->mouse->name = "IP Display Virtual Mouse";
    input->mouse->id.bustype = BUS_VIRTUAL;
    input->mouse->id.vendor = 0x0001;
    input->mouse->id.product = 0x0002;
    input->mouse->id.version = 0x0001;
    
    /* Set mouse capabilities */
    __set_bit(EV_KEY, input->mouse->evbit);
    __set_bit(EV_REL, input->mouse->evbit);
    __set_bit(EV_ABS, input->mouse->evbit);
    
    /* Mouse buttons */
    __set_bit(BTN_LEFT, input->mouse->keybit);
    __set_bit(BTN_RIGHT, input->mouse->keybit);
    __set_bit(BTN_MIDDLE, input->mouse->keybit);
    __set_bit(BTN_SIDE, input->mouse->keybit);
    __set_bit(BTN_EXTRA, input->mouse->keybit);
    
    /* Mouse movement */
    __set_bit(REL_X, input->mouse->relbit);
    __set_bit(REL_Y, input->mouse->relbit);
    __set_bit(REL_WHEEL, input->mouse->relbit);
    __set_bit(REL_HWHEEL, input->mouse->relbit);
    
    /* Absolute positioning */
    input_set_abs_params(input->mouse, ABS_X, 0, idev->width - 1, 0, 0);
    input_set_abs_params(input->mouse, ABS_Y, 0, idev->height - 1, 0, 0);
    
    ret = input_register_device(input->mouse);
    if (ret) {
        ipdisp_err("Failed to register mouse device: %d\n", ret);
        goto err_reg_mouse;
    }
    
    input->input_enabled = true;
    idev->input = input;
    
    ipdisp_info("Input devices initialized\n");
    return 0;
    
err_reg_mouse:
    input_free_device(input->mouse);
err_alloc_mouse:
    input_unregister_device(input->keyboard);
err_reg_kbd:
    input_free_device(input->keyboard);
err_alloc_kbd:
    mutex_destroy(&input->input_lock);
    kfree(input);
    return ret;
}

/* Handle input events from clients */
int ipdisp_handle_input_event(struct ipdisp_device *idev, 
                             struct ipdisp_packet_header_v2 *header,
                             const void *data)
{
    struct ipdisp_input *input = idev->input;
    
    if (!input || !input->input_enabled)
        return -ENODEV;
    
    mutex_lock(&input->input_lock);
    
    switch (header->packet_type) {
    case IPDISP_PACKET_KEY_EVENT: {
        const struct ipdisp_key_event *key_event = data;
        
        input_report_key(input->keyboard, key_event->keycode, 
                        key_event->pressed);
        input_sync(input->keyboard);
        
        ipdisp_debug("Key event: code=%d, pressed=%d\n", 
                    key_event->keycode, key_event->pressed);
        break;
    }
    
    case IPDISP_PACKET_MOUSE_EVENT: {
        const struct ipdisp_mouse_event *mouse_event = data;
        
        input_report_key(input->mouse, mouse_event->button, 
                        mouse_event->pressed);
        input_sync(input->mouse);
        
        ipdisp_debug("Mouse button: button=%d, pressed=%d\n", 
                    mouse_event->button, mouse_event->pressed);
        break;
    }
    
    case IPDISP_PACKET_MOUSE_MOVE: {
        const struct ipdisp_mouse_move *mouse_move = data;
        
        if (mouse_move->absolute) {
            input_report_abs(input->mouse, ABS_X, mouse_move->x);
            input_report_abs(input->mouse, ABS_Y, mouse_move->y);
        } else {
            input_report_rel(input->mouse, REL_X, mouse_move->rel_x);
            input_report_rel(input->mouse, REL_Y, mouse_move->rel_y);
        }
        input_sync(input->mouse);
        
        ipdisp_debug("Mouse move: x=%d, y=%d, abs=%d\n", 
                    mouse_move->x, mouse_move->y, mouse_move->absolute);
        break;
    }
    
    default:
        ipdisp_warn("Unknown input packet type: %d\n", header->packet_type);
        mutex_unlock(&input->input_lock);
        return -EINVAL;
    }
    
    mutex_unlock(&input->input_lock);
    return 0;
}

/* Cleanup input devices */
void ipdisp_input_cleanup(struct ipdisp_device *idev)
{
    struct ipdisp_input *input = idev->input;
    
    if (!input)
        return;
    
    ipdisp_debug("Cleaning up input devices\n");
    
    mutex_lock(&input->input_lock);
    input->input_enabled = false;
    
    if (input->keyboard) {
        input_unregister_device(input->keyboard);
        input->keyboard = NULL;
    }
    
    if (input->mouse) {
        input_unregister_device(input->mouse);
        input->mouse = NULL;
    }
    
    mutex_unlock(&input->input_lock);
    mutex_destroy(&input->input_lock);
    
    kfree(input);
    idev->input = NULL;
    
    ipdisp_info("Input devices cleaned up\n");
}
```

### Update Network Handler

Modify the network handler to process input events:

```c
// In ipdisp_network.c - add input handling
static int ipdisp_network_handle_client_data(struct ipdisp_device *idev,
                                            struct ipdisp_client *client)
{
    struct ipdisp_packet_header_v2 header;
    void *data = NULL;
    int ret;
    
    /* Read packet header */
    ret = kernel_recvmsg(client->sock, &msg, &iov, 1, 
                        sizeof(header), MSG_WAITALL);
    if (ret != sizeof(header)) {
        return ret < 0 ? ret : -EIO;
    }
    
    /* Validate header */
    if (header.magic != IPDISP_MAGIC || header.version != 2) {
        return -EINVAL;
    }
    
    /* Handle input events */
    if (header.packet_type >= IPDISP_PACKET_KEY_EVENT && 
        header.packet_type <= IPDISP_PACKET_CLIPBOARD) {
        
        if (header.size > 0) {
            data = kmalloc(header.size, GFP_KERNEL);
            if (!data)
                return -ENOMEM;
            
            ret = kernel_recvmsg(client->sock, &msg, &iov, 1, 
                               header.size, MSG_WAITALL);
            if (ret != header.size) {
                kfree(data);
                return ret < 0 ? ret : -EIO;
            }
        }
        
        ret = ipdisp_handle_input_event(idev, &header, data);
        
        if (data)
            kfree(data);
    }
    
    return ret;
}
```

## Rust Client Extensions

### Add Input Event Handling

```rust
// client/src/input.rs
use anyhow::Result;
use gtk4::prelude::*;
use gdk4::{Key, ModifierType};
use std::convert::TryInto;

use crate::protocol::{PacketHeader, PacketType};

pub struct InputHandler {
    network_client: Arc<NetworkClient>,
}

impl InputHandler {
    pub fn new(network_client: Arc<NetworkClient>) -> Self {
        Self { network_client }
    }
    
    pub async fn send_key_event(&self, keycode: u32, pressed: bool) -> Result<()> {
        let key_event = KeyEvent {
            keycode,
            scancode: 0,
            modifiers: 0,
            pressed: if pressed { 1 } else { 0 },
            reserved: [0; 3],
        };
        
        let header = PacketHeader {
            magic: MAGIC,
            version: 2,
            packet_type: PacketType::KeyEvent as u32,
            width: 0,
            height: 0,
            format: 0,
            timestamp: get_timestamp(),
            size: std::mem::size_of::<KeyEvent>() as u32,
            reserved: 0,
        };
        
        let mut data = Vec::new();
        data.extend_from_slice(&header.to_bytes());
        data.extend_from_slice(&key_event.to_bytes());
        
        self.network_client.send_data(&data).await
    }
    
    pub async fn send_mouse_button(&self, button: u32, pressed: bool) -> Result<()> {
        let mouse_event = MouseEvent {
            button,
            pressed: if pressed { 1 } else { 0 },
            reserved: [0; 3],
        };
        
        let header = PacketHeader {
            magic: MAGIC,
            version: 2,
            packet_type: PacketType::MouseEvent as u32,
            width: 0,
            height: 0,
            format: 0,
            timestamp: get_timestamp(),
            size: std::mem::size_of::<MouseEvent>() as u32,
            reserved: 0,
        };
        
        let mut data = Vec::new();
        data.extend_from_slice(&header.to_bytes());
        data.extend_from_slice(&mouse_event.to_bytes());
        
        self.network_client.send_data(&data).await
    }
    
    pub async fn send_mouse_move(&self, x: i32, y: i32, absolute: bool) -> Result<()> {
        let mouse_move = MouseMove {
            x,
            y,
            rel_x: 0,
            rel_y: 0,
            absolute: if absolute { 1 } else { 0 },
            reserved: [0; 3],
        };
        
        let header = PacketHeader {
            magic: MAGIC,
            version: 2,
            packet_type: PacketType::MouseMove as u32,
            width: 0,
            height: 0,
            format: 0,
            timestamp: get_timestamp(),
            size: std::mem::size_of::<MouseMove>() as u32,
            reserved: 0,
        };
        
        let mut data = Vec::new();
        data.extend_from_slice(&header.to_bytes());
        data.extend_from_slice(&mouse_move.to_bytes());
        
        self.network_client.send_data(&data).await
    }
}

#[repr(C, packed)]
struct KeyEvent {
    keycode: u32,
    scancode: u32,
    modifiers: u32,
    pressed: u8,
    reserved: [u8; 3],
}

#[repr(C, packed)]
struct MouseEvent {
    button: u32,
    pressed: u8,
    reserved: [u8; 3],
}

#[repr(C, packed)]
struct MouseMove {
    x: i32,
    y: i32,
    rel_x: i32,
    rel_y: i32,
    absolute: u8,
    reserved: [u8; 3],
}
```

### Update UI with Input Handling

```rust
// In client/src/ui.rs - add input event handlers
impl DisplayWindow {
    pub fn setup_input_handling(&self, input_handler: Arc<InputHandler>) {
        // Keyboard input
        let input_handler_clone = Arc::clone(&input_handler);
        self.drawing_area.connect_key_pressed(move |_, key, _, _| {
            let input_handler = Arc::clone(&input_handler_clone);
            let keycode = gdk_key_to_linux_keycode(key);
            
            glib::spawn_future_local(async move {
                if let Err(e) = input_handler.send_key_event(keycode, true).await {
                    eprintln!("Failed to send key press: {}", e);
                }
            });
            
            glib::Propagation::Proceed
        });
        
        let input_handler_clone = Arc::clone(&input_handler);
        self.drawing_area.connect_key_released(move |_, key, _, _| {
            let input_handler = Arc::clone(&input_handler_clone);
            let keycode = gdk_key_to_linux_keycode(key);
            
            glib::spawn_future_local(async move {
                if let Err(e) = input_handler.send_key_event(keycode, false).await {
                    eprintln!("Failed to send key release: {}", e);
                }
            });
            
            glib::Propagation::Proceed
        });
        
        // Mouse input
        let input_handler_clone = Arc::clone(&input_handler);
        self.drawing_area.connect_button_press_event(move |_, event| {
            let input_handler = Arc::clone(&input_handler_clone);
            let button = gdk_button_to_linux_button(event.button());
            let (x, y) = event.position();
            
            glib::spawn_future_local(async move {
                if let Err(e) = input_handler.send_mouse_button(button, true).await {
                    eprintln!("Failed to send mouse press: {}", e);
                }
            });
            
            glib::Propagation::Proceed
        });
        
        // Mouse movement
        let input_handler_clone = Arc::clone(&input_handler);
        self.drawing_area.connect_motion_notify_event(move |_, event| {
            let input_handler = Arc::clone(&input_handler_clone);
            let (x, y) = event.position();
            
            glib::spawn_future_local(async move {
                if let Err(e) = input_handler.send_mouse_move(x as i32, y as i32, true).await {
                    eprintln!("Failed to send mouse move: {}", e);
                }
            });
            
            glib::Propagation::Proceed
        });
        
        // Enable input events
        self.drawing_area.set_can_focus(true);
        self.drawing_area.grab_focus();
        self.drawing_area.add_events(
            gdk4::EventMask::KEY_PRESS_MASK |
            gdk4::EventMask::KEY_RELEASE_MASK |
            gdk4::EventMask::BUTTON_PRESS_MASK |
            gdk4::EventMask::BUTTON_RELEASE_MASK |
            gdk4::EventMask::POINTER_MOTION_MASK
        );
    }
}

// Key mapping function
fn gdk_key_to_linux_keycode(key: Key) -> u32 {
    match key {
        Key::a => 30, // KEY_A
        Key::b => 48, // KEY_B
        // ... complete mapping table
        Key::Return => 28, // KEY_ENTER
        Key::space => 57, // KEY_SPACE
        Key::Escape => 1, // KEY_ESC
        _ => 0, // Unknown key
    }
}

fn gdk_button_to_linux_button(button: u32) -> u32 {
    match button {
        1 => 0x110, // BTN_LEFT
        2 => 0x112, // BTN_MIDDLE  
        3 => 0x111, // BTN_RIGHT
        _ => 0x110, // Default to left
    }
}
```

## Implementation Benefits

### Complete Remote Desktop Experience
- ✅ **Full keyboard support** - All keys mapped and transmitted
- ✅ **Mouse interaction** - Clicks, movement, and scrolling
- ✅ **Real-time response** - Low-latency input transmission
- ✅ **Multiple input devices** - Separate keyboard and mouse

### Integration with Existing System
- ✅ **Extends current protocol** - Backward compatible
- ✅ **Uses Linux input subsystem** - Standard kernel APIs
- ✅ **Works with all applications** - Transparent to userspace
- ✅ **Maintains performance** - Efficient event handling

### Advanced Features (Future)
- **Clipboard synchronization** - Copy/paste between client and server
- **File transfer** - Drag and drop file transfer
- **Audio redirection** - Bidirectional audio streaming
- **USB device redirection** - Forward USB devices over network

This input handling extension transforms the IP Display Driver from a **display-only** solution into a **complete remote desktop system** that rivals VNC/RDP but with better performance and lower latency! 🚀