#!/usr/bin/env python3
"""
C64 Stream - Network Timing Validation Unit Tests
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Unit tests for validate_network_timing() in tests/e2e/e2e.py.
These tests are designed to catch severe pacing regressions such as
"all packets sent instantly".
"""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


def _load_e2e_module():
    here = Path(__file__).resolve().parent
    e2e_path = here / 'e2e.py'
    spec = importlib.util.spec_from_file_location('c64stream_e2e', e2e_path)
    if spec is None or spec.loader is None:
        raise RuntimeError('failed to load e2e.py module spec')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestNetworkTimingValidation(unittest.TestCase):
    def setUp(self):
        self.e2e = _load_e2e_module()

    def _write_network_json(self, path: Path, *, duration_ms: float | None, v_mean_us: float, a_mean_us: float,
                            v_ooo_pct: float = 0.0, a_ooo_pct: float = 0.0,
                            v_jitter_max_ms: float = 0.0, a_jitter_max_ms: float = 0.0):
        data = {
            'summary': {},
            'video': {
                'spacing_mean_us': v_mean_us,
                'out_of_order_rate_pct': v_ooo_pct,
                'jitter_max_ms': v_jitter_max_ms,
            },
            'audio': {
                'spacing_mean_us': a_mean_us,
                'out_of_order_rate_pct': a_ooo_pct,
                'jitter_max_ms': a_jitter_max_ms,
            },
        }
        if duration_ms is not None:
            data['summary']['duration_ms'] = duration_ms
        path.write_text(json.dumps(data), encoding='utf-8')

    def test_pass_baseline_ntsc(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'network.json'
            # ~1s span for 60 NTSC frames
            self._write_network_json(p, duration_ms=1000.0, v_mean_us=278.586, a_mean_us=4005.006)

            status, details, errors, warnings = self.e2e.validate_network_timing(
                network_json_path=p,
                video_format='NTSC',
                frames=60,
                network_simulation={},
            )

            self.assertEqual(status, 'pass')
            self.assertTrue(details)
            self.assertEqual(errors, [])
            self.assertEqual(warnings, [])

    def test_fail_span_too_short(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'network.json'
            self._write_network_json(p, duration_ms=50.0, v_mean_us=278.586, a_mean_us=4005.006)

            status, _details, errors, _warnings = self.e2e.validate_network_timing(
                network_json_path=p,
                video_format='NTSC',
                frames=60,
                network_simulation={},
            )

            self.assertEqual(status, 'fail')
            self.assertTrue(any('span too short' in e for e in errors))

    def test_fail_spacing_mean_too_small(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'network.json'
            self._write_network_json(p, duration_ms=1000.0, v_mean_us=10.0, a_mean_us=4005.006)

            status, _details, errors, _warnings = self.e2e.validate_network_timing(
                network_json_path=p,
                video_format='NTSC',
                frames=60,
                network_simulation={},
            )

            self.assertEqual(status, 'fail')
            self.assertTrue(any('spacing mean too small' in e for e in errors))

    def test_warning_span_unusually_long(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'network.json'
            # For ~1s expected duration, >3s should warn (expected + 2000ms slack)
            self._write_network_json(p, duration_ms=4000.0, v_mean_us=278.586, a_mean_us=4005.006)

            status, _details, _errors, warnings = self.e2e.validate_network_timing(
                network_json_path=p,
                video_format='NTSC',
                frames=60,
                network_simulation={},
            )

            self.assertEqual(status, 'warning')
            self.assertTrue(any('span unusually long' in w for w in warnings))

    def test_warning_out_of_order_without_simulation(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'network.json'
            self._write_network_json(
                p,
                duration_ms=1000.0,
                v_mean_us=278.586,
                a_mean_us=4005.006,
                v_ooo_pct=2.0,
                a_ooo_pct=0.0,
            )

            status, _details, _errors, warnings = self.e2e.validate_network_timing(
                network_json_path=p,
                video_format='NTSC',
                frames=60,
                network_simulation={'max_jitter_ms': 0, 'reorder_percent': 0},
            )

            self.assertEqual(status, 'warning')
            self.assertTrue(any('Out-of-order without simulation' in w for w in warnings))

    def test_missing_duration_is_warning(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'network.json'
            self._write_network_json(p, duration_ms=None, v_mean_us=278.586, a_mean_us=4005.006)

            status, _details, _errors, warnings = self.e2e.validate_network_timing(
                network_json_path=p,
                video_format='NTSC',
                frames=60,
                network_simulation={},
            )

            self.assertEqual(status, 'warning')
            self.assertTrue(any('missing duration_ms' in w for w in warnings))
