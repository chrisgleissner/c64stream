#!/usr/bin/env bash
set -euo pipefail

BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; }

install_core_packages() {
    log_info "Installing core build dependencies..."

    sudo apt-get update -qq
    sudo apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        git \
        zsh \
        curl \
        wget \
        libcurl4-openssl-dev \
        libsimde-dev \
        ccache \
        python3 \
        python3-pip

    log_success "Core packages installed"
}

install_gersemi() {
    log_info "Installing gersemi..."

    if command -v gersemi >/dev/null 2>&1; then
        log_success "gersemi already installed"
        return
    fi

    python3 -m pip install --user gersemi

    export PATH="$HOME/.local/bin:$PATH"

    if ! command -v gersemi >/dev/null; then
        log_error "gersemi installed but not found in PATH"
        exit 1
    fi

    log_success "gersemi installed"
}

main() {
    log_info "=== C64 Stream Copilot Dependencies Installer ==="

    install_core_packages
    install_gersemi

    log_success "=== All dependencies installed successfully ==="
}

main "$@"
