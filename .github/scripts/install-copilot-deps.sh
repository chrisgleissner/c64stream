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
        python3-pip \
        ca-certificates \
        gnupg \
        lsb-release \
        software-properties-common

    log_success "Core packages installed"
}

install_llvm_21_repo() {
    log_info "Adding LLVM 21 APT repository (noble)..."

    if [[ ! -f /etc/apt/trusted.gpg.d/apt.llvm.org.asc ]]; then
        curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
            | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null
    fi

    if [[ ! -f /etc/apt/sources.list.d/llvm-21.list ]]; then
        echo "deb https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main" \
            | sudo tee /etc/apt/sources.list.d/llvm-21.list >/dev/null
    fi

    sudo apt-get update -qq
}

install_clang_format_21() {
    log_info "Installing clang-format 21..."

    sudo apt-get install -y \
        clang-tools-21 \
        clang-21 \
        clangd-21 \
        lld-21 \
        lldb-21

    if ! command -v clang-format >/dev/null; then
        log_error "clang-format binary not found"
        exit 1
    fi

    # Force LLVM clang-format to win over Ubuntu's clang-format 18
    sudo update-alternatives --install \
        /usr/bin/clang-format clang-format \
        /usr/bin/clang-format 2100

    sudo update-alternatives --set clang-format /usr/bin/clang-format

    local major
    major=$(clang-format --version | sed -n 's/.*version \([0-9]\+\).*/\1/p')

    if [[ "$major" != "21" ]]; then
        log_error "clang-format is not version 21"
        clang-format --version
        exit 1
    fi

    log_success "clang-format 21 installed and selected"
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
    install_llvm_21_repo
    install_clang_format_21
    install_gersemi

    log_success "=== All dependencies installed successfully ==="
}

main "$@"
