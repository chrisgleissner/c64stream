# C64 Stream E2E Test Report

## Scenario: Unknown

Generated: 2026-01-08 15:21:04 UTC

## Test configuration

- Format: PAL
- Frames: 1500
- Duration: 30.0 seconds
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

- ⚠️ UDP Packet Reception: 247509/109478 packets (226543 video, 20122 audio, minor loss)
- ⚠️ Network Timing: span=51943.1ms, video_mean=228.5us, audio_mean=2538.9us
- ✅ Frame Processing: 23749 frames processed
- ✅ Video Recording: 22.6 MB
- ✅ Content Integrity: 45.5s duration

### Resource Usage

During the test's processing window (46.2s, 93 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 41.5% | 53.8% | 55.85% | 77.2% |
| RAM | 7366.05 MB | 7877.96 MB | 7864.67 MB | 8112.23 MB |
| GPU | 42.73% | 46.83% | 49.62% | 60.06% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 102000 video, 7478 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 51943.110 ms
- Total packets analyzed: 246730

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 246730 | 0.001 ms | 0.417 ms | 21.823 ms | 225.09% | 8.18% | 31.52% | 718.000 |
| Video | 226541 | 0.001 ms | 0.228 ms | 21.823 ms | 239.88% | 8.90% | 29.44% | 541.200 |
| Audio | 20120 | 0.003 ms | 2.539 ms | 20.854 ms | 62.53% | 35.23% | 0.59% | 1.847 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 226541 | 0.002 ms | 21.818 ms | 33943 (15.0%) |
| Audio | 20120 | 1.105 ms | 17.672 ms | 6695 (33.3%) |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 6260.2ms, max 18507.9ms

#### Sync Details

- • Pop #1 [R]: audio=231.0ms, video=12548.6ms (frame 629), diff=12317.6ms
- • Pop #2 [L]: audio=1153.0ms, video=12548.6ms (frame 629), diff=11395.6ms
- • Pop #3 [L]: audio=1443.0ms, video=12548.6ms (frame 629), diff=11105.6ms
- • Pop #4 [L]: audio=1984.0ms, video=12548.6ms (frame 629), diff=10564.6ms
- • Pop #5 [R]: audio=2645.0ms, video=12548.6ms (frame 629), diff=9903.6ms
- • Pop #6 [R]: audio=3659.0ms, video=12548.6ms (frame 629), diff=8889.6ms
- • Pop #7 [L]: audio=4202.0ms, video=12548.6ms (frame 629), diff=8346.6ms
- • Pop #8 [R]: audio=5203.0ms, video=12548.6ms (frame 629), diff=7345.6ms
- • Pop #9 [R]: audio=5575.0ms, video=12548.6ms (frame 629), diff=6973.6ms
- • Pop #10 [R]: audio=5903.0ms, video=12548.6ms (frame 629), diff=6645.6ms
- • Pop #11 [L]: audio=6507.0ms, video=12548.6ms (frame 629), diff=6041.6ms
- • Pop #12 [R]: audio=6744.0ms, video=12548.6ms (frame 629), diff=5804.6ms
- • Pop #13 [R]: audio=7815.0ms, video=12548.6ms (frame 629), diff=4733.6ms
- • Pop #14 [R]: audio=8304.0ms, video=12548.6ms (frame 629), diff=4244.6ms
- • Pop #15 [R]: audio=8727.0ms, video=12548.6ms (frame 629), diff=3821.6ms
- • Pop #16 [R]: audio=9595.0ms, video=12548.6ms (frame 629), diff=2953.6ms
- • Pop #17 [L]: audio=10699.0ms, video=12548.6ms (frame 629), diff=1849.6ms
- • Pop #18 [R]: audio=11500.0ms, video=12548.6ms (frame 629), diff=1048.6ms
- • Pop #19 [L]: audio=11920.0ms, video=12548.6ms (frame 629), diff=628.6ms
- 🟢 Pop #20 [L]: audio=12580.0ms, video=12568.6ms (frame 630), diff=11.4ms
- • Pop #21 [R]: audio=12896.0ms, video=12568.6ms (frame 630), diff=327.4ms
- • Pop #22 [R]: audio=13909.0ms, video=13526.2ms (frame 678), diff=382.8ms
- • Pop #23 [L]: audio=16884.0ms, video=17336.7ms (frame 869), diff=452.7ms
- • Pop #24 [R]: audio=17804.0ms, video=17356.6ms (frame 870), diff=447.4ms
- • Pop #25 [R]: audio=18843.0ms, video=19251.9ms (frame 965), diff=408.9ms
- • Pop #26 [L]: audio=19891.0ms, video=20209.5ms (frame 1013), diff=318.5ms
- 🟡 Pop #27 [R]: audio=20277.0ms, video=20229.4ms (frame 1014), diff=47.6ms
- • Pop #28 [R]: audio=20594.0ms, video=20229.4ms (frame 1014), diff=364.6ms
- 🟡 Pop #29 [L]: audio=21227.0ms, video=21187.0ms (frame 1062), diff=40.0ms
- • Pop #30 [R]: audio=21719.0ms, video=22124.7ms (frame 1109), diff=405.7ms
- • Pop #31 [L]: audio=22023.0ms, video=22124.7ms (frame 1109), diff=101.7ms
- • Pop #32 [L]: audio=22665.0ms, video=23082.3ms (frame 1157), diff=417.3ms
- • Pop #33 [R]: audio=23577.0ms, video=24039.9ms (frame 1205), diff=462.9ms
- • Pop #34 [L]: audio=26583.0ms, video=25975.1ms (frame 1302), diff=607.9ms
- • Pop #35 [R]: audio=26794.0ms, video=25975.1ms (frame 1302), diff=818.9ms
- • Pop #36 [R]: audio=27819.0ms, video=25975.1ms (frame 1302), diff=1843.9ms
- • Pop #37 [L]: audio=28772.0ms, video=25975.1ms (frame 1302), diff=2796.9ms
- • Pop #38 [R]: audio=29526.0ms, video=25975.1ms (frame 1302), diff=3550.9ms
- • Pop #39 [L]: audio=30628.0ms, video=25975.1ms (frame 1302), diff=4652.9ms
- • Pop #40 [R]: audio=31235.0ms, video=25975.1ms (frame 1302), diff=5259.9ms
- • Pop #41 [R]: audio=31574.0ms, video=25975.1ms (frame 1302), diff=5598.9ms
- • Pop #42 [R]: audio=32060.0ms, video=25975.1ms (frame 1302), diff=6084.9ms
- • Pop #43 [R]: audio=33143.0ms, video=25975.1ms (frame 1302), diff=7167.9ms
- • Pop #44 [L]: audio=34149.0ms, video=25975.1ms (frame 1302), diff=8173.9ms
- • Pop #45 [R]: audio=34699.0ms, video=25975.1ms (frame 1302), diff=8723.9ms
- • Pop #46 [R]: audio=35258.0ms, video=25975.1ms (frame 1302), diff=9282.9ms
- • Pop #47 [L]: audio=36521.0ms, video=25975.1ms (frame 1302), diff=10545.9ms
- • Pop #48 [R]: audio=36959.0ms, video=25975.1ms (frame 1302), diff=10983.9ms
- • Pop #49 [R]: audio=38869.0ms, video=25975.1ms (frame 1302), diff=12893.9ms
- • Pop #50 [L]: audio=39072.0ms, video=25975.1ms (frame 1302), diff=13096.9ms
- • Pop #51 [L]: audio=39938.0ms, video=25975.1ms (frame 1302), diff=13962.9ms
- • Pop #52 [L]: audio=40636.0ms, video=25975.1ms (frame 1302), diff=14660.9ms
- • Pop #53 [R]: audio=41759.0ms, video=25975.1ms (frame 1302), diff=15783.9ms
- • Pop #54 [R]: audio=43089.0ms, video=25975.1ms (frame 1302), diff=17113.9ms
- • Pop #55 [R]: audio=43752.0ms, video=25975.1ms (frame 1302), diff=17776.9ms
- • Pop #56 [L]: audio=44114.0ms, video=25975.1ms (frame 1302), diff=18138.9ms
- • Pop #57 [L]: audio=44483.0ms, video=25975.1ms (frame 1302), diff=18507.9ms

- Channels: RLLLRRLRRRLRRRRRLRLLRRLRRLRRLRLLRLRRLRLRRRRLRRLRRLLLRRRLL
- 🔁 Channel alternation: MISMATCH

### Frame Progression

- 🟢 Frame sequence verified (400 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 23/2/2/3 | 29/1/1/5 | 1 | 0 |
| After settling | 30/2/2/3 | 29/1/1/5 | 2 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 12.090–26.733).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 79 | 16.382 | 2.298 | 7.641 | 12.389–20.030 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 45.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 630 at 00:12.6 of the 45.5 s video above.
