# C64 Stream E2E Test Results

This directory contains reference recordings and test results for E2E testing.

Important: Most subfolders here are generated locally and are intentionally not committed.
Only a small curated subset is tracked in git to avoid bloating the repository.

## Directory Structure

```
tests/e2e/results/
├── README.md
├── ntsc_default/        # committed (NTSC, Default preset)
├── ntsc_green_monitor/  # committed (NTSC, Green Monitor preset)
└── pal_default/         # committed (PAL, Default preset)
```

Any other folders under `tests/e2e/results/` are expected to be local-only outputs
from running scenarios (e.g. `ntsc_classic_crt/`, `scanlines/`, etc.).

## Results

- [NTSC Default](./ntsc_default/README.md)
- [NTSC Green Monitor](./ntsc_green_monitor/README.md)
- [PAL Default](./pal_default/README.md)

## Per-Preset Contents

Each scenario folder contains:

- `c64_recording.mp4` - OBS recording of the test run
- `c64_recording_still.png` - Sample frame showing A/V pop
- `README.md` - Test report with validation results
- `validation_results.json` - Machine-readable validation data
- `network.csv` - Network packet reception log (what arrived at the plugin)
- `obs.csv` - OBS event log (what was submitted into OBS)
- `playback.csv` - Playback timeline analysis (what was observed during playback)
- `config_used/` - Copy of properties.ini used for the test

### CSV Pipeline

The CSV files represent different stages of the streaming pipeline:

```
network.csv   → what arrived at the plugin (UDP packets)
obs.csv       → what was submitted into OBS (frames/audio events)
playback.csv  → what was observed during playback (decoded frames + anomalies)
```

#### obs.csv Columns

| Column | Description |
|--------|-------------|
| `event_type` | "video" or "audio" |
| `frame_num` | Frame counter from the stream |
| `elapsed_us` | Microseconds since recording started |
| `data_size_bytes` | Size of the frame/audio data |
| `fps` | Current measured FPS |
| `audio_samples_total` | Cumulative audio samples processed |
| `video_packets_received` | Cumulative video packets received |
| `audio_packets_received` | Cumulative audio packets received |
| `sequence_errors` | Cumulative sequence errors detected |

#### playback.csv Columns

`playback.csv` is the authoritative source for skipped/repeated frame analysis. Each row represents one displayed frame in the recording (1:1 mapping with `playback_frame_index`).

| Column | Description |
|--------|-------------|
| `playback_frame_index` | Absolute frame index in the recording (0-based) |
| `frame_num` | C64U stream frame number from obs.csv (empty for logo frames) |
| `video_time_s` | Timestamp in seconds since recording start |
| `repeated` | If start of repeated run: total times shown; empty otherwise |
| `skipped` | Frames permanently lost before this one; empty if none |
| `event` | Human-readable summary (see below) |

**Frame Number Mapping:**

The `frame_num` column uses detected video colors as ground truth:
1. Content bounds detection identifies first/last content frames
2. For each video frame, the top-left corner color is detected (0-15)
3. Colors are matched to obs.csv entries where `frame_num % 16` equals the color
4. This ensures playback.csv reflects actual displayed content, not assumptions

For scenarios with visual effects (CRT filters, phosphor glow), color detection may be
skipped and playback.csv falls back to frame sequencing without frame_num mapping.

**Event Values:**

| Event | Meaning |
|-------|---------|
| `repeated` | Start of a run where same content is displayed multiple times |
| `skipped` | Source frames were permanently lost before this frame |
| `repeated+skipped` | Both anomalies on same frame (rare) |
| _(empty)_ | Normal frame, no anomaly |

**Semantics:**

- **`repeated`**: Source didn't deliver a new frame in time, so the previous content was displayed again. The `repeated` column shows the count only on the FIRST frame of the run (e.g., `3` means shown 3 times total). Continuation frames have empty `repeated` column.

- **`skipped`**: Frames are permanently missing - the frame counter jumped. These frames will never appear. The count indicates how many source frames were lost before this frame arrived.

- Both can occur on the same frame (rare): frames were lost, then the arriving frame was repeated.

**Example:**

```csv
playback_frame_index,frame_num,video_time_s,repeated,skipped,event
523,523,8.717,,,
524,524,8.733,,,
525,525,8.75,3,,repeated
526,526,8.767,,,
527,527,8.783,,,
528,528,8.8,,1,skipped
529,529,8.817,,,
540,540,9.0,2,3,repeated+skipped
541,541,9.017,,,
```

Reading this example:
- Frames 523-524: Normal playback
- Frame 525: Content shown 3 times (at indices 525, 526, 527)
- Frames 526-527: Continuation of repeated run (no markers needed)
- Frame 528: 1 source frame was permanently lost before this arrived
- Frame 540: 3 frames lost, then this frame was shown twice (rare combined case)

## Running Tests

### Single Scenario

```bash
cd tests/e2e
./e2e.sh --scenario ntsc_default --verbose
```

### All Scenarios

```bash
./run_all_scenarios.sh
```

## CI Integration

In CI, tests run via the GitHub Actions workflow with matrix builds:
- Each scenario runs in parallel
- Results are uploaded as artifacts
- Failures are reported in the job summary
