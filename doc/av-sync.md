# A/V Sync Pops (C64 Ultimate + OBS)

## Purpose and overview

These A/V pop programs create deterministic, low-overhead markers that make A/V sync errors and long-term drift
observable in logs and CSVs. A video pop is a full-frame white flash; an audio pop is a short, sharp low C3 pulse.
The OBS plugin can detect these edges (Debug-only), log timing deltas, and annotate CSV outputs for offline analysis.

Use these markers to:
- Compare Windows vs Linux behavior with the same test pattern
- Let end users verify their own setup
- Join `network.csv`, `obs.csv`, and OBS logs without manual timeline inspection

## C64 programs

### Manual mode: `av-sync.asm`

Behavior:
- Start: border black, background black, SID silent
- Hold SPACE: border white, background white, audio on
- Release SPACE: border black, background black, audio off

Audio:
- Pulse waveform, low C3 tone (~131 Hz), single SID voice
- ADSR: attack 0, decay 0, sustain max, release 0

### Automatic mode: `av-sync-auto.asm`

Behavior:
- Start: border black, background black, SID silent
- Every 48 frames: generate a one-frame A/V pop

Timing:
- Detect PAL vs NTSC
- `START_LINE = MAX_RASTER_LINE - 2`
- A raster IRQ at `START_LINE` starts the pop
- Busy-wait until the next frame begins and raster line 2 is reached, then stop the pop

Audio:
- Same C3 pulse tone as manual mode
- Exactly one frame long

### Build with the repo toolchain

```bash
cd tools/c64
./c64-build.sh av-sync.asm
./c64-build.sh av-sync-auto.asm
```

The script outputs `.prg` files next to the sources (or in a custom output folder if specified).

### Install on a C64 Ultimate (FTP or USB)

1. Copy the `.prg` files to the Ultimate via FTP or a USB drive.
2. Use the Ultimate's file browser to select the PRG.
3. Run it from the file browser (or from BASIC with `SYS` if preferred).

### Why a low C3 tone?

A low C3 (~131 Hz) pulse is:
- Easy to hear and identify
- Comfortable over headphones or speakers
- Low enough to avoid clipping or harshness

## OBS configuration

1. Add or select the C64 Stream source in OBS.
2. Open the source properties.
3. Enable the **Debug** checkbox (labeled "Show Debug Messages").

Important:
- Pop detection and CSV extensions exist **only** when Debug is enabled.
- When Debug is disabled, all detection and annotation logic is bypassed.

## Outputs and interpretation

### OBS log examples (Debug enabled)

```text
[c64stream] VIDEO: A/V pop video #3: frame=528 ts=123456789000 ns, audio_delta_ms=2.4
[c64stream] AUDIO: A/V pop audio #3: ts=123456791400 ns, audio_delta_ms=2.4
```

`audio_delta_ms` is `audio_time - video_time`:
- Positive values mean audio lags video
- Negative values mean audio leads video

### `network.csv` (packet-level, Debug enabled)

Two extra columns are appended:
- `is_all_white` (video packets only): 1 if the UDP payload is all `0x11`
- `has_signal` (audio packets only): 1 if any sample in the packet is non-silent

Example header:
```text
packet_type,elapsed_us,sequence_num,frame_num,line_num,last_packet,packet_size,data_payload,jitter_us,packet_interval_us,total_video_packets,total_audio_packets,sequence_errors,is_all_white,has_signal
```

### `obs.csv` (OBS submissions, Debug enabled)

Two extra columns are appended:
- `is_all_white` (video rows): 1 if the submitted frame is all white
- `has_signal` (audio rows): 1 if any sample in the buffer is non-silent

Example header:
```text
event_type,frame_num,elapsed_us,data_size_bytes,fps,audio_samples_total,video_packets_received,audio_packets_received,sequence_errors,is_all_white,has_signal
```

### Correlating logs and CSVs

1. Use the existing counters (`frame_num`, `sequence_num`, `video_packets_received`, `audio_packets_received`) as
   join keys across logs and CSVs.
2. Pair the first edge for video and audio pops, then compute:
   `delta_ms = audio_time_ms - video_time_ms`.
3. **Fixed offset** looks like a stable delta across pops.
4. **Drift** appears as a delta that grows or shrinks over time.

For automatic runs, pops occur every 48 frames, so a long capture makes drift easy to spot.
