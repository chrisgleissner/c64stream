#!/bin/bash

# Default build directory if not set
BUILD_DIR="${BUILD_DIR:-build_x86_64}"

build_project() {
    if [[ "${SKIP_BUILD}" == true ]]; then
        log_info "Skipping build (--skip-build specified)"
        return
    fi

    log_info "Building C64 Stream plugin and E2E tools..."

    cd "${PROJECT_ROOT}"

    # Configure build

    if [[ "${VERBOSE}" == true ]]; then
        cmake --preset ubuntu-x86_64
    else
        cmake --preset ubuntu-x86_64 > /dev/null
    fi

    # Build plugin and E2E tools
    if [[ "${VERBOSE}" == true ]]; then
        cmake --build "${BUILD_DIR}" --target c64stream udp_replay
    else
        cmake --build "${BUILD_DIR}" --target c64stream udp_replay > /dev/null
    fi

    # Verify build artifacts
    if [[ ! -f "${BUILD_DIR}/c64stream.so" ]]; then
        log_error "Plugin build failed: c64stream.so not found"
        exit 1
    fi

    if [[ ! -f "${BUILD_DIR}/tests/e2e/udp_replay" ]]; then
        log_error "E2E tool build failed: udp_replay not found"
        exit 1
    fi

    log_success "Build completed successfully"
}


# Install plugin to OBS
install_plugin() {
    if [[ "${SKIP_BUILD}" == true ]]; then
        log_info "Skipping plugin installation (--skip-build specified, plugin already installed by workflow)"
        return
    fi

    log_info "Installing plugin to OBS..."

    local obs_plugin_dir="${HOME}/.config/obs-studio/plugins/c64stream"

    mkdir -p "${obs_plugin_dir}/bin/64bit"
    mkdir -p "${obs_plugin_dir}/data"

    # Copy plugin binary
    cp "${BUILD_DIR}/c64stream.so" "${obs_plugin_dir}/bin/64bit/"

    # Copy plugin data files
    if [[ -d "${PROJECT_ROOT}/data" ]]; then
        cp -r "${PROJECT_ROOT}/data"/* "${obs_plugin_dir}/data/"
    fi

    if [[ "${VERBOSE}" == true ]]; then
        log_info "Plugin installation details:"
        echo "  Binary location: ${obs_plugin_dir}/bin/64bit/c64stream.so"
        if [[ -f "${obs_plugin_dir}/bin/64bit/c64stream.so" ]]; then
            echo "    Size: $(du -h "${obs_plugin_dir}/bin/64bit/c64stream.so" | cut -f1)"
            echo "    MD5: $(md5sum "${obs_plugin_dir}/bin/64bit/c64stream.so" | cut -d' ' -f1)"
        fi
        echo "  Data location: ${obs_plugin_dir}/data"
        if [[ -d "${obs_plugin_dir}/data" ]]; then
            echo "  Data contents:"
            ls -lah "${obs_plugin_dir}/data" 2>/dev/null | sed 's/^/    /' || echo "    (empty)"
        fi
        echo "  Full plugin directory structure:"
        find "${obs_plugin_dir}" -type f -o -type d 2>/dev/null | sed 's/^/    /' || echo "    (not found)"
    fi

    log_success "Plugin installed to OBS"
}
