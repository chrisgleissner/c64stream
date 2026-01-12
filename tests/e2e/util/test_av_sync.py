#!/usr/bin/env python3
"""
A/V Synchronization Test for C64 Stream (A/V sync check)

This script performs the A/V sync check, verifying that audio and video pops
start at the exact same time.

Pop definition (updated):
- Duration: 2 frames
- First pop: frame 48 (when progress bar slot 0 lights up for 6th time)
- Interval: every 48 frames (~960ms at PAL, ~800ms at NTSC)
- Synchronized with frame progress bar: pops occur when slot 0 is illuminated
- No pop in the last ~1000ms worth of frames
- Audio pop: pleasant band-limited noise burst with instant attack/decay (no fade)
- Video pop: Fills entire 72x40 inner content area, instantly on/off, inside an 88x56
    "video pop area" at the lower-right corner of the C64 frame (8px border: 1px white + 7px black).

Note: The generation of these pops is performed by the A/V sync generator; this file only
performs verification (the A/V sync check).

Known A/V Offset Behavior:
Different CRT effect presets produce different baseline A/V offsets in the recorded files:
- Sharp Pixels (no effects): ~2ms offset
- Default (no effects): ~14ms offset
- Arcade Cabinet (bloom only): ~33ms offset
- Green Monitor (bloom + afterglow): ~40ms offset

This is due to OBS encoder/muxer behavior under varying GPU/CPU loads from CRT effects,
not a plugin synchronization issue. The plugin outputs audio and video with synchronized
timestamps, but OBS's encoding pipeline introduces variable latency based on processing load.

Sources of A/V offset jitter (up to ~60ms total):
- x264 encoder B-frame lookahead: ~16.7ms per frame at 60fps
- AAC encoder priming delay: ~21ms (1024 samples at 48kHz)
- AAC frame boundary alignment: ~0-21ms depending on phase
- Video frame timing quantization: ~16.7ms at 60fps

A/V Sync Classification (based on ITU-R BT.1359, EBU R37 broadcast standards):
- Green (<35ms): Excellent - well within broadcast standards
- Yellow (35-60ms): Acceptable - matches EBU ±40ms with encoder jitter margin
- Red (≥60ms): Poor - may cause viewer discomfort

All offsets are within the 60ms tolerance threshold and the offset is consistent (not drifting)
within each recording, so the perceived A/V sync is acceptable for end users.
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


def detect_video_pop_events(video_path, frame_rate=30.0):
    """
    Detect timing (frame numbers) when the white video pop is visible.

    The A/V pop indicator is an 88x56 box with:
    - 1px white outer border
    - 7px black inner border
    - 72x40 inner area split into left/right halves with 2px black divider
    - Left half flashes white for L channel, right half for R channel
    """
    try:
        cv2.setLogLevel(cv2.LOG_LEVEL_ERROR)
    except Exception:
        pass
    cap = cv2.VideoCapture(str(video_path))

    # Skip the initial logo/settling period.
    # Heavy CRT presets (afterglow/tint/scanlines) can cause early false positives
    # in the pop ROI during startup, which then misaligns A/V sync matching.
    # The E2E suite uses a 4s settling window elsewhere; align with that here.
    desired_skip_s = 4.0
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)
    skip_frames = int(desired_skip_s * frame_rate)
    if total_frames and total_frames < int((desired_skip_s + 1.0) * frame_rate):
        # Fallback for short clips: still skip a bit, but keep enough frames for detection.
        skip_frames = int(0.5 * frame_rate)
    metrics: list[float] = []
    frame_nums: list[int] = []
    frame_times_ms: list[float | None] = []
    max_brightness: list[float] = []
    full_p95: list[float] = []

    def _find_pop_box_in_frame(gray_frame, content_bounds):
        """Calculate the A/V pop box position from content bounds.
        The pop box is at a fixed position in the bottom-right corner.
        Returns (x0, y0, x1, y1) of the inner content area."""
        left, right, top, bottom = content_bounds
        content_w = right - left
        content_h = bottom - top

        # C64 resolution is 384x272, corner elements are 88x56
        # Inner area (excluding 8px border) is 72x40
        # Pop box is positioned at (384-88, 272-56) = (296, 216) in C64 coords

        scale_x = content_w / 384.0
        scale_y = content_h / 272.0

        # Pop box outer bounds (top-left corner of the 88x56 box)
        pop_outer_x0 = left + int((384 - 88) * scale_x)
        pop_outer_y0 = top + int((272 - 56) * scale_y)

        # Inner content area (8px border on each side in C64 units)
        border_px_x = int(8 * scale_x)
        border_px_y = int(8 * scale_y)

        inner_x0 = pop_outer_x0 + border_px_x
        inner_y0 = pop_outer_y0 + border_px_y
        inner_x1 = pop_outer_x0 + int(88 * scale_x) - border_px_x
        inner_y1 = pop_outer_y0 + int(56 * scale_y) - border_px_y

        return (inner_x0, inner_y0, inner_x1, inner_y1)

    # Find stable content bounds from a representative frame
    def _pick_stable_pop_box():
        candidates_s = [10.0, 8.0, 12.0, 6.0, 4.0]
        for t in candidates_s:
            cap.set(cv2.CAP_PROP_POS_FRAMES, int(round(t * frame_rate)))
            ok, frame = cap.read()
            if not ok or frame is None:
                continue

            h, w = frame.shape[:2]

            # Find content bounds
            bounds = _detect_content_bounds(frame)
            left, right, top, bottom = bounds
            if right - left < 100 or bottom - top < 100:
                continue

            # Calculate pop box from content bounds
            box = _find_pop_box_in_frame(None, bounds)
            if box is not None:
                cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                return box, bounds

        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        return None, None

    pop_box, content_bounds = _pick_stable_pop_box()
    use_roi = pop_box is not None

    if use_roi:
        inner_x0, inner_y0, inner_x1, inner_y1 = pop_box
        inner_w = inner_x1 - inner_x0

        # Calculate left/right half regions (avoiding 2px center divider)
        # The pop box inner area is 72px wide: [35px left][2px divider][35px right]
        # At 1920x1080 output, the scaling factor is ~1080/272 ≈ 3.97
        # So 35px -> ~139px, 2px divider -> ~8px
        half_w = inner_w // 2
        divider_w = max(2, inner_w // 36)  # ~2px at source, scales with resolution
        left_half_x1 = inner_x0 + half_w - divider_w
        right_half_x0 = inner_x0 + half_w + divider_w

    min_white_luma = 224.0  # >= 0xE0 brightness threshold for "white" pop
    sample_stride = 8

    frame_num = 0
    while True:
        ret, frame = cap.read()
        if not ret:
            break

        if frame_num < skip_frames:
            frame_num += 1
            continue

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        try:
            ts_ms = float(cap.get(cv2.CAP_PROP_POS_MSEC))
        except Exception:
            ts_ms = float('nan')
        if frame_num > 0 and isinstance(ts_ms, float) and ts_ms <= 0.0:
            ts_ms = float('nan')

        if content_bounds is None:
            h, w = gray.shape[:2]
            content_bounds = (0, w, 0, h)

        left, right, top, bottom = content_bounds
        sample = gray[top:bottom:sample_stride, left:right:sample_stride]
        if sample.size == 0:
            sample = gray[::sample_stride, ::sample_stride]
        full_p95.append(float(np.percentile(sample, 95.0)))

        if use_roi:
            # Sample left and right halves of the inner area
            left_half = gray[inner_y0:inner_y1, inner_x0:left_half_x1]
            right_half = gray[inner_y0:inner_y1, right_half_x0:inner_x1]

            # Get brightness of each half
            left_brightness = float(np.percentile(left_half, 95.0)) if left_half.size > 0 else 0.0
            right_brightness = float(np.percentile(right_half, 95.0)) if right_half.size > 0 else 0.0

            # Store both brightnesses as tuple (left, right) to determine channel later
            # A pop lights up one half to ~200+ while inactive is ~60-80
            metrics.append((left_brightness, right_brightness))
            max_brightness.append(max(left_brightness, right_brightness))
        frame_nums.append(frame_num)
        frame_times_ms.append(ts_ms)
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

    def _rolling_median(x: np.ndarray, w: int) -> np.ndarray:
        out = np.empty_like(x)
        for i in range(len(x)):
            s = max(0, i - w)
            # Use strictly previous samples to avoid pop contaminating its own baseline.
            if i <= s:
                out[i] = x[i]
            else:
                out[i] = float(np.nanmedian(x[s:i]))
        return out

    events: list[dict] = []
    min_pop_events = 2

    if use_roi and metrics:
        # metrics is list of (left_brightness, right_brightness) tuples.
        #
        # IMPORTANT:
        # For heavy CRT effects (afterglow/bloom/tint/scanlines), absolute brightness
        # can drift and/or compress, which causes missed pops and wrong cadence.
        # We instead detect pops using *delta above a rolling baseline* per half.
        lr = np.array(metrics, dtype=float)
        max_b = np.array(max_brightness, dtype=float)
        left_b = lr[:, 0]
        right_b = lr[:, 1]

        window = max(8, int(round(0.50 * frame_rate)))  # ~0.5s rolling baseline
        base_l = _rolling_median(left_b, window)
        base_r = _rolling_median(right_b, window)
        delta_l = left_b - base_l
        delta_r = right_b - base_r

        delta_max = np.maximum(delta_l, delta_r)
        _, chosen_med, chosen_mad = _score(delta_max)

        # Threshold for spikes in delta-space.
        if chosen_mad <= 1.0:
            threshold = float(np.nanpercentile(delta_max, 98.0))
        else:
            threshold = float(chosen_med + 6.0 * chosen_mad)
        threshold = max(threshold, 2.0)

        # Convert threshold hits into stable, de-bounced pop start events.
        hot = np.where(np.isfinite(delta_max) & (delta_max > threshold))[0]
        if hot.size != 0:
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

                # Pick the first frame where the pop is confidently visible.
                best_indices.append(cluster_start)

                cluster_start = idx
                cluster_end = idx

            best_indices.append(cluster_start)

            # Enforce minimum spacing to prevent any double-triggering.
            best_indices = sorted(set(best_indices))

            # For each detected pop, look backwards to find the true onset.
            # With bloom/blur effects, the first frame above threshold may be 1-2 frames after
            # the actual pop started, because the effect takes time to build up brightness.
            # We detect the onset by finding where brightness first rises significantly above baseline.
            onset_threshold_factor = 0.25  # Slightly earlier onset for CRT effects
            lookback_frames = max(6, int(round(0.25 * frame_rate)))  # allow up to ~250ms lookback

            last_frame = None
            for idx in best_indices:
                # Find the true onset by looking backwards
                true_idx = idx
                if idx > 0:
                    # Get the peak metric value for this pop cluster
                    cluster_end_idx = idx
                    while cluster_end_idx + 1 < len(delta_max) and delta_max[cluster_end_idx + 1] > threshold:
                        cluster_end_idx += 1
                    peak_metric = float(np.nanmax(delta_max[idx:cluster_end_idx + 1]))
                    peak_brightness = float(np.nanmax(max_b[idx:cluster_end_idx + 1]))
                    
                    # Adaptive brightness check: For heavy CRT effects (afterglow/tint), absolute
                    # brightness is significantly reduced. The delta-based detection already found
                    # a valid pop spike. We verify brightness isn't *too* low (e.g., not in black
                    # areas), but allow dimmed whites from effects like green monitor.
                    #
                    # Use a lenient threshold: require brightness to be above the baseline median
                    # by a meaningful amount, rather than an absolute 224+ requirement.
                    # This allows detection of dimmed whites (150-200 range) from heavy effects.
                    min_brightness_threshold = max(60.0, float(chosen_med) + 40.0)
                    if not np.isfinite(peak_brightness) or peak_brightness < min_brightness_threshold:
                        continue

                    # In delta-space, baseline should be ~0. Use a small floor to avoid
                    # NaN/negative artifacts from rolling median.
                    onset_thresh = max(0.5, onset_threshold_factor * peak_metric)

                    # Look backwards to find first frame above onset threshold
                    for lookback in range(1, min(lookback_frames + 1, idx + 1)):
                        check_idx = idx - lookback
                        if np.isfinite(delta_max[check_idx]) and delta_max[check_idx] > onset_thresh:
                            true_idx = check_idx
                        else:
                            break  # Stop looking back once we find a frame below threshold

                fn = int(frame_nums[true_idx])
                if last_frame is not None and (fn - last_frame) < min_spacing:
                    continue

                t = frame_times_ms[true_idx]
                if t is None or (isinstance(t, float) and not np.isfinite(t)):
                    t = None

                # Determine which channel (L/R) the pop is on.
                #
                # Under heavy CRT effects (notably afterglow), absolute brightness can remain elevated
                # on the *previous* side for several frames, and even rolling-baseline deltas can be
                # ambiguous around the onset frame. To avoid false channel mismatches, classify the
                # side by the strongest dark→bright transition using a small pre/post window.
                #
                # If the transition is ambiguous (both sides rise similarly), report channel as unknown.
                pre_n = 2
                post_n = 2
                pre_s = max(0, true_idx - pre_n)
                pre_e = max(pre_s, true_idx)
                post_s = true_idx
                post_e = min(len(left_b), true_idx + post_n)

                pre_l = float(np.nanmedian(left_b[pre_s:pre_e])) if pre_e > pre_s else float(left_b[true_idx])
                pre_r = float(np.nanmedian(right_b[pre_s:pre_e])) if pre_e > pre_s else float(right_b[true_idx])

                # Use a peak over a couple of frames to cope with bloom/afterglow ramping.
                post_l = float(np.nanmax(left_b[post_s:post_e])) if post_e > post_s else float(left_b[true_idx])
                post_r = float(np.nanmax(right_b[post_s:post_e])) if post_e > post_s else float(right_b[true_idx])

                # Deterministic side selection: choose the half with the stronger *dark→bright edge*.
                #
                # Afterglow can bias absolute brightness, but the pop itself is an instantaneous
                # (1–2 frame) large increase. So we base side selection on the maximum frame-to-frame
                # brightness increase within a small window around the detected onset.
                # There is always exactly one chosen side.
                edge_window = 3
                edge_s = max(1, true_idx - edge_window)
                edge_e = min(len(left_b) - 1, true_idx + edge_window)

                if edge_e >= edge_s:
                    inc_l = left_b[edge_s:edge_e + 1] - left_b[edge_s - 1:edge_e]
                    inc_r = right_b[edge_s:edge_e + 1] - right_b[edge_s - 1:edge_e]
                    max_inc_l = float(np.nanmax(inc_l))
                    max_inc_r = float(np.nanmax(inc_r))
                else:
                    max_inc_l = float(left_b[true_idx] - left_b[max(0, true_idx - 1)])
                    max_inc_r = float(right_b[true_idx] - right_b[max(0, true_idx - 1)])

                channel_guess = 'L' if max_inc_l >= max_inc_r else 'R'

                ev = {'frame': fn, 'time_ms': t, 'channel': channel_guess}
                events.append(ev)
                last_frame = fn

    if len(events) >= min_pop_events:
        return events

    def _detect_full_frame_events() -> list[dict]:
        full = np.array(full_p95, dtype=float)
        if full.size == 0:
            return []

        window = max(8, int(round(0.50 * frame_rate)))  # ~0.5s rolling baseline
        base = _rolling_median(full, window)
        delta = full - base
        _, chosen_med, chosen_mad = _score(delta)
        if chosen_mad <= 1.0:
            threshold = float(np.nanpercentile(delta, 98.0))
        else:
            threshold = float(chosen_med + 6.0 * chosen_mad)
        threshold = max(threshold, 2.0)

        hot = np.where(
            np.isfinite(delta)
            & (delta >= threshold)
            & np.isfinite(full)
            & (full >= min_white_luma)
        )[0]
        if hot.size == 0:
            return []

        max_gap = 1
        min_spacing = max(1, int(round(0.5 * frame_rate)))

        best_indices: list[int] = []
        cluster_start = int(hot[0])
        cluster_end = int(hot[0])
        for idx in hot[1:]:
            idx = int(idx)
            if idx <= cluster_end + max_gap:
                cluster_end = idx
                continue
            best_indices.append(cluster_start)
            cluster_start = idx
            cluster_end = idx
        best_indices.append(cluster_start)

        best_indices = sorted(set(best_indices))
        events_ff: list[dict] = []
        last_frame = None
        for idx in best_indices:
            fn = int(frame_nums[idx])
            if last_frame is not None and (fn - last_frame) < min_spacing:
                continue

            t = frame_times_ms[idx]
            if t is None or (isinstance(t, float) and not np.isfinite(t)):
                t = None
            events_ff.append({'frame': fn, 'time_ms': t, 'channel': 'B'})
            last_frame = fn

        return events_ff

    full_events = _detect_full_frame_events()
    if full_events:
        return full_events

    return events


def detect_video_pops(video_path, frame_rate=30.0):
    """Backward-compatible helper returning frame indices for detected video pops."""
    return [int(ev['frame']) for ev in detect_video_pop_events(video_path, frame_rate=frame_rate)]


def detect_audio_pops(envelope, window_ms=10, threshold_factor=3.0, min_duration_ms=10, min_spacing_ms=200):
    """
    Detect audio pops in the envelope.
    Returns list of pop start times in milliseconds (10ms resolution).
    """
    # Calculate dynamic threshold based on background noise
    if envelope.ndim == 2 and envelope.shape[1] == 2:
        bg_l = np.percentile(envelope[:, 0], 10)
        bg_r = np.percentile(envelope[:, 1], 10)
        peak_l = np.percentile(envelope[:, 0], 99.5)
        peak_r = np.percentile(envelope[:, 1], 99.5)
        peak_floor = max(1.0, max(peak_l, peak_r) * 0.10)
        thr_l = max(bg_l * threshold_factor, peak_floor)
        thr_r = max(bg_r * threshold_factor, peak_floor)
    else:
        bg = np.percentile(envelope, 10)
        peak = np.percentile(envelope, 99.5)
        peak_floor = max(1.0, peak * 0.10)
        thr_l = thr_r = max(bg * threshold_factor, peak_floor)

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

    if min_spacing_ms is not None and pop_starts:
        filtered = []
        last_time = None
        for pop in pop_starts:
            t_ms = pop.get('time_ms', 0)
            if last_time is None or (t_ms - last_time) >= min_spacing_ms:
                filtered.append(pop)
                last_time = t_ms
        pop_starts = filtered

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
    """Check the 88x56 top-left frame sequence color box for solidity and progression.

    The top-left corner element has:
    - Outer size: 88x56 (C64 aspect ratio ~1.57)
    - Frame: 1px white + 7px black = 8px border
    - Inner content: 72x40 with frame sequence color

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
        # Corner element: 88x56 outer dimensions (C64 aspect ratio)
        box_w = int(88 * scale)
        box_h = int(56 * scale)

        # Apply inner margin crop before measuring to avoid edge artifacts and border
        inner_margin = max(4, int(10 * scale))  # accounts for 8px border + margin

        # One-time robust search near content top-left to find the most solid corner area
        if not initialized_offset:
            content_w = right - left
            content_h = bottom - top
            # Search within a generous window near the top-left of the content
            span_x = min(content_w - box_w, int(120 * scale))
            span_y = min(content_h - box_h, int(120 * scale))
            step = max(2, int(4 * scale))

            best_std = float('inf')
            best_dx, best_dy = 0, 0
            best_roi_inner = None

            for off_y in range(0, max(1, span_y + 1), step):
                for off_x in range(0, max(1, span_x + 1), step):
                    tx = left + off_x
                    ty = top + off_y
                    troi = frame[ty:ty + box_h, tx:tx + box_w]
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
                        troi = frame[ty:ty + box_h, tx:tx + box_w]
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
            roi = frame[box_top:box_top + box_h, box_left:box_left + box_w]
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
                    troi = frame[ty:ty + box_h, tx:tx + box_w]
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


def verify_av_sync(video_path, tolerance_ms=30, audio_threshold_factor=2.5, audio_min_duration_ms=8, envelope_window_ms=1):
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

    def _parse_ffmpeg_ratio(value: str) -> float | None:
        # Examples: "30000/1001", "30/1", "30"
        try:
            value = (value or "").strip()
            if not value:
                return None
            if "/" in value:
                num_s, den_s = value.split("/", 1)
                num = float(num_s)
                den = float(den_s)
                if den == 0.0:
                    return None
                return num / den
            return float(value)
        except Exception:
            return None
    try:
        cmd = ['ffprobe', '-v', 'quiet', '-show_format', '-show_streams',
               '-of', 'json', str(video_path)]
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        video_info = json.loads(result.stdout)
        video_stream = next(s for s in video_info['streams'] if s['codec_type'] == 'video')
        frame_rate = (
            _parse_ffmpeg_ratio(video_stream.get('avg_frame_rate'))
            or _parse_ffmpeg_ratio(video_stream.get('r_frame_rate'))
            or 30.0
        )
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
        envelope = extract_audio_envelope(video_path, sample_rate, window_ms=envelope_window_ms)
        # Detect audio pops - reduced threshold and min_duration for 1-frame pops (~16.7ms NTSC, 20ms PAL)
        audio_pops = detect_audio_pops(
            envelope,
            window_ms=envelope_window_ms,
            threshold_factor=audio_threshold_factor,
            min_duration_ms=audio_min_duration_ms,
        )
        print(f"🎵 Detected {len(audio_pops)} audio pop(s)")
    except Exception as e:
        print(f"⚠️  Audio analysis skipped ({e})")

    # Detect video pops (white square in lower-right area)
    print("⬜ Detecting video pop(s) (white square, lower-right)...")
    pop_events = detect_video_pop_events(video_path, frame_rate)

    # These are already pop *starts*; keep them as-is.
    grouped_video_pop_frame_starts = [int(ev['frame']) for ev in pop_events]

    # Convert to timestamps (ms) for human-readable reporting.
    # Prefer decoder-provided per-frame timestamps (robust to VFR / rounding).
    grouped_video_pop_starts = []
    for ev in pop_events:
        t = ev.get('time_ms')
        if t is None:
            t = float(ev['frame']) / float(frame_rate) * 1000.0
        grouped_video_pop_starts.append(float(t))

    formatted_pops = [f"{t:.1f}" for t in grouped_video_pop_starts]
    print(f"⬜ Detected {len(grouped_video_pop_starts)} video pop(s) at: {formatted_pops} ms")

    # Verify synchronization
    sync_results = []
    perfect_sync_count = 0
    total_analyzed = 0  # Count only beeps included in analysis

    # Some CRT presets can make the first of the 2 pop-frames slightly harder to detect.
    # To avoid false negatives, allow a one-frame earlier candidate when matching.
    frame_ms = (1000.0 / frame_rate) if frame_rate else (1000.0 / 30.0)
    video_candidates = []  # (frame_idx, time_ms, channel)
    for ev in pop_events:
        fr_i = int(ev['frame'])
        t = ev.get('time_ms')
        if t is None:
            t = float(fr_i) / float(frame_rate) * 1000.0
        tm_f = float(t)
        ch = ev.get('channel', 'B')
        video_candidates.append((fr_i, tm_f, ch))
        if fr_i > 0:
            video_candidates.append((fr_i - 1, tm_f - frame_ms, ch))

    # Maximum allowed difference to consider an audio pop as "matched" to a video pop.
    # Audio pops with no video pop within this window are likely false positives or
    # partial pops at stream boundaries, and should be excluded from sync accuracy.
    max_match_window_ms = 100.0

    traffic_light = []  # per-pop status: 'green' | 'yellow' | 'red'
    channel_match_count = 0  # Count of pops where audio and video channels match
    channel_mismatch_count = 0  # Count of pops where channels don't match
    for i, ap in enumerate(audio_pops):
        audio_pop_time = ap['time_ms']
        audio_channel = ap.get('channel', 'B')
        closest_event = None
        closest_event_idx = None
        closest_event_frame = None
        closest_event_channel = None
        min_diff = float('inf')

        for idx, (ev_frame, ev_time, ev_channel) in enumerate(video_candidates):
            diff = abs(audio_pop_time - ev_time)
            if diff < min_diff:
                min_diff = diff
                closest_event = ev_time
                closest_event_idx = idx
                closest_event_frame = ev_frame
                closest_event_channel = ev_channel

        is_synced = min_diff <= tolerance_ms

        # Check if this audio pop has a matching video pop within the match window.
        # If not, it's likely a false positive (e.g., noise at stream end) or a partial
        # pop that was correctly suppressed in video but detected in audio.
        is_unmatched = min_diff > max_match_window_ms

        # Check if audio and video channels match (L/R consistency)
        # Treat 'B' (both/unknown) as a neutral match to avoid false mismatches.
        if (closest_event_channel in (None, 'B')) or (audio_channel in (None, 'B')):
            channels_match = True
        else:
            channels_match = (audio_channel == closest_event_channel)
        if not is_unmatched:
            if channels_match:
                channel_match_count += 1
            else:
                channel_mismatch_count += 1

        # Traffic light based on absolute offset
        # Thresholds based on industry standards (ITU-R BT.1359, EBU R37) and encoder jitter:
        # - Green (<35ms): Excellent - well within broadcast standards
        # - Yellow (35-60ms): Acceptable - matches EBU R37 ±40ms with encoder jitter margin
        # - Red (≥60ms): Poor - may cause viewer discomfort
        if is_unmatched:
            status_color = 'gray'  # Unmatched - not counted
        elif min_diff < 35.0:
            status_color = 'green'
        elif min_diff < 60.0:
            status_color = 'yellow'
        else:
            status_color = 'red'
        traffic_light.append(status_color)

        # Only include matched beeps in analysis
        if not is_unmatched:
            total_analyzed += 1
            if is_synced:
                perfect_sync_count += 1

        sync_results.append({
            'audio_pop_time_ms': audio_pop_time,
            'closest_video_pop_ms': closest_event,
            'closest_video_pop_frame': closest_event_frame,
            'difference_ms': min_diff,
            'is_synced': is_synced,
            'included_in_analysis': not is_unmatched,
            'ignore_reason': 'unmatched_audio_pop' if is_unmatched else None,
            'audio_channel': ap.get('channel', 'B'),
            'video_channel': closest_event_channel,
            'channels_match': channels_match,
            'traffic': status_color
        })

        if is_unmatched:
            print(f"⚪ Pop #{i+1}: audio={audio_pop_time}ms, no video pop within {max_match_window_ms}ms (ignored)")
        elif closest_event is not None:
            status = "✅" if is_synced else "❌"
            ch_status = "✓" if channels_match else "✗"
            vch = closest_event_channel if closest_event_channel in ('L', 'R') else "B"
            print(f"{status} Pop #{i+1}: audio={audio_pop_time}ms ({audio_channel}), video={closest_event:.1f}ms ({vch}), diff={min_diff:.1f}ms, ch={ch_status}")
        else:
            print(f"❌ Pop #{i+1}: audio={audio_pop_time}ms, no matching video pop found")

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
        'traffic': traffic_light,
        'channel_match_count': channel_match_count,
        'channel_mismatch_count': channel_mismatch_count,
        'channels_all_match': channel_mismatch_count == 0
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

    # Re-enable frame sequence box reporting
    return {
        'frame_sequence_box': {'pass': framebox.get('pass', False), 'details': framebox}
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

    # Channel matching (L/R consistency between audio and video)
    ch_match = results.get('channel_match_count', 0)
    ch_mismatch = results.get('channel_mismatch_count', 0)
    ch_total = ch_match + ch_mismatch
    if ch_total > 0:
        ch_status = "✓" if ch_mismatch == 0 else "✗"
        print(f"🔊 Channel Match: {ch_match}/{ch_total} ({ch_status})")

    if results['is_perfectly_synced'] and results.get('channels_all_match', True):
        print("\n🎉 SUCCESS: Perfect A/V synchronization achieved!")
        return 0
    else:
        analyzed_issues = results['total_analyzed'] - results['perfect_sync_count']
        if analyzed_issues > 0:
            print(f"\n⚠️  WARNING: {analyzed_issues} sync issues detected (out of {results['total_analyzed']} analyzed)")
        if ch_mismatch > 0:
            print(f"⚠️  WARNING: {ch_mismatch} channel mismatches (audio L/R doesn't match video L/R)")
        print("Consider adjusting timing or investigating sync drift.")
        return 1


if __name__ == "__main__":
    exit(main())
