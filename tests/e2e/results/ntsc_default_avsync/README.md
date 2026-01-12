# C64 Stream E2E Test Report

## Scenario: ntsc_default_avsync

Generated: 2026-01-12 13:00:59 UTC

## Test configuration

- Format: NTSC
- Frames: 600
- Duration: 10.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 38456 packets (35952 video, 0 audio)
- ✅ Network Timing: span=10322.3ms, video_mean=340.6us, audio_mean=4005.0us
- ✅ Frame Processing: 599 frames processed
- ✅ Video Recording: 6.8 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (12.6s, 26 of 26 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 52.5% | 54.8% | 56.97% | 78.9% |
| RAM | 6199.31 MB | 6292.93 MB | 6281.01 MB | 6328.88 MB |
| GPU | 30.4% | 89.8% | 83.71% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 38456 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 10322.302 ms
- Total packets analyzed: 31906

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 31906 | 0.001 ms | 0.628 ms | 9.302 ms | 169.53% | 9.95% | 15.58% | 16.488 |
| Video | 29403 | 0.001 ms | 0.341 ms | 5.455 ms | 106.18% | 10.77% | 8.42% | 8.276 |
| Audio | 2503 | 0.062 ms | 4.005 ms | 9.302 ms | 18.37% | 0.80% | 0.04% | 1.517 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 29403 | 0.019 ms | 5.176 ms | 0 |
| Audio | 2503 | 0.121 ms | 5.300 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1919.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2720.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3525.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4327.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=5127.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #6 [L]: audio=5932.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #7 [R]: audio=6733.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #8 [L]: audio=7534.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #9 [L]: audio=8339.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #10 [L]: audio=9140.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #11 [R]: audio=9941.0ms (ignored: unmatched_audio_pop)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 13.7 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)