# C64 Stream E2E Test Report

## Scenario: ntsc_vintage_tv

Generated: 2026-01-12 12:38:21 UTC

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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19252 packets (18000 video, 0 audio)
- ✅ Network Timing: span=5195.5ms, video_mean=410.2us, audio_mean=4005.0us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.1s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 93.0% | 94.2% | 94.35% | 96.6% |
| RAM | 5489.63 MB | 5532.60 MB | 5521.83 MB | 5549.55 MB |
| GPU | 30.4% | 89.8% | 81.83% | 98.5% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19252 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5195.541 ms
- Total packets analyzed: 13475

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 13475 | 0.001 ms | 0.744 ms | 25.522 ms | 204.31% | 20.04% | 17.45% | 23.618 |
| Video | 12224 | 0.001 ms | 0.410 ms | 22.612 ms | 218.32% | 21.34% | 9.79% | 13.346 |
| Audio | 1251 | 0.001 ms | 4.005 ms | 25.522 ms | 57.62% | 12.87% | 4.00% | 3.194 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 12224 | 0.038 ms | 22.329 ms | 0 |
| Audio | 1251 | 0.622 ms | 21.523 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1782.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2583.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3388.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4189.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=4990.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟡 Frame sequence verified (380.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 32.0/2.0/2.0/24.0 | 57.0/1.0/1.0/3.0 | 2.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 51.0/1.0/1.0/3.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 35 | 5.326 | 0.711 | 2.390 | 4.095–6.485 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)