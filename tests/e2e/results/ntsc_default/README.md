# C64 Stream E2E Test Report

## Scenario: NTSC Default

Generated: 2026-01-13 15:54:36 UTC
Git Branch: test/modularize-e2e
Git ID: 27dd99e
Environment: local

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

- ✅ UDP Packet Reception: Expected 30803, Received 30769, Missing 34 (0.11%)
- ✅ Network Timing: span=8014.6ms, video_mean=341.6us, audio_mean=4005.0us
- ✅ Frame Processing: 477 frames processed
- ✅ Video Recording: 9.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 51.4% | 58.2% | 58.01% | 77.9% |
| RAM | 5523.89 MB | 5598.1 MB | 5589.69 MB | 5617.04 MB |
| GPU | 30.43% | 89.83% | 86.55% | 95.65% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8014.566 ms
- Total packets analyzed: 25462

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 25462 | 0.001 ms | 0.629 ms | 13.528 ms | 170.07% | 10.27% | 15.80% | 16.623 |
| Video | 23462 | 0.001 ms | 0.342 ms | 8.306 ms | 108.63% | 11.13% | 8.64% | 8.182 |
| Audio | 2000 | 0.002 ms | 4.005 ms | 13.528 ms | 19.39% | 0.95% | 0.05% | 1.523 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 23462 | 0.017 ms | 8.026 ms | 1 (0.0%) |
| Audio | 2000 | 0.114 ms | 9.529 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 8.8ms, max 10.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9921.0ms, video=9912.1ms (frame 593), diff=8.9ms
- 🟢 Pop #2 [R]: audio=10721.0ms, video=10714.4ms (frame 641), diff=6.6ms
- 🟢 Pop #3 [L]: audio=11527.0ms, video=11516.7ms (frame 689), diff=10.3ms
- 🟢 Pop #4 [R]: audio=12328.0ms, video=12319.1ms (frame 737), diff=8.9ms
- 🟢 Pop #5 [L]: audio=13129.0ms, video=13121.4ms (frame 785), diff=7.6ms
- 🟢 Pop #6 [R]: audio=13934.0ms, video=13923.7ms (frame 833), diff=10.3ms
- 🟢 Pop #7 [L]: audio=14735.0ms, video=14726.0ms (frame 881), diff=9.0ms
- 🟢 Pop #8 [R]: audio=15536.0ms, video=15528.4ms (frame 929), diff=7.6ms
- 🟢 Pop #9 [L]: audio=16341.0ms, video=16330.7ms (frame 977), diff=10.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (477 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.528–17.501).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 12.938 | 0.000 | 0.000 | 12.938–12.938 |
| 2 | 1 | 13.991 | 0.000 | 0.000 | 13.991–13.991 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.4 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 593 at 00:09.9 of the 19.4 s video above.
