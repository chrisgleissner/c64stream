#!/usr/bin/env bash
#
# Real C64U A/V sync test runner.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

HOST="c64u"
REST_SCHEME="http"
RUN_PRG_ENDPOINT="/v1/runners:run_prg"
RESET_ENDPOINT="/v1/machine:reset"
RESET_METHOD="PUT"
REST_TOKEN=""
REST_TOKEN_HEADER="X-Password"
OUTPUT_DIR="${SCRIPT_DIR}/results/real_c64u_av_sync"
DURATION=10
FORMAT="NTSC"
MAX_DELTA_MS=30
P50_MAX_MS=20
P95_MAX_MS=40
MAX_MAX_MS=60
MIN_POP_EVENTS=2
NO_BUILD=false
VIDEO_PORT=11000  # C64 Ultimate default video port (different from synthetic tests)
AUDIO_PORT=11001  # C64 Ultimate default audio port (different from synthetic tests)
CONTROL_PORT=64
ANALYZE_ONLY=""
OBS_CSV=""
NETWORK_CSV=""
OBS_LOG=""
VERBOSE=false
NO_MP4_ANALYSIS=false
OBS_ONLY=false

usage() {
    cat <<'EOF'
Real C64U A/V sync test runner

Usage:
    real-device-av-sync.sh [--host <name|ip>] [options]
  real-device-av-sync.sh --analyze-only <path> [options]
  real-device-av-sync.sh --obs-csv <path> [--network-csv <path>] [--obs-log <path>] [options]

Default (full run):
    --host <name|ip>           C64 Ultimate hostname or IP (default: c64u)

Options:
  --duration <sec>           Recording duration (default: 10)
    --format <PAL|NTSC>        OBS output format (default: NTSC)
  --output-dir <dir>         Output base dir (default: tests/e2e/results/real_c64u_av_sync)
    --max-delta-ms <ms>        Legacy max allowed A/V delta (default: 30)
    --p50-max-ms <ms>          Max allowed p50 A/V delta (default: 20)
    --p95-max-ms <ms>          Max allowed p95 A/V delta (default: 40)
    --max-max-ms <ms>          Max allowed max A/V delta (default: 60)
  --min-pop-events <n>       Minimum pop events required (default: 2)
  --video-port <port>        Video UDP port (default: 11000 - C64U native default)
                             NOTE: Synthetic tests use 21000 for port isolation
  --audio-port <port>        Audio UDP port (default: 11001 - C64U native default)
                             NOTE: Synthetic tests use 21001 for port isolation
  --control-port <port>      Control TCP port (default: 64)
  --rest-scheme <scheme>     REST scheme (default: http)
  --run-prg-endpoint <path>  REST endpoint for PRG upload+run (default: /v1/runners:run_prg)
    --reset-endpoint <path>    REST endpoint to reset the device (default: /v1/machine:reset)
    --reset-method <method>    HTTP method for reset endpoint (default: PUT)
  --rest-token <token>       Optional REST auth token
  --rest-token-header <hdr>  Header name for REST token (default: X-Password)
  --no-build                 Skip PRG build (use existing av-sync-auto.prg)
  --obs-only                 Skip PRG build/run, only do OBS recording (assumes C64U already running av-sync-auto.prg)
  --no-mp4-analysis          Skip MP4 recording analysis (only analyze CSV/logs)
  --analyze-only <path>      Analyze a log/CSV directory or file (no OBS run)
  --obs-csv <path>           Analyze obs.csv directly (no OBS run)
  --network-csv <path>       Analyze network.csv directly (no OBS run)
  --obs-log <path>           Analyze obs.log directly (no OBS run)
  --verbose                  Verbose logs
  --help                     Show this help

Examples:
    # Full run: build PRG, run on C64U, record with OBS, analyze CSV/logs/MP4
    ./tests/e2e/real-device-av-sync.sh

    # Connect to specific host
    ./tests/e2e/real-device-av-sync.sh --host 192.168.1.13

    # Custom duration and thresholds
    ./tests/e2e/real-device-av-sync.sh --host c64u.local --duration 15 --max-delta-ms 25

  # OBS-only mode (PRG already running on C64U)
  ./tests/e2e/real-device-av-sync.sh --obs-only --duration 20

  # Skip MP4 analysis (faster, only CSV/log analysis)
  ./tests/e2e/real-device-av-sync.sh --no-mp4-analysis

  # Analyze existing session results
  ./tests/e2e/real-device-av-sync.sh --analyze-only /path/to/results/session_20250209_120000

  # Analyze specific log file
  ./tests/e2e/real-device-av-sync.sh --obs-log /path/to/obs.log --max-delta-ms 30
EOF
}

log() {
    echo "[real-device-av-sync] $*"
}

run_analyzer() {
    local args=(
        "--max-delta-ms" "${MAX_DELTA_MS}"
        "--p50-max-ms" "${P50_MAX_MS}"
        "--p95-max-ms" "${P95_MAX_MS}"
        "--max-max-ms" "${MAX_MAX_MS}"
        "--min-pop-events" "${MIN_POP_EVENTS}"
    )
    if [[ "${VERBOSE}" == "true" ]]; then
        args+=("--verbose")
    fi

    if [[ -n "${ANALYZE_ONLY}" ]]; then
        if [[ -d "${ANALYZE_ONLY}" ]]; then
            args+=("--input-dir" "${ANALYZE_ONLY}")
        elif [[ -f "${ANALYZE_ONLY}" ]]; then
            case "${ANALYZE_ONLY}" in
                *.csv)
                    if [[ "${ANALYZE_ONLY}" == *network* ]]; then
                        args+=("--network-csv" "${ANALYZE_ONLY}")
                    else
                        args+=("--obs-csv" "${ANALYZE_ONLY}")
                    fi
                    ;;
                *.log|*.txt)
                    args+=("--obs-log" "${ANALYZE_ONLY}")
                    ;;
                *)
                    args+=("--input-dir" "${ANALYZE_ONLY}")
                    ;;
            esac
        fi
    fi

    if [[ -n "${OBS_CSV}" ]]; then
        args+=("--obs-csv" "${OBS_CSV}")
    fi
    if [[ -n "${NETWORK_CSV}" ]]; then
        args+=("--network-csv" "${NETWORK_CSV}")
    fi
    if [[ -n "${OBS_LOG}" ]]; then
        args+=("--obs-log" "${OBS_LOG}")
    fi

    python3 "${SCRIPT_DIR}/av_pop_analyzer.py" "${args[@]}"
}

run_prg() {
    local prg_path="$1"
    local base_url="${REST_SCHEME}://${HOST}"
    local url="${base_url}${RUN_PRG_ENDPOINT}"
    local curl_args=(
        --fail
        --silent
        --show-error
        --connect-timeout 5
        --max-time 20
        -X POST
        -F "file=@${prg_path}"
        "${url}"
    )

    if [[ -n "${REST_TOKEN}" ]]; then
        curl_args+=(-H "${REST_TOKEN_HEADER}: ${REST_TOKEN}")
    fi

    log "Starting PRG via REST: ${url}"
    if ! curl "${curl_args[@]}"; then
        log "REST call failed. Check host/endpoint and auth."
        return 1
    fi
    log "PRG started successfully."
}

stop_streams() {
    local base_url="${REST_SCHEME}://${HOST}"
    local streams=("video" "audio" "debug")

    for stream in "${streams[@]}"; do
        local url="${base_url}/v1/streams/${stream}:stop"
        local curl_args=(
            --fail
            --silent
            --show-error
            --connect-timeout 5
            --max-time 10
            -X PUT
            "${url}"
        )

        if [[ -n "${REST_TOKEN}" ]]; then
            curl_args+=(-H "${REST_TOKEN_HEADER}: ${REST_TOKEN}")
        fi

        log "Stopping ${stream} stream: PUT ${url}"
        if ! curl "${curl_args[@]}"; then
            log "Warning: Failed to stop ${stream} stream. It may not have been running."
        else
            log "${stream} stream stopped successfully."
        fi
    done
}

reset_device() {
    local base_url="${REST_SCHEME}://${HOST}"
    local url="${base_url}${RESET_ENDPOINT}"
    local curl_args=(
        --fail
        --silent
        --show-error
        --connect-timeout 5
        --max-time 20
        -X "${RESET_METHOD}"
        "${url}"
    )

    if [[ -n "${REST_TOKEN}" ]]; then
        curl_args+=(-H "${REST_TOKEN_HEADER}: ${REST_TOKEN}")
    fi

    log "Resetting device via REST: ${RESET_METHOD} ${url}"
    if ! curl "${curl_args[@]}"; then
        log "Device reset call failed. Check endpoint/method/auth."
        return 1
    fi
    log "Device reset request sent."
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)
            HOST="$2"
            shift 2
            ;;
        --duration)
            DURATION="$2"
            shift 2
            ;;
        --format)
            FORMAT="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --max-delta-ms)
            MAX_DELTA_MS="$2"
            shift 2
            ;;
        --p50-max-ms)
            P50_MAX_MS="$2"
            shift 2
            ;;
        --p95-max-ms)
            P95_MAX_MS="$2"
            shift 2
            ;;
        --max-max-ms)
            MAX_MAX_MS="$2"
            shift 2
            ;;
        --min-pop-events)
            MIN_POP_EVENTS="$2"
            shift 2
            ;;
        --video-port)
            VIDEO_PORT="$2"
            shift 2
            ;;
        --audio-port)
            AUDIO_PORT="$2"
            shift 2
            ;;
        --control-port)
            CONTROL_PORT="$2"
            shift 2
            ;;
        --rest-scheme)
            REST_SCHEME="$2"
            shift 2
            ;;
        --run-prg-endpoint)
            RUN_PRG_ENDPOINT="$2"
            shift 2
            ;;
        --reset-endpoint)
            RESET_ENDPOINT="$2"
            shift 2
            ;;
        --reset-method)
            RESET_METHOD="$2"
            shift 2
            ;;
        --rest-token)
            REST_TOKEN="$2"
            shift 2
            ;;
        --rest-token-header)
            REST_TOKEN_HEADER="$2"
            shift 2
            ;;
        --no-build)
            NO_BUILD=true
            shift
            ;;
        --obs-only)
            OBS_ONLY=true
            shift
            ;;
        --no-mp4-analysis)
            NO_MP4_ANALYSIS=true
            shift
            ;;
        --analyze-only)
            ANALYZE_ONLY="$2"
            shift 2
            ;;
        --obs-csv)
            OBS_CSV="$2"
            shift 2
            ;;
        --network-csv)
            NETWORK_CSV="$2"
            shift 2
            ;;
        --obs-log)
            OBS_LOG="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            log "Unknown argument: $1"
            usage
            exit 1
            ;;
    esac
done

if [[ -n "${ANALYZE_ONLY}" || -n "${OBS_CSV}" || -n "${NETWORK_CSV}" || -n "${OBS_LOG}" ]]; then
    run_analyzer
    exit $?
fi

if [[ -z "${HOST}" ]]; then
    HOST="c64u"
fi

# Only reset device at end if we actually ran the PRG (not in OBS-only mode)
if [[ "${OBS_ONLY}" != "true" ]]; then
    trap 'stop_streams || true; reset_device || true' EXIT
fi

# Build and run PRG unless in OBS-only mode
if [[ "${OBS_ONLY}" != "true" ]]; then
    if [[ "${NO_BUILD}" != "true" ]]; then
        log "Building av-sync-auto.prg..."
        "${ROOT_DIR}/tools/c64/c64-build.sh" "${ROOT_DIR}/tools/c64/av-sync-auto.asm"
    fi

    PRG_PATH="${ROOT_DIR}/tools/c64/av-sync-auto.prg"
    if [[ ! -f "${PRG_PATH}" ]]; then
        log "PRG not found: ${PRG_PATH}"
        exit 1
    fi

    run_prg "${PRG_PATH}"
else
    log "Skipping PRG build/run (OBS-only mode). Assuming av-sync-auto.prg already running on C64U."
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
session_dir="${OUTPUT_DIR}/session_${timestamp}"
mkdir -p "${session_dir}"

log "Running OBS capture (duration: ${DURATION}s)"
python3 "${SCRIPT_DIR}/real_device_av_sync.py" \
    --host "${HOST}" \
    --duration "${DURATION}" \
    --format "${FORMAT}" \
    --output-dir "${session_dir}" \
    --max-delta-ms "${MAX_DELTA_MS}" \
    --p50-max-ms "${P50_MAX_MS}" \
    --p95-max-ms "${P95_MAX_MS}" \
    --max-max-ms "${MAX_MAX_MS}" \
    --min-pop-events "${MIN_POP_EVENTS}" \
    --video-port "${VIDEO_PORT}" \
    --audio-port "${AUDIO_PORT}" \
    --control-port "${CONTROL_PORT}" \
    $( [[ "${VERBOSE}" == "true" ]] && echo "--verbose" ) \
    $( [[ "${NO_MP4_ANALYSIS}" == "true" ]] && echo "--no-mp4-analysis" )

log "Done. Results: ${session_dir}"
