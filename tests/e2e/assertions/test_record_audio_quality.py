"""Isolated regression tests for the plugin WAV quality assertion."""

import tempfile
import unittest
import wave
from pathlib import Path

from assertions.base import AssertionStatus
from assertions.record_audio_quality import RecordAudioQualityAssertion


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


if __name__ == "__main__":
    unittest.main()
