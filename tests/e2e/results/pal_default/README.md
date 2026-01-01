# C64 Stream E2E Test Report

## Scenario: PAL Default

Generated: 2026-01-01 09:30:41 UTC

## Test configuration

- Format: PAL
- Frames: 250
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
| CPU | 45.8% | 63.35% | 62.08% | 83.6% |
| RAM | 5486.56 MB | 5567.84 MB | 5676.75 MB | 5926.38 MB |
| GPU | 25.7% | 32.7% | 33.99% | 40.32% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 17000 video, 1246 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality

| Stream | Packets | Jitter (median) | Jitter (max) |
|--------|---------|-----------------|--------------|
| Video | 16999 | 0.002 ms | 15.637 ms |
| Audio | 1244 | 0.530 ms | 9.558 ms |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 17.1ms, max 18.5ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=2929.0ms, video=2912.7ms (frame 146), diff=16.3ms
- 🟢 Pop #2 [R]: audio=3886.0ms, video=3870.3ms (frame 194), diff=15.7ms
- 🟢 Pop #3 [L]: audio=4846.0ms, video=4827.9ms (frame 242), diff=18.1ms
- 🟢 Pop #4 [R]: audio=5804.0ms, video=5785.5ms (frame 290), diff=18.5ms

- Channels: LRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (398 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 13/2/2/3 | 13/1/1/2 | 0 | 0 |
| After settling | 0/0/0/0 | 0/0/0/0 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 2.454–10.394).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 4 | 5.651 | 0.030 | 0.080 | 5.606–5.686 |
| 2 | 3 | 4.688 | 0.033 | 0.080 | 4.648–4.728 |
| 3 | 3 | 6.244 | 0.033 | 0.080 | 6.204–6.284 |

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
- Taken from frame 146 at 00:02.9 of the 10.9 s video above.
