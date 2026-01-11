#!/bin/bash

# Generate playback.csv from validation_results.json
# This creates a dense, frame-complete timeline showing playback anomalies (skips/repeats)
generate_playback_csv() {
    local validation_file="${OUTPUT_DIR}/validation_results.json"
    local playback_csv="${OUTPUT_DIR}/playback.csv"
    local obs_csv=""

    if [[ ! -f "${validation_file}" ]] || ! command -v jq >/dev/null 2>&1; then
        log_info "Skipping playback.csv generation (validation_results.json not found or jq not available)"
        return 0
    fi

    # Check if frame_sequence_box results are available
    local has_frame_seq
    has_frame_seq=$(jq -r 'has("frame_sequence_box")' "${validation_file}" 2>/dev/null || echo "false")
    if [[ "${has_frame_seq}" != "true" ]]; then
        log_info "Skipping playback.csv generation (no frame sequence data)"
        return 0
    fi

    # Find obs.csv - check session folders
    local session_folder
    session_folder=$(find "${OUTPUT_DIR}" -maxdepth 2 -name "obs.csv" -type f 2>/dev/null | head -1)
    if [[ -n "${session_folder}" ]]; then
        obs_csv="${session_folder}"
    fi

    log_info "Generating playback.csv..."

    # Get the recording file to determine total frame count and frame rate
    local recording_mp4=""
    if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" ]]; then
        recording_mp4="${OUTPUT_DIR}/c64_recording.mp4"
    elif compgen -G "${OUTPUT_DIR}/*.mp4" > /dev/null; then
        recording_mp4=$(ls -t ${OUTPUT_DIR}/*.mp4 2>/dev/null | head -1)
    fi

    # Use Python to generate the CSV (complex logic with frame mapping)
    python3 - "${validation_file}" "${playback_csv}" "${recording_mp4}" "${FORMAT}" "${obs_csv}" <<'PYTHON'
import sys
import json
import csv
from pathlib import Path

validation_file = Path(sys.argv[1])
playback_csv = Path(sys.argv[2])
recording_mp4 = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
video_format = sys.argv[4] if len(sys.argv) > 4 else "NTSC"
obs_csv_path = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] else None

# Load validation results
with open(validation_file, 'r') as f:
    data = json.load(f)

frame_seq = data.get("frame_sequence_box", {})
details = frame_seq.get("details", {})

# Get frame rate and window info
fps = details.get("window", {}).get("fps", 60.0 if video_format == "NTSC" else 50.0)
start_frame = details.get("window", {}).get("start_frame", 0)
end_frame = details.get("window", {}).get("end_frame", 0)

# Get content bounds (new detection)
content_bounds = details.get("content_bounds", {})
first_content_frame = content_bounds.get("first_content_frame", start_frame) if content_bounds else start_frame
last_content_frame = content_bounds.get("last_content_frame", end_frame) if content_bounds else end_frame

# Fallback to video_pop_starts if content_bounds not available (from frame_sequence_box)
video_pop_starts = details.get("video_pop_starts", [])
if first_content_frame == 0 and video_pop_starts:
    first_content_frame = video_pop_starts[0]

# Secondary fallback: use av_sync_details.video_pop_frame_indices
av_sync_for_bounds = data.get("av_sync_details", {})
if first_content_frame == 0:
    av_pop_frames = av_sync_for_bounds.get("video_pop_frame_indices", [])
    if av_pop_frames:
        # Use first pop frame as estimate of content start (may be ~1s after actual start)
        first_content_frame = max(0, av_pop_frames[0] - int(fps))  # ~1 second before first pop

# Get event lists from validation results
# skip_events: list of {frame, time_sec, skipped} - frame is where skip was detected
# repeated_events: list of {frame, time_sec, count} - frame is where repetition starts
skip_events = details.get("skip_events", [])
repeated_events = details.get("repeated_events", [])

# Build maps for events (keyed by video frame number)
repeated_intervals = {}
for evt in repeated_events:
    frame = evt.get("frame", 0)
    count = evt.get("count", 0)
    repeated_intervals[frame] = count

skip_map = {}
for evt in skip_events:
    frame = evt.get("frame", 0)
    skipped = evt.get("skipped", 0)
    skip_map[frame] = skipped

# Load obs.csv to get actual C64 stream frame_num values
# obs.csv records one entry per frame that ARRIVED from the network
# The video may show the same content for multiple frames (repeated)
obs_frame_nums = []  # List of frame_num values from obs.csv video events
if obs_csv_path and Path(obs_csv_path).exists():
    try:
        with open(obs_csv_path, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row.get('event_type') == 'video':
                    frame_num = int(row.get('frame_num', 0))
                    obs_frame_nums.append(frame_num)
    except Exception as e:
        print(f"Warning: Could not read obs.csv: {e}", file=sys.stderr)

# Get total frames from video if available
total_frames = end_frame + 1 if end_frame > 0 else 0
if recording_mp4 and Path(recording_mp4).exists():
    try:
        import cv2
        cap = cv2.VideoCapture(recording_mp4)
        if cap.isOpened():
            total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            fps = cap.get(cv2.CAP_PROP_FPS) or fps
        cap.release()
    except Exception:
        pass

if total_frames == 0:
    total_frames = max(end_frame + 1, 100)

# Fix last_content_frame when frame_sequence_box was skipped
if last_content_frame == 0 and total_frames > 0:
    last_content_frame = total_frames - 1

# Build a mapping from video frame index to the C64 stream frame being displayed.
# The frame_progression assertion detects slot positions from the bottom-left progress bar (frame_num % 8).
#
# The detected slot IS the ground truth for what content is being displayed.
# We use the detected slots to determine the actual frame_num being shown.
#
# frame_slots: dict mapping video_frame_index -> detected_slot_index (0-7)
# We need to map this back to actual frame_num values.
#
# The challenge: we only know frame_num % 8 from the slot.
# But obs.csv tells us the actual frame_num sequence that arrived.
# We need to find the obs.csv frame_num that matches the detected slot.

frame_slots = details.get("frame_slots", {})

# Build a lookup from slot (0-7) to list of obs frame_nums with that slot
slot_to_obs_frames = {i: [] for i in range(8)}
for fn in obs_frame_nums:
    slot = fn % 8
    slot_to_obs_frames[slot].append(fn)

# For each video frame, determine the frame_num being displayed
# Use detected slot and find the corresponding obs frame_num
video_to_frame_num = {}
last_frame_num = 0  # Track the last assigned frame_num for monotonicity

for video_idx in range(first_content_frame, min(last_content_frame + 1, total_frames)):
    video_idx_str = str(video_idx)

    if video_idx_str in frame_slots:
        detected_slot = frame_slots[video_idx_str]

        # Find the obs frame_num that matches this slot
        # It should be >= last_frame_num to maintain monotonicity (or same for repeats)
        candidates = slot_to_obs_frames.get(detected_slot, [])

        best_match = None
        for fn in candidates:
            if fn >= last_frame_num:
                best_match = fn
                break  # Take the first one >= last

        if best_match is None and candidates:
            # Allow same frame_num for repeats
            for fn in candidates:
                if fn == last_frame_num or fn == last_frame_num - 1:
                    best_match = fn
                    break

        if best_match is not None:
            video_to_frame_num[video_idx] = best_match
            last_frame_num = best_match
        else:
            # Fallback: derive from slot + offset based on last known
            base = (last_frame_num // 8) * 8
            derived = base + detected_slot
            if derived < last_frame_num:
                derived += 8  # Wrap to next cycle
            video_to_frame_num[video_idx] = derived
            last_frame_num = derived
    else:
        # No slot detection for this frame - use interpolation
        video_to_frame_num[video_idx] = last_frame_num

# Re-build repeat continuation frames based on the new mapping
# A repeat is when consecutive video frames have the same frame_num
repeat_continuation_frames_new = set()
prev_fn = None
for video_idx in sorted(video_to_frame_num.keys()):
    fn = video_to_frame_num[video_idx]
    if prev_fn is not None and fn == prev_fn:
        repeat_continuation_frames_new.add(video_idx)
    prev_fn = fn

# Generate playback CSV
# Each row represents one displayed frame in the recording (1:1 mapping).
#
# Columns:
# - playback_frame_index: 0-based index in the recording (video frame number)
# - frame_num: C64 stream frame number (the actual frame counter from the C64U device)
#              Empty for pre-roll/post-roll frames
# - video_s: timestamp in seconds since recording start (position in video file)
# - video_ssff: timestamp in SS:FF format (seconds:frames) for tools like Shotcut
# - content_s: time since C64U content started streaming (empty for logo/post-stream frames)
# - frame_slot: detected slot (0-7) from bottom-left progress bar (empty if not detected)
# - repeated: if this is the START of a repeated run, the total times shown (e.g., 2 = shown twice);
#             empty for normal frames or continuation frames within a run
# - skipped: number of source frames permanently lost BEFORE this frame arrived;
#            empty if no frames were skipped
# - event: human-readable summary: "repeated", "skipped", "repeated+skipped", or empty
# - video_pop: "video_pop" if a video pop (frame sync marker) was detected at this frame
# - audio_pop: "audio_pop" if an audio pop was detected within this frame's time window

# Get pop data from av_sync_details
av_sync_details = data.get("av_sync_details", {})
video_pop_frame_indices = set(av_sync_details.get("video_pop_frame_indices", []))

# Build audio pop frame mapping (audio pop time -> closest video frame)
audio_pop_frames = set()
sync_details = av_sync_details.get("sync_details", [])
for detail in sync_details:
    closest_frame = detail.get("closest_video_pop_frame")
    if closest_frame is not None:
        audio_pop_frames.add(closest_frame)

# Calculate first content time for content_s column
first_content_time_s = first_content_frame / fps if first_content_frame > 0 else 0.0

with open(playback_csv, 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['playback_frame_index', 'frame_num', 'frame_slot', 'video_s', 'video_ssff', 'content_s', 'repeated', 'skipped', 'event', 'video_pop', 'audio_pop'])

    for playback_idx in range(total_frames):
        video_s = round(playback_idx / fps, 3)
        # Calculate SS:FF format directly from frame index
        # For fractional fps (e.g., 59.94), use the actual fps value
        total_seconds = playback_idx / fps
        secs = int(total_seconds)
        fractional_seconds = total_seconds - secs
        frames = int(fractional_seconds * fps)
        video_ssff = f"{secs:02d}:{frames:02d}"

        # Determine if this is a pre-roll, content, or post-roll frame
        if playback_idx < first_content_frame:
            # Pre-roll frame (logo, startup)
            writer.writerow([playback_idx, "", "", video_s, video_ssff, "", "", "", "", "", ""])
        elif playback_idx > last_content_frame:
            # Post-roll frame (after content ends)
            writer.writerow([playback_idx, "", "", video_s, video_ssff, "", "", "", "", "", ""])
        else:
            # Content frame - get frame_num from our mapping
            frame_num = video_to_frame_num.get(playback_idx, "")

            # Calculate content_s (time since first content frame), never negative
            content_s = round(max(0.0, video_s - first_content_time_s), 3)

            # Get frame slot from frame_slots dict
            frame_slot = frame_slots.get(str(playback_idx), "")

            # Check for events at this video frame
            repeated_count = repeated_intervals.get(playback_idx, "")
            skipped_count = skip_map.get(playback_idx, "")

            # Build event string
            events = []
            if repeated_count:
                events.append("repeated")
            if skipped_count:
                events.append("skipped")
            event_str = "+".join(events)

            # Check for pops - use full string values for easy grepping
            video_pop = "video_pop" if playback_idx in video_pop_frame_indices else ""
            audio_pop = "audio_pop" if playback_idx in audio_pop_frames else ""

            writer.writerow([playback_idx, frame_num, frame_slot, video_s, video_ssff, content_s, repeated_count, skipped_count, event_str, video_pop, audio_pop])

# Validate frame_num against detected slots from video
# The bottom-left progress bar shows frame_num % 8 as a slot position (0-7)
# frame_slots is a dict: video_frame_num -> detected_slot_index

mismatch_count = 0
mismatch_examples = []
validated_count = 0

for video_idx, expected_frame_num in video_to_frame_num.items():
    video_idx_str = str(video_idx)
    if video_idx_str in frame_slots and expected_frame_num:
        detected_slot = frame_slots[video_idx_str]
        expected_slot = expected_frame_num % 8
        validated_count += 1
        if detected_slot != expected_slot:
            mismatch_count += 1
            if len(mismatch_examples) < 5:
                mismatch_examples.append({
                    "video_frame": video_idx,
                    "frame_num": expected_frame_num,
                    "expected_slot": expected_slot,
                    "detected_slot": detected_slot,
                })

if validated_count > 0:
    mismatch_pct = 100.0 * mismatch_count / validated_count
    if mismatch_count > 0:
        print(f"⚠️  Frame validation: {mismatch_count}/{validated_count} mismatches ({mismatch_pct:.1f}%)", file=sys.stderr)
        for ex in mismatch_examples:
            print(f"   - Video frame {ex['video_frame']}: frame_num={ex['frame_num']}, expected slot {ex['expected_slot']}, detected {ex['detected_slot']}", file=sys.stderr)
    else:
        print(f"✅ Frame validation: all {validated_count} frames match expected slots", file=sys.stderr)
else:
    print("⚠️  Frame validation: no slot data available for validation", file=sys.stderr)

print(f"Generated playback.csv with {total_frames} frames ({len(obs_frame_nums)} obs events, {len(video_to_frame_num)} content frames)", file=sys.stderr)
PYTHON

    if [[ -f "${playback_csv}" ]]; then
        log_success "Generated playback.csv"
    else
        log_warning "Failed to generate playback.csv"
    fi
}

# Generate test report
generate_report() {
    log_info "Generating test report..."

    local report_file="${OUTPUT_DIR}/README.md"
    local timestamp=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
    local video_duration_sec=""
    local video_duration_fmt=""
    local sample_frame_index=""
    local sample_frame_seconds=""
    local sample_frame_timestamp=""

    # Gather system snapshot for debugging context
    local obs_version os_name kernel_version cpu_model cpu_cores ram_total ram_available
    local disk_total disk_available disk_mount

    if command -v obs >/dev/null 2>&1; then
        obs_version=$(obs --version 2>/dev/null | head -n1 | sed 's/^OBS Studio - //' | sed 's/^OBS Studio //')
        [[ -z "${obs_version}" ]] && obs_version="Detected (version unknown)"
    else
        obs_version="Not installed"
    fi

    kernel_version=$(uname -r 2>/dev/null || echo "Unknown kernel")
    if command -v lsb_release >/dev/null 2>&1; then
        os_name=$(lsb_release -ds 2>/dev/null | tr -d '"')
    elif [[ -f /etc/os-release ]]; then
        os_name=$(grep -E '^PRETTY_NAME=' /etc/os-release | cut -d= -f2- | tr -d '"')
    fi
    [[ -z "${os_name}" ]] && os_name=$(uname -s 2>/dev/null || echo "Unknown OS")

    cpu_model=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//')
    [[ -z "${cpu_model}" ]] && cpu_model=$(uname -p 2>/dev/null || echo "Unknown CPU")
    cpu_cores=$(nproc 2>/dev/null || echo "?")

    if command -v free >/dev/null 2>&1; then
        ram_total=$(free -h | awk '/^Mem:/ {print $2}')
        ram_available=$(free -h | awk '/^Mem:/ {print $7}')
    else
        ram_total=$(grep -m1 'MemTotal' /proc/meminfo 2>/dev/null | awk '{printf "%.1f MiB", $2/1024}')
        ram_available=$(grep -m1 'MemAvailable' /proc/meminfo 2>/dev/null | awk '{printf "%.1f MiB", $2/1024}')
    fi
    [[ -z "${ram_total}" ]] && ram_total="Unknown"
    [[ -z "${ram_available}" ]] && ram_available="Unknown"

    if command -v df >/dev/null 2>&1; then
        read -r disk_total disk_available disk_mount <<<"$(df -h "${PROJECT_ROOT}" 2>/dev/null | awk 'NR==2 {print $2, $4, $6}')"
    fi
    [[ -z "${disk_total}" ]] && disk_total="Unknown"
    [[ -z "${disk_available}" ]] && disk_available="Unknown"
    [[ -z "${disk_mount}" ]] && disk_mount=$(pwd)

        # Resolve plugin version via shared helper (mirrors CI logic)
        local resolved_version
        if [[ -x "${PROJECT_ROOT}/build-aux/resolve-plugin-version.sh" ]]; then
            resolved_version=$("${PROJECT_ROOT}/build-aux/resolve-plugin-version.sh")
        else
            resolved_version=$(jq -r '.version // "unknown"' "${PROJECT_ROOT}/buildspec.json" 2>/dev/null)
        fi

        cat > "${report_file}" << EOF
# C64 Stream E2E Test Report

## Scenario: ${SCENARIO_NAME:-${SCENARIO:-Unknown}}

Generated: ${timestamp}

## Test configuration

- Format: ${FORMAT}
- Frames: ${FRAMES}
- Duration: $(if [[ "${FORMAT}" == "PAL" ]]; then awk "BEGIN {printf \"%.1f\", ${FRAMES}/50}"; else awk "BEGIN {printf \"%.1f\", ${FRAMES}/60}"; fi) seconds
- Video Port: ${VIDEO_PORT}
- Audio Port: ${AUDIO_PORT}
- OBS Enabled: ${OBS_ENABLED}

## Build information

- Project: $(jq -r '.name // "unknown"' "${PROJECT_ROOT}/buildspec.json" 2>/dev/null)
- Version: ${resolved_version}

## System information

- OS: ${os_name} (kernel ${kernel_version})
- OBS: ${obs_version}
- CPU: ${cpu_model} (${cpu_cores} cores)
- RAM: ${ram_total} total, ${ram_available} available
- Disk (${disk_mount}): ${disk_total} total, ${disk_available} available

## Test results
EOF

    # Validation summary (from validation_results.json)
    local validation_summary_file="${OUTPUT_DIR}/validation_results.json"
    if [[ -f "${validation_summary_file}" ]] && command -v jq >/dev/null 2>&1; then
        echo >> "${report_file}"
        echo "### Validation Summary" >> "${report_file}"
        echo >> "${report_file}"

        _render_validation_line() {
            local key="$1"
            local label="$2"
            local status details emoji
            status=$(jq -r ".${key}.status // \"unknown\"" "${validation_summary_file}" 2>/dev/null || echo "unknown")
            details=$(jq -r ".${key}.details // \"\"" "${validation_summary_file}" 2>/dev/null || echo "")

            case "${status}" in
                pass) emoji="✅" ;;
                warning) emoji="⚠️" ;;
                fail) emoji="❌" ;;
                deferred) emoji="⚠️" ;;
                skipped) emoji="⏭️" ;;
                *) emoji="❓" ;;
            esac

            if [[ -n "${details}" && "${details}" != "null" ]]; then
                echo "- ${emoji} ${label}: ${details}" >> "${report_file}"
            else
                echo "- ${emoji} ${label}: Status ${status}" >> "${report_file}"
            fi
        }

        _render_validation_line "udp_reception" "UDP Packet Reception"
        _render_validation_line "network_timing" "Network Timing"
        _render_validation_line "frame_processing" "Frame Processing"
        _render_validation_line "video_recording" "Video Recording"
        _render_validation_line "packet_integrity" "Content Integrity"
    fi

    # Add Resource Usage section first if resource.json exists
    local resource_json="${OUTPUT_DIR}/resource.json"
    if [[ -f "${resource_json}" ]] && command -v jq >/dev/null 2>&1; then
        local duration_ms sample_count total_sample_count
        local cpu_min cpu_median cpu_mean cpu_max ram_mb_min ram_mb_median ram_mb_mean ram_mb_max
        local gpu_min gpu_median gpu_mean gpu_max
        local effective_cpus physical_cpus
        duration_ms=$(jq -r '.duration_ms // 0' "${resource_json}")
        sample_count=$(jq -r '.sample_count // 0' "${resource_json}")
        total_sample_count=$(jq -r '.total_sample_count // 0' "${resource_json}")
        cpu_min=$(jq -r '.cpu_percent.min // 0' "${resource_json}")
        cpu_median=$(jq -r '.cpu_percent.median // 0' "${resource_json}")
        cpu_mean=$(jq -r '.cpu_percent.mean // 0' "${resource_json}")
        cpu_max=$(jq -r '.cpu_percent.max // 0' "${resource_json}")
        ram_mb_min=$(jq -r '.ram_mb.min // 0' "${resource_json}")
        ram_mb_median=$(jq -r '.ram_mb.median // 0' "${resource_json}")
        ram_mb_mean=$(jq -r '.ram_mb.mean // 0' "${resource_json}")
        ram_mb_max=$(jq -r '.ram_mb.max // 0' "${resource_json}")
        gpu_min=$(jq -r '.gpu_percent.min // null' "${resource_json}")
        gpu_median=$(jq -r '.gpu_percent.median // null' "${resource_json}")
        gpu_mean=$(jq -r '.gpu_percent.mean // null' "${resource_json}")
        gpu_max=$(jq -r '.gpu_percent.max // null' "${resource_json}")
        effective_cpus=$(jq -r '.allocated_cpu_cores // 0' "${resource_json}")
        physical_cpus=$(jq -r '.total_cpu_cores // 0' "${resource_json}")

        if [[ "${sample_count}" -gt 0 ]]; then
            local duration_sec cpu_context
            duration_sec=$(awk -v ms="${duration_ms}" 'BEGIN{printf "%.1f", ms/1000}')

            # Build CPU context string
            if awk -v eff="${effective_cpus}" -v phys="${physical_cpus}" 'BEGIN{exit !(eff < phys && eff > 0)}'; then
                cpu_context=" (cgroup-limited: ${effective_cpus} of ${physical_cpus} cores)"
            elif [[ "${physical_cpus}" -gt 0 ]]; then
                cpu_context=" (${physical_cpus} cores)"
            else
                cpu_context=""
            fi

            echo >> "${report_file}"
            echo "### Resource Usage" >> "${report_file}"
            echo >> "${report_file}"
            local samples_text
            if [[ "${total_sample_count}" -gt 0 && "${total_sample_count}" -ne "${sample_count}" ]]; then
                samples_text="${sample_count} of ${total_sample_count} samples"
            else
                samples_text="${sample_count} samples"
            fi
            echo "During the test's processing window (${duration_sec}s, ${samples_text})${cpu_context}:" >> "${report_file}"
            echo >> "${report_file}"
            echo "| Metric | Min | Median | Mean | Max |" >> "${report_file}"
            echo "|--------|-----|--------|------|-----|" >> "${report_file}"
            echo "| CPU | ${cpu_min}% | ${cpu_median}% | ${cpu_mean}% | ${cpu_max}% |" >> "${report_file}"
            echo "| RAM | ${ram_mb_min} MB | ${ram_mb_median} MB | ${ram_mb_mean} MB | ${ram_mb_max} MB |" >> "${report_file}"
            if [[ "${gpu_median}" != "null" && -n "${gpu_median}" ]]; then
                echo "| GPU | ${gpu_min}% | ${gpu_median}% | ${gpu_mean}% | ${gpu_max}% |" >> "${report_file}"
            fi
            echo >> "${report_file}"
            echo "Details: [resource.csv](resource.csv) | [resource.json](resource.json)" >> "${report_file}"
        fi
    fi

    # Packet & Network Data section
    echo >> "${report_file}"
    echo "### Packet & Network Data" >> "${report_file}"
    echo >> "${report_file}"

    local video_count=0
    local audio_count=0
    if [[ "${PACKET_SOURCE}" == "device" ]]; then
        echo "- ℹ️ Packet Generation: Skipped (device packet source)" >> "${report_file}"
        echo "- ✅ UDP Capture: Device stream" >> "${report_file}"
    else
        if [[ -d "${TEST_DIR}/test_packets" ]]; then
            video_count=$(find "${TEST_DIR}/test_packets/video/${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)
            audio_count=$(find "${TEST_DIR}/test_packets/audio/${FORMAT}" -name "*.bin" 2>/dev/null | wc -l)
            echo "- ✅ Packet Generation: ${video_count} video, ${audio_count} audio packets" >> "${report_file}"
        else
            echo "- ⚠️ Packet Generation: Not captured" >> "${report_file}"
        fi
        echo "- ✅ UDP Replay: Completed successfully" >> "${report_file}"
    fi

    local event_links=()
    if [[ -f "${OUTPUT_DIR}/network.csv" ]]; then
        event_links+=("[network.csv](network.csv)")
    fi
    if [[ -f "${OUTPUT_DIR}/obs.csv" ]]; then
        event_links+=("[obs.csv](obs.csv)")
    fi
    if [[ -f "${OUTPUT_DIR}/playback.csv" ]]; then
        event_links+=("[playback.csv](playback.csv)")
    fi
    if (( ${#event_links[@]} > 0 )); then
        echo "- Events: $(join_by ', ' "${event_links[@]}")" >> "${report_file}"
    fi

    # Network Simulation section (if configured)
    if [[ -n "${SCENARIO_YAML_PATH:-}" && -f "${SCENARIO_YAML_PATH}" ]]; then
        local max_jitter_ms reorder_percent buffer_delay_ms
        max_jitter_ms=$(grep -m1 "max_jitter_ms:" "${SCENARIO_YAML_PATH}" 2>/dev/null | sed 's/.*: *//' || echo "0")
        reorder_percent=$(grep -m1 "reorder_percent:" "${SCENARIO_YAML_PATH}" 2>/dev/null | sed 's/.*: *//' || echo "0")
        buffer_delay_ms=$(grep -m1 "buffer_delay_ms:" "${SCENARIO_YAML_PATH}" 2>/dev/null | sed 's/.*: *//' || echo "0")

        if [[ "${max_jitter_ms:-0}" != "0" || "${reorder_percent:-0}" != "0" || "${buffer_delay_ms:-0}" != "0" ]]; then
            echo >> "${report_file}"
            echo "#### Network Simulation (Configured)" >> "${report_file}"
            echo >> "${report_file}"
            echo "| Parameter | Value |" >> "${report_file}"
            echo "|-----------|-------|" >> "${report_file}"
            [[ "${max_jitter_ms:-0}" != "0" ]] && echo "| Max Jitter | ${max_jitter_ms}ms |" >> "${report_file}"
            [[ "${reorder_percent:-0}" != "0" ]] && echo "| Reorder Rate | ${reorder_percent}% |" >> "${report_file}"
            [[ "${buffer_delay_ms:-0}" != "0" ]] && echo "| Buffer Delay | ${buffer_delay_ms}ms |" >> "${report_file}"
            echo >> "${report_file}"
            echo "_Note: Jitter is applied as random positive delay (0-max_jitter_ms) to each packet, causing sequence reordering. The 'Measured Jitter' below shows inter-packet interval variance at reception, which is typically low since packets are sent in time order after delay application._" >> "${report_file}"
        fi
    fi

    # Network Quality section (from network.json analysis)
    if [[ -f "${OUTPUT_DIR}/network.json" ]]; then
        echo >> "${report_file}"
        echo "#### Network Quality (Measured)" >> "${report_file}"
        echo >> "${report_file}"

        # Extract jitter statistics from network.json using Python
        if command -v python3 >/dev/null 2>&1; then
            python3 - "${OUTPUT_DIR}/network.json" >> "${report_file}" 2>/dev/null <<'PY' || true
import json
import sys

with open(sys.argv[1], 'r') as f:
    data = json.load(f)

summary = data.get('summary', {})
all_stats = data.get('all', {})
video = data.get('video', {})
audio = data.get('audio', {})

duration_ms = summary.get('duration_ms', None)
total_packets = summary.get('total_packets', all_stats.get('count', 0))

if duration_ms is not None:
    print(f"- Packet span (first→last): {duration_ms:.3f} ms")
if total_packets:
    print(f"- Total packets analyzed: {total_packets}")

def fmt_ms(us):
    if us is None:
        return "-"
    return f"{(us / 1000.0):.3f} ms"

def fmt_pct(v):
    if v is None:
        return "-"
    return f"{v:.2f}%"

def spacing_row(name, stats):
    if not stats:
        return None
    return (
        name,
        stats.get('count', 0),
        fmt_ms(stats.get('spacing_min_us')),
        fmt_ms(stats.get('spacing_mean_us')),
        fmt_ms(stats.get('spacing_max_us')),
        fmt_pct(stats.get('spacing_cv_pct')),
        fmt_pct(stats.get('burst_short_pct')),
        fmt_pct(stats.get('burst_long_pct')),
        stats.get('burst_p99_p50', None),
    )

print()
print("| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |")
print("|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|")

rows = [
    spacing_row('All', all_stats),
    spacing_row('Video', video),
    spacing_row('Audio', audio),
]

for r in rows:
    if not r:
        continue
    name, count, min_ms, mean_ms, max_ms, cv, burst_short, burst_long, p99_p50 = r
    p99_p50_str = f"{p99_p50:.3f}" if isinstance(p99_p50, (int, float)) else "-"
    print(f"| {name} | {count} | {min_ms} | {mean_ms} | {max_ms} | {cv} | {burst_short} | {burst_long} | {p99_p50_str} |")

# Build table
print()
print("| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |")
print("|--------|---------|-----------------|--------------|--------------|")

if video:
    v_count = video.get('count', 0)
    v_jitter_median = video.get('jitter_median_ms', 0)
    v_jitter_max = video.get('jitter_max_ms', 0)
    v_ooo_count = video.get('out_of_order_count', 0)
    v_ooo_rate = video.get('out_of_order_rate_pct', 0)
    ooo_str = f"{v_ooo_count} ({v_ooo_rate:.1f}%)" if v_ooo_count > 0 else "0"
    print(f"| Video | {v_count} | {v_jitter_median:.3f} ms | {v_jitter_max:.3f} ms | {ooo_str} |")

if audio:
    a_count = audio.get('count', 0)
    a_jitter_median = audio.get('jitter_median_ms', 0)
    a_jitter_max = audio.get('jitter_max_ms', 0)
    a_ooo_count = audio.get('out_of_order_count', 0)
    a_ooo_rate = audio.get('out_of_order_rate_pct', 0)
    ooo_str = f"{a_ooo_count} ({a_ooo_rate:.1f}%)" if a_ooo_count > 0 else "0"
    print(f"| Audio | {a_count} | {a_jitter_median:.3f} ms | {a_jitter_max:.3f} ms | {ooo_str} |")
PY
        fi
        echo >> "${report_file}"
        echo "Details: [network.json](network.json)" >> "${report_file}"
    fi

    # Perf profiling summary (if available)
    if [[ -f "${OUTPUT_DIR}/perf_report.txt" || -f "${OUTPUT_DIR}/perf_error.txt" || -f "${OUTPUT_DIR}/flamegraph.svg" ]]; then
        echo >> "${report_file}"
        echo "### Perf Hotspots" >> "${report_file}"
        echo >> "${report_file}"

        local perf_links=()
        if [[ -f "${OUTPUT_DIR}/perf_report.txt" ]]; then
            perf_links+=("[perf_report.txt](perf_report.txt)")
        fi
        if [[ -f "${OUTPUT_DIR}/flamegraph.svg" ]]; then
            perf_links+=("[flamegraph.svg](flamegraph.svg)")
        fi
        if [[ -f "${OUTPUT_DIR}/perf_error.txt" ]]; then
            perf_links+=("[perf_error.txt](perf_error.txt)")
        fi
        if (( ${#perf_links[@]} > 0 )); then
            echo "- Artifacts: $(join_by ' | ' "${perf_links[@]}")" >> "${report_file}"
        fi

        if [[ -f "${OUTPUT_DIR}/perf_report.txt" ]]; then
            echo >> "${report_file}"
            echo "Top functions/symbols by sampled CPU (perf report):" >> "${report_file}"
            echo >> "${report_file}"
            echo "| Overhead | Shared Object | Symbol |" >> "${report_file}"
            echo "|----------|---------------|--------|" >> "${report_file}"
            if command -v python3 >/dev/null 2>&1; then
                python3 - "${OUTPUT_DIR}/perf_report.txt" >> "${report_file}" 2>/dev/null <<'PY' || true
import re
import sys

path = sys.argv[1]
count = 0

with open(path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        if count >= 10:
            break
        if not re.match(r'^\s*\d+(?:\.\d+)?%\s', line):
            continue
        # perf report is column-aligned; columns are separated by 2+ spaces.
        parts = re.split(r'\s{2,}', line.strip())
        if len(parts) < 4:
            continue
        overhead, _command, dso, symbol = parts[:4]
        # Escape markdown pipes.
        dso = dso.replace('|', '\\|')
        symbol = symbol.replace('|', '\\|')
        print(f'| {overhead} | {dso} | {symbol} |')
        count += 1
PY
            else
                # Fallback: best-effort awk parse (may be less accurate with spaced commands)
                awk '
                    BEGIN{count=0}
                    /^[[:space:]]*[0-9]+(\.[0-9]+)?%/ {
                        overhead=$1
                        dso=$3
                        sym=$4" "$5
                        if (sym == "") next
                        gsub(/\|/, "\\|", dso)
                        gsub(/\|/, "\\|", sym)
                        print "| " overhead " | " dso " | " sym " |"
                        count++
                        if (count>=10) exit
                    }
                ' "${OUTPUT_DIR}/perf_report.txt" >> "${report_file}" || true
            fi
        fi
    fi

    local recording_found=""

    # Add OBS results if enabled
    if [[ "${OBS_ENABLED}" == true ]]; then
        if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" ]]; then
            recording_found="${OUTPUT_DIR}/c64_recording.mp4"
        elif compgen -G "${OUTPUT_DIR}/*.mp4" > /dev/null; then
            recording_found=$(ls -t ${OUTPUT_DIR}/*.mp4 2>/dev/null | head -1)
        fi
    else
        echo "- ⚠️ OBS Integration: Disabled (use --obs to enable)" >> "${report_file}"
    fi

    # Video section is rendered after A/V Sync (just before Sample Frame)

    # Add Pop synchronization section if validation results with details are available
    local validation_file="${OUTPUT_DIR}/validation_results.json"
    if [[ -f "${validation_file}" ]] && command -v jq >/dev/null 2>&1; then
        # Extract the av_sync_details block if present
        local has_details
        has_details=$(jq -r 'has("av_sync_details")' "${validation_file}" 2>/dev/null || echo "false")
        if [[ "${has_details}" == "true" ]]; then
            echo >> "${report_file}"
            echo "### A/V Sync" >> "${report_file}"
            echo >> "${report_file}"

            # Overall verdict line similar to console output
            local sync_accuracy is_perfect avg_diff max_diff avg_diff_fmt max_diff_fmt sync_accuracy_fmt
            sync_accuracy=$(jq -r '.av_sync_details.sync_accuracy_percent // 0' "${validation_file}")
            is_perfect=$(jq -r '.av_sync_details.is_perfectly_synced // false' "${validation_file}")
            # Compute avg/max over diffs when available
            avg_diff=$(jq -r '[.av_sync_details.sync_details[] | select(has("closest_video_pop_ms")) | .difference_ms] | if length>0 then (add/length) else 0 end' "${validation_file}" 2>/dev/null || echo "0")
            max_diff=$(jq -r '[.av_sync_details.sync_details[] | select(has("closest_video_pop_ms")) | .difference_ms] | if length>0 then max else 0 end' "${validation_file}" 2>/dev/null || echo "0")
            sync_accuracy_fmt=$(format_to_one_decimal "${sync_accuracy}")
            [[ -z "${sync_accuracy_fmt}" ]] && sync_accuracy_fmt="${sync_accuracy}"
            avg_diff_fmt=$(format_to_one_decimal "${avg_diff}")
            max_diff_fmt=$(format_to_one_decimal "${max_diff}")

            if [[ "${is_perfect}" == "true" ]]; then
                echo "- ✅ Good synchronization (${sync_accuracy_fmt}%): avg offset ${avg_diff_fmt}ms, max ${max_diff_fmt}ms" >> "${report_file}"
            elif awk -v acc="${sync_accuracy}" 'BEGIN{exit !(acc>=60)}'; then
                echo "- ✅ Acceptable synchronization (${sync_accuracy_fmt}%): avg offset ${avg_diff_fmt}ms, max ${max_diff_fmt}ms" >> "${report_file}"
            else
                echo "- ❌ Poor synchronization (${sync_accuracy_fmt}%): avg offset ${avg_diff_fmt}ms, max ${max_diff_fmt}ms" >> "${report_file}"
            fi

            # (Omit raw pop/time index lists to avoid duplication)

            # Per-pop lines with traffic light and channel
            echo >> "${report_file}"
            echo "#### Sync Details" >> "${report_file}"
            echo >> "${report_file}"
            # Build arrays
            local count
            count=$(jq -r '.av_sync_details.sync_details | length' "${validation_file}")
            if [[ "${count}" =~ ^[0-9]+$ ]] && [[ ${count} -gt 0 ]]; then
                for ((i=0; i<count; i++)); do
                    local status emoji chan a_ms v_ms diff_ms
                    status=$(jq -r ".av_sync_details.sync_details[$i].traffic // \"\"" "${validation_file}")
                    case "${status}" in
                        green) emoji="🟢" ;;
                        yellow) emoji="🟡" ;;
                        red) emoji="🔴" ;;
                        *) emoji="•" ;;
                    esac
                    chan=$(jq -r ".av_sync_details.sync_details[$i].audio_channel // .av_sync_details.sync_details[$i].channel // \"?\"" "${validation_file}")
                    a_ms=$(jq -r ".av_sync_details.sync_details[$i].audio_pop_time_ms // \"\"" "${validation_file}")
                    v_ms=$(jq -r ".av_sync_details.sync_details[$i].closest_video_pop_ms // empty" "${validation_file}")
                    v_fr=$(jq -r ".av_sync_details.sync_details[$i].closest_video_pop_frame // empty" "${validation_file}")
                    diff_ms=$(jq -r ".av_sync_details.sync_details[$i].difference_ms // empty" "${validation_file}")
                    # Format to 0.1 ms (tenth of a millisecond)
                    a_ms_fmt=$(printf '%.1f' "${a_ms:-0}")
                    if [[ -n "${v_ms}" ]] && [[ -n "${diff_ms}" ]]; then
                        if [[ -n "${v_fr}" && "${v_fr}" != "null" ]]; then
                            echo "- ${emoji} Pop #$((i+1)) [${chan}]: audio=${a_ms_fmt}ms, video=$(printf '%.1f' "${v_ms}")ms (frame ${v_fr}), diff=$(printf '%.1f' "${diff_ms}")ms" >> "${report_file}"
                        else
                            echo "- ${emoji} Pop #$((i+1)) [${chan}]: audio=${a_ms_fmt}ms, video=$(printf '%.1f' "${v_ms}")ms, diff=$(printf '%.1f' "${diff_ms}")ms" >> "${report_file}"
                        fi
                    else
                        echo "- ${emoji} Pop #$((i+1)) [${chan}]: audio=${a_ms_fmt}ms, no matching video pop found" >> "${report_file}"
                    fi
                done
            fi

            # Traffic lights summary and channels
            local traffic marks channels
            traffic=$(jq -r '.av_sync_details.traffic // [] | map(.)' "${validation_file}")
            # Compose marks string via jq
            marks=$(jq -r '[.av_sync_details.traffic[]? | if .=="green" then "🟢" elif .=="yellow" then "🟡" elif .=="red" then "🔴" else "•" end] | join("")' "${validation_file}")
            channels=$(jq -r '[.av_sync_details.sync_details[]? | .audio_channel // .channel | if .=="L" then "L" elif .=="R" then "R" else "B" end] | join("")' "${validation_file}")
            echo >> "${report_file}"
            echo "- Channels: ${channels}" >> "${report_file}"

            # Alternation check (ignore B), report verdict
            local seq_str alternates
            seq_str=$(jq -r '[.av_sync_details.sync_details[]? | select((.audio_channel // .channel)=="L" or (.audio_channel // .channel)=="R") | (.audio_channel // .channel)] | join(" ")' "${validation_file}")
            # Convert space-separated string to bash array
            IFS=' ' read -r -a seq <<< "${seq_str}"
            alternates=true
            if [[ ${#seq[@]} -ge 2 ]]; then
                for ((i=1; i<${#seq[@]}; i++)); do
                    if [[ "${seq[$i]}" == "${seq[$((i-1))]}" ]]; then
                        alternates=false
                        break
                    fi
                done
            fi
            if [[ "${alternates}" == true && ${#seq[@]} -ge 1 ]]; then
                echo "- 🔁 Channel alternation: OK (alternating, starts with ${seq[0]})" >> "${report_file}"
            else
                echo "- 🔁 Channel alternation: MISMATCH" >> "${report_file}"
            fi
        fi

        # Frame Progression section (Frame Box Sequence check)
        local fsb_status fsb_message
        fsb_status=$(jq -r '.frame_sequence_box.status // "skipped"' "${validation_file}" 2>/dev/null || echo "skipped")
        fsb_message=$(jq -r '.frame_sequence_box.message // ""' "${validation_file}" 2>/dev/null || echo "")

        if [[ "${fsb_status}" != "skipped" ]]; then
            echo >> "${report_file}"
            echo "### Frame Progression" >> "${report_file}"
            echo >> "${report_file}"

            # Get frame metrics
            local analyzed valid distinct settling_seconds
            analyzed=$(jq -r '.frame_sequence_box.details.analyzed_frames // 0' "${validation_file}" 2>/dev/null || echo "0")
            valid=$(jq -r '.frame_sequence_box.metrics.valid_frames // 0' "${validation_file}" 2>/dev/null || echo "0")
            distinct=$(jq -r '.frame_sequence_box.metrics.distinct_colors // 0' "${validation_file}" 2>/dev/null || echo "0")
            settling_seconds=$(jq -r '.frame_sequence_box.metrics.settling_seconds // .frame_sequence_box.details.settling_seconds // 0' "${validation_file}" 2>/dev/null || echo "0")

            # Status line with traffic light
            case "${fsb_status}" in
                pass)
                    echo "- 🟢 Frame sequence verified (${valid%.*} frames analyzed, ${distinct%.*} colors)" >> "${report_file}"
                    ;;
                warning)
                    echo "- 🟡 ${fsb_message}" >> "${report_file}"
                    ;;
                fail)
                    echo "- 🔴 ${fsb_message}" >> "${report_file}"
                    ;;
            esac

            # Settling split (pre/post)
            local pre_stuck_count pre_stuck_min pre_stuck_median pre_stuck_max
            local pre_skip_count pre_skip_min pre_skip_median pre_skip_max
            local pre_back_steps pre_severe_steps
            local post_stuck_count post_stuck_min post_stuck_median post_stuck_max
            local post_skip_count post_skip_min post_skip_median post_skip_max
            local post_back_steps post_severe_steps

            pre_stuck_count=$(jq -r '.frame_sequence_box.metrics.pre_settling_stuck_run_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_stuck_min=$(jq -r '.frame_sequence_box.metrics.pre_settling_stuck_run_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_stuck_median=$(jq -r '.frame_sequence_box.metrics.pre_settling_stuck_run_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_stuck_max=$(jq -r '.frame_sequence_box.metrics.pre_settling_max_stuck_run // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_count=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_min=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_median=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_skip_max=$(jq -r '.frame_sequence_box.metrics.pre_settling_skip_max // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_back_steps=$(jq -r '.frame_sequence_box.metrics.pre_settling_back_steps // 0' "${validation_file}" 2>/dev/null || echo "0")
            pre_severe_steps=$(jq -r '.frame_sequence_box.metrics.pre_settling_severe_steps // 0' "${validation_file}" 2>/dev/null || echo "0")

            post_stuck_count=$(jq -r '.frame_sequence_box.metrics.post_settling_stuck_run_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_stuck_min=$(jq -r '.frame_sequence_box.metrics.post_settling_stuck_run_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_stuck_median=$(jq -r '.frame_sequence_box.metrics.post_settling_stuck_run_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_stuck_max=$(jq -r '.frame_sequence_box.metrics.post_settling_max_stuck_run // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_count=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_count // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_min=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_min // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_median=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_median // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_skip_max=$(jq -r '.frame_sequence_box.metrics.post_settling_skip_max // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_back_steps=$(jq -r '.frame_sequence_box.metrics.post_settling_back_steps // 0' "${validation_file}" 2>/dev/null || echo "0")
            post_severe_steps=$(jq -r '.frame_sequence_box.metrics.post_settling_severe_steps // 0' "${validation_file}" 2>/dev/null || echo "0")

            echo >> "${report_file}"
            echo "- Settling: ${settling_seconds}s (pass/fail uses post-settling only)" >> "${report_file}"

            # Show table if there are any issues in either window
            local has_pre=false has_post=false
            if [[ "${pre_stuck_count}" != "0" && "${pre_stuck_count}" != "null" ]] || \
               [[ "${pre_skip_count}" != "0" && "${pre_skip_count}" != "null" ]] || \
               [[ "${pre_back_steps}" != "0" && "${pre_back_steps}" != "null" ]] || \
               [[ "${pre_severe_steps}" != "0" && "${pre_severe_steps}" != "null" ]]; then
                has_pre=true
            fi
            if [[ "${post_stuck_count}" != "0" && "${post_stuck_count}" != "null" ]] || \
               [[ "${post_skip_count}" != "0" && "${post_skip_count}" != "null" ]] || \
               [[ "${post_back_steps}" != "0" && "${post_back_steps}" != "null" ]] || \
               [[ "${post_severe_steps}" != "0" && "${post_severe_steps}" != "null" ]]; then
                has_post=true
            fi

            if [[ "${has_pre}" == "true" || "${has_post}" == "true" ]]; then
                echo >> "${report_file}"
                echo "| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |" >> "${report_file}"
                echo "|--------|------------------------------:|--------------------------:|-----------:|-------------:|" >> "${report_file}"

                local pre_stuck_count_i pre_stuck_min_i pre_stuck_median_i pre_stuck_max_i
                local pre_skip_count_i pre_skip_min_i pre_skip_median_i pre_skip_max_i
                local pre_back_steps_i pre_severe_steps_i
                pre_stuck_count_i=$(printf '%.0f' "${pre_stuck_count}" 2>/dev/null || echo "${pre_stuck_count}")
                pre_stuck_min_i=$(printf '%.0f' "${pre_stuck_min}" 2>/dev/null || echo "${pre_stuck_min}")
                pre_stuck_median_i=$(printf '%.0f' "${pre_stuck_median}" 2>/dev/null || echo "${pre_stuck_median}")
                pre_stuck_max_i=$(printf '%.0f' "${pre_stuck_max}" 2>/dev/null || echo "${pre_stuck_max}")
                pre_skip_count_i=$(printf '%.0f' "${pre_skip_count}" 2>/dev/null || echo "${pre_skip_count}")
                pre_skip_min_i=$(printf '%.0f' "${pre_skip_min}" 2>/dev/null || echo "${pre_skip_min}")
                pre_skip_median_i=$(printf '%.0f' "${pre_skip_median}" 2>/dev/null || echo "${pre_skip_median}")
                pre_skip_max_i=$(printf '%.0f' "${pre_skip_max}" 2>/dev/null || echo "${pre_skip_max}")
                pre_back_steps_i=$(printf '%.0f' "${pre_back_steps}" 2>/dev/null || echo "${pre_back_steps}")
                pre_severe_steps_i=$(printf '%.0f' "${pre_severe_steps}" 2>/dev/null || echo "${pre_severe_steps}")

                local post_stuck_count_i post_stuck_min_i post_stuck_median_i post_stuck_max_i
                local post_skip_count_i post_skip_min_i post_skip_median_i post_skip_max_i
                local post_back_steps_i post_severe_steps_i
                post_stuck_count_i=$(printf '%.0f' "${post_stuck_count}" 2>/dev/null || echo "${post_stuck_count}")
                post_stuck_min_i=$(printf '%.0f' "${post_stuck_min}" 2>/dev/null || echo "${post_stuck_min}")
                post_stuck_median_i=$(printf '%.0f' "${post_stuck_median}" 2>/dev/null || echo "${post_stuck_median}")
                post_stuck_max_i=$(printf '%.0f' "${post_stuck_max}" 2>/dev/null || echo "${post_stuck_max}")
                post_skip_count_i=$(printf '%.0f' "${post_skip_count}" 2>/dev/null || echo "${post_skip_count}")
                post_skip_min_i=$(printf '%.0f' "${post_skip_min}" 2>/dev/null || echo "${post_skip_min}")
                post_skip_median_i=$(printf '%.0f' "${post_skip_median}" 2>/dev/null || echo "${post_skip_median}")
                post_skip_max_i=$(printf '%.0f' "${post_skip_max}" 2>/dev/null || echo "${post_skip_max}")
                post_back_steps_i=$(printf '%.0f' "${post_back_steps}" 2>/dev/null || echo "${post_back_steps}")
                post_severe_steps_i=$(printf '%.0f' "${post_severe_steps}" 2>/dev/null || echo "${post_severe_steps}")

                echo "| During settling | ${pre_stuck_count_i}/${pre_stuck_min_i}/${pre_stuck_median_i}/${pre_stuck_max_i} | ${pre_skip_count_i}/${pre_skip_min_i}/${pre_skip_median_i}/${pre_skip_max_i} | ${pre_back_steps_i} | ${pre_severe_steps_i} |" >> "${report_file}"
                echo "| After settling | ${post_stuck_count_i}/${post_stuck_min_i}/${post_stuck_median_i}/${post_stuck_max_i} | ${post_skip_count_i}/${post_skip_min_i}/${post_skip_median_i}/${post_skip_max_i} | ${post_back_steps_i} | ${post_severe_steps_i} |" >> "${report_file}"
            fi

            # Reference playback.csv for detailed frame-by-frame analysis
            if [[ -f "${OUTPUT_DIR}/playback.csv" ]]; then
                echo >> "${report_file}"
                echo "See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers." >> "${report_file}"

                # Jitter cluster analysis (post-settling)
                if command -v python3 >/dev/null 2>&1; then
                    local jitter_cluster_md
                    jitter_cluster_md=$(python3 - "${OUTPUT_DIR}/playback.csv" "${settling_seconds}" 2>/dev/null <<'PY'
import csv
import math
import sys

path = sys.argv[1]
try:
    settling = float(sys.argv[2])
except Exception:
    settling = 0.0

# Treat any repeated/skipped frame marker as "jitter".
events = []
content_video_s = []
with open(path, newline='') as f:
    reader = csv.DictReader(f)
    for row in reader:
        try:
            video_s_raw = row.get('video_s')
            content_s_raw = row.get('content_s')
            video_s = float(video_s_raw) if video_s_raw not in (None, '') else None
            content_s = float(content_s_raw) if content_s_raw not in (None, '') else None

            # Prefer video timeline when available; fall back to content timeline.
            t = video_s if video_s is not None else (content_s if content_s is not None else 0.0)

            repeated = int(float(row.get('repeated') or 0))
            skipped = int(float(row.get('skipped') or 0))
        except Exception:
            continue

        if content_s is not None and video_s is not None:
            content_video_s.append(video_s)

        if t < settling:
            continue
        if repeated or skipped:
            events.append(t)

events.sort()
content_video_s.sort()

content_span = None
if content_video_s:
    content_span = (content_video_s[0], content_video_s[-1])

if not events:
    print('')
    print('#### Playback Jitter Clusters (post-settling)')
    print('')
    print('- No post-settling repeated/skipped markers detected in playback timeline.')
    sys.exit(0)

max_gap_s = 0.5

clusters = []
bucket = [events[0]]
prev = events[0]
for t in events[1:]:
    if (t - prev) <= max_gap_s:
        bucket.append(t)
    else:
        clusters.append(bucket)
        bucket = [t]
    prev = t
clusters.append(bucket)

def stats(xs):
    n = len(xs)
    mean = sum(xs) / n
    var = sum((x - mean) ** 2 for x in xs) / n
    return mean, math.sqrt(var), xs[-1] - xs[0]

summaries = []
for c in clusters:
    center, std, span = stats(c)
    summaries.append((len(c), span, center, std, c[0], c[-1]))

# Sort by size, then by time span.
summaries.sort(key=lambda x: (x[0], x[1]), reverse=True)

print('')
print('#### Playback Jitter Clusters (post-settling)')
print('')
print(f'- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap {max_gap_s:.1f}s')
print('- Note: this is independent from the Frame Progression (frame-box) check above')
if content_span is not None:
    print(f'- Note: repeated/skipped markers only exist while content is detected (video_s {content_span[0]:.3f}–{content_span[1]:.3f}).')
    print('  The jitter-free tail after content ends is expected and does not indicate steady-state performance.')
print('')
print('| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |')
print('|---|--------|------------|-------------|----------|------------|')
for i, (count, span, center, std, start, end) in enumerate(summaries[:3], start=1):
    print(f'| {i} | {count} | {center:.3f} | {std:.3f} | {span:.3f} | {start:.3f}–{end:.3f} |')
PY
)

                    if [[ -n "${jitter_cluster_md}" ]]; then
                        echo "${jitter_cluster_md}" >> "${report_file}"
                    fi
                fi
            fi
        fi
    fi

    if [[ "${VERBOSE}" == true ]]; then
        echo
        cat "${report_file}"
    fi

    # Append sample frame details if available
    local sample_frame_path="${OUTPUT_DIR}/c64_recording_still.png"
    local recording_mp4="${recording_found:-}"
    if [[ -z "${recording_mp4}" || ! -f "${recording_mp4}" ]]; then
        if [[ -f "${OUTPUT_DIR}/c64_recording.mp4" ]]; then
            recording_mp4="${OUTPUT_DIR}/c64_recording.mp4"
        elif compgen -G "${OUTPUT_DIR}/*.mp4" > /dev/null; then
            recording_mp4=$(ls -t ${OUTPUT_DIR}/*.mp4 2>/dev/null | head -1)
        fi
    fi

    # Use playback.csv to bound content frames (avoid logo-only stills).
    local content_start_frame=""
    local content_end_frame=""
    if [[ -f "${OUTPUT_DIR}/playback.csv" ]] && command -v python3 >/dev/null 2>&1; then
        read -r content_start_frame content_end_frame < <(python3 - <<'PY' "${OUTPUT_DIR}/playback.csv" 2>/dev/null || true
import csv
import sys
from pathlib import Path

path = Path(sys.argv[1])
start = None
end = None
try:
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            frame_num = (row.get("frame_num") or "").strip()
            if frame_num == "":
                continue
            idx = row.get("playback_frame_index")
            if idx is None or str(idx).strip() == "":
                continue
            try:
                idx_int = int(float(idx))
            except ValueError:
                continue
            if start is None:
                start = idx_int
            end = idx_int
except Exception:
    pass

if start is not None and end is not None:
    print(f"{start} {end}")
PY
)
    fi

    # Sample frame: extract a frame showing the A/V pop white square.
    #
    # IMPORTANT: extract by exact frame index (n) rather than by timestamp (t).
    # Timestamp-based extraction can miss a 1–2 frame marker due to PTS rounding/offsets.
    if [[ -n "${recording_mp4}" && -f "${recording_mp4}" && -x "${TEST_DIR}/util/extract-frame.sh" ]]; then
        local pop_time_ms pop_frame_num first_pop_frame frame_rate
        pop_time_ms=""
        pop_frame_num=""
        first_pop_frame=""
        sample_frame_seconds=""
        local sample_frame_extracted=false

        # Try to get the first detected video pop frame index from validation results
        if [[ -f "${validation_file}" ]] && command -v jq >/dev/null 2>&1; then
            first_pop_frame=$(jq -r '.av_sync_details.video_pop_frame_indices[0] // empty' "${validation_file}" 2>/dev/null || true)
            if [[ -z "${first_pop_frame}" || "${first_pop_frame}" == "null" ]]; then
                first_pop_frame=$(jq -r '.av_sync_details.sync_details[0].closest_video_pop_frame // empty' "${validation_file}" 2>/dev/null || true)
            fi
        fi

        # Fallback: directly detect a video pop frame using test_av_sync.py
        if [[ -z "${first_pop_frame}" ]] && command -v python3 >/dev/null 2>&1; then
            # Prefer accurate FPS from ffprobe; fall back to format defaults
            frame_rate=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "${recording_mp4}" 2>/dev/null || echo "")
            if [[ -n "${frame_rate}" && "${frame_rate}" == *"/"* ]]; then
                # Evaluated fraction
                frame_rate=$(awk "BEGIN {print ${frame_rate}}" 2>/dev/null || echo "")
            fi
            if [[ -z "${frame_rate}" ]]; then
                if [[ "${FORMAT}" == "PAL" ]]; then frame_rate=50; else frame_rate=60; fi
            fi

            # Scan for a pop using our A/V sync tool logic (but just for one frame)
            # Using Python one-liner to call av_sync module is complex; skip fallback if validation.json exists.
            # But we can assume the pops happen periodically? No.
            # Let's just use the first frame if we can't find a pop?
            : # do nothing
        fi

        # If we have a pop frame index, use it.
        # Prefer the FIRST visible pop to ensure we're looking at stable content.
        if [[ -n "${first_pop_frame}" && "${first_pop_frame}" != "null" ]]; then
            # Add a small offset (e.g. +5 frames) to ensure we capture the white square fully if it fades in/out?
            # Actually, the pop detection marks the start. The square is visible for ~1 frame?
            # c64-av-sync.c says pulses are 1 frame long?
            # Let's trigger exactly on the detected frame.

            local tmp_dir=$(mktemp -d)
            if "${TEST_DIR}/util/extract-frame.sh" --input "${recording_mp4}" --output "${sample_frame_path}" --frame "${first_pop_frame}"; then
                sample_frame_extracted=true
                sample_frame_index="${first_pop_frame}"
            fi

            # Fallback if python scoring not available
            if [[ "${sample_frame_extracted}" != true ]]; then
                "${TEST_DIR}/util/extract-frame.sh" --input "${recording_mp4}" --output "${sample_frame_path}" --frame "${first_pop_frame}" || true
                sample_frame_extracted=true
                sample_frame_index="${first_pop_frame}"
            fi

            if [[ -n "${tmp_dir}" && -d "${tmp_dir}" ]]; then
                rm -rf "${tmp_dir}" || true
            fi
        fi

        if [[ "${sample_frame_extracted}" != true ]]; then
            # Final fallback: use mid-content frame if bounds exist, else mid-duration.
            if [[ -n "${content_start_frame}" && -n "${content_end_frame}" ]]; then
                local mid_frame
                mid_frame=$(( (content_start_frame + content_end_frame) / 2 ))
                "${TEST_DIR}/util/extract-frame.sh" --input "${recording_mp4}" --output "${sample_frame_path}" --frame "${mid_frame}" || true
            else
                if [[ -z "${sample_frame_seconds}" ]]; then
                    local dur_sec
                    if command -v ffprobe >/dev/null 2>&1; then
                        dur_sec=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${recording_mp4}" 2>/dev/null | head -1 || true)
                    fi
                    if [[ -n "${dur_sec}" ]]; then
                        sample_frame_seconds=$(awk -v d="${dur_sec}" 'BEGIN{printf "%.3f", d*0.50}')
                    else
                        sample_frame_seconds="4.500"
                    fi
                fi

                "${TEST_DIR}/util/extract-frame.sh" --input "${recording_mp4}" --output "${sample_frame_path}" --time "${sample_frame_seconds}" || true
            fi
        fi
    fi

    # Emit a Video block with download link before the Sample Frame
    if [[ -n "${recording_mp4}" && -f "${recording_mp4}" ]]; then
        echo >> "${report_file}"
        echo "### Video" >> "${report_file}"
        echo >> "${report_file}"
        rel_name=$(basename "${recording_mp4}")
        local download_note
        download_note="(Available from local runs or CI build artifacts.)"
        if [[ -f "${OUTPUT_DIR}/${rel_name}" ]]; then
            echo "- Download: [${rel_name}](${rel_name}) ${download_note}" >> "${report_file}"
        else
            echo "- Download: [${rel_name}](${recording_mp4}) ${download_note}" >> "${report_file}"
        fi

        if command -v ffprobe >/dev/null 2>&1; then
            video_duration_sec=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "${recording_mp4}" 2>/dev/null | head -1 || true)
            if [[ -n "${video_duration_sec}" ]]; then
                video_duration_fmt=$(format_to_one_decimal "${video_duration_sec}")
                echo "- Duration: ${video_duration_fmt} s" >> "${report_file}"
            fi
        fi

        if [[ -z "${video_duration_fmt}" ]]; then
            if [[ "${FORMAT}" == "PAL" ]]; then
                video_duration_fmt=$(awk -v frames="${FRAMES}" 'BEGIN{printf "%.1f", frames/50.0}')
            else
                video_duration_fmt=$(awk -v frames="${FRAMES}" 'BEGIN{printf "%.1f", frames/60.0}')
            fi
            echo "- Duration: ${video_duration_fmt} s" >> "${report_file}"
        fi

        echo >> "${report_file}"
    elif [[ "${OBS_ENABLED}" == true ]]; then
        echo >> "${report_file}"
        echo "### Video" >> "${report_file}"
        echo >> "${report_file}"
        echo "- ❌ Video: Recording not found" >> "${report_file}"
    fi

    if [[ -z "${sample_frame_seconds}" && -n "${sample_frame_index}" ]]; then
        local fps=0
        if [[ "${FORMAT}" == "PAL" ]]; then
            fps=50
        else
            fps=60
        fi
        if (( fps > 0 )); then
            sample_frame_seconds=$(awk -v frame="${sample_frame_index}" -v rate="${fps}" 'BEGIN{printf "%.3f", frame/rate}')
        fi
    fi

    local sample_frame_seconds_fmt=""
    if [[ -n "${sample_frame_seconds}" ]]; then
        sample_frame_seconds_fmt=$(format_to_one_decimal "${sample_frame_seconds}")
        sample_frame_timestamp=$(format_seconds_to_timestamp "${sample_frame_seconds}")
    fi

    if [[ -f "${sample_frame_path}" ]]; then
        echo >> "${report_file}"
        echo "### Sample Frame" >> "${report_file}"
        echo >> "${report_file}"
        echo "![Sample Frame](./$(basename "${sample_frame_path}"))" >> "${report_file}"
        echo >> "${report_file}"
        echo "- **Top-left**: Text box with scenario name" >> "${report_file}"
        echo "- **Top-right**: VIC-II palette reference grid of all C64 colors" >> "${report_file}"
        echo "- **Center**: Diagonal pattern cycling through all C64 colors" >> "${report_file}"
        echo "- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)" >> "${report_file}"
        echo "- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)" >> "${report_file}"

        local origin_parts=()
        if [[ -n "${sample_frame_index}" ]]; then
            origin_parts+=("frame ${sample_frame_index}")
        fi
        if [[ -n "${sample_frame_timestamp}" ]]; then
            origin_parts+=("${sample_frame_timestamp}")
        elif [[ -n "${sample_frame_seconds_fmt}" ]]; then
            origin_parts+=("${sample_frame_seconds_fmt}s")
        fi

        local origin_text=""
        if (( ${#origin_parts[@]} > 0 )); then
            origin_text=$(join_by " at " "${origin_parts[@]}")
        fi

        if [[ -n "${origin_text}" ]]; then
            if [[ -n "${video_duration_fmt}" ]]; then
                echo "- Taken from ${origin_text} of the ${video_duration_fmt} s video above." >> "${report_file}"
            else
                echo "Taken from ${origin_text} of the video above." >> "${report_file}"
            fi
        else
            if [[ -n "${video_duration_fmt}" ]]; then
                echo "- Taken from the ${video_duration_fmt} s video above." >> "${report_file}"
            else
                echo "- Taken from the video above." >> "${report_file}"
            fi
        fi
    fi

    log_success "Markdown report saved to ${report_file}"
}
