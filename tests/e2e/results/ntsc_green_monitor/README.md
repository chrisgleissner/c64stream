# C64 Stream E2E Test Report

## Scenario: ntsc_green_monitor

Generated: 2026-01-12 13:15:12 UTC

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

- ✅ UDP Packet Reception: 19185 packets (17933 video, 0 audio)
- ✅ Network Timing: span=5185.3ms, video_mean=423.2us, audio_mean=4006.4us
- ❌ Frame Processing: Accuracy: 0.0% (0 mismatches)
- ✅ Video Recording: 4.0 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (7.1s, 15 of 15 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 89.8% | 91.9% | 91.95% | 95.5% |
| RAM | 6290.01 MB | 6343.16 MB | 6338.43 MB | 6351.80 MB |
| GPU | 55.0% | 89.8% | 90.23% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 19185 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5185.262 ms
- Total packets analyzed: 13055

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 13055 | 0.001 ms | 0.767 ms | 20.112 ms | 183.80% | 23.51% | 21.75% | 22.461 |
| Video | 11804 | 0.001 ms | 0.423 ms | 20.112 ms | 186.81% | 25.60% | 13.94% | 13.336 |
| Audio | 1251 | 0.002 ms | 4.006 ms | 15.731 ms | 44.73% | 9.75% | 2.88% | 2.565 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 11804 | 0.055 ms | 19.829 ms | 0 |
| Audio | 1251 | 0.657 ms | 11.731 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ⚠️ Accuracy: 0.0% (0/0 perfect)

#### Sync Details

- ⚪ Pop #1 [L]: audio=1608.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #2 [R]: audio=2409.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #3 [L]: audio=3214.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #4 [R]: audio=4015.0ms (ignored: unmatched_audio_pop)
- ⚪ Pop #5 [L]: audio=4816.0ms (ignored: unmatched_audio_pop)

### Frame Progression

- 🟢 Frame sequence verified (370.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 20.0/2.0/3.0/30.0 | 48.0/1.0/1.0/5.0 | 1.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 24.0/1.0/1.0/2.0 | 2.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 25 | 5.132 | 0.726 | 2.306 | 4.012–6.318 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 8.1 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)