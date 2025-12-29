# C64 Stream E2E Test Report

Generated: 2025-12-29 16:28:36 UTC

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
- RAM: 62Gi total, 54Gi available
- Disk (/): 916G total, 497G available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (20 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 15.5% | 19.6% | 19.34% | 20.7% |
| RAM | 5452.61 MB | 5481.82 MB | 5478.99 MB | 5510.34 MB |
| GPU | 78.52% | 79.58% | 81.53% | 90.32% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 891.3ms, max 3201.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=7969.0ms, video=7966.7ms (frame 239), diff=2.3ms
- 🟢 Pop #2 [R]: audio=8769.0ms, video=8766.7ms (frame 263), diff=2.3ms
- 🟢 Pop #3 [L]: audio=9590.0ms, video=9600.0ms (frame 288), diff=10.0ms
- 🟢 Pop #4 [R]: audio=10396.0ms, video=10400.0ms (frame 312), diff=4.0ms
- 🟢 Pop #5 [L]: audio=11196.0ms, video=11200.0ms (frame 336), diff=4.0ms
- • Pop #6 [R]: audio=11996.0ms, video=11200.0ms (frame 336), diff=796.0ms
- • Pop #7 [L]: audio=12801.0ms, video=11200.0ms (frame 336), diff=1601.0ms
- • Pop #8 [R]: audio=13601.0ms, video=11200.0ms (frame 336), diff=2401.0ms
- • Pop #9 [L]: audio=14401.0ms, video=11200.0ms (frame 336), diff=3201.0ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=119, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 115/1/1/2 | 0 | 0 |
| After settling | 0/0/0/0 | 117/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 7.600–18.633).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 232 | 11.585 | 2.265 | 7.934 | 7.633–15.567 |

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
- Taken from frame 240 at 00:04.0 of the 19.2 s video above.
