# IP Display Driver

A Linux kernel module that creates a virtual display device accessible over IP networks. This allows you to create remote displays that can be accessed by network clients.

## Architecture

```
┌─────────────────┐    Network    ┌─────────────────┐
│   Source Host   │◄─────────────►│   Client Host   │
│                 │               │                 │
│ ┌─────────────┐ │               │ ┌─────────────┐ │
│ │ Application │ │               │ │   Display   │ │
│ │ (Wayland/X) │ │               │ │   Client    │ │
│ └─────────────┘ │               │ │ (Rust/GTK)  │ │
│        │        │               │ └─────────────┘ │
│ ┌─────────────┐ │               │                 │
│ │ DRM Driver  │ │   Video       │                 │
│ │ (ipdisp.ko) │◄┼───Stream──────┤                 │
│ │     (C)     │ │               │                 │
│ └─────────────┘ │               │                 │
└─────────────────┘               └─────────────────┘
```

## Components

### Kernel Module (DRM Driver)
- **Location**: `kernel/`
- **Language**: C
- **Purpose**: Creates a virtual DRM display device
- **Features**: 
  - DRM/KMS compliance
  - Network streaming capabilities
  - Hardware acceleration support
  - Multiple display modes

### Display Client
- **Location**: `client/`
- **Language**: Rust with GTK4
- **Purpose**: Receives and displays the video stream
- **Features**:
  - Real-time video decoding
  - Multiple connection support
  - GTK4 modern UI
  - Hardware acceleration

### Protocol
- **Transport**: TCP/UDP
- **Encoding**: H.264/H.265 (configurable)
- **Compression**: Real-time optimized

## Building

### Prerequisites
```bash
# For kernel module
sudo apt install linux-headers-$(uname -r) build-essential

# For Rust client
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
sudo apt install libgtk-4-dev libgstreamer1.0-dev
```

### Kernel Module
```bash
cd kernel
make
sudo insmod ipdisp.ko
```

### Rust Client
```bash
cd client
cargo build --release
./target/release/ip-display-client
```

## Usage

1. **Load the kernel module**:
   ```bash
   sudo insmod kernel/ipdisp.ko
   ```

2. **Configure display**:
   ```bash
   # The driver will create /dev/dri/cardN
   # Configure via DRM properties
   ```

3. **Start client**:
   ```bash
   ./client/target/release/ip-display-client --server <ip> --port <port>
   ```

## Configuration

### Module Parameters
- `width`: Display width (default: 1920)
- `height`: Display height (default: 1080)
- `port`: Network port (default: 8080)
- `codec`: Video codec (h264, h265)

### Client Options
- `--server`: Server IP address
- `--port`: Server port
- `--fullscreen`: Start in fullscreen mode
- `--vsync`: Enable vertical sync

## Protocol Specification

The IP Display Protocol (IDP) is a custom protocol for streaming display data:

```
Header: [Magic][Version][Width][Height][Format][Timestamp]
Data: [Compressed Frame Data]
```

## Development

### Testing
```bash
# Run kernel module tests
cd kernel && make test

# Run client tests
cd client && cargo test
```

### Debugging
```bash
# Kernel logs
sudo dmesg | grep ipdisp

# Client debug mode
RUST_LOG=debug ./target/release/ip-display-client
```

## License

GPL v2 (Kernel module) / MIT (Client application)

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## Roadmap

- [x] Basic DRM driver implementation
- [x] Network streaming protocol
- [x] Rust GTK4 client
- [ ] Hardware acceleration
- [ ] Audio streaming
- [ ] Multi-client support
- [ ] Authentication/encryption
- [ ] Performance optimizations
