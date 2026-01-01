# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-01 22:21:05 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 19252/19252 packets (18000 video, 1252 audio)
- ✅ Network Timing: span=5015.8ms, video_mean=278.7us, audio_mean=4006.6us
- ✅ Frame Processing: 1550 frames processed
- ✅ Video Recording: 9.4 MB
- ✅ Content Integrity: 18.9s duration

### Resource Usage

During the test's processing window (4.6s, 10 of 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 50.5% | 52.8% | 54.61% | 68.8% |
| RAM | 4287.1 MB | 4313.21 MB | 4308.8 MB | 4328.15 MB |
| GPU | 31.33% | 33.36% | 33.18% | 34.22% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 5015.795 ms
- Total packets analyzed: 19249

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19249 | 0.001 ms | 0.521 ms | 23.418 ms | 211.60% | 0.44% | 33.09% | 1171.250 |
| Video | 17998 | 0.001 ms | 0.279 ms | 23.418 ms | 202.92% | 0.47% | 28.46% | 559.500 |
| Audio | 1251 | 0.002 ms | 4.007 ms | 21.478 ms | 26.22% | 1.28% | 0.08% | 1.570 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17998 | 0.002 ms | 23.414 ms | 0 |
| Audio | 1251 | 0.547 ms | 17.241 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 6.9ms, max 9.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10841.0ms, video=10831.4ms (frame 648), diff=9.6ms
- 🟢 Pop #2 [R]: audio=11641.0ms, video=11633.7ms (frame 696), diff=7.3ms
- 🟢 Pop #3 [L]: audio=12441.0ms, video=12436.1ms (frame 744), diff=4.9ms
- 🟢 Pop #4 [R]: audio=13246.0ms, video=13238.4ms (frame 792), diff=7.6ms
- 🟢 Pop #5 [L]: audio=14046.0ms, video=14040.7ms (frame 840), diff=5.3ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (477 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.447–18.420).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 13.288 | 0.016 | 0.033 | 13.272–13.305 |
| 2 | 1 | 18.420 | 0.000 | 0.000 | 18.420–18.420 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 18.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 648 at 00:10.8 of the 18.9 s video above.
