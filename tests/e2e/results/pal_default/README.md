# C64 Stream E2E Test Report

Generated: 2025-12-29 17:02:46 UTC

## Test configuration

- Format: PAL
- Frames: 400
- Duration: 8.0 seconds
- Video Port: 11000
- Audio Port: 11001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 30.0.2.1-3build1
- CPU: Intel(R) Core(TM) i5-14600K (20 cores)
- RAM: 62Gi total, 53Gi available
- Disk (/): 916G total, 496G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 26.9% | 33.6% | 33.21% | 35.3% |
| RAM | 6026.57 MB | 6055.65 MB | 6054.17 MB | 6081.87 MB |
| GPU | 81.74% | 89.23% | 86.88% | 89.23% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 27200 video, 1994 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 13.2ms, max 35.2ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=8009.0ms, video=8020.0ms (frame 402), diff=11.0ms
- 🟢 Pop #2 [R]: audio=8966.0ms, video=8977.6ms (frame 450), diff=11.6ms
- 🟡 Pop #3 [L]: audio=9900.0ms, video=9935.2ms (frame 498), diff=35.2ms
- 🟢 Pop #4 [R]: audio=10884.0ms, video=10892.8ms (frame 546), diff=8.8ms
- 🟢 Pop #5 [L]: audio=11841.0ms, video=11850.4ms (frame 594), diff=9.4ms
- 🟢 Pop #6 [R]: audio=12801.0ms, video=12808.0ms (frame 642), diff=7.0ms
- 🟢 Pop #7 [L]: audio=13756.0ms, video=13765.6ms (frame 690), diff=9.6ms

- Channels: LRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (401 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 9/2/2/5 | 9/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.581–18.514).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 12 | 15.002 | 0.296 | 1.057 | 14.444–15.501 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4)
- Duration: 19.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 403 at 00:08.1 of the 19.0 s video above.
