# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

- Generated: 2026-01-23 12:55:23 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30792, Missing 11 (0.04%)
- ✅ Network Timing: span=8022.8ms, video_mean=408.4us, audio_mean=4004.4us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 34 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 92.9% | 94.2% | 94.22% | 95.1% |
| RAM | 4735.86 MB | 4852.05 MB | 4840.83 MB | 4898.25 MB |
| GPU | 0% | 0% | 8.09% | 50% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8022.823 ms
- Total packets analyzed: 21645

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 21645 | 0.001 ms | 0.741 ms | 21.589 ms | 197.96% | 17.51% | 17.01% | 23.350 |
| Video | 19643 | 0.001 ms | 0.408 ms | 20.768 ms | 203.74% | 18.63% | 9.32% | 13.258 |
| Audio | 2002 | 0.001 ms | 4.004 ms | 21.589 ms | 54.35% | 12.29% | 3.80% | 3.094 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 19643 | 0.032 ms | 20.485 ms | 0 |
| Audio | 2002 | 0.538 ms | 17.589 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 13.2ms, max 30.8ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9855.0ms, video=9861.9ms (frame 590), diff=6.9ms
- 🟢 Pop #2 [R]: audio=10676.0ms, video=10664.3ms (frame 638), diff=11.7ms
- 🟢 Pop #3 [L]: audio=11482.0ms, video=11483.3ms (frame 687), diff=1.3ms
- 🟢 Pop #4 [R]: audio=12283.0ms, video=12252.2ms (frame 733), diff=30.8ms
- 🟢 Pop #5 [L]: audio=13085.0ms, video=13054.5ms (frame 781), diff=30.5ms
- 🟢 Pop #6 [R]: audio=13889.0ms, video=13873.6ms (frame 830), diff=15.4ms
- 🟢 Pop #7 [L]: audio=14691.0ms, video=14692.6ms (frame 879), diff=1.6ms
- 🟢 Pop #8 [R]: audio=15491.0ms, video=15494.9ms (frame 927), diff=3.9ms
- 🟢 Pop #9 [L]: audio=16297.0ms, video=16280.5ms (frame 974), diff=16.5ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 5/2/21/242 | 1/1/1/1 | 2 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–17.451).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 4.196 | 0.000 | 0.000 | 4.196–4.196 |

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
- Taken from frame 590 at 00:09.9 of the 21.3 s video above.
