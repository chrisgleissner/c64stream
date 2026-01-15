# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

- Generated: 2026-01-15 16:29:00 UTC
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

- ✅ UDP Packet Reception: Expected 30803, Received 30797, Missing 6 (0.02%)
- ✅ Network Timing: span=8021.5ms, video_mean=414.4us, audio_mean=4003.8us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 10.6 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 93.2% | 95% | 95% | 96.6% |
| RAM | 2757.48 MB | 2828.44 MB | 2818.45 MB | 2841.34 MB |
| GPU | 30.43% | 89.83% | 73.16% | 98.52% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8021.475 ms
- Total packets analyzed: 21358

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 21358 | 0.001 ms | 0.751 ms | 21.423 ms | 198.12% | 16.44% | 17.08% | 23.300 |
| Video | 19357 | 0.001 ms | 0.414 ms | 20.760 ms | 208.84% | 17.46% | 9.28% | 13.791 |
| Audio | 2001 | 0.001 ms | 4.004 ms | 21.423 ms | 54.08% | 12.59% | 3.80% | 3.166 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 19357 | 0.029 ms | 20.478 ms | 0 |
| Audio | 2001 | 0.643 ms | 17.424 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Acceptable synchronization (77.8%): avg offset 22.7ms, max 45.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9885.0ms, video=9878.6ms (frame 591), diff=6.4ms
- 🟢 Pop #2 [R]: audio=10684.0ms, video=10664.3ms (frame 638), diff=19.7ms
- 🟢 Pop #3 [L]: audio=11490.0ms, video=11483.3ms (frame 687), diff=6.7ms
- 🟢 Pop #4 [R]: audio=12290.0ms, video=12285.6ms (frame 735), diff=4.4ms
- 🟡 Pop #5 [L]: audio=13114.0ms, video=13071.2ms (frame 782), diff=42.8ms
- 🟡 Pop #6 [R]: audio=13919.0ms, video=13873.6ms (frame 830), diff=45.4ms
- 🟢 Pop #7 [L]: audio=14719.0ms, video=14692.6ms (frame 879), diff=26.4ms
- 🟢 Pop #8 [R]: audio=15519.0ms, video=15494.9ms (frame 927), diff=24.1ms
- 🟢 Pop #9 [L]: audio=16326.0ms, video=16297.3ms (frame 975), diff=28.7ms

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
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–17.467).
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
- Taken from frame 591 at 00:09.9 of the 21.3 s video above.
