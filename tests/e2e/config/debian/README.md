# Debian 12 E2E Test Configuration

## Differences from Ubuntu

Debian 12 (Bookworm) has the following differences that may affect E2E tests:

- **OBS Version**: May be older than Ubuntu PPA version
- **FFmpeg Version**: Uses Debian-packaged FFmpeg (may differ from Ubuntu)
- **Qt Version**: System Qt6 packages (version may vary)
- **Mesa Drivers**: Debian-stable Mesa version

## Configuration Overrides

None required currently - using defaults.

## Known Issues

None identified yet.

## Testing Notes

- First runs may show slight rendering differences due to library versions
- If tests fail, check OBS and FFmpeg versions first
- Consider adjusting image comparison thresholds if needed
