# C64 Stream E2E Test Report

Generated: 2025-12-27 12:00:02 UTC

## Test configuration

- Format: NTSC
- Frames: 180
- Duration: 3.0 seconds
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

During the test's processing window (2.6s, 6 of 26 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 57.5% | 73.5% | 72.98% | 86.3% |
| RAM | 6487.66 MB | 6673.72 MB | 6773.14 MB | 7228.46 MB |
| GPU | 33.85% | 35.13% | 39.69% | 57.26% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 10800 video, 751 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 36.3ms, max 36.3ms

#### Sync Details

- 🟡 Pop #1 [L]: audio=8753.0ms, video=8716.7ms (frame 523), diff=36.3ms

- Channels: L
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (360 frames analyzed, 16 colors)

| Metric | Count | Min | Median | Max |
|--------|-------|-----|--------|-----|
| Repeated frames | 25 runs | 2 | 2 | 3 |
| Skipped frames | 23 skips | 1 | 1 | 3 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

- ↩️ Back steps: 0 (frame counter went backwards)

- ⚡ Severe jumps: 0 (large sequence discontinuity)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 16.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 523 at 00:08.7 of the 16.7 s video above.
