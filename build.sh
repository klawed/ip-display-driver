#!/bin/bash
# Build script for IP Display Driver

set -e

echo "Building IP Display Driver..."

# Build kernel module
echo "Building kernel module..."
cd kernel
make clean
make

echo "Kernel module built successfully"

# Build Rust client
echo "Building Rust client..."
cd ../client
cargo build --release

echo "Rust client built successfully"

echo "Build complete!"
echo ""
echo "To install:"
echo "  sudo insmod kernel/ipdisp.ko"
echo ""
echo "To run client:"
echo "  ./client/target/release/ip-display-client --server <ip>"
