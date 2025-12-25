# Fedora 40 E2E Test Configuration

## Differences from Ubuntu

Fedora 40 has the following differences that may affect E2E tests:

- **OBS Version**: Latest upstream OBS Studio
- **FFmpeg Version**: FFmpeg-free (some codecs may differ)
- **Qt Version**: Latest Qt6 from Fedora repos
- **Mesa Drivers**: Latest Mesa from Fedora repos

## Configuration Overrides

None required currently - using defaults.

## Known Issues

None identified yet.

## Testing Notes

- Fedora typically has newer packages than Ubuntu
- May catch issues with latest upstream dependencies
- Good for testing bleeding-edge compatibility
