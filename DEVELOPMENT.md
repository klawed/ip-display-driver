# IP Display Driver - Development Guide

## Architecture Overview

The IP Display Driver creates a virtual display device that streams its output over a network. The system consists of two main components:

### 1. Kernel Module (DRM Driver)
- **Location**: `kernel/`
- **Language**: C
- **Purpose**: Creates a virtual DRM display device that applications can render to

#### Key Components:
- **ipdisp_main.c**: Module initialization and platform device management
- **ipdisp_drm.c**: DRM/KMS implementation with display pipeline
- **ipdisp_network.c**: TCP server for client connections
- **ipdisp_encoder.c**: Frame encoding and streaming workqueue

### 2. Display Client
- **Location**: `client/`
- **Language**: Rust with GTK4
- **Purpose**: Receives and displays the video stream from the kernel module

#### Key Components:
- **main.rs**: Application entry point and GTK setup
- **protocol.rs**: Network protocol implementation
- **network.rs**: TCP client and frame receiving
- **ui.rs**: GTK4 user interface
- **renderer.rs**: Cairo-based frame rendering

## Protocol Specification

### Packet Header (32 bytes)
```c
struct ipdisp_packet_header {
    u32 magic;      // 0x49504453 ("IPDS")
    u32 version;    // Protocol version (1)
    u32 width;      // Frame width
    u32 height;     // Frame height
    u32 format;     // Frame format (see enum)
    u64 timestamp;  // Frame timestamp (nanoseconds)
    u32 size;       // Data payload size
    u32 reserved;   // Reserved for future use
} __packed;
```

### Frame Formats
- **RGBA32** (0): 32-bit RGBA with alpha channel
- **RGB24** (1): 24-bit RGB without alpha
- **H264** (2): H.264 compressed video (future)
- **H265** (3): H.265 compressed video (future)

### Message Flow
1. Client connects to kernel module TCP server
2. Kernel sends display info packet (size=0)
3. Client receives display dimensions
4. Kernel sends frame data when display updates
5. Client renders received frames

## Building and Testing

### Prerequisites
```bash
# Kernel development
sudo apt install linux-headers-$(uname -r) build-essential

# Rust and GTK4
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
sudo apt install libgtk-4-dev libgstreamer1.0-dev pkg-config
```

### Build Process
```bash
# Build everything
chmod +x build.sh
./build.sh

# Or build individually
cd kernel && make
cd ../client && cargo build --release
```

### Installation
```bash
# Load kernel module
sudo insmod kernel/ipdisp.ko width=1920 height=1080 port=8080

# Check if loaded
lsmod | grep ipdisp
dmesg | grep ipdisp

# Check DRM device
ls -la /dev/dri/
```

### Running Client
```bash
# Basic usage
./client/target/release/ip-display-client --server 127.0.0.1 --port 8080

# Fullscreen mode
./client/target/release/ip-display-client --server 192.168.1.100 --fullscreen

# Custom window size
./client/target/release/ip-display-client --width 1024 --height 768
```

## Testing the Display

### 1. Using X11/Wayland
```bash
# Set display environment
export DISPLAY=:1  # For additional display

# Test with simple applications
xterm -display :1 &
firefox --display=:1 &
```

### 2. Using DRM/KMS directly
```bash
# List available displays
modetest -M ipdisp

# Set mode (requires root)
sudo modetest -M ipdisp -s <connector>:<mode>
```

### 3. Frame Buffer Test
```bash
# Find the framebuffer device
ls /dev/fb*

# Draw test pattern
sudo dd if=/dev/urandom of=/dev/fb1 bs=1024 count=1024
```

## Debugging

### Kernel Module Debug
```bash
# Enable debug messages
echo 8 > /proc/sys/kernel/printk

# View kernel logs
dmesg -w | grep ipdisp

# Module information
modinfo kernel/ipdisp.ko

# Check DRM registration
cat /proc/modules | grep ipdisp
cat /sys/class/drm/*/status
```

### Client Debug
```bash
# Enable Rust logging
RUST_LOG=debug ./client/target/release/ip-display-client

# Network debugging
RUST_LOG=ip_display_client::network=trace ./client/target/release/ip-display-client

# GTK debugging
GTK_DEBUG=interactive ./client/target/release/ip-display-client
```

### Network Testing
```bash
# Check if kernel module is listening
netstat -tlnp | grep 8080
ss -tlnp | grep 8080

# Test network connectivity
telnet 127.0.0.1 8080

# Monitor network traffic
sudo tcpdump -i lo port 8080
```

## Performance Considerations

### Kernel Module
- Frame buffer allocation uses DMA coherent memory
- Network transmission is non-blocking with client cleanup
- Work queue used for frame streaming to avoid blocking DRM operations

### Client
- Async Rust with Tokio for network operations
- Cairo surface rendering with hardware acceleration where available
- Frame scaling and centering for different window sizes

### Network Protocol
- TCP for reliable delivery
- Binary protocol with fixed-size headers
- Raw frame data transmission (compression planned for future)

## Common Issues

### 1. Module Load Failures
```bash
# Check kernel version compatibility
uname -r
ls /lib/modules/$(uname -r)/build

# Verify DRM support
grep CONFIG_DRM /boot/config-$(uname -r)
```

### 2. Client Connection Issues
```bash
# Check firewall
sudo ufw status
sudo iptables -L

# Check if module is listening
sudo netstat -tlnp | grep ipdisp
```

### 3. Display Issues
```bash
# Check DRM device permissions
ls -la /dev/dri/
groups $USER

# Add user to video group if needed
sudo usermod -a -G video $USER
```

## Future Enhancements

### Planned Features
- [ ] H.264/H.265 hardware encoding support
- [ ] Audio streaming integration
- [ ] Multi-client broadcasting
- [ ] Authentication and encryption
- [ ] Dynamic resolution switching
- [ ] Hardware cursor support
- [ ] Direct rendering optimizations

### Performance Improvements
- [ ] Zero-copy buffer sharing
- [ ] GPU-accelerated encoding
- [ ] Adaptive bitrate streaming
- [ ] Frame dropping for network congestion
- [ ] Client-side frame interpolation

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature-name`
3. Make your changes with proper commit messages
4. Test thoroughly on different kernel versions
5. Submit a pull request with detailed description

## License

- **Kernel Module**: GPL v2 (required for kernel modules)
- **Rust Client**: MIT License

## References

- [Linux DRM/KMS Documentation](https://docs.kernel.org/gpu/drm-kms.html)
- [GTK4 Documentation](https://docs.gtk.org/gtk4/)
- [Rust Async Programming](https://rust-lang.github.io/async-book/)
- [Cairo Graphics Library](https://www.cairographics.org/documentation/)
