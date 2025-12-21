# C64 Stream E2E Test Report

Generated: 2025-10-18 23:29:20 UTC

## Test configuration

- Format: PAL
- Frames: 250
- Duration: 5.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 0.8.1

## Test results

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 10.0ms, max 10.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7530.0ms, video=7540.0ms (frame 377), diff=10.0ms
- 🟢 Pop #2 [R]: audio=8530.0ms, video=8540.0ms (frame 427), diff=10.0ms
- 🟢 Pop #3 [L]: audio=9530.0ms, video=9520.0ms (frame 476), diff=10.0ms

- Channels: LRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.5 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left cycles through all C64 colours to check frame progression. Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks.
- Taken from frame 377 at 00:07.5 of the 19.5 s video above.
