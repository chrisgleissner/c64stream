# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2025-12-31 14:08:03 UTC

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
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
- RAM: 31Gi total, 22Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 94.5% | 95.2% | 95.27% | 96.2% |
| RAM | 4535.99 MB | 4639.04 MB | 4610.71 MB | 4654.05 MB |
| GPU | 0.0% | 0.47% | 0.48% | 0.96% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 282.1ms, max 847.1ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2745.0ms, video=2741.3ms (frame 164), diff=3.7ms
- • Pop #2 [R]: audio=3566.0ms, video=4329.2ms (frame 259), diff=763.2ms
- 🟢 Pop #3 [L]: audio=4366.0ms, video=4345.9ms (frame 260), diff=20.1ms
- • Pop #4 [R]: audio=5193.0ms, video=4345.9ms (frame 260), diff=847.1ms
- • Pop #5 [L]: audio=5993.0ms, video=6736.2ms (frame 403), diff=743.2ms
- 🟡 Pop #6 [R]: audio=6793.0ms, video=6752.9ms (frame 404), diff=40.1ms
- 🟡 Pop #7 [L]: audio=7598.0ms, video=7555.2ms (frame 452), diff=42.8ms
- 🟡 Pop #8 [R]: audio=8398.0ms, video=8357.6ms (frame 500), diff=40.4ms
- 🟡 Pop #9 [L]: audio=9198.0ms, video=9159.9ms (frame 548), diff=38.1ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=117, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 53/2/2/129 | 48/1/1/2 | 0 | 0 |
| After settling | 59/2/2/3 | 100/1/1/3 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–10.347).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 123 | 5.947 | 1.172 | 4.162 | 4.012–8.174 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 13.9 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 164 at 00:02.7 of the 13.9 s video above.
