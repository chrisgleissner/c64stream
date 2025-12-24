# C64 Stream E2E Test Report

Generated: 2025-12-23 23:12:12 UTC

## Test configuration

- Format: PAL
- Frames: 300
- Duration: 6.0 seconds
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
- RAM: 31Gi total, 25Gi available
- Disk (/): 1.8T total, 1.2T available

## Test results

- ✅ Packet Generation: 20400 video, 1495 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 10.2ms, max 20.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7556.0ms, video=7560.0ms (frame 378), diff=4.0ms
- 🟢 Pop #2 [R]: audio=8577.0ms, video=8560.0ms (frame 428), diff=17.0ms
- 🟢 Pop #3 [L]: audio=9580.0ms, video=9580.0ms (frame 479), diff=0.0ms
- 🟢 Pop #4 [R]: audio=10580.0ms, video=10560.0ms (frame 528), diff=20.0ms

- Channels: LRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 20.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left shows the frame index (color = frame_num % 16). Top-right shows a stable 4×4 tile of all 16 VIC colours (drift check). Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks / afterglow tail.
- Taken from the 20.5 s video above.
