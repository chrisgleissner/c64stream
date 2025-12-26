# C64 Stream E2E Test Report

Generated: 2025-12-26 18:17:17 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 30 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 46.8% | 49.05% | 49.18% | 52.0% |
| RAM | 4631.51 MB | 4649.5 MB | 4645.25 MB | 4655.26 MB |
| GPU | 0.0% | 1.6% | 4.18% | 16.74% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 9.9ms, max 21.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8721.0ms, video=8700.0ms (frame 522), diff=21.0ms
- 🟢 Pop #2 [R]: audio=9721.0ms, video=9716.7ms (frame 583), diff=4.3ms
- 🟢 Pop #3 [L]: audio=10721.0ms, video=10716.7ms (frame 643), diff=4.3ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 522 at 00:08.7 of the 20.9 s video above.
