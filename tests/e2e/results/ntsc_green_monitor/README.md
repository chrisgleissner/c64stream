# C64 Stream E2E Test Report

Generated: 2025-12-24 08:45:56 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 0.8.3

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: - 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 27Gi available
- Disk (/): 1.8T total, 1.2T available

## Test results

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 15.1ms, max 28.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7604.0ms, video=7616.7ms (frame 457), diff=12.7ms
- 🟢 Pop #2 [R]: audio=8625.0ms, video=8616.7ms (frame 517), diff=8.3ms
- 🟢 Pop #3 [L]: audio=9628.0ms, video=9616.7ms (frame 577), diff=11.3ms
- 🟢 Pop #4 [R]: audio=10622.0ms, video=10650.0ms (frame 639), diff=28.0ms

- Channels: LRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from the 19.1 s video above.
