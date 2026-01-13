#!/bin/bash

# Check system dependencies
check_dependencies() {
    log_info "Checking system dependencies..."

    # If running inside a virtual environment, prefer installing Python module
    # dependencies into the venv via pip. System package installs (apt/dnf/etc)
    # won't be visible to the venv's interpreter.
    local in_venv=false
    if [[ -n "${VIRTUAL_ENV:-}" ]]; then
        in_venv=true
    fi

    local missing_deps=()

    # Required tools - map command names to package names
    local -A tool_packages=(
        ["cmake"]="cmake"
        ["ninja"]="ninja-build"
        ["python3"]="python3"
        ["gcc"]="gcc"
    )

    for tool in "${!tool_packages[@]}"; do
        if ! command -v "${tool}" &> /dev/null; then
            missing_deps+=("${tool_packages[$tool]}")
        fi
    done

    # Python packages required by packet generation + assertions
    if ! python3 -c "import numpy" >/dev/null 2>&1; then
        missing_deps+=("python3-numpy")
    fi
    if ! python3 -c "import cv2" >/dev/null 2>&1; then
        missing_deps+=("python3-opencv")
    fi
    if ! python3 -c "from PIL import Image" >/dev/null 2>&1; then
        missing_deps+=("python3-pil")
    fi
    if ! python3 -c "import yaml" >/dev/null 2>&1; then
        missing_deps+=("python3-yaml")
    fi
    if ! python3 -c "import scipy" >/dev/null 2>&1; then
        missing_deps+=("python3-scipy")
    fi
    if ! python3 -c "import requests" >/dev/null 2>&1; then
        missing_deps+=("python3-requests")
    fi
    if ! python3 -c "import websocket" >/dev/null 2>&1; then
        missing_deps+=("python3-websocket")
    fi

    if [[ "${in_venv}" == "true" ]]; then
        local -a pip_pkgs=()
        for dep in "${missing_deps[@]}"; do
            case "${dep}" in
                python3-numpy) pip_pkgs+=("numpy") ;;
                python3-opencv) pip_pkgs+=("opencv-python-headless") ;;
                python3-pil) pip_pkgs+=("Pillow") ;;
                python3-yaml) pip_pkgs+=("PyYAML") ;;
                python3-scipy) pip_pkgs+=("scipy") ;;
                python3-requests) pip_pkgs+=("requests") ;;
                python3-websocket) pip_pkgs+=("websocket-client") ;;
            esac
        done

        if [[ ${#pip_pkgs[@]} -gt 0 ]]; then
            if python3 -m pip --version >/dev/null 2>&1; then
                log_info "Detected Python venv; installing missing Python modules via pip: ${pip_pkgs[*]}"
                if [[ "${VERBOSE}" == true ]]; then
                    python3 -m pip install --upgrade "${pip_pkgs[@]}"
                else
                    python3 -m pip install --upgrade "${pip_pkgs[@]}" >/dev/null
                fi

                # Re-check Python deps after pip install.
                missing_deps=()
                for tool in "${!tool_packages[@]}"; do
                    if ! command -v "${tool}" &> /dev/null; then
                        missing_deps+=("${tool_packages[$tool]}")
                    fi
                done

                if ! python3 -c "import numpy" >/dev/null 2>&1; then
                    missing_deps+=("python3-numpy")
                fi
                if ! python3 -c "import cv2" >/dev/null 2>&1; then
                    missing_deps+=("python3-opencv")
                fi
                if ! python3 -c "from PIL import Image" >/dev/null 2>&1; then
                    missing_deps+=("python3-pil")
                fi
                if ! python3 -c "import yaml" >/dev/null 2>&1; then
                    missing_deps+=("python3-yaml")
                fi
                if ! python3 -c "import scipy" >/dev/null 2>&1; then
                    missing_deps+=("python3-scipy")
                fi
                if ! python3 -c "import requests" >/dev/null 2>&1; then
                    missing_deps+=("python3-requests")
                fi
                if ! python3 -c "import websocket" >/dev/null 2>&1; then
                    missing_deps+=("python3-websocket")
                fi
            else
                log_warning "Detected Python venv but pip is unavailable; cannot auto-install Python module dependencies."
            fi
        fi
    fi

    # Virtual display tools (always needed for headless testing)
    local -A display_packages=(
        ["Xvfb"]="xvfb"
    )

    for tool in "${!display_packages[@]}"; do
        if ! command -v "${tool}" &> /dev/null; then
            missing_deps+=("${display_packages[$tool]}")
        fi
    done

    # Optional tools
    if [[ "${OBS_ENABLED}" == true ]]; then
        # Check for OBS Studio (don't try to install it via apt as it's not available)
        if ! command -v obs &> /dev/null; then
            log_warning "OBS Studio not found - E2E tests will run in validation-only mode"
            log_info "To install OBS Studio: https://obsproject.com/download"
        fi

        # Check for ffmpeg (can be installed via apt)
        if ! command -v ffmpeg &> /dev/null; then
            missing_deps+=("ffmpeg")
        fi
    fi

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_warning "Missing dependencies: ${missing_deps[*]}"
        log_info "Installing missing dependencies..."

        local pkg_manager=""
        local update_cmd=()
        local install_cmd=()
        local manual_hint=""

        if command -v apt-get >/dev/null 2>&1; then
            pkg_manager="apt"
            update_cmd=(apt-get update)
            install_cmd=(apt-get install -y)
            manual_hint="apt-get install"
        elif command -v dnf >/dev/null 2>&1; then
            pkg_manager="dnf"
            update_cmd=(dnf makecache)
            install_cmd=(dnf install -y)
            manual_hint="dnf install"
        elif command -v yum >/dev/null 2>&1; then
            pkg_manager="yum"
            update_cmd=(yum makecache)
            install_cmd=(yum install -y)
            manual_hint="yum install"
        elif command -v pacman >/dev/null 2>&1; then
            pkg_manager="pacman"
            update_cmd=(pacman -Sy --noconfirm)
            install_cmd=(pacman -S --noconfirm)
            manual_hint="pacman -S --noconfirm"
        elif command -v zypper >/dev/null 2>&1; then
            pkg_manager="zypper"
            update_cmd=(zypper refresh)
            install_cmd=(zypper --non-interactive install)
            manual_hint="zypper install"
        elif command -v apk >/dev/null 2>&1; then
            pkg_manager="apk"
            update_cmd=(apk update)
            install_cmd=(apk add)
            manual_hint="apk add"
        fi

        if [[ -z "${pkg_manager}" ]]; then
            log_error "No supported package manager found for auto-install."
            log_info "Please install missing dependencies manually: ${missing_deps[*]}"
            exit 1
        fi

        # Determine privilege escalation (use sudo if available, else run directly if root)
        local SUDO="sudo"
        if [[ $(id -u) -eq 0 ]] || ! command -v sudo >/dev/null 2>&1; then
            SUDO=""
        fi

        local mapped_deps=()
        for dep in "${missing_deps[@]}"; do
            case "${pkg_manager}:${dep}" in
                apt:*) mapped_deps+=("${dep}") ;;
                dnf:python3-pil) mapped_deps+=("python3-pillow") ;;
                dnf:python3-yaml) mapped_deps+=("python3-pyyaml") ;;
                dnf:python3-websocket) mapped_deps+=("python3-websocket-client") ;;
                dnf:xvfb) mapped_deps+=("xorg-x11-server-Xvfb") ;;
                dnf:ffmpeg) mapped_deps+=("ffmpeg-free") ;;
                dnf:*) mapped_deps+=("${dep}") ;;
                yum:python3-pil) mapped_deps+=("python3-pillow") ;;
                yum:python3-yaml) mapped_deps+=("python3-pyyaml") ;;
                yum:python3-websocket) mapped_deps+=("python3-websocket-client") ;;
                yum:xvfb) mapped_deps+=("xorg-x11-server-Xvfb") ;;
                yum:ffmpeg) mapped_deps+=("ffmpeg-free") ;;
                yum:*) mapped_deps+=("${dep}") ;;
                pacman:ninja-build) mapped_deps+=("ninja") ;;
                pacman:python3) mapped_deps+=("python") ;;
                pacman:python3-numpy) mapped_deps+=("python-numpy") ;;
                pacman:python3-opencv) mapped_deps+=("python-opencv") ;;
                pacman:python3-pil) mapped_deps+=("python-pillow") ;;
                pacman:python3-yaml) mapped_deps+=("python-yaml") ;;
                pacman:python3-scipy) mapped_deps+=("python-scipy") ;;
                pacman:python3-requests) mapped_deps+=("python-requests") ;;
                pacman:python3-websocket) mapped_deps+=("python-websocket-client") ;;
                pacman:xvfb) mapped_deps+=("xorg-server-xvfb") ;;
                pacman:*) mapped_deps+=("${dep}") ;;
                zypper:python3-pil) mapped_deps+=("python3-Pillow") ;;
                zypper:python3-yaml) mapped_deps+=("python3-PyYAML") ;;
                zypper:python3-websocket) mapped_deps+=("python3-websocket-client") ;;
                zypper:xvfb) mapped_deps+=("xorg-x11-server") ;;
                zypper:*) mapped_deps+=("${dep}") ;;
                apk:python3-pil) mapped_deps+=("py3-pillow") ;;
                apk:python3-yaml) mapped_deps+=("py3-yaml") ;;
                apk:python3-websocket) mapped_deps+=("py3-websocket-client") ;;
                apk:python3-requests) mapped_deps+=("py3-requests") ;;
                apk:python3-numpy) mapped_deps+=("py3-numpy") ;;
                apk:python3-scipy) mapped_deps+=("py3-scipy") ;;
                apk:python3-opencv) mapped_deps+=("py3-opencv") ;;
                apk:xvfb) mapped_deps+=("xvfb") ;;
                apk:python3) mapped_deps+=("python3") ;;
                apk:*) mapped_deps+=("${dep}") ;;
            esac
        done

        # Update package list / cache
        if [[ ${#update_cmd[@]} -gt 0 ]]; then
            if [[ "${VERBOSE}" == true ]]; then
                ${SUDO} "${update_cmd[@]}"
            else
                ${SUDO} "${update_cmd[@]}" > /dev/null 2>&1
            fi
        fi

        # Install missing packages
        if [[ "${VERBOSE}" == true ]]; then
            ${SUDO} "${install_cmd[@]}" "${mapped_deps[@]}"
        else
            ${SUDO} "${install_cmd[@]}" "${mapped_deps[@]}" > /dev/null 2>&1
        fi

        # Verify installation succeeded
        local still_missing=()
        for tool in "${!tool_packages[@]}"; do
            if ! command -v "${tool}" &> /dev/null; then
                still_missing+=("${tool_packages[$tool]}")
            fi
        done

        for tool in "${!display_packages[@]}"; do
            if ! command -v "${tool}" &> /dev/null; then
                still_missing+=("${display_packages[$tool]}")
            fi
        done

        if ! python3 -c "import numpy" 2>/dev/null; then
            still_missing+=("python3-numpy")
        fi
        if ! python3 -c "import cv2" 2>/dev/null; then
            still_missing+=("python3-opencv")
        fi
        if ! python3 -c "from PIL import Image" 2>/dev/null; then
            still_missing+=("python3-pil")
        fi
        if ! python3 -c "import yaml" 2>/dev/null; then
            still_missing+=("python3-yaml")
        fi
        if ! python3 -c "import scipy" 2>/dev/null; then
            still_missing+=("python3-scipy")
        fi
        if ! python3 -c "import requests" 2>/dev/null; then
            still_missing+=("python3-requests")
        fi
        if ! python3 -c "import websocket" 2>/dev/null; then
            still_missing+=("python3-websocket")
        fi

        if [[ ${#still_missing[@]} -gt 0 ]]; then
            log_error "Failed to install dependencies: ${still_missing[*]}"
            log_info "Please install manually: sudo ${manual_hint} ${still_missing[*]}"
            exit 1
        fi

        log_success "Dependencies installed successfully"
    else
        log_success "All dependencies satisfied"
    fi
}
