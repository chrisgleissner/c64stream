# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-13 00:01:28 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream

- Version: 1.0.3

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 27Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30767 packets (28766 video, 2001 audio)
- ✅ Network Timing: span=8011.8ms, video_mean=339.2us, audio_mean=4004.2us
- ✅ Frame Processing: Accuracy: 100.0%
- ✅ Video Recording: 9.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.0% | 54.1% | 53.98% | 67.0% |
| RAM | 3861.50 MB | 3928.77 MB | 3920.30 MB | 3948.66 MB |
| GPU | 30.4% | 55.0% | 62.43% | 95.7% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 30767 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8011.817 ms
- Total packets analyzed: 25620

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 25620 | 0.001 ms | 0.625 ms | 6.884 ms | 168.99% | 9.93% | 15.61% | 16.295 |
| Video | 23620 | 0.001 ms | 0.339 ms | 3.185 ms | 103.95% | 10.75% | 8.49% | 8.125 |
| Audio | 2000 | 1.108 ms | 4.004 ms | 6.884 ms | 16.79% | 0.55% | 0.00% | 1.487 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23620 | 0.017 ms | 2.905 ms | 0 |
| Audio | 2000 | 0.077 ms | 2.892 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9844.0ms, video=9845.2ms (frame 589), diff=1.2ms
- 🟢 Pop #2 [R]: audio=10646.0ms, video=10647.5ms (frame 637), diff=1.5ms
- 🟢 Pop #3 [L]: audio=11471.0ms, video=11449.9ms (frame 685), diff=21.1ms
- 🟢 Pop #4 [R]: audio=12271.0ms, video=12252.2ms (frame 733), diff=18.8ms
- 🟢 Pop #5 [L]: audio=13073.0ms, video=13054.5ms (frame 781), diff=18.5ms
- 🟢 Pop #6 [R]: audio=13877.0ms, video=13856.9ms (frame 829), diff=20.1ms
- 🟢 Pop #7 [L]: audio=14679.0ms, video=14659.2ms (frame 877), diff=19.8ms
- 🟢 Pop #8 [R]: audio=15481.0ms, video=15461.5ms (frame 925), diff=19.5ms
- 🟢 Pop #9 [L]: audio=16285.0ms, video=16263.8ms (frame 973), diff=21.2ms

### Frame Progression

- 🟢 Frame sequence verified (477.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 1.0/3.0/3.0/3.0 | 1.0/2.0/2.0/2.0 | 0.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 0.0/0.0/0.0/0.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above

- No jitter events detected (post-settling)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.3 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)