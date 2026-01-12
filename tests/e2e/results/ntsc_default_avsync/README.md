# C64 Stream E2E Test Report

## Scenario: ntsc_default_avsync

Generated: 2026-01-12 14:14:45 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19197 packets (17945 video, 0 audio)
- ✅ Network Timing: span=5155.0ms, video_mean=337.4us, audio_mean=4004.6us
- ✅ Frame Processing: 299 frames processed
- ✅ Video Recording: 4.1 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.1s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.4% | 54.2% | 55.68% | 69.9% |
| RAM | 6370.77 MB | 6412.79 MB | 6413.78 MB | 6438.03 MB |
| GPU | 30.4% | 30.4% | 41.26% | 95.7% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19197 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5155.038 ms
- Total packets analyzed: 16067

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 16067 | 0.001 ms | 0.623 ms | 6.949 ms | 170.85% | 10.39% | 15.47% | 17.218 |
| Video | 14816 | 0.001 ms | 0.337 ms | 3.300 ms | 106.13% | 11.26% | 8.36% | 8.176 |
| Audio | 1251 | 1.148 ms | 4.005 ms | 6.949 ms | 19.79% | 1.52% | 0.00% | 1.534 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 14816 | 0.018 ms | 3.021 ms | 0 |
| Audio | 1251 | 0.154 ms | 2.949 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [R]: audio=1779.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [L]: audio=2580.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [R]: audio=3385.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4186.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [R]: audio=4987.0ms (ignored: unmatched_audio_pop)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.4 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)