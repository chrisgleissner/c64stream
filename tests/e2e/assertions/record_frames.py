#!/usr/bin/env python3
"""
C64 Stream - Record Frames Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Assertion to verify that BMP frames were saved correctly during E2E testing.
Checks that frame files exist and have valid content.
"""

import struct
from pathlib import Path
from typing import Any, Optional

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig

# BMP magic bytes
BMP_SIGNATURE = b'BM'


class RecordFramesAssertion(EffectAssertion):
    """Verify that BMP frames were recorded correctly."""

    def __init__(
        self,
        thresholds: Optional[dict[str, float]] = None,
        min_frames: int = 30,
        min_width: int = 384,
        min_height: int = 240,
    ):
        """
        Initialize the record frames assertion.

        Args:
            thresholds: Optional threshold overrides
            min_frames: Minimum number of frame files expected
            min_width: Minimum expected frame width
            min_height: Minimum expected frame height
        """
        super().__init__("record_frames", thresholds)
        self.min_frames = min_frames
        self.min_width = min_width
        self.min_height = min_height

    def _find_frames_dir(self, mp4_path: Path) -> Path | None:
        """Find the frames directory in plugin recording session or output_dir."""
        output_dir = mp4_path.parent

        # Check output_dir first
        if output_dir.exists():
            frames_dir = output_dir / "frames"
            if frames_dir.exists() and any(frames_dir.glob("*.bmp")):
                return frames_dir

            # Check for session subdirectories
            for session_dir in output_dir.glob("session_*"):
                frames_dir = session_dir / "frames"
                if frames_dir.exists() and any(frames_dir.glob("*.bmp")):
                    return frames_dir

        # Check plugin's default recording folder
        plugin_recordings = Path.home() / "Documents" / "obs-studio" / "c64stream" / "recordings"
        if plugin_recordings.exists():
            # Find the most recent session with frames
            sessions = sorted(plugin_recordings.glob("session_*"), key=lambda p: p.stat().st_mtime, reverse=True)
            for session_dir in sessions[:5]:  # Check last 5 sessions
                frames_dir = session_dir / "frames"
                if frames_dir.exists() and any(frames_dir.glob("*.bmp")):
                    return frames_dir

        return None

    def _get_bmp_dimensions(self, filepath: Path) -> tuple[int, int] | None:
        """Read BMP dimensions from file header."""
        try:
            with open(filepath, 'rb') as f:
                # Read BMP signature
                signature = f.read(2)
                if signature != BMP_SIGNATURE:
                    return None

                # Skip file size (4 bytes), reserved (4 bytes)
                f.read(8)

                # Read data offset (4 bytes)
                f.read(4)

                # Read DIB header size (4 bytes)
                header_size = struct.unpack('<I', f.read(4))[0]

                if header_size >= 12:
                    # BITMAPINFOHEADER or newer - width and height are 4-byte signed integers
                    width = struct.unpack('<i', f.read(4))[0]
                    height = struct.unpack('<i', f.read(4))[0]
                    # Height can be negative (top-down DIB), take absolute value
                    return (abs(width), abs(height))
                else:
                    return None
        except Exception:
            return None

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        """Run the record frames assertion."""
        frames_dir = self._find_frames_dir(mp4_path)

        if frames_dir is None:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="No frames directory found",
                details={"error": "Could not find frames directory in output or plugin recordings"}
            )

        # Find all BMP files
        frame_files = sorted(frames_dir.glob("*.bmp"))
        frame_count = len(frame_files)

        if frame_count == 0:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message="No frame files found",
                details={"frames_dir": str(frames_dir), "frame_count": 0}
            )

        # Check frame count
        if frame_count < self.min_frames:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Too few frames: {frame_count} (min {self.min_frames})",
                details={
                    "frames_dir": str(frames_dir),
                    "frame_count": frame_count,
                    "min_frames": self.min_frames
                }
            )

        # Sample some frames to verify dimensions
        sample_files = [frame_files[0], frame_files[len(frame_files) // 2], frame_files[-1]]
        dimensions = []
        invalid_files = []

        for sample in sample_files:
            dims = self._get_bmp_dimensions(sample)
            if dims is None:
                invalid_files.append(sample.name)
            else:
                dimensions.append(dims)

        if invalid_files:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Invalid BMP files: {', '.join(invalid_files)}",
                details={
                    "frames_dir": str(frames_dir),
                    "invalid_files": invalid_files
                }
            )

        # Check dimensions
        width, height = dimensions[0]
        if width < self.min_width or height < self.min_height:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Frame dimensions too small: {width}x{height} (min {self.min_width}x{self.min_height})",
                details={
                    "frames_dir": str(frames_dir),
                    "width": width,
                    "height": height,
                    "min_width": self.min_width,
                    "min_height": self.min_height
                }
            )

        # Calculate total size
        total_size = sum(f.stat().st_size for f in frame_files)
        avg_size = total_size // frame_count if frame_count > 0 else 0

        print(f"[Record Frames] {frame_count} frames, {width}x{height}, avg {avg_size} bytes")

        return AssertionResult(
            status=AssertionStatus.PASS,
            name=self.name,
            message=f"frames OK: {frame_count} frames, {width}x{height}",
            details={
                "frames_dir": str(frames_dir),
                "frame_count": frame_count,
                "width": width,
                "height": height,
                "total_size_bytes": total_size,
                "avg_frame_size": avg_size
            }
        )
