# C64 Stream E2E Test Report

## Scenario: NTSC Arcade Cabinet

- Generated: 2026-01-23 12:29:18 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30766, Missing 37 (0.12%)
- ✅ Network Timing: span=8015.6ms, video_mean=436.6us, audio_mean=4004.6us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.1s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 88.4% | 90.95% | 90.56% | 93.4% |
| RAM | 4709.94 MB | 4807.21 MB | 4799.39 MB | 4845.89 MB |
| GPU | 0% | 0% | 5.26% | 31% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8015.551 ms
- Total packets analyzed: 20358

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 20358 | 0.001 ms | 0.787 ms | 14.100 ms | 179.40% | 21.27% | 23.18% | 22.577 |
| Video | 18358 | 0.001 ms | 0.437 ms | 11.438 ms | 172.06% | 23.04% | 15.47% | 13.319 |
| Audio | 2000 | 0.001 ms | 4.005 ms | 14.100 ms | 47.63% | 11.00% | 4.00% | 2.766 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 18358 | 0.054 ms | 11.156 ms | 0 |
| Audio | 2000 | 0.608 ms | 10.100 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 25.3ms, max 29.6ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9957.0ms, video=9928.8ms (frame 594), diff=28.2ms
- 🟢 Pop #2 [R]: audio=10756.0ms, video=10731.1ms (frame 642), diff=24.9ms
- 🟢 Pop #3 [L]: audio=11563.0ms, video=11533.4ms (frame 690), diff=29.6ms
- 🟢 Pop #4 [R]: audio=12362.0ms, video=12335.8ms (frame 738), diff=26.2ms
- 🟢 Pop #5 [L]: audio=13166.0ms, video=13154.8ms (frame 787), diff=11.2ms
- 🟢 Pop #6 [R]: audio=13969.0ms, video=13940.4ms (frame 834), diff=28.6ms
- 🟢 Pop #7 [L]: audio=14769.0ms, video=14742.8ms (frame 882), diff=26.2ms
- 🟢 Pop #8 [R]: audio=15570.0ms, video=15545.1ms (frame 930), diff=24.9ms
- 🟢 Pop #9 [L]: audio=16375.0ms, video=16347.4ms (frame 978), diff=27.6ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 9/2/4/349 | 0/0/0/0 | 5 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

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
- Taken from frame 594 at 00:09.9 of the 21.4 s video above.
