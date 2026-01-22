# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

- Generated: 2026-01-22 22:41:42 UTC
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
- RAM: 31Gi total, 18Gi available
- Disk (/): 1.8T total, 964G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30784, Missing 19 (0.06%)
- ✅ Network Timing: span=8019.0ms, video_mean=340.0us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.1% | 59.8% | 64.03% | 92.7% |
| RAM | 11170.11 MB | 11238.9 MB | 11278.29 MB | 11469.28 MB |
| GPU | 16% | 39% | 32.03% | 44% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8019.008 ms
- Total packets analyzed: 25587

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25587 | 0.001 ms | 0.627 ms | 23.038 ms | 172.77% | 14.05% | 16.71% | 16.420 |
| Video | 23586 | 0.001 ms | 0.340 ms | 8.194 ms | 112.06% | 15.10% | 9.80% | 7.061 |
| Audio | 2001 | 0.002 ms | 4.005 ms | 23.038 ms | 23.61% | 1.80% | 0.40% | 1.631 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23586 | 0.037 ms | 7.914 ms | 0 |
| Audio | 2001 | 0.140 ms | 19.036 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 192.1ms, max 824.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9865.0ms, video=9845.2ms (frame 589), diff=19.8ms
- 🟢 Pop #2 [R]: audio=10665.0ms, video=10647.5ms (frame 637), diff=17.5ms
- 🟢 Pop #3 [L]: audio=11471.0ms, video=11449.9ms (frame 685), diff=21.1ms
- 🟢 Pop #4 [R]: audio=12273.0ms, video=12252.2ms (frame 733), diff=20.8ms
- 🟢 Pop #5 [L]: audio=13073.0ms, video=13054.5ms (frame 781), diff=18.5ms
- 🟢 Pop #6 [R]: audio=13878.0ms, video=13856.9ms (frame 829), diff=21.1ms
- • Pop #7 [L]: audio=14679.0ms, video=15444.8ms (frame 924), diff=765.8ms
- 🟢 Pop #8 [R]: audio=15481.0ms, video=15461.5ms (frame 925), diff=19.5ms
- • Pop #9 [L]: audio=16286.0ms, video=15461.5ms (frame 925), diff=824.5ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 18/2/2/3 | 21/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.461–20.459).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 14 | 14.824 | 0.245 | 0.853 | 14.425–15.278 |
| 2 | 10 | 16.396 | 0.178 | 0.618 | 16.047–16.665 |
| 3 | 2 | 13.723 | 0.017 | 0.034 | 13.706–13.740 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 589 at 00:09.8 of the 21.4 s video above.
