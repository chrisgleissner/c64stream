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
from .av_sync_csv_validation import AvSyncCsvValidationAssertion
from .av_sync_offset import AvSyncOffsetAssertion
from .av_sync_log_validation import AvSyncLogValidationAssertion
from .base import AssertionResult, AssertionStatus, EffectAssertion
from .debug_log_presence import DebugLogPresenceAssertion
from .config import (
    PresetConfig,
    load_preset_from_ini,
    load_properties,
    load_settings_from_obs_scene,
)
from .effect_transition import EffectTransitionAssertion
from .frame_progression import FrameProgressionAssertion
from .palette_mapping import PaletteMappingAssertion
from .palette_stability import PaletteStabilityAssertion
from .record_audio import RecordAudioAssertion
from .record_frames import RecordFramesAssertion
from .record_network import RecordNetworkAssertion
from .record_obs import RecordObsAssertion
from .record_video import RecordVideoAssertion
from .script_record import ScriptRecordAssertion
from .runner import (
    AssertionRunner,
    create_assertions_from_list,
    create_preset_assertions,
)
from .scanlines import ScanlineAssertion
from .sharp_pixels import SharpPixelsAssertion
from .tint import TintAssertion
from .tint_transition import TintTransitionAssertion
from .video_quality import VideoQualityAssertion
from .script_log import ScriptLogAssertion

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
    "DebugLogPresenceAssertion",
    "TintAssertion",
    "TintTransitionAssertion",
    "EffectTransitionAssertion",
    "PaletteMappingAssertion",
    "PaletteStabilityAssertion",
    "AfterglowAssertion",
    "AfterglowDecayAssertion",
    "AfterglowWidthAssertion",
    "ScanlineAssertion",
    "SharpPixelsAssertion",
    "FrameProgressionAssertion",
    "ScriptLogAssertion",
    "RecordAudioAssertion",
    "RecordVideoAssertion",
    "ScriptRecordAssertion",
    "RecordObsAssertion",
    "RecordNetworkAssertion",
    "RecordFramesAssertion",
    # Runner
    "AssertionRunner",
    "create_preset_assertions",
    "create_assertions_from_list",
]
