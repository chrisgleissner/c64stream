# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2025-12-30 00:12:47 UTC

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
- OBS: 30.0.2.1-3build1
- CPU: Intel(R) Core(TM) i5-14600K (20 cores)
- RAM: 62Gi total, 53Gi available
- Disk (/): 916G total, 495G available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 16.8% | 21.25% | 22.21% | 30.4% |
| RAM | 5972.09 MB | 6262.47 MB | 6236.59 MB | 6398.49 MB |
| GPU | 100.0% | 100.0% | 100.0% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 6.4ms, max 13.4ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7969.0ms, video=7973.1ms (frame 477), diff=4.1ms
- 🟢 Pop #2 [R]: audio=8772.0ms, video=8775.4ms (frame 525), diff=3.4ms
- 🟢 Pop #3 [L]: audio=9572.0ms, video=9577.8ms (frame 573), diff=5.8ms
- 🟢 Pop #4 [R]: audio=10350.0ms, video=10363.4ms (frame 620), diff=13.4ms
- 🟢 Pop #5 [L]: audio=11177.0ms, video=11182.4ms (frame 669), diff=5.4ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Video stream froze for 181 frames (3.0s) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 1/181/181/181 | 1/5/5/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.589–15.579).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 1 | 12.570 | 0.000 | 0.000 | 12.570–12.570 |
| 2 | 1 | 15.579 | 0.000 | 0.000 | 15.579–15.579 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Videos are not checked into Git due to their size; available from local runs or CI build artifacts.)
- Duration: 16.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 477 at 00:08.0 of the 16.1 s video above.
