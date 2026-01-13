#!/usr/bin/env python3
"""
C64 Stream - E2E Assertion Base Classes
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
"""

import os
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Optional

from .config import PresetConfig


def is_ci() -> bool:
    """Detect if running in CI environment."""
    return bool(os.environ.get("CI") or os.environ.get("GITHUB_ACTIONS"))


class AssertionStatus(Enum):
    """Status of an assertion result."""

    PASS = "pass"
    FAIL = "fail"
    SKIP = "skip"
    WARNING = "warning"


@dataclass
class AssertionResult:
    """Result of a single assertion."""

    status: AssertionStatus
    name: str
    message: str
    details: dict[str, Any] = field(default_factory=dict)
    metrics: dict[str, float] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "status": self.status.value,
            "name": self.name,
            "message": self.message,
            "details": self.details,
            "metrics": self.metrics,
        }


class EffectAssertion(ABC):
    """Base class for effect assertions."""

    def __init__(self, name: str, thresholds: Optional[dict[str, float]] = None):
        self.name = name
        self.thresholds = thresholds or {}

    @abstractmethod
    def verify(
        self, mp4_path: Path, properties: dict[str, Any], preset: PresetConfig, verbose: bool = False
    ) -> AssertionResult:
        """
        Verify the assertion against the recording.

        Args:
            mp4_path: Path to the OBS-produced MP4 file
            properties: Parsed properties.ini as a dict
            preset: The effect preset configuration
            verbose: Enable verbose logging

        Returns:
            AssertionResult with the verification outcome
        """
        pass

    def log(self, message: str, verbose: bool) -> None:
        if verbose:
            print(f"[{self.name}] {message}")
