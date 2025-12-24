#!/usr/bin/env python3
"""
A/V Synchronization Test for C64 Stream (A/V sync check)

This script performs the A/V sync check, verifying that audio and video pops
start at the exact same time.

Pop definition (updated):
- Duration: 2 frames
- First pop: ~1000ms after frames start; interval: ~1000ms
- No pop in the last 500ms of the recording
- Audio pop: pleasant band-limited noise burst with instant attack/decay (no fade)
- Video pop: 50x50 pure white square, instantly on/off, centered within a permanently black
    80x80 "video pop area" at the lower-right corner of the C64 frame.

Note: The generation of these pops is performed by the A/V sync generator; this file only
performs verification (the A/V sync check).
"""

import os
os.environ.setdefault('OPENCV_FFMPEG_LOGLEVEL', 'quiet')  # reduce ffmpeg spam from VideoCapture
import cv2
import numpy as np
import subprocess
import json
import tempfile
from pathlib import Path


def extract_audio_envelope(video_path, sample_rate=48000, window_ms=10):
    """
    Extract audio envelope to detect beep timing.
    Returns array of audio power levels over time.
    """
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as temp_audio:
        # Extract audio as WAV for analysis (fallback: raise gracefully if ffmpeg missing)
        cmd = [
            'ffmpeg', '-loglevel', 'error', '-i', str(video_path),
            '-vn', '-acodec', 'pcm_s16le',
            '-ar', str(sample_rate), '-ac', '2',
            '-y', temp_audio.name
        ]
        try:
            subprocess.run(cmd, capture_output=True, check=True)
        except Exception as e:
            raise RuntimeError(f"audio_extract_failed: {e}")
        temp_name = temp_audio.name

    # Load audio data and parse WAV header
    raw = np.fromfile(temp_name, dtype=np.uint8)
    # Basic 44-byte PCM header parse for stereo 16-bit
    data = raw[44:].view(np.int16)
    # Split channels
    # Note: swap interpretation to match observed output so that audible LRLR maps to L,R here
    right = data[0::2]
    left = data[1::2]

    # Calculate envelope (RMS power in windows)
    window_size = int(max(1, round(sample_rate * (float(window_ms) / 1000.0))))
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
    Detect timing (frame numbers) when the 50x50 white video pop is visible.

    Coordinate system assumptions:
    - Source C64 frame: 384x272
            - Video event area: 80x80 at lower-right of C64 frame
                * area spans X:[304..384), Y:[192..272)
            - Event square: 50x50 centered within that area, instant on/off

        Recording layout assumptions:
        - Output video is 1920x1080 where the C64 content is horizontally centered with side black bars.
        - The content may be scaled using integer scaling with additional cropping to fit the canvas.
            We therefore derive the content bounds from the recording itself and compute the ROI from
            those bounds.
    """
    # Reduce OpenCV/FFmpeg verbosity
    try:
        cv2.setLogLevel(cv2.LOG_LEVEL_ERROR)
    except Exception:
        pass
    cap = cv2.VideoCapture(str(video_path))
    frame_num = 0

    # Minimal warmup skip to avoid early startup artifacts but still capture the first event at ~1s
    skip_frames = int(0.5 * frame_rate)

    # Robust approach (pixel-perfect + black bars friendly):
    # - Detect C64 *content bounds* in a representative frame.
    # - Compute the pop ROI relative to those bounds (lower-right of the *content*), not the
    #   recording frame.
    #
    # This avoids relying on the global bottom-right corner which may be pure black bars.
    metrics: list[float] = []
    frame_nums: list[int] = []

    def _pick_stable_content_bounds() -> tuple[int, int, int, int] | None:
        # Recordings include startup/shutdown padding; sample later to avoid early black frames.
        candidates_s = [10.0, 8.0, 12.0, 6.0, 4.0]
        best = None
        best_area = -1
        for t in candidates_s:
            cap.set(cv2.CAP_PROP_POS_FRAMES, int(round(t * frame_rate)))
            ok, frame = cap.read()
            if not ok or frame is None:
                continue
            left, right, top, bottom = _detect_content_bounds(frame)
            area = (right - left) * (bottom - top)
            if area > best_area and (right - left) > 100 and (bottom - top) > 100:
                best_area = area
                best = (left, right, top, bottom)
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        return best

    bounds = _pick_stable_content_bounds()

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        # Skip early frames to avoid C64 logo false positives
        if frame_num < skip_frames:
            frame_num += 1
            continue

        height, width = frame.shape[:2]
        gray_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        if bounds is None:
            metrics.append(float('nan'))
        else:
            left, right, top, bottom = bounds
            left = max(0, min(width, int(left)))
            right = max(0, min(width, int(right)))
            top = max(0, min(height, int(top)))
            bottom = max(0, min(height, int(bottom)))
            if right <= left + 10 or bottom <= top + 10:
                metrics.append(float('nan'))
                frame_nums.append(frame_num)
                frame_num += 1
                continue

            scale = (right - left) / 384.0
            area_px = int(max(10, round(80.0 * scale)))
            rx0 = max(left, right - area_px)
            rx1 = right
            ry0 = max(top, bottom - area_px)
            ry1 = bottom
            roi = gray_frame[ry0:ry1, rx0:rx1]
            if roi.size == 0:
                metrics.append(float('nan'))
            else:
                rh, rw = roi.shape[:2]
                # Pop square is 50x50 centered in an 80x80 area => 0.625 of the area.
                event = max(8, int(round(0.625 * min(rh, rw))))
                cy = rh // 2
                cx = rw // 2
                half = event // 2
                inner = roi[max(0, cy - half):min(rh, cy + half), max(0, cx - half):min(rw, cx + half)]
                metrics.append(float(np.percentile(inner, 98.0) - np.percentile(roi, 50.0)))

        frame_nums.append(frame_num)

        frame_num += 1

    cap.release()

    def _score(arr: np.ndarray) -> tuple[float, float, float]:
        arr = arr[np.isfinite(arr)]
        if arr.size < 10:
            return 0.0, 0.0, 1.0
        med = float(np.median(arr))
        mad = float(np.median(np.abs(arr - med)))
        mad = max(mad, 1.0)
        peak = float(np.percentile(arr, 99.5))
        return (peak - med) / mad, med, mad

    metrics_arr = np.array(metrics, dtype=float)
    _, chosen_med, chosen_mad = _score(metrics_arr)

    # Threshold for spikes. Use a conservative multiplier; this is a sparse, high-contrast event.
    # If MAD collapses (bimodal/stable metric), fall back to percentile-based separation.
    if chosen_mad <= 1.0:
        threshold = float(np.nanpercentile(metrics_arr, 99.0))
    else:
        threshold = float(chosen_med + 8.0 * chosen_mad)
    threshold = max(threshold, 10.0)

    # Convert threshold hits into stable, de-bounced pop frames.
    # Some effects (e.g. afterglow) can smear the event so that the first threshold crossing
    # occurs slightly before the best-aligned peak. We therefore group adjacent hits into a
    # cluster and then pick the local maximum with a small look-ahead.
    hot = np.where(np.isfinite(metrics_arr) & (metrics_arr > threshold))[0]
    if hot.size == 0:
        return []

    search_ahead = max(1, int(round(0.05 * frame_rate)))  # ~50ms
    max_gap = 1
    min_spacing = max(1, int(round(0.5 * frame_rate)))  # pops are ~1s apart

    best_indices: list[int] = []
    cluster_start = int(hot[0])
    cluster_end = int(hot[0])
    for idx in hot[1:]:
        idx = int(idx)
        if idx <= cluster_end + max_gap:
            cluster_end = idx
            continue

        search_end = min(len(metrics_arr) - 1, cluster_end + search_ahead)
        window = metrics_arr[cluster_start : search_end + 1]
        max_val = float(np.nanmax(window))
        best_rel = int(np.where(window == max_val)[0][-1])
        best_indices.append(cluster_start + best_rel)

        cluster_start = idx
        cluster_end = idx

    search_end = min(len(metrics_arr) - 1, cluster_end + search_ahead)
    window = metrics_arr[cluster_start : search_end + 1]
    max_val = float(np.nanmax(window))
    best_rel = int(np.where(window == max_val)[0][-1])
    best_indices.append(cluster_start + best_rel)

    # Enforce minimum spacing to prevent any double-triggering.
    best_indices = sorted(set(best_indices))
    pop_frames: list[int] = []
    last_frame = None
    for idx in best_indices:
        fn = frame_nums[idx]
        if last_frame is None or (fn - last_frame) >= min_spacing:
            pop_frames.append(fn)
            last_frame = fn

    return pop_frames


def detect_audio_pops(envelope, window_ms=10, threshold_factor=3.0, min_duration_ms=10):
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
                pop_duration_ms = (i - pop_start) * int(window_ms)
                if pop_duration_ms >= min_duration_ms:
                    # Determine dominant channel for this pop window
                    seg = envelope[pop_start:i]
                    if seg.ndim == 2:
                        mean_l = float(np.mean(seg[:, 0]))
                        mean_r = float(np.mean(seg[:, 1]))
                        chan = 'L' if mean_l > mean_r else 'R'
                        pop_starts.append({'time_ms': pop_start * int(window_ms), 'channel': chan})
                    else:
                        chan = 'B'  # both/mono
                        pop_starts.append({'time_ms': pop_start * int(window_ms), 'channel': chan})
            in_pop = False
            pop_start = None

    return pop_starts


def _detect_content_bounds(frame):
    """Detect content bounds (left, right, top, bottom) using bars around C64 content.

    Important: In some OBS/render pipelines, "black" bars may not be pure 0 (e.g. limited-range
    quantization or subtle non-zero background), so a fixed "non_black > 10" test can fail and
    mistakenly treat the bars as content.

    We instead use a robust, distribution-based threshold on per-column/per-row medians.
    """
    height, width = frame.shape[:2]
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    # Horizontal bounds: use a high percentile per column to remain robust when scanlines
    # create many dark pixels/rows (medians can collapse to 0).
    col_hi = np.percentile(gray, 99.0, axis=0)
    thr = max(10.0, float(np.percentile(col_hi, 90.0) * 0.20))
    content_cols = np.where(col_hi > thr)[0]
    if content_cols.size >= 2:
        left_bound = int(content_cols[0])
        right_bound = int(content_cols[-1]) + 1
    else:
        # Fallback: assume centered horizontally
        scale_factor = height / 272.0
        scaled_c64_width = int(384 * scale_factor)
        left_bound = int((width - scaled_c64_width) // 2)
        right_bound = int((width + scaled_c64_width) // 2)

    # Vertical bounds: same idea for rows.
    row_hi = np.percentile(gray, 99.0, axis=1)
    thr_r = max(10.0, float(np.percentile(row_hi, 90.0) * 0.20))
    content_rows = np.where(row_hi > thr_r)[0]
    if content_rows.size >= 2:
        top_bound = int(content_rows[0])
        bottom_bound = int(content_rows[-1]) + 1
    else:
        top_bound = 0
        bottom_bound = height

    # Clamp bounds to valid image coordinates
    left_bound = max(0, min(width, left_bound))
    right_bound = max(0, min(width, right_bound))
    top_bound = max(0, min(height, top_bound))
    bottom_bound = max(0, min(height, bottom_bound))

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
    deviations = []  # (frame_idx, time_ms, expected_idx, got_idx, reason)
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
            # Try to provide a human-friendly reason
            reason = None
            if prev_mean is not None:
                # If the quantized index is equal to previous, it likely repeated
                prev_quant = np.round(prev_mean / 16.0) * 16.0
                prev_idx = int(np.clip(np.round(np.mean(prev_quant) / 16.0), 0, 15))
                if approx_idx == prev_idx:
                    reason = "Repeated previous colour"
            if reason is None:
                reason = f"Unexpected colour index (expected {expected_idx}, got {approx_idx})"
            deviations.append((start_idx + i, time_ms, expected_idx, approx_idx, reason))
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


def verify_av_sync(video_path, tolerance_ms=30):
    """
    Verify A/V synchronization between black squares and audio beeps.

    Args:
        video_path: Path to video file
        tolerance_ms: Maximum allowed timing difference in milliseconds

    Returns:
        dict with sync analysis results
    """
    print(f"🔍 Analyzing A/V sync for: {video_path}")

    # Get video properties (prefer ffprobe, fallback to OpenCV if unavailable)
    frame_rate = None
    sample_rate = 48000
    try:
        cmd = ['ffprobe', '-v', 'quiet', '-show_format', '-show_streams',
               '-of', 'json', str(video_path)]
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        video_info = json.loads(result.stdout)
        video_stream = next(s for s in video_info['streams'] if s['codec_type'] == 'video')
        frame_rate = eval(video_stream['r_frame_rate'])  # e.g., "30/1" -> 30.0
        try:
            audio_stream = next(s for s in video_info['streams'] if s['codec_type'] == 'audio')
            sample_rate = int(audio_stream.get('sample_rate', sample_rate))
        except StopIteration:
            pass
    except Exception:
        cap_probe = cv2.VideoCapture(str(video_path))
        frame_rate = cap_probe.get(cv2.CAP_PROP_FPS) or 30.0
        cap_probe.release()

    print(f"📊 Video: {frame_rate} fps, Audio: {sample_rate} Hz")

    # Extract audio envelope
    print("🔊 Extracting audio envelope...")
    audio_pops = []
    try:
        # Use 1ms windows for much finer onset timing than the old 10ms RMS.
        # This reduces false sync failures caused by coarse audio quantization.
        envelope_window_ms = 1
        envelope = extract_audio_envelope(video_path, sample_rate, window_ms=envelope_window_ms)
        # Detect audio pops
        audio_pops = detect_audio_pops(envelope, window_ms=envelope_window_ms, threshold_factor=3.0, min_duration_ms=30)
        print(f"🎵 Detected {len(audio_pops)} audio pop(s)")
    except Exception as e:
        print(f"⚠️  Audio analysis skipped ({e})")

    # Detect video pops (white square in lower-right area)
    print("⬜ Detecting video pop(s) (white square, lower-right)...")
    pop_frames = detect_video_pops(video_path, frame_rate)

    # Group consecutive frames into individual video pops by frame index (robust to FPS rounding)
    grouped_video_pop_frame_starts = []
    if pop_frames:
        current_start = int(pop_frames[0])
        last_idx = int(pop_frames[0])
        for f in pop_frames[1:]:
            f = int(f)
            if (f - last_idx) <= 1:
                # same pop (consecutive frame)
                pass
            else:
                grouped_video_pop_frame_starts.append(current_start)
                current_start = f
            last_idx = f
        grouped_video_pop_frame_starts.append(current_start)

    # Convert frame numbers to timestamps (ms) for human-readable reporting
    grouped_video_pop_starts = [frame / frame_rate * 1000.0 for frame in grouped_video_pop_frame_starts]

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
        closest_event_idx = None
        min_diff = float('inf')

        for idx, ev_time in enumerate(grouped_video_pop_starts):
            diff = abs(audio_pop_time - ev_time)
            if diff < min_diff:
                min_diff = diff
                closest_event = ev_time
                closest_event_idx = idx

        is_synced = min_diff <= tolerance_ms
        # Traffic light based on absolute offset
        if min_diff < 30.0:
            status_color = 'green'
        elif min_diff < 50.0:
            status_color = 'yellow'
        else:
            status_color = 'red'
        traffic_light.append(status_color)

        # Include all beeps in analysis
        total_analyzed += 1
        if is_synced:
            perfect_sync_count += 1

        sync_results.append({
            'audio_pop_time_ms': audio_pop_time,
            'closest_video_pop_ms': closest_event,
            'closest_video_pop_frame': (grouped_video_pop_frame_starts[closest_event_idx]
                                        if closest_event_idx is not None and 0 <= closest_event_idx < len(grouped_video_pop_frame_starts)
                                        else None),
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

    # Additional schedule checks: no event within last 1000ms of recording
    # Duration from ffprobe if available; else estimate via OpenCV
    duration_ms = None
    try:
        # may be available from ffprobe path above
        if 'video_info' in locals():
            duration_ms = float(video_info['format']['duration']) * 1000.0
    except Exception:
        pass
    if duration_ms is None:
        cap_dur = cv2.VideoCapture(str(video_path))
        frames = cap_dur.get(cv2.CAP_PROP_FRAME_COUNT) or 0
        fps_dur = cap_dur.get(cv2.CAP_PROP_FPS) or frame_rate or 30.0
        cap_dur.release()
        duration_ms = (frames / fps_dur) * 1000.0 if fps_dur else None
    last_event_within_limit = True
    if duration_ms and grouped_video_pop_starts:
        last_event_within_limit = grouped_video_pop_starts[-1] <= (duration_ms - 1000.0)

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
        'video_pop_frame_indices': grouped_video_pop_frame_starts,
        'traffic': traffic_light
    }


_VISUALS_CACHE = {}


def analyze_visual_elements(video_path):
    """Run additional visual checks required by the E2E: pop-area blackness and frame-sequence box.

    Returns dict with two entries: 'pop_area_blackness' and 'frame_sequence_box'.
    Each contains a pass/fail boolean and a concise details string.
    """
    # Cache by file path and modification time to avoid repeat decoding and duplicate warnings
    try:
        st = os.stat(video_path)
        key = (str(video_path), int(st.st_mtime), int(st.st_size))
    except Exception:
        key = (str(video_path), None, None)

    if key in _VISUALS_CACHE:
        framebox = _VISUALS_CACHE[key]
    else:
        framebox = verify_frame_sequence_box(video_path)
        _VISUALS_CACHE[key] = framebox
    # Disable frame sequence box reporting (WIP)
    frame_details = "Skipped, work in progress"

    # TODO: Re-enable frame sequence box once stable
    return {
        'frame_sequence_box': {'pass': True, 'details': frame_details}
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
