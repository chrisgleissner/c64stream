# C64 Stream E2E Test Report

Generated: 2025-10-18 23:52:01 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 0.8.1

## Test results

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 8.3ms, max 10.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2390.0ms, video=2383.3ms (frame 143), diff=6.7ms
- 🟢 Pop #2 [R]: audio=3390.0ms, video=3383.3ms (frame 203), diff=6.7ms
- 🟢 Pop #3 [L]: audio=4390.0ms, video=4400.0ms (frame 264), diff=10.0ms
- 🟢 Pop #4 [R]: audio=5390.0ms, video=5400.0ms (frame 324), diff=10.0ms

- Channels: LRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 13.6 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left cycles through all C64 colours to check frame progression. Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks.
- Taken from frame 143 at 00:02.4 of the 13.6 s video above.
