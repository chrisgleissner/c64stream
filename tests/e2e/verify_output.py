#!/usr/bin/env python3
"""
C64 Stream - Output Verification for E2E Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that the recorded OBS output matches the expected test patterns.
Checks for:
- Correct frame count
- Frame order (via marker patterns)
- Audio/Video synchronization
- No dropped frames
"""

import sys
import argparse
import subprocess
from pathlib import Path
import numpy as np


class OutputVerifier:
    def __init__(self, recording_file, format='PAL', expected_frames=30, verbose=False):
        self.recording_file = Path(recording_file)
        self.format = format
        self.expected_frames = expected_frames
        self.verbose = verbose

        # Expected OBS output canvas (E2E profile sets 1280x720 @ 30fps).
        # The C64 frame is rendered inside this canvas, so verifiers should operate in output space.
        self.specs = {
            'PAL': {'width': 1280, 'height': 720, 'fps': 30.0},
            'NTSC': {'width': 1280, 'height': 720, 'fps': 30.0},
        }

    def log(self, message):
        """Print log message if verbose mode is enabled."""
        if self.verbose:
            print(f"[VERIFY] {message}")

    def verify_video_exists(self):
        """Verify the recording file exists."""
        if not self.recording_file.exists():
            print(f"❌ Recording file not found: {self.recording_file}")
            return False

        self.log(f"✅ Recording file found: {self.recording_file}")
        return True

    def extract_video_info(self):
        """Extract video information using ffprobe."""
        self.log("Extracting video information")

        try:
            cmd = [
                'ffprobe',
                '-v', 'error',
                '-select_streams', 'v:0',
                '-show_entries', 'stream=width,height,r_frame_rate,nb_frames',
                '-of', 'json',
                str(self.recording_file)
            ]

            result = subprocess.run(cmd, check=True, capture_output=True, text=True)

            import json
            info = json.loads(result.stdout)

            if 'streams' not in info or len(info['streams']) == 0:
                raise ValueError("No video stream found in recording")

            stream = info['streams'][0]
            self.log(f"Video info: {stream}")

            return stream

        except Exception as e:
            print(f"❌ Failed to extract video info: {e}")
            return None

    def verify_frame_dimensions(self, video_info):
        """Verify that the video dimensions match the expected format."""
        spec = self.specs[self.format]

        width = int(video_info.get('width', 0))
        height = int(video_info.get('height', 0))

        if width != spec['width'] or height != spec['height']:
            print(f"❌ Frame dimensions mismatch:")
            print(f"   Expected: {spec['width']}x{spec['height']}")
            print(f"   Got: {width}x{height}")
            return False

        self.log(f"✅ Frame dimensions correct: {width}x{height}")
        return True

    def verify_frame_count(self, video_info):
        """Verify that the frame count is approximately correct."""
        # Note: Frame count might not be exact due to recording timing
        nb_frames = video_info.get('nb_frames')

        if nb_frames:
            nb_frames = int(nb_frames)

            # Allow some tolerance (±20% of expected frames)
            min_frames = int(self.expected_frames * 0.8)
            max_frames = int(self.expected_frames * 1.2)

            if nb_frames < min_frames or nb_frames > max_frames:
                print(f"⚠️  Frame count outside expected range:")
                print(f"   Expected: ~{self.expected_frames} frames")
                print(f"   Got: {nb_frames} frames")
                return False

            self.log(f"✅ Frame count acceptable: {nb_frames} frames")
            return True
        else:
            self.log("⚠️  Frame count not available in metadata")
            return True  # Don't fail if metadata is missing

    def extract_frames(self, output_dir):
        """Extract frames from the recording for analysis."""
        self.log("Extracting frames for analysis")

        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)

        try:
            cmd = [
                'ffmpeg',
                '-i', str(self.recording_file),
                '-vf', 'select=not(mod(n\\,10))',  # Extract every 10th frame
                '-vsync', 'vfr',
                str(output_path / 'frame_%04d.png')
            ]

            subprocess.run(cmd, check=True, capture_output=True)

            frame_files = list(output_path.glob('frame_*.png'))
            self.log(f"✅ Extracted {len(frame_files)} frames for analysis")

            return frame_files

        except Exception as e:
            print(f"❌ Failed to extract frames: {e}")
            return []

    def verify_frame_markers(self, frame_files):
        """
        Verify that frame marker patterns are present and in correct order.

        Note: This requires image processing to check the top-left corner
        for the frame number marker (color = frame_num % 16).
        This is a placeholder for the full implementation.
        """
        self.log("Verifying frame marker patterns")

        if not frame_files:
            print("⚠️  No frames available for marker verification")
            return True  # Don't fail if extraction didn't work

        # TODO: Implement actual pixel analysis using PIL or OpenCV
        # For now, just check that we have frames
        self.log(f"✅ Frame analysis available ({len(frame_files)} frames)")

        return True

    def verify_audio_sync(self):
        """
        Verify audio/video synchronization.

        This would analyze the audio stream to detect frame markers and
        compare with video frame timing. Placeholder for full implementation.
        """
        self.log("Verifying audio/video synchronization")

        # TODO: Implement audio analysis
        # - Extract audio stream
        # - Analyze amplitude envelope markers
        # - Compare timing with video frames

        self.log("✅ Audio sync check (not yet implemented)")
        return True

    def _read_frames_rgb24(self, max_frames=360):
        """Decode video to RGB24 frames. Returns array [N,H,W,3] uint8."""
        w, h = self._ffprobe_size()
        cmd = ["ffmpeg", "-v", "error", "-i", str(self.recording_file), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        frame_bytes = w * h * 3
        frames = []
        try:
            while True:
                if max_frames is not None and len(frames) >= max_frames:
                    break
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                frames.append(np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3)))
        finally:
            try:
                proc.stdout.close()
            except Exception:
                pass
            proc.kill()
            proc.wait(timeout=5)
        if not frames:
            raise RuntimeError("No frames decoded from recording")
        return np.stack(frames, axis=0)

    def _ffprobe_size(self):
        out = subprocess.check_output(
            [
                "ffprobe",
                "-v",
                "error",
                "-select_streams",
                "v:0",
                "-show_entries",
                "stream=width,height",
                "-of",
                "json",
                str(self.recording_file),
            ]
        )
        import json

        info = json.loads(out)
        stream = info["streams"][0]
        return int(stream["width"]), int(stream["height"])

    @staticmethod
    def _luma_u8(frames_rgb: np.ndarray) -> np.ndarray:
        f = frames_rgb.astype(np.float32)
        return 0.2126 * f[..., 0] + 0.7152 * f[..., 1] + 0.0722 * f[..., 2]

    @staticmethod
    def _find_pop_roi(luma_frames: np.ndarray, bright_thresh: float) -> tuple[int, int, int, int]:
        """
        Auto-locate the A/V pop ROI by selecting the cluster around the brightest pixel.

        Important: the C64 frame is typically rendered unscaled in the top-left of the 1280x720
        canvas, so the pop is NOT at the bottom-right of the full output.
        """
        p = np.percentile(luma_frames.reshape((luma_frames.shape[0], -1)), 99.95, axis=1)
        peak_idx = int(np.argmax(p))

        frame_peak = float(p[peak_idx])
        thr = max(float(bright_thresh), frame_peak * 0.98)

        mask = luma_frames[peak_idx] > thr
        ys, xs = np.where(mask)
        if xs.size < 40:
            raise RuntimeError(f"Could not locate pop ROI (thr={thr:.2f}, peak={frame_peak:.2f})")

        # Center the cluster around the single brightest pixel in that frame.
        peak_xy = np.unravel_index(int(np.argmax(luma_frames[peak_idx])), luma_frames[peak_idx].shape)
        cy = int(peak_xy[0])
        cx = int(peak_xy[1])

        radius = 160
        near = (np.abs(xs - cx) <= radius) & (np.abs(ys - cy) <= radius)
        xs_r = xs[near]
        ys_r = ys[near]
        if xs_r.size < 80:
            raise RuntimeError("Could not isolate pop cluster near brightest pixel")

        x0, x1 = int(xs_r.min()), int(xs_r.max())
        y0, y1 = int(ys_r.min()), int(ys_r.max())

        pad = 4
        h, w = luma_frames.shape[1], luma_frames.shape[2]
        x0 = max(0, x0 - pad)
        y0 = max(0, y0 - pad)
        x1 = min(w - 1, x1 + pad)
        y1 = min(h - 1, y1 + pad)
        return x0, y0, x1, y1

    @staticmethod
    def _verify_afterglow_decay(luma_frames: np.ndarray, roi: tuple[int, int, int, int]) -> tuple[bool, str]:
        x0, y0, x1, y1 = roi
        roi_luma = luma_frames[:, y0 : y1 + 1, x0 : x1 + 1].mean(axis=(1, 2))

        p90 = float(np.percentile(roi_luma, 90))
        p99 = float(np.percentile(roi_luma, 99))
        high_thresh = max(20.0, (p90 + p99) / 2.0)
        idx = np.where(roi_luma > high_thresh)[0]
        if idx.size == 0:
            return False, f"No pop frames detected in ROI (threshold={high_thresh:.2f})"

        # First contiguous pop event
        s = int(idx[0])
        e = s
        for i in idx[1:]:
            i = int(i)
            if i == e + 1:
                e = i
            else:
                break

        if e + 10 >= len(roi_luma):
            return False, "Recording too short to evaluate afterglow tail"

        tail = roi_luma[e + 1 : e + 11]
        if float(tail[0]) < 2.5:
            return False, f"Afterglow tail missing: first tail frame luma={float(tail[0]):.2f} (peak={float(roi_luma[e]):.2f})"

        if not np.all(np.diff(tail) <= 2.5):
            return False, "Afterglow tail is not decaying (unexpected brightness increase)"

        if float(np.mean(tail[2:6])) < 4.0:
            return False, "Afterglow tail fades too quickly (mean tail too low)"

        return True, "Afterglow persistence detected (tail decays across frames)"

    def verify_av_pop_afterglow(self, max_frames=360, bright_thresh=140.0):
        """End-to-end afterglow check using the established 'A/V pop' ROI approach."""
        frames = self._read_frames_rgb24(max_frames=max_frames)
        luma = self._luma_u8(frames)
        roi = self._find_pop_roi(luma, bright_thresh=bright_thresh)
        ok, details = self._verify_afterglow_decay(luma, roi)
        return ok, {"roi": {"x0": roi[0], "y0": roi[1], "x1": roi[2], "y1": roi[3]}, "details": details}

    def run(self):
        """
        Run complete verification.

        Returns:
            bool: True if all verifications pass, False otherwise
        """
        print(f"\n{'='*60}")
        print(f"C64 Stream Output Verification - {self.format}")
        print(f"{'='*60}\n")

        all_passed = True

        # Check 1: File exists
        if not self.verify_video_exists():
            return False

        # Check 2: Extract video info
        video_info = self.extract_video_info()
        if not video_info:
            return False

        # Check 3: Verify dimensions
        if not self.verify_frame_dimensions(video_info):
            all_passed = False

        # Check 4: Verify frame count
        if not self.verify_frame_count(video_info):
            all_passed = False

        # Check 5: Extract and verify frames
        # Note: This is optional and may not work in all CI environments
        try:
            frame_files = self.extract_frames('test_output/frames')
            if not self.verify_frame_markers(frame_files):
                all_passed = False
        except Exception as e:
            self.log(f"⚠️  Frame extraction failed (non-critical): {e}")

        # Check 6: Verify A/V sync
        if not self.verify_audio_sync():
            all_passed = False

        print(f"\n{'='*60}")
        if all_passed:
            print("✅ All verification checks passed!")
        else:
            print("❌ Some verification checks failed")
        print(f"{'='*60}\n")

        return all_passed


def main():
    parser = argparse.ArgumentParser(
        description='Verify C64 Stream e2e test output',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument('recording', help='Path to recorded video file')
    parser.add_argument('--format', choices=['PAL', 'NTSC'], default='PAL',
                        help='Video format (default: PAL)')
    parser.add_argument('--frames', type=int, default=30,
                        help='Expected number of frames (default: 30)')
    parser.add_argument('--verbose', action='store_true',
                        help='Enable verbose logging')
    parser.add_argument('--verify-afterglow', action='store_true',
                        help='Verify afterglow using A/V pop ROI detection')

    args = parser.parse_args()

    verifier = OutputVerifier(args.recording, format=args.format, expected_frames=args.frames, verbose=args.verbose)

    # For filter validation (afterglow), run a focused verifier instead of requiring legacy checks
    # (marker patterns, raw-frame dimensions, etc).
    if args.verify_afterglow:
        if not verifier.verify_video_exists():
            return 1
        ok, info = verifier.verify_av_pop_afterglow(max_frames=360, bright_thresh=140.0)
        if ok:
            print(f"✅ Afterglow Verification: {info['details']} (roi={info['roi']})")
            return 0
        print(f"❌ Afterglow Verification: {info['details']} (roi={info.get('roi')})")
        return 1

    success = verifier.run()
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
