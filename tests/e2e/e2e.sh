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
OUTPUT_DIR="${TEST_DIR}/test_output"

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
DEFAULT_MONITOR_RESOURCES=false  # Resource monitoring for CI

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
    -v, --verbose           Enable verbose logging
    -s, --skip-build        Skip building plugin and tools
    -o, --obs               Enable OBS integration (default)
    --no-obs                Disable OBS integration
    --no-cleanup            Skip cleanup of temporary files
    --monitor-resources     Enable periodic system resource monitoring
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

PACKET GENERATION:
    PAL:  50 FPS, 384x272 resolution, ~3400 packets/sec
    NTSC: 60 FPS, 384x240 resolution, ~4080 packets/sec

    Video packets: 780 bytes, ~300μs intervals
    Audio packets: 770 bytes, ~4ms intervals

OUTPUT:
    Test artifacts are saved to: ${OUTPUT_DIR}
    - Generated packets: test_packets/
    - Test logs: test_output/
    - Recordings (if OBS enabled): recording_*.mkv

EOF
}

# Parse command line arguments
parse_args() {
    FORMAT="${DEFAULT_FORMAT}"
    FRAMES="${DEFAULT_FRAMES}"
    DURATION=""
    VIDEO_PORT="${DEFAULT_VIDEO_PORT}"
    AUDIO_PORT="${DEFAULT_AUDIO_PORT}"
    VERBOSE="${DEFAULT_VERBOSE}"
    SKIP_BUILD="${DEFAULT_SKIP_BUILD}"
    CLEANUP="${DEFAULT_CLEANUP}"
    OBS_ENABLED="${DEFAULT_OBS_ENABLED}"
    MONITOR_RESOURCES="${DEFAULT_MONITOR_RESOURCES}"

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

    # Python packages
    for package in numpy PIL cv2; do
        if ! python3 -c "import ${package}" 2>/dev/null; then
            case "${package}" in
                "cv2") missing_deps+=("python3-opencv") ;;
                *) missing_deps+=("python3-${package,,}") ;;
            esac
        fi
    done

    # Check Python E2E test dependencies via system package manager
    log_info "Installing Python E2E test dependencies..."

    # Check for numpy
    if ! python3 -c "import numpy" 2>/dev/null; then
        missing_deps+=("python3-numpy")
    fi

    # Check for OpenCV (cv2 module name)
    if ! python3 -c "import cv2" 2>/dev/null; then
        missing_deps+=("python3-opencv")
    fi    # Virtual display tools (always needed for headless testing)
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

        # Update package list
        if [[ "${VERBOSE}" == true ]]; then
            sudo apt-get update
        else
            sudo apt-get update -qq
        fi

        # Install missing packages
        if [[ "${VERBOSE}" == true ]]; then
            sudo apt-get install -y "${missing_deps[@]}"
        else
            sudo apt-get install -y "${missing_deps[@]}" > /dev/null 2>&1
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

        for package in numpy PIL; do
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

    log_success "Plugin installed to OBS"
}

# Generate test packets
generate_packets() {
    log_info "Generating ${FORMAT} test packets (${FRAMES} frames)..."

    cd "${TEST_DIR}"

    # Create output directory
    mkdir -p test_packets

    # Generate packets
    local cmd=(
        "./generate_packets.py"
        "--frames" "${FRAMES}"
        "--format" "${FORMAT}"
        "--output" "test_packets"
    )

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
    log_info "Running E2E test..."

    cd "${TEST_DIR}"

    # Prepare output directory
    mkdir -p "${OUTPUT_DIR}"

    # Build test command
    local cmd=(
        "python3" "./e2e.py"
        "--test-dir" "."
        "--format" "${FORMAT}"
        "--frames" "${FRAMES}"
        "--video-port" "${VIDEO_PORT}"
        "--audio-port" "${AUDIO_PORT}"
        "--udp-replay" "${BUILD_DIR}/tests/e2e/udp_replay"
    )

    if [[ "${VERBOSE}" == true ]]; then
        cmd+=("--verbose")
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

# Generate test report
generate_report() {
    log_info "Generating test report..."

    local report_file="${OUTPUT_DIR}/test_report.txt"
    local timestamp=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

    cat > "${report_file}" << EOF
C64 Stream E2E Test Report
Generated: ${timestamp}

Test Configuration:
  Format: ${FORMAT}
  Frames: ${FRAMES}
  Duration: $(if [[ "${FORMAT}" == "PAL" ]]; then awk "BEGIN {printf \"%.1f\", ${FRAMES}/50}"; else awk "BEGIN {printf \"%.1f\", ${FRAMES}/60}"; fi) seconds
  Video Port: ${VIDEO_PORT}
  Audio Port: ${AUDIO_PORT}
  OBS Enabled: ${OBS_ENABLED}

Build Information:
  Project: $(jq -r '.name // "unknown"' "${PROJECT_ROOT}/buildspec.json" 2>/dev/null)
  Version: $(jq -r '.version // "unknown"' "${PROJECT_ROOT}/buildspec.json" 2>/dev/null)

Test Results:
EOF

    # Add packet statistics
    if [[ -d "${TEST_DIR}/test_packets" ]]; then
        local video_count audio_count
        video_count=$(find "${TEST_DIR}/test_packets/video/${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)
        audio_count=$(find "${TEST_DIR}/test_packets/audio/${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)

        cat >> "${report_file}" << EOF
  ✅ Packet Generation: ${video_count} video, ${audio_count} audio packets
  ✅ UDP Replay: Completed successfully
EOF
    fi

    # Add OBS results if enabled
    if [[ "${OBS_ENABLED}" == true ]]; then
        if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" || -f "${OUTPUT_DIR}/c64_recording.mkv" ]]; then
            echo "  ✅ OBS Recording: Available" >> "${report_file}"
        else
            echo "  ❌ OBS Recording: Not found" >> "${report_file}"
        fi
    else
        echo "  ⚠️  OBS Integration: Disabled (use --obs to enable)" >> "${report_file}"
    fi

    cat >> "${report_file}" << EOF

Artifacts:
  Report: ${report_file}
  Packets: ${TEST_DIR}/test_packets/
  Output: ${OUTPUT_DIR}/
EOF

    if [[ "${VERBOSE}" == true ]]; then
        echo
        cat "${report_file}"
    fi

    log_success "Test report saved to ${report_file}"
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

# Main execution
main() {
    echo "=========================================="
    echo "      C64 Stream E2E Test Suite"
    echo "=========================================="
    echo

    # Parse arguments
    parse_args "$@"

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
        echo "View detailed report: ${OUTPUT_DIR}/test_report.txt"
    else
        log_warning "E2E test encountered issues"
        echo "Check logs in: ${OUTPUT_DIR}/"
    fi

    exit ${test_result}
}

# Run main function with all arguments
main "$@"
