# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-22 16:58:17 UTC
- Git Branch: cursor/c64-stream-effects-filter-4ac5
- Git ID: be4430f
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
- RAM: 31Gi total, 17Gi available
- Disk (/): 1.8T total, 973G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30752, Missing 51 (0.17%)
- ✅ Network Timing: span=8009.4ms, video_mean=411.7us, audio_mean=4004.9us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 56% | 77.8% | 77.76% | 95.8% |
| RAM | 10527.57 MB | 10644.51 MB | 10841.16 MB | 11344.58 MB |
| GPU | 18% | 37% | 32.6% | 44% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8009.416 ms
- Total packets analyzed: 21455

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 21455 | 0.001 ms | 0.746 ms | 14.707 ms | 174.25% | 22.67% | 21.32% | 19.912 |
| Video | 19456 | 0.001 ms | 0.412 ms | 11.638 ms | 164.05% | 24.74% | 13.67% | 12.484 |
| Audio | 1999 | 0.002 ms | 4.005 ms | 14.707 ms | 35.40% | 6.15% | 1.25% | 2.064 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 19456 | 0.060 ms | 11.359 ms | 1 (0.0%) |
| Audio | 1999 | 0.501 ms | 10.707 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 96.2ms, max 776.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9785.0ms, video=9778.4ms (frame 585), diff=6.6ms
- 🟢 Pop #2 [R]: audio=10586.0ms, video=10580.7ms (frame 633), diff=5.3ms
- 🟢 Pop #3 [L]: audio=11391.0ms, video=11383.0ms (frame 681), diff=8.0ms
- 🟢 Pop #4 [R]: audio=12193.0ms, video=12185.3ms (frame 729), diff=7.7ms
- 🟢 Pop #5 [L]: audio=12994.0ms, video=12970.9ms (frame 776), diff=23.1ms
- • Pop #6 [R]: audio=13799.0ms, video=14575.6ms (frame 872), diff=776.6ms
- 🟢 Pop #7 [L]: audio=14600.0ms, video=14592.3ms (frame 873), diff=7.7ms
- 🟢 Pop #8 [R]: audio=15401.0ms, video=15394.6ms (frame 921), diff=6.4ms
- 🟢 Pop #9 [L]: audio=16205.0ms, video=16180.3ms (frame 968), diff=24.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 15/2/2/3 | 15/1/1/2 | 0 | 0 |
| After settling | 19/2/2/3 | 22/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.394–20.376).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 41 | 13.409 | 0.613 | 2.107 | 12.469–14.576 |
| 2 | 10 | 16.015 | 0.283 | 0.769 | 15.545–16.314 |
| 3 | 4 | 10.756 | 0.084 | 0.184 | 10.664–10.848 |

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
- Taken from frame 585 at 00:09.8 of the 21.5 s video above.
