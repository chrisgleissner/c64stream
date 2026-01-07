# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-07 15:04:43 UTC

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
- RAM: 31Gi total, 21Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28773 video, 1991 audio)
- ✅ Network Timing: span=8022.8ms, video_mean=278.6us, audio_mean=4001.4us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 11.0 MB
- ✅ Content Integrity: 22.2s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 94.5% | 95.25% | 95.48% | 98.3% |
| RAM | 7969.42 MB | 7986.15 MB | 7989.92 MB | 8017.93 MB |
| GPU | 8.7% | 48.01% | 40.48% | 51.03% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8022.825 ms
- Total packets analyzed: 30764

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30764 | 0.001 ms | 0.519 ms | 35.226 ms | 272.96% | 0.01% | 28.88% | 2121.333 |
| Video | 28771 | 0.001 ms | 0.279 ms | 35.226 ms | 320.28% | 0.01% | 24.44% | 1177.000 |
| Audio | 1989 | 0.002 ms | 4.001 ms | 24.682 ms | 64.22% | 13.68% | 5.08% | 3.534 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28771 | 0.001 ms | 35.223 ms | 0 |
| Audio | 1989 | 1.027 ms | 20.455 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 23.7ms, max 30.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=11060.0ms, video=11032.0ms (frame 660), diff=28.0ms
- 🟢 Pop #2 [R]: audio=11862.0ms, video=11834.3ms (frame 708), diff=27.7ms
- 🟢 Pop #3 [L]: audio=12662.0ms, video=12636.6ms (frame 756), diff=25.4ms
- 🟢 Pop #4 [R]: audio=13468.0ms, video=13439.0ms (frame 804), diff=29.0ms
- 🟢 Pop #5 [L]: audio=14268.0ms, video=14241.3ms (frame 852), diff=26.7ms
- 🟢 Pop #6 [R]: audio=15070.0ms, video=15060.3ms (frame 901), diff=9.7ms
- 🟢 Pop #7 [L]: audio=15876.0ms, video=15846.0ms (frame 948), diff=30.0ms
- 🟢 Pop #8 [R]: audio=16676.0ms, video=16648.3ms (frame 996), diff=27.7ms
- 🟢 Pop #9 [L]: audio=17476.0ms, video=17467.3ms (frame 1045), diff=8.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/21/419 | 1/1/1/1 | 2 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 22.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 660 at 00:11.0 of the 22.2 s video above.
