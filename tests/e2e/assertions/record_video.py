#!/usr/bin/env python3
"""
C64 Stream - Record Video Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Verifies that the video.avi file was recorded correctly in the session folder.
"""

import json
import subprocess
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class RecordVideoAssertion(EffectAssertion):
    """Verify video.avi recording exists and has valid content."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_duration_seconds": 1.0,
            "min_frame_count": 30,
            "expected_width": 384,
            "expected_height": 272,
        }
        super().__init__("Record Video", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        # Find video.avi in the session folder
        output_dir = mp4_path.parent
        video_avi = self._find_video_avi(output_dir)

        if video_avi is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="video.avi not found in session folder",
                details={"searched_dir": str(output_dir)},
            )

        try:
            # Analyze video file with ffprobe
            cmd = [
                "ffprobe",
                "-v",
                "error",
                "-select_streams",
                "v:0",
                "-show_entries",
                "stream=width,height,codec_name,r_frame_rate,nb_frames,pix_fmt:format=duration,size",
                "-of",
                "json",
                str(video_avi),
            ]
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            data = json.loads(result.stdout)

            if not data.get("streams"):
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="No video stream found in video.avi",
                    details={"path": str(video_avi)},
                )

            stream = data["streams"][0]
            fmt = data.get("format", {})

            width = int(stream.get("width", 0))
            height = int(stream.get("height", 0))
            codec = stream.get("codec_name", "unknown")
            frame_rate_str = stream.get("r_frame_rate", "0/1")
            nb_frames_str = stream.get("nb_frames", "0")
            duration = float(fmt.get("duration", 0))
            file_size = int(fmt.get("size", 0))

            # Parse frame rate (e.g., "50/1" or "60/1")
            try:
                if "/" in frame_rate_str:
                    num, den = frame_rate_str.split("/")
                    frame_rate = float(num) / float(den) if float(den) != 0 else 0
                else:
                    frame_rate = float(frame_rate_str)
            except ValueError:
                frame_rate = 0

            # Parse frame count
            try:
                nb_frames = int(nb_frames_str)
            except ValueError:
                nb_frames = 0

            # For raw video, calculate frame count from file size if not provided
            # Raw video: width * height * bytes_per_pixel per frame
            if nb_frames == 0 and width > 0 and height > 0 and file_size > 0:
                pix_fmt = stream.get("pix_fmt", "")
                # bgr24 and rgb24 = 3 bytes per pixel
                if pix_fmt in ("bgr24", "rgb24"):
                    bytes_per_pixel = 3
                elif pix_fmt in ("bgra", "rgba"):
                    bytes_per_pixel = 4
                else:
                    bytes_per_pixel = 3  # default assumption

                frame_size = width * height * bytes_per_pixel
                if frame_size > 0:
                    # Account for AVI header overhead (estimate ~1KB per frame headers)
                    estimated_frames = file_size // (frame_size + 8)
                    if estimated_frames > 0:
                        nb_frames = estimated_frames

            # For raw AVI files, duration might be N/A, so calculate from frames/fps
            if duration == 0 and nb_frames > 0 and frame_rate > 0:
                duration = nb_frames / frame_rate

            self.log(f"video.avi: {width}x{height}, {duration:.2f}s, {nb_frames} frames, {frame_rate:.1f}fps, {codec}", verbose)

            details = {
                "path": str(video_avi),
                "width": width,
                "height": height,
                "duration_seconds": duration,
                "frame_count": nb_frames,
                "frame_rate": frame_rate,
                "codec": codec,
            }
            metrics = {
                "width": float(width),
                "height": float(height),
                "duration_seconds": duration,
                "frame_count": float(nb_frames),
                "frame_rate": frame_rate,
            }

            # Check minimum duration
            min_duration = float(self.thresholds["min_duration_seconds"])
            if duration < min_duration:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"video.avi too short: {duration:.2f}s (min {min_duration}s)",
                    details=details,
                    metrics=metrics,
                )

            # Check minimum frame count
            min_frames = int(self.thresholds["min_frame_count"])
            if nb_frames < min_frames:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message=f"video.avi too few frames: {nb_frames} (min {min_frames})",
                    details=details,
                    metrics=metrics,
                )

            # Check dimensions (warn if different)
            expected_width = int(self.thresholds["expected_width"])
            expected_height = int(self.thresholds["expected_height"])
            if width != expected_width or height != expected_height:
                return AssertionResult(
                    status=AssertionStatus.WARNING,
                    name=self.name,
                    message=f"Unexpected dimensions: {width}x{height} (expected {expected_width}x{expected_height})",
                    details=details,
                    metrics=metrics,
                )

            return AssertionResult(
                status=AssertionStatus.PASS,
                name=self.name,
                message=f"video.avi OK: {width}x{height}, {nb_frames} frames, {duration:.2f}s",
                details=details,
                metrics=metrics,
            )

        except subprocess.CalledProcessError as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Failed to analyze video.avi: {e}",
                details={"path": str(video_avi)},
            )
        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Error verifying video.avi: {e}",
                details={"path": str(video_avi)},
            )

    def _find_video_avi(self, output_dir: Path) -> Optional[Path]:
        """Find video.avi in output directory or session subdirectories."""
        # Check directly in output dir
        direct = output_dir / "video.avi"
        if direct.exists():
            return direct

        # Check in session_* subdirectories of output dir
        for subdir in output_dir.glob("session_*"):
            if subdir.is_dir():
                avi = subdir / "video.avi"
                if avi.exists():
                    return avi

        # Check in plugin's default recording folder
        plugin_recordings = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
        if plugin_recordings.exists():
            # Find most recent session folder
            sessions = sorted(plugin_recordings.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
            for session in sessions:
                avi = session / "video.avi"
                if avi.exists():
                    return avi

        return None
