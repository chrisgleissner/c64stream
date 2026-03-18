#!/usr/bin/env python3
"""
C64 Stream - Tint Transition Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import subprocess
from contextlib import suppress
from pathlib import Path
from typing import Any, Optional

import numpy as np

from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import PresetConfig


class TintTransitionAssertion(EffectAssertion):
    """Verify that the video cycles between green and amber tints."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_tint_ratio": 1.20,  # Dominant channel must be 20% higher than avg of others
            "min_nonblack_sum": 500_000,  # Ignore very dark frames
            "sample_fps": 3.0,
            "max_frames": 60,
            "min_state_frames": 2,  # Minimum consecutive frames to count as a stable tint state
            "min_transitions": 1,  # Minimum green<->amber transitions required
        }
        super().__init__("TintTransition", {**defaults, **(thresholds or {})})

    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        try:
            frames = self._sample_frames(mp4_path, verbose)
            if not frames:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="No frames could be sampled for tint transition analysis",
                )

            ok, message, details = self._verify_transitions(frames, verbose)

            return AssertionResult(
                status=AssertionStatus.PASS if ok else AssertionStatus.FAIL,
                name=self.name,
                message=message,
                details=details,
            )

        except Exception as e:
            return AssertionResult(
                status=AssertionStatus.FAIL,
                name=self.name,
                message=f"Tint transition verification failed: {e}",
            )

    def _sample_frames(self, mp4_path: Path, verbose: bool) -> list[tuple[int, int, int]]:
        """Sample frames and return RGB channel sums."""
        w, h = 1920, 1080
        frame_bytes = w * h * 3
        fps = self.thresholds["sample_fps"]
        max_frames = int(self.thresholds["max_frames"])

        cmd = [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(mp4_path),
            "-vf",
            f"fps={fps}",
            "-frames:v",
            str(max_frames),
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ]

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        frames = []

        try:
            for _ in range(max_frames):
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                arr = np.frombuffer(buf, dtype=np.uint8)
                rgb = arr.reshape((-1, 3)).astype(np.uint64)
                r_sum, g_sum, b_sum = (int(x) for x in rgb.sum(axis=0))
                frames.append((r_sum, g_sum, b_sum))
        finally:
            with suppress(Exception):
                proc.stdout.close()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

        self.log(f"Sampled {len(frames)} frames for tint transition analysis", verbose)
        return frames

    def _classify_frame(self, r_sum: int, g_sum: int, b_sum: int) -> str:
        """Classify a frame as 'green', 'amber', or 'neutral'."""
        total = r_sum + g_sum + b_sum
        if total < self.thresholds["min_nonblack_sum"]:
            return "neutral"

        min_ratio = self.thresholds["min_tint_ratio"]

        # Green: G dominant over avg(R, B)
        rb_avg = (r_sum + b_sum) / 2.0 if (r_sum + b_sum) > 0 else 1.0
        if g_sum > r_sum and g_sum > b_sum and g_sum / rb_avg >= min_ratio:
            return "green"

        # Amber: R dominant, G > B (R > G > B pattern)
        gb_avg = (g_sum + b_sum) / 2.0 if (g_sum + b_sum) > 0 else 1.0
        if r_sum > g_sum and g_sum > b_sum and r_sum / gb_avg >= min_ratio:
            return "amber"

        return "neutral"

    def _verify_transitions(
        self, frames: list[tuple[int, int, int]], verbose: bool
    ) -> tuple[bool, str, dict[str, Any]]:
        min_state_frames = int(self.thresholds["min_state_frames"])
        min_transitions = int(self.thresholds["min_transitions"])

        classified = [self._classify_frame(r, g, b) for r, g, b in frames]
        self.log(f"Frame classification: {classified}", verbose)

        # Build stable runs: consecutive same-state blocks (including neutral)
        runs: list[tuple[str, int]] = []
        for state in classified:
            if runs and runs[-1][0] == state:
                runs[-1] = (state, runs[-1][1] + 1)
            else:
                runs.append((state, 1))

        # Keep only runs of non-neutral tint with enough consecutive frames
        stable_tints = [(state, count) for state, count in runs if state != "neutral" and count >= min_state_frames]

        seen_tints = {state for state, _ in stable_tints}
        self.log(f"Stable tint runs: {stable_tints}, seen: {seen_tints}", verbose)

        # Count transitions between distinct adjacent stable tints
        transitions = sum(1 for (a, _), (b, _) in zip(stable_tints, stable_tints[1:]) if a != b)

        details: dict[str, Any] = {
            "total_frames": len(frames),
            "classified": classified,
            "stable_tint_runs": [(s, c) for s, c in stable_tints],
            "seen_tints": sorted(seen_tints),
            "transitions": transitions,
            "min_transitions_required": min_transitions,
            "min_state_frames_required": min_state_frames,
        }

        if not stable_tints:
            return False, "No stable tint states detected (neither green nor amber)", details

        if "green" not in seen_tints:
            return False, f"Green tint not detected (only saw: {sorted(seen_tints)})", details

        if "amber" not in seen_tints:
            return False, f"Amber tint not detected (only saw: {sorted(seen_tints)})", details

        if transitions < min_transitions:
            return (
                False,
                f"Insufficient tint transitions: expected {min_transitions}, found {transitions}",
                details,
            )

        return (
            True,
            f"Tint transition verified: {transitions} green<->amber transition(s) across {len(stable_tints)} stable state(s)",
            details,
        )
