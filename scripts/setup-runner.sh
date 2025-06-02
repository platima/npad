#!/bin/bash

# npad - GitHub Runner Setup Script
# Sets up a Linux Mint/Ubuntu system for building npad
# Run this ONCE on your self-hosted runner to install all dependencies
# This should be run manually, NOT in CI/CD workflows

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_section() {
    echo
    echo -e "${BLUE}==== $1 ====${NC}"
}

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    print_error "Do not run this script as root. It will use sudo when needed."
    exit 1
fi

print_section "npad Build Environment Setup"

# Check OS
if ! command -v apt-get &> /dev/null; then
    print_error "This script is designed for Debian/Ubuntu-based systems (like Linux Mint)"
    exit 1
fi

print_status "Detected Debian/Ubuntu-based system"

# Update package lists
print_section "Updating Package Lists"
sudo apt-get update

# Install basic build tools
print_section "Installing Basic Build Tools"
sudo apt-get install -y \
    build-essential \
    make \
    git \
    curl \
    wget

# Install cross-compilation tools for Windows
print_section "Installing Windows Cross-Compilation Tools"
sudo apt-get install -y \
    gcc-mingw-w64 \
    gcc-mingw-w64-x86-64 \
    gcc-mingw-w64-i686

# Install Linux development dependencies
print_section "Installing Linux Development Dependencies"
sudo apt-get install -y \
    libx11-dev \
    libxext-dev \
    libxft-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxrender-dev \
    libxfixes-dev \
    libpng-dev \
    libjpeg-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev

# Install code quality tools
print_section "Installing Code Quality Tools"
sudo apt-get install -y \
    cppcheck \
    clang-format \
    clang-format-14 \
    valgrind

# Install ncurses for terminal version
print_section "Installing ncurses Development Libraries"
sudo apt-get install -y \
    libncurses-dev \
    libncursesw5-dev

# Verify installations
print_section "Verifying Installations"

check_command() {
    if command -v "$1" &> /dev/null; then
        print_status "$1: $(command -v $1)"
    else
        print_error "$1: Not found"
        return 1
    fi
}

check_file() {
    if [ -f "$1" ]; then
        print_status "$1: Found"
    else
        print_error "$1: Not found"
        return 1
    fi
}

# Check compilers
print_status "Checking compilers..."
check_command gcc
check_command x86_64-w64-mingw32-gcc
check_command i686-w64-mingw32-gcc

# Check development libraries
print_status "Checking development libraries..."
check_file /usr/include/X11/Xlib.h
check_file /usr/include/ncurses.h

# Check code quality tools
print_status "Checking code quality tools..."
check_command cppcheck
check_command clang-format

# Test cross-compilation
print_section "Testing Cross-Compilation"

# Create a simple test program
TEST_DIR=$(mktemp -d)
cat > "$TEST_DIR/test.c" << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello, World!\n");
    return 0;
}
EOF

print_status "Testing native compilation..."
if gcc -o "$TEST_DIR/test-native" "$TEST_DIR/test.c"; then
    print_status "Native compilation: OK"
else
    print_error "Native compilation: FAILED"
fi

print_status "Testing Windows x64 cross-compilation..."
if x86_64-w64-mingw32-gcc -o "$TEST_DIR/test-win64.exe" "$TEST_DIR/test.c"; then
    print_status "Windows x64 cross-compilation: OK"
else
    print_error "Windows x64 cross-compilation: FAILED"
fi

print_status "Testing Windows x86 cross-compilation..."
if i686-w64-mingw32-gcc -o "$TEST_DIR/test-win32.exe" "$TEST_DIR/test.c"; then
    print_status "Windows x86 cross-compilation: OK"
else
    print_error "Windows x86 cross-compilation: FAILED"
fi

# Cleanup test
rm -rf "$TEST_DIR"

print_section "Setup Complete!"
print_status "Your system is now ready for npad development and CI/CD"
print_status ""
print_status "To test the build system:"
print_status "  git clone https://github.com/platima/npad.git"
print_status "  cd npad"
print_status "  make help"
print_status "  make windows-x64"
print_status "  make linux"
print_status ""
print_status "For code quality:"
print_status "  make lint"
print_status "  make format-check"

echo
print_status "🎉 Setup completed successfully!"