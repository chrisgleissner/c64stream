# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

- Generated: 2026-01-23 12:45:58 UTC
- Git Branch: doc/improvements
- Git ID: bd15461
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
- RAM: 31Gi total, 26Gi available
- Disk (/): 1.8T total, 962G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30744, Missing 59 (0.19%)
- ✅ Network Timing: span=8006.8ms, video_mean=420.9us, audio_mean=4004.8us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 87.2% | 88.75% | 88.95% | 94% |
| RAM | 4695.55 MB | 4828.29 MB | 4824.5 MB | 4884.32 MB |
| GPU | 0% | 1% | 19.65% | 66% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8006.849 ms
- Total packets analyzed: 21016

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 21016 | 0.001 ms | 0.762 ms | 14.660 ms | 182.65% | 19.69% | 21.22% | 22.102 |
| Video | 19017 | 0.001 ms | 0.421 ms | 12.205 ms | 178.86% | 21.29% | 13.50% | 13.078 |
| Audio | 1999 | 0.001 ms | 4.005 ms | 14.660 ms | 45.63% | 10.11% | 3.70% | 2.719 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 19017 | 0.049 ms | 11.922 ms | 0 |
| Audio | 1999 | 0.528 ms | 10.659 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 23.0ms, max 28.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9872.0ms, video=9845.2ms (frame 589), diff=26.8ms
- 🟢 Pop #2 [R]: audio=10673.0ms, video=10647.5ms (frame 637), diff=25.5ms
- 🟢 Pop #3 [L]: audio=11478.0ms, video=11449.9ms (frame 685), diff=28.1ms
- 🟢 Pop #4 [R]: audio=12279.0ms, video=12252.2ms (frame 733), diff=26.8ms
- 🟢 Pop #5 [L]: audio=13079.0ms, video=13054.5ms (frame 781), diff=24.5ms
- 🟢 Pop #6 [R]: audio=13884.0ms, video=13856.9ms (frame 829), diff=27.1ms
- 🟢 Pop #7 [L]: audio=14686.0ms, video=14659.2ms (frame 877), diff=26.8ms
- 🟢 Pop #8 [R]: audio=15487.0ms, video=15478.2ms (frame 926), diff=8.8ms
- 🟢 Pop #9 [L]: audio=16293.0ms, video=16280.5ms (frame 974), diff=12.5ms

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
- Duration: 21.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 589 at 00:09.8 of the 21.3 s video above.
