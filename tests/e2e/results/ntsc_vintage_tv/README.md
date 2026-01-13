# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-13 15:21:50 UTC

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

- ✅ UDP Packet Reception: 30738 packets (28739 video, 1999 audio)
- ✅ Network Timing: span=8006.0ms, video_mean=447.0us, audio_mean=4004.8us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.3s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 93.8% | 95.8% | 95.74% | 97.9% |
| RAM | 5754.82 MB | 5926.86 MB | 5907.44 MB | 5986.9 MB |
| GPU | 30.43% | 89.83% | 77.51% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8005.993 ms
- Total packets analyzed: 19910

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 19910 | 0.001 ms | 0.804 ms | 38.850 ms | 217.57% | 23.48% | 18.73% | 26.431 |
| Video | 17912 | 0.001 ms | 0.447 ms | 38.850 ms | 258.18% | 24.99% | 10.89% | 16.918 |
| Audio | 1998 | 0.001 ms | 4.005 ms | 30.193 ms | 66.85% | 15.22% | 5.06% | 3.578 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 17912 | 0.050 ms | 38.568 ms | 1 (0.0%) |
| Audio | 1998 | 0.637 ms | 26.195 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Acceptable synchronization (87.5%): avg offset 103.7ms, max 758.8ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9852.0ms, video=9845.2ms (frame 589), diff=6.8ms
- 🟢 Pop #2 [R]: audio=10652.0ms, video=10647.5ms (frame 637), diff=4.5ms
- 🟢 Pop #3 [L]: audio=11457.0ms, video=11449.9ms (frame 685), diff=7.1ms
- • Pop #4 [R]: audio=12279.0ms, video=13037.8ms (frame 780), diff=758.8ms
- 🟢 Pop #5 [L]: audio=13081.0ms, video=13054.5ms (frame 781), diff=26.5ms
- 🟢 Pop #6 [R]: audio=13886.0ms, video=13856.9ms (frame 829), diff=29.1ms
- 🟡 Pop #7 [L]: audio=14687.0ms, video=14642.5ms (frame 876), diff=44.5ms
- 🟢 Pop #8 [R]: audio=15488.0ms, video=15461.5ms (frame 925), diff=26.5ms
- 🟢 Pop #9 [L]: audio=16293.0ms, video=16263.8ms (frame 973), diff=29.2ms

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
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 589 at 00:09.8 of the 19.2 s video above.
