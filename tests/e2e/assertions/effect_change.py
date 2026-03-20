#!/usr/bin/env python3
"""
C64 Stream - Effect Change Assertion
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

from typing import Optional

from .effect_transition import EffectTransitionAssertion


class EffectChangeAssertion(EffectTransitionAssertion):
    """Lightweight effect-transition check: require at least two distinct visual states."""

    def __init__(self, thresholds: Optional[dict[str, float]] = None):
        defaults = {
            "min_distinct_states": 2,
            "min_state_frames": 2,
            "sample_fps": 2.0,
            "max_frames": 90,
            "min_nonblack_sum": 500_000,
            "state_distance_threshold": 0.035,
        }
        super().__init__({**defaults, **(thresholds or {})})
        self.name = "Effect Change"
