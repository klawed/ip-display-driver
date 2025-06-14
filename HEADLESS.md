# Headless Server Setup Guide

## Overview

The IP Display Driver can be configured to load at boot time, making it perfect for headless servers that need remote display capabilities. This enables:

- Remote desktop access to servers without physical displays
- Virtual display for automated testing and CI/CD
- Remote GUI access to headless cloud instances
- Development environments on remote servers

## Boot-time Loading Methods

### Method 1: systemd Module Loading (Recommended)

Create a systemd configuration to load the module at boot:

```bash
# Create module configuration
sudo tee /etc/modules-load.d/ipdisp.conf << EOF
ipdisp
EOF

# Create module parameters
sudo tee /etc/modprobe.d/ipdisp.conf << EOF
# IP Display Driver Configuration
options ipdisp width=1920 height=1080 port=8080 codec=raw
EOF

# Copy module to system location
sudo mkdir -p /lib/modules/$(uname -r)/extra
sudo cp kernel/ipdisp.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a

# Enable loading at boot
sudo systemctl enable systemd-modules-load.service
```

### Method 2: initramfs Integration

For earlier loading during boot process:

```bash
# Copy module to initramfs
sudo mkdir -p /etc/initramfs-tools/modules.d
echo "ipdisp width=1920 height=1080 port=8080" | sudo tee /etc/initramfs-tools/modules.d/ipdisp

# Copy module files
sudo cp kernel/ipdisp.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a

# Update initramfs
sudo update-initramfs -u
```

### Method 3: Traditional /etc/modules

For older systems:

```bash
# Add to /etc/modules
echo "ipdisp" | sudo tee -a /etc/modules

# Set parameters
sudo tee /etc/modprobe.d/ipdisp.conf << EOF
options ipdisp width=1920 height=1080 port=8080
EOF
```

## Display Manager Integration

### Configure X11 for Virtual Display

Create X11 configuration to use the IP display:

```bash
# Create X11 configuration
sudo tee /etc/X11/xorg.conf.d/20-ipdisp.conf << EOF
Section "Device"
    Identifier "ipdisp"
    Driver "modesetting"
    Option "kmsdev" "/dev/dri/card0"
    BusID "platform:ipdisp"
EndSection

Section "Monitor"
    Identifier "ipdisp-monitor"
    Option "DPMS" "false"
EndSection

Section "Screen"
    Identifier "ipdisp-screen"
    Device "ipdisp"
    Monitor "ipdisp-monitor"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Modes "1920x1080" "1680x1050" "1280x1024" "1024x768"
    EndSubSection
EndSection

Section "ServerLayout"
    Identifier "headless"
    Screen "ipdisp-screen"
    Option "DontVTSwitch" "true"
    Option "AllowMouseOpenFail" "true"
    Option "PciForceNone" "true"
    Option "AutoEnableDevices" "false"
    Option "AutoAddDevices" "false"
EndSection
EOF
```

### systemd Display Service

Create a systemd service for automatic display startup:

```bash
sudo tee /etc/systemd/system/ipdisp-display.service << EOF
[Unit]
Description=IP Display Virtual Desktop
After=graphical.target
Wants=graphical.target

[Service]
Type=simple
User=root
ExecStartPre=/sbin/modprobe ipdisp width=1920 height=1080 port=8080
ExecStart=/usr/bin/X :1 -config /etc/X11/xorg.conf.d/20-ipdisp.conf -nolisten tcp
Restart=always
RestartSec=5

[Install]
WantedBy=graphical.target
EOF

# Enable the service
sudo systemctl enable ipdisp-display.service
```

### Desktop Environment Service

For automatic desktop environment:

```bash
sudo tee /etc/systemd/system/ipdisp-desktop.service << EOF
[Unit]
Description=IP Display Desktop Environment
After=ipdisp-display.service
Requires=ipdisp-display.service

[Service]
Type=simple
User=displayuser
Group=displayuser
Environment=DISPLAY=:1
Environment=XDG_RUNTIME_DIR=/run/user/1001
ExecStart=/usr/bin/startxfce4
Restart=always
RestartSec=10

[Install]
WantedBy=graphical.target
EOF

# Create display user
sudo useradd -m -s /bin/bash displayuser
sudo usermod -a -G video,audio displayuser

# Enable desktop service
sudo systemctl enable ipdisp-desktop.service
```

## Network Configuration

### Firewall Setup

```bash
# Allow IP display port
sudo ufw allow 8080/tcp
sudo ufw reload

# Or with iptables
sudo iptables -A INPUT -p tcp --dport 8080 -j ACCEPT
sudo iptables-save > /etc/iptables/rules.v4
```

### Systemd Network Service

Create a network announcement service:

```bash
sudo tee /etc/systemd/system/ipdisp-announce.service << EOF
[Unit]
Description=IP Display Network Announcer
After=network.target ipdisp-display.service

[Service]
Type=simple
ExecStart=/bin/bash -c 'while true; do echo "IP Display available at $(hostname -I | cut -d" " -f1):8080" | logger -t ipdisp-announce; sleep 60; done'
Restart=always

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl enable ipdisp-announce.service
```

## Complete Installation Script

Create an automated installation script:

```bash
sudo tee /usr/local/bin/install-ipdisp-headless.sh << 'EOF'
#!/bin/bash
set -e

IPDISP_WIDTH=${1:-1920}
IPDISP_HEIGHT=${2:-1080}  
IPDISP_PORT=${3:-8080}
DISPLAY_NUM=${4:-1}

echo "Installing IP Display Driver for headless server..."
echo "Resolution: ${IPDISP_WIDTH}x${IPDISP_HEIGHT}"
echo "Port: ${IPDISP_PORT}"
echo "Display: :${DISPLAY_NUM}"

# Install kernel module
if [ ! -f "/lib/modules/$(uname -r)/extra/ipdisp.ko" ]; then
    echo "Error: ipdisp.ko not found. Please build and copy it first."
    exit 1
fi

# Configure module loading
cat > /etc/modules-load.d/ipdisp.conf << EOL
ipdisp
EOL

cat > /etc/modprobe.d/ipdisp.conf << EOL
options ipdisp width=${IPDISP_WIDTH} height=${IPDISP_HEIGHT} port=${IPDISP_PORT}
EOL

# Configure X11
mkdir -p /etc/X11/xorg.conf.d
cat > /etc/X11/xorg.conf.d/20-ipdisp.conf << EOL
Section "Device"
    Identifier "ipdisp"
    Driver "modesetting"
    Option "kmsdev" "/dev/dri/card0"
EndSection

Section "Monitor"
    Identifier "ipdisp-monitor"
    Option "DPMS" "false"
EndSection

Section "Screen"
    Identifier "ipdisp-screen"
    Device "ipdisp"
    Monitor "ipdisp-monitor"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Modes "${IPDISP_WIDTH}x${IPDISP_HEIGHT}"
    EndSubSection
EndSection
EOL

# Create display user if not exists
if ! id "displayuser" &>/dev/null; then
    useradd -m -s /bin/bash displayuser
    usermod -a -G video,audio displayuser
fi

# Configure systemd services
cat > /etc/systemd/system/ipdisp-display.service << EOL
[Unit]
Description=IP Display Virtual Desktop
After=multi-user.target
Wants=multi-user.target

[Service]
Type=simple
User=root
ExecStartPre=/sbin/modprobe ipdisp
ExecStart=/usr/bin/X :${DISPLAY_NUM} -config /etc/X11/xorg.conf.d/20-ipdisp.conf -nolisten tcp
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOL

# Enable services
systemctl daemon-reload
systemctl enable ipdisp-display.service

# Configure firewall
if command -v ufw &> /dev/null; then
    ufw allow ${IPDISP_PORT}/tcp
fi

echo "Installation complete!"
echo "Reboot to start the virtual display server."
echo "Connect with: ip-display-client --server <server-ip> --port ${IPDISP_PORT}"
EOF

sudo chmod +x /usr/local/bin/install-ipdisp-headless.sh
```

## Usage Examples

### Basic Headless Server Setup

```bash
# Install and configure
sudo /usr/local/bin/install-ipdisp-headless.sh 1920 1080 8080

# Reboot to activate
sudo reboot

# After reboot, check status
systemctl status ipdisp-display.service
lsmod | grep ipdisp
ls -la /dev/dri/
```

### Remote Desktop with VNC Alternative

The IP display can serve as a much more efficient alternative to VNC:

```bash
# Server side (automatic after setup)
# Module loads at boot, X11 starts on :1

# Client side - connect from any machine
./ip-display-client --server 192.168.1.100 --port 8080 --fullscreen

# Multiple clients can connect simultaneously
./ip-display-client --server 192.168.1.100 --port 8080 --width 1280 --height 720
```

### Cloud Instance Setup

Perfect for cloud instances without display hardware:

```bash
# On AWS/GCP/Azure instance
sudo /usr/local/bin/install-ipdisp-headless.sh
sudo reboot

# From local machine
./ip-display-client --server <cloud-instance-ip> --port 8080
```

## Advantages over Traditional Solutions

### vs VNC/RDP
- **Lower latency**: Direct kernel-to-network streaming
- **Better performance**: No additional encoding/compression overhead  
- **Multiple clients**: Native support for concurrent viewers
- **No authentication complexity**: Simple TCP connection

### vs Physical Display
- **No hardware required**: Pure software solution
- **Network accessible**: Remote access from anywhere
- **Scalable**: Multiple virtual displays possible
- **Cost effective**: No additional hardware costs

### vs Xvfb/Virtual Framebuffer
- **Real DRM device**: Works with modern applications expecting GPU
- **Better compatibility**: Full DRM/KMS compliance
- **Hardware acceleration ready**: Can integrate with GPU when available
- **Live streaming**: Real-time remote access vs offline rendering

The IP display driver provides a modern, efficient solution for headless servers that need remote display capabilities!