# C64 Stream E2E Test Report

Generated: 2025-12-29 17:20:25 UTC

## Test configuration

- Format: NTSC
- Frames: 480
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
| CPU | 14.5% | 20.45% | 20.33% | 25.6% |
| RAM | 5541.05 MB | 5591.69 MB | 5587.04 MB | 5626.62 MB |
| GPU | 83.87% | 89.77% | 90.99% | 100.0% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 540.8ms, max 1600.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7981.0ms, video=7999.7ms (frame 240), diff=18.7ms
- 🟢 Pop #2 [R]: audio=8806.0ms, video=8799.7ms (frame 264), diff=6.3ms
- 🟢 Pop #3 [L]: audio=9609.0ms, video=9599.7ms (frame 288), diff=9.3ms
- 🟢 Pop #4 [R]: audio=10412.0ms, video=10399.7ms (frame 312), diff=12.3ms
- • Pop #5 [L]: audio=11233.0ms, video=10433.0ms (frame 313), diff=800.0ms
- • Pop #6 [R]: audio=12033.0ms, video=10433.0ms (frame 313), diff=1600.0ms
- • Pop #7 [L]: audio=12838.0ms, video=14433.7ms (frame 433), diff=1595.7ms
- • Pop #8 [R]: audio=13613.0ms, video=14433.7ms (frame 433), diff=820.7ms
- 🟢 Pop #9 [L]: audio=14438.0ms, video=14433.7ms (frame 433), diff=4.3ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=119, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 116/1/1/2 | 0 | 0 |
| After settling | 0/0/0/0 | 118/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.667–18.667).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 234 | 11.662 | 2.308 | 7.933 | 7.700–15.633 |

### Video

- Download: [c64_recording.mkv](c64_recording.mkv)
- Duration: 19.2 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 241 at 00:04.0 of the 19.2 s video above.
