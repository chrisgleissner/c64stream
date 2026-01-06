#!/usr/bin/env bash
# Install minimal dependencies for Copilot agent builds
# Simple and resilient approach using official LLVM APT repository

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Install minimal build dependencies
install_minimal_deps() {
    log_info "Installing minimal build dependencies..."
    
    local -a core_packages=(
        build-essential
        cmake
        ninja-build
        pkg-config
        git
        zsh
        curl
        wget
        libcurl4-openssl-dev
        libsimde-dev
        ccache
        python3
        python3-pip
        ca-certificates
        gnupg
        lsb-release
        software-properties-common
    )
    
    sudo apt-get update -qq
    sudo apt-get install -y --no-install-recommends "${core_packages[@]}"
    log_success "Core packages installed"
}

# Install clang-format 21 via official LLVM APT repository
install_clang_format_21() {
    log_info "Installing clang-format 21 from official LLVM repository..."

    # Add LLVM repo if not already present
    if ! grep -Rq "llvm-toolchain-.*-21" /etc/apt/sources.list.d /etc/apt/sources.list; then
        log_info "Adding LLVM APT repository..."
        curl -fsSL https://apt.llvm.org/llvm.sh | sudo bash -s -- 21
    fi

    sudo apt-get update -qq
    sudo apt-get install -y clang-tools-21

    # Verify binary exists
    if ! command -v clang-format >/dev/null 2>&1; then
        log_error "clang-format binary not found after installation"
        return 1
    fi

    # Verify major version
    local version
    version=$(clang-format --version | grep -oE '[0-9]+' | head -1)

    if [[ "$version" != "21" ]]; then
        log_error "clang-format is not version 21"
        clang-format --version
        return 1
    fi

    log_success "clang-format 21 installed and verified"
}

# Install gersemi for CMake formatting
install_gersemi() {
    if command -v gersemi >/dev/null 2>&1; then
        log_success "gersemi already installed"
        return 0
    fi
    
    log_info "Installing gersemi for CMake formatting..."
    
    if python3 -m pip install --user --break-system-packages gersemi 2>/dev/null; then
        log_success "gersemi installed via pip"
    elif python3 -m pip install --user gersemi 2>/dev/null; then
        log_success "gersemi installed via pip"
    else
        log_error "Failed to install gersemi"
        return 1
    fi
}

# Main installation
main() {
    log_info "=== C64 Stream Copilot Dependencies Installer ==="
    echo
    
    install_minimal_deps
    echo
    
    install_clang_format_21
    echo
    
    install_gersemi
    echo
    
    log_success "=== All dependencies installed successfully ==="
}

main "$@"
