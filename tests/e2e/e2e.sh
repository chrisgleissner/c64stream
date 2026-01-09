#!/bin/bash
set -euo pipefail

#
# C64 Stream - One-Stop E2E Test Script
# Copyright (C) 2025 Christian Gleissner
#
# Licensed under the GNU General Public License v2.0 or later.
# See <https://www.gnu.org/licenses/> for details.
#
# This script provides a complete end-to-end testing solution for the C64 Stream
# OBS plugin. It can be run both locally for development and in CI environments.
#
# Features:
# - Builds the plugin and E2E tools
# - Generates test packets with configurable duration
# - Starts virtual display (Xvfb) for headless testing
# - Installs plugin to OBS
# - Runs complete E2E test with packet replay
# - Validates output and provides detailed reporting
#

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_x86_64"
TEST_DIR="${PROJECT_ROOT}/tests/e2e"
DEFAULT_OUTPUT_DIR="${TEST_DIR}/results"

# Default test parameters
DEFAULT_FORMAT="NTSC"
DEFAULT_FRAMES=300  # Frame-count fallback when --duration is unset
DEFAULT_DURATION=8  # seconds (default); translated to frames per format
DEFAULT_VIDEO_PORT=21000
DEFAULT_AUDIO_PORT=21001
DEFAULT_VERBOSE=false
DEFAULT_SKIP_BUILD=false
DEFAULT_CLEANUP=true
DEFAULT_OBS_ENABLED=true   # OBS integration now implemented
DEFAULT_X11_DISPLAY=":99"
DEFAULT_MONITOR_RESOURCES=true  # Resource monitoring for CI (enabled by default)
DEFAULT_SCENARIO_OVERRIDES=""
DEFAULT_SCENARIO_NAME=""
DEFAULT_PACKET_PATTERN=""
DEFAULT_FULL_FRAME_POP=false
DEFAULT_SCENARIO=""
DEFAULT_CSV_MAX_ROWS=2000  # 0 = unlimited CSV lines (preserve all data)
SCENARIO_CI_SKIPPED=false  # Set by load_scenario if ci_skip=true on CI
SCENARIO_YAML_PATH=""  # Path to scenario.yaml if scenario is loaded
DEFAULT_RUN_ALL_SCENARIOS=false  # Run all scenarios in sequence
DEFAULT_ENABLE_RESOURCE_MONITORING=true  # CPU/GPU/RAM monitoring during packet replay (enabled by default)
DEFAULT_RESOURCE_INTERVAL_MS=500  # Resource monitoring sample interval in ms (internal)
DEFAULT_DISABLE_POPS=false  # Disable A/V sync pops in generated packets
DEFAULT_SETTLING_SECONDS=0  # Ignore early frame progression errors for pass/fail

# Optional CPU profiling (Linux perf). Disabled by default.
DEFAULT_PERF_PROFILE=false
DEFAULT_PERF_FLAMEGRAPH=false
DEFAULT_PERF_FREQUENCY_HZ=99
DEFAULT_PERF_CALLGRAPH="fp"
DEFAULT_PERF_DURATION=""

# Float validation helper (non-negative).
is_non_negative_number() {
    local value="$1"
    [[ "${value}" =~ ^[0-9]+([.][0-9]+)?$ ]]
}

# Round float to nearest int (0.5 rounds up).
round_to_int() {
    local value="$1"
    awk -v x="${value}" 'BEGIN{printf "%d", (x<0?int(x-0.5):int(x+0.5))}'
}

# Scenario directory
SCENARIOS_DIR="${TEST_DIR}/scenarios"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $*"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

join_by() {
    local sep="$1"; shift || true
    local out=""; local first=1
    for part in "$@"; do
        if (( first )); then
            out="$part"; first=0
        else
            out+="${sep}${part}"
        fi
    done
    printf "%s" "$out"
}

format_to_one_decimal() {
    local value="$1"
    if [[ -z "${value}" || "${value}" == "null" ]]; then
        echo ""
        return
    fi
    LC_ALL=C printf '%.1f' "${value}"
}

format_seconds_to_timestamp() {
    local seconds="$1"
    if [[ -z "${seconds}" || "${seconds}" == "null" ]]; then
        echo ""
        return
    fi
    # Convert to tenths with rounding
    local tenths
    tenths=$(awk -v s="${seconds}" 'BEGIN{printf "%.0f", s*10}')
    local hours=$((tenths / 36000))
    local rem=$((tenths % 36000))
    local minutes=$((rem / 600))
    rem=$((rem % 600))
    local sec=$((rem / 10))
    local tenth=$((rem % 10))
    if (( hours > 0 )); then
        printf "%02d:%02d:%02d.%d" "${hours}" "${minutes}" "${sec}" "${tenth}"
    else
        printf "%02d:%02d.%d" "${minutes}" "${sec}" "${tenth}"
    fi
}

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

    local current_rmem_max=""
    if [[ -r /proc/sys/net/core/rmem_max ]]; then
        current_rmem_max=$(cat /proc/sys/net/core/rmem_max 2>/dev/null || echo "")
    fi

    local target_buffer=8388608  # 8MB

    if [[ -n "${current_rmem_max}" ]] && [[ "${current_rmem_max}" =~ ^[0-9]+$ ]] && [[ "${current_rmem_max}" -ge "${target_buffer}" ]]; then
        return 0  # Already adequate
    fi

    # Try to increase permanently (best-effort, will fail in CI containers)
    # Only attempt if we have sudo AND we're in an interactive session
    if [[ "$(id -u)" == "0" ]]; then
        # Running as root - apply directly and make persistent
        sysctl -w net.core.rmem_max=${target_buffer} >/dev/null 2>&1
        sysctl -w net.core.wmem_max=${target_buffer} >/dev/null 2>&1
        # Make persistent
        echo "net.core.rmem_max = ${target_buffer}" > /etc/sysctl.d/99-c64stream-udp.conf
        echo "net.core.wmem_max = ${target_buffer}" >> /etc/sysctl.d/99-c64stream-udp.conf
    elif command -v sudo >/dev/null 2>&1 && [[ -t 0 ]]; then
        # Check if persistent config already exists (skip prompt if so)
        if [[ -f "/etc/sysctl.d/99-c64stream-udp.conf" ]]; then
            log_info "UDP buffer config already exists: /etc/sysctl.d/99-c64stream-udp.conf"
            # Reapply it in case current kernel value is lower
            sudo sysctl -p /etc/sysctl.d/99-c64stream-udp.conf >/dev/null 2>&1
        else
            # Interactive terminal: offer to increase buffers permanently (one-time setup)
            log_warning "UDP receive buffer too small: ${current_rmem_max} bytes (< ${target_buffer} recommended)"
            log_info "High-jitter tests may drop packets without larger buffers."
            echo ""
            echo "This is a one-time setup that will:"
            echo "  1. Increase UDP buffers to 8MB immediately"
            echo "  2. Make the change persistent (survives reboots)"
            echo "  3. Create: /etc/sysctl.d/99-c64stream-udp.conf"
            echo ""
            echo -n "Increase UDP buffers permanently (requires sudo)? [y/N] "
            read -r response
            if [[ "${response}" =~ ^[Yy] ]]; then
                # Create persistent configuration
                echo "net.core.rmem_max = ${target_buffer}" | sudo tee /etc/sysctl.d/99-c64stream-udp.conf >/dev/null
                echo "net.core.wmem_max = ${target_buffer}" | sudo tee -a /etc/sysctl.d/99-c64stream-udp.conf >/dev/null
                # Apply immediately
                sudo sysctl -p /etc/sysctl.d/99-c64stream-udp.conf >/dev/null 2>&1
                log_success "UDP buffers increased permanently"
            else
                log_info "Skipping UDP buffer increase (tests will use MIN_PACKET_DELAY)"
            fi
        fi
    fi

    local new_rmem_max=$(cat /proc/sys/net/core/rmem_max 2>/dev/null || echo "0")
    if [[ "${new_rmem_max}" -ge "${target_buffer}" ]]; then
        log_success "UDP buffers: ${new_rmem_max} bytes"
    else
        # CI/non-interactive: Tests adapt to smaller buffers with MIN_PACKET_DELAY
        log_info "UDP buffers: ${new_rmem_max} bytes (tests will use MIN_PACKET_DELAY)"
        log_info "This is expected in CI environments (kernel parameters are read-only)"
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

# Help message
show_help() {
    cat << EOF
C64 Stream E2E Test Script

Usage: $0 [OPTIONS]

This script runs comprehensive end-to-end tests for the C64 Stream OBS plugin.
It builds the plugin, generates test packets, and validates the complete pipeline.

OPTIONS:
    -f, --format FORMAT     Video format (PAL, NTSC) [default: ${DEFAULT_FORMAT}]
    -F, --frames FRAMES     Number of frames to test (overridden by --duration) [default: ${DEFAULT_FRAMES}]
    -d, --duration SECONDS  Test duration in seconds (overrides --frames); supports fractions (e.g. 12.5) [default: ${DEFAULT_DURATION}]
    --output-dir DIR        Output directory for test artifacts [default: ${DEFAULT_OUTPUT_DIR}]
    --csv-max-rows LINES    Truncate CSV files to first LINES lines incl header (0=disable) [default: ${DEFAULT_CSV_MAX_ROWS}]
    -v, --verbose           Enable verbose logging
    -s, --skip-build        Skip building plugin and tools
    -o, --obs               Enable OBS integration (default)
    --no-obs                Disable OBS integration
    --no-cleanup            Skip cleanup of temporary files
    --disable-pops          Disable A/V sync pops in generated packets (for testing)
    --settling-duration SEC Ignore frame progression errors during first SEC seconds; supports fractions (e.g. 4.0) [default: ${DEFAULT_SETTLING_SECONDS}]
                           (alias: --settling-seconds)
    --monitor-resources     Enable periodic system resource monitoring
    --enable-resource-monitoring  Enable CPU/GPU/RAM monitoring during packet replay (saves to resource.csv/json)
    --monitor-resource-duration SEC  Resource monitoring sample interval in seconds; supports fractions (e.g. 0.5) [default: $(awk 'BEGIN{printf "%.3f", '${DEFAULT_RESOURCE_INTERVAL_MS}'/1000.0}')]
                                   (alias: --resource-interval-ms)
    --perf-profile          Record a perf CPU profile of OBS during packet replay (best-effort; may require perf permissions)
    --perf-flamegraph       Generate flamegraph.svg if FlameGraph scripts are installed (tools/FlameGraph or /usr/share/flamegraph)
    --perf-frequency-hz HZ  perf sampling frequency [default: ${DEFAULT_PERF_FREQUENCY_HZ}]
    --perf-callgraph MODE   perf callgraph mode (dwarf|fp) [default: ${DEFAULT_PERF_CALLGRAPH}]
    --perf-duration SEC     Override perf capture duration in seconds (default: auto from frames+grace)
    --all                   Run ALL scenarios in sequence
    -h, --help             Show this help message

EXAMPLES:
    # Quick test (default is ~12s on PAL/NTSC)
    $0

    # Extended 30-second stress test
    $0 --duration 30 --verbose

    # NTSC format test with custom ports
    $0 --format NTSC --video-port 12000 --audio-port 12001

    # Development mode - skip build, keep artifacts
    $0 --skip-build --no-cleanup --verbose

    # Full integration test with OBS (when available)
    $0 --obs --duration 10

    # Run a specific scenario (auto-discovers format and settings)
    $0 --scenario ntsc_amber_monitor --verbose

    # Run ALL scenarios (results saved to results/<scenario>/)
    $0 --all --verbose

    # List available scenarios
    $0 --list-scenarios

PACKET GENERATION:
    PAL:  50 FPS, 384x272 resolution, ~3400 packets/sec
    NTSC: 60 FPS, 384x240 resolution, ~4080 packets/sec

    Video packets: 780 bytes, ~300μs intervals
    Audio packets: ~4ms intervals

OUTPUT:
    Test artifacts are saved to: \${OUTPUT_DIR}
    - Generated packets: test_packets/
    - Test logs: \${OUTPUT_DIR}/
    - Recordings (if OBS enabled): recording_*.mp4

EOF
}

# List available scenarios
list_scenarios() {
    echo "Available E2E scenarios:"
    echo ""
    python3 "${TEST_DIR}/scenario_loader.py" --list 2>/dev/null || {
        if [[ -d "${SCENARIOS_DIR}" ]]; then
            for scenario_dir in "${SCENARIOS_DIR}"/*/; do
                if [[ -f "${scenario_dir}scenario.yaml" ]]; then
                    local name
                    name=$(basename "${scenario_dir}")
                    local display_name format preset ci_skip
                    display_name=$(grep "^name:" "${scenario_dir}scenario.yaml" | sed 's/^name: *//')
                    format=$(grep "^format:" "${scenario_dir}scenario.yaml" | sed 's/^format: *//')
                    preset=$(grep "^preset:" "${scenario_dir}scenario.yaml" | sed 's/^preset: *//')
                    ci_skip=$(grep "^ci_skip:" "${scenario_dir}scenario.yaml" | sed 's/^ci_skip: *//')
                    local ci_marker=""
                    if [[ "${ci_skip}" == "true" ]]; then
                        ci_marker=" [CI-SKIP]"
                    fi
                    printf "  %-25s %s (%s, preset: %s)%s\n" "${name}:" "${display_name}" "${format}" "${preset:-Default}" "${ci_marker}"
                fi
            done
        else
            echo "  No scenarios found in ${SCENARIOS_DIR}"
        fi
    }
    echo ""
}

# Load scenario configuration from scenario.yaml
load_scenario() {
    local scenario_name="$1"
    local scenario_dir="${SCENARIOS_DIR}/${scenario_name}"
    local scenario_yaml="${scenario_dir}/scenario.yaml"

    # Make scenario_yaml path available globally
    SCENARIO_YAML_PATH="${scenario_yaml}"

    if [[ ! -f "${scenario_yaml}" ]]; then
        log_error "Scenario not found: ${scenario_name}"
        log_error "Expected: ${scenario_yaml}"
        log_info "Use --list-scenarios to see available scenarios"
        exit 1
    fi

    log_info "Loading scenario: ${scenario_name}"

    # Parse scenario.yaml (new concise format)
    local name format preset pattern full_frame_pop csv_max_rows
    name=$(grep -m1 "^name:" "${scenario_yaml}" | sed 's/^name: *//' || true)
    format=$(grep -m1 "^format:" "${scenario_yaml}" | sed 's/^format: *//' || true)
    preset=$(grep -m1 "^preset:" "${scenario_yaml}" | sed 's/^preset: *//' || true)
    pattern=$(grep -m1 "^pattern:" "${scenario_yaml}" | sed 's/^pattern: *//' || true)
    full_frame_pop=$(grep -m1 "^full_frame_pop:" "${scenario_yaml}" | sed 's/^full_frame_pop: *//' || true)
    csv_max_rows=$(grep -m1 "^csv_max_rows:" "${scenario_yaml}" | sed 's/^csv_max_rows: *//' || true)

    if [[ -z "${name}" || -z "${format}" ]]; then
        log_error "Invalid scenario.yaml (missing required fields)"
        log_error "Scenario: ${scenario_name}"
        log_error "File: ${scenario_yaml}"
        log_error "Expected at least: name:, format:"
        exit 1
    fi

    # Check for CI-skip flag
    local ci_skip ci_skip_reason
    ci_skip=$(grep -m1 "^ci_skip:" "${scenario_yaml}" | sed 's/^ci_skip: *//' || true)
    ci_skip_reason=$(grep -m1 "^ci_skip_reason:" "${scenario_yaml}" | sed 's/^ci_skip_reason: *//' | tr -d '"' || true)

    if [[ "${ci_skip}" == "true" ]] && [[ "${CI:-false}" == "true" || "${GITHUB_ACTIONS:-false}" == "true" ]]; then
        log_warning "⏭️  Skipping scenario '${name}' on CI"
        if [[ -n "${ci_skip_reason}" ]]; then
            log_info "  Reason: ${ci_skip_reason}"
        fi
        log_info "  This scenario requires hardware rendering (run locally)"
        SCENARIO_CI_SKIPPED=true
        return 0
    fi

    # Set FORMAT from scenario if not explicitly set via CLI
    if [[ "${FORMAT}" == "${DEFAULT_FORMAT}" ]]; then
        FORMAT="${format}"
        log_info "  Format: ${FORMAT} (from scenario)"
    else
        log_info "  Format: ${FORMAT} (from CLI, overrides scenario: ${format})"
    fi

    # Set SCENARIO_NAME (display name) and SCENARIO_ID (directory name) if not already set
    if [[ -z "${SCENARIO_NAME}" ]]; then
        SCENARIO_NAME="${name}"
    fi
    SCENARIO_ID="${scenario_name}"  # Always use the directory name as ID

    # Optional packet pattern (solid/diagonal) for scanline-specific scenarios
    if [[ -n "${pattern}" ]]; then
        PACKET_PATTERN="${pattern}"
        log_info "  Packet pattern: ${PACKET_PATTERN}"
    fi
    if [[ "${full_frame_pop}" == "true" ]]; then
        FULL_FRAME_POP=true
        log_info "  Packet mode: full-frame-pop"
    fi
    if [[ -n "${csv_max_rows}" ]]; then
        CSV_MAX_ROWS="${csv_max_rows}"
        log_info "  CSV max rows: ${CSV_MAX_ROWS}"
    fi

    # Generate OBS scene JSON from scenario
    local generated_dir="${scenario_dir}/generated"
    mkdir -p "${generated_dir}/basic/scenes"
    if [[ "${VERBOSE}" == true ]]; then
        python3 "${TEST_DIR}/scenario_loader.py" --scenario "${scenario_name}" \
            --output "${generated_dir}/basic/scenes/C64StreamTest.json"
    else
        python3 "${TEST_DIR}/scenario_loader.py" --scenario "${scenario_name}" \
            --output "${generated_dir}/basic/scenes/C64StreamTest.json" 2>/dev/null
    fi

    if [[ $? -eq 0 ]]; then
        # Copy static overrides from scenario's overrides directory if present
        local static_overrides="${scenario_dir}/overrides"
        if [[ -d "${static_overrides}" ]]; then
            cp -r "${static_overrides}"/* "${generated_dir}/" 2>/dev/null || true
            log_info "  Static overrides: ${static_overrides}"
        fi

        SCENARIO_OVERRIDES="${generated_dir}"
        log_info "  Preset: ${preset:-Default}"
        log_info "  Generated scene: ${generated_dir}/basic/scenes/C64StreamTest.json"
    else
        log_error "Failed to generate scene JSON from scenario"
        exit 1
    fi
}

# Parse command line arguments
parse_args() {
    FORMAT="${DEFAULT_FORMAT}"
    FRAMES="${DEFAULT_FRAMES}"
    DURATION="${DEFAULT_DURATION}"
    OUTPUT_DIR="${DEFAULT_OUTPUT_DIR}"
    CSV_MAX_ROWS="${DEFAULT_CSV_MAX_ROWS}"
    VIDEO_PORT="${DEFAULT_VIDEO_PORT}"
    AUDIO_PORT="${DEFAULT_AUDIO_PORT}"
    VERBOSE="${DEFAULT_VERBOSE}"
    SKIP_BUILD="${DEFAULT_SKIP_BUILD}"
    CLEANUP="${DEFAULT_CLEANUP}"
    OBS_ENABLED="${DEFAULT_OBS_ENABLED}"
    MONITOR_RESOURCES="${DEFAULT_MONITOR_RESOURCES}"
    SCENARIO_OVERRIDES="${DEFAULT_SCENARIO_OVERRIDES}"
    SCENARIO_NAME="${DEFAULT_SCENARIO_NAME}"
    SCENARIO="${DEFAULT_SCENARIO}"
    PACKET_PATTERN="${DEFAULT_PACKET_PATTERN}"
    FULL_FRAME_POP="${DEFAULT_FULL_FRAME_POP}"
    RUN_ALL_SCENARIOS="${DEFAULT_RUN_ALL_SCENARIOS}"
    ENABLE_RESOURCE_MONITORING="${DEFAULT_ENABLE_RESOURCE_MONITORING}"
    RESOURCE_INTERVAL_MS="${DEFAULT_RESOURCE_INTERVAL_MS}"
    DISABLE_POPS="${DEFAULT_DISABLE_POPS}"
    SETTLING_SECONDS="${DEFAULT_SETTLING_SECONDS}"

    PERF_PROFILE="${DEFAULT_PERF_PROFILE}"
    PERF_FLAMEGRAPH="${DEFAULT_PERF_FLAMEGRAPH}"
    PERF_FREQUENCY_HZ="${DEFAULT_PERF_FREQUENCY_HZ}"
    PERF_CALLGRAPH="${DEFAULT_PERF_CALLGRAPH}"
    PERF_DURATION="${DEFAULT_PERF_DURATION}"

    # New user-facing option naming: durations in seconds (float). Kept for consistent parsing.
    # When unset, we derive defaults from *_MS / *_SECONDS.
    MONITOR_RESOURCE_DURATION=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            -f|--format)
                FORMAT="$2"
                if [[ "${FORMAT}" != "PAL" && "${FORMAT}" != "NTSC" ]]; then
                    log_error "Invalid format: ${FORMAT}. Must be PAL or NTSC."
                    exit 1
                fi
                shift 2
                ;;
            -F|--frames)
                FRAMES="$2"
                if ! [[ "${FRAMES}" =~ ^[0-9]+$ ]] || [[ "${FRAMES}" -lt 1 ]]; then
                    log_error "Invalid frame count: ${FRAMES}. Must be a positive integer."
                    exit 1
                fi
                # Explicit --frames overrides duration-based default.
                DURATION=""
                shift 2
                ;;
            -d|--duration)
                DURATION="$2"
                if ! is_non_negative_number "${DURATION}"; then
                    log_error "Invalid duration: ${DURATION}. Must be a non-negative number (supports fractions)."
                    exit 1
                fi
                # Disallow zero duration (keeps previous behavior of requiring >=1s).
                if awk -v x="${DURATION}" 'BEGIN{exit !(x>0.0)}'; then
                    :
                else
                    log_error "Invalid duration: ${DURATION}. Must be > 0."
                    exit 1
                fi
                shift 2
                ;;
            --output-dir)
                OUTPUT_DIR="$2"
                if [[ -z "${OUTPUT_DIR}" ]]; then
                    log_error "Output directory cannot be empty."
                    exit 1
                fi
                shift 2
                ;;
            --csv-max-rows)
                CSV_MAX_ROWS="$2"
                if ! [[ "${CSV_MAX_ROWS}" =~ ^[0-9]+$ ]]; then
                    log_error "Invalid CSV max rows: ${CSV_MAX_ROWS}. Must be a non-negative integer."
                    exit 1
                fi
                shift 2
                ;;
            --video-port)
                VIDEO_PORT="$2"
                if ! [[ "${VIDEO_PORT}" =~ ^[0-9]+$ ]] || [[ "${VIDEO_PORT}" -lt 1024 ]] || [[ "${VIDEO_PORT}" -gt 65535 ]]; then
                    log_error "Invalid video port: ${VIDEO_PORT}. Must be between 1024-65535."
                    exit 1
                fi
                shift 2
                ;;
            --audio-port)
                AUDIO_PORT="$2"
                if ! [[ "${AUDIO_PORT}" =~ ^[0-9]+$ ]] || [[ "${AUDIO_PORT}" -lt 1024 ]] || [[ "${AUDIO_PORT}" -gt 65535 ]]; then
                    log_error "Invalid audio port: ${AUDIO_PORT}. Must be between 1024-65535."
                    exit 1
                fi
                shift 2
                ;;
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -s|--skip-build)
                SKIP_BUILD=true
                shift
                ;;
            -o|--obs)
                OBS_ENABLED=true
                shift
                ;;
            --no-obs)
                OBS_ENABLED=false
                shift
                ;;
            --no-cleanup)
                CLEANUP=false
                shift
                ;;
            --disable-pops)
                DISABLE_POPS=true
                shift
                ;;
            --settling-duration|--settling-seconds)
                SETTLING_SECONDS="$2"
                if ! is_non_negative_number "${SETTLING_SECONDS}"; then
                    log_error "Invalid settling duration: ${SETTLING_SECONDS}. Must be a non-negative number (supports fractions)."
                    exit 1
                fi
                shift 2
                ;;
            --monitor-resources)
                MONITOR_RESOURCES=true
                shift
                ;;
            --enable-resource-monitoring)
                ENABLE_RESOURCE_MONITORING=true
                shift
                ;;
            --monitor-resource-duration)
                MONITOR_RESOURCE_DURATION="$2"
                if ! is_non_negative_number "${MONITOR_RESOURCE_DURATION}"; then
                    log_error "Invalid monitor resource duration: ${MONITOR_RESOURCE_DURATION}. Must be a non-negative number (supports fractions)."
                    exit 1
                fi
                # Enforce >= 0.1s (matches prior >=100ms).
                if awk -v x="${MONITOR_RESOURCE_DURATION}" 'BEGIN{exit !(x>=0.1)}'; then
                    :
                else
                    log_error "Invalid monitor resource duration: ${MONITOR_RESOURCE_DURATION}. Must be >= 0.1s."
                    exit 1
                fi
                shift 2
                ;;
            --perf-profile)
                PERF_PROFILE=true
                shift
                ;;
            --perf-flamegraph)
                PERF_FLAMEGRAPH=true
                shift
                ;;
            --perf-frequency-hz)
                PERF_FREQUENCY_HZ="$2"
                if ! [[ "${PERF_FREQUENCY_HZ}" =~ ^[0-9]+$ ]] || [[ "${PERF_FREQUENCY_HZ}" -lt 1 ]]; then
                    log_error "Invalid perf frequency: ${PERF_FREQUENCY_HZ}. Must be a positive integer."
                    exit 1
                fi
                shift 2
                ;;
            --perf-callgraph)
                PERF_CALLGRAPH="$2"
                if [[ "${PERF_CALLGRAPH}" != "dwarf" && "${PERF_CALLGRAPH}" != "fp" ]]; then
                    log_error "Invalid perf callgraph mode: ${PERF_CALLGRAPH}. Must be dwarf or fp."
                    exit 1
                fi
                shift 2
                ;;
            --perf-duration)
                PERF_DURATION="$2"
                if ! is_non_negative_number "${PERF_DURATION}"; then
                    log_error "Invalid perf duration: ${PERF_DURATION}. Must be a non-negative number (supports fractions)."
                    exit 1
                fi
                # Require >0 when explicitly set
                if awk -v x="${PERF_DURATION}" 'BEGIN{exit !(x>0.0)}'; then
                    :
                else
                    log_error "Invalid perf duration: ${PERF_DURATION}. Must be > 0."
                    exit 1
                fi
                shift 2
                ;;
            --resource-interval-ms)
                RESOURCE_INTERVAL_MS="$2"
                if ! [[ "${RESOURCE_INTERVAL_MS}" =~ ^[0-9]+$ ]] || [[ "${RESOURCE_INTERVAL_MS}" -lt 100 ]]; then
                    log_error "Invalid resource interval: ${RESOURCE_INTERVAL_MS}. Must be >= 100ms."
                    exit 1
                fi
                shift 2
                ;;
            --display)
                DEFAULT_X11_DISPLAY="$2"
                shift 2
                ;;
            --scenario)
                SCENARIO="$2"
                shift 2
                ;;
            --scenario-overrides)
                SCENARIO_OVERRIDES="$2"
                shift 2
                ;;
            --scenario-name)
                SCENARIO_NAME="$2"
                shift 2
                ;;
            --list-scenarios)
                list_scenarios
                exit 0
                ;;
            --all)
                RUN_ALL_SCENARIOS=true
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done

    # Load scenario configuration if specified
    if [[ -n "${SCENARIO}" ]]; then
        load_scenario "${SCENARIO}"
    fi

    # Check if scenario was skipped for CI
    if [[ "${SCENARIO_CI_SKIPPED}" == "true" ]]; then
        log_success "Scenario skipped on CI (success)"
        exit 0
    fi

    # Default output layout: keep all artifacts under tests/e2e/results/.
    # If a scenario is specified and --output-dir wasn't overridden, write into results/<scenario>/.
    if [[ -n "${SCENARIO}" ]] && [[ "${OUTPUT_DIR}" == "${DEFAULT_OUTPUT_DIR}" ]]; then
        OUTPUT_DIR="${DEFAULT_OUTPUT_DIR}/${SCENARIO}"
    fi

    # Calculate frames from duration if specified
    if [[ -n "${DURATION}" ]]; then
        local fps
        if [[ "${FORMAT}" == "PAL" ]]; then
            fps=50  # Simplified PAL frame rate for frame count calculation
        else
            fps=60  # Simplified NTSC frame rate for frame count calculation
        fi
        # Round to nearest frame to keep total duration close to user intent.
        FRAMES=$(awk -v d="${DURATION}" -v f="${fps}" 'BEGIN{printf "%d", int(d*f + 0.5)}')
        if [[ "${FRAMES}" -lt 1 ]]; then
            log_error "Duration ${DURATION}s is too short (yields <1 frame)."
            exit 1
        fi
        log_info "Duration ${DURATION}s ≈ ${FRAMES} frames for ${FORMAT}"
    fi

    # Convert monitor resource duration (seconds) to internal ms if provided.
    if [[ -n "${MONITOR_RESOURCE_DURATION}" ]]; then
        RESOURCE_INTERVAL_MS=$(awk -v d="${MONITOR_RESOURCE_DURATION}" 'BEGIN{printf "%d", int(d*1000.0 + 0.5)}')
    fi
}

# Check system dependencies
check_dependencies() {
    log_info "Checking system dependencies..."

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

# Build plugin and tools
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

# Stop real C64 Ultimate device from streaming to prevent cross-pollution
stop_real_c64_streaming() {
    local c64_host="c64u"
    local reset_endpoint="/v1/machine:reset"
    local reset_method="PUT"

    # Check if c64u is reachable
    if ! host "${c64_host}" >/dev/null 2>&1 && ! ping -c 1 -W 1 "${c64_host}" >/dev/null 2>&1; then
        if [[ "${VERBOSE}" == true ]]; then
            log_info "Real C64 device (${c64_host}) not reachable - skipping reset"
        fi
        return 0
    fi

    log_info "Stopping real C64 device streaming to prevent test cross-pollution..."
    log_warning "NOTE: If C64U is configured to stream to ports 21000/21001, cross-pollution may occur!"
    log_warning "Real device tests should use ports 11000/11001 (C64U hardware default)."
    log_warning "To fix: Reconfigure C64U via web interface to use default UDP ports 11000/11001."
    
    # Attempt to reset via REST API (may not work if device auto-restarts streaming)
    if command -v curl &>/dev/null; then
        local url="http://${c64_host}${reset_endpoint}"
        if curl -s -X "${reset_method}" "${url}" >/dev/null 2>&1; then
            if [[ "${VERBOSE}" == true ]]; then
                log_success "Reset request sent to ${c64_host}"
            fi
        else
            if [[ "${VERBOSE}" == true ]]; then
                log_warning "Could not reset real C64 device - it may not be running or REST API unavailable"
                log_warning "Continuing anyway, but test may receive real device packets if it's streaming"
            fi
        fi
    fi
    
    sleep 1
    return 0
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

# Generate test packets
generate_packets() {
    log_info "Generating ${FORMAT} test packets (${FRAMES} frames)..."

    cd "${TEST_DIR}"

    # Create output directory
    rm -rf test_packets
    mkdir -p test_packets

    # Generate packets
    local cmd=(
        "./generate_packets.py"
        "--frames" "${FRAMES}"
        "--format" "${FORMAT}"
        "--output" "test_packets"
        "--scenario" "${SCENARIO_ID:-DEFAULT}"
    )

    # Optional pattern selection (useful for scanline and sharp pixel assertions).
    # Supported by our packet generator: diagonal (default), solid (uniform field), or dots (sparse white dots).
    if [[ -n "${PACKET_PATTERN}" ]]; then
        if [[ "${PACKET_PATTERN}" != "diagonal" && "${PACKET_PATTERN}" != "solid" && "${PACKET_PATTERN}" != "dots" ]]; then
            log_error "Invalid packet pattern: ${PACKET_PATTERN} (expected: diagonal|solid|dots)"
            exit 1
        fi
        cmd+=("--pattern" "${PACKET_PATTERN}")
    fi
    if [[ "${FULL_FRAME_POP}" == true ]]; then
        cmd+=("--full-frame-pop")
    fi

    # Optional pop disabling (useful for testing network strain hypothesis)
    if [[ "${DISABLE_POPS}" == true ]]; then
        cmd+=("--disable-pops")
    fi

    if [[ "${VERBOSE}" == true ]]; then
        log_info "Running: ${cmd[*]}"
        "${cmd[@]}"
    else
        "${cmd[@]}" > /dev/null 2>&1
    fi

    if [[ $? -ne 0 ]]; then
        log_error "Packet generation failed"
        exit 1
    fi

    # Verify generated packets
    local video_count audio_count
    video_count=$(find test_packets/video/"${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)
    audio_count=$(find test_packets/audio/"${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)

    log_success "Generated ${video_count} video packets, ${audio_count} audio packets"
}

# Run E2E test
run_e2e_test() {
    if [[ -n "${SCENARIO_NAME}" ]]; then
        log_info "Running E2E test for scenario: ${SCENARIO_NAME}"
    else
        log_info "Running E2E test..."
    fi

    cd "${TEST_DIR}"

    # Prepare output directory
    mkdir -p "${OUTPUT_DIR}"

    # Best-effort UDP buffer size increase (prevents packet loss in E2E tests).
    ensure_udp_buffers

    # Best-effort perf permissions setup (local dev only).
    ensure_perf_permissions

    # Determine udp_replay path
    local udp_replay_path="${BUILD_DIR}/tests/e2e/udp_replay"
    if [[ ! -f "${udp_replay_path}" ]]; then
        # If the prebuilt tool doesn't exist (e.g., when --skip-build is used),
        # let the Python harness auto-build into the current directory.
        udp_replay_path="./udp_replay"
    fi

    # Build test command
    local cmd=(
        "python3" "./e2e.py"
        "--test-dir" "."
        "--output-dir" "${OUTPUT_DIR}"
        "--format" "${FORMAT}"
        "--frames" "${FRAMES}"
        "--settling-duration" "${SETTLING_SECONDS}"
        "--video-port" "${VIDEO_PORT}"
        "--audio-port" "${AUDIO_PORT}"
        "--udp-replay" "${udp_replay_path}"
    )

    if [[ "${OBS_ENABLED}" == true ]]; then
        cmd+=("--enable-websocket")
    fi

    if [[ -n "${SCENARIO_NAME}" ]]; then
        cmd+=("--scenario-name" "${SCENARIO_NAME}")
    fi

    # Provide stable scenario id (folder name) for gating scenario-specific checks.
    if [[ -n "${SCENARIO}" ]]; then
        cmd+=("--scenario-id" "${SCENARIO}")
    fi

    # Pass scenario overrides if provided
    if [[ -n "${SCENARIO_OVERRIDES}" ]]; then
        cmd+=("--scenario-overrides" "${SCENARIO_OVERRIDES}")
    fi

    # Pass scenario YAML path if available (for network_simulation config)
    if [[ -n "${SCENARIO_YAML_PATH}" ]]; then
        cmd+=("--scenario-yaml" "${SCENARIO_YAML_PATH}")
    fi

    # Pass CSV max rows
    cmd+=("--csv-max-rows" "${CSV_MAX_ROWS}")

    # Ensure X environment variables are set
    export DISPLAY="${DEFAULT_X11_DISPLAY}"

    # Only set CI-specific Qt/GL environment variables in CI environment
    if [[ "${CI:-false}" == "true" ]] || [[ "${GITHUB_ACTIONS:-false}" == "true" ]]; then
        export QT_QPA_PLATFORM=xcb
        export QT_X11_NO_MITSHM=1
        export LIBGL_ALWAYS_SOFTWARE=1
        log_info "🏗️ CI environment detected - applied Qt/GL environment variables"
    else
        log_info "🚀 Local environment detected - using default Qt/GL settings"
    fi

    if [[ "${VERBOSE}" == true ]]; then
        cmd+=("--verbose")
    fi

    # Pass resource monitoring options
    if [[ "${ENABLE_RESOURCE_MONITORING}" == true ]]; then
        cmd+=("--enable-resource-monitoring")
        local resource_interval_sec
        resource_interval_sec=$(awk -v ms="${RESOURCE_INTERVAL_MS}" 'BEGIN{printf "%.3f", ms/1000.0}')
        cmd+=("--monitor-resource-duration" "${resource_interval_sec}")
    fi

    # Optional perf profiling (best-effort)
    if [[ "${PERF_PROFILE}" == true ]]; then
        cmd+=("--perf-profile")
        cmd+=("--perf-frequency-hz" "${PERF_FREQUENCY_HZ}")
        cmd+=("--perf-callgraph" "${PERF_CALLGRAPH}")
        if [[ -n "${PERF_DURATION}" ]]; then
            cmd+=("--perf-duration" "${PERF_DURATION}")
        fi
        if [[ "${PERF_FLAMEGRAPH}" == true ]]; then
            cmd+=("--perf-flamegraph")
        fi
    fi

    # Run test
    local test_result=0
    if ! "${cmd[@]}"; then
        test_result=1
        log_warning "E2E test encountered issues"
    else
        log_success "E2E test completed successfully"
    fi

    return ${test_result}
}

# Run scenario-specific assertions against the recording
run_scenario_assertions() {
    log_info "Running scenario assertions for: ${SCENARIO}"

    cd "${TEST_DIR}"

    # Find the recording file
    local recording_file=""
    if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" ]]; then
        recording_file="${OUTPUT_DIR}/c64_recording.mp4"
    else
        log_warning "No recording found for assertions"
        return 0  # Not a fatal error
    fi

    # Run assertions using the assertions module
    local cmd=(
        "python3" "-m" "assertions"
        "--mp4" "${recording_file}"
        "--scenario" "${SCENARIO}"
        "--settling-duration" "${SETTLING_SECONDS}"
    )

    if [[ "${VERBOSE}" == true ]]; then
        cmd+=("--verbose")
    fi

    log_info "Running: ${cmd[*]}"

    local assertion_result=0
    if ! "${cmd[@]}"; then
        assertion_result=1
        log_error "Scenario assertions failed"
    else
        log_success "Scenario assertions passed"
    fi

    return ${assertion_result}
}

# Generate playback.csv from validation_results.json
# This creates a dense, frame-complete timeline showing playback anomalies (skips/repeats)
generate_playback_csv() {
    local validation_file="${OUTPUT_DIR}/validation_results.json"
    local playback_csv="${OUTPUT_DIR}/playback.csv"
    local obs_csv=""

    if [[ ! -f "${validation_file}" ]] || ! command -v jq >/dev/null 2>&1; then
        log_info "Skipping playback.csv generation (validation_results.json not found or jq not available)"
        return 0
    fi

    # Check if frame_sequence_box results are available
    local has_frame_seq
    has_frame_seq=$(jq -r 'has("frame_sequence_box")' "${validation_file}" 2>/dev/null || echo "false")
    if [[ "${has_frame_seq}" != "true" ]]; then
        log_info "Skipping playback.csv generation (no frame sequence data)"
        return 0
    fi

    # Find obs.csv - check session folders
    local session_folder
    session_folder=$(find "${OUTPUT_DIR}" -maxdepth 2 -name "obs.csv" -type f 2>/dev/null | head -1)
    if [[ -n "${session_folder}" ]]; then
        obs_csv="${session_folder}"
    fi

    log_info "Generating playback.csv..."

    # Get the recording file to determine total frame count and frame rate
    local recording_mp4=""
    if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" ]]; then
        recording_mp4="${OUTPUT_DIR}/c64_recording.mp4"
    elif compgen -G "${OUTPUT_DIR}/*.mp4" > /dev/null; then
        recording_mp4=$(ls -t ${OUTPUT_DIR}/*.mp4 2>/dev/null | head -1)
    fi

    # Use Python to generate the CSV (complex logic with frame mapping)
    python3 - "${validation_file}" "${playback_csv}" "${recording_mp4}" "${FORMAT}" "${obs_csv}" <<'PYTHON'
import sys
import json
import csv
from pathlib import Path

validation_file = Path(sys.argv[1])
playback_csv = Path(sys.argv[2])
recording_mp4 = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
video_format = sys.argv[4] if len(sys.argv) > 4 else "NTSC"
obs_csv_path = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] else None

# Load validation results
with open(validation_file, 'r') as f:
    data = json.load(f)

frame_seq = data.get("frame_sequence_box", {})
details = frame_seq.get("details", {})

# Get frame rate and window info
fps = details.get("window", {}).get("fps", 60.0 if video_format == "NTSC" else 50.0)
start_frame = details.get("window", {}).get("start_frame", 0)
end_frame = details.get("window", {}).get("end_frame", 0)

# Get content bounds (new detection)
content_bounds = details.get("content_bounds", {})
first_content_frame = content_bounds.get("first_content_frame", start_frame) if content_bounds else start_frame
last_content_frame = content_bounds.get("last_content_frame", end_frame) if content_bounds else end_frame

# Fallback to video_pop_starts if content_bounds not available (from frame_sequence_box)
video_pop_starts = details.get("video_pop_starts", [])
if first_content_frame == 0 and video_pop_starts:
    first_content_frame = video_pop_starts[0]

# Secondary fallback: use av_sync_details.video_pop_frame_indices
av_sync_for_bounds = data.get("av_sync_details", {})
if first_content_frame == 0:
    av_pop_frames = av_sync_for_bounds.get("video_pop_frame_indices", [])
    if av_pop_frames:
        # Use first pop frame as estimate of content start (may be ~1s after actual start)
        first_content_frame = max(0, av_pop_frames[0] - int(fps))  # ~1 second before first pop

# Get event lists from validation results
# skip_events: list of {frame, time_sec, skipped} - frame is where skip was detected
# repeated_events: list of {frame, time_sec, count} - frame is where repetition starts
skip_events = details.get("skip_events", [])
repeated_events = details.get("repeated_events", [])

# Build maps for events (keyed by video frame number)
repeated_intervals = {}
for evt in repeated_events:
    frame = evt.get("frame", 0)
    count = evt.get("count", 0)
    repeated_intervals[frame] = count

skip_map = {}
for evt in skip_events:
    frame = evt.get("frame", 0)
    skipped = evt.get("skipped", 0)
    skip_map[frame] = skipped

# Load obs.csv to get actual C64 stream frame_num values
# obs.csv records one entry per frame that ARRIVED from the network
# The video may show the same content for multiple frames (repeated)
obs_frame_nums = []  # List of frame_num values from obs.csv video events
if obs_csv_path and Path(obs_csv_path).exists():
    try:
        with open(obs_csv_path, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row.get('event_type') == 'video':
                    frame_num = int(row.get('frame_num', 0))
                    obs_frame_nums.append(frame_num)
    except Exception as e:
        print(f"Warning: Could not read obs.csv: {e}", file=sys.stderr)

# Get total frames from video if available
total_frames = end_frame + 1 if end_frame > 0 else 0
if recording_mp4 and Path(recording_mp4).exists():
    try:
        import cv2
        cap = cv2.VideoCapture(recording_mp4)
        if cap.isOpened():
            total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            fps = cap.get(cv2.CAP_PROP_FPS) or fps
        cap.release()
    except Exception:
        pass

if total_frames == 0:
    total_frames = max(end_frame + 1, 100)

# Fix last_content_frame when frame_sequence_box was skipped
if last_content_frame == 0 and total_frames > 0:
    last_content_frame = total_frames - 1

# Build a mapping from video frame index to the C64 stream frame being displayed.
# The frame_progression assertion detects slot positions from the bottom-left progress bar (frame_num % 8).
#
# The detected slot IS the ground truth for what content is being displayed.
# We use the detected slots to determine the actual frame_num being shown.
#
# frame_slots: dict mapping video_frame_index -> detected_slot_index (0-7)
# We need to map this back to actual frame_num values.
#
# The challenge: we only know frame_num % 8 from the slot.
# But obs.csv tells us the actual frame_num sequence that arrived.
# We need to find the obs.csv frame_num that matches the detected slot.

frame_slots = details.get("frame_slots", {})

# Build a lookup from slot (0-7) to list of obs frame_nums with that slot
slot_to_obs_frames = {i: [] for i in range(8)}
for fn in obs_frame_nums:
    slot = fn % 8
    slot_to_obs_frames[slot].append(fn)

# For each video frame, determine the frame_num being displayed
# Use detected slot and find the corresponding obs frame_num
video_to_frame_num = {}
last_frame_num = 0  # Track the last assigned frame_num for monotonicity

for video_idx in range(first_content_frame, min(last_content_frame + 1, total_frames)):
    video_idx_str = str(video_idx)

    if video_idx_str in frame_slots:
        detected_slot = frame_slots[video_idx_str]

        # Find the obs frame_num that matches this slot
        # It should be >= last_frame_num to maintain monotonicity (or same for repeats)
        candidates = slot_to_obs_frames.get(detected_slot, [])

        best_match = None
        for fn in candidates:
            if fn >= last_frame_num:
                best_match = fn
                break  # Take the first one >= last

        if best_match is None and candidates:
            # Allow same frame_num for repeats
            for fn in candidates:
                if fn == last_frame_num or fn == last_frame_num - 1:
                    best_match = fn
                    break

        if best_match is not None:
            video_to_frame_num[video_idx] = best_match
            last_frame_num = best_match
        else:
            # Fallback: derive from slot + offset based on last known
            base = (last_frame_num // 8) * 8
            derived = base + detected_slot
            if derived < last_frame_num:
                derived += 8  # Wrap to next cycle
            video_to_frame_num[video_idx] = derived
            last_frame_num = derived
    else:
        # No slot detection for this frame - use interpolation
        video_to_frame_num[video_idx] = last_frame_num

# Re-build repeat continuation frames based on the new mapping
# A repeat is when consecutive video frames have the same frame_num
repeat_continuation_frames_new = set()
prev_fn = None
for video_idx in sorted(video_to_frame_num.keys()):
    fn = video_to_frame_num[video_idx]
    if prev_fn is not None and fn == prev_fn:
        repeat_continuation_frames_new.add(video_idx)
    prev_fn = fn

# Generate playback CSV
# Each row represents one displayed frame in the recording (1:1 mapping).
#
# Columns:
# - playback_frame_index: 0-based index in the recording (video frame number)
# - frame_num: C64 stream frame number (the actual frame counter from the C64U device)
#              Empty for pre-roll/post-roll frames
# - video_s: timestamp in seconds since recording start (position in video file)
# - video_ssff: timestamp in SS:FF format (seconds:frames) for tools like Shotcut
# - content_s: time since C64U content started streaming (empty for logo/post-stream frames)
# - frame_slot: detected slot (0-7) from bottom-left progress bar (empty if not detected)
# - repeated: if this is the START of a repeated run, the total times shown (e.g., 2 = shown twice);
#             empty for normal frames or continuation frames within a run
# - skipped: number of source frames permanently lost BEFORE this frame arrived;
#            empty if no frames were skipped
# - event: human-readable summary: "repeated", "skipped", "repeated+skipped", or empty
# - video_pop: "video_pop" if a video pop (frame sync marker) was detected at this frame
# - audio_pop: "audio_pop" if an audio pop was detected within this frame's time window

# Get pop data from av_sync_details
av_sync_details = data.get("av_sync_details", {})
video_pop_frame_indices = set(av_sync_details.get("video_pop_frame_indices", []))

# Build audio pop frame mapping (audio pop time -> closest video frame)
audio_pop_frames = set()
sync_details = av_sync_details.get("sync_details", [])
for detail in sync_details:
    closest_frame = detail.get("closest_video_pop_frame")
    if closest_frame is not None:
        audio_pop_frames.add(closest_frame)

# Calculate first content time for content_s column
first_content_time_s = first_content_frame / fps if first_content_frame > 0 else 0.0

with open(playback_csv, 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['playback_frame_index', 'frame_num', 'frame_slot', 'video_s', 'video_ssff', 'content_s', 'repeated', 'skipped', 'event', 'video_pop', 'audio_pop'])

    for playback_idx in range(total_frames):
        video_s = round(playback_idx / fps, 3)
        # Calculate SS:FF format directly from frame index
        # For fractional fps (e.g., 59.94), use the actual fps value
        total_seconds = playback_idx / fps
        secs = int(total_seconds)
        fractional_seconds = total_seconds - secs
        frames = int(fractional_seconds * fps)
        video_ssff = f"{secs:02d}:{frames:02d}"

        # Determine if this is a pre-roll, content, or post-roll frame
        if playback_idx < first_content_frame:
            # Pre-roll frame (logo, startup)
            writer.writerow([playback_idx, "", "", video_s, video_ssff, "", "", "", "", "", ""])
        elif playback_idx > last_content_frame:
            # Post-roll frame (after content ends)
            writer.writerow([playback_idx, "", "", video_s, video_ssff, "", "", "", "", "", ""])
        else:
            # Content frame - get frame_num from our mapping
            frame_num = video_to_frame_num.get(playback_idx, "")

            # Calculate content_s (time since first content frame), never negative
            content_s = round(max(0.0, video_s - first_content_time_s), 3)

            # Get frame slot from frame_slots dict
            frame_slot = frame_slots.get(str(playback_idx), "")

            # Check for events at this video frame
            repeated_count = repeated_intervals.get(playback_idx, "")
            skipped_count = skip_map.get(playback_idx, "")

            # Build event string
            events = []
            if repeated_count:
                events.append("repeated")
            if skipped_count:
                events.append("skipped")
            event_str = "+".join(events)

            # Check for pops - use full string values for easy grepping
            video_pop = "video_pop" if playback_idx in video_pop_frame_indices else ""
            audio_pop = "audio_pop" if playback_idx in audio_pop_frames else ""

            writer.writerow([playback_idx, frame_num, frame_slot, video_s, video_ssff, content_s, repeated_count, skipped_count, event_str, video_pop, audio_pop])

# Validate frame_num against detected slots from video
# The bottom-left progress bar shows frame_num % 8 as a slot position (0-7)
# frame_slots is a dict: video_frame_num -> detected_slot_index

mismatch_count = 0
mismatch_examples = []
validated_count = 0

for video_idx, expected_frame_num in video_to_frame_num.items():
    video_idx_str = str(video_idx)
    if video_idx_str in frame_slots and expected_frame_num:
        detected_slot = frame_slots[video_idx_str]
        expected_slot = expected_frame_num % 8
        validated_count += 1
        if detected_slot != expected_slot:
            mismatch_count += 1
            if len(mismatch_examples) < 5:
                mismatch_examples.append({
                    "video_frame": video_idx,
                    "frame_num": expected_frame_num,
                    "expected_slot": expected_slot,
                    "detected_slot": detected_slot,
                })

if validated_count > 0:
    mismatch_pct = 100.0 * mismatch_count / validated_count
    if mismatch_count > 0:
        print(f"⚠️  Frame validation: {mismatch_count}/{validated_count} mismatches ({mismatch_pct:.1f}%)", file=sys.stderr)
        for ex in mismatch_examples:
            print(f"   - Video frame {ex['video_frame']}: frame_num={ex['frame_num']}, expected slot {ex['expected_slot']}, detected {ex['detected_slot']}", file=sys.stderr)
    else:
        print(f"✅ Frame validation: all {validated_count} frames match expected slots", file=sys.stderr)
else:
    print("⚠️  Frame validation: no slot data available for validation", file=sys.stderr)

print(f"Generated playback.csv with {total_frames} frames ({len(obs_frame_nums)} obs events, {len(video_to_frame_num)} content frames)", file=sys.stderr)
PYTHON

    if [[ -f "${playback_csv}" ]]; then
        log_success "Generated playback.csv"
    else
        log_warning "Failed to generate playback.csv"
    fi
}

# Generate test report
generate_report() {
    log_info "Generating test report..."

    local report_file="${OUTPUT_DIR}/README.md"
    local timestamp=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
    local video_duration_sec=""
    local video_duration_fmt=""
    local sample_frame_index=""
    local sample_frame_seconds=""
    local sample_frame_timestamp=""

    # Gather system snapshot for debugging context
    local obs_version os_name kernel_version cpu_model cpu_cores ram_total ram_available
    local disk_total disk_available disk_mount

    if command -v obs >/dev/null 2>&1; then
        obs_version=$(obs --version 2>/dev/null | head -n1 | sed 's/^OBS Studio - //' | sed 's/^OBS Studio //')
        [[ -z "${obs_version}" ]] && obs_version="Detected (version unknown)"
    else
        obs_version="Not installed"
    fi

    kernel_version=$(uname -r 2>/dev/null || echo "Unknown kernel")
    if command -v lsb_release >/dev/null 2>&1; then
        os_name=$(lsb_release -ds 2>/dev/null | tr -d '"')
    elif [[ -f /etc/os-release ]]; then
        os_name=$(grep -E '^PRETTY_NAME=' /etc/os-release | cut -d= -f2- | tr -d '"')
    fi
    [[ -z "${os_name}" ]] && os_name=$(uname -s 2>/dev/null || echo "Unknown OS")

    cpu_model=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//')
    [[ -z "${cpu_model}" ]] && cpu_model=$(uname -p 2>/dev/null || echo "Unknown CPU")
    cpu_cores=$(nproc 2>/dev/null || echo "?")

    if command -v free >/dev/null 2>&1; then
        ram_total=$(free -h | awk '/^Mem:/ {print $2}')
        ram_available=$(free -h | awk '/^Mem:/ {print $7}')
    else
        ram_total=$(grep -m1 'MemTotal' /proc/meminfo 2>/dev/null | awk '{printf "%.1f MiB", $2/1024}')
        ram_available=$(grep -m1 'MemAvailable' /proc/meminfo 2>/dev/null | awk '{printf "%.1f MiB", $2/1024}')
    fi
    [[ -z "${ram_total}" ]] && ram_total="Unknown"
    [[ -z "${ram_available}" ]] && ram_available="Unknown"

    if command -v df >/dev/null 2>&1; then
        read -r disk_total disk_available disk_mount <<<"$(df -h "${PROJECT_ROOT}" 2>/dev/null | awk 'NR==2 {print $2, $4, $6}')"
    fi
    [[ -z "${disk_total}" ]] && disk_total="Unknown"
    [[ -z "${disk_available}" ]] && disk_available="Unknown"
    [[ -z "${disk_mount}" ]] && disk_mount=$(pwd)

        # Resolve plugin version via shared helper (mirrors CI logic)
        local resolved_version
        if [[ -x "${PROJECT_ROOT}/build-aux/resolve-plugin-version.sh" ]]; then
            resolved_version=$("${PROJECT_ROOT}/build-aux/resolve-plugin-version.sh")
        else
            resolved_version=$(jq -r '.version // "unknown"' "${PROJECT_ROOT}/buildspec.json" 2>/dev/null)
        fi

        cat > "${report_file}" << EOF
# C64 Stream E2E Test Report

## Scenario: ${SCENARIO_NAME:-${SCENARIO:-Unknown}}

Generated: ${timestamp}

## Test configuration

- Format: ${FORMAT}
- Frames: ${FRAMES}
- Duration: $(if [[ "${FORMAT}" == "PAL" ]]; then awk "BEGIN {printf \"%.1f\", ${FRAMES}/50}"; else awk "BEGIN {printf \"%.1f\", ${FRAMES}/60}"; fi) seconds
- Video Port: ${VIDEO_PORT}
- Audio Port: ${AUDIO_PORT}
- OBS Enabled: ${OBS_ENABLED}

## Build information

- Project: $(jq -r '.name // "unknown"' "${PROJECT_ROOT}/buildspec.json" 2>/dev/null)
- Version: ${resolved_version}

## System information

- OS: ${os_name} (kernel ${kernel_version})
- OBS: ${obs_version}
- CPU: ${cpu_model} (${cpu_cores} cores)
- RAM: ${ram_total} total, ${ram_available} available
- Disk (${disk_mount}): ${disk_total} total, ${disk_available} available

## Test results
EOF

    # Validation summary (from validation_results.json)
    local validation_summary_file="${OUTPUT_DIR}/validation_results.json"
    if [[ -f "${validation_summary_file}" ]] && command -v jq >/dev/null 2>&1; then
        echo >> "${report_file}"
        echo "### Validation Summary" >> "${report_file}"
        echo >> "${report_file}"

        _render_validation_line() {
            local key="$1"
            local label="$2"
            local status details emoji
            status=$(jq -r ".${key}.status // \"unknown\"" "${validation_summary_file}" 2>/dev/null || echo "unknown")
            details=$(jq -r ".${key}.details // \"\"" "${validation_summary_file}" 2>/dev/null || echo "")

            case "${status}" in
                pass) emoji="✅" ;;
                warning) emoji="⚠️" ;;
                fail) emoji="❌" ;;
                deferred) emoji="⚠️" ;;
                skipped) emoji="⏭️" ;;
                *) emoji="❓" ;;
            esac

            if [[ -n "${details}" && "${details}" != "null" ]]; then
                echo "- ${emoji} ${label}: ${details}" >> "${report_file}"
            else
                echo "- ${emoji} ${label}: Status ${status}" >> "${report_file}"
            fi
        }

        _render_validation_line "udp_reception" "UDP Packet Reception"
        _render_validation_line "network_timing" "Network Timing"
        _render_validation_line "frame_processing" "Frame Processing"
        _render_validation_line "video_recording" "Video Recording"
        _render_validation_line "packet_integrity" "Content Integrity"
    fi

    # Add Resource Usage section first if resource.json exists
    local resource_json="${OUTPUT_DIR}/resource.json"
    if [[ -f "${resource_json}" ]] && command -v jq >/dev/null 2>&1; then
        local duration_ms sample_count total_sample_count
        local cpu_min cpu_median cpu_mean cpu_max ram_mb_min ram_mb_median ram_mb_mean ram_mb_max
        local gpu_min gpu_median gpu_mean gpu_max
        local effective_cpus physical_cpus
        duration_ms=$(jq -r '.duration_ms // 0' "${resource_json}")
        sample_count=$(jq -r '.sample_count // 0' "${resource_json}")
        total_sample_count=$(jq -r '.total_sample_count // 0' "${resource_json}")
        cpu_min=$(jq -r '.cpu_percent.min // 0' "${resource_json}")
        cpu_median=$(jq -r '.cpu_percent.median // 0' "${resource_json}")
        cpu_mean=$(jq -r '.cpu_percent.mean // 0' "${resource_json}")
        cpu_max=$(jq -r '.cpu_percent.max // 0' "${resource_json}")
        ram_mb_min=$(jq -r '.ram_mb.min // 0' "${resource_json}")
        ram_mb_median=$(jq -r '.ram_mb.median // 0' "${resource_json}")
        ram_mb_mean=$(jq -r '.ram_mb.mean // 0' "${resource_json}")
        ram_mb_max=$(jq -r '.ram_mb.max // 0' "${resource_json}")
        gpu_min=$(jq -r '.gpu_percent.min // null' "${resource_json}")
        gpu_median=$(jq -r '.gpu_percent.median // null' "${resource_json}")
        gpu_mean=$(jq -r '.gpu_percent.mean // null' "${resource_json}")
        gpu_max=$(jq -r '.gpu_percent.max // null' "${resource_json}")
        effective_cpus=$(jq -r '.allocated_cpu_cores // 0' "${resource_json}")
        physical_cpus=$(jq -r '.total_cpu_cores // 0' "${resource_json}")

        if [[ "${sample_count}" -gt 0 ]]; then
            local duration_sec cpu_context
            duration_sec=$(awk -v ms="${duration_ms}" 'BEGIN{printf "%.1f", ms/1000}')

            # Build CPU context string
            if awk -v eff="${effective_cpus}" -v phys="${physical_cpus}" 'BEGIN{exit !(eff < phys && eff > 0)}'; then
                cpu_context=" (cgroup-limited: ${effective_cpus} of ${physical_cpus} cores)"
            elif [[ "${physical_cpus}" -gt 0 ]]; then
                cpu_context=" (${physical_cpus} cores)"
            else
                cpu_context=""
            fi

            echo >> "${report_file}"
            echo "### Resource Usage" >> "${report_file}"
            echo >> "${report_file}"
            local samples_text
            if [[ "${total_sample_count}" -gt 0 && "${total_sample_count}" -ne "${sample_count}" ]]; then
                samples_text="${sample_count} of ${total_sample_count} samples"
            else
                samples_text="${sample_count} samples"
            fi
            echo "During the test's processing window (${duration_sec}s, ${samples_text})${cpu_context}:" >> "${report_file}"
            echo >> "${report_file}"
            echo "| Metric | Min | Median | Mean | Max |" >> "${report_file}"
            echo "|--------|-----|--------|------|-----|" >> "${report_file}"
            echo "| CPU | ${cpu_min}% | ${cpu_median}% | ${cpu_mean}% | ${cpu_max}% |" >> "${report_file}"
            echo "| RAM | ${ram_mb_min} MB | ${ram_mb_median} MB | ${ram_mb_mean} MB | ${ram_mb_max} MB |" >> "${report_file}"
            if [[ "${gpu_median}" != "null" && -n "${gpu_median}" ]]; then
                echo "| GPU | ${gpu_min}% | ${gpu_median}% | ${gpu_mean}% | ${gpu_max}% |" >> "${report_file}"
            fi
            echo >> "${report_file}"
            echo "Details: [resource.csv](resource.csv) | [resource.json](resource.json)" >> "${report_file}"
        fi
    fi

    # Packet & Network Data section
    echo >> "${report_file}"
    echo "### Packet & Network Data" >> "${report_file}"
    echo >> "${report_file}"

    local video_count=0
    local audio_count=0
    if [[ -d "${TEST_DIR}/test_packets" ]]; then
        video_count=$(find "${TEST_DIR}/test_packets/video/${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)
        audio_count=$(find "${TEST_DIR}/test_packets/audio/${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)
        echo "- ✅ Packet Generation: ${video_count} video, ${audio_count} audio packets" >> "${report_file}"
    else
        echo "- ⚠️ Packet Generation: Not captured" >> "${report_file}"
    fi
    echo "- ✅ UDP Replay: Completed successfully" >> "${report_file}"

    local event_links=()
    if [[ -f "${OUTPUT_DIR}/network.csv" ]]; then
        event_links+=("[network.csv](network.csv)")
    fi
    if [[ -f "${OUTPUT_DIR}/obs.csv" ]]; then
        event_links+=("[obs.csv](obs.csv)")
    fi
    if [[ -f "${OUTPUT_DIR}/playback.csv" ]]; then
        event_links+=("[playback.csv](playback.csv)")
    fi
    if (( ${#event_links[@]} > 0 )); then
        echo "- Events: $(join_by ', ' "${event_links[@]}")" >> "${report_file}"
    fi

    # Network Simulation section (if configured)
    if [[ -n "${SCENARIO_YAML_PATH:-}" && -f "${SCENARIO_YAML_PATH}" ]]; then
        local max_jitter_ms reorder_percent buffer_delay_ms
        max_jitter_ms=$(grep -m1 "max_jitter_ms:" "${SCENARIO_YAML_PATH}" 2>/dev/null | sed 's/.*: *//' || echo "0")
        reorder_percent=$(grep -m1 "reorder_percent:" "${SCENARIO_YAML_PATH}" 2>/dev/null | sed 's/.*: *//' || echo "0")
        buffer_delay_ms=$(grep -m1 "buffer_delay_ms:" "${SCENARIO_YAML_PATH}" 2>/dev/null | sed 's/.*: *//' || echo "0")

        if [[ "${max_jitter_ms:-0}" != "0" || "${reorder_percent:-0}" != "0" || "${buffer_delay_ms:-0}" != "0" ]]; then
            echo >> "${report_file}"
            echo "#### Network Simulation (Configured)" >> "${report_file}"
            echo >> "${report_file}"
            echo "| Parameter | Value |" >> "${report_file}"
            echo "|-----------|-------|" >> "${report_file}"
            [[ "${max_jitter_ms:-0}" != "0" ]] && echo "| Max Jitter | ${max_jitter_ms}ms |" >> "${report_file}"
            [[ "${reorder_percent:-0}" != "0" ]] && echo "| Reorder Rate | ${reorder_percent}% |" >> "${report_file}"
            [[ "${buffer_delay_ms:-0}" != "0" ]] && echo "| Buffer Delay | ${buffer_delay_ms}ms |" >> "${report_file}"
            echo >> "${report_file}"
            echo "_Note: Jitter is applied as random positive delay (0-max_jitter_ms) to each packet, causing sequence reordering. The 'Measured Jitter' below shows inter-packet interval variance at reception, which is typically low since packets are sent in time order after delay application._" >> "${report_file}"
        fi
    fi

    # Network Quality section (from network.json analysis)
    if [[ -f "${OUTPUT_DIR}/network.json" ]]; then
        echo >> "${report_file}"
        echo "#### Network Quality (Measured)" >> "${report_file}"
        echo >> "${report_file}"

        # Extract jitter statistics from network.json using Python
        if command -v python3 >/dev/null 2>&1; then
            python3 - "${OUTPUT_DIR}/network.json" >> "${report_file}" 2>/dev/null <<'PY' || true
import json
import sys

with open(sys.argv[1], 'r') as f:
    data = json.load(f)

summary = data.get('summary', {})
all_stats = data.get('all', {})
video = data.get('video', {})
audio = data.get('audio', {})

duration_ms = summary.get('duration_ms', None)
total_packets = summary.get('total_packets', all_stats.get('count', 0))

if duration_ms is not None:
    print(f"- Packet span (first→last): {duration_ms:.3f} ms")
if total_packets:
    print(f"- Total packets analyzed: {total_packets}")

def fmt_ms(us):
    if us is None:
        return "-"
    return f"{(us / 1000.0):.3f} ms"

def fmt_pct(v):
    if v is None:
        return "-"
    return f"{v:.2f}%"

def spacing_row(name, stats):
    if not stats:
        return None
    return (
        name,
        stats.get('count', 0),
        fmt_ms(stats.get('spacing_min_us')),
        fmt_ms(stats.get('spacing_mean_us')),
        fmt_ms(stats.get('spacing_max_us')),
        fmt_pct(stats.get('spacing_cv_pct')),
        fmt_pct(stats.get('burst_short_pct')),
        fmt_pct(stats.get('burst_long_pct')),
        stats.get('burst_p99_p50', None),
    )

print()
print("| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |")
print("|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|")

rows = [
    spacing_row('All', all_stats),
    spacing_row('Video', video),
    spacing_row('Audio', audio),
]

for r in rows:
    if not r:
        continue
    name, count, min_ms, mean_ms, max_ms, cv, burst_short, burst_long, p99_p50 = r
    p99_p50_str = f"{p99_p50:.3f}" if isinstance(p99_p50, (int, float)) else "-"
    print(f"| {name} | {count} | {min_ms} | {mean_ms} | {max_ms} | {cv} | {burst_short} | {burst_long} | {p99_p50_str} |")

# Build table
print()
print("| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |")
print("|--------|---------|-----------------|--------------|--------------|")

if video:
    v_count = video.get('count', 0)
    v_jitter_median = video.get('jitter_median_ms', 0)
    v_jitter_max = video.get('jitter_max_ms', 0)
    v_ooo_count = video.get('out_of_order_count', 0)
    v_ooo_rate = video.get('out_of_order_rate_pct', 0)
    ooo_str = f"{v_ooo_count} ({v_ooo_rate:.1f}%)" if v_ooo_count > 0 else "0"
    print(f"| Video | {v_count} | {v_jitter_median:.3f} ms | {v_jitter_max:.3f} ms | {ooo_str} |")

if audio:
    a_count = audio.get('count', 0)
    a_jitter_median = audio.get('jitter_median_ms', 0)
    a_jitter_max = audio.get('jitter_max_ms', 0)
    a_ooo_count = audio.get('out_of_order_count', 0)
    a_ooo_rate = audio.get('out_of_order_rate_pct', 0)
    ooo_str = f"{a_ooo_count} ({a_ooo_rate:.1f}%)" if a_ooo_count > 0 else "0"
    print(f"| Audio | {a_count} | {a_jitter_median:.3f} ms | {a_jitter_max:.3f} ms | {ooo_str} |")
PY
        fi
        echo >> "${report_file}"
        echo "Details: [network.json](network.json)" >> "${report_file}"
    fi

    # Perf profiling summary (if available)
    if [[ -f "${OUTPUT_DIR}/perf_report.txt" || -f "${OUTPUT_DIR}/perf_error.txt" || -f "${OUTPUT_DIR}/flamegraph.svg" ]]; then
        echo >> "${report_file}"
        echo "### Perf Hotspots" >> "${report_file}"
        echo >> "${report_file}"

        local perf_links=()
        if [[ -f "${OUTPUT_DIR}/perf_report.txt" ]]; then
            perf_links+=("[perf_report.txt](perf_report.txt)")
        fi
        if [[ -f "${OUTPUT_DIR}/flamegraph.svg" ]]; then
            perf_links+=("[flamegraph.svg](flamegraph.svg)")
        fi
        if [[ -f "${OUTPUT_DIR}/perf_error.txt" ]]; then
            perf_links+=("[perf_error.txt](perf_error.txt)")
        fi
        if (( ${#perf_links[@]} > 0 )); then
            echo "- Artifacts: $(join_by ' | ' "${perf_links[@]}")" >> "${report_file}"
        fi

        if [[ -f "${OUTPUT_DIR}/perf_report.txt" ]]; then
            echo >> "${report_file}"
            echo "Top functions/symbols by sampled CPU (perf report):" >> "${report_file}"
            echo >> "${report_file}"
            echo "| Overhead | Shared Object | Symbol |" >> "${report_file}"
            echo "|----------|---------------|--------|" >> "${report_file}"
            if command -v python3 >/dev/null 2>&1; then
                python3 - "${OUTPUT_DIR}/perf_report.txt" >> "${report_file}" 2>/dev/null <<'PY' || true
import re
import sys

path = sys.argv[1]
count = 0

with open(path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        if count >= 10:
            break
        if not re.match(r'^\s*\d+(?:\.\d+)?%\s', line):
            continue
        # perf report is column-aligned; columns are separated by 2+ spaces.
        parts = re.split(r'\s{2,}', line.strip())
        if len(parts) < 4:
            continue
        overhead, _command, dso, symbol = parts[:4]
        # Escape markdown pipes.
        dso = dso.replace('|', '\\|')
        symbol = symbol.replace('|', '\\|')
        print(f'| {overhead} | {dso} | {symbol} |')
        count += 1
PY
            else
                # Fallback: best-effort awk parse (may be less accurate with spaced commands)
                awk '
                    BEGIN{count=0}
                    /^[[:space:]]*[0-9]+(\.[0-9]+)?%/ {
                        overhead=$1
                        dso=$3
                        sym=$4" "$5
                        if (sym == "") next
                        gsub(/\|/, "\\|", dso)
                        gsub(/\|/, "\\|", sym)
                        print "| " overhead " | " dso " | " sym " |"
                        count++
                        if (count>=10) exit
                    }
                ' "${OUTPUT_DIR}/perf_report.txt" >> "${report_file}" || true
            fi
        fi
    fi

    local recording_found=""

    # Add OBS results if enabled
    if [[ "${OBS_ENABLED}" == true ]]; then
        if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" ]]; then
            recording_found="${OUTPUT_DIR}/c64_recording.mp4"
        elif compgen -G "${OUTPUT_DIR}/*.mp4" > /dev/null; then
            recording_found=$(ls -t ${OUTPUT_DIR}/*.mp4 2>/dev/null | head -1)
        fi
    else
        echo "- ⚠️ OBS Integration: Disabled (use --obs to enable)" >> "${report_file}"
    fi

    # Video section is rendered after A/V Sync (just before Sample Frame)

    # Add Pop synchronization section if validation results with details are available
    local validation_file="${OUTPUT_DIR}/validation_results.json"
    if [[ -f "${validation_file}" ]] && command -v jq >/dev/null 2>&1; then
        # Extract the av_sync_details block if present
        local has_details
        has_details=$(jq -r 'has("av_sync_details")' "${validation_file}" 2>/dev/null || echo "false")
        if [[ "${has_details}" == "true" ]]; then
            echo >> "${report_file}"
            echo "### A/V Sync" >> "${report_file}"
            echo >> "${report_file}"

            # Overall verdict line similar to console output
            local sync_accuracy is_perfect avg_diff max_diff avg_diff_fmt max_diff_fmt sync_accuracy_fmt
            sync_accuracy=$(jq -r '.av_sync_details.sync_accuracy_percent // 0' "${validation_file}")
            is_perfect=$(jq -r '.av_sync_details.is_perfectly_synced // false' "${validation_file}")
            # Compute avg/max over diffs when available
            avg_diff=$(jq -r '[.av_sync_details.sync_details[] | select(has("closest_video_pop_ms")) | .difference_ms] | if length>0 then (add/length) else 0 end' "${validation_file}" 2>/dev/null || echo "0")
            max_diff=$(jq -r '[.av_sync_details.sync_details[] | select(has("closest_video_pop_ms")) | .difference_ms] | if length>0 then max else 0 end' "${validation_file}" 2>/dev/null || echo "0")
            sync_accuracy_fmt=$(format_to_one_decimal "${sync_accuracy}")
            [[ -z "${sync_accuracy_fmt}" ]] && sync_accuracy_fmt="${sync_accuracy}"
            avg_diff_fmt=$(format_to_one_decimal "${avg_diff}")
            max_diff_fmt=$(format_to_one_decimal "${max_diff}")

            if [[ "${is_perfect}" == "true" ]]; then
                echo "- ✅ Good synchronization (${sync_accuracy_fmt}%): avg offset ${avg_diff_fmt}ms, max ${max_diff_fmt}ms" >> "${report_file}"
            elif awk -v acc="${sync_accuracy}" 'BEGIN{exit !(acc>=60)}'; then
                echo "- ✅ Acceptable synchronization (${sync_accuracy_fmt}%): avg offset ${avg_diff_fmt}ms, max ${max_diff_fmt}ms" >> "${report_file}"
            else
                echo "- ❌ Poor synchronization (${sync_accuracy_fmt}%): avg offset ${avg_diff_fmt}ms, max ${max_diff_fmt}ms" >> "${report_file}"
            fi

            # (Omit raw pop/time index lists to avoid duplication)

            # Per-pop lines with traffic light and channel
            echo >> "${report_file}"
            echo "#### Sync Details" >> "${report_file}"
            echo >> "${report_file}"
            # Build arrays
            local count
            count=$(jq -r '.av_sync_details.sync_details | length' "${validation_file}")
            if [[ "${count}" =~ ^[0-9]+$ ]] && [[ ${count} -gt 0 ]]; then
                for ((i=0; i<count; i++)); do
                    local status emoji chan a_ms v_ms diff_ms
                    status=$(jq -r ".av_sync_details.sync_details[$i].traffic // \"\"" "${validation_file}")
                    case "${status}" in
                        green) emoji="🟢" ;;
                        yellow) emoji="🟡" ;;
                        red) emoji="🔴" ;;
                        *) emoji="•" ;;
                    esac
                    chan=$(jq -r ".av_sync_details.sync_details[$i].audio_channel // .av_sync_details.sync_details[$i].channel // \"?\"" "${validation_file}")
                    a_ms=$(jq -r ".av_sync_details.sync_details[$i].audio_pop_time_ms // \"\"" "${validation_file}")
                    v_ms=$(jq -r ".av_sync_details.sync_details[$i].closest_video_pop_ms // empty" "${validation_file}")
                    v_fr=$(jq -r ".av_sync_details.sync_details[$i].closest_video_pop_frame // empty" "${validation_file}")
                    diff_ms=$(jq -r ".av_sync_details.sync_details[$i].difference_ms // empty" "${validation_file}")
                    # Format to 0.1 ms (tenth of a millisecond)
                    a_ms_fmt=$(printf '%.1f' "${a_ms:-0}")
                    if [[ -n "${v_ms}" ]] && [[ -n "${diff_ms}" ]]; then
                        if [[ -n "${v_fr}" && "${v_fr}" != "null" ]]; then
                            echo "- ${emoji} Pop #$((i+1)) [${chan}]: audio=${a_ms_fmt}ms, video=$(printf '%.1f' "${v_ms}")ms (frame ${v_fr}), diff=$(printf '%.1f' "${diff_ms}")ms" >> "${report_file}"
                        else
                            echo "- ${emoji} Pop #$((i+1)) [${chan}]: audio=${a_ms_fmt}ms, video=$(printf '%.1f' "${v_ms}")ms, diff=$(printf '%.1f' "${diff_ms}")ms" >> "${report_file}"
                        fi
                    else
                        echo "- ${emoji} Pop #$((i+1)) [${chan}]: audio=${a_ms_fmt}ms, no matching video pop found" >> "${report_file}"
                    fi
                done
            fi

            # Traffic lights summary and channels
            local traffic marks channels
            traffic=$(jq -r '.av_sync_details.traffic // [] | map(.)' "${validation_file}")
            # Compose marks string via jq
            marks=$(jq -r '[.av_sync_details.traffic[]? | if .=="green" then "🟢" elif .=="yellow" then "🟡" elif .=="red" then "🔴" else "•" end] | join("")' "${validation_file}")
            channels=$(jq -r '[.av_sync_details.sync_details[]? | .audio_channel // .channel | if .=="L" then "L" elif .=="R" then "R" else "B" end] | join("")' "${validation_file}")
            echo >> "${report_file}"
            echo "- Channels: ${channels}" >> "${report_file}"

            # Alternation check (ignore B), report verdict
            local seq_str alternates
            seq_str=$(jq -r '[.av_sync_details.sync_details[]? | select((.audio_channel // .channel)=="L" or (.audio_channel // .channel)=="R") | (.audio_channel // .channel)] | join(" ")' "${validation_file}")
            # Convert space-separated string to bash array
            IFS=' ' read -r -a seq <<< "${seq_str}"
            alternates=true
            if [[ ${#seq[@]} -ge 2 ]]; then
                for ((i=1; i<${#seq[@]}; i++)); do
                    if [[ "${seq[$i]}" == "${seq[$((i-1))]}" ]]; then
                        alternates=false
                        break
                    fi
                done
            fi
            if [[ "${alternates}" == true && ${#seq[@]} -ge 1 ]]; then
                echo "- 🔁 Channel alternation: OK (alternating, starts with ${seq[0]})" >> "${report_file}"
            else
                echo "- 🔁 Channel alternation: MISMATCH" >> "${report_file}"
            fi
        fi

        # Frame Progression section (Frame Box Sequence check)
        local fsb_status fsb_message
        fsb_status=$(jq -r '.frame_sequence_box.status // "skipped"' "${validation_file}" 2>/dev/null || echo "skipped")
        fsb_message=$(jq -r '.frame_sequence_box.message // ""' "${validation_file}" 2>/dev/null || echo "")

        if [[ "${fsb_status}" != "skipped" ]]; then
            echo >> "${report_file}"
            echo "### Frame Progression" >> "${report_file}"
            echo >> "${report_file}"

            # Get frame metrics
            local analyzed valid distinct settling_seconds
            analyzed=$(jq -r '.frame_sequence_box.details.analyzed_frames // 0' "${validation_file}" 2>/dev/null || echo "0")
            valid=$(jq -r '.frame_sequence_box.metrics.valid_frames // 0' "${validation_file}" 2>/dev/null || echo "0")
            distinct=$(jq -r '.frame_sequence_box.metrics.distinct_colors // 0' "${validation_file}" 2>/dev/null || echo "0")
            settling_seconds=$(jq -r '.frame_sequence_box.metrics.settling_seconds // .frame_sequence_box.details.settling_seconds // 0' "${validation_file}" 2>/dev/null || echo "0")

            # Status line with traffic light
            case "${fsb_status}" in
                pass)
                    echo "- 🟢 Frame sequence verified (${valid%.*} frames analyzed, ${distinct%.*} colors)" >> "${report_file}"
                    ;;
                warning)
                    echo "- 🟡 ${fsb_message}" >> "${report_file}"
                    ;;
                fail)
                    echo "- 🔴 ${fsb_message}" >> "${report_file}"
                    ;;
            esac

            # Settling split (pre/post)
            local pre_stuck_count pre_stuck_min pre_stuck_median pre_stuck_max
            local pre_skip_count pre_skip_min pre_skip_median pre_skip_max
            local pre_back_steps pre_severe_steps
            local post_stuck_count post_stuck_min post_stuck_median post_stuck_max
            local post_skip_count post_skip_min post_skip_median post_skip_max
            local post_back_steps post_severe_steps

            pre_stuck_count=$(jq -r '.frame_sequence_box.metrics.pre_settling_stuck_run_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_stuck_min=$(jq -r '.frame_sequence_box.metrics.pre_settling_stuck_run_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_stuck_median=$(jq -r '.frame_sequence_box.metrics.pre_settling_stuck_run_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_stuck_max=$(jq -r '.frame_sequence_box.metrics.pre_settling_max_stuck_run // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_count=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_min=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_median=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_max=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_max // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_back_steps=$(jq -r '.frame_sequence_box.metrics.pre_settling_back_steps // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_severe_steps=$(jq -r '.frame_sequence_box.metrics.pre_settling_severe_steps // 0' "${validation_file}" 2>/dev/null || echo "0")

            post_stuck_count=$(jq -r '.frame_sequence_box.metrics.post_settling_stuck_run_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_stuck_min=$(jq -r '.frame_sequence_box.metrics.post_settling_stuck_run_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_stuck_median=$(jq -r '.frame_sequence_box.metrics.post_settling_stuck_run_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_stuck_max=$(jq -r '.frame_sequence_box.metrics.post_settling_max_stuck_run // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_count=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_min=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_median=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_max=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_max // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_back_steps=$(jq -r '.frame_sequence_box.metrics.post_settling_back_steps // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_severe_steps=$(jq -r '.frame_sequence_box.metrics.post_settling_severe_steps // 0' "${validation_file}" 2>/dev/null || echo "0")

            echo >> "${report_file}"
            echo "- Settling: ${settling_seconds}s (pass/fail uses post-settling only)" >> "${report_file}"

            # Show table if there are any issues in either window
            local has_pre=false has_post=false
            if [[ "${pre_stuck_count}" != "0" && "${pre_stuck_count}" != "null" ]] || \
               [[ "${pre_skip_count}" != "0" && "${pre_skip_count}" != "null" ]] || \
               [[ "${pre_back_steps}" != "0" && "${pre_back_steps}" != "null" ]] || \
               [[ "${pre_severe_steps}" != "0" && "${pre_severe_steps}" != "null" ]]; then
                has_pre=true
            fi
            if [[ "${post_stuck_count}" != "0" && "${post_stuck_count}" != "null" ]] || \
               [[ "${post_skip_count}" != "0" && "${post_skip_count}" != "null" ]] || \
               [[ "${post_back_steps}" != "0" && "${post_back_steps}" != "null" ]] || \
               [[ "${post_severe_steps}" != "0" && "${post_severe_steps}" != "null" ]]; then
                has_post=true
            fi

            if [[ "${has_pre}" == "true" || "${has_post}" == "true" ]]; then
                echo >> "${report_file}"
                echo "| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |" >> "${report_file}"
                echo "|--------|------------------------------:|--------------------------:|-----------:|-------------:|" >> "${report_file}"

                local pre_stuck_count_i pre_stuck_min_i pre_stuck_median_i pre_stuck_max_i
                local pre_skip_count_i pre_skip_min_i pre_skip_median_i pre_skip_max_i
                local pre_back_steps_i pre_severe_steps_i
                pre_stuck_count_i=$(printf '%.0f' "${pre_stuck_count}" 2>/dev/null || echo "${pre_stuck_count}")
                pre_stuck_min_i=$(printf '%.0f' "${pre_stuck_min}" 2>/dev/null || echo "${pre_stuck_min}")
                pre_stuck_median_i=$(printf '%.0f' "${pre_stuck_median}" 2>/dev/null || echo "${pre_stuck_median}")
                pre_stuck_max_i=$(printf '%.0f' "${pre_stuck_max}" 2>/dev/null || echo "${pre_stuck_max}")
                pre_skip_count_i=$(printf '%.0f' "${pre_skip_count}" 2>/dev/null || echo "${pre_skip_count}")
                pre_skip_min_i=$(printf '%.0f' "${pre_skip_min}" 2>/dev/null || echo "${pre_skip_min}")
                pre_skip_median_i=$(printf '%.0f' "${pre_skip_median}" 2>/dev/null || echo "${pre_skip_median}")
                pre_skip_max_i=$(printf '%.0f' "${pre_skip_max}" 2>/dev/null || echo "${pre_skip_max}")
                pre_back_steps_i=$(printf '%.0f' "${pre_back_steps}" 2>/dev/null || echo "${pre_back_steps}")
                pre_severe_steps_i=$(printf '%.0f' "${pre_severe_steps}" 2>/dev/null || echo "${pre_severe_steps}")

                local post_stuck_count_i post_stuck_min_i post_stuck_median_i post_stuck_max_i
                local post_skip_count_i post_skip_min_i post_skip_median_i post_skip_max_i
                local post_back_steps_i post_severe_steps_i
                post_stuck_count_i=$(printf '%.0f' "${post_stuck_count}" 2>/dev/null || echo "${post_stuck_count}")
                post_stuck_min_i=$(printf '%.0f' "${post_stuck_min}" 2>/dev/null || echo "${post_stuck_min}")
                post_stuck_median_i=$(printf '%.0f' "${post_stuck_median}" 2>/dev/null || echo "${post_stuck_median}")
                post_stuck_max_i=$(printf '%.0f' "${post_stuck_max}" 2>/dev/null || echo "${post_stuck_max}")
                post_skip_count_i=$(printf '%.0f' "${post_skip_count}" 2>/dev/null || echo "${post_skip_count}")
                post_skip_min_i=$(printf '%.0f' "${post_skip_min}" 2>/dev/null || echo "${post_skip_min}")
                post_skip_median_i=$(printf '%.0f' "${post_skip_median}" 2>/dev/null || echo "${post_skip_median}")
                post_skip_max_i=$(printf '%.0f' "${post_skip_max}" 2>/dev/null || echo "${post_skip_max}")
                post_back_steps_i=$(printf '%.0f' "${post_back_steps}" 2>/dev/null || echo "${post_back_steps}")
                post_severe_steps_i=$(printf '%.0f' "${post_severe_steps}" 2>/dev/null || echo "${post_severe_steps}")

                echo "| During settling | ${pre_stuck_count_i}/${pre_stuck_min_i}/${pre_stuck_median_i}/${pre_stuck_max_i} | ${pre_skip_count_i}/${pre_skip_min_i}/${pre_skip_median_i}/${pre_skip_max_i} | ${pre_back_steps_i} | ${pre_severe_steps_i} |" >> "${report_file}"
                echo "| After settling | ${post_stuck_count_i}/${post_stuck_min_i}/${post_stuck_median_i}/${post_stuck_max_i} | ${post_skip_count_i}/${post_skip_min_i}/${post_skip_median_i}/${post_skip_max_i} | ${post_back_steps_i} | ${post_severe_steps_i} |" >> "${report_file}"
            fi

            # Reference playback.csv for detailed frame-by-frame analysis
            if [[ -f "${OUTPUT_DIR}/playback.csv" ]]; then
                echo >> "${report_file}"
                echo "See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers." >> "${report_file}"

                # Jitter cluster analysis (post-settling)
                if command -v python3 >/dev/null 2>&1; then
                    local jitter_cluster_md
                    jitter_cluster_md=$(python3 - "${OUTPUT_DIR}/playback.csv" "${settling_seconds}" 2>/dev/null <<'PY'
import csv
import math
import sys

path = sys.argv[1]
try:
    settling = float(sys.argv[2])
except Exception:
    settling = 0.0

# Treat any repeated/skipped frame marker as "jitter".
events = []
content_video_s = []
with open(path, newline='') as f:
    reader = csv.DictReader(f)
    for row in reader:
        try:
            video_s_raw = row.get('video_s')
            content_s_raw = row.get('content_s')
            video_s = float(video_s_raw) if video_s_raw not in (None, '') else None
            content_s = float(content_s_raw) if content_s_raw not in (None, '') else None

            # Prefer video timeline when available; fall back to content timeline.
            t = video_s if video_s is not None else (content_s if content_s is not None else 0.0)

            repeated = int(float(row.get('repeated') or 0))
            skipped = int(float(row.get('skipped') or 0))
        except Exception:
            continue

        if content_s is not None and video_s is not None:
            content_video_s.append(video_s)

        if t < settling:
            continue
        if repeated or skipped:
            events.append(t)

events.sort()
content_video_s.sort()

content_span = None
if content_video_s:
    content_span = (content_video_s[0], content_video_s[-1])

if not events:
    print('')
    print('#### Playback Jitter Clusters (post-settling)')
    print('')
    print('- No post-settling repeated/skipped markers detected in playback timeline.')
    sys.exit(0)

max_gap_s = 0.5

clusters = []
bucket = [events[0]]
prev = events[0]
for t in events[1:]:
    if (t - prev) <= max_gap_s:
        bucket.append(t)
    else:
        clusters.append(bucket)
        bucket = [t]
    prev = t
clusters.append(bucket)

def stats(xs):
    n = len(xs)
    mean = sum(xs) / n
    var = sum((x - mean) ** 2 for x in xs) / n
    return mean, math.sqrt(var), xs[-1] - xs[0]

summaries = []
for c in clusters:
    center, std, span = stats(c)
    summaries.append((len(c), span, center, std, c[0], c[-1]))

# Sort by size, then by time span.
summaries.sort(key=lambda x: (x[0], x[1]), reverse=True)

print('')
print('#### Playback Jitter Clusters (post-settling)')
print('')
print(f'- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap {max_gap_s:.1f}s')
print('- Note: this is independent from the Frame Progression (frame-box) check above')
if content_span is not None:
    print(f'- Note: repeated/skipped markers only exist while content is detected (video_s {content_span[0]:.3f}–{content_span[1]:.3f}).')
    print('  The jitter-free tail after content ends is expected and does not indicate steady-state performance.')
print('')
print('| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |')
print('|---|--------|------------|-------------|----------|------------|')
for i, (count, span, center, std, start, end) in enumerate(summaries[:3], start=1):
    print(f'| {i} | {count} | {center:.3f} | {std:.3f} | {span:.3f} | {start:.3f}–{end:.3f} |')
PY
)

                    if [[ -n "${jitter_cluster_md}" ]]; then
                        echo "${jitter_cluster_md}" >> "${report_file}"
                    fi
                fi
            fi
        fi
    fi

    if [[ "${VERBOSE}" == true ]]; then
        echo
        cat "${report_file}"
    fi

    # Append sample frame details if available
    local sample_frame_path="${OUTPUT_DIR}/c64_recording_still.png"
    local recording_mp4="${recording_found:-}"
    if [[ -z "${recording_mp4}" || ! -f "${recording_mp4}" ]]; then
        if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" ]]; then
            recording_mp4="${OUTPUT_DIR}/c64_recording.mp4"
        elif compgen -G "${OUTPUT_DIR}/*.mp4" > /dev/null; then
            recording_mp4=$(ls -t ${OUTPUT_DIR}/*.mp4 2>/dev/null | head -1)
        fi
    fi

    # Use playback.csv to bound content frames (avoid logo-only stills).
    local content_start_frame=""
    local content_end_frame=""
    if [[ -f "${OUTPUT_DIR}/playback.csv" ]] && command -v python3 >/dev/null 2>&1; then
        read -r content_start_frame content_end_frame < <(python3 - <<'PY' "${OUTPUT_DIR}/playback.csv" 2>/dev/null || true
import csv
import sys
from pathlib import Path

path = Path(sys.argv[1])
start = None
end = None
try:
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            frame_num = (row.get("frame_num") or "").strip()
            if frame_num == "":
                continue
            idx = row.get("playback_frame_index")
            if idx is None or str(idx).strip() == "":
                continue
            try:
                idx_int = int(float(idx))
            except ValueError:
                continue
            if start is None:
                start = idx_int
            end = idx_int
except Exception:
    pass

if start is not None and end is not None:
    print(f"{start} {end}")
PY
)
    fi

    # Sample frame: extract a frame showing the A/V pop white square.
    #
    # IMPORTANT: extract by exact frame index (n) rather than by timestamp (t).
    # Timestamp-based extraction can miss a 1–2 frame marker due to PTS rounding/offsets.
    if [[ -n "${recording_mp4}" && -f "${recording_mp4}" && -x "${TEST_DIR}/extract.frame" ]]; then
        local pop_time_ms pop_frame_num first_pop_frame frame_rate
        pop_time_ms=""
        pop_frame_num=""
        first_pop_frame=""
        sample_frame_seconds=""
        local sample_frame_extracted=false

        # Try to get the first detected video pop frame index from validation results
        if [[ -f "${validation_file}" ]] && command -v jq >/dev/null 2>&1; then
            first_pop_frame=$(jq -r '.av_sync_details.video_pop_frame_indices[0] // empty' "${validation_file}" 2>/dev/null || true)
            if [[ -z "${first_pop_frame}" || "${first_pop_frame}" == "null" ]]; then
                first_pop_frame=$(jq -r '.av_sync_details.sync_details[0].closest_video_pop_frame // empty' "${validation_file}" 2>/dev/null || true)
            fi
        fi

        # Fallback: directly detect a video pop frame using test_av_sync.py
        if [[ -z "${first_pop_frame}" ]] && command -v python3 >/dev/null 2>&1; then
            # Prefer accurate FPS from ffprobe; fall back to format defaults
            frame_rate=""
            if command -v ffprobe >/dev/null 2>&1; then
                frame_rate=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of default=noprint_wrappers=1:nokey=1 "${recording_mp4}" 2>/dev/null | head -1 || true)
            fi
            if [[ -z "${frame_rate}" ]]; then
                if [[ "${FORMAT}" == "PAL" ]]; then
                    frame_rate="50.0"
                else
                    frame_rate="60.0"
                fi
            else
                # Convert e.g. 30000/1001 into float with python
                frame_rate=$(python3 - <<PY 2>/dev/null || true
import sys
try:
    s = "${frame_rate}".strip()
    if "/" in s:
        a,b = s.split("/",1)
        print(float(a)/float(b))
    else:
        print(float(s))
except Exception:
    pass
PY
)
                if [[ -z "${frame_rate}" ]]; then
                    if [[ "${FORMAT}" == "PAL" ]]; then
                        frame_rate="50.0"
                    else
                        frame_rate="60.0"
                    fi
                fi
            fi

            pop_frame_num=$(python3 -c "
import sys
sys.path.insert(0, '${TEST_DIR}')
from test_av_sync import detect_video_pops
pops = detect_video_pops('${recording_mp4}', frame_rate=float('${frame_rate}'))
if pops:
    print(int(pops[0]))
" 2>/dev/null || true)
            if [[ -n "${pop_frame_num}" && "${pop_frame_num}" =~ ^[0-9]+$ ]]; then
                first_pop_frame="${pop_frame_num}"
            fi
        fi

        # If we have a frame index, extract that exact frame.
        if [[ -n "${first_pop_frame}" && "${first_pop_frame}" =~ ^[0-9]+$ ]]; then
            if [[ -n "${content_start_frame}" && -n "${content_end_frame}" ]]; then
                if (( first_pop_frame < content_start_frame || first_pop_frame > content_end_frame )); then
                    first_pop_frame=""
                fi
            fi
        fi

        # If we have a frame index, extract that exact frame.
        if [[ -n "${first_pop_frame}" && "${first_pop_frame}" =~ ^[0-9]+$ ]]; then
            # Extract a still that actually contains the marker.
            # Even with frame-index extraction, some pipelines can be off by 1 frame (PTS rounding,
            # encoder delays, etc.). We therefore try a window around the detected frame and
            # pick the earliest candidate that contains the marker (or the best-scoring one).
            #
            # For filter-rich scenarios (bloom, afterglow), the marker:
            #   1. Appears gradually (soft fade-in from afterglow persistence)
            #   2. Persists longer (afterglow can spread marker over 3-5+ frames)
            #   3. Has lower contrast (bloom spreads brightness)
            # So we need a wider search window biased forward (where marker is strongest).
            local best_tmp best_score best_frame
            best_tmp=""
            best_score="-1"
            best_frame="${first_pop_frame}"

            local tmp_dir
            tmp_dir=$(mktemp -d 2>/dev/null || true)

            # Candidates: detected frame first, then forward frames (where afterglow shows marker
            # more clearly), then backward frames as safety. Extended window for filter-rich scenarios.
            local candidates=("${first_pop_frame}" "$((first_pop_frame + 1))" "$((first_pop_frame + 2))" "$((first_pop_frame + 3))" "$((first_pop_frame - 1))" "$((first_pop_frame + 4))" "$((first_pop_frame - 2))")
            local found_good=false

            if [[ -n "${tmp_dir}" && -d "${tmp_dir}" ]] && command -v python3 >/dev/null 2>&1; then
                for cand in "${candidates[@]}"; do
                    [[ "${cand}" =~ ^-?[0-9]+$ ]] || continue
                    if (( cand < 0 )); then
                        continue
                    fi
                    if [[ -n "${content_start_frame}" && -n "${content_end_frame}" ]]; then
                        if (( cand < content_start_frame || cand > content_end_frame )); then
                            continue
                        fi
                    fi
                    local out_tmp
                    out_tmp="${tmp_dir}/frame_${cand}.png"
                    "${TEST_DIR}/extract.frame" --input "${recording_mp4}" --output "${out_tmp}" --frame "${cand}" >/dev/null 2>&1 || continue
                    if [[ ! -s "${out_tmp}" ]]; then
                        continue
                    fi

                    # Score marker presence in the expected pop area (lower-right of the content).
                    local score
                    score=$(python3 - <<'PY' "${out_tmp}" 2>/dev/null || true
import sys
try:
    import cv2  # type: ignore
    import numpy as np  # type: ignore
except Exception:
    sys.exit(0)

p = sys.argv[1]
img = cv2.imread(p)
if img is None:
    print(0)
    sys.exit(0)

# Use robust content-bound detection matching test_av_sync.py
# (handles limited-range video, filters, CRT effects with glow/bloom)
g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
h, w = g.shape[:2]

# Horizontal bounds: use 99th percentile per column (robust when scanlines create dark rows)
col_hi = np.percentile(g, 99.0, axis=0)
thr_col = max(10.0, float(np.percentile(col_hi, 90.0) * 0.20))
content_cols = np.where(col_hi > thr_col)[0]
if content_cols.size >= 2:
    left = int(content_cols[0])
    right = int(content_cols[-1]) + 1
else:
    # Fallback: assume centered horizontally
    scale_factor = h / 272.0
    scaled_c64_width = int(384 * scale_factor)
    left = int((w - scaled_c64_width) // 2)
    right = int((w + scaled_c64_width) // 2)

# Vertical bounds: same approach for rows
row_hi = np.percentile(g, 99.0, axis=1)
thr_row = max(10.0, float(np.percentile(row_hi, 90.0) * 0.20))
content_rows = np.where(row_hi > thr_row)[0]
if content_rows.size >= 2:
    top = int(content_rows[0])
    bottom = int(content_rows[-1]) + 1
else:
    top, bottom = 0, h

# Clamp bounds to valid image coordinates
left = max(0, min(w, left))
right = max(0, min(w, right))
top = max(0, min(h, top))
bottom = max(0, min(h, bottom))

content_w = max(1, right - left)
scale = content_w / 384.0
area_px = max(10, int(round(80 * scale)))

area_left = max(0, right - area_px)
area_right = right
area_bottom = bottom
area_top = max(0, bottom - area_px)
roi = g[area_top:area_bottom, area_left:area_right]
if roi.size == 0:
    print(0)
    sys.exit(0)

# Marker score: count of "much brighter than background" pixels + contrast vs ROI median.
# Avoid absolute thresholds (e.g. >200) because some presets tint/soften the marker.
med = float(np.median(roi))
thr = max(50.0, med + 60.0)
bright = int((roi > thr).sum())
contrast = float(roi.mean() - med)
score = bright + max(0.0, contrast) * 10.0
print(int(score))
PY
)
                    [[ -n "${score}" ]] || score=0

                    # Consider it “good” if there are enough bright pixels.
                    if (( score >= 5000 )); then
                        # Pick earliest good candidate.
                        best_tmp="${out_tmp}"
                        best_frame="${cand}"
                        found_good=true
                        break
                    fi

                    # Otherwise, keep best score.
                    if (( score > best_score )); then
                        best_score="${score}"
                        best_tmp="${out_tmp}"
                        best_frame="${cand}"
                    fi
                done

                if [[ -n "${best_tmp}" && -s "${best_tmp}" ]]; then
                    cp -f "${best_tmp}" "${sample_frame_path}" || true
                    sample_frame_extracted=true
                    sample_frame_index="${best_frame}"
                fi
            fi

            # Fallback if python scoring not available
            if [[ "${sample_frame_extracted}" != true ]]; then
                "${TEST_DIR}/extract.frame" --input "${recording_mp4}" --output "${sample_frame_path}" --frame "${first_pop_frame}" || true
                sample_frame_extracted=true
                sample_frame_index="${first_pop_frame}"
            fi

            if [[ -n "${tmp_dir}" && -d "${tmp_dir}" ]]; then
                rm -rf "${tmp_dir}" || true
            fi
        fi

        if [[ "${sample_frame_extracted}" != true ]]; then
            # Final fallback: use mid-content frame if bounds exist, else mid-duration.
            if [[ -n "${content_start_frame}" && -n "${content_end_frame}" ]]; then
                local mid_frame
                mid_frame=$(( (content_start_frame + content_end_frame) / 2 ))
                "${TEST_DIR}/extract.frame" --input "${recording_mp4}" --output "${sample_frame_path}" --frame "${mid_frame}" || true
            else
                if [[ -z "${sample_frame_seconds}" ]]; then
                    local dur_sec
                    if command -v ffprobe >/dev/null 2>&1; then
                        dur_sec=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${recording_mp4}" 2>/dev/null | head -1 || true)
                    fi
                    if [[ -n "${dur_sec}" ]]; then
                        sample_frame_seconds=$(awk -v d="${dur_sec}" 'BEGIN{printf "%.3f", d*0.50}')
                    else
                        sample_frame_seconds="4.500"
                    fi
                fi

                "${TEST_DIR}/extract.frame" --input "${recording_mp4}" --output "${sample_frame_path}" --time "${sample_frame_seconds}" || true
            fi
        fi
    fi

    # Emit a Video block with download link before the Sample Frame
    if [[ -n "${recording_mp4}" && -f "${recording_mp4}" ]]; then
        echo >> "${report_file}"
        echo "### Video" >> "${report_file}"
        echo >> "${report_file}"
        rel_name=$(basename "${recording_mp4}")
        local download_note
        download_note="(Available from local runs or CI build artifacts.)"
        if [[ -f "${OUTPUT_DIR}/${rel_name}" ]]; then
            echo "- Download: [${rel_name}](${rel_name}) ${download_note}" >> "${report_file}"
        else
            echo "- Download: [${rel_name}](${recording_mp4}) ${download_note}" >> "${report_file}"
        fi

        if command -v ffprobe >/dev/null 2>&1; then
            video_duration_sec=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${recording_mp4}" 2>/dev/null | head -1 || true)
            if [[ -n "${video_duration_sec}" ]]; then
                video_duration_fmt=$(format_to_one_decimal "${video_duration_sec}")
                echo "- Duration: ${video_duration_fmt} s" >> "${report_file}"
            fi
        fi

        if [[ -z "${video_duration_fmt}" ]]; then
            if [[ "${FORMAT}" == "PAL" ]]; then
                video_duration_fmt=$(awk -v frames="${FRAMES}" 'BEGIN{printf "%.1f", frames/50.0}')
            else
                video_duration_fmt=$(awk -v frames="${FRAMES}" 'BEGIN{printf "%.1f", frames/60.0}')
            fi
            echo "- Duration: ${video_duration_fmt} s" >> "${report_file}"
        fi

        echo >> "${report_file}"
    elif [[ "${OBS_ENABLED}" == true ]]; then
        echo >> "${report_file}"
        echo "### Video" >> "${report_file}"
        echo >> "${report_file}"
        echo "- ❌ Video: Recording not found" >> "${report_file}"
    fi

    if [[ -z "${sample_frame_seconds}" && -n "${sample_frame_index}" ]]; then
        local fps=0
        if [[ "${FORMAT}" == "PAL" ]]; then
            fps=50
        else
            fps=60
        fi
        if (( fps > 0 )); then
            sample_frame_seconds=$(awk -v frame="${sample_frame_index}" -v rate="${fps}" 'BEGIN{printf "%.3f", frame/rate}')
        fi
    fi

    local sample_frame_seconds_fmt=""
    if [[ -n "${sample_frame_seconds}" ]]; then
        sample_frame_seconds_fmt=$(format_to_one_decimal "${sample_frame_seconds}")
        sample_frame_timestamp=$(format_seconds_to_timestamp "${sample_frame_seconds}")
    fi

    if [[ -f "${sample_frame_path}" ]]; then
        echo >> "${report_file}"
        echo "### Sample Frame" >> "${report_file}"
        echo >> "${report_file}"
        echo "![Sample Frame](./$(basename "${sample_frame_path}"))" >> "${report_file}"
        echo >> "${report_file}"
        echo "- **Top-left**: Text box with scenario name" >> "${report_file}"
        echo "- **Top-right**: VIC-II palette reference grid of all C64 colors" >> "${report_file}"
        echo "- **Center**: Diagonal pattern cycling through all C64 colors" >> "${report_file}"
        echo "- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)" >> "${report_file}"
        echo "- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)" >> "${report_file}"

        local origin_parts=()
        if [[ -n "${sample_frame_index}" ]]; then
            origin_parts+=("frame ${sample_frame_index}")
        fi
        if [[ -n "${sample_frame_timestamp}" ]]; then
            origin_parts+=("${sample_frame_timestamp}")
        elif [[ -n "${sample_frame_seconds_fmt}" ]]; then
            origin_parts+=("${sample_frame_seconds_fmt}s")
        fi

        local origin_text=""
        if (( ${#origin_parts[@]} > 0 )); then
            origin_text=$(join_by " at " "${origin_parts[@]}")
        fi

        if [[ -n "${origin_text}" ]]; then
            if [[ -n "${video_duration_fmt}" ]]; then
                echo "- Taken from ${origin_text} of the ${video_duration_fmt} s video above." >> "${report_file}"
            else
                echo "Taken from ${origin_text} of the video above." >> "${report_file}"
            fi
        else
            if [[ -n "${video_duration_fmt}" ]]; then
                echo "- Taken from the ${video_duration_fmt} s video above." >> "${report_file}"
            else
                echo "- Taken from the video above." >> "${report_file}"
            fi
        fi
    fi

    log_success "Markdown report saved to ${report_file}"
}

# Cleanup temporary files
cleanup() {
    # Always stop resource monitoring regardless of cleanup flag
    stop_resource_monitoring

    if [[ "${CLEANUP}" == false ]]; then
        log_info "Skipping cleanup (--no-cleanup specified)"
        return
    fi

    log_info "Cleaning up temporary files..."

    # Remove large packet files but keep logs
    if [[ -d "${TEST_DIR}/test_packets" ]]; then
        rm -rf "${TEST_DIR}/test_packets"
    fi
}

# Run all scenarios in sequence
run_all_scenarios() {
    local scenarios_dir="${TEST_DIR}/scenarios"
    local passed=0
    local failed=0
    local skipped=0
    local failed_scenarios=()

    echo "=========================================="
    echo "   Running ALL E2E Scenarios"
    echo "=========================================="
    echo

    # Get list of scenarios
    local scenarios=()
    for scenario_dir in "${scenarios_dir}"/*/; do
        if [[ -f "${scenario_dir}/scenario.yaml" ]]; then
            scenarios+=("$(basename "${scenario_dir}")")
        fi
    done

    log_info "Found ${#scenarios[@]} scenarios to run"
    echo

    # Run each scenario
    for scenario in "${scenarios[@]}"; do
        echo
        echo "----------------------------------------"
        echo "  Scenario: ${scenario}"
        echo "----------------------------------------"

        # Build command with preserved options
        local cmd=("${SCRIPT_DIR}/e2e.sh" "--scenario" "${scenario}")

        # Pass through common options
        [[ "${VERBOSE}" == true ]] && cmd+=("--verbose")
        [[ "${SKIP_BUILD}" == true ]] && cmd+=("--skip-build")
        [[ "${OBS_ENABLED}" == false ]] && cmd+=("--no-obs")
        [[ -n "${DURATION}" ]] && cmd+=("--duration" "${DURATION}")
        [[ "${CLEANUP}" == false ]] && cmd+=("--no-cleanup")

        if "${cmd[@]}"; then
            passed=$((passed + 1))
            log_success "Scenario ${scenario}: PASSED"
        else
            local exit_code=$?
            if [[ ${exit_code} -eq 0 ]]; then
                skipped=$((skipped + 1))
                log_info "Scenario ${scenario}: SKIPPED"
            else
                failed=$((failed + 1))
                failed_scenarios+=("${scenario}")
                log_error "Scenario ${scenario}: FAILED"
            fi
        fi
    done

    # Summary
    echo
    echo "=========================================="
    echo "         All Scenarios Summary"
    echo "=========================================="
    echo "  Total:   ${#scenarios[@]}"
    echo "  Passed:  ${passed}"
    echo "  Failed:  ${failed}"
    echo "  Skipped: ${skipped}"
    echo

    if [[ ${#failed_scenarios[@]} -gt 0 ]]; then
        log_error "Failed scenarios:"
        for s in "${failed_scenarios[@]}"; do
            echo "    - ${s}"
        done
        echo
        return 1
    fi

    log_success "All scenarios passed!"
    return 0
}

# Main execution
main() {
    echo "=========================================="
    echo "      C64 Stream E2E Test Suite"
    echo "=========================================="
    echo

    # Parse arguments
    parse_args "$@"

    # If --all specified, run all scenarios and exit
    if [[ "${RUN_ALL_SCENARIOS}" == true ]]; then
        run_all_scenarios
        exit $?
    fi

    # Show configuration
    log_info "Test Configuration:"
    echo "  Format: ${FORMAT}"
    echo "  Frames: ${FRAMES}"
    echo "  Video Port: ${VIDEO_PORT}"
    echo "  Audio Port: ${AUDIO_PORT}"
    echo "  Verbose: ${VERBOSE}"
    echo "  Skip Build: ${SKIP_BUILD}"
    echo "  OBS Enabled: ${OBS_ENABLED}"
    echo "  Monitor Resources: ${MONITOR_RESOURCES}"
    echo

    # Ensure background resource logging never outlives the script.
    # stop_resource_monitoring is idempotent, so it is safe to call multiple times.
    CLEANUP_DONE=false
    cleanup_once() {
        if [[ "${CLEANUP_DONE}" == true ]]; then
            return 0
        fi
        CLEANUP_DONE=true
        cleanup
    }

    cleanup_on_signal() {
        stop_resource_monitoring
        cleanup_once
        exit 1
    }

    # Always stop periodic resource logging on exit.
    trap stop_resource_monitoring EXIT
    trap cleanup_on_signal INT TERM

    # Execute test pipeline
    local start_time=$(date +%s)
    local test_result=0

    start_resource_monitoring

    check_dependencies
    setup_process_priority

    # CRITICAL: Stop real C64 device from streaming to prevent cross-pollution
    # Synthetic tests use mock C64U on localhost, but real device may be sending
    # UDP packets to the same ports, causing tests to receive wrong packets
    stop_real_c64_streaming

    build_project
    install_plugin
    generate_packets

    if ! run_e2e_test; then
        test_result=1
    fi

    # Run scenario-specific assertions if a scenario was specified
    if [[ -n "${SCENARIO}" && ${test_result} -eq 0 ]]; then
        if ! run_scenario_assertions; then
            test_result=1
        fi
    fi

    # Stop resource monitoring before generating final outputs to prevent
    # the script from appearing to hang with periodic resource logs
    stop_resource_monitoring

    # Generate playback.csv before report (report references it)
    generate_playback_csv
    generate_report
    cleanup_once

    # Show detailed test summary
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    echo
    echo "=========================================="
    echo "         Test Summary"
    echo "=========================================="
    echo "Duration: ${duration} seconds"
    echo

    # Show detailed validation results if available
    local validation_file="${OUTPUT_DIR}/validation_results.json"
    if [[ -f "${validation_file}" ]] && command -v jq >/dev/null 2>&1; then
        echo "Validation Results:"

        # UDP Reception
        local udp_status=$(jq -r '.udp_reception.status' "${validation_file}" 2>/dev/null || echo "unknown")
        local udp_details=$(jq -r '.udp_reception.details' "${validation_file}" 2>/dev/null || echo "")
        case "${udp_status}" in
            "pass") echo "  ✅ UDP Packet Reception: ${udp_details}" ;;
            "warning") echo "  ⚠️  UDP Packet Reception: ${udp_details}" ;;
            "fail") echo "  ❌ UDP Packet Reception: ${udp_details}" ;;
            *) echo "  ❓ UDP Packet Reception: Status unknown" ;;
        esac

        # Network Timing
        local net_status=$(jq -r '.network_timing.status' "${validation_file}" 2>/dev/null || echo "unknown")
        local net_details=$(jq -r '.network_timing.details' "${validation_file}" 2>/dev/null || echo "")
        case "${net_status}" in
            "pass") echo "  ✅ Network Timing: ${net_details}" ;;
            "warning") echo "  ⚠️  Network Timing: ${net_details}" ;;
            "fail") echo "  ❌ Network Timing: ${net_details}" ;;
            *) echo "  ❓ Network Timing: Status unknown" ;;
        esac

        # Frame Processing
        local frame_status=$(jq -r '.frame_processing.status' "${validation_file}" 2>/dev/null || echo "unknown")
        local frame_details=$(jq -r '.frame_processing.details' "${validation_file}" 2>/dev/null || echo "")
        case "${frame_status}" in
            "pass") echo "  ✅ Frame Processing: ${frame_details}" ;;
            "warning") echo "  ⚠️  Frame Processing: ${frame_details}" ;;
            "fail") echo "  ❌ Frame Processing: ${frame_details}" ;;
            *) echo "  ❓ Frame Processing: Status unknown" ;;
        esac

        # Video Recording
        local video_status=$(jq -r '.video_recording.status' "${validation_file}" 2>/dev/null || echo "unknown")
        local video_details=$(jq -r '.video_recording.details' "${validation_file}" 2>/dev/null || echo "")
        case "${video_status}" in
            "pass") echo "  ✅ Video Recording: ${video_details}" ;;
            "warning") echo "  ⚠️  Video Recording: ${video_details}" ;;
            "fail") echo "  ❌ Video Recording: ${video_details}" ;;
            *) echo "  ❓ Video Recording: Status unknown" ;;
        esac

        # Packet Integrity (Duration Check)
        local integrity_status=$(jq -r '.packet_integrity.status' "${validation_file}" 2>/dev/null || echo "unknown")
        local integrity_details=$(jq -r '.packet_integrity.details' "${validation_file}" 2>/dev/null || echo "")
        case "${integrity_status}" in
            "pass") echo "  ✅ Content Integrity: ${integrity_details}" ;;
            "warning") echo "  ⚠️  Content Integrity: ${integrity_details}" ;;
            "fail") echo "  ❌ Content Integrity: ${integrity_details}" ;;
            "unknown") echo "  ❓ Content Integrity: ${integrity_details}" ;;
            *) echo "  ❓ Content Integrity: Status unknown" ;;
        esac

        echo
    fi

    if [[ ${test_result} -eq 0 ]]; then
        log_success "E2E test completed successfully!"
    echo "View detailed report: ${OUTPUT_DIR}/README.md"
    else
        log_warning "E2E test encountered issues"
        echo "Check logs in: ${OUTPUT_DIR}/"
    fi

    exit ${test_result}
}

# Run main function with all arguments
main "$@"
