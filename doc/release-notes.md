# Release Notes

## Unreleased

### Audio transport resilience (Discussion #114)

- Conceal isolated missing audio packets in both OBS and plugin WAV recordings,
  while retaining the sequence-derived A/V timeline.
- Drop late and duplicate audio packets instead of replaying stale PCM.
- Emit a rate-limited `Network errors (last 60s): …` warning when transport
  errors occur; routine network and A/V health diagnostics are debug-only.
- Drain audio independently of video and write recording WAV/AVI data through
  a bounded background writer so disk I/O cannot block packet processing.

### Preserve Preview Size

- Added **Preserve preview size** to both the C64 Stream input source and the C64 Stream Effects filter.
- New instances default to stable OBS-facing bounds while CRT effects keep using internal virtual scaling.
- Existing saved scenes remain on the legacy size-changing behavior unless `preserve_size` is explicitly enabled, so existing layouts are not silently changed.
- `EFFECTPARAM "preserve_size" 1` and `EFFECTPARAM "preserve_size" 0` can toggle the behavior at runtime.
- Presets no longer override `preserve_size`.
