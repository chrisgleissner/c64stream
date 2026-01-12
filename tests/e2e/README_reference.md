# C64 Stream E2E Test Report

## Scenario: NTSC Phosphor Glow

Generated: 2026-01-10 20:45:28 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 19220/19252 packets (17970 video, 1250 audio, minor loss)
- ⚠️ Network Timing: span=15482.2ms, video_mean=424.0us, audio_mean=4004.9us
- ✅ Frame Processing: 1550 frames processed
- ✅ Video Recording: 9.4 MB
- ✅ Content Integrity: 18.9s duration

### Resource Usage

During the test's processing window (15.2s, 31 of 43 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 90.2% | 91.5% | 92.22% | 97.7% |
| RAM | 6071.57 MB | 6379.09 MB | 6398.83 MB | 6931.01 MB |
| GPU | 9.83% | 12.92% | 14.25% | 26.59% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 15482.241 ms
- Total packets analyzed: 13053

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 13053 | 0.001 ms | 0.767 ms | 14.897 ms | 182.76% | 24.03% | 22.50% | 22.357 |
| Video | 11804 | 0.001 ms | 0.424 ms | 11.944 ms | 182.48% | 26.13% | 14.84% | 13.206 |
| Audio | 1249 | 0.001 ms | 4.005 ms | 14.897 ms | 45.10% | 10.01% | 3.12% | 2.622 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 11804 | 0.059 ms | 11.662 ms | 0 |
| Audio | 1249 | 0.586 ms | 10.897 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 12.7ms, max 21.8ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10784.0ms, video=10781.3ms (frame 645), diff=2.7ms
- 🟢 Pop #2 [R]: audio=11585.0ms, video=11583.6ms (frame 693), diff=1.4ms
- 🟢 Pop #3 [L]: audio=12391.0ms, video=12369.2ms (frame 740), diff=21.8ms
- 🟢 Pop #4 [R]: audio=13190.0ms, video=13171.5ms (frame 788), diff=18.5ms
- 🟢 Pop #5 [L]: audio=13993.0ms, video=13973.9ms (frame 836), diff=19.1ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 9/2/6/421 | 5/1/4/5 | 2 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- No post-settling repeated/skipped markers detected in playback timeline.

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
- Taken from frame 645 at 00:10.8 of the 18.9 s video above.
