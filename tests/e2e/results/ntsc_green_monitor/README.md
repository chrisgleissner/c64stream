# C64 Stream E2E Test Report

Generated: 2025-12-26 11:31:03 UTC

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
- OBS: - 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ❌ Poor synchronization (0.0%): avg offset 61.8ms, max 67.3ms

#### Sync Details

- 🔴 Pop #1 [L]: audio=8134.0ms, video=8066.7ms (frame 484), diff=67.3ms
- 🔴 Pop #2 [R]: audio=9134.0ms, video=9083.3ms (frame 545), diff=50.7ms
- 🔴 Pop #3 [L]: audio=10134.0ms, video=10066.7ms (frame 604), diff=67.3ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.6 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from the 19.6 s video above.
