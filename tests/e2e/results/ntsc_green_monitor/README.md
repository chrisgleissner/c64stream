# C64 Stream E2E Test Report

Generated: 2025-12-27 12:46:10 UTC

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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 30 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 91.4% | 93.8% | 94.52% | 100.0% |
| RAM | 7289.92 MB | 7359.11 MB | 7355.12 MB | 7458.79 MB |
| GPU | 44.81% | 47.1% | 47.42% | 50.79% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 23.4ms, max 29.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8729.0ms, video=8716.7ms (frame 523), diff=12.3ms
- 🟢 Pop #2 [R]: audio=9729.0ms, video=9700.0ms (frame 582), diff=29.0ms
- 🟢 Pop #3 [L]: audio=10729.0ms, video=10700.0ms (frame 642), diff=29.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from frame 523 at 00:08.7 of the 20.9 s video above.
