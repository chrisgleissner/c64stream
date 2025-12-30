# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2025-12-30 13:58:34 UTC

## Test configuration

- Format: NTSC
- Frames: 300
- Duration: 5.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.0

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 95.5% | 96.3% | 96.21% | 97.2% |
| RAM | 7136.96 MB | 7176.88 MB | 7171.8 MB | 7189.86 MB |
| GPU | 32.76% | 40.67% | 40.23% | 44.94% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 18.2ms, max 29.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2806.0ms, video=2808.1ms (frame 168), diff=2.1ms
- 🟢 Pop #2 [R]: audio=3606.0ms, video=3577.0ms (frame 214), diff=29.0ms
- 🟢 Pop #3 [L]: audio=4406.0ms, video=4379.4ms (frame 262), diff=26.6ms
- 🟢 Pop #4 [R]: audio=5233.0ms, video=5215.1ms (frame 312), diff=17.9ms
- 🟢 Pop #5 [L]: audio=6033.0ms, video=6017.5ms (frame 360), diff=15.5ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=111, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 44/2/2/136 | 39/1/1/3 | 0 | 0 |
| After settling | 83/2/2/6 | 78/1/1/5 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.134–7.371).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 126 | 5.673 | 0.972 | 3.327 | 4.028–7.355 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 10.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 168 at 00:02.8 of the 10.9 s video above.
