#!/usr/bin/env python3
"""
Investigation script for video pop detection.
Tests different approaches against actual recorded video with effects like:
- Scanlines
- Monochrome (amber/green monitors)  
- Phosphor afterglow
- Different resolutions

Goal: Find extremely resilient detection logic that works in all scenarios.
"""

import cv2
import numpy as np
import sys
from pathlib import Path

def analyze_pop_box_region(video_path):
    """Analyze the pop box region across all frames to understand its behavior."""
    print(f"\n{'='*80}")
    print(f"ANALYZING: {video_path}")
    print('='*80)
    
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        print(f"❌ Failed to open video: {video_path}")
        return
    
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    
    print(f"📹 Video: {width}x{height} @ {fps:.2f} fps, {total_frames} frames")
    
    # Expected C64 content area (384x272 at source, scaled to output)
    # Pop box is in bottom-right: outer 88x56, inner 72x40
    # Position: (384-88, 272-56) = (296, 216) at source
    
    # Find content bounds first
    cap.set(cv2.CAP_PROP_POS_FRAMES, int(total_frames * 0.3))
    ret, sample_frame = cap.read()
    if not ret:
        print("❌ Failed to read sample frame")
        cap.release()
        return
    
    gray_sample = cv2.cvtColor(sample_frame, cv2.COLOR_BGR2GRAY)
    
    # Detect content bounds (non-black area)
    _, binary = cv2.threshold(gray_sample, 10, 255, cv2.THRESH_BINARY)
    coords = cv2.findNonZero(binary)
    if coords is None:
        print("❌ No content detected")
        cap.release()
        return
    
    x, y, w, h = cv2.boundingRect(coords)
    content_left, content_top = x, y
    content_right, content_bottom = x + w, y + h
    content_w, content_h = w, h
    
    print(f"📦 Content bounds: ({content_left}, {content_top}) to ({content_right}, {content_bottom})")
    print(f"   Size: {content_w}x{content_h}")
    
    # Calculate pop box position from content bounds
    # At C64 source: pop box is at (296, 216), size 88x56 outer
    # Inner content: offset 8px from all sides, so 72x40
    scale_x = content_w / 384.0
    scale_y = content_h / 272.0
    
    # Pop box outer position
    pop_outer_x0 = content_left + int(296 * scale_x)
    pop_outer_y0 = content_top + int(216 * scale_y)
    pop_outer_x1 = pop_outer_x0 + int(88 * scale_x)
    pop_outer_y1 = pop_outer_y0 + int(56 * scale_y)
    
    # Pop box inner content (8px frame on all sides)
    frame_offset_x = int(8 * scale_x)
    frame_offset_y = int(8 * scale_y)
    pop_inner_x0 = pop_outer_x0 + frame_offset_x
    pop_inner_y0 = pop_outer_y0 + frame_offset_y
    pop_inner_x1 = pop_outer_x1 - frame_offset_x
    pop_inner_y1 = pop_outer_y1 - frame_offset_y
    
    print(f"🎯 Pop box outer: ({pop_outer_x0}, {pop_outer_y0}) to ({pop_outer_x1}, {pop_outer_y1})")
    print(f"🎯 Pop box inner: ({pop_inner_x0}, {pop_inner_y0}) to ({pop_inner_x1}, {pop_inner_y1})")
    
    # Calculate left/right half regions
    inner_w = pop_inner_x1 - pop_inner_x0
    inner_h = pop_inner_y1 - pop_inner_y0
    half_w = inner_w // 2
    divider_w = max(2, int(2 * scale_x))  # 2px divider at source
    
    left_x0 = pop_inner_x0
    left_x1 = pop_inner_x0 + half_w - divider_w
    right_x0 = pop_inner_x0 + half_w + divider_w
    right_x1 = pop_inner_x1
    
    print(f"◀️  Left half: x=[{left_x0}, {left_x1})")
    print(f"▶️  Right half: x=[{right_x0}, {right_x1})")
    
    # Analyze frame-by-frame
    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    frame_data = []
    
    frame_num = 0
    while True:
        ret, frame = cap.read()
        if not ret:
            break
        
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        # Sample the pop box inner area (both halves)
        left_region = gray[pop_inner_y0:pop_inner_y1, left_x0:left_x1]
        right_region = gray[pop_inner_y0:pop_inner_y1, right_x0:right_x1]
        divider_region = gray[pop_inner_y0:pop_inner_y1, left_x1:right_x0]
        
        # Multiple metrics to understand the behavior
        left_mean = float(np.mean(left_region))
        left_median = float(np.median(left_region))
        left_p95 = float(np.percentile(left_region, 95))
        left_max = float(np.max(left_region))
        
        right_mean = float(np.mean(right_region))
        right_median = float(np.median(right_region))
        right_p95 = float(np.percentile(right_region, 95))
        right_max = float(np.max(right_region))
        
        divider_mean = float(np.mean(divider_region)) if divider_region.size > 0 else 0.0
        
        time_ms = (frame_num / fps) * 1000.0
        
        frame_data.append({
            'frame': frame_num,
            'time_ms': time_ms,
            'left_mean': left_mean,
            'left_median': left_median,
            'left_p95': left_p95,
            'left_max': left_max,
            'right_mean': right_mean,
            'right_median': right_median,
            'right_p95': right_p95,
            'right_max': right_max,
            'divider_mean': divider_mean,
            'max_brightness': max(left_max, right_max),
            'mean_diff': abs(left_mean - right_mean),
        })
        
        frame_num += 1
    
    cap.release()
    
    print(f"\n📊 Analyzed {len(frame_data)} frames")
    
    # Statistics
    max_brightnesses = [f['max_brightness'] for f in frame_data]
    mean_diffs = [f['mean_diff'] for f in frame_data]
    
    print(f"\n📈 Brightness statistics (max of left/right):")
    print(f"   Min: {np.min(max_brightnesses):.1f}")
    print(f"   Median: {np.median(max_brightnesses):.1f}")
    print(f"   Mean: {np.mean(max_brightnesses):.1f}")
    print(f"   95th: {np.percentile(max_brightnesses, 95):.1f}")
    print(f"   Max: {np.max(max_brightnesses):.1f}")
    
    print(f"\n📈 Mean difference statistics (|left - right|):")
    print(f"   Min: {np.min(mean_diffs):.1f}")
    print(f"   Median: {np.median(mean_diffs):.1f}")
    print(f"   Mean: {np.mean(mean_diffs):.1f}")
    print(f"   95th: {np.percentile(mean_diffs, 95):.1f}")
    print(f"   Max: {np.max(mean_diffs):.1f}")
    
    # Try to detect pops using various strategies
    print(f"\n🔍 DETECTION STRATEGIES:")
    
    # Strategy 1: Brightness threshold
    threshold = np.median(max_brightnesses) + 3 * np.std(max_brightnesses)
    bright_frames = [f for f in frame_data if f['max_brightness'] > threshold]
    print(f"\n1. Brightness threshold ({threshold:.1f}):")
    print(f"   Detected {len(bright_frames)} potential pops")
    if bright_frames:
        for f in bright_frames[:5]:
            print(f"     Frame {f['frame']} @ {f['time_ms']:.0f}ms: L={f['left_max']:.0f} R={f['right_max']:.0f}")
    
    # Strategy 2: Mean difference threshold
    diff_threshold = np.percentile(mean_diffs, 95)
    diff_frames = [f for f in frame_data if f['mean_diff'] > diff_threshold]
    print(f"\n2. Mean difference threshold ({diff_threshold:.1f}):")
    print(f"   Detected {len(diff_frames)} potential pops")
    if diff_frames:
        for f in diff_frames[:5]:
            print(f"     Frame {f['frame']} @ {f['time_ms']:.0f}ms: diff={f['mean_diff']:.0f}")
    
    # Strategy 3: Temporal derivative (sudden changes)
    derivatives = []
    for i in range(1, len(frame_data)):
        curr_brightness = frame_data[i]['max_brightness']
        prev_brightness = frame_data[i-1]['max_brightness']
        deriv = abs(curr_brightness - prev_brightness)
        derivatives.append((i, deriv))
    
    if derivatives:
        deriv_threshold = np.percentile([d[1] for d in derivatives], 98)
        sudden_frames = [frame_data[i] for i, d in derivatives if d > deriv_threshold]
        print(f"\n3. Temporal derivative threshold ({deriv_threshold:.1f}):")
        print(f"   Detected {len(sudden_frames)} sudden changes")
        if sudden_frames:
            for f in sudden_frames[:5]:
                print(f"     Frame {f['frame']} @ {f['time_ms']:.0f}ms: L={f['left_max']:.0f} R={f['right_max']:.0f}")
    
    # Strategy 4: Bimodal detection (inactive vs active states)
    # Check if brightness distribution is bimodal
    hist, bins = np.histogram(max_brightnesses, bins=50)
    peaks = []
    for i in range(1, len(hist)-1):
        if hist[i] > hist[i-1] and hist[i] > hist[i+1] and hist[i] > np.max(hist) * 0.1:
            peaks.append(bins[i])
    
    print(f"\n4. Bimodal analysis:")
    print(f"   Found {len(peaks)} peaks in brightness histogram")
    if len(peaks) >= 2:
        # Assume lower peak is inactive, higher peak is active
        inactive_brightness = min(peaks)
        active_brightness = max(peaks)
        mid_threshold = (inactive_brightness + active_brightness) / 2
        pop_frames = [f for f in frame_data if f['max_brightness'] > mid_threshold]
        print(f"   Inactive: ~{inactive_brightness:.0f}, Active: ~{active_brightness:.0f}")
        print(f"   Threshold: {mid_threshold:.0f}")
        print(f"   Detected {len(pop_frames)} potential pops")
        if pop_frames:
            for f in pop_frames[:5]:
                print(f"     Frame {f['frame']} @ {f['time_ms']:.0f}ms: L={f['left_max']:.0f} R={f['right_max']:.0f}")
    
    # Export frame data for manual inspection
    output_csv = Path(video_path).parent / "pop_analysis.csv"
    with open(output_csv, 'w') as f:
        f.write("frame,time_ms,left_mean,left_median,left_p95,left_max,right_mean,right_median,right_p95,right_max,divider_mean,max_brightness,mean_diff\n")
        for d in frame_data:
            f.write(f"{d['frame']},{d['time_ms']:.1f},{d['left_mean']:.1f},{d['left_median']:.1f},{d['left_p95']:.1f},{d['left_max']:.1f},"
                   f"{d['right_mean']:.1f},{d['right_median']:.1f},{d['right_p95']:.1f},{d['right_max']:.1f},{d['divider_mean']:.1f},"
                   f"{d['max_brightness']:.1f},{d['mean_diff']:.1f}\n")
    print(f"\n💾 Exported frame analysis to: {output_csv}")
    
    return frame_data


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 investigate_pop_detection.py <video.mp4>")
        print("Example: python3 investigate_pop_detection.py results/pal_default/c64_recording.mp4")
        sys.exit(1)
    
    video_path = Path(sys.argv[1])
    if not video_path.exists():
        print(f"❌ Video not found: {video_path}")
        sys.exit(1)
    
    analyze_pop_box_region(video_path)


if __name__ == '__main__':
    main()
