# C64 Stream E2E Test Report

## Scenario: pal_default

Generated: 2026-01-12 12:44:49 UTC

## Test configuration

- Format: PAL
- Frames: 250
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 18240 packets (16994 video, 0 audio)
- ✅ Network Timing: span=5132.0ms, video_mean=330.9us, audio_mean=4001.0us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.1s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 44.6% | 46.8% | 48.02% | 56.1% |
| RAM | 5483.85 MB | 5554.45 MB | 5544.44 MB | 5571.07 MB |
| GPU | 30.4% | 89.8% | 76.90% | 97.1% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18240 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5132.047 ms
- Total packets analyzed: 16309

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 16309 | 0.001 ms | 0.611 ms | 6.642 ms | 166.77% | 8.06% | 13.56% | 15.211 |
| Video | 15064 | 0.001 ms | 0.331 ms | 3.809 ms | 82.16% | 8.72% | 6.46% | 5.986 |
| Audio | 1245 | 1.328 ms | 4.001 ms | 6.642 ms | 13.02% | 0.16% | 0.00% | 1.401 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 15064 | 0.018 ms | 3.522 ms | 0 |
| Audio | 1245 | 0.047 ms | 2.672 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1771.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2728.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3684.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4644.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟢 Frame sequence verified (247.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 1.0/2.0/2.0/2.0 | 12.0/1.0/1.0/1.0 | 0.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 3.0/1.0/1.0/1.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 7 | 4.754 | 0.419 | 1.120 | 4.340–5.460 |
| 2 | 3 | 6.213 | 0.159 | 0.380 | 6.000–6.380 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)