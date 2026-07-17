"""Isolated regression tests for the plugin WAV quality assertion."""

import os
import tempfile
import time
import unittest
from unittest.mock import patch
import wave
from pathlib import Path

from assertions.base import AssertionStatus
from assertions.record_audio_quality import RecordAudioQualityAssertion
from assertions.record_audio import RecordAudioAssertion


class TestRecordAudioQualityAssertion(unittest.TestCase):
    """Recording validity must be established before click analysis."""

    def test_empty_wav_fails_before_click_analysis(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_dir = Path(temporary_directory)
            with wave.open(str(output_dir / "audio.wav"), "wb") as wav_file:
                wav_file.setnchannels(2)
                wav_file.setsampwidth(2)
                wav_file.setframerate(48000)

            result = RecordAudioQualityAssertion().verify(
                output_dir / "recording.mp4", {}, None
            )

        self.assertEqual(result.status, AssertionStatus.FAIL)
        self.assertIn("contains no PCM frames", result.message)
        self.assertIn("click analysis not run", result.message)
        self.assertNotIn("click_count", result.details)
        self.assertEqual(result.details["pcm_bytes"], 0)

    def test_stale_default_session_wav_is_not_used_for_current_replay(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            output_dir = root / "result"
            output_dir.mkdir()
            manifest = output_dir / "audio_manifest.csv"
            manifest.write_text("filename,delay_us\naudio_0000.bin,1\n")
            now = time.time()
            os.utime(manifest, (now, now))

            stale_wav = root / "Documents" / "obs-studio" / "c64stream" / "recordings" / "session_old" / "audio.wav"
            stale_wav.parent.mkdir(parents=True)
            stale_wav.write_bytes(b"stale")
            os.utime(stale_wav, (now - 60, now - 60))

            with patch("assertions.record_audio.Path.home", return_value=root):
                found = RecordAudioAssertion()._find_audio_wav(output_dir)

        self.assertIsNone(found)


if __name__ == "__main__":
    unittest.main()
