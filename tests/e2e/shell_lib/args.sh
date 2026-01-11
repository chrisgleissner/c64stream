#!/bin/bash

# Default test parameters
DEFAULT_FORMAT="NTSC"
DEFAULT_FRAMES=300
DEFAULT_DURATION=8
DEFAULT_VIDEO_PORT=21000
DEFAULT_AUDIO_PORT=21001
DEFAULT_VERBOSE=false
DEFAULT_SKIP_BUILD=false
DEFAULT_CLEANUP=true
DEFAULT_OBS_ENABLED=true
DEFAULT_X11_DISPLAY="${DEFAULT_X11_DISPLAY:-:99}"
DEFAULT_MONITOR_RESOURCES=true
DEFAULT_SCENARIO_OVERRIDES=""
DEFAULT_SCENARIO_NAME=""
DEFAULT_PACKET_PATTERN=""
DEFAULT_FULL_FRAME_POP=false
DEFAULT_SCENARIO=""
DEFAULT_CSV_MAX_ROWS=2000
SCENARIO_CI_SKIPPED=false
SCENARIO_YAML_PATH=""
DEFAULT_RUN_ALL_SCENARIOS=false
DEFAULT_ENABLE_RESOURCE_MONITORING=true
DEFAULT_RESOURCE_INTERVAL_MS=500
DEFAULT_DISABLE_POPS=false
DEFAULT_SETTLING_SECONDS=0
DEFAULT_PACKET_SOURCE="mock"
DEFAULT_PERF_PROFILE=false
DEFAULT_PERF_FLAMEGRAPH=false
DEFAULT_PERF_FREQUENCY_HZ=99
DEFAULT_PERF_CALLGRAPH="fp"
DEFAULT_PERF_DURATION=""
DEFAULT_OUTPUT_DIR="${TEST_DIR}/results"


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
    python3 "${TEST_DIR}/util/scenario_loader.py" --list 2>/dev/null || {
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
    PACKET_SOURCE="${DEFAULT_PACKET_SOURCE}"

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
