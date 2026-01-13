# C64 Stream E2E Test Report

## Scenario: Unknown

Generated: 2026-01-08 19:21:14 UTC

## Test configuration

- Format: PAL
- Frames: 180
- Duration: 3.6 seconds
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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ⚠️ UDP Packet Reception: 13095/13137 packets (12177 video, 887 audio, minor loss)
- ✅ Network Timing: span=3579.1ms, video_mean=293.7us, audio_mean=4004.0us
- ✅ Frame Processing: 1075 frames processed
- ✅ Video Recording: 8.6 MB
- ✅ Content Integrity: 17.4s duration

### Resource Usage

During the test's processing window (3.1s, 7 of 40 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 47.1% | 59.5% | 57.01% | 63.3% |
| RAM | 7949.17 MB | 7956.25 MB | 7966.47 MB | 8015.84 MB |
| GPU | 31.8% | 36.78% | 36.44% | 42.35% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 12240 video, 897 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 3579.140 ms
- Total packets analyzed: 13063

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 13063 | 0.001 ms | 0.545 ms | 8.838 ms | 204.10% | 10.35% | 34.18% | 960.200 |
| Video | 12175 | 0.001 ms | 0.294 ms | 5.382 ms | 191.13% | 0.20% | 31.75% | 591.500 |
| Audio | 886 | 0.006 ms | 4.004 ms | 8.838 ms | 26.30% | 2.71% | 0.11% | 1.613 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 12175 | 0.002 ms | 5.378 ms | 0 |
| Audio | 886 | 0.581 ms | 4.591 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 7.1ms, max 8.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10841.0ms, video=10832.9ms (frame 543), diff=8.1ms
- 🟢 Pop #2 [R]: audio=11798.0ms, video=11790.5ms (frame 591), diff=7.5ms
- 🟢 Pop #3 [L]: audio=12754.0ms, video=12748.1ms (frame 639), diff=5.9ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (328 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 15/2/2/2 | 21/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 4/1/3/3 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 10.374–16.918).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 22 | 12.272 | 1.002 | 3.351 | 10.494–13.845 |
| 2 | 2 | 14.863 | 0.100 | 0.200 | 14.763–14.963 |
| 3 | 2 | 16.908 | 0.010 | 0.020 | 16.898–16.918 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 17.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 543 at 00:10.9 of the 17.4 s video above.
