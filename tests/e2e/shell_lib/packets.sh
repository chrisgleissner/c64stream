#!/bin/bash

# Generate test packets
generate_packets() {
    if [[ "${PACKET_SOURCE}" == "device" ]]; then
        # Device scenarios use a real C64U stream; pre-generated packets are not used and would
        # mislead validation/reporting if left around from previous runs.
        cd "${TEST_DIR}"
        rm -rf test_packets
        log_info "Skipping packet generation (packet_source=device)"
        return 0
    fi

    log_info "Generating ${FORMAT} test packets (${FRAMES} frames)..."

    cd "${TEST_DIR}"

    # Create output directory
    rm -rf test_packets
    mkdir -p test_packets

    # Generate packets
    local cmd=(
        "./util/generate_packets.py"
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
