# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

- Generated: 2026-01-15 16:12:46 UTC
- Git Branch: feature/rest-control
- Git ID: 7c1ee20
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
- RAM: 31Gi total, 28Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30801, Missing 2 (0.01%)
- ✅ Network Timing: span=8023.3ms, video_mean=325.7us, audio_mean=4005.1us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 42.2% | 49.7% | 49.45% | 53.7% |
| RAM | 2511.58 MB | 2596.57 MB | 2590.36 MB | 2606.65 MB |
| GPU | 30.43% | 68.09% | 62.43% | 97.13% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.343 ms
- Total packets analyzed: 26639

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 26639 | 0.001 ms | 0.602 ms | 6.464 ms | 171.50% | 10.20% | 13.38% | 15.375 |
| Video | 24637 | 0.001 ms | 0.326 ms | 2.867 ms | 98.38% | 10.91% | 6.45% | 7.743 |
| Audio | 2002 | 1.779 ms | 4.005 ms | 6.464 ms | 15.96% | 0.20% | 0.00% | 1.475 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 24637 | 0.031 ms | 2.583 ms | 0 |
| Audio | 2002 | 0.063 ms | 2.462 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.2ms, max 10.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9903.0ms, video=9895.4ms (frame 592), diff=7.6ms
- 🟢 Pop #2 [R]: audio=10703.0ms, video=10697.7ms (frame 640), diff=5.3ms
- 🟢 Pop #3 [L]: audio=11509.0ms, video=11500.0ms (frame 688), diff=9.0ms
- 🟢 Pop #4 [R]: audio=12308.0ms, video=12302.3ms (frame 736), diff=5.7ms
- 🟢 Pop #5 [L]: audio=13111.0ms, video=13104.7ms (frame 784), diff=6.3ms
- 🟢 Pop #6 [R]: audio=13916.0ms, video=13907.0ms (frame 832), diff=9.0ms
- 🟢 Pop #7 [L]: audio=14716.0ms, video=14709.3ms (frame 880), diff=6.7ms
- 🟢 Pop #8 [R]: audio=15517.0ms, video=15511.7ms (frame 928), diff=5.3ms
- 🟢 Pop #9 [L]: audio=16324.0ms, video=16314.0ms (frame 976), diff=10.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.511–20.493).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 11.801 | 0.000 | 0.000 | 11.801–11.801 |

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
- Taken from frame 592 at 00:09.9 of the 21.4 s video above.
