# C64 Stream E2E Test Report

## Scenario: ntsc_default

Generated: 2026-01-12 17:04:25 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.0T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30765 packets (28764 video, 0 audio)
- ✅ Network Timing: span=8013.9ms, video_mean=336.9us, audio_mean=4005.0us
- ✅ Frame Processing: Accuracy: 100.0%
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 47.2% | 51.8% | 52.67% | 73.4% |
| RAM | 5845.63 MB | 5920.80 MB | 5908.42 MB | 5929.76 MB |
| GPU | 30.4% | 52.2% | 60.55% | 95.7% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 30765 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8013.923 ms
- Total packets analyzed: 25790

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 25790 | 0.001 ms | 0.621 ms | 6.410 ms | 169.29% | 8.93% | 15.09% | 16.321 |
| Video | 23790 | 0.001 ms | 0.337 ms | 4.644 ms | 102.05% | 9.68% | 7.96% | 8.065 |
| Audio | 2000 | 1.817 ms | 4.005 ms | 6.410 ms | 16.74% | 0.35% | 0.00% | 1.491 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23790 | 0.013 ms | 4.365 ms | 0 |
| Audio | 2000 | 0.068 ms | 2.410 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9707.0ms, video=9694.8ms (frame 580), diff=12.2ms
- 🟢 Pop #2 [R]: audio=10506.0ms, video=10497.1ms (frame 628), diff=8.9ms
- 🟢 Pop #3 [L]: audio=11313.0ms, video=11299.4ms (frame 676), diff=13.6ms
- 🟢 Pop #4 [R]: audio=12113.0ms, video=12101.8ms (frame 724), diff=11.2ms
- 🟢 Pop #5 [L]: audio=12916.0ms, video=12904.1ms (frame 772), diff=11.9ms
- 🟢 Pop #6 [R]: audio=13720.0ms, video=13706.4ms (frame 820), diff=13.6ms
- 🟢 Pop #7 [L]: audio=14521.0ms, video=14508.7ms (frame 868), diff=12.3ms
- 🟢 Pop #8 [R]: audio=15323.0ms, video=15311.1ms (frame 916), diff=11.9ms
- 🟢 Pop #9 [L]: audio=16129.0ms, video=16113.4ms (frame 964), diff=15.6ms

### Frame Progression

- 🟢 Frame sequence verified (477.0 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|--------------------------------:|---------------------------:|-----------:|-------------:|
| During settling | 1.0/2.0/2.0/2.0 | 1.0/1.0/1.0/1.0 | 0.0 | 0.0 |
| After settling | 0.0/0.0/0.0/0.0 | 0.0/0.0/0.0/0.0 | 0.0 | 0.0 |

See [playback.csv](playback.csv) for details.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 13.238 | 0.000 | 0.000 | 13.238–13.238 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)