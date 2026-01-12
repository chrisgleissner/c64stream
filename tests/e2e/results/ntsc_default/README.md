# C64 Stream E2E Test Report

## Scenario: ntsc_default

Generated: 2026-01-12 12:41:58 UTC

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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 38461 packets (35957 video, 0 audio)
- ✅ Network Timing: span=10823.1ms, video_mean=341.8us, audio_mean=4005.0us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 6.8 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (12.7s, 26 of 26 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 55.3% | 58.2% | 58.96% | 67.7% |
| RAM | 5404.08 MB | 5454.08 MB | 5446.26 MB | 5465.55 MB |
| GPU | 30.4% | 89.8% | 75.58% | 98.5% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 38461 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 10823.121 ms
- Total packets analyzed: 31809

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 31809 | 0.001 ms | 0.630 ms | 6.787 ms | 169.43% | 10.16% | 15.79% | 16.733 |
| Video | 29306 | 0.001 ms | 0.342 ms | 3.301 ms | 106.15% | 11.00% | 8.61% | 8.150 |
| Audio | 2503 | 1.158 ms | 4.005 ms | 6.787 ms | 18.98% | 0.92% | 0.00% | 1.540 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 29306 | 0.020 ms | 3.021 ms | 0 |
| Audio | 2503 | 0.142 ms | 2.842 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=2017.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2818.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3623.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4424.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=5225.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #6 [R]: audio=6030.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #7 [L]: audio=6831.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #8 [R]: audio=7632.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #9 [L]: audio=8437.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #10 [R]: audio=9238.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #11 [L]: audio=10039.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟢 Frame sequence verified (478.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 0.0/0.0/0.0/0.0 | 0.0/0.0/0.0/0.0 | 0.0 | 0.0 |
| After settling | 1.0/2.0/2.0/2.0 | 1.0/1.0/1.0/1.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 6.703 | 0.000 | 0.000 | 6.703–6.703 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 13.8 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)