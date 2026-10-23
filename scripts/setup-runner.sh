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
    echo "Installing GTK 3 development libraries..."
    sudo apt-get install -y pkg-config libgtk-3-dev
    
    # Terminal development (ncurses; libncurses5-dev is gone from current
    # Debian and Ubuntu, libncurses-dev is the name everywhere)
    echo "Installing ncurses development..."
    sudo apt-get install -y libncurses-dev

    # Linux packaging: dpkg-deb (the .deb fallback without nfpm) and zstd
    # (release validation reads the Arch package); nfpm itself is below
    sudo apt-get install -y dpkg-dev zstd

elif command -v dnf &> /dev/null; then
    echo "Using dnf (Fedora/RHEL)..."
    sudo dnf install -y gcc make mingw64-gcc pkgconf-pkg-config gtk3-devel ncurses-devel
    
elif command -v yum &> /dev/null; then
    echo "Using yum (CentOS/RHEL)..."
    sudo yum install -y gcc make mingw64-gcc pkgconfig gtk3-devel ncurses-devel
    
elif command -v pacman &> /dev/null; then
    echo "Using pacman (Arch Linux)..."
    sudo pacman -S gcc make mingw-w64-gcc pkgconf gtk3 ncurses
    
else
    echo "Warning: Unknown package manager. Please install build dependencies manually:"
    echo "- gcc, make, build-essential"
    echo "- mingw-w64 (for Windows cross-compilation)"
    echo "- pkg-config and GTK 3 development files, e.g. libgtk-3-dev (for the Linux desktop app)"
    echo "- libncurses-dev (for terminal UI)"
fi

echo ""
echo "==== Installing Code Quality Tools ===="

# The canonical tool versions: `make format-check` is canonical against
# clang-format 18 and `make lint` against cppcheck 2.13, what CI runs (ci.yml
# pins ubuntu-24.04 for them and checks the versions). Other versions format
# and warn differently, so a local pass with them proves little.
CANONICAL_CLANG_FORMAT=18
CANONICAL_CPPCHECK=2.13

# Install clang-format 18 if the distro has it; otherwise the nearest version,
# with a warning and the way to get 18 anyway
install_clang_format() {
    local fallback_versions="19 17 20 16 15 14"
    local installed=""

    if command -v apt-cache &> /dev/null; then
        for version in $CANONICAL_CLANG_FORMAT $fallback_versions; do
            if apt-cache show "clang-format-$version" &> /dev/null; then
                echo "Installing clang-format-$version..."
                sudo apt-get install -y "clang-format-$version"
                installed=$version
                break
            fi
        done
        if [ -z "$installed" ]; then
            echo "Trying to install the generic clang-format package..."
            sudo apt-get install -y clang-format || echo "Warning: Could not install clang-format"
        fi
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y clang-tools-extra || echo "Warning: Could not install clang-format"
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --needed clang || echo "Warning: Could not install clang-format"
    fi

    # A versioned binary is used through CLANG_FORMAT rather than by replacing
    # /usr/bin/clang-format, which other projects on this machine may rely on
    if [ -n "$installed" ] && ! command -v clang-format &> /dev/null; then
        sudo ln -sf "/usr/bin/clang-format-$installed" /usr/bin/clang-format
    fi
    if [ "$installed" != "$CANONICAL_CLANG_FORMAT" ]; then
        echo "Warning: clang-format $CANONICAL_CLANG_FORMAT is the canonical version and this distro does not package it."
        echo "         Install it with: pip install --user clang-format==18.1.8"
        echo "         then run: CLANG_FORMAT=~/.local/bin/clang-format make format-check"
    elif [ "$(readlink -f "$(command -v clang-format)")" != "$(readlink -f /usr/bin/clang-format-$installed)" ]; then
        echo "Note: 'clang-format' on PATH is not version $CANONICAL_CLANG_FORMAT; run: CLANG_FORMAT=clang-format-$installed make format-check"
    fi
}

# nfpm (the .deb, .rpm and Arch packages) and llvm-mingw (the Windows on Arm
# build), the same pinned, checksum-verified releases CI installs
install_packaging_tools() {
    case $(uname -m) in
    x86_64 | amd64 | aarch64 | arm64)
        if ! command -v nfpm &> /dev/null; then
            echo "Installing nfpm (pinned, checksum-verified)..."
            sudo sh scripts/install-nfpm.sh || echo "Warning: Could not install nfpm (make deb falls back to dpkg-deb)"
        fi
        ;;
    esac
    case $(uname -m) in
    x86_64 | amd64)
        if [ ! -x /opt/llvm-mingw/bin/aarch64-w64-mingw32-clang ]; then
            echo "Installing llvm-mingw to /opt/llvm-mingw (pinned, checksum-verified)..."
            sudo sh scripts/install-llvm-mingw.sh /opt/llvm-mingw || echo "Warning: Could not install llvm-mingw (make windows-arm64 unavailable)"
        fi
        echo "For make windows-arm64, put /opt/llvm-mingw/bin at the END of PATH (PATH=\"\$PATH:/opt/llvm-mingw/bin\"):"
        echo "ahead of the MinGW GCC tools, its dlltool replaces GNU dlltool and make windows fails."
        ;;
    *)
        echo "Note: llvm-mingw is pinned for x86_64 hosts only; make windows-arm64 is unavailable here"
        ;;
    esac
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
if command -v cppcheck &> /dev/null; then
    if ! cppcheck --version | grep -q "^Cppcheck $CANONICAL_CPPCHECK"; then
        echo "Warning: $(cppcheck --version) is not cppcheck $CANONICAL_CPPCHECK, which CI runs."
        echo "         Older versions are less strict and have passed changes CI then rejected:"
        echo "         build $CANONICAL_CPPCHECK from source and run: CPPCHECK=/path/to/cppcheck make lint"
    fi
fi
if [ -f scripts/install-nfpm.sh ]; then
    install_packaging_tools
fi

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