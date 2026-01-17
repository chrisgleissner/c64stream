# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-17 17:00:27 UTC
- Git Branch: fix/improve-keyboard-mappings
- Git ID: 5886543
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1022G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30737, Missing 66 (0.21%)
- ✅ Network Timing: span=8005.1ms, video_mean=368.2us, audio_mean=4004.7us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 62.8% | 72.7% | 72.51% | 83.5% |
| RAM | 4818.62 MB | 4901.2 MB | 4896.51 MB | 4931.7 MB |
| GPU | 17% | 21% | 29.06% | 45% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8005.113 ms
- Total packets analyzed: 23737

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23737 | 0.001 ms | 0.674 ms | 14.117 ms | 170.50% | 18.70% | 18.90% | 17.435 |
| Video | 21739 | 0.001 ms | 0.368 ms | 14.117 ms | 135.51% | 20.37% | 11.62% | 9.779 |
| Audio | 1998 | 0.004 ms | 4.005 ms | 8.951 ms | 23.48% | 2.45% | 0.05% | 1.666 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21739 | 0.044 ms | 13.836 ms | 0 |
| Audio | 1998 | 0.354 ms | 4.955 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 15.9ms, max 17.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9761.0ms, video=9744.9ms (frame 583), diff=16.1ms
- 🟢 Pop #2 [R]: audio=10562.0ms, video=10547.3ms (frame 631), diff=14.7ms
- 🟢 Pop #3 [L]: audio=11367.0ms, video=11349.6ms (frame 679), diff=17.4ms
- 🟢 Pop #4 [R]: audio=12167.0ms, video=12151.9ms (frame 727), diff=15.1ms
- 🟢 Pop #5 [L]: audio=12970.0ms, video=12954.2ms (frame 775), diff=15.8ms
- 🟢 Pop #6 [R]: audio=13774.0ms, video=13756.6ms (frame 823), diff=17.4ms
- 🟢 Pop #7 [L]: audio=14575.0ms, video=14558.9ms (frame 871), diff=16.1ms
- 🟢 Pop #8 [R]: audio=15375.0ms, video=15361.2ms (frame 919), diff=13.8ms
- 🟢 Pop #9 [L]: audio=16180.0ms, video=16163.5ms (frame 967), diff=16.5ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/2/2/2 | 2/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.360–20.359).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 13.866 | 0.009 | 0.017 | 13.857–13.874 |
| 2 | 1 | 17.200 | 0.000 | 0.000 | 17.200–17.200 |

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
- Taken from frame 583 at 00:09.7 of the 21.3 s video above.
