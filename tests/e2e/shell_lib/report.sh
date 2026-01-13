#!/bin/bash

# Generate playback.csv from validation_results.json
# This creates a dense, frame-complete timeline showing playback anomalies (skips/repeats)
# Also generates the README.md report now via the unified Python generator
generate_playback_csv() {
    local validation_file="${OUTPUT_DIR}/validation_results.json"

    if [[ ! -f "${validation_file}" ]]; then
        log_info "Skipping playback.csv generation (validation_results.json not found)"
        return 0
    fi

    log_info "Generating playback.csv and test report..."

    # Ensure PROJECT_ROOT is set
    local project_root="${PROJECT_ROOT:-$(pwd)}"
    if [[ ! -d "${project_root}/.git" ]]; then
        # fallback try to find root
        project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
    fi

    # Using the new standalone python report generator
    if python3 "${TEST_DIR}/util/report_generator.py" \
        "${OUTPUT_DIR}" \
        "${SCENARIO_NAME:-Unknown}" \
        "${FORMAT}" \
        "${FRAMES}" \
        "${project_root}"; then

        local playback_csv="${OUTPUT_DIR}/playback.csv"
        if [[ -f "${playback_csv}" ]]; then
            log_success "Generated playback.csv"
        else
            log_warning "Failed to generate playback.csv"
        fi

        local report_file="${OUTPUT_DIR}/README.md"
        if [[ -f "${report_file}" ]]; then
            log_success "Generated README.md"
        else
            log_warning "Failed to generate README.md"
        fi
    else
        log_error "Python report generator failed"
    fi
}

# Generate test report
# Now handled by generate_playback_csv via the unified Python generator
generate_report() {
    # No-op here as it's done in the previous step
    :
}
