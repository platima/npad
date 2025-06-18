#!/bin/bash
# npad development environment setup for Linux
# Enhanced for better distribution compatibility

set -e

echo "==== npad Development Environment Setup ===="
echo "Detected OS: $(uname -s) $(uname -r)"

# Function to detect distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID $VERSION_ID"
    elif command -v lsb_release &> /dev/null; then
        echo "$(lsb_release -si) $(lsb_release -sr)"
    else
        echo "unknown unknown"
    fi
}

DISTRO_INFO=$(detect_distro)
echo "Distribution: $DISTRO_INFO"

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "Warning: Running as root. Consider running as normal user with sudo when needed."
fi

echo ""
echo "==== Installing Build Dependencies ===="

# Update package lists
if command -v apt-get &> /dev/null; then
    echo "Updating apt package lists..."
    sudo apt-get update
    
    # Core build tools
    echo "Installing core build tools..."
    sudo apt-get install -y build-essential gcc make
    
    # Cross-compilation for Windows
    echo "Installing MinGW for Windows cross-compilation..."
    sudo apt-get install -y gcc-mingw-w64 gcc-mingw-w64-x86-64
    
    # Linux GUI development
    echo "Installing X11 development libraries..."
    sudo apt-get install -y libx11-dev
    
    # Terminal development (ncurses)
    echo "Installing ncurses development..."
    sudo apt-get install -y libncurses5-dev libncurses5

elif command -v dnf &> /dev/null; then
    echo "Using dnf (Fedora/RHEL)..."
    sudo dnf install -y gcc make mingw64-gcc libX11-devel ncurses-devel
    
elif command -v yum &> /dev/null; then
    echo "Using yum (CentOS/RHEL)..."
    sudo yum install -y gcc make mingw64-gcc libX11-devel ncurses-devel
    
elif command -v pacman &> /dev/null; then
    echo "Using pacman (Arch Linux)..."
    sudo pacman -S gcc make mingw-w64-gcc libx11 ncurses
    
else
    echo "Warning: Unknown package manager. Please install build dependencies manually:"
    echo "- gcc, make, build-essential"
    echo "- mingw-w64 (for Windows cross-compilation)"
    echo "- libx11-dev (for Linux GUI)"
    echo "- libncurses5-dev (for terminal UI)"
fi

echo ""
echo "==== Installing Code Quality Tools ===="

# Function to find and install clang-format
install_clang_format() {
    local available_versions="16 15 14 13 12 11 10"
    local installed=false
    
    for version in $available_versions; do
        if command -v apt-cache &> /dev/null; then
            if apt-cache show "clang-format-$version" &> /dev/null; then
                echo "Installing clang-format-$version..."
                sudo apt-get install -y "clang-format-$version"
                # Create symlink if clang-format doesn't exist
                if ! command -v clang-format &> /dev/null; then
                    sudo ln -sf "/usr/bin/clang-format-$version" /usr/bin/clang-format
                fi
                installed=true
                break
            fi
        fi
    done
    
    if [ "$installed" = false ]; then
        echo "Trying to install generic clang-format package..."
        if command -v apt-get &> /dev/null; then
            sudo apt-get install -y clang-format || echo "Warning: Could not install clang-format"
        fi
    fi
}

# Function to install cppcheck
install_cppcheck() {
    if command -v apt-get &> /dev/null; then
        sudo apt-get install -y cppcheck
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y cppcheck
    elif command -v yum &> /dev/null; then
        sudo yum install -y cppcheck
    elif command -v pacman &> /dev/null; then
        sudo pacman -S cppcheck
    else
        echo "Warning: Could not install cppcheck with available package manager"
    fi
}

install_clang_format
install_cppcheck

echo ""
echo "==== Installing Additional Development Tools ===="

if command -v apt-get &> /dev/null; then
    # Git (if not already installed)
    sudo apt-get install -y git
    
    # Useful development tools
    sudo apt-get install -y vim nano curl wget
    
    # Optional: Install VS Code if not present (uncomment if desired)
    # if ! command -v code &> /dev/null; then
    #     echo "Installing VS Code..."
    #     wget -qO- https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor > packages.microsoft.gpg
    #     sudo install -o root -g root -m 644 packages.microsoft.gpg /etc/apt/trusted.gpg.d/
    #     sudo sh -c 'echo "deb [arch=amd64,arm64,armhf signed-by=/etc/apt/trusted.gpg.d/packages.microsoft.gpg] https://packages.microsoft.com/repos/code stable main" > /etc/apt/sources.list.d/vscode.list'
    #     sudo apt-get update
    #     sudo apt-get install -y code
    # fi
fi

echo ""
echo "==== Verifying Installation ===="

# Verify core tools
echo "Checking installed tools:"
echo -n "gcc: "
if command -v gcc &> /dev/null; then
    gcc --version | head -n1
else
    echo "NOT FOUND"
fi

echo -n "make: "
if command -v make &> /dev/null; then
    make --version | head -n1
else
    echo "NOT FOUND"
fi

echo -n "mingw64-gcc: "
if command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    x86_64-w64-mingw32-gcc --version | head -n1
else
    echo "NOT FOUND (Windows cross-compilation unavailable)"
fi

echo -n "clang-format: "
if command -v clang-format &> /dev/null; then
    clang-format --version
else
    echo "NOT FOUND (code formatting unavailable)"
fi

echo -n "cppcheck: "
if command -v cppcheck &> /dev/null; then
    cppcheck --version
else
    echo "NOT FOUND (static analysis unavailable)"
fi

echo ""
echo "==== Testing Build System ===="

# Test if we can build
if [ -f "Makefile" ]; then
    echo "Testing build system..."
    
    echo "Available make targets:"
    make help | grep -E "^  [a-z]" | head -5
    
    echo ""
    echo "You can now build npad with:"
    echo "  make windows     # Build Windows version (cross-compile)"
    echo "  make linux       # Build Linux versions"
    echo "  make debug       # Build with debug symbols"
    echo "  make test        # Run tests"
    echo "  make lint        # Run code quality checks"
    echo "  make format      # Format code"
else
    echo "Warning: Makefile not found. Make sure you're in the npad project directory."
fi

echo ""
echo "==== Setup Complete ===="
echo "Development environment is ready!"
echo ""
echo "Quick start:"
echo "1. make windows        # Build Windows executable"
echo "2. make linux          # Build Linux executables"
echo "3. make test           # Run test suite"
echo "4. make lint           # Check code quality"
echo ""
echo "For more options, run: make help"