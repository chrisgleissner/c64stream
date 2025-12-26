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
DEFAULT_FRAMES=300  # ~5 seconds at NTSC timing
DEFAULT_DURATION=5  # seconds - alternative to frames
DEFAULT_VIDEO_PORT=11000
DEFAULT_AUDIO_PORT=11001
DEFAULT_VERBOSE=false
DEFAULT_SKIP_BUILD=false
DEFAULT_CLEANUP=true
DEFAULT_OBS_ENABLED=true   # OBS integration now implemented
DEFAULT_X11_DISPLAY=":99"
DEFAULT_MONITOR_RESOURCES=true  # Resource monitoring for CI (enabled by default)
DEFAULT_SCENARIO_OVERRIDES=""
DEFAULT_SCENARIO_NAME=""
DEFAULT_PACKET_PATTERN=""
DEFAULT_SCENARIO=""
DEFAULT_CSV_MAX_ROWS=1000  # Truncate CSV files to first 1000 rows
SCENARIO_CI_SKIPPED=false  # Set by load_scenario if ci_skip=true on CI
DEFAULT_RUN_ALL_SCENARIOS=false  # Run all scenarios in sequence
DEFAULT_ENABLE_RESOURCE_MONITORING=true  # CPU/GPU/RAM monitoring during packet replay (enabled by default)
DEFAULT_RESOURCE_INTERVAL_MS=500  # Resource monitoring sample interval in ms

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

# Help message
show_help() {
    cat << EOF
C64 Stream E2E Test Script

Usage: $0 [OPTIONS]

This script runs comprehensive end-to-end tests for the C64 Stream OBS plugin.
It builds the plugin, generates test packets, and validates the complete pipeline.

OPTIONS:
    -f, --format FORMAT     Video format (PAL, NTSC) [default: ${DEFAULT_FORMAT}]
    -F, --frames FRAMES     Number of frames to test [default: ${DEFAULT_FRAMES}]
    -d, --duration SECONDS  Test duration in seconds (overrides --frames)
    --output-dir DIR        Output directory for test artifacts [default: ${DEFAULT_OUTPUT_DIR}]
    --csv-max-rows ROWS     Truncate CSV files to first ROWS rows (0=disable) [default: ${DEFAULT_CSV_MAX_ROWS}]
    -v, --verbose           Enable verbose logging
    -s, --skip-build        Skip building plugin and tools
    -o, --obs               Enable OBS integration (default)
    --no-obs                Disable OBS integration
    --no-cleanup            Skip cleanup of temporary files
    --monitor-resources     Enable periodic system resource monitoring
    --enable-resource-monitoring  Enable CPU/GPU/RAM monitoring during packet replay (saves to resource.csv/json)
    --resource-interval-ms MS     Resource monitoring interval in milliseconds [default: ${DEFAULT_RESOURCE_INTERVAL_MS}]
    --all                   Run ALL scenarios in sequence
    -h, --help             Show this help message

EXAMPLES:
    # Quick 5-second test (default)
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
    - Recordings (if OBS enabled): recording_*.mkv

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

    if [[ ! -f "${scenario_yaml}" ]]; then
        log_error "Scenario not found: ${scenario_name}"
        log_error "Expected: ${scenario_yaml}"
        log_info "Use --list-scenarios to see available scenarios"
        exit 1
    fi

    log_info "Loading scenario: ${scenario_name}"

    # Parse scenario.yaml (new concise format)
    local name format preset pattern
    name=$(grep -m1 "^name:" "${scenario_yaml}" | sed 's/^name: *//' || true)
    format=$(grep -m1 "^format:" "${scenario_yaml}" | sed 's/^format: *//' || true)
    preset=$(grep -m1 "^preset:" "${scenario_yaml}" | sed 's/^preset: *//' || true)
    pattern=$(grep -m1 "^pattern:" "${scenario_yaml}" | sed 's/^pattern: *//' || true)

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

    # Set SCENARIO_NAME if not already set
    if [[ -z "${SCENARIO_NAME}" ]]; then
        SCENARIO_NAME="${name}"
    fi

    # Optional packet pattern (solid/diagonal) for scanline-specific scenarios
    if [[ -n "${pattern}" ]]; then
        PACKET_PATTERN="${pattern}"
        log_info "  Packet pattern: ${PACKET_PATTERN}"
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
    DURATION=""
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
    RUN_ALL_SCENARIOS="${DEFAULT_RUN_ALL_SCENARIOS}"
    ENABLE_RESOURCE_MONITORING="${DEFAULT_ENABLE_RESOURCE_MONITORING}"
    RESOURCE_INTERVAL_MS="${DEFAULT_RESOURCE_INTERVAL_MS}"

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
                shift 2
                ;;
            -d|--duration)
                DURATION="$2"
                if ! [[ "${DURATION}" =~ ^[0-9]+$ ]] || [[ "${DURATION}" -lt 1 ]]; then
                    log_error "Invalid duration: ${DURATION}. Must be a positive integer."
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
            --monitor-resources)
                MONITOR_RESOURCES=true
                shift
                ;;
            --enable-resource-monitoring)
                ENABLE_RESOURCE_MONITORING=true
                shift
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
        if [[ "${FORMAT}" == "PAL" ]]; then
            FRAMES=$((DURATION * 50))  # 50 FPS for PAL
        else
            FRAMES=$((DURATION * 60))  # 60 FPS for NTSC
        fi
        log_info "Duration ${DURATION}s = ${FRAMES} frames for ${FORMAT}"
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

    # Python packages (only numpy required by generate_packets.py)
    for package in numpy; do
        if ! python3 -c "import ${package}" >/dev/null 2>&1; then
            # Prefer distro package when not using a virtual environment
            missing_deps+=("python3-${package,,}")
        fi
    done

    # Check for optional Python runtime deps (requests, websocket) - used for OBS WebSocket API
    # These are optional - E2E tests work without them by gracefully degrading functionality
    if ! python3 -c "import requests" >/dev/null 2>&1; then
        log_info "Optional: python3-requests not found (OBS WebSocket API will be disabled)"
    fi
    if ! python3 -c "import websocket" >/dev/null 2>&1; then
        log_info "Optional: python3-websocket not found (OBS WebSocket API will be disabled)"
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

        # Determine privilege escalation (use sudo if available, else run directly if root)
        local SUDO="sudo"
        if [[ $(id -u) -eq 0 ]] || ! command -v sudo >/dev/null 2>&1; then
            SUDO=""
        fi

        # Update package list
        if [[ "${VERBOSE}" == true ]]; then
            ${SUDO} apt-get update
        else
            ${SUDO} apt-get update -qq
        fi

        # Install missing packages
        if [[ "${VERBOSE}" == true ]]; then
            ${SUDO} apt-get install -y "${missing_deps[@]}"
        else
            ${SUDO} apt-get install -y "${missing_deps[@]}" > /dev/null 2>&1
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

        for package in numpy; do
            if ! python3 -c "import ${package}" 2>/dev/null; then
                still_missing+=("python3-${package,,}")
            fi
        done

        if [[ ${#still_missing[@]} -gt 0 ]]; then
            log_error "Failed to install dependencies: ${still_missing[*]}"
            log_info "Please install manually: sudo apt-get install ${still_missing[*]}"
            exit 1
        fi

        log_success "Dependencies installed successfully"
    else
        log_success "All dependencies satisfied"
    fi
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
        "--video-port" "${VIDEO_PORT}"
        "--audio-port" "${AUDIO_PORT}"
        "--udp-replay" "${udp_replay_path}"
    )

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
        cmd+=("--resource-interval-ms" "${RESOURCE_INTERVAL_MS}")
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
    elif [[ -f "${OUTPUT_DIR}/c64_recording.mkv" ]]; then
        recording_file="${OUTPUT_DIR}/c64_recording.mkv"
    else
        log_warning "No recording found for assertions"
        return 0  # Not a fatal error
    fi

    # Run assertions using the assertions module
    local cmd=(
        "python3" "-m" "assertions"
        "--mp4" "${recording_file}"
        "--scenario" "${SCENARIO}"
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

    # Add Resource Usage section first if resource.json exists
    local resource_json="${OUTPUT_DIR}/resource.json"
    if [[ -f "${resource_json}" ]] && command -v jq >/dev/null 2>&1; then
        local duration_ms sample_count cpu_median cpu_max ram_mb_median gpu_median gpu_max obs_cpu_median obs_cpu_max
        duration_ms=$(jq -r '.duration_ms // 0' "${resource_json}")
        sample_count=$(jq -r '.sample_count // 0' "${resource_json}")
        cpu_median=$(jq -r '.cpu_percent.median // 0' "${resource_json}")
        cpu_max=$(jq -r '.cpu_percent.max // 0' "${resource_json}")
        ram_mb_median=$(jq -r '.ram_mb.median // 0' "${resource_json}")
        obs_cpu_median=$(jq -r '.obs_cpu_percent.median // null' "${resource_json}")
        obs_cpu_max=$(jq -r '.obs_cpu_percent.max // null' "${resource_json}")
        gpu_median=$(jq -r '.gpu_percent.median // null' "${resource_json}")
        gpu_max=$(jq -r '.gpu_percent.max // null' "${resource_json}")

        if [[ "${sample_count}" -gt 0 ]]; then
            local duration_sec
            duration_sec=$(awk -v ms="${duration_ms}" 'BEGIN{printf "%.1f", ms/1000}')
            echo >> "${report_file}"
            echo "### Resource Usage" >> "${report_file}"
            echo >> "${report_file}"
            echo "During the test's processing window (${duration_sec}s, ${sample_count} samples):" >> "${report_file}"
            echo >> "${report_file}"
            echo "- CPU: ${cpu_median}% median (max: ${cpu_max}%)" >> "${report_file}"
            if [[ "${obs_cpu_median}" != "null" && -n "${obs_cpu_median}" ]]; then
                echo "- OBS CPU: ${obs_cpu_median}% median (max: ${obs_cpu_max}%)" >> "${report_file}"
            fi
            echo "- RAM: ${ram_mb_median} MB median" >> "${report_file}"
            if [[ "${gpu_median}" != "null" && -n "${gpu_median}" ]]; then
                echo "- GPU: ${gpu_median}% median (max: ${gpu_max}%)" >> "${report_file}"
            fi
            echo "- [resource.csv](resource.csv) | [resource.json](resource.json)" >> "${report_file}"
        fi
    fi

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
    if (( ${#event_links[@]} > 0 )); then
        echo "- Events: $(join_by ', ' "${event_links[@]}")" >> "${report_file}"
    fi

    local recording_found=""

    # Add OBS results if enabled
    if [[ "${OBS_ENABLED}" == true ]]; then
        if [[ -f "${OUTPUT_DIR}/recording_${FORMAT}.mkv" ]]; then
            recording_found="${OUTPUT_DIR}/recording_${FORMAT}.mkv"
        elif compgen -G "${OUTPUT_DIR}/c64_recording.*" > /dev/null; then
            recording_found=$(ls -t ${OUTPUT_DIR}/c64_recording.* 2>/dev/null | head -1)
        elif compgen -G "${OUTPUT_DIR}/*.mkv" > /dev/null; then
            recording_found=$(ls -t ${OUTPUT_DIR}/*.mkv 2>/dev/null | head -1)
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
                    chan=$(jq -r ".av_sync_details.sync_details[$i].channel // \"?\"" "${validation_file}")
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
            channels=$(jq -r '[.av_sync_details.sync_details[]? | if .channel=="L" then "L" elif .channel=="R" then "R" else "B" end] | join("")' "${validation_file}")
            echo >> "${report_file}"
            echo "- Channels: ${channels}" >> "${report_file}"

            # Alternation check (ignore B), report verdict
            local seq_str alternates
            seq_str=$(jq -r '[.av_sync_details.sync_details[]? | select(.channel=="L" or .channel=="R") | .channel] | join(" ")' "${validation_file}")
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
    fi

    if [[ "${VERBOSE}" == true ]]; then
        echo
        cat "${report_file}"
    fi

    # Append sample frame details if available
    local sample_frame_path="${OUTPUT_DIR}/c64_recording_still.png"
    local recording_mp4="${recording_found:-}"
    if [[ -z "${recording_mp4}" || ! -f "${recording_mp4}" ]]; then
        if compgen -G "${OUTPUT_DIR}/c64_recording.*" > /dev/null; then
            recording_mp4=$(ls -t ${OUTPUT_DIR}/c64_recording.* 2>/dev/null | head -1)
        elif compgen -G "${OUTPUT_DIR}/*.mp4" > /dev/null; then
            recording_mp4=$(ls -t ${OUTPUT_DIR}/*.mp4 2>/dev/null | head -1)
        elif [[ -f "${OUTPUT_DIR}/recording_${FORMAT}.mkv" ]]; then
            recording_mp4="${OUTPUT_DIR}/recording_${FORMAT}.mkv"
        fi
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
                fi
            fi

            # Fallback if python scoring not available
            if [[ "${sample_frame_extracted}" != true ]]; then
                "${TEST_DIR}/extract.frame" --input "${recording_mp4}" --output "${sample_frame_path}" --frame "${first_pop_frame}" || true
                sample_frame_extracted=true
            fi

            if [[ -n "${tmp_dir}" && -d "${tmp_dir}" ]]; then
                rm -rf "${tmp_dir}" || true
            fi
        fi

        if [[ "${sample_frame_extracted}" != true ]]; then
            # Final fallback to mid-point if no pop found
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

    # Emit a Video block with download link before the Sample Frame
    if [[ -n "${recording_mp4}" && -f "${recording_mp4}" ]]; then
        echo >> "${report_file}"
        echo "### Video" >> "${report_file}"
        echo >> "${report_file}"
        rel_name=$(basename "${recording_mp4}")
        if [[ -f "${OUTPUT_DIR}/${rel_name}" ]]; then
            echo "- Download: [${rel_name}](${rel_name})" >> "${report_file}"
        else
            echo "- Download: [${rel_name}](${recording_mp4})" >> "${report_file}"
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
        echo "- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail." >> "${report_file}"

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

    # Cleanup function to handle interruptions
    cleanup_on_exit() {
        stop_resource_monitoring
        cleanup
        exit 1
    }
    trap cleanup_on_exit INT TERM

    # Execute test pipeline
    local start_time=$(date +%s)
    local test_result=0

    start_resource_monitoring

    check_dependencies
    build_project
    install_plugin
    generate_packets

    if ! run_e2e_test; then
        test_result=1
    fi

    # Run scenario-specific assertions if a scenario was specified
    if [[ -n "${SCENARIO}" && ${test_result} -eq 0 ]]; then
        run_scenario_assertions
        if [[ $? -ne 0 ]]; then
            test_result=1
        fi
    fi

    generate_report
    cleanup

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
