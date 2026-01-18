# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-18 14:50:59 UTC
- Git Branch: feat/c64script-extension
- Git ID: 3ad75ae
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
- RAM: 31Gi total, 20Gi available
- Disk (/): 1.8T total, 1018G available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30775, Missing 28 (0.09%)
- ✅ Network Timing: span=8015.4ms, video_mean=368.6us, audio_mean=4005.2us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.7 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.4s, 35 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 58.8% | 72.7% | 73.53% | 90.8% |
| RAM | 10152.78 MB | 10265.82 MB | 10263.28 MB | 10379.29 MB |
| GPU | 15% | 39% | 30.4% | 45% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8015.419 ms
- Total packets analyzed: 23747

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23747 | 0.001 ms | 0.675 ms | 15.400 ms | 170.55% | 17.06% | 18.88% | 17.540 |
| Video | 21746 | 0.001 ms | 0.369 ms | 7.858 ms | 131.56% | 18.56% | 11.61% | 9.498 |
| Audio | 2001 | 0.002 ms | 4.005 ms | 15.400 ms | 25.75% | 3.00% | 0.20% | 1.725 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21746 | 0.044 ms | 7.577 ms | 0 |
| Audio | 2001 | 0.308 ms | 11.400 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 12.2ms, max 13.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=10075.0ms, video=10062.5ms (frame 602), diff=12.5ms
- 🟢 Pop #2 [R]: audio=10876.0ms, video=10864.8ms (frame 650), diff=11.2ms
- 🟢 Pop #3 [L]: audio=11681.0ms, video=11667.2ms (frame 698), diff=13.8ms
- 🟢 Pop #4 [R]: audio=12482.0ms, video=12469.5ms (frame 746), diff=12.5ms
- 🟢 Pop #5 [L]: audio=13282.0ms, video=13271.8ms (frame 794), diff=10.2ms
- 🟢 Pop #6 [R]: audio=14087.0ms, video=14074.1ms (frame 842), diff=12.9ms
- 🟢 Pop #7 [L]: audio=14889.0ms, video=14876.5ms (frame 890), diff=12.5ms
- 🟢 Pop #8 [R]: audio=15689.0ms, video=15678.8ms (frame 938), diff=10.2ms
- 🟢 Pop #9 [L]: audio=16495.0ms, video=16481.1ms (frame 986), diff=13.9ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (478 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/2/2/2 | 1/1/1/1 | 0 | 0 |
| After settling | 12/2/2/3 | 13/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.678–20.660).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 11 | 16.220 | 0.089 | 0.284 | 16.063–16.347 |
| 2 | 9 | 14.802 | 0.110 | 0.368 | 14.659–15.027 |
| 3 | 2 | 13.414 | 0.008 | 0.016 | 13.406–13.422 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 21.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 602 at 00:10.1 of the 21.7 s video above.
