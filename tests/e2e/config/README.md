# Distribution-Specific E2E Test Configurations

This directory contains distribution-specific configurations for E2E tests.

## Structure

```
tests/e2e/config/
├── ubuntu/     # Ubuntu-specific configurations (if needed)
├── debian/     # Debian-specific configurations
├── fedora/     # Fedora-specific configurations
└── arch/       # Arch Linux-specific configurations
```

## Purpose

While the E2E test suite is designed to work identically across all distributions, some distributions may require:

- **Different OBS paths**: Package managers install OBS in different locations
- **Different library versions**: System library versions may affect behavior
- **Different environment variables**: Distribution-specific environment setup
- **Different test thresholds**: Slight rendering differences may require adjusted tolerances

## Configuration Files

Each distribution directory may contain:

- **obs_config.ini**: OBS-specific configuration overrides
- **test_thresholds.json**: Custom tolerance values for image comparison
- **env_setup.sh**: Environment variable setup script
- **skip_tests.txt**: List of tests to skip on this distribution

## Example: Custom Threshold

`debian/test_thresholds.json`:
```json
{
  "pixel_diff_threshold": 0.02,
  "frame_similarity_threshold": 0.98,
  "notes": "Debian 12 uses older FFmpeg which produces slightly different encoding"
}
```

## Default Behavior

If no distribution-specific configuration exists, the E2E test suite uses default values that work across all distributions.

## Usage in E2E Tests

```python
# Load distribution-specific config
distro = os.environ.get('TEST_DISTRO', 'ubuntu')
config_dir = f'config/{distro}'
if os.path.exists(config_dir):
    load_custom_config(config_dir)
```

## Maintenance

- **Add configurations only when necessary**: Start with defaults
- **Document distribution differences**: Explain why custom config is needed
- **Keep configurations minimal**: Override only what's absolutely necessary
- **Test on primary platform first**: Ubuntu remains the primary test platform
