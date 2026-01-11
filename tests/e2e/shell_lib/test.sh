#!/bin/bash

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
        # fall back to the repo copy under util/.
        udp_replay_path="./util/udp_replay"
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

    # Packet-source behavior (mock vs device) is scenario-defined.
    if [[ -n "${PACKET_SOURCE}" ]]; then
        cmd+=("--packet-source" "${PACKET_SOURCE}")
    fi

    # Make full-frame-pop behavior explicit in the Python harness (avoid scenario-id special casing).
    if [[ "${FULL_FRAME_POP}" == true ]]; then
        cmd+=("--full-frame-pop")
    fi

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
