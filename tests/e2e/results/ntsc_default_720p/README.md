# C64 Stream E2E Test Report

## Scenario: NTSC Default 720p

Generated: 2025-12-31 00:28:31 UTC

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
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (7.6s, 16 of 33 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 40.5% | 44.45% | 44.79% | 48.7% |
| RAM | 4044.94 MB | 4057.88 MB | 4057.82 MB | 4064.84 MB |
| GPU | 25.88% | 30.23% | 34.39% | 44.89% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 15.9ms, max 20.0ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2774.0ms, video=2774.7ms (frame 166), diff=0.7ms
- 🟢 Pop #2 [R]: audio=3597.0ms, video=3577.0ms (frame 214), diff=20.0ms
- 🟢 Pop #3 [L]: audio=4398.0ms, video=4379.4ms (frame 262), diff=18.6ms
- 🟢 Pop #4 [R]: audio=5201.0ms, video=5181.7ms (frame 310), diff=19.3ms
- 🟢 Pop #5 [L]: audio=6003.0ms, video=5984.0ms (frame 358), diff=19.0ms
- 🟢 Pop #6 [R]: audio=6804.0ms, video=6786.3ms (frame 406), diff=17.7ms
- 🟢 Pop #7 [L]: audio=7606.0ms, video=7588.7ms (frame 454), diff=17.3ms
- 🟢 Pop #8 [R]: audio=8406.0ms, video=8391.0ms (frame 502), diff=15.0ms
- 🟢 Pop #9 [L]: audio=9209.0ms, video=9193.3ms (frame 550), diff=15.7ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |
| After settling | 2/2/2/2 | 1/1/1/1 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.390–13.389).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 2 | 8.926 | 0.017 | 0.034 | 8.909–8.943 |
| 2 | 1 | 10.380 | 0.000 | 0.000 | 10.380–10.380 |

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
- Taken from frame 166 at 00:02.8 of the 13.9 s video above.
