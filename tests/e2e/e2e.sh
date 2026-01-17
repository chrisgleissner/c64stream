#!/bin/bash
# e2e.sh - End-to-End Test Runner for C64 Stream

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="${SCRIPT_DIR}"
SCENARIOS_DIR="${TEST_DIR}/scenarios"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Source libraries
source "${SCRIPT_DIR}/shell_lib/util.sh"
source "${SCRIPT_DIR}/shell_lib/args.sh"
source "${SCRIPT_DIR}/shell_lib/system.sh"
source "${SCRIPT_DIR}/shell_lib/deps.sh"
source "${SCRIPT_DIR}/shell_lib/scenarios.sh"
source "${SCRIPT_DIR}/shell_lib/build.sh"
source "${SCRIPT_DIR}/shell_lib/packets.sh"
source "${SCRIPT_DIR}/shell_lib/test.sh"
source "${SCRIPT_DIR}/shell_lib/report.sh"

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

configure_nvidia_accel() {
    export C64_E2E_USE_NVIDIA=0

    if [[ "${DISABLE_NVIDIA}" == true ]]; then
        log_info "NVIDIA acceleration disabled via --no-nvidia"
        return 0
    fi

    if [[ "${CI:-false}" == "true" ]] || [[ "${GITHUB_ACTIONS:-false}" == "true" ]]; then
        log_info "CI environment detected - NVIDIA acceleration disabled"
        return 0
    fi

    if ! command -v ffmpeg >/dev/null 2>&1; then
        log_info "ffmpeg not found; skipping NVIDIA acceleration"
        return 0
    fi

    if ! ffmpeg -hide_banner -loglevel error -hwaccels 2>/dev/null | grep -qiE '(^|\s)cuda(\s|$)'; then
        log_info "FFmpeg CUDA hwaccel not available; skipping NVIDIA acceleration"
        return 0
    fi

    if ! ffmpeg -hide_banner -loglevel error -encoders 2>/dev/null | grep -qiE 'h264_nvenc'; then
        log_info "FFmpeg NVENC encoder not available; skipping NVIDIA acceleration"
        return 0
    fi

    if command -v nvidia-smi >/dev/null 2>&1; then
        if ! nvidia-smi -L >/dev/null 2>&1; then
            log_info "nvidia-smi did not report a GPU; skipping NVIDIA acceleration"
            return 0
        fi
    fi

    export C64_E2E_USE_NVIDIA=1
    log_info "NVIDIA acceleration enabled (NVENC/NVDEC)"

    local nvidia_overrides="${TEST_DIR}/config/obs-studio-overrides/nvidia"
    if [[ -d "${nvidia_overrides}" ]]; then
        if [[ -n "${SCENARIO_OVERRIDES}" ]]; then
            local merged_dir="${TEST_DIR}/.e2e-tools/overrides_nvidia_$$"
            mkdir -p "${merged_dir}"
            cp -a "${SCENARIO_OVERRIDES}/." "${merged_dir}/" 2>/dev/null || true
            cp -a "${nvidia_overrides}/." "${merged_dir}/"
            SCENARIO_OVERRIDES="${merged_dir}"
            log_info "NVIDIA overrides merged into scenario overrides: ${SCENARIO_OVERRIDES}"
        else
            SCENARIO_OVERRIDES="${nvidia_overrides}"
            log_info "NVIDIA overrides enabled: ${SCENARIO_OVERRIDES}"
        fi
    else
        log_warning "NVIDIA overrides directory not found: ${nvidia_overrides}"
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

    # If --all specified, run all scenarios and exit
    if [[ "${RUN_ALL_SCENARIOS}" == true ]]; then
        run_all_scenarios
        exit $?
    fi

    # Check dependencies before proceeding
    check_dependencies

    # Check for scenario
    if [[ -n "${SCENARIO}" ]]; then
        load_scenario "${SCENARIO}"
    fi
    if [[ "${SCENARIO_SKIPPED}" == "true" ]]; then
        log_info "Scenario skipped: ${SCENARIO_SKIP_REASON}"
        exit 2
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
    CLEANUP_DONE=false

    # Always stop periodic resource logging on exit.
    trap stop_resource_monitoring EXIT
    trap cleanup_on_signal INT TERM

    # Execute test pipeline
    local start_time=$(date +%s)
    local test_result=0

    start_resource_monitoring

    setup_process_priority

    # CRITICAL: Stop real C64 device from streaming to prevent cross-pollution
    # Only send reset for the device test to avoid disrupting the real C64U unnecessarily
    if [[ "${SCENARIO}" == "ntsc_default_avsync_device" ]]; then
        stop_real_c64_streaming
    fi

    configure_nvidia_accel

    if [[ "${SKIP_BUILD}" != true ]]; then
        build_project || exit 1
        install_plugin || exit 1
    else
        log_info "Skipping build step..."
    fi

    # Generate test packets (if not using device)
    if [[ "${PACKET_SOURCE}" != "device" ]]; then
        generate_packets || exit 1
    fi

    if ! run_e2e_test; then
        test_result=1
    fi

    # Run scenario-specific assertions if a scenario was specified
    if [[ -n "${SCENARIO}" && ${test_result} -eq 0 ]]; then
        if ! run_scenario_assertions; then
            test_result=1
        fi
    fi

    # Stop resource monitoring before generating final outputs
    stop_resource_monitoring

    # Clean up real C64 device after device test
    if [[ "${SCENARIO}" == "ntsc_default_avsync_device" ]]; then
        stop_real_c64_streaming
    fi

    # Generate playback.csv before report
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

        # Helper to print validation status
        print_validation_status() {
            local key="$1"
            local label="$2"
            local status=$(jq -r ".${key}.status" "${validation_file}" 2>/dev/null || echo "unknown")
            local details=$(jq -r ".${key}.details" "${validation_file}" 2>/dev/null || echo "")

            case "${status}" in
                "pass") echo "  ✅ ${label}: ${details}" ;;
                "warning") echo "  ⚠️  ${label}: ${details}" ;;
                "fail") echo "  ❌ ${label}: ${details}" ;;
                *) echo "  ❓ ${label}: Status unknown" ;;
            esac
        }

        print_validation_status "udp_reception" "UDP Packet Reception"
        print_validation_status "network_timing" "Network Timing"
        print_validation_status "frame_processing" "Frame Processing"
        print_validation_status "video_recording" "Video Recording"
        print_validation_status "packet_integrity" "Content Integrity"

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

main "$@"
