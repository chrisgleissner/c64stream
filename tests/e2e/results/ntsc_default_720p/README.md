# C64 Stream E2E Test Report

## Scenario: ntsc_default_720p

Generated: 2026-01-12 16:53:41 UTC

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

- ✅ UDP Packet Reception: 30789 packets (28786 video, 0 audio)
- ✅ Network Timing: span=8019.1ms, video_mean=322.2us, audio_mean=4005.0us
- ✅ Frame Processing: Accuracy: 100.0%
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 of 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 44.7% | 49.1% | 52.30% | 74.8% |
| RAM | 5892.40 MB | 5950.10 MB | 6114.26 MB | 6454.05 MB |
| GPU | 30.4% | 89.8% | 82.21% | 95.7% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 30789 (approx)
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8019.068 ms
- Total packets analyzed: 26889

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|----------------|-------------|---------|
| All | 26889 | 0.001 ms | 0.596 ms | 7.095 ms | 169.47% | 8.18% | 13.76% | 15.456 |
| Video | 24887 | 0.001 ms | 0.322 ms | 4.455 ms | 83.94% | 8.82% | 6.83% | 5.796 |
| Audio | 2002 | 0.875 ms | 4.005 ms | 7.095 ms | 12.72% | 0.40% | 0.00% | 1.384 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 24887 | 0.019 ms | 4.175 ms | 0 |
| Audio | 2002 | 0.058 ms | 3.125 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 0.0ms, max 0.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9724.0ms, video=9711.5ms (frame 581), diff=12.5ms
- 🟢 Pop #2 [R]: audio=10524.0ms, video=10513.8ms (frame 629), diff=10.2ms
- 🟢 Pop #3 [L]: audio=11329.0ms, video=11316.2ms (frame 677), diff=12.8ms
- 🟢 Pop #4 [R]: audio=12130.0ms, video=12118.5ms (frame 725), diff=11.5ms
- 🟢 Pop #5 [L]: audio=12931.0ms, video=12920.8ms (frame 773), diff=10.2ms
- 🟢 Pop #6 [R]: audio=13735.0ms, video=13723.1ms (frame 821), diff=11.9ms
- 🟢 Pop #7 [L]: audio=14537.0ms, video=14525.5ms (frame 869), diff=11.5ms
- 🟢 Pop #8 [R]: audio=15339.0ms, video=15327.8ms (frame 917), diff=11.2ms
- 🟢 Pop #9 [L]: audio=16144.0ms, video=16130.1ms (frame 965), diff=13.9ms

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
| 1 | 1 | 10.297 | 0.000 | 0.000 | 10.297–10.297 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.2 s

### Sample Frame

![Sample Frame](./c64_recording_still.png)