#!/bin/bash
#
# MemRogue Uninstall Script
# Cleanly removes MemRogue from the system
#
# Usage:
#   sudo ./uninstall.sh           # Remove system-wide installation
#   ./uninstall.sh --user         # Remove user installation
#   ./uninstall.sh --prefix=/opt  # Remove from custom prefix
#

set -e

# ============================================================================
# Configuration
# ============================================================================

VERSION="1.0.0"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default installation prefix (matches install.sh defaults)
PREFIX="/usr/local"
USER_INSTALL=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================================
# Helper Functions
# ============================================================================

print_header() {
    echo -e "${BLUE}"
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║              MemRogue Uninstaller v${VERSION}                        ║"
    echo "║              Memory Debugging Made Easy                          ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Uninstall MemRogue memory debugger from the system.

OPTIONS:
    -h, --help              Show this help message
    -u, --user              Remove user installation (~/.local)
    -p, --prefix=PATH       Remove from custom prefix (default: /usr/local)
    -n, --dry-run           Show what would be removed without removing
    -v, --verbose           Verbose output

EXAMPLES:
    sudo ./uninstall.sh              # Remove system-wide installation
    ./uninstall.sh --user            # Remove user installation
    ./uninstall.sh --prefix=/opt/memrogue  # Remove from custom location

EOF
    exit 0
}

# ============================================================================
# Parse Arguments
# ============================================================================

DRY_RUN=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            ;;
        -u|--user)
            USER_INSTALL=true
            PREFIX="$HOME/.local"
            shift
            ;;
        -p|--prefix)
            PREFIX="$2"
            shift 2
            ;;
        --prefix=*)
            PREFIX="${1#*=}"
            shift
            ;;
        -n|--dry-run)
            DRY_RUN=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            ;;
    esac
done

# ============================================================================
# Check Permissions
# ============================================================================

check_permissions() {
    if [[ "$USER_INSTALL" == false && "$PREFIX" == "/usr"* && $EUID -ne 0 ]]; then
        log_error "Root privileges required to uninstall from $PREFIX"
        log_info "Run with: sudo $0"
        log_info "Or use --user for user installation removal"
        exit 1
    fi
}

# ============================================================================
# Remove Files
# ============================================================================

remove_file() {
    local file="$1"
    if [[ -e "$file" || -L "$file" ]]; then
        if [[ "$DRY_RUN" == true ]]; then
            log_info "[DRY-RUN] Would remove: $file"
        else
            if [[ "$VERBOSE" == true ]]; then
                log_info "Removing: $file"
            fi
            rm -f "$file"
        fi
        return 0
    fi
    return 1
}

remove_dir_if_empty() {
    local dir="$1"
    if [[ -d "$dir" ]]; then
        if [[ -z "$(ls -A "$dir" 2>/dev/null)" ]]; then
            if [[ "$DRY_RUN" == true ]]; then
                log_info "[DRY-RUN] Would remove empty directory: $dir"
            else
                if [[ "$VERBOSE" == true ]]; then
                    log_info "Removing empty directory: $dir"
                fi
                rmdir "$dir" 2>/dev/null || true
            fi
        fi
    fi
}

# ============================================================================
# Main Uninstall
# ============================================================================

uninstall_memrogue() {
    local removed_count=0
    
    log_info "Uninstalling MemRogue from: $PREFIX"
    echo ""
    
    # Files to remove
    local files=(
        # Binaries
        "$PREFIX/bin/memrogue"
        "$PREFIX/bin/memrogue-report"
        
        # Libraries
        "$PREFIX/lib/libmemrogue.so"
        "$PREFIX/lib/libmemrogue.so.1"
        "$PREFIX/lib/libmemrogue.so.1.0.0"
        "$PREFIX/lib/libmemrogue_core.a"
        "$PREFIX/lib/libmemrogue_intercept.so"
        "$PREFIX/lib/libmemrogue_intercept.so.1"
        "$PREFIX/lib/libmemrogue_intercept.so.1.0.0"
        
        # 64-bit library paths (some distros)
        "$PREFIX/lib64/libmemrogue.so"
        "$PREFIX/lib64/libmemrogue.so.1"
        "$PREFIX/lib64/libmemrogue.so.1.0.0"
        "$PREFIX/lib64/libmemrogue_core.a"
        "$PREFIX/lib64/libmemrogue_intercept.so"
        "$PREFIX/lib64/libmemrogue_intercept.so.1"
        "$PREFIX/lib64/libmemrogue_intercept.so.1.0.0"
        
        # pkg-config
        "$PREFIX/lib/pkgconfig/memrogue.pc"
        "$PREFIX/lib64/pkgconfig/memrogue.pc"
        "$PREFIX/share/pkgconfig/memrogue.pc"
        
        # Man pages
        "$PREFIX/share/man/man1/memrogue.1"
        "$PREFIX/share/man/man1/memrogue.1.gz"
        
        # Shell completions
        "$PREFIX/share/bash-completion/completions/memrogue"
        "$PREFIX/share/zsh/site-functions/_memrogue"
        "$PREFIX/etc/bash_completion.d/memrogue"
    )
    
    # Remove files
    echo "Removing files..."
    for file in "${files[@]}"; do
        if remove_file "$file"; then
            ((removed_count++))
        fi
    done
    
    # Remove headers directory
    if [[ -d "$PREFIX/include/memrogue" ]]; then
        if [[ "$DRY_RUN" == true ]]; then
            log_info "[DRY-RUN] Would remove directory: $PREFIX/include/memrogue"
        else
            if [[ "$VERBOSE" == true ]]; then
                log_info "Removing directory: $PREFIX/include/memrogue"
            fi
            rm -rf "$PREFIX/include/memrogue"
        fi
        ((removed_count++))
    fi
    
    # Remove documentation directory
    if [[ -d "$PREFIX/share/doc/memrogue" ]]; then
        if [[ "$DRY_RUN" == true ]]; then
            log_info "[DRY-RUN] Would remove directory: $PREFIX/share/doc/memrogue"
        else
            if [[ "$VERBOSE" == true ]]; then
                log_info "Removing directory: $PREFIX/share/doc/memrogue"
            fi
            rm -rf "$PREFIX/share/doc/memrogue"
        fi
        ((removed_count++))
    fi
    
    # Remove examples directory
    if [[ -d "$PREFIX/share/memrogue" ]]; then
        if [[ "$DRY_RUN" == true ]]; then
            log_info "[DRY-RUN] Would remove directory: $PREFIX/share/memrogue"
        else
            if [[ "$VERBOSE" == true ]]; then
                log_info "Removing directory: $PREFIX/share/memrogue"
            fi
            rm -rf "$PREFIX/share/memrogue"
        fi
        ((removed_count++))
    fi
    
    # Clean up empty directories
    echo ""
    echo "Cleaning up empty directories..."
    remove_dir_if_empty "$PREFIX/lib/pkgconfig"
    remove_dir_if_empty "$PREFIX/lib64/pkgconfig"
    remove_dir_if_empty "$PREFIX/share/pkgconfig"
    remove_dir_if_empty "$PREFIX/share/man/man1"
    remove_dir_if_empty "$PREFIX/share/man"
    remove_dir_if_empty "$PREFIX/share/bash-completion/completions"
    remove_dir_if_empty "$PREFIX/share/bash-completion"
    remove_dir_if_empty "$PREFIX/share/zsh/site-functions"
    remove_dir_if_empty "$PREFIX/share/zsh"
    remove_dir_if_empty "$PREFIX/etc/bash_completion.d"
    
    # Update ldconfig if system install
    if [[ "$USER_INSTALL" == false && "$DRY_RUN" == false ]]; then
        if command -v ldconfig &> /dev/null; then
            echo ""
            log_info "Updating library cache..."
            ldconfig 2>/dev/null || true
        fi
    fi
    
    return $removed_count
}

# ============================================================================
# Post-Uninstall
# ============================================================================

print_summary() {
    local removed_count=$1
    
    echo ""
    echo -e "${GREEN}════════════════════════════════════════════════════════════════════${NC}"
    
    if [[ "$DRY_RUN" == true ]]; then
        echo -e "${YELLOW}DRY RUN COMPLETE${NC}"
        echo "No files were actually removed."
        echo "Run without --dry-run to perform actual uninstallation."
    elif [[ $removed_count -gt 0 ]]; then
        echo -e "${GREEN}UNINSTALLATION COMPLETE${NC}"
        echo ""
        echo "MemRogue has been removed from: $PREFIX"
        echo ""
        
        if [[ "$USER_INSTALL" == true ]]; then
            echo "You may want to remove these from your shell config:"
            echo "  - PATH modification for ~/.local/bin"
            echo "  - LD_LIBRARY_PATH modification for ~/.local/lib"
        fi
    else
        echo -e "${YELLOW}NO MEMROGUE INSTALLATION FOUND${NC}"
        echo ""
        echo "MemRogue does not appear to be installed at: $PREFIX"
        echo ""
        echo "Try one of these options:"
        echo "  - Use --prefix to specify a different installation location"
        echo "  - Use --user if installed in ~/.local"
    fi
    
    echo -e "${GREEN}════════════════════════════════════════════════════════════════════${NC}"
}

# ============================================================================
# Main
# ============================================================================

main() {
    print_header
    
    check_permissions
    
    uninstall_memrogue
    local removed_count=$?
    
    print_summary $removed_count
}

main "$@"
