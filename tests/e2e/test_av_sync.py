#!/usr/bin/env python3
"""
A/V Synchronization Test for C64 Stream (A/V sync check)

This script performs the A/V sync check, verifying that audio and video pops
start at the exact same time.

Pop definition (updated):
- Duration: 1 frame
- First pop: ~1000ms after frames start; interval: ~1000ms
- No pop in the last 500ms of the recording
- Audio pop: pleasant band-limited noise burst with instant attack/decay (no fade)
- Video pop: 30x30 pure white square, instantly on/off, centered within a permanently black
    60x60 "video pop area" at the lower-right corner of the C64 frame.

Note: The generation of these pops is performed by the A/V sync generator; this file only
performs verification (the A/V sync check).
"""

import cv2
import numpy as np
import subprocess
import json
import tempfile
import os
from pathlib import Path


def extract_audio_envelope(video_path, sample_rate=48000):
    """
    Extract audio envelope to detect beep timing.
    Returns array of audio power levels over time.
    """
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as temp_audio:
        # Extract audio as WAV for analysis
        cmd = [
            'ffmpeg', '-i', str(video_path),
            '-vn', '-acodec', 'pcm_s16le',
            '-ar', str(sample_rate), '-ac', '2',
            '-y', temp_audio.name
        ]
        subprocess.run(cmd, capture_output=True, check=True)
        temp_name = temp_audio.name

    # Load audio data and parse WAV header
    raw = np.fromfile(temp_name, dtype=np.uint8)
    # Basic 44-byte PCM header parse for stereo 16-bit
    data = raw[44:].view(np.int16)
    # Split channels
    left = data[0::2]
    right = data[1::2]

    # Calculate envelope (RMS power in 10ms windows)
    window_size = sample_rate // 100  # 10ms windows
    envelope = []

    for i in range(0, len(left) - window_size, window_size):
        wl = left[i:i + window_size]
        wr = right[i:i + window_size]
        rms_l = np.sqrt(np.mean(wl.astype(float) ** 2))
        rms_r = np.sqrt(np.mean(wr.astype(float) ** 2))
        envelope.append((rms_l, rms_r))

    # Clean up temp file
    os.unlink(temp_name)

    return np.array(envelope)


def detect_video_pops(video_path, frame_rate=30.0):
    """
    Detect timing (frame numbers) when the 30x30 white video pop is visible.

    Coordinate system assumptions:
    - Source C64 frame: 384x272
            - Video event area: 80x80 at lower-right of C64 frame
                * area spans X:[304..384), Y:[192..272)
            - Event square: 50x50 centered within that area, instant on/off

    Recording layout assumptions:
    - Output video is 1920x1080 where the C64 content fills the vertical dimension and is
      horizontally centered with side black bars. We compute the scaled positions accordingly.
    """
    cap = cv2.VideoCapture(str(video_path))
    pop_frames = []
    frame_num = 0

    # Minimal warmup skip to avoid early startup artifacts but still capture the first event at ~1s
    skip_frames = int(0.8 * frame_rate)

    # We'll detect content bounds per frame (cheap) to avoid using stale first-frame bounds
    # which can include black bars during scene transitions.
    cached_bounds = None  # kept for potential future smoothing, but we recompute below

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        # Skip early frames to avoid C64 logo false positives
        if frame_num < skip_frames:
            frame_num += 1
            continue

        # Get frame dimensions and calculate scaling from C64 source
        height, width = frame.shape[:2]

        # C64 source and geometry
        c64_width, c64_height = 384, 272

        # Video event area (80x80) at lower-right of C64 frame
        area_size = 80
        # Event (white) square 50x50 centered within the area
        event_size = 50

        # We'll derive the horizontal scale from detected content bounds (robust to placement)
        # and compute vertical positions using the actual scaled area size rather than PAL/NTSC guess.
        # Initialize with a sane default until bounds are detected
        scale_factor = height / 272.0
        scaled_c64_width = int(c64_width * scale_factor)

        # Detect C64 content bounds each frame for robust ROI placement
        left_bound, right_bound, top_bound, bottom_bound = _detect_content_bounds(frame)
        c64_left_offset = left_bound
        c64_right_offset = right_bound
        c64_top_offset = top_bound
        c64_bottom_offset = bottom_bound
        # Derive scale directly from detected content bounds
        detected_c64_width = max(1, c64_right_offset - c64_left_offset)
        scale_factor = detected_c64_width / float(c64_width)
        scaled_c64_width = detected_c64_width

        # Compute scaled video event area rectangle in recording (lower-right of content)
        scaled_area_size = int(area_size * scale_factor)
        scaled_area_left = c64_left_offset + scaled_c64_width - scaled_area_size
        scaled_area_right = c64_left_offset + scaled_c64_width
        scaled_area_top = c64_bottom_offset - scaled_area_size
        scaled_area_bottom = c64_bottom_offset

        # Extract area (permanently black when no pop)
        area_region = frame[scaled_area_top:scaled_area_bottom, scaled_area_left:scaled_area_right]
        if area_region.size == 0:
            frame_num += 1
            continue

    # Extract central pop subregion (50x50 scaled) to detect white square
        scaled_event_size = int(event_size * scale_factor)
        event_center_x = scaled_area_left + scaled_area_size // 2
        event_center_y = scaled_area_top + scaled_area_size // 2
        ev_left = event_center_x - scaled_event_size // 2
        ev_right = event_center_x + scaled_event_size // 2
        ev_top = event_center_y - scaled_event_size // 2
        ev_bottom = event_center_y + scaled_event_size // 2

        # Clamp to frame bounds
        ev_left = max(0, ev_left)
        ev_top = max(0, ev_top)
        ev_right = min(width, ev_right)
        ev_bottom = min(height, ev_bottom)

        event_region = frame[ev_top:ev_bottom, ev_left:ev_right]
        if event_region.size == 0:
            frame_num += 1
            continue

        # Convert to grayscale for brightness analysis
        gray_event = cv2.cvtColor(event_region, cv2.COLOR_BGR2GRAY)
        mean_brightness = float(np.mean(gray_event))

        # Detect pure white pop with a high threshold (instant on/off)
        if mean_brightness > 200.0:
            pop_frames.append(frame_num)

        frame_num += 1

    cap.release()
    return pop_frames


def detect_audio_pops(envelope, threshold_factor=3.0, min_duration_ms=10):
    """
    Detect audio pops in the envelope.
    Returns list of pop start times in milliseconds (10ms resolution).
    """
    # Calculate dynamic threshold based on background noise
    if envelope.ndim == 2 and envelope.shape[1] == 2:
        bg_l = np.percentile(envelope[:, 0], 10)
        bg_r = np.percentile(envelope[:, 1], 10)
        thr_l = bg_l * threshold_factor
        thr_r = bg_r * threshold_factor
    else:
        bg = np.percentile(envelope, 10)
        thr_l = thr_r = bg * threshold_factor

    pop_starts = []
    in_pop = False
    pop_start = None

    for i in range(len(envelope)):
        level_l = envelope[i][0] if envelope.ndim == 2 else envelope[i]
        level_r = envelope[i][1] if envelope.ndim == 2 else envelope[i]
        is_above = (level_l > thr_l) or (level_r > thr_r)
        if is_above and not in_pop:
            # Start of pop
            pop_start = i
            in_pop = True
        elif (not is_above) and in_pop:
            # End of pop
            if pop_start is not None:
                pop_duration_ms = (i - pop_start) * 10  # 10ms per sample
                if pop_duration_ms >= min_duration_ms:
                    # Determine dominant channel for this pop window
                    seg = envelope[pop_start:i]
                    if seg.ndim == 2:
                        mean_l = float(np.mean(seg[:, 0]))
                        mean_r = float(np.mean(seg[:, 1]))
                        chan = 'L' if mean_l > mean_r else 'R'
                    else:
                        chan = 'B'  # both/mono
                    pop_starts.append({'time_ms': pop_start * 10, 'channel': chan})
            in_pop = False
            pop_start = None

    return pop_starts


def _detect_content_bounds(frame):
    """Detect content bounds (left, right, top, bottom) using black bars around C64 content."""
    height, width = frame.shape[:2]
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    _, non_black = cv2.threshold(gray, 10, 255, cv2.THRESH_BINARY)

    # Horizontal bounds
    col_counts = np.sum(non_black > 0, axis=0)
    col_thresh = max(1, int(0.10 * height))
    content_cols = np.where(col_counts >= col_thresh)[0]
    if content_cols.size >= 2:
        left_bound = int(content_cols[0])
        right_bound = int(content_cols[-1]) + 1
    else:
        # Fallback: assume centered horizontally
        scale_factor = height / 272.0
        scaled_c64_width = int(384 * scale_factor)
        left_bound = (width - scaled_c64_width) // 2
        right_bound = (width + scaled_c64_width) // 2

    # Vertical bounds
    row_counts = np.sum(non_black > 0, axis=1)
    row_thresh = max(1, int(0.10 * width))
    content_rows = np.where(row_counts >= row_thresh)[0]
    if content_rows.size >= 2:
        top_bound = int(content_rows[0])
        bottom_bound = int(content_rows[-1]) + 1
    else:
        # Fallback: fill vertically
        top_bound = 0
        bottom_bound = height

    return left_bound, right_bound, top_bound, bottom_bound


def verify_video_pop_area_blackness(video_path, sample_count=4):
    """Verify the pop staging area (60x60 lower-right) is black when no pop is active.

    Samples a few frames between detected pops and before the first pop.
    Returns dict: { pass: bool, samples: int, failures: [ {frame: int, mean: float} ] }
    """
    cap = cv2.VideoCapture(str(video_path))
    frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0

    # Determine sample frames: pick midpoints between detected video pops to ensure we sample
    # when the staging area should be black, and avoid early logo frames entirely.
    ret, first_frame = cap.read()
    if not ret:
        return {'pass': False, 'samples': 0, 'failures': [{'error': 'could not read video'}]}

    # Detect video pop frames (use fps for accurate timing conversion)
    pop_frames = detect_video_pops(video_path, frame_rate=fps)
    indices = []
    for a, b in zip(pop_frames, pop_frames[1:]):
        mid = (a + b) // 2
        indices.append(int(mid))
    # If not enough samples, add a couple after first pop at +0.25s and +0.5s
    if len(indices) < sample_count and pop_frames:
        base = pop_frames[0]
        indices += [int(base + 0.25 * fps), int(base + 0.5 * fps)]
    # Constrain to safe end (avoid last 500ms)
    duration_ms = (frame_count / fps) * 1000.0
    safe_end_frame = int(max(0, (duration_ms - 500.0) / 1000.0 * fps))
    indices = [i for i in indices if 0 <= i < safe_end_frame]
    # Limit to requested sample_count unique frames
    indices = sorted(set(indices))[:sample_count]

    failures = []
    for idx in indices:
        cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ret, frame = cap.read()
        if not ret:
            continue
        # Detect bounds and compute area per frame
        left, right, top, bottom = _detect_content_bounds(frame)
        scale = (right - left) / 384.0
        area_px = int(80 * scale)
        area_left = right - area_px
        area_right = right
        area_bottom = bottom
        area_top = bottom - area_px
        roi = frame[area_top:area_bottom, area_left:area_right]
        if roi.size == 0:
            continue
        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        mean_brightness = float(np.mean(gray))
        # Expect near black
        if mean_brightness > 30.0:
            failures.append({'frame': int(idx), 'mean': mean_brightness})

    cap.release()
    return {'pass': len(failures) == 0, 'samples': len(indices), 'failures': failures}


def verify_frame_sequence_box(video_path, solid_stddev_thresh=26.0, change_dist_thresh=14.0):
    """Check the 40x40 top-left frame sequence color box for solidity and progression.

    Robustness improvements:
    - Auto-locate the box within a small search window near the top-left of the C64 content
      to tolerate minor cropping/position shifts and scaling interpolation.
    - Use an inner margin when sampling to avoid edge bleed from scaling.

    Returns dict: { pass: bool, solid_ratio: float, distinct_colors: int, changes: int, frames: int, issues: [..] }
    """
    cap = cv2.VideoCapture(str(video_path))
    frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0

    ret, first = cap.read()
    if not ret:
        return {'pass': False, 'solid_ratio': 0.0, 'distinct_colors': 0, 'changes': 0, 'frames': 0, 'issues': ['could not read video']}

    # Find video pops to align analysis window with actual content frames
    pop_frames = detect_video_pops(video_path, frame_rate=fps)
    start_idx = 0
    if pop_frames:
        start_idx = max(pop_frames[0] - int(0.5 * fps), 0)
        cap.set(cv2.CAP_PROP_POS_FRAMES, start_idx)
        ret, first = cap.read()
        if not ret:
            return {'pass': False, 'solid_ratio': 0.0, 'distinct_colors': 0, 'changes': 0, 'frames': 0, 'issues': ['could not seek to analysis start']}

    prev_mean = None
    solids = 0
    changes = 0
    means = []
    issues = []

    # Cache offset from first analysis frame for speed (dx, dy)
    cached_offset = (0, 0)
    initialized_offset = False

    # Analyze up to first ~10 seconds for speed
    max_frames = min(frame_count, int(10 * fps))
    deviations = []  # (frame_idx, time_ms, expected_idx, got_idx)
    for i in range(max_frames):
        if i == 0:
            frame = first
        else:
            ret, frame = cap.read()
            if not ret:
                break

        # Detect bounds
        left, right, top, bottom = _detect_content_bounds(frame)
        scale = (right - left) / 384.0
        box_wh = int(40 * scale)

        # Apply inner margin crop before measuring to avoid edge artifacts
        inner_margin = max(4, int(8 * scale))

        # One-time robust search near content top-left to find the most solid 40x40 area
        if not initialized_offset:
            content_w = right - left
            content_h = bottom - top
            # Search within a generous window near the top-left of the content
            span_x = min(content_w - box_wh, int(120 * scale))
            span_y = min(content_h - box_wh, int(120 * scale))
            step = max(2, int(4 * scale))

            best_std = float('inf')
            best_dx, best_dy = 0, 0
            best_roi_inner = None

            for off_y in range(0, max(1, span_y + 1), step):
                for off_x in range(0, max(1, span_x + 1), step):
                    tx = left + off_x
                    ty = top + off_y
                    troi = frame[ty:ty + box_wh, tx:tx + box_wh]
                    if troi.size == 0 or troi.shape[0] < inner_margin * 2 + 2 or troi.shape[1] < inner_margin * 2 + 2:
                        continue
                    ti = troi[inner_margin:troi.shape[0] - inner_margin,
                              inner_margin:troi.shape[1] - inner_margin]
                    # Optional tiny blur to reduce compression noise before stddev
                    ti_blur = cv2.GaussianBlur(ti, (0, 0), sigmaX=0.6)
                    s = float(np.std(ti_blur))
                    if s < best_std:
                        best_std = s
                        best_dx, best_dy = off_x, off_y
                        best_roi_inner = ti

            # Fine refinement around the best coarse location
            refine_range = max(1, int(2 * scale))
            if best_roi_inner is None:
                # Fallback to origin if search failed (shouldn't happen)
                cached_offset = (0, 0)
                best_std = float('inf')
                best_roi_inner = np.empty((0, 0, 3), dtype=frame.dtype)
            else:
                for ddy in range(-refine_range, refine_range + 1):
                    for ddx in range(-refine_range, refine_range + 1):
                        tx = left + best_dx + ddx
                        ty = top + best_dy + ddy
                        troi = frame[ty:ty + box_wh, tx:tx + box_wh]
                        if troi.size == 0 or troi.shape[0] < inner_margin * 2 + 2 or troi.shape[1] < inner_margin * 2 + 2:
                            continue
                        ti = troi[inner_margin:troi.shape[0] - inner_margin,
                                  inner_margin:troi.shape[1] - inner_margin]
                        ti_blur = cv2.GaussianBlur(ti, (0, 0), sigmaX=0.6)
                        s = float(np.std(ti_blur))
                        if s < best_std:
                            best_std = s
                            best_dx, best_dy = best_dx + ddx, best_dy + ddy
                            best_roi_inner = ti
                cached_offset = (best_dx, best_dy)
                initialized_offset = True

            # Use the best found ROI for metrics on this first frame
            best_roi_inner_curr = best_roi_inner
            stddev = best_std if best_std != float('inf') else float(np.std(best_roi_inner_curr))
        else:
            # Start with cached offset; refine slightly frame-to-frame
            dx, dy = cached_offset
            box_left = left + dx
            box_top = top + dy
            roi = frame[box_top:box_top + box_wh, box_left:box_left + box_wh]
            if roi.size == 0:
                continue
            roi_inner = roi[inner_margin:roi.shape[0] - inner_margin,
                            inner_margin:roi.shape[1] - inner_margin]
            if roi_inner.size == 0:
                continue

            best_std = float('inf')
            best_roi_inner = roi_inner
            best_dx, best_dy = dx, dy
            for ddy in (-int(2 * scale), 0, int(2 * scale)):
                for ddx in (-int(2 * scale), 0, int(2 * scale)):
                    tx = left + dx + ddx
                    ty = top + dy + ddy
                    troi = frame[ty:ty + box_wh, tx:tx + box_wh]
                    if troi.size == 0 or troi.shape[0] < inner_margin * 2 + 2 or troi.shape[1] < inner_margin * 2 + 2:
                        continue
                    ti = troi[inner_margin:troi.shape[0] - inner_margin,
                              inner_margin:troi.shape[1] - inner_margin]
                    s = float(np.std(ti))
                    if s < best_std:
                        best_std = s
                        best_roi_inner = ti
                        best_dx, best_dy = dx + ddx, dy + ddy
            cached_offset = (best_dx, best_dy)
            best_roi_inner_curr = best_roi_inner
            stddev = best_std if best_std != float('inf') else float(np.std(best_roi_inner_curr))

        # Solidity: standard deviation over inner ROI should be low if solid color
        if stddev < solid_stddev_thresh:
            solids += 1
        else:
            issues.append(f'frame {i}: high stddev {stddev:.1f}')

        mean = np.mean(best_roi_inner_curr.reshape(-1, 3), axis=0)
        means.append(mean)
        # Verify expected color sequence: frame index modulo 16 (approximate mapping by quantizing)
        # Quantize mean to nearest 16-step in RGB, map to an index by averaging channels
        quant = np.round(mean / 16.0) * 16.0
        approx_idx = int(np.clip(np.round(np.mean(quant) / 16.0), 0, 15))
        expected_idx = (start_idx + i) % 16
        if approx_idx != expected_idx:
            time_ms = ((start_idx + i) / fps) * 1000.0
            deviations.append((start_idx + i, time_ms, expected_idx, approx_idx))
        if prev_mean is not None:
            dist = float(np.linalg.norm(mean - prev_mean))
            if dist > change_dist_thresh:
                changes += 1
        prev_mean = mean

    cap.release()
    frames_analyzed = len(means)
    solid_ratio = (solids / frames_analyzed) if frames_analyzed else 0.0
    # Count distinct colors using simple rounding bins
    if frames_analyzed:
        rounded = np.round(np.array(means) / 16.0) * 16.0
        # unique rows
        distinct = np.unique(rounded.astype(int), axis=0).shape[0]
    else:
        distinct = 0

    passed = (solid_ratio >= 0.85) and (distinct >= 6) and (changes >= max(3, int(0.3 * frames_analyzed))) and (len(deviations) == 0)
    return {
        'pass': passed,
        'solid_ratio': solid_ratio,
        'distinct_colors': int(distinct),
        'changes': int(changes),
        'frames': int(frames_analyzed),
        'issues': issues[:5],
        'deviations': deviations[:10]
    }


def verify_av_sync(video_path, tolerance_ms=25):
    """
    Verify A/V synchronization between black squares and audio beeps.

    Args:
        video_path: Path to video file
        tolerance_ms: Maximum allowed timing difference in milliseconds

    Returns:
        dict with sync analysis results
    """
    print(f"🔍 Analyzing A/V sync for: {video_path}")

    # Get video properties
    cmd = ['ffprobe', '-v', 'quiet', '-show_format', '-show_streams',
           '-of', 'json', str(video_path)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    video_info = json.loads(result.stdout)

    # Find video and audio streams
    video_stream = next(s for s in video_info['streams'] if s['codec_type'] == 'video')
    audio_stream = next(s for s in video_info['streams'] if s['codec_type'] == 'audio')

    frame_rate = eval(video_stream['r_frame_rate'])  # e.g., "30/1" -> 30.0
    sample_rate = int(audio_stream['sample_rate'])

    print(f"📊 Video: {frame_rate} fps, Audio: {sample_rate} Hz")

    # Extract audio envelope
    print("🔊 Extracting audio envelope...")
    envelope = extract_audio_envelope(video_path, sample_rate)

    # Detect audio pops
    audio_pops = detect_audio_pops(envelope, threshold_factor=3.0, min_duration_ms=30)
    print(f"🎵 Detected {len(audio_pops)} audio pop(s)")

    # Detect video pops (white square in lower-right area)
    print("⬜ Detecting video pop(s) (white square, lower-right)...")
    pop_frames = detect_video_pops(video_path, frame_rate)

    # Convert frame numbers to timestamps (ms)
    video_pop_times_ms = [frame / frame_rate * 1000.0 for frame in pop_frames]

    # Group consecutive frames into individual video pops (1 frame duration now)
    grouped_video_pop_starts = []
    if video_pop_times_ms:
        current_group_start = video_pop_times_ms[0]
        last_time = video_pop_times_ms[0]
        group_len = 1
        frame_interval_ms = 1000.0 / float(frame_rate)
        for t in video_pop_times_ms[1:]:
            if (t - last_time) <= (frame_interval_ms * 1.5):
                group_len += 1
            else:
                if group_len >= 1:  # accept >=1 frames as one pop
                    grouped_video_pop_starts.append(current_group_start)
                current_group_start = t
                group_len = 1
            last_time = t
        # last group
        if group_len >= 1:
            grouped_video_pop_starts.append(current_group_start)

    formatted_pops = [f"{t:.1f}" for t in grouped_video_pop_starts]
    print(f"⬜ Detected {len(grouped_video_pop_starts)} video pop(s) at: {formatted_pops} ms")

    # Verify synchronization
    sync_results = []
    perfect_sync_count = 0
    total_analyzed = 0  # Count only beeps included in analysis

    traffic_light = []  # per-pop status: 'green' | 'yellow' | 'red'
    for i, ap in enumerate(audio_pops):
        audio_pop_time = ap['time_ms']
        closest_event = None
        min_diff = float('inf')

        for ev_time in grouped_video_pop_starts:
            diff = abs(audio_pop_time - ev_time)
            if diff < min_diff:
                min_diff = diff
                closest_event = ev_time

        is_synced = min_diff <= tolerance_ms
        # Traffic light based on absolute offset
        if min_diff < 20.0:
            status_color = 'green'
        elif min_diff < 60.0:
            status_color = 'yellow'
        else:
            status_color = 'red'
        traffic_light.append(status_color)

        # Include all beeps in analysis - no more first/last exclusion complexity
        total_analyzed += 1
        if is_synced:
            perfect_sync_count += 1

        sync_results.append({
            'audio_pop_time_ms': audio_pop_time,
            'closest_video_pop_ms': closest_event,
            'difference_ms': min_diff,
            'is_synced': is_synced,
            'included_in_analysis': True,
            'ignore_reason': None,
            'channel': ap.get('channel', 'B'),
            'traffic': status_color
        })

        status = "✅" if is_synced else "❌"
        if closest_event is not None:
            print(f"{status} Pop #{i+1}: audio={audio_pop_time}ms, video={closest_event:.1f}ms, diff={min_diff:.1f}ms")
        else:
            print(f"{status} Pop #{i+1}: audio={audio_pop_time}ms, no matching video pop found")

    # Calculate sync accuracy based only on analyzed beeps
    sync_accuracy = (perfect_sync_count / total_analyzed * 100) if total_analyzed > 0 else 0

    # Additional schedule checks: no event within last 500ms of recording
    try:
        duration_ms = float(video_info['format']['duration']) * 1000.0
    except Exception:
        duration_ms = None
    last_event_within_limit = True
    if duration_ms and grouped_video_pop_starts:
        last_event_within_limit = grouped_video_pop_starts[-1] <= (duration_ms - 500.0)

    return {
    'total_audio_pops': len(audio_pops),
        'total_analyzed': total_analyzed,
        'total_video_pops': len(grouped_video_pop_starts),
        'perfect_sync_count': perfect_sync_count,
        'sync_accuracy_percent': sync_accuracy,
        'tolerance_ms': tolerance_ms,
        'sync_details': sync_results,
        'is_perfectly_synced': sync_accuracy == 100.0,
        'last_event_within_limit': last_event_within_limit,
        'duration_ms': duration_ms,
        'video_pop_times_ms': grouped_video_pop_starts,
        'traffic': traffic_light
    }


def analyze_visual_elements(video_path):
    """Run additional visual checks required by the E2E: pop-area blackness and frame-sequence box.

    Returns dict with two entries: 'pop_area_blackness' and 'frame_sequence_box'.
    Each contains a pass/fail boolean and a concise details string.
    """
    black = verify_video_pop_area_blackness(video_path)
    framebox = verify_frame_sequence_box(video_path)

    black_details = (f"{black['samples']} samples, all dark" if black['pass']
                     else f"{len(black['failures'])}/{black['samples']} bright samples" +
                          (f" (e.g., frame {black['failures'][0]['frame']} mean={black['failures'][0]['mean']:.1f})" if black['failures'] and isinstance(black['failures'][0], dict) and 'mean' in black['failures'][0] else ""))
    if framebox['pass']:
        frame_details = (f"solid {framebox['solid_ratio']*100:.0f}%, colors {framebox['distinct_colors']}, changes {framebox['changes']}/{framebox['frames']}")
    else:
        dev = framebox.get('deviations', [])
        dev_txt = ""
        if dev:
            fidx, tms, expi, goti = dev[0]
            dev_txt = f"; dev at frame {fidx} (~{tms:.1f}ms): exp {expi}, got {goti}"
        frame_details = (f"solid {framebox['solid_ratio']*100:.0f}%, colors {framebox['distinct_colors']}, changes {framebox['changes']}/{framebox['frames']}" +
                         (f" (e.g., {framebox['issues'][0]})" if framebox['issues'] else "") + dev_txt)

    return {
        'pop_area_blackness': {'pass': black['pass'], 'details': black_details},
        'frame_sequence_box': {'pass': framebox['pass'], 'details': frame_details}
    }


def main():
    """Run A/V sync test on the latest generated recording."""

    # Find the latest recording
    test_output_dir = Path(__file__).parent / "test_output"
    recording_path = test_output_dir / "c64_recording.mp4"

    if not recording_path.exists():
        print(f"❌ Recording not found: {recording_path}")
        print("Please run the E2E test first to generate a recording.")
        return 1

    # Run A/V sync analysis
    results = verify_av_sync(recording_path)

    print("\n" + "="*60)
    print("A/V SYNCHRONIZATION CHECK RESULTS")
    print("="*60)

    print(f"📊 Total Audio Pops: {results['total_audio_pops']}")
    print(f"🔍 Analyzed Pops: {results['total_analyzed']}")
    print(f"⬜ Total Video Pops: {results['total_video_pops']}")
    print(f"✅ Perfect Sync Count: {results['perfect_sync_count']} / {results['total_analyzed']}")
    print(f"🎯 Sync Accuracy: {results['sync_accuracy_percent']:.1f}%")
    print(f"⏱️  Tolerance: {results['tolerance_ms']}ms")

    if results['is_perfectly_synced']:
        print("\n🎉 SUCCESS: Perfect A/V synchronization achieved!")
        return 0
    else:
        analyzed_issues = results['total_analyzed'] - results['perfect_sync_count']
        print(f"\n⚠️  WARNING: {analyzed_issues} sync issues detected (out of {results['total_analyzed']} analyzed)")
        print("Consider adjusting timing or investigating sync drift.")
        return 1


if __name__ == "__main__":
    exit(main())
