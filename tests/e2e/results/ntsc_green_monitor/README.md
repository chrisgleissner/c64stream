# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2026-01-13 15:10:55 UTC

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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: 30748 packets (28747 video, 2001 audio)
- ✅ Network Timing: span=8006.3ms, video_mean=450.4us, audio_mean=4003.1us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 92.2% | 93.1% | 93.27% | 95% |
| RAM | 5929.38 MB | 5965.22 MB | 5974.85 MB | 6027.82 MB |
| GPU | 30.43% | 79.74% | 71.83% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8006.290 ms
- Total packets analyzed: 19775

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19775 | 0.001 ms | 0.810 ms | 16.136 ms | 191.74% | 25.27% | 22.32% | 26.134 |
| Video | 17775 | 0.001 ms | 0.450 ms | 14.829 ms | 200.75% | 27.19% | 14.67% | 14.712 |
| Audio | 2000 | 0.001 ms | 4.003 ms | 16.136 ms | 57.09% | 13.85% | 5.45% | 3.142 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17775 | 0.075 ms | 14.548 ms | 0 |
| Audio | 2000 | 0.757 ms | 12.139 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 18.1ms, max 24.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9769.0ms, video=9744.9ms (frame 583), diff=24.1ms
- 🟢 Pop #2 [R]: audio=10569.0ms, video=10547.3ms (frame 631), diff=21.7ms
- 🟢 Pop #3 [L]: audio=11375.0ms, video=11366.3ms (frame 680), diff=8.7ms
- 🟢 Pop #4 [R]: audio=12175.0ms, video=12151.9ms (frame 727), diff=23.1ms
- 🟢 Pop #5 [L]: audio=12976.0ms, video=12954.2ms (frame 775), diff=21.8ms
- 🟢 Pop #6 [R]: audio=13781.0ms, video=13756.6ms (frame 823), diff=24.4ms
- 🟢 Pop #7 [L]: audio=14583.0ms, video=14575.6ms (frame 872), diff=7.4ms
- 🟢 Pop #8 [R]: audio=15384.0ms, video=15361.2ms (frame 919), diff=22.8ms
- 🟢 Pop #9 [L]: audio=16189.0ms, video=16180.3ms (frame 968), diff=8.7ms

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
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 583 at 00:09.7 of the 19.2 s video above.
