#!/usr/bin/env bash
# Install minimal dependencies for Copilot agent builds
# This script is optimized for fast session startup with caching

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Check if we're in a GitHub Actions environment
is_github_actions() {
    [[ "${GITHUB_ACTIONS:-false}" == "true" ]]
}

# Install minimal build dependencies (no OBS, no GUI deps)
install_minimal_deps() {
    log_info "Installing minimal build dependencies..."
    
    local -a core_packages=(
        build-essential
        cmake
        ninja-build
        pkg-config
        git
        zsh                    # Required by build scripts
        curl                   # Required for downloads and REST client
        libcurl4-openssl-dev   # Required for libcurl development
        libsimde-dev           # SIMD optimizations
        ccache                 # Build caching
        python3
        python3-pip
    )
    
    # Check which packages are already installed
    local -a missing_packages=()
    for pkg in "${core_packages[@]}"; do
        if ! dpkg -l "$pkg" 2>/dev/null | grep -q "^ii"; then
            missing_packages+=("$pkg")
        fi
    done
    
    if [[ ${#missing_packages[@]} -eq 0 ]]; then
        log_success "All core packages already installed"
    else
        log_info "Installing missing packages: ${missing_packages[*]}"
        sudo apt-get update -qq
        sudo apt-get install -y --no-install-recommends "${missing_packages[@]}"
        log_success "Core packages installed"
    fi
}

# Install clang-format 21 via Homebrew (persistent, works across sessions)
install_clang_format_21() {
    log_info "Checking clang-format 21 installation..."
    
    # Check if already installed and working
    if command -v clang-format >/dev/null 2>&1; then
        local version=$(clang-format --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        if [[ -n "$version" ]]; then
            local major=$(echo "$version" | cut -d. -f1)
            if [[ "$major" -ge 21 ]]; then
                log_success "clang-format $version already installed"
                return 0
            fi
        fi
    fi
    
    log_info "Installing clang-format 21 via Homebrew..."
    
    # Install Homebrew if not present (to ~/.linuxbrew for persistence)
    if [[ ! -d "$HOME/.linuxbrew" ]]; then
        log_info "Installing Homebrew to ~/.linuxbrew (persistent across sessions)..."
        mkdir -p "$HOME/.linuxbrew"
        if [[ ! -d "$HOME/.linuxbrew/Homebrew" ]]; then
            git clone --depth=1 https://github.com/Homebrew/brew "$HOME/.linuxbrew/Homebrew"
        fi
        mkdir -p "$HOME/.linuxbrew/bin"
        ln -sf ../Homebrew/bin/brew "$HOME/.linuxbrew/bin/brew"
        log_success "Homebrew installed to ~/.linuxbrew"
    else
        log_info "Homebrew already installed at ~/.linuxbrew"
    fi
    
    # Set up Homebrew environment
    eval "$("$HOME/.linuxbrew/bin/brew" shellenv)"
    export PATH="$HOME/.linuxbrew/bin:$PATH"
    
    # Install LLVM (includes clang-format 21)
    if ! brew list llvm &>/dev/null; then
        log_info "Installing LLVM via Homebrew (includes clang-format 21)..."
        # Disable auto-update to speed up installation
        export HOMEBREW_NO_AUTO_UPDATE=1
        export HOMEBREW_NO_INSTALL_CLEANUP=1
        brew install llvm
        log_success "LLVM installed"
    else
        log_info "LLVM already installed"
    fi
    
    # Add LLVM binaries to PATH
    local llvm_prefix="$(brew --prefix llvm)"
    export PATH="$llvm_prefix/bin:$PATH"
    
    # Verify installation
    if command -v clang-format >/dev/null 2>&1; then
        local version=$(clang-format --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        log_success "clang-format $version installed and available"
        
        # Create a persistent shell profile entry
        local profile="$HOME/.bash_profile"
        if [[ ! -f "$profile" ]] || ! grep -q "linuxbrew" "$profile"; then
            log_info "Adding Homebrew to shell profile for persistence..."
            cat >> "$profile" << 'EOF'
# Homebrew (for clang-format 21)
if [[ -d "$HOME/.linuxbrew" ]]; then
    eval "$("$HOME/.linuxbrew/bin/brew" shellenv)"
    export PATH="$(brew --prefix llvm)/bin:$PATH"
fi
EOF
            log_success "Homebrew added to $profile"
        fi
    else
        log_error "clang-format installation failed"
        return 1
    fi
}

# Install gersemi for CMake formatting
install_gersemi() {
    if command -v gersemi >/dev/null 2>&1; then
        log_success "gersemi already installed"
        return 0
    fi
    
    log_info "Installing gersemi for CMake formatting..."
    
    if ! command -v python3 >/dev/null 2>&1; then
        log_warning "Python3 not found; cannot install gersemi"
        return 1
    fi
    
    if python3 -m pip install --user --break-system-packages gersemi 2>/dev/null; then
        log_success "gersemi installed via pip"
        return 0
    elif python3 -m pip install --user gersemi 2>/dev/null; then
        log_success "gersemi installed via pip"
        return 0
    else
        log_warning "Failed to install gersemi automatically"
        return 1
    fi
}

# Create cache marker to track what's been installed
create_cache_marker() {
    local cache_dir="$HOME/.cache/c64stream"
    mkdir -p "$cache_dir"
    
    cat > "$cache_dir/copilot-deps.marker" << EOF
# C64 Stream Copilot Dependencies Cache Marker
# Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
DEPS_INSTALLED=true
CLANG_FORMAT_VERSION=$(clang-format --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || echo "unknown")
HOMEBREW_PREFIX=$(brew --prefix 2>/dev/null || echo "not installed")
EOF
    
    log_success "Cache marker created at $cache_dir/copilot-deps.marker"
}

# Main installation flow
main() {
    log_info "=== C64 Stream Copilot Dependencies Installer ==="
    log_info "Optimized for fast Copilot session startup with caching"
    echo
    
    # Install minimal dependencies
    install_minimal_deps
    echo
    
    # Install clang-format 21
    install_clang_format_21
    echo
    
    # Install gersemi
    install_gersemi
    echo
    
    # Create cache marker
    create_cache_marker
    echo
    
    log_success "=== All dependencies installed successfully ==="
    echo
    log_info "Next steps:"
    log_info "  1. Source your shell profile: source ~/.bash_profile"
    log_info "  2. Verify clang-format: clang-format --version"
    log_info "  3. Build the project: ./local-build.sh linux"
}

main "$@"
