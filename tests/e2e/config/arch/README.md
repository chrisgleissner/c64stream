# Arch Linux E2E Test Configuration

## Differences from Ubuntu

Arch Linux (rolling release) has the following differences that may affect E2E tests:

- **OBS Version**: Latest upstream OBS Studio (rolling)
- **FFmpeg Version**: Latest FFmpeg (rolling)
- **Qt Version**: Latest Qt6 (rolling)
- **Mesa Drivers**: Latest Mesa (rolling)

## Configuration Overrides

None required currently - using defaults.

## Known Issues

None identified yet.

## Testing Notes

- Arch uses absolute latest packages (rolling release)
- Best for catching issues with cutting-edge dependencies
- May require more frequent updates to handle upstream changes
- Good canary for upcoming library version issues
