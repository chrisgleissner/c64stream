# C64 Stream E2E Test Report

## Scenario: ntsc_vintage_tv

Generated: 2026-01-12 13:22:21 UTC

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

- ✅ UDP Packet Reception: 19199 packets (17947 video, 0 audio)
- ✅ Network Timing: span=5192.4ms, video_mean=424.5us, audio_mean=4005.2us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 4.1 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.2s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 93.5% | 94.5% | 94.63% | 97.8% |
| RAM | 6140.46 MB | 6197.65 MB | 6193.30 MB | 6211.77 MB |
| GPU | 30.4% | 89.8% | 79.02% | 97.1% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19199 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5192.361 ms
- Total packets analyzed: 13019

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 13019 | 0.001 ms | 0.769 ms | 24.057 ms | 204.29% | 21.49% | 18.26% | 23.989 |
| Video | 11768 | 0.001 ms | 0.425 ms | 24.057 ms | 223.55% | 23.01% | 10.44% | 14.972 |
| Audio | 1251 | 0.001 ms | 4.005 ms | 21.800 ms | 59.06% | 13.27% | 3.76% | 3.460 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 11768 | 0.044 ms | 23.775 ms | 0 |
| Audio | 1251 | 0.648 ms | 17.803 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1768.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2569.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3374.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4175.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=4976.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟡 Frame sequence verified (379.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 34.0/2.0/2.0/28.0 | 60.0/1.0/1.0/3.0 | 2.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 48.0/1.0/1.0/4.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 33 | 5.273 | 0.737 | 2.423 | 4.062–6.485 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.4 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)