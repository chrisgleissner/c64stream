#!/usr/bin/env python3
"""
C64 Stream - E2E Assertions
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

This package provides assertions for E2E testing of the C64 Stream plugin.
"""

from .afterglow import AfterglowAssertion
from .afterglow_decay import AfterglowDecayAssertion
from .afterglow_width import AfterglowWidthAssertion
from .audio import AudioAssertion
from .av_pop_offset import AvPopOffsetAssertion
from .av_sync_log_validation import AvSyncLogValidationAssertion
from .base import AssertionResult, AssertionStatus, EffectAssertion
from .config import (
    PresetConfig,
    load_preset_from_ini,
    load_properties,
    load_settings_from_obs_scene,
)
from .frame_progression import FrameProgressionAssertion
from .palette_mapping import PaletteMappingAssertion
from .palette_stability import PaletteStabilityAssertion
from .record_audio import RecordAudioAssertion
from .record_frames import RecordFramesAssertion
from .record_network import RecordNetworkAssertion
from .record_obs import RecordObsAssertion
from .record_video import RecordVideoAssertion
from .runner import (
    AssertionRunner,
    create_assertions_from_list,
    create_preset_assertions,
)
from .scanlines import ScanlineAssertion
from .sharp_pixels import SharpPixelsAssertion
from .tint import TintAssertion
from .video_quality import VideoQualityAssertion

__all__ = [
    # Base
    "AssertionStatus",
    "AssertionResult",
    "EffectAssertion",
    # Config
    "PresetConfig",
    "load_settings_from_obs_scene",
    "load_preset_from_ini",
    "load_properties",
    # Assertions
    "VideoQualityAssertion",
    "AudioAssertion",
    "AvPopOffsetAssertion",
    "TintAssertion",
    "PaletteMappingAssertion",
    "PaletteStabilityAssertion",
    "AfterglowAssertion",
    "AfterglowDecayAssertion",
    "AfterglowWidthAssertion",
    "ScanlineAssertion",
    "SharpPixelsAssertion",
    "FrameProgressionAssertion",
    "RecordAudioAssertion",
    "RecordVideoAssertion",
    "RecordObsAssertion",
    "RecordNetworkAssertion",
    "RecordFramesAssertion",
    # Runner
    "AssertionRunner",
    "create_preset_assertions",
    "create_assertions_from_list",
]
