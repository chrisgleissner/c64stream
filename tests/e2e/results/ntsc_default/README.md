# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-22 15:26:41 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: e7819f0
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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 973G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30773, Missing 30 (0.1%)
- ✅ Network Timing: span=8013.0ms, video_mean=342.5us, audio_mean=4004.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 52.9% | 57.5% | 58.39% | 76.7% |
| RAM | 8541.27 MB | 8578.64 MB | 8581.61 MB | 8632.14 MB |
| GPU | 14% | 36% | 32.74% | 46% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8013.002 ms
- Total packets analyzed: 25396

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25396 | 0.001 ms | 0.631 ms | 10.936 ms | 168.94% | 10.59% | 16.10% | 16.578 |
| Video | 23395 | 0.001 ms | 0.343 ms | 4.819 ms | 105.59% | 11.48% | 8.96% | 8.068 |
| Audio | 2001 | 0.002 ms | 4.004 ms | 10.936 ms | 18.42% | 0.90% | 0.05% | 1.515 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23395 | 0.021 ms | 4.539 ms | 0 |
| Audio | 2001 | 0.113 ms | 6.935 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 17.5ms, max 23.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9846.0ms, video=9845.2ms (frame 589), diff=0.8ms
- 🟢 Pop #2 [R]: audio=10647.0ms, video=10647.5ms (frame 637), diff=0.5ms
- 🟢 Pop #3 [L]: audio=11473.0ms, video=11449.9ms (frame 685), diff=23.1ms
- 🟢 Pop #4 [R]: audio=12274.0ms, video=12252.2ms (frame 733), diff=21.8ms
- 🟢 Pop #5 [L]: audio=13076.0ms, video=13054.5ms (frame 781), diff=21.5ms
- 🟢 Pop #6 [R]: audio=13880.0ms, video=13856.9ms (frame 829), diff=23.1ms
- 🟢 Pop #7 [L]: audio=14681.0ms, video=14659.2ms (frame 877), diff=21.8ms
- 🟢 Pop #8 [R]: audio=15483.0ms, video=15461.5ms (frame 925), diff=21.5ms
- 🟢 Pop #9 [L]: audio=16287.0ms, video=16263.8ms (frame 973), diff=23.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 18/2/2/2 | 18/1/1/1 | 0 | 0 |
| After settling | 14/2/2/2 | 16/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.461–20.459).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 17 | 13.948 | 0.841 | 3.042 | 12.286–15.328 |
| 2 | 10 | 9.969 | 0.313 | 0.836 | 9.544–10.380 |
| 3 | 4 | 11.592 | 0.166 | 0.401 | 11.366–11.767 |

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
