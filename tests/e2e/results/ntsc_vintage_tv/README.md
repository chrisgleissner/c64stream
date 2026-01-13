# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-13 16:33:16 UTC
Git Branch: test/modularize-e2e
Git ID: f4f3cd8
Environment: local

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

- ✅ UDP Packet Reception: Expected 30803, Received 30757, Missing 46 (0.15%)
- ✅ Network Timing: span=8010.7ms, video_mean=456.8us, audio_mean=4004.9us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 93.5% | 95.5% | 95.72% | 98.3% |
| RAM | 5687.88 MB | 5800.23 MB | 5886.65 MB | 6273.96 MB |
| GPU | 30.43% | 89.83% | 71.75% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8010.704 ms
- Total packets analyzed: 19533

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19533 | 0.001 ms | 0.820 ms | 74.338 ms | 233.96% | 22.97% | 18.74% | 26.799 |
| Video | 17533 | 0.001 ms | 0.457 ms | 51.109 ms | 262.28% | 24.18% | 11.05% | 17.982 |
| Audio | 2000 | 0.001 ms | 4.005 ms | 74.338 ms | 86.75% | 17.95% | 5.40% | 4.256 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17533 | 0.050 ms | 50.827 ms | 0 |
| Audio | 2000 | 0.811 ms | 70.344 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ❌ Poor synchronization (50.0%): avg offset 292.9ms, max 869.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9801.0ms, video=9795.1ms (frame 586), diff=5.9ms
- 🟢 Pop #2 [R]: audio=10602.0ms, video=10597.4ms (frame 634), diff=4.6ms
- 🟢 Pop #3 [L]: audio=11407.0ms, video=11399.7ms (frame 682), diff=7.3ms
- • Pop #4 [R]: audio=12250.0ms, video=11399.7ms (frame 682), diff=850.3ms
- • Pop #5 [L]: audio=13051.0ms, video=13773.3ms (frame 824), diff=722.3ms
- 🔴 Pop #6 [R]: audio=13855.0ms, video=13790.0ms (frame 825), diff=65.0ms
- 🟡 Pop #7 [L]: audio=14657.0ms, video=14609.0ms (frame 874), diff=48.0ms
- 🔴 Pop #8 [R]: audio=15458.0ms, video=15394.6ms (frame 921), diff=63.4ms
- • Pop #9 [L]: audio=16264.0ms, video=15394.6ms (frame 921), diff=869.4ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/21/242 | 1/1/1/1 | 2 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–17.367).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 4.196 | 0.000 | 0.000 | 4.196–4.196 |

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
- Taken from frame 586 at 00:09.8 of the 19.2 s video above.
