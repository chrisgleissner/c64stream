# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-22 19:39:25 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: c325f69
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
- Disk (/): 1.8T total, 972G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30762, Missing 41 (0.13%)
- ✅ Network Timing: span=8013.3ms, video_mean=359.7us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 58.2% | 67.3% | 67.56% | 84.7% |
| RAM | 11729.93 MB | 11845.84 MB | 11834.42 MB | 11894.41 MB |
| GPU | 0% | 0% | 0% | 0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8013.300 ms
- Total packets analyzed: 24278

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 24278 | 0.001 ms | 0.660 ms | 7.365 ms | 168.58% | 14.39% | 17.84% | 17.106 |
| Video | 22278 | 0.001 ms | 0.360 ms | 6.947 ms | 121.51% | 15.66% | 10.56% | 9.093 |
| Audio | 2000 | 0.285 ms | 4.005 ms | 7.365 ms | 20.98% | 1.50% | 0.00% | 1.612 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 22278 | 0.032 ms | 6.667 ms | 0 |
| Audio | 2000 | 0.275 ms | 3.716 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 1.0ms, max 2.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9778.0ms, video=9778.4ms (frame 585), diff=0.4ms
- 🟢 Pop #2 [R]: audio=10580.0ms, video=10580.7ms (frame 633), diff=0.7ms
- 🟢 Pop #3 [L]: audio=11384.0ms, video=11383.0ms (frame 681), diff=1.0ms
- 🟢 Pop #4 [R]: audio=12186.0ms, video=12185.3ms (frame 729), diff=0.7ms
- 🟢 Pop #5 [L]: audio=12986.0ms, video=12987.7ms (frame 777), diff=1.7ms
- 🟢 Pop #6 [R]: audio=13791.0ms, video=13790.0ms (frame 825), diff=1.0ms
- 🟢 Pop #7 [L]: audio=14593.0ms, video=14592.3ms (frame 873), diff=0.7ms
- 🟢 Pop #8 [R]: audio=15394.0ms, video=15394.6ms (frame 921), diff=0.6ms
- 🟢 Pop #9 [L]: audio=16199.0ms, video=16197.0ms (frame 969), diff=2.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.394–20.376).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 14.476 | 0.034 | 0.067 | 14.442–14.509 |
| 2 | 2 | 10.706 | 0.008 | 0.016 | 10.698–10.714 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 585 at 00:09.8 of the 21.3 s video above.
