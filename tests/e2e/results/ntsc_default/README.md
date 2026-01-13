# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-13 14:59:47 UTC

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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30801 packets (28798 video, 2003 audio)
- ✅ Network Timing: span=8021.9ms, video_mean=372.9us, audio_mean=4004.9us
- ✅ Frame Processing: 477 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 65.4% | 72.8% | 73.19% | 83.3% |
| RAM | 5680.73 MB | 5752.36 MB | 5749.61 MB | 5780.94 MB |
| GPU | 30.43% | 78.26% | 65.25% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8021.931 ms
- Total packets analyzed: 23513

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23513 | 0.001 ms | 0.682 ms | 10.230 ms | 168.57% | 16.82% | 19.13% | 17.716 |
| Video | 21511 | 0.001 ms | 0.373 ms | 6.913 ms | 129.70% | 18.30% | 11.70% | 9.690 |
| Audio | 2002 | 0.003 ms | 4.005 ms | 10.230 ms | 24.33% | 1.80% | 0.25% | 1.721 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21511 | 0.041 ms | 6.632 ms | 0 |
| Audio | 2002 | 0.300 ms | 6.235 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 17.3ms, max 20.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9746.0ms, video=9728.2ms (frame 582), diff=17.8ms
- 🟢 Pop #2 [R]: audio=10545.0ms, video=10530.5ms (frame 630), diff=14.5ms
- 🟢 Pop #3 [L]: audio=11353.0ms, video=11332.9ms (frame 678), diff=20.1ms
- 🟢 Pop #4 [R]: audio=12151.0ms, video=12135.2ms (frame 726), diff=15.8ms
- 🟢 Pop #5 [L]: audio=12955.0ms, video=12937.5ms (frame 774), diff=17.5ms
- 🟢 Pop #6 [R]: audio=13758.0ms, video=13739.8ms (frame 822), diff=18.2ms
- 🟢 Pop #7 [L]: audio=14559.0ms, video=14542.2ms (frame 870), diff=16.8ms
- 🟢 Pop #8 [R]: audio=15360.0ms, video=15344.5ms (frame 918), diff=15.5ms
- 🟢 Pop #9 [L]: audio=16166.0ms, video=16146.8ms (frame 966), diff=19.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (477 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 0/0/0/0 | 4/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.344–17.317).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 3 | 15.852 | 0.044 | 0.100 | 15.813–15.913 |
| 2 | 1 | 15.110 | 0.000 | 0.000 | 15.110–15.110 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 582 at 00:09.7 of the 19.2 s video above.
