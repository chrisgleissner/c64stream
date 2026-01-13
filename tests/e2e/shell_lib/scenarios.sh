#!/bin/bash

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
    local name format preset pattern full_frame_pop csv_max_rows packet_source
    name=$(grep -m1 "^name:" "${scenario_yaml}" | sed 's/^name: *//' || true)
    format=$(grep -m1 "^format:" "${scenario_yaml}" | sed 's/^format: *//' || true)
    preset=$(grep -m1 "^preset:" "${scenario_yaml}" | sed 's/^preset: *//' || true)
    pattern=$(grep -m1 "^pattern:" "${scenario_yaml}" | sed 's/^pattern: *//' || true)
    full_frame_pop=$(grep -m1 "^full_frame_pop:" "${scenario_yaml}" | sed 's/^full_frame_pop: *//' || true)
    csv_max_rows=$(grep -m1 "^csv_max_rows:" "${scenario_yaml}" | sed 's/^csv_max_rows: *//' || true)
    packet_source=$(grep -m1 "^packet_source:" "${scenario_yaml}" | sed 's/^packet_source: *//' || true)

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

    if [[ "${packet_source}" == "device" ]]; then
        if [[ "${E2E_DEVICE_TESTS:-0}" != "1" ]]; then
            SCENARIO_SKIPPED=true
            SCENARIO_SKIP_REASON="Device tests disabled (set E2E_DEVICE_TESTS=1 to run)"
            log_warning "⏭️  Skipping scenario '${name}' (device packet source)"
            log_info "  Reason: ${SCENARIO_SKIP_REASON}"
            return 0
        fi
        local c64_host
        c64_host=$(grep -m1 "^[[:space:]]*c64_host:" "${scenario_yaml}" | sed 's/^[[:space:]]*c64_host: *//' || true)
        c64_host="${c64_host:-c64u}"
        local c64_port
        c64_port=$(grep -m1 "^[[:space:]]*control_port:" "${scenario_yaml}" | sed 's/^[[:space:]]*control_port: *//' || true)
        c64_port="${c64_port:-64}"
        if ! python3 - <<PY
import socket
host = "${c64_host}"
port = int("${c64_port}")
sock = socket.socket()
sock.settimeout(1.0)
try:
    sock.connect((host, port))
except Exception:
    raise SystemExit(1)
finally:
    sock.close()
raise SystemExit(0)
PY
        then
            SCENARIO_SKIPPED=true
            SCENARIO_SKIP_REASON="Real C64 Ultimate device (${c64_host}:${c64_port}) not reachable"
            log_warning "⏭️  Skipping scenario '${name}' (device packet source)"
            log_info "  Reason: ${SCENARIO_SKIP_REASON}"
            return 0
        fi
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
    if [[ -n "${packet_source}" ]]; then
        PACKET_SOURCE="${packet_source}"
        log_info "  Packet source: ${PACKET_SOURCE}"
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
        python3 "${TEST_DIR}/util/scenario_loader.py" --scenario "${scenario_name}" \
            --output "${generated_dir}/basic/scenes/C64StreamTest.json"
    else
        python3 "${TEST_DIR}/util/scenario_loader.py" --scenario "${scenario_name}" \
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
