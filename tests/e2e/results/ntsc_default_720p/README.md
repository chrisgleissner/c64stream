# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2026-01-01 18:20:21 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28800 video, 2003 audio)
- ⚠️ Network Timing: span=8023.0ms, video_mean=278.6us, audio_mean=4005.0us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.3s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 42.1% | 56.05% | 53.68% | 66.2% |
| RAM | 6616.78 MB | 6697.78 MB | 6689.64 MB | 6762.79 MB |
| GPU | 24.02% | 29.14% | 30.57% | 39.62% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8022.983 ms
- Total packets analyzed: 30800

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30800 | 0.001 ms | 0.521 ms | 27.933 ms | 211.19% | 6.94% | 32.59% | 897.200 |
| Video | 28799 | 0.001 ms | 0.279 ms | 27.933 ms | 200.65% | 0.04% | 29.71% | 494.500 |
| Audio | 2001 | 0.003 ms | 4.005 ms | 25.132 ms | 26.55% | 1.35% | 0.20% | 1.407 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28799 | 0.001 ms | 27.929 ms | 0 |
| Audio | 2001 | 0.325 ms | 20.877 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 9.3ms, max 13.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11196.0ms, video=11182.4ms (frame 669), diff=13.6ms
- 🟢 Pop #2 [R]: audio=11996.0ms, video=11984.8ms (frame 717), diff=11.2ms
- 🟢 Pop #3 [L]: audio=12796.0ms, video=12787.1ms (frame 765), diff=8.9ms
- 🟢 Pop #4 [R]: audio=13601.0ms, video=13589.4ms (frame 813), diff=11.6ms
- 🟢 Pop #5 [L]: audio=14401.0ms, video=14391.7ms (frame 861), diff=9.3ms
- 🟢 Pop #6 [R]: audio=15201.0ms, video=15194.1ms (frame 909), diff=6.9ms
- 🟢 Pop #7 [L]: audio=16006.0ms, video=15996.4ms (frame 957), diff=9.6ms
- 🟢 Pop #8 [R]: audio=16806.0ms, video=16798.7ms (frame 1005), diff=7.3ms
- 🟢 Pop #9 [L]: audio=17606.0ms, video=17601.0ms (frame 1053), diff=5.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (473 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 4/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.798–21.780).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 4 | 18.115 | 0.079 | 0.201 | 17.985–18.186 |
| 2 | 1 | 12.988 | 0.000 | 0.000 | 12.988–12.988 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 669 at 00:11.2 of the 22.3 s video above.
