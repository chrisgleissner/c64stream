# C64 Stream E2E Test Report

## Scenario: ntsc_default

Generated: 2026-01-12 13:07:34 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19198 packets (17946 video, 0 audio)
- ✅ Network Timing: span=5167.2ms, video_mean=339.3us, audio_mean=4006.0us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.1s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 51.0% | 58.9% | 59.01% | 71.5% |
| RAM | 6112.89 MB | 6177.20 MB | 6168.41 MB | 6189.58 MB |
| GPU | 89.8% | 89.8% | 92.64% | 97.1% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19198 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5167.239 ms
- Total packets analyzed: 15979

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 15979 | 0.001 ms | 0.626 ms | 6.889 ms | 170.45% | 11.76% | 16.02% | 16.915 |
| Video | 14728 | 0.001 ms | 0.339 ms | 3.482 ms | 109.11% | 12.75% | 8.89% | 8.179 |
| Audio | 1251 | 1.179 ms | 4.006 ms | 6.889 ms | 18.60% | 0.72% | 0.00% | 1.530 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 14728 | 0.021 ms | 3.202 ms | 0 |
| Audio | 1251 | 0.176 ms | 2.891 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1642.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2443.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3248.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4049.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=4850.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟢 Frame sequence verified (297.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 0.0/0.0/0.0/0.0 | 1.0/1.0/1.0/1.0 | 0.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 0.0/0.0/0.0/0.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above

- No jitter events detected (post-settling)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)