# C64 Stream E2E Test Report

## Scenario: NTSC Green Monitor

Generated: 2025-12-31 00:10:19 UTC

## Test configuration

- Format: NTSC
- Frames: 1200
- Duration: 20.0 seconds
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
- RAM: 31Gi total, 21Gi available
- Disk (/): 1.8T total, 1.1T available

## Test results

### Resource Usage

During the test's processing window (19.7s, 40 of 57 samples) (8 cores):

| Metric | Min | Median | Mean | Max |
|--------|-----|--------|------|-----|
| CPU | 90.8% | 92.2% | 92.56% | 96.2% |
| RAM | 5716.18 MB | 5755.87 MB | 5873.45 MB | 6177.85 MB |
| GPU | 26.26% | 33.33% | 35.32% | 43.71% |

Details: [resource.csv](resource.csv) | [resource.json](resource.json)

### Packet & Network Data

- ✅ Packet Generation: 72000 video, 5008 audio packets
- ✅ UDP Replay: Completed successfully
- Events: [network.csv](network.csv), [obs.csv](obs.csv), [playback.csv](playback.csv)

### Perf Hotspots

- Artifacts: [perf_report.txt](perf_report.txt)

Top functions/symbols by sampled CPU (perf report):

| Overhead | Shared Object | Symbol |
|----------|---------------|--------|
| 6.92% | libx264.so.164 | [.] x264_8_trellis_coefn |
| 1.25% | libx264.so.164 | [.] x264_8_trellis_coefn |
| 0.74% | libgallium-25.0.7-0ubuntu0.24.04.2.so | [.] 0x00000000007595fa |
| 0.74% | libgallium-25.0.7-0ubuntu0.24.04.2.so | [.] 0x00000000007595fa |
| 0.62% | libgallium-25.0.7-0ubuntu0.24.04.2.so | [.] 0x00000000007595fa |
| 0.61% | libc.so.6 | [.] __memmove_avx_unaligned_erms |
| 0.58% | libgallium-25.0.7-0ubuntu0.24.04.2.so | [.] 0x00000000007595fa |
| 0.53% | libgallium-25.0.7-0ubuntu0.24.04.2.so | [.] 0x00000000007595fa |
| 0.52% | libgallium-25.0.7-0ubuntu0.24.04.2.so | [.] 0x00000000007595fa |
| 0.52% | libgallium-25.0.7-0ubuntu0.24.04.2.so | [.] 0x00000000007595fa |

### A/V Sync

- ✅ Good synchronization (100.0%): avg offset 247.0ms, max 839.0ms

#### Sync Details

- • Pop #1 [L]: audio=2953.0ms, video=3710.8ms (frame 222), diff=757.8ms
- 🟢 Pop #2 [R]: audio=3753.0ms, video=3727.5ms (frame 223), diff=25.5ms
- • Pop #3 [L]: audio=4553.0ms, video=5315.4ms (frame 318), diff=762.4ms
- 🟢 Pop #4 [R]: audio=5358.0ms, video=5332.1ms (frame 319), diff=25.9ms
- • Pop #5 [L]: audio=6158.0ms, video=6920.1ms (frame 414), diff=762.1ms
- 🟢 Pop #6 [R]: audio=6958.0ms, video=6936.8ms (frame 415), diff=21.2ms
- • Pop #7 [L]: audio=7764.0ms, video=8524.7ms (frame 510), diff=760.7ms
- 🟢 Pop #8 [R]: audio=8564.0ms, video=8541.4ms (frame 511), diff=22.6ms
- 🟢 Pop #9 [L]: audio=9364.0ms, video=9360.5ms (frame 560), diff=3.5ms
- 🟢 Pop #10 [R]: audio=10190.0ms, video=10162.8ms (frame 608), diff=27.2ms
- 🟡 Pop #11 [L]: audio=10990.0ms, video=10948.4ms (frame 655), diff=41.6ms
- 🟡 Pop #12 [R]: audio=11790.0ms, video=11750.7ms (frame 703), diff=39.3ms
- 🟡 Pop #13 [L]: audio=12596.0ms, video=12553.1ms (frame 751), diff=42.9ms
- • Pop #14 [R]: audio=13396.0ms, video=14157.7ms (frame 847), diff=761.7ms
- 🟢 Pop #15 [L]: audio=14196.0ms, video=14174.4ms (frame 848), diff=21.6ms
- 🟡 Pop #16 [R]: audio=15001.0ms, video=14960.1ms (frame 895), diff=40.9ms
- 🟡 Pop #17 [L]: audio=15801.0ms, video=15762.4ms (frame 943), diff=38.6ms
- 🟡 Pop #18 [R]: audio=16601.0ms, video=16564.7ms (frame 991), diff=36.3ms
- 🟡 Pop #19 [L]: audio=17406.0ms, video=17367.0ms (frame 1039), diff=39.0ms
- • Pop #20 [R]: audio=18206.0ms, video=17367.0ms (frame 1039), diff=839.0ms
- • Pop #21 [L]: audio=19006.0ms, video=19774.0ms (frame 1183), diff=768.0ms
- 🟢 Pop #22 [R]: audio=19812.0ms, video=19790.7ms (frame 1184), diff=21.3ms
- 🟡 Pop #23 [L]: audio=20612.0ms, video=20576.3ms (frame 1231), diff=35.7ms
- 🟢 Pop #24 [R]: audio=21412.0ms, video=21378.7ms (frame 1279), diff=33.3ms

- Channels: LRLRLRLRLRLRLRLRLRLRLRLR
- 🔁 Channel alternation: OK (alternating, starts with L)

### Frame Progression

- 🟢 Frame sequence verified (479 frames analyzed, 0 colors)

- Settling: 4.0s (pass/fail uses post-settling only)

| Window | Stuck runs (count/min/med/max) | Skips (count/min/med/max) | Back steps | Severe steps |
|--------|------------------------------:|--------------------------:|-----------:|-------------:|
| During settling | 24/2/2/133 | 22/1/1/1 | 1 | 0 |
| After settling | 39/2/2/3 | 38/1/1/2 | 0 | 0 |

See [playback.csv](playback.csv) for frame-by-frame playback timeline with anomaly markers.

#### Playback Jitter Clusters (post-settling)

- Definition: rows with repeated=1 or skipped=1 in playback.csv; clustering uses max gap 0.5s
- Note: this is independent from the Frame Progression (frame-box) check above
- Note: repeated/skipped markers only exist while content is detected (video_s 0.201–22.549).
  The jitter-free tail after content ends is expected and does not indicate steady-state performance.

| # | Events | Center (s) | Std dev (s) | Span (s) | Window (s) |
|---|--------|------------|-------------|----------|------------|
| 1 | 81 | 6.006 | 1.178 | 4.129 | 4.045–8.174 |

### Video

- Download: [c64_recording.mp4](c64_recording.mp4) (Available from local runs or CI build artifacts.)
- Duration: 26.1 s


### Sample Frame

![Sample Frame](./c64_recording_still.png)

- **Top-left**: Text box with scenario name
- **Top-right**: VIC-II palette reference grid of all C64 colors
- **Center**: Diagonal pattern cycling through all C64 colors
- **Bottom-left**: Frame progression indicator (8-slot moving bar, cycles every 8 frames)
- **Bottom-right**: A/V pop indicator (pops every 48 frames, split left/right for audio channels)
- Taken from frame 223 at 00:03.7 of the 26.1 s video above.
