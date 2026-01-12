# C64 Stream E2E Test Report

## Scenario: ntsc_default_720p

Generated: 2026-01-12 13:08:30 UTC

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

- ✅ UDP Packet Reception: 19208 packets (17956 video, 0 audio)
- ✅ Network Timing: span=5144.0ms, video_mean=312.9us, audio_mean=4003.7us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.2s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 39.1% | 45.2% | 45.81% | 57.1% |
| RAM | 5884.03 MB | 5923.70 MB | 5926.90 MB | 5951.71 MB |
| GPU | 30.4% | 91.3% | 78.92% | 98.5% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19208 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5143.977 ms
- Total packets analyzed: 17236

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 17236 | 0.001 ms | 0.581 ms | 6.385 ms | 171.50% | 7.99% | 12.35% | 14.986 |
| Video | 15985 | 0.001 ms | 0.313 ms | 3.327 ms | 81.13% | 8.61% | 5.51% | 5.867 |
| Audio | 1251 | 1.860 ms | 4.004 ms | 6.385 ms | 11.57% | 0.08% | 0.00% | 1.372 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 15985 | 0.025 ms | 3.042 ms | 0 |
| Audio | 1251 | 0.051 ms | 2.385 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1593.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2394.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3199.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4000.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=4801.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟢 Frame sequence verified (297.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 0.0/0.0/0.0/0.0 | 0.0/0.0/0.0/0.0 | 0.0 | 0.0 |
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