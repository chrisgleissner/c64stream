#!/bin/bash

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Stop real C64 Ultimate device from streaming to prevent cross-pollution
stop_real_c64_streaming() {
    local c64_host="${C64_DEVICE_HOST:-c64u}"
    local reset_endpoint="/v1/machine:reset"
    local reset_method="PUT"

    # Check if c64u is reachable
    if ! host "${c64_host}" >/dev/null 2>&1 && ! ping -c 1 -W 1 "${c64_host}" >/dev/null 2>&1; then
        if [[ "${VERBOSE:-false}" == true ]]; then
            log_info "Real C64 device (${c64_host}) not reachable - skipping stream stop"
        fi
        return 0
    fi

    # Explicitly stop all streams before resetting
    log_info "Stopping real C64 device streaming to prevent test cross-pollution..."
    if command -v curl &>/dev/null; then
        local streams=("video" "audio" "debug")
        for stream in "${streams[@]}"; do
            local stop_url="http://${c64_host}/v1/streams/${stream}:stop"
            if curl -s -X PUT "${stop_url}" >/dev/null 2>&1; then
                if [[ "${VERBOSE:-false}" == true ]]; then
                    log_success "Stopped ${stream} stream on ${c64_host}"
                fi
            else
                if [[ "${VERBOSE:-false}" == true ]]; then
                    log_warning "Could not stop ${stream} stream - it may not have been running"
                fi
            fi
        done

        # Then reset the machine
        local url="http://${c64_host}${reset_endpoint}"
        if curl -s -X "${reset_method}" "${url}" >/dev/null 2>&1; then
            if [[ "${VERBOSE:-false}" == true ]]; then
                log_success "Reset request sent to ${c64_host}"
            fi
        else
            if [[ "${VERBOSE:-false}" == true ]]; then
                log_warning "Could not reset real C64 device - it may not be running or REST API unavailable"
                log_warning "Continuing anyway, but test may receive real device packets if it's streaming"
            fi
        fi
    fi

    sleep 1
    return 0
}
