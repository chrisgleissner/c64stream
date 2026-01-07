# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-07 14:39:55 UTC

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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30803/30803 packets (28732 video, 1965 audio)
- ✅ Network Timing: span=8021.5ms, video_mean=278.3us, audio_mean=4008.6us
- ✅ Frame Processing: 2481 frames processed
- ✅ Video Recording: 10.9 MB
- ✅ Content Integrity: 22.1s duration

### Resource Usage

During the test's processing window (7.6s, 16 of 49 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 56.7% | 60.75% | 62.41% | 77.5% |
| RAM | 7844.74 MB | 7914.78 MB | 7899.87 MB | 7923.19 MB |
| GPU | 30.02% | 49.38% | 47.85% | 55.66% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8021.526 ms
- Total packets analyzed: 30701

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 30701 | 0.001 ms | 0.517 ms | 8.587 ms | 209.52% | 0.14% | 32.95% | 1174.500 |
| Video | 28730 | 0.001 ms | 0.278 ms | 5.077 ms | 197.81% | 0.15% | 28.35% | 580.250 |
| Audio | 1963 | 0.016 ms | 4.009 ms | 8.587 ms | 23.51% | 1.58% | 0.10% | 1.527 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 28730 | 0.001 ms | 5.073 ms | 0 |
| Audio | 1963 | 0.548 ms | 4.342 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 14.2ms, max 16.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10998.0ms, video=10981.8ms (frame 657), diff=16.2ms
- 🟢 Pop #2 [R]: audio=11798.0ms, video=11784.2ms (frame 705), diff=13.8ms
- 🟢 Pop #3 [L]: audio=12598.0ms, video=12586.5ms (frame 753), diff=11.5ms
- 🟢 Pop #4 [R]: audio=13404.0ms, video=13388.8ms (frame 801), diff=15.2ms
- 🟢 Pop #5 [L]: audio=14206.0ms, video=14191.2ms (frame 849), diff=14.8ms
- 🟢 Pop #6 [R]: audio=15006.0ms, video=14993.5ms (frame 897), diff=12.5ms
- 🟢 Pop #7 [L]: audio=15812.0ms, video=15795.8ms (frame 945), diff=16.2ms
- 🟢 Pop #8 [R]: audio=16612.0ms, video=16598.1ms (frame 993), diff=13.9ms
- 🟢 Pop #9 [L]: audio=17414.0ms, video=17400.5ms (frame 1041), diff=13.5ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
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
- Taken from frame 657 at 00:11.0 of the 22.1 s video above.
