#!/bin/bash

# Resource monitoring functions
MONITOR_PID=""

start_resource_monitoring() {
    if [[ "${MONITOR_RESOURCES}" != true ]]; then
        return
    fi

    log_info "Starting resource monitoring..."

    # Function to get system stats (called inline, not as background process)
    get_resource_stats() {
        printf "📊 [%s] CPU:%s%% MEM:%s/%s(%s%%) DISK:%s%% LOAD:%s PROCS:%d" \
            "$(date '+%H:%M:%S')" \
            "$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d'%' -f1)" \
            "$(free -h | awk '/^Mem:/ {print $3}')" \
            "$(free -h | awk '/^Mem:/ {print $2}')" \
            "$(free | awk '/^Mem:/ {printf "%.0f", $3/$2*100}')" \
            "$(df /tmp | awk 'NR==2 {print $5}' | cut -d'%' -f1)" \
            "$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | cut -d',' -f1)" \
            "$(ps aux | wc -l)"
    }

    # Show initial system state
    echo "=== Initial System State ==="
    echo "Hardware: $(nproc) CPUs, $(free -h | awk '/^Mem:/ {print $2}') RAM, $(df -h /tmp | awk 'NR==2 {print $2}') /tmp"
    get_resource_stats
    echo

    # Start background monitoring with simple while loop
    (
        while true; do
            sleep 10
            get_resource_stats
            echo
        done
    ) &
    MONITOR_PID=$!
}

stop_resource_monitoring() {
    if [[ "${MONITOR_RESOURCES}" != true ]] || [[ -z "${MONITOR_PID}" ]]; then
        return
    fi

    log_info "Stopping resource monitoring..."
    kill "${MONITOR_PID}" 2>/dev/null || true
    wait "${MONITOR_PID}" 2>/dev/null || true
    MONITOR_PID=""

    # Show final state
    echo "=== Final System State ==="
    printf "📊 Final: CPU:%s%% MEM:%s/%s(%s%%) DISK:%s%% LOAD:%s PROCS:%d\n" \
        "$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d'%' -f1)" \
        "$(free -h | awk '/^Mem:/ {print $3}')" \
        "$(free -h | awk '/^Mem:/ {print $2}')" \
        "$(free | awk '/^Mem:/ {printf "%.0f", $3/$2*100}')" \
        "$(df /tmp | awk 'NR==2 {print $5}' | cut -d'%' -f1)" \
        "$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | cut -d',' -f1)" \
        "$(ps aux | wc -l)"
}

# Ensure perf can run (local dev best-effort).
ensure_udp_buffers() {
    # Ensure adequate UDP buffer sizes for high-throughput E2E tests.
    # NOTE: GitHub Actions containers have a 1MB UDP buffer limit that CANNOT be changed.
    # The E2E test accommodates this via MIN_PACKET_DELAY in manifest generation.
    # This function only helps on local dev machines.

    if [[ "$(uname -s 2>/dev/null || echo '')" != "Linux" ]]; then
        return 0
    fi

    # Check all critical UDP parameters
    local current_rmem_max current_rmem_default current_netdev_backlog
    current_rmem_max=$(cat /proc/sys/net/core/rmem_max 2>/dev/null || echo "0")
    current_rmem_default=$(cat /proc/sys/net/core/rmem_default 2>/dev/null || echo "0")
    current_netdev_backlog=$(cat /proc/sys/net/core/netdev_max_backlog 2>/dev/null || echo "0")

    local target_max=8388608      # 8MB max
    local target_default=2097152  # 2MB default (critical for new sockets)
    local target_backlog=5000     # 5000 packets (handles 19K burst)

    # Check if all settings are adequate
    local needs_update=false
    if [[ "${current_rmem_max}" -lt "${target_max}" ]] || \
       [[ "${current_rmem_default}" -lt "${target_default}" ]] || \
       [[ "${current_netdev_backlog}" -lt "${target_backlog}" ]]; then
        needs_update=true
    fi

    if [[ "${needs_update}" == "false" ]]; then
        return 0  # Already adequate
    fi

    # Try to increase permanently (best-effort, will fail in CI containers)
    # Only attempt if we have sudo AND we're in an interactive session
    if [[ "$(id -u)" == "0" ]]; then
        # Running as root - apply directly and make persistent
        sysctl -w net.core.rmem_max=${target_max} >/dev/null 2>&1
        sysctl -w net.core.wmem_max=${target_max} >/dev/null 2>&1
        sysctl -w net.core.rmem_default=${target_default} >/dev/null 2>&1
        sysctl -w net.core.wmem_default=${target_default} >/dev/null 2>&1
        sysctl -w net.core.netdev_max_backlog=${target_backlog} >/dev/null 2>&1
        # Make persistent
        cat > /etc/sysctl.d/99-c64stream-udp.conf <<EOF
# C64 Stream E2E Test UDP Configuration
# Ensures adequate buffers for high-throughput packet replay (19K packets in ~5s)
net.core.rmem_max = ${target_max}
net.core.wmem_max = ${target_max}
net.core.rmem_default = ${target_default}
net.core.wmem_default = ${target_default}
net.core.netdev_max_backlog = ${target_backlog}
EOF
    elif command -v sudo >/dev/null 2>&1 && [[ -t 0 ]]; then
        # Check if persistent config already exists (skip prompt if so)
        if [[ -f "/etc/sysctl.d/99-c64stream-udp.conf" ]]; then
            log_info "UDP buffer config already exists: /etc/sysctl.d/99-c64stream-udp.conf"
            # Reapply it in case current kernel value is lower
            sudo sysctl -p /etc/sysctl.d/99-c64stream-udp.conf >/dev/null 2>&1
        else
            # Interactive terminal: offer to increase buffers permanently (one-time setup)
            log_warning "UDP configuration insufficient for E2E tests:"
            log_info "  rmem_default: ${current_rmem_default} bytes (need ${target_default})"
            log_info "  rmem_max: ${current_rmem_max} bytes (need ${target_max})"
            log_info "  netdev_max_backlog: ${current_netdev_backlog} packets (need ${target_backlog})"
            echo ""
            echo "This is a one-time setup that will:"
            echo "  1. Set UDP buffer defaults to 2MB (critical for packet reception)"
            echo "  2. Set UDP buffer max to 8MB"
            echo "  3. Increase netdev backlog to 5000 packets (handles 19K burst)"
            echo "  4. Make changes persistent (survives reboots)"
            echo "  5. Create: /etc/sysctl.d/99-c64stream-udp.conf"
            echo ""
            echo "Without these settings, E2E tests will drop ~30% of packets."
            echo ""
            echo -n "Apply UDP tuning permanently (requires sudo)? [y/N] "
            read -r response
            if [[ "${response}" =~ ^[Yy] ]]; then
                # Create persistent configuration
                cat | sudo tee /etc/sysctl.d/99-c64stream-udp.conf >/dev/null <<EOF
# C64 Stream E2E Test UDP Configuration
# Ensures adequate buffers for high-throughput packet replay (19K packets in ~5s)
net.core.rmem_max = ${target_max}
net.core.wmem_max = ${target_max}
net.core.rmem_default = ${target_default}
net.core.wmem_default = ${target_default}
net.core.netdev_max_backlog = ${target_backlog}
EOF
                # Apply immediately
                sudo sysctl -p /etc/sysctl.d/99-c64stream-udp.conf >/dev/null 2>&1
                log_success "UDP tuning applied permanently"
            else
                log_warning "Skipping UDP tuning - tests will drop packets (expect ~30% loss)"
            fi
        fi
    fi

    # Verify final state
    local new_rmem_max new_rmem_default new_netdev_backlog
    new_rmem_max=$(cat /proc/sys/net/core/rmem_max 2>/dev/null || echo "0")
    new_rmem_default=$(cat /proc/sys/net/core/rmem_default 2>/dev/null || echo "0")
    new_netdev_backlog=$(cat /proc/sys/net/core/netdev_max_backlog 2>/dev/null || echo "0")

    if [[ "${new_rmem_max}" -ge "${target_max}" ]] && \
       [[ "${new_rmem_default}" -ge "${target_default}" ]] && \
       [[ "${new_netdev_backlog}" -ge "${target_backlog}" ]]; then
        log_success "UDP tuning: default=${new_rmem_default}, max=${new_rmem_max}, backlog=${new_netdev_backlog}"
    else
        # CI/non-interactive: Tests adapt to smaller buffers with MIN_PACKET_DELAY
        log_warning "UDP tuning insufficient: default=${new_rmem_default}, max=${new_rmem_max}, backlog=${new_netdev_backlog}"
        log_info "Tests will adapt with MIN_PACKET_DELAY (expected in CI environments)"
    fi
}

ensure_perf_permissions() {
    if [[ "${PERF_PROFILE}" != true ]]; then
        return 0
    fi

    # Only relevant on Linux.
    if [[ "$(uname -s 2>/dev/null || echo '')" != "Linux" ]]; then
        return 0
    fi

    local paranoid=""
    if [[ -r /proc/sys/kernel/perf_event_paranoid ]]; then
        paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "")
    fi

    # If perf is heavily restricted, offer a one-time sudo setup.
    if [[ -n "${paranoid}" ]] && [[ "${paranoid}" =~ ^[0-9]+$ ]] && [[ "${paranoid}" -ge 3 ]]; then
        log_warning "perf profiling appears blocked (kernel.perf_event_paranoid=${paranoid})."
        log_info "Perf capture needs: kernel.perf_event_paranoid=1 and kernel.kptr_restrict=0"
        log_info "One-time setup script: ${PROJECT_ROOT}/build-aux/install-perf-sudoers.sh"

        # Never block in non-interactive runs.
        if [[ ! -t 0 ]]; then
            log_warning "Non-interactive stdin detected; skipping sudo perf setup."
            return 0
        fi

        if ! command -v sudo >/dev/null 2>&1; then
            log_warning "sudo not available; cannot adjust perf sysctls automatically."
            return 0
        fi

        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "  PERF PROFILING SETUP"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
        echo "  This run requested --perf-profile, but perf is currently blocked."
        echo "  I can apply the needed sysctls and install a sudoers rule so future runs"
        echo "  won't prompt again."
        echo ""
        echo "  Will run (via sudo):"
        echo "    ${PROJECT_ROOT}/build-aux/install-perf-sudoers.sh"
        echo ""
        echo -n "  Proceed? [Y/n] "
        local response
        read -r response
        response=${response:-Y}
        if [[ "${response}" =~ ^[Yy]$ ]]; then
            sudo "${PROJECT_ROOT}/build-aux/install-perf-sudoers.sh" || true
        else
            log_info "Skipping perf setup; perf artifacts may be empty."
        fi
    fi
}

# Setup process priority capabilities for smoother frame delivery
# This allows e2e.py to boost OBS process priority without root
setup_process_priority() {
    # Skip in CI - typically runs as root or in containers
    if [[ "${CI:-false}" == "true" ]] || [[ "${GITHUB_ACTIONS:-false}" == "true" ]]; then
        log_info "CI environment detected - skipping priority setup (not needed)"
        return 0
    fi

    # Check if we can already use renice with negative values
    local test_result
    test_result=$(renice -n -1 -p $$ 2>&1) || true
    if [[ ! "${test_result}" =~ "permission denied" ]] && [[ ! "${test_result}" =~ "Operation not permitted" ]]; then
        log_success "Process priority boost already available"
        return 0
    fi

    log_info "Setting up process priority capabilities for smoother frame delivery..."
    log_info "This helps reduce skipped/repeated frames by giving OBS higher scheduling priority."

    # Check if setcap is available
    if ! command -v setcap &> /dev/null; then
        log_warning "setcap not available - install libcap2-bin for priority boost support"
        log_info "Run: sudo apt-get install libcap2-bin"
        return 0
    fi

    # Get the path to the Python interpreter
    local python_path
    python_path=$(which python3)
    if [[ -z "${python_path}" ]]; then
        log_warning "Python3 not found - cannot set priority capabilities"
        return 0
    fi

    # Resolve symlinks to get the actual binary
    local real_python_path
    real_python_path=$(readlink -f "${python_path}")

    # Check if capability is already set
    local current_caps
    current_caps=$(getcap "${real_python_path}" 2>/dev/null || true)
    if [[ "${current_caps}" =~ "cap_sys_nice" ]]; then
        log_success "Python already has CAP_SYS_NICE capability"
        return 0
    fi

    log_info "Python interpreter: ${real_python_path}"
    log_info "Adding CAP_SYS_NICE capability requires root privileges."
    log_info "This is a one-time setup that enables OBS priority boosting."

    # Never block in non-interactive runs.
    if [[ ! -t 0 ]]; then
        log_info "Non-interactive stdin detected - skipping priority capability prompt"
        return 0
    fi

    # Determine privilege escalation
    local SUDO="sudo"
    if [[ $(id -u) -eq 0 ]]; then
        SUDO=""
    elif ! command -v sudo >/dev/null 2>&1; then
        log_warning "sudo not available and not running as root - cannot set capabilities"
        log_info "Run as root or install sudo, then re-run e2e.sh"
        return 0
    fi

    # Prompt user for confirmation
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  PROCESS PRIORITY SETUP"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "  To reduce skipped/repeated frames, we need to give OBS higher CPU priority."
    echo "  This requires adding the CAP_SYS_NICE capability to Python."
    echo ""
    echo "  Command to run:"
    echo "    ${SUDO} setcap 'cap_sys_nice=eip' ${real_python_path}"
    echo ""
    echo "  This is safe and only affects process scheduling priority."
    echo ""
    echo -n "  Proceed? [Y/n] "

    local response
    read -r response
    response=${response:-Y}

    if [[ ! "${response}" =~ ^[Yy] ]]; then
        log_info "Skipping priority setup - E2E tests will still run but may have more frame anomalies"
        return 0
    fi

    # Set the capability
    if ${SUDO} setcap 'cap_sys_nice=eip' "${real_python_path}"; then
        log_success "CAP_SYS_NICE capability added to Python"
        log_info "OBS will now run with boosted CPU priority for smoother frame delivery"
    else
        log_warning "Failed to set capability - E2E tests will still run but may have more frame anomalies"
    fi

    echo ""
}
