# C64 Stream E2E Test Report

Generated: 2025-10-18 23:28:31 UTC

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

- ✅ Good synchronization (100.0%): avg offset 8.3ms, max 23.3ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7470.0ms, video=7466.7ms (frame 448), diff=3.3ms
- 🟢 Pop #2 [R]: audio=8470.0ms, video=8466.7ms (frame 508), diff=3.3ms
- 🟢 Pop #3 [L]: audio=9470.0ms, video=9466.7ms (frame 568), diff=3.3ms
- 🟢 Pop #4 [R]: audio=10490.0ms, video=10466.7ms (frame 628), diff=23.3ms

- Channels: LRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 18.7 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)
- Top-left cycles through all C64 colours to check frame progression. Center shows scrolling colour bars. Bottom-right flashes with a pop sound for A/V sync checks.
- Taken from frame 448 at 00:07.5 of the 18.7 s video above.
