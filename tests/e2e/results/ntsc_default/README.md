# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-15 16:11:43 UTC
- Git Branch: feature/rest-control
- Git ID: 7c1ee20
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
- RAM: 31Gi total, 27Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30754, Missing 49 (0.16%)
- ✅ Network Timing: span=8010.8ms, video_mean=337.3us, audio_mean=4005.4us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.5% | 50.4% | 50.4% | 64.6% |
| RAM | 2713.3 MB | 2800.29 MB | 2797.1 MB | 2832.88 MB |
| GPU | 30.43% | 89.83% | 70.3% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8010.814 ms
- Total packets analyzed: 25747

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25747 | 0.001 ms | 0.622 ms | 6.654 ms | 168.97% | 8.63% | 15.40% | 16.279 |
| Video | 23747 | 0.001 ms | 0.337 ms | 3.086 ms | 100.60% | 9.35% | 8.29% | 8.079 |
| Audio | 2000 | 1.857 ms | 4.005 ms | 6.654 ms | 16.83% | 0.40% | 0.00% | 1.497 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23747 | 0.013 ms | 2.807 ms | 0 |
| Audio | 2000 | 0.058 ms | 2.653 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 93.0ms, max 773.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9839.0ms, video=9845.2ms (frame 589), diff=6.2ms
- 🟢 Pop #2 [R]: audio=10639.0ms, video=10630.8ms (frame 636), diff=8.2ms
- 🟢 Pop #3 [L]: audio=11445.0ms, video=11449.9ms (frame 685), diff=4.9ms
- 🟢 Pop #4 [R]: audio=12244.0ms, video=12252.2ms (frame 733), diff=8.2ms
- 🟢 Pop #5 [L]: audio=13047.0ms, video=13054.5ms (frame 781), diff=7.5ms
- 🟢 Pop #6 [R]: audio=13852.0ms, video=13856.9ms (frame 829), diff=4.9ms
- 🟢 Pop #7 [L]: audio=14652.0ms, video=14659.2ms (frame 877), diff=7.2ms
- • Pop #8 [R]: audio=15474.0ms, video=16247.1ms (frame 972), diff=773.1ms
- 🟢 Pop #9 [L]: audio=16281.0ms, video=16263.8ms (frame 973), diff=17.2ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 1/3/3/3 | 1/2/2/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.461–20.476).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 15.462 | 0.016 | 0.033 | 15.445–15.478 |
| 2 | 1 | 10.464 | 0.000 | 0.000 | 10.464–10.464 |

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
- Taken from frame 589 at 00:09.8 of the 21.4 s video above.
