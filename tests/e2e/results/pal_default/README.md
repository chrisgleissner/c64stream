# C64 Stream E2E Test Report

## Scenario: pal_default

Generated: 2026-01-12 13:23:20 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 18239 packets (16993 video, 0 audio)
- ✅ Network Timing: span=5131.4ms, video_mean=331.4us, audio_mean=4001.4us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.1s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 43.3% | 46.1% | 47.33% | 56.3% |
| RAM | 6101.61 MB | 6170.09 MB | 6158.38 MB | 6175.49 MB |
| GPU | 55.0% | 95.7% | 84.72% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18239 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5131.395 ms
- Total packets analyzed: 16289

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 16289 | 0.001 ms | 0.612 ms | 6.360 ms | 166.21% | 7.74% | 13.71% | 15.066 |
| Video | 15044 | 0.001 ms | 0.331 ms | 3.931 ms | 80.68% | 8.38% | 6.60% | 5.909 |
| Audio | 1245 | 1.735 ms | 4.001 ms | 6.360 ms | 11.96% | 0.16% | 0.00% | 1.373 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 15044 | 0.017 ms | 3.644 ms | 0 |
| Audio | 1245 | 0.048 ms | 2.360 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1781.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2738.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3694.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4655.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟢 Frame sequence verified (247.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 0.0/0.0/0.0/0.0 | 1.0/1.0/1.0/1.0 | 0.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 0.0/0.0/0.0/0.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 5.140 | 0.000 | 0.000 | 5.140–5.140 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)