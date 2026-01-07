# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2026-01-07 11:59:22 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.2

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28770 video, 1991 audio)
- ✅ Network Timing: span=8026.6ms, video_mean=278.1us, audio_mean=4001.9us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.2% | 92.05% | 92.61% | 96.2% |
| RAM | 6260.76 MB | 6288.59 MB | 6292.19 MB | 6346.04 MB |
| GPU | 11.83% | 49.97% | 43.69% | 51.91% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8026.584 ms
- Total packets analyzed: 30764

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30764 | 0.001 ms | 0.519 ms | 29.302 ms | 258.50% | 0.01% | 27.23% | 1524.250 |
| Video | 28769 | 0.001 ms | 0.278 ms | 29.302 ms | 296.86% | 0.01% | 26.11% | 1090.667 |
| Audio | 1989 | 0.003 ms | 4.002 ms | 27.761 ms | 55.90% | 11.61% | 4.02% | 2.792 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28769 | 0.001 ms | 29.299 ms | 0 |
| Audio | 1989 | 0.981 ms | 23.549 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 17.7ms, max 34.7ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11052.0ms, video=11048.7ms (frame 661), diff=3.3ms
- 🟢 Pop #2 [R]: audio=11852.0ms, video=11834.3ms (frame 708), diff=17.7ms
- 🟢 Pop #3 [L]: audio=12652.0ms, video=12619.9ms (frame 755), diff=32.1ms
- 🟢 Pop #4 [R]: audio=13457.0ms, video=13422.3ms (frame 803), diff=34.7ms
- 🟢 Pop #5 [L]: audio=14260.0ms, video=14241.3ms (frame 852), diff=18.7ms
- 🟢 Pop #6 [R]: audio=15060.0ms, video=15043.6ms (frame 900), diff=16.4ms
- 🟢 Pop #7 [L]: audio=15865.0ms, video=15846.0ms (frame 948), diff=19.0ms
- 🟢 Pop #8 [R]: audio=16665.0ms, video=16665.0ms (frame 997), diff=0.0ms
- 🟢 Pop #9 [L]: audio=17468.0ms, video=17450.6ms (frame 1044), diff=17.4ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 7/4/6/434 | 6/1/5/5 | 1 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 661 at 00:11.0 of the 22.1 s video above.
