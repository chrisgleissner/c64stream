# C64 Stream E2E Test Report

## Scenario: NTSC Vintage TV

Generated: 2026-01-01 09:29:45 UTC

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
- RAM: 31Gi total, 24Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (4.6s, 10 of 27 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 94.9% | 95.4% | 95.52% | 96.8% |
| RAM | 5420.14 MB | 5452.63 MB | 5489.17 MB | 5674.33 MB |
| GPU | 38.58% | 42.31% | 41.99% | 43.11% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 18000 video, 1252 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality

| Stream | Packets | Jitter (median) | Jitter (max) |
|--------|---------|-----------------|--------------|
| Video | 17999 | 0.001 ms | 57.726 ms |
| Audio | 1248 | 1.036 ms | 25.163 ms |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 172.4ms, max 778.9ms

#### Sync Details

- • Pop #1 [L]: audio=2865.0ms, video=3643.9ms (frame 218), diff=778.9ms
- 🟢 Pop #2 [R]: audio=3686.0ms, video=3660.6ms (frame 219), diff=25.4ms
- 🟢 Pop #3 [L]: audio=4486.0ms, video=4462.9ms (frame 267), diff=23.1ms
- 🟢 Pop #4 [R]: audio=5292.0ms, video=5265.3ms (frame 315), diff=26.7ms
- 🟢 Pop #5 [L]: audio=6092.0ms, video=6084.3ms (frame 364), diff=7.7ms

- Channels: LRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=102, back_steps=1) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 27/2/2/136 | 48/1/1/2 | 0 | 0 |
| After settling | 0/0/0/0 | 78/1/1/3 | 1 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.217–7.422).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 84 | 5.586 | 0.952 | 3.394 | 4.028–7.422 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 11.0 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 219 at 00:03.6 of the 11.0 s video above.
