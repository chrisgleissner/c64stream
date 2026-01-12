# C64 Stream E2E Test Report

## Scenario: ntsc_default_avsync

Generated: 2026-01-12 13:42:56 UTC

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
- RAM: 31Gi total, 21Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19239 packets (17987 video, 0 audio)
- ✅ Network Timing: span=5191.0ms, video_mean=471.2us, audio_mean=4005.0us
- ✅ Frame Processing: 299 frames processed
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.2s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 83.6% | 91.5% | 91.39% | 96.2% |
| RAM | 7281.79 MB | 7303.74 MB | 7306.16 MB | 7329.79 MB |
| GPU | 30.4% | 30.4% | 30.43% | 30.4% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19239 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5190.994 ms
- Total packets analyzed: 11876

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 11876 | 0.001 ms | 0.843 ms | 10.212 ms | 170.15% | 31.35% | 24.43% | 21.681 |
| Video | 10625 | 0.001 ms | 0.471 ms | 7.187 ms | 179.65% | 34.11% | 17.04% | 17.122 |
| Audio | 1251 | 0.002 ms | 4.005 ms | 10.212 ms | 37.82% | 8.71% | 1.04% | 2.001 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 10625 | 0.173 ms | 6.932 ms | 0 |
| Audio | 1251 | 0.646 ms | 6.215 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [R]: audio=1620.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [L]: audio=2421.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [R]: audio=3226.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4027.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [R]: audio=4828.0ms (ignored: unmatched_audio_pop)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.1 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)