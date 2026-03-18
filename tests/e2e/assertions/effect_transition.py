#!/usr/bin/env python3
"""
C64 Stream - Effect Transition Assertion
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


class EffectTransitionAssertion(EffectAssertion):
    """Detect multiple distinct visual effect states across a video recording.

    Uses a 3-dimensional feature vector per frame:
      - r_ratio: R_sum / (R+G+B) — detects amber tint (high r_ratio)
      - g_ratio: G_sum / (R+G+B) — detects green tint (high g_ratio)
      - row_cv:  std(per-row brightness) / mean(per-row brightness)
                 — coefficient of variation; high value indicates scanlines

    Frames are grouped into sequential "runs" where all frames are within
    state_distance_threshold of the run centroid.  Runs shorter than
    min_state_frames are discarded.  The assertion passes when at least
    min_distinct_states distinct runs remain.

    Expected distinct clusters for demo_effect_preset_cycle:
      1. Default / Sharp Pixels  — neutral colour, no scanlines
      2. Amber Monitor           — amber tint + scanlines
      3. Green Monitor           — green tint + scanlines
      4. CRT-style effects       — neutral colour + scanlines (Classic CRT /
                                   Phosphor Glow / Vintage TV / Arcade Cabinet)
    """

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_distinct_states": 4,    # require at least 4 visual states
            "min_state_frames": 3,       # min consecutive frames per stable run
            "sample_fps": 2.0,           # frames per second to sample
            "max_frames": 90,            # cap (45 s at 2 fps)
            "min_nonblack_sum": 500_000, # ignore very dark / black frames
            "state_distance_threshold": 0.04,  # normalised Euclidean distance
        }
        super().__init__("EffectTransition", {**defaults, **(thresholds or {})})

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    def verify(
        self,
        mp4_path: Path,
        properties: dict[str, Any],
        preset: PresetConfig,
        verbose: bool = False,
    ) -> AssertionResult:
        try:
            frames_raw = self._sample_frames(mp4_path, verbose)
            if not frames_raw:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="No frames could be sampled for effect transition analysis",
                )

            features = [self._compute_features(buf, verbose) for buf in frames_raw]
            non_black = [(f, buf) for f, buf in zip(features, frames_raw) if f is not None]
            if not non_black:
                return AssertionResult(
                    status=AssertionStatus.FAIL,
                    name=self.name,
                    message="All sampled frames were too dark for analysis",
                )

            feat_list = [f for f, _ in non_black]
            ok, message, details = self._verify_transitions(feat_list, verbose)
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
                message=f"Effect transition verification failed: {e}",
            )

    # ------------------------------------------------------------------
    # Frame sampling
    # ------------------------------------------------------------------

    def _sample_frames(self, mp4_path: Path, verbose: bool) -> list[bytes]:
        """Return raw RGB24 buffers for sampled frames."""
        w, h = 1920, 1080
        frame_bytes = w * h * 3
        fps = self.thresholds["sample_fps"]
        max_frames = int(self.thresholds["max_frames"])

        cmd = [
            "ffmpeg", "-v", "error",
            "-i", str(mp4_path),
            "-vf", f"fps={fps}",
            "-frames:v", str(max_frames),
            "-f", "rawvideo",
            "-pix_fmt", "rgb24",
            "-",
        ]

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
        frames: list[bytes] = []
        try:
            for _ in range(max_frames):
                buf = proc.stdout.read(frame_bytes)
                if len(buf) != frame_bytes:
                    break
                frames.append(buf)
        finally:
            with suppress(Exception):
                proc.stdout.close()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

        self.log(f"Sampled {len(frames)} frames for effect transition analysis", verbose)
        return frames

    # ------------------------------------------------------------------
    # Feature extraction
    # ------------------------------------------------------------------

    def _compute_features(
        self, buf: bytes, verbose: bool
    ) -> Optional[tuple[float, float, float]]:
        """Return (r_ratio, g_ratio, row_cv) or None if frame is too dark."""
        w, h = 1920, 1080
        min_sum = self.thresholds["min_nonblack_sum"]

        arr = np.frombuffer(buf, dtype=np.uint8).reshape((h, w, 3)).astype(np.float64)

        r_sum = float(arr[:, :, 0].sum())
        g_sum = float(arr[:, :, 1].sum())
        b_sum = float(arr[:, :, 2].sum())
        total = r_sum + g_sum + b_sum

        if total < min_sum:
            return None  # too dark

        r_ratio = r_sum / total
        g_ratio = g_sum / total

        # Row-level brightness coefficient of variation.
        # Scanlines (darkened alternating rows) produce high row_cv.
        gray = arr.mean(axis=2)          # (h, w) brightness
        row_means = gray.mean(axis=1)    # (h,) one value per row
        mean_brightness = float(row_means.mean())
        if mean_brightness < 1.0:
            row_cv = 0.0
        else:
            row_cv = float(row_means.std()) / mean_brightness

        return (r_ratio, g_ratio, row_cv)

    # ------------------------------------------------------------------
    # Clustering and verification
    # ------------------------------------------------------------------

    @staticmethod
    def _distance(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
        return float(sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5)

    def _find_closest_state(
        self,
        feat: tuple[float, float, float],
        centroids: list[tuple[float, float, float]],
    ) -> tuple[int, float]:
        """Return (index, distance) of closest centroid."""
        best_idx, best_dist = 0, float("inf")
        for i, c in enumerate(centroids):
            d = self._distance(feat, c)
            if d < best_dist:
                best_dist = d
                best_idx = i
        return best_idx, best_dist

    def _verify_transitions(
        self,
        features: list[tuple[float, float, float]],
        verbose: bool,
    ) -> tuple[bool, str, dict[str, Any]]:
        threshold = self.thresholds["state_distance_threshold"]
        min_state_frames = int(self.thresholds["min_state_frames"])
        min_distinct = int(self.thresholds["min_distinct_states"])

        # --- greedy sequential run-grouping ---
        # Each run tracks its centroid as the mean of its members.
        runs: list[dict] = []  # {centroid, count, label}
        state_centroids: list[tuple[float, float, float]] = []

        for feat in features:
            if not state_centroids:
                state_centroids.append(feat)
                runs.append({"centroid": feat, "count": 1, "label": 0})
                continue

            # Is this frame close enough to the CURRENT run's centroid?
            cur = runs[-1]
            d_cur = self._distance(feat, cur["centroid"])

            if d_cur <= threshold:
                # Same run: update centroid (running mean)
                n = cur["count"]
                new_centroid = tuple((c * n + v) / (n + 1) for c, v in zip(cur["centroid"], feat))
                cur["centroid"] = new_centroid  # type: ignore[assignment]
                cur["count"] += 1
            else:
                # New run: find or create a global state
                if state_centroids:
                    idx, d_global = self._find_closest_state(feat, state_centroids)
                    if d_global <= threshold:
                        label = idx
                    else:
                        label = len(state_centroids)
                        state_centroids.append(feat)
                else:
                    label = 0
                    state_centroids.append(feat)
                runs.append({"centroid": feat, "count": 1, "label": label})

        # Discard short runs
        stable_runs = [r for r in runs if r["count"] >= min_state_frames]

        # Collect distinct state labels seen in stable runs
        seen_labels = list(dict.fromkeys(r["label"] for r in stable_runs))
        num_distinct = len(seen_labels)

        # Build summary of what we saw
        def _classify_label(label: int) -> str:
            if label >= len(state_centroids):
                return "unknown"
            c = state_centroids[label]
            r_r, g_r, cv = c
            if g_r > 0.40:
                return "green-tint"
            if r_r > 0.38:
                return "amber-tint"
            if cv > 0.06:
                return "crt-scanlines"
            return "neutral"

        state_labels = {label: _classify_label(label) for label in seen_labels}

        details: dict[str, Any] = {
            "total_frames_analysed": len(features),
            "stable_runs": [(r["label"], r["count"]) for r in stable_runs],
            "distinct_states_found": num_distinct,
            "min_distinct_states_required": min_distinct,
            "state_labels": state_labels,
            "state_centroids": [
                {"r_ratio": round(c[0], 4), "g_ratio": round(c[1], 4), "row_cv": round(c[2], 4)}
                for c in state_centroids
            ],
        }

        self.log(
            f"Distinct states: {num_distinct} (need {min_distinct}); "
            f"labels: {state_labels}",
            verbose,
        )

        if num_distinct < min_distinct:
            return (
                False,
                f"Insufficient distinct effect states: found {num_distinct}, "
                f"need {min_distinct}",
                details,
            )

        return (
            True,
            f"Effect transitions verified: {num_distinct} distinct visual states detected",
            details,
        )
