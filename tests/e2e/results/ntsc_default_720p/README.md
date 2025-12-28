# C64 Stream E2E Test Report

Generated: 2025-12-28 20:06:01 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 41.2% | 48.05% | 48.07% | 53.9% |
| RAM | 6384.05 MB | 6393.01 MB | 6393.68 MB | 6410.34 MB |
| GPU | 26.94% | 39.12% | 35.04% | 40.9% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 118.3ms, max 729.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8158.0ms, video=8140.3ms (frame 487), diff=17.7ms
- 🟢 Pop #2 [R]: audio=8958.0ms, video=8942.6ms (frame 535), diff=15.4ms
- • Pop #3 [L]: audio=9801.0ms, video=10530.5ms (frame 630), diff=729.5ms
- 🟡 Pop #4 [R]: audio=10606.0ms, video=10547.3ms (frame 631), diff=58.7ms
- 🟡 Pop #5 [L]: audio=11406.0ms, video=11349.6ms (frame 679), diff=56.4ms
- 🟡 Pop #6 [R]: audio=12206.0ms, video=12151.9ms (frame 727), diff=54.1ms
- 🟡 Pop #7 [L]: audio=13012.0ms, video=12954.2ms (frame 775), diff=57.8ms
- 🟡 Pop #8 [R]: audio=13812.0ms, video=13773.3ms (frame 824), diff=38.7ms
- 🟡 Pop #9 [L]: audio=14612.0ms, video=14575.6ms (frame 872), diff=36.4ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 1/3/3/3 | 1/2/2/2 | 0 | 0 |
| After settling | 14/2/2/2 | 13/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.756–18.754).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 21 | 13.792 | 0.268 | 1.020 | 13.188–14.208 |
| 2 | 2 | 9.761 | 0.017 | 0.033 | 9.745–9.778 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 23.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 487 at 00:08.1 of the 23.0 s video above.
