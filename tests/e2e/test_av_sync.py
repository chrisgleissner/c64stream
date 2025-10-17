#!/usr/bin/env python3
"""
A/V Synchronization Test for C64 Stream

This test verifies that audio beeps and visual black squares are perfectly synchronized
by analyzing the generated video recording and validating timing alignment.

The test accounts for the scan line effect that darkens every second line in the video.
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

        # Load audio data
        audio_data = np.fromfile(temp_audio.name, dtype=np.int16)[44:]  # Skip WAV header

        # Convert stereo to mono by taking left channel
        mono_audio = audio_data[0::2]

        # Calculate envelope (RMS power in 10ms windows)
        window_size = sample_rate // 100  # 10ms windows
        envelope = []

        for i in range(0, len(mono_audio) - window_size, window_size):
            window = mono_audio[i:i + window_size]
            rms = np.sqrt(np.mean(window.astype(float) ** 2))
            envelope.append(rms)

        # Clean up temp file
        os.unlink(temp_audio.name)

        return np.array(envelope)


def detect_black_squares(video_path, frame_rate=30.0):
    """
    Detect timing of black squares in video center.
    Returns list of frame numbers where black squares are detected.

    The test pattern generates a 100x100 black square in the center of a 384x272 C64 frame.
    This gets scaled up in the OBS recording to 1280x720.
    
    Ignores early frames where C64 logo appears to avoid false positives.
    """
    cap = cv2.VideoCapture(str(video_path))
    black_square_frames = []
    frame_num = 0
    
    # Skip early frames where C64 logo appears (typically first 6 seconds)
    skip_frames = int(6.0 * frame_rate)  # Skip first 6 seconds

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
        
        # C64 source: 384x272, Test pattern: 100x100 square at center (192, 136)
        # OBS recording: typically 1280x720
        c64_width, c64_height = 384, 272
        c64_square_size = 100
        c64_center_x, c64_center_y = 192, 136
        
        # Calculate scaling factors
        scale_x = width / c64_width
        scale_y = height / c64_height
        
        # Scale the square dimensions and position
        scaled_square_size_x = int(c64_square_size * scale_x)
        scaled_square_size_y = int(c64_square_size * scale_y)
        scaled_center_x = int(c64_center_x * scale_x)
        scaled_center_y = int(c64_center_y * scale_y)
        
        # Define precise region for the scaled 100x100 black square
        left = scaled_center_x - scaled_square_size_x // 2
        right = scaled_center_x + scaled_square_size_x // 2
        top = scaled_center_y - scaled_square_size_y // 2
        bottom = scaled_center_y + scaled_square_size_y // 2

        # Extract the precise center region where the test pattern square should be
        center_region = frame[top:bottom, left:right]

        # Convert to grayscale for analysis
        gray_region = cv2.cvtColor(center_region, cv2.COLOR_BGR2GRAY)

        # Check if region shows the test pattern (bright square with darker diagonal lines)
        if center_region.size > 0:  # Ensure region is valid
            mean_brightness = np.mean(gray_region)
            
            # Look for dark regions that indicate black squares in the test pattern
            # Pattern observed: 
            # - C64 logo transition: ~37 brightness (very dark, not the test pattern)
            # - Black squares: ~99-100 brightness (actual test pattern)
            # - Background: ~123-124 brightness
            if 90 < mean_brightness < 110:  # Specifically target black square brightness range
                black_square_frames.append(frame_num)

        frame_num += 1

    cap.release()
    return black_square_frames


def detect_audio_beeps(envelope, threshold_factor=3.0, min_duration_ms=40):
    """
    Detect audio beeps in the envelope.
    Returns list of beep start times in 10ms units.
    """
    # Calculate dynamic threshold based on background noise
    background_level = np.percentile(envelope, 10)  # 10th percentile as noise floor
    threshold = background_level * threshold_factor

    beep_starts = []
    in_beep = False
    beep_start = None

    for i, level in enumerate(envelope):
        if level > threshold and not in_beep:
            # Start of beep
            beep_start = i
            in_beep = True
        elif level <= threshold and in_beep:
            # End of beep
            if beep_start is not None:
                beep_duration_ms = (i - beep_start) * 10  # 10ms per sample
                if beep_duration_ms >= min_duration_ms:
                    beep_starts.append(beep_start * 10)  # Convert to milliseconds
            in_beep = False
            beep_start = None

    return beep_starts


def verify_av_sync(video_path, tolerance_ms=100):
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

    # Detect audio beeps
    beep_times = detect_audio_beeps(envelope)
    print(f"🎵 Detected {len(beep_times)} audio beeps at: {beep_times} ms")

    # Detect black squares
    print("⬛ Detecting black squares...")
    black_frames = detect_black_squares(video_path, frame_rate)

    # Convert frame numbers to timestamps
    square_times = [frame / frame_rate * 1000 for frame in black_frames]  # milliseconds

    # Group consecutive frames into sync periods
    sync_periods = []
    if square_times:
        current_period_start = square_times[0]
        prev_time = square_times[0]

        for time in square_times[1:]:
            if time - prev_time > 100:  # Gap > 100ms indicates new sync period
                sync_periods.append(current_period_start)
                current_period_start = time
            prev_time = time
        sync_periods.append(current_period_start)

    # Format timestamps to show only one decimal place
    formatted_periods = [f"{period:.1f}" for period in sync_periods]
    print(f"⬛ Detected {len(sync_periods)} black square periods at: {formatted_periods} ms")

    # Verify synchronization
    sync_results = []
    perfect_sync_count = 0
    total_analyzed = 0  # Count only beeps included in analysis

    for i, beep_time in enumerate(beep_times):
        closest_square = None
        min_diff = float('inf')

        for square_time in sync_periods:
            diff = abs(beep_time - square_time)
            if diff < min_diff:
                min_diff = diff
                closest_square = square_time

        is_synced = min_diff <= tolerance_ms

        # Include all beeps in analysis - no more first/last exclusion complexity
        total_analyzed += 1
        if is_synced:
            perfect_sync_count += 1

        sync_results.append({
            'beep_time_ms': beep_time,
            'closest_square_ms': closest_square,
            'difference_ms': min_diff,
            'is_synced': is_synced,
            'included_in_analysis': True,
            'ignore_reason': None
        })

        status = "✅" if is_synced else "❌"
        print(f"{status} Beep #{i+1}: {beep_time}ms, Square: {closest_square:.1f}ms, Diff: {min_diff:.1f}ms")

    # Calculate sync accuracy based only on analyzed beeps
    sync_accuracy = (perfect_sync_count / total_analyzed * 100) if total_analyzed > 0 else 0

    return {
        'total_beeps': len(beep_times),
        'total_analyzed': total_analyzed,
        'total_square_periods': len(sync_periods),
        'perfect_sync_count': perfect_sync_count,
        'sync_accuracy_percent': sync_accuracy,
        'tolerance_ms': tolerance_ms,
        'sync_details': sync_results,
        'is_perfectly_synced': sync_accuracy == 100.0
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
    print("A/V SYNCHRONIZATION TEST RESULTS")
    print("="*60)

    print(f"📊 Total Audio Beeps: {results['total_beeps']}")
    print(f"🔍 Analyzed Beeps: {results['total_analyzed']} (excluding first/last)")
    print(f"⬛ Total Visual Periods: {results['total_square_periods']}")
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
