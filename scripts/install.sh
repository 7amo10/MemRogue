#!/bin/bash
#
# MemRogue Installation Script
# 
# This script provides flexible installation options for MemRogue:
# - System-wide installation (requires sudo)
# - User-local installation (~/.local)
# - Custom prefix installation
#
# Usage:
#   ./install.sh                    # Interactive mode
#   ./install.sh --system           # System-wide to /usr/local
#   ./install.sh --user             # User-local to ~/.local
#   ./install.sh --prefix=/opt/memrogue  # Custom prefix
#   ./install.sh --help             # Show help
#
# Supports: Debian/Ubuntu, RHEL/CentOS/Fedora, Arch Linux, Alpine, and other Linux distros
#

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default values
PREFIX=""
INSTALL_MODE=""
BUILD_DIR="build"
VERBOSE=0
FORCE=0

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# ============================================================================
# Helper Functions
# ============================================================================

print_banner() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║                    MemRogue Installation                         ║"
    echo "║            Lightweight Memory Debugging for C/C++                ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Installation Modes:"
    echo "  --system              Install system-wide to /usr/local (requires sudo)"
    echo "  --user                Install to ~/.local (no sudo required)"
    echo "  --prefix=PATH         Install to custom prefix"
    echo ""
    echo "Options:"
    echo "  --build-dir=DIR       Specify build directory (default: build)"
    echo "  --rebuild             Force rebuild even if build exists"
    echo "  --force               Overwrite existing installation"
    echo "  --verbose             Enable verbose output"
    echo "  -h, --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --system           # Install to /usr/local (sudo)"
    echo "  $0 --user             # Install to ~/.local"
    echo "  $0 --prefix=/opt/memrogue"
    echo ""
    echo "After installation, use MemRogue with:"
    echo "  memrogue ./your_application"
    echo "  # or"
    echo "  LD_PRELOAD=libmemrogue_intercept.so ./your_application"
}

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO_ID="$ID"
        DISTRO_NAME="$NAME"
        DISTRO_VERSION="$VERSION_ID"
    elif [ -f /etc/lsb-release ]; then
        . /etc/lsb-release
        DISTRO_ID="$DISTRIB_ID"
        DISTRO_NAME="$DISTRIB_DESCRIPTION"
        DISTRO_VERSION="$DISTRIB_RELEASE"
    else
        DISTRO_ID="unknown"
        DISTRO_NAME="Unknown Linux"
        DISTRO_VERSION=""
    fi
    
    log_info "Detected distribution: $DISTRO_NAME ($DISTRO_ID)"
}

check_dependencies() {
    log_info "Checking dependencies..."
    
    local missing_deps=()
    
    # Check for required tools
    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi
    
    if ! command -v make &> /dev/null; then
        missing_deps+=("make")
    fi
    
    if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
        missing_deps+=("gcc or clang")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Missing required dependencies: ${missing_deps[*]}"
        echo ""
        echo "Install dependencies based on your distribution:"
        echo ""
        case "$DISTRO_ID" in
            ubuntu|debian|linuxmint|pop)
                echo "  sudo apt update && sudo apt install -y build-essential cmake"
                ;;
            fedora)
                echo "  sudo dnf install -y gcc gcc-c++ make cmake"
                ;;
            centos|rhel|rocky|almalinux)
                echo "  sudo yum install -y gcc gcc-c++ make cmake"
                ;;
            arch|manjaro)
                echo "  sudo pacman -S --noconfirm base-devel cmake"
                ;;
            alpine)
                echo "  sudo apk add build-base cmake"
                ;;
            opensuse*|sles)
                echo "  sudo zypper install -y gcc gcc-c++ make cmake"
                ;;
            *)
                echo "  Please install: gcc, make, cmake"
                ;;
        esac
        exit 1
    fi
    
    log_success "All dependencies satisfied"
}

select_install_mode() {
    if [ -n "$INSTALL_MODE" ]; then
        return
    fi
    
    echo ""
    echo "Select installation mode:"
    echo ""
    echo "  1) System-wide (/usr/local) - Requires sudo, available to all users"
    echo "  2) User-local (~/.local)    - No sudo required, current user only"
    echo "  3) Custom prefix            - Specify your own installation path"
    echo ""
    read -p "Enter choice [1-3]: " choice
    
    case "$choice" in
        1)
            INSTALL_MODE="system"
            PREFIX="/usr/local"
            ;;
        2)
            INSTALL_MODE="user"
            PREFIX="$HOME/.local"
            ;;
        3)
            read -p "Enter installation prefix: " PREFIX
            INSTALL_MODE="custom"
            ;;
        *)
            log_error "Invalid choice"
            exit 1
            ;;
    esac
}

build_memrogue() {
    log_info "Building MemRogue..."
    
    cd "$PROJECT_ROOT"
    
    # Create or clean build directory
    if [ -d "$BUILD_DIR" ] && [ "$FORCE" -eq 1 ]; then
        log_info "Cleaning existing build directory..."
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure with CMake
    log_info "Configuring with CMake..."
    local cmake_opts=(
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
        -DMEMROGUE_BUILD_TESTS=OFF
        -DMEMROGUE_BUILD_EXAMPLES=OFF
        -DMEMROGUE_BUILD_BENCHMARKS=OFF
    )
    
    if [ "$VERBOSE" -eq 1 ]; then
        cmake "${cmake_opts[@]}" ..
    else
        cmake "${cmake_opts[@]}" .. > /dev/null 2>&1
    fi
    
    # Build
    log_info "Compiling..."
    local nproc_val
    nproc_val=$(nproc 2>/dev/null || echo 4)
    
    if [ "$VERBOSE" -eq 1 ]; then
        make -j"$nproc_val"
    else
        make -j"$nproc_val" > /dev/null 2>&1
    fi
    
    log_success "Build completed successfully"
}

install_memrogue() {
    log_info "Installing MemRogue to $PREFIX..."
    
    cd "$PROJECT_ROOT/$BUILD_DIR"
    
    # Check if we need sudo
    local use_sudo=""
    if [ "$INSTALL_MODE" = "system" ]; then
        if [ "$EUID" -ne 0 ]; then
            use_sudo="sudo"
            log_info "Requesting sudo privileges for system-wide installation..."
        fi
    fi
    
    # Create directories
    $use_sudo mkdir -p "$PREFIX/lib"
    $use_sudo mkdir -p "$PREFIX/bin"
    $use_sudo mkdir -p "$PREFIX/include/memrogue"
    $use_sudo mkdir -p "$PREFIX/share/memrogue"
    $use_sudo mkdir -p "$PREFIX/lib/pkgconfig"
    
    # Install libraries
    log_info "Installing libraries..."
    $use_sudo cp -P lib/libmemrogue_core.* "$PREFIX/lib/" 2>/dev/null || true
    $use_sudo cp -P lib/libmemrogue_intercept.* "$PREFIX/lib/" 2>/dev/null || true
    
    # Install binary
    log_info "Installing binaries..."
    $use_sudo cp bin/memrogue-report "$PREFIX/bin/"
    
    # Install wrapper script
    log_info "Installing wrapper script..."
    $use_sudo cp "$PROJECT_ROOT/scripts/memrogue" "$PREFIX/bin/"
    $use_sudo chmod +x "$PREFIX/bin/memrogue"
    
    # Install headers
    log_info "Installing headers..."
    $use_sudo cp "$PROJECT_ROOT"/include/*.h "$PREFIX/include/memrogue/"
    
    # Install pkg-config file
    log_info "Installing pkg-config file..."
    local temp_pc
    temp_pc=$(mktemp)
    cat > "$temp_pc" << EOF
prefix=$PREFIX
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: MemRogue
Description: Lightweight Memory Debugging Library for C/C++
Version: 1.0.0
Libs: -L\${libdir} -lmemrogue_core -lpthread
Cflags: -I\${includedir}/memrogue
EOF
    $use_sudo mv "$temp_pc" "$PREFIX/lib/pkgconfig/memrogue.pc"
    
    # Update ldconfig for system installation
    if [ "$INSTALL_MODE" = "system" ]; then
        if command -v ldconfig &> /dev/null; then
            log_info "Updating library cache..."
            $use_sudo ldconfig
        fi
    fi
    
    log_success "Installation completed successfully!"
}

print_post_install() {
    echo ""
    echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║              MemRogue Installed Successfully!                    ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    
    case "$INSTALL_MODE" in
        system)
            echo "MemRogue has been installed system-wide to $PREFIX"
            echo ""
            echo "Usage:"
            echo "  memrogue ./your_application"
            echo "  memrogue --verbose ./your_application"
            echo ""
            echo "Or use LD_PRELOAD directly:"
            echo "  LD_PRELOAD=libmemrogue_intercept.so ./your_application"
            ;;
        user)
            echo "MemRogue has been installed to $PREFIX"
            echo ""
            echo "Add the following to your ~/.bashrc or ~/.zshrc:"
            echo ""
            echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
            echo "  export LD_LIBRARY_PATH=\"\$HOME/.local/lib:\$LD_LIBRARY_PATH\""
            echo ""
            echo "Then reload your shell or run:"
            echo "  source ~/.bashrc"
            echo ""
            echo "Usage:"
            echo "  memrogue ./your_application"
            ;;
        custom)
            echo "MemRogue has been installed to $PREFIX"
            echo ""
            echo "Add the following to your environment:"
            echo ""
            echo "  export PATH=\"$PREFIX/bin:\$PATH\""
            echo "  export LD_LIBRARY_PATH=\"$PREFIX/lib:\$LD_LIBRARY_PATH\""
            echo ""
            echo "Usage:"
            echo "  memrogue ./your_application"
            ;;
    esac
    
    echo ""
    echo "Quick Test:"
    echo "  echo 'int main() { malloc(100); return 0; }' > /tmp/test_leak.c"
    echo "  gcc -o /tmp/test_leak /tmp/test_leak.c"
    echo "  memrogue /tmp/test_leak"
    echo ""
    echo "Documentation: https://github.com/7amo10/MemRogue"
    echo ""
}

# ============================================================================
# Main
# ============================================================================

main() {
    print_banner
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --system)
                INSTALL_MODE="system"
                PREFIX="/usr/local"
                shift
                ;;
            --user)
                INSTALL_MODE="user"
                PREFIX="$HOME/.local"
                shift
                ;;
            --prefix=*)
                PREFIX="${1#*=}"
                INSTALL_MODE="custom"
                shift
                ;;
            --build-dir=*)
                BUILD_DIR="${1#*=}"
                shift
                ;;
            --rebuild|--force)
                FORCE=1
                shift
                ;;
            --verbose|-v)
                VERBOSE=1
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    # Detect distribution
    detect_distro
    
    # Check dependencies
    check_dependencies
    
    # Select installation mode if not specified
    select_install_mode
    
    log_info "Installation prefix: $PREFIX"
    
    # Build
    build_memrogue
    
    # Install
    install_memrogue
    
    # Post-install message
    print_post_install
}

main "$@"
