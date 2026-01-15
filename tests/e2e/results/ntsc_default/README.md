# C64 Stream E2E Test Report

## Scenario: NTSC Default

- Generated: 2026-01-15 11:21:38 UTC
- Git Branch: feature/rest-control
- Git ID: 53cad8a
- Environment: local

## Test configuration

- Format: NTSC
- Frames: 480
- Duration: 8.0 seconds
- Video Port: 21000
- Audio Port: 21001
- OBS Enabled: true

## Build information

- Project: c64stream
- Version: 1.0.3

## System information

- OS: Ubuntu 24.04.3 LTS (kernel 6.14.0-37-generic)
- OBS: 32.0.2
- CPU: Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz (8 cores)
- RAM: 31Gi total, 23Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Validation Summary

- ✅ UDP Packet Reception: Expected 30803, Received 30777, Missing 26 (0.08%)
- ✅ Network Timing: span=8015.7ms, video_mean=365.5us, audio_mean=4005.0us
- ✅ Frame Processing: 478 frames processed
- ✅ Video Recording: 9.5 MB
- ✅ Content Integrity: Verified

### Resource Usage

During the test's processing window (18.2s, 37 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 52% | 56.9% | 62.21% | 97.5% |
| RAM | 7086.21 MB | 7125.2 MB | 7203.17 MB | 7606.64 MB |
| GPU | 44.96% | 89.83% | 87.18% | 100% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 28800 video, 2003 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

#### Network Quality (Measured)

- Packet span (first→last): 8015.705 ms
- Total packets analyzed: 23933

| Stream | Packets | Spacing (min) | Spacing (mean) | Spacing (max) | CV | Burst <0.5×P50 | Gaps >2×P50 | P99/P50 |
|--------|---------|---------------|----------------|---------------|----|--------------|------------|--------|
| All | 23933 | 0.001 ms | 0.670 ms | 55.140 ms | 189.99% | 13.90% | 16.72% | 17.996 |
| Video | 21932 | 0.001 ms | 0.365 ms | 27.697 ms | 165.95% | 14.91% | 9.41% | 9.293 |
| Audio | 2001 | 0.001 ms | 4.005 ms | 55.140 ms | 44.64% | 4.55% | 1.00% | 1.979 |

| Stream | Packets | Jitter (median) | Jitter (max) | Out-of-Order |
|--------|---------|-----------------|--------------|--------------|
| Video | 21932 | 0.029 ms | 27.417 ms | 0 |
| Audio | 2001 | 0.216 ms | 51.140 ms | 0 |

Details: [network.json](network.json)

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 110.2ms, max 848.9ms

#### Sync Details

- 🟢 Pop #1 [L]: audio=9834.0ms, video=9811.8ms (frame 587), diff=22.2ms
- 🟢 Pop #2 [R]: audio=10635.0ms, video=10614.1ms (frame 635), diff=20.9ms
- 🟢 Pop #3 [L]: audio=11439.0ms, video=11433.2ms (frame 684), diff=5.8ms
- 🟢 Pop #4 [R]: audio=12240.0ms, video=12235.5ms (frame 732), diff=4.5ms
- 🟢 Pop #5 [L]: audio=13044.0ms, video=13021.1ms (frame 779), diff=22.9ms
- 🟢 Pop #6 [R]: audio=13847.0ms, video=13823.4ms (frame 827), diff=23.6ms
- 🟢 Pop #7 [L]: audio=14647.0ms, video=14625.7ms (frame 875), diff=21.3ms
- 🟢 Pop #8 [R]: audio=15450.0ms, video=15428.1ms (frame 923), diff=21.9ms
- • Pop #9 [L]: audio=16277.0ms, video=15428.1ms (frame 923), diff=848.9ms

- Channels: LRLRLRLRL
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟡 Frame sequence OK with jitter (skips=65, back_steps=0) (post-settling)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 53/2/2/2 | 53/1/1/1 | 0 | 0 |
| After settling | 0/0/0/0 | 57/1/1/3 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 9.427–17.417).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 140 | 13.096 | 2.362 | 7.839 | 9.528–17.367 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 19.3 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 587 at 00:09.8 of the 19.3 s video above.
