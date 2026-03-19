# Release Notes

## Unreleased

### Preserve Preview Size

- Added **Preserve preview size** to both the C64 Stream input source and the C64 Stream Effects filter.
- New instances default to stable OBS-facing bounds while CRT effects keep using internal virtual scaling.
- Existing saved scenes remain on the legacy size-changing behavior unless `preserve_size` is explicitly enabled, so existing layouts are not silently changed.
- `EFFECTPARAM "preserve_size" 1` and `EFFECTPARAM "preserve_size" 0` can toggle the behavior at runtime.
- Presets no longer override `preserve_size`.
