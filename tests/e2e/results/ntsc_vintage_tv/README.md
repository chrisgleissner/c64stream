# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-07 12:11:41 UTC

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

- ✅ UDP Packet Reception: 30803/30803 packets (28777 video, 1996 audio)
- ✅ Network Timing: span=8023.2ms, video_mean=278.7us, audio_mean=4008.9us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 94.5% | 95.2% | 95.61% | 98.8% |
| RAM | 6500.29 MB | 6529.98 MB | 6537.17 MB | 6593.26 MB |
| GPU | 13.39% | 45.26% | 40.29% | 51.66% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8023.171 ms
- Total packets analyzed: 30774

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30774 | 0.001 ms | 0.521 ms | 30.430 ms | 267.21% | 0.01% | 29.44% | 2121.667 |
| Video | 28775 | 0.001 ms | 0.279 ms | 30.430 ms | 297.75% | 0.01% | 25.05% | 1182.667 |
| Audio | 1995 | 0.003 ms | 4.009 ms | 30.135 ms | 65.55% | 15.09% | 5.31% | 3.087 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28775 | 0.001 ms | 30.427 ms | 0 |
| Audio | 1995 | 1.035 ms | 25.902 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 100.1ms, max 774.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11006.0ms, video=11015.3ms (frame 659), diff=9.3ms
- • Pop #2 [R]: audio=11806.0ms, video=11032.0ms (frame 660), diff=774.0ms
- 🟢 Pop #3 [L]: audio=12609.0ms, video=12603.2ms (frame 754), diff=5.8ms
- 🟢 Pop #4 [R]: audio=13436.0ms, video=13405.5ms (frame 802), diff=30.5ms
- 🟢 Pop #5 [L]: audio=14236.0ms, video=14207.9ms (frame 850), diff=28.1ms
- 🟢 Pop #6 [R]: audio=15036.0ms, video=15026.9ms (frame 899), diff=9.1ms
- 🟢 Pop #7 [L]: audio=15841.0ms, video=15846.0ms (frame 948), diff=5.0ms
- 🟢 Pop #8 [R]: audio=16644.0ms, video=16614.8ms (frame 994), diff=29.2ms
- 🟢 Pop #9 [L]: audio=17444.0ms, video=17433.9ms (frame 1043), diff=10.1ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/21/415 | 1/1/1/1 | 2 | 0 |
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
- Taken from frame 660 at 00:11.0 of the 22.1 s video above.
