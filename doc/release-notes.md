# Release Notes

## Unreleased

### Experimental: follow the device palette

- **Palette → Follow device** and `PALETTE "device"` use the palette the Ultimate is currently
  rendering with, including runtime changes. This requires Ultimate 64 firmware 3.16, which has
  not been released yet; it is not yet known when the C64 Ultimate firmware will support it. With
  other firmware the default palette stays in use.

### Device discovery and transport fixes

- Rediscovery updates device addresses without replacing saved names, DNS settings,
  or video/audio/control ports.
- First discovery carries migrated network settings forward when it identifies
  the configured host by its hardware ID, preserving custom UDP ports.
- Force REST reports failure when its client is unavailable, and legacy stream
  control reports connection and send failures accurately. Receive-only sources
  configured with `0.0.0.0` continue to accept incoming streams.
- Device teardown attempts both video and audio stops even if video teardown fails.
- Switching devices clears cached keyboard transport capabilities so the new
  device gets its own REST negotiation.
- Failed demo-script copies always close their destination files before cleanup.

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
