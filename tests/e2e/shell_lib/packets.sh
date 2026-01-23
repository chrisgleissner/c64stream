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

    if [[ "${PACKET_SOURCE}" == "media" ]]; then
        log_info "Generating ${FORMAT} media source from packets (${FRAMES} frames)..."

        cd "${TEST_DIR}"

        local media_output="${TEST_DIR}/test_packets/c64_media_source.mp4"
        local cmd=(
            "python3" "./util/generate_media_source.py"
            "--frames" "${FRAMES}"
            "--format" "${FORMAT}"
            "--output" "${media_output}"
            "--packet-dir" "test_packets"
            "--scenario" "${SCENARIO_ID:-DEFAULT}"
        )

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
            log_error "Media source generation failed"
            exit 1
        fi

        if [[ ! -f "${media_output}" ]]; then
            log_error "Media source MP4 not found: ${media_output}"
            exit 1
        fi

        local scene_path="${SCENARIO_OVERRIDES}/basic/scenes/C64StreamTest.json"
        if [[ -f "${scene_path}" ]]; then
            python3 - <<PY
from pathlib import Path
scene_path = Path(r"${scene_path}")
media_path = Path(r"${media_output}").resolve()
text = scene_path.read_text(encoding="utf-8")
placeholder = "__C64_E2E_MEDIA_PATH__"
if placeholder in text:
    scene_path.write_text(text.replace(placeholder, str(media_path)), encoding="utf-8")
PY
            log_info "  Media source path injected into scene JSON"
        else
            log_warning "Media source scene JSON not found: ${scene_path}"
        fi

        log_success "Generated media source: ${media_output}"
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
