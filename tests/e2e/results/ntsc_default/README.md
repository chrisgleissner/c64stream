# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-22 22:40:22 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: 912712e
- Environment: local

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
- RAM: 31Gi total, 19Gi available
- Disk (/): 1.8T total, 964G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30781, Missing 22 (0.07%)
- ✅ Network Timing: span=8016.7ms, video_mean=394.7us, audio_mean=4006.3us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 66.9% | 78.9% | 78.44% | 89.3% |
| RAM | 11330.66 MB | 11453.74 MB | 11448.76 MB | 11500.15 MB |
| GPU | 12% | 32% | 30.03% | 44% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8016.744 ms
- Total packets analyzed: 22312

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 22312 | 0.001 ms | 0.719 ms | 13.919 ms | 169.45% | 21.24% | 21.22% | 18.225 |
| Video | 20311 | 0.001 ms | 0.395 ms | 11.332 ms | 147.08% | 23.15% | 13.72% | 11.011 |
| Audio | 2001 | 0.003 ms | 4.006 ms | 13.919 ms | 27.77% | 3.30% | 0.55% | 1.810 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 20311 | 0.056 ms | 11.052 ms | 0 |
| Audio | 2001 | 0.400 ms | 9.922 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 181.9ms, max 771.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9860.0ms, video=9845.2ms (frame 589), diff=14.8ms
- 🟢 Pop #2 [R]: audio=10660.0ms, video=10647.5ms (frame 637), diff=12.5ms
- • Pop #3 [L]: audio=11465.0ms, video=12235.5ms (frame 732), diff=770.5ms
- 🟢 Pop #4 [R]: audio=12266.0ms, video=12252.2ms (frame 733), diff=13.8ms
- 🟢 Pop #5 [L]: audio=13066.0ms, video=13054.5ms (frame 781), diff=11.5ms
- • Pop #6 [R]: audio=13871.0ms, video=14642.5ms (frame 876), diff=771.5ms
- 🟢 Pop #7 [L]: audio=14673.0ms, video=14659.2ms (frame 877), diff=13.8ms
- 🟢 Pop #8 [R]: audio=15475.0ms, video=15461.5ms (frame 925), diff=13.5ms
- 🟢 Pop #9 [L]: audio=16279.0ms, video=16263.8ms (frame 973), diff=15.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/2/2/3 | 6/1/1/2 | 0 | 0 |
| After settling | 2/2/2/2 | 4/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.461–20.459).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 7 | 11.450 | 0.048 | 0.134 | 11.383–11.517 |
| 2 | 4 | 13.944 | 0.056 | 0.150 | 13.857–14.007 |
| 3 | 4 | 12.549 | 0.032 | 0.084 | 12.503–12.587 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 589 at 00:09.8 of the 21.5 s video above.
