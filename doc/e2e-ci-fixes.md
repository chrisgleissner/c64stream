# E2E CI Test Fixes - Implementation Summary

## Problem Analysis

The E2E tests on CI were failing with the error:
```
❌ No .tar.xz file found in artifact
Error: Process completed with exit code 1.
```

### Root Cause

The artifact structure differs between release builds and branch builds:

1. **Release builds (tagged)**: 
   - Created via `cmake --build ... -t package` 
   - Produces `.deb` and `.ddeb` packages
   - No `.tar.xz` archive included

2. **Branch builds (main)**:
   - Created via tar archive (when `package=false`)
   - Produces `.tar.xz` archive
   - Internal structure: `lib/x86_64-linux-gnu/obs-plugins/` and `share/obs/obs-plugins/`

3. **The E2E workflow**:
   - Was trying to use release artifacts (0.8.0) which only had `.deb` files
   - Expected `.tar.xz` files
   - Had no logic to handle `.deb` packages

## Solution Implemented

### 1. Enhanced Logging in e2e.sh

Added detailed plugin installation logging when `--verbose` flag is used:

```bash
# Shows when verbose mode is enabled:
- Binary location and file size
- MD5 checksum of installed binary
- Data directory contents
- Full plugin directory structure
```

This helps diagnose installation issues on CI.

### 2. Fixed Artifact Handling in e2e-test.yaml

The "Install Plugin" step now:

- **Detects artifact type** (`.deb`, `.tar.xz`, or zip containing either)
- **Handles .deb packages**:
  - Installs via `dpkg -i`
  - Runs `apt-get install -f` if dependencies are missing
- **Handles .tar.xz archives**:
  - Extracts to find plugin files
  - Tries multiple possible paths for plugin binary
  - Installs to user plugin directory (`~/.config/obs-studio/plugins/`)
- **Comprehensive logging** at each step for debugging
- **Verification step** checks both user and system plugin directories

### 3. Enhanced Manual E2E Workflow

Added support for testing different plugin sources:

#### New `source_type` Input Options:

1. **`latest_build`** (default):
   - Downloads from latest successful build on main branch
   - Uses GitHub Actions API to find latest workflow run
   - Downloads artifact via nightly.link (public access without token)
   - Handles both `.deb` and `.tar.xz` formats

2. **`latest_release`**:
   - Downloads from latest GitHub release
   - Uses GitHub Releases API

3. **`specific_release`**:
   - Downloads from specific release tag
   - Requires `release_tag` input

#### Implementation Details:

```yaml
# Example manual workflow dispatch:
source_type: latest_build  # Test latest build from main
format: PAL
frames: 250
```

The workflow:
- Fetches artifact metadata from GitHub API
- Downloads via nightly.link (no authentication required)
- Extracts and installs plugin
- Runs E2E tests
- Uploads results including OBS logs

### 4. Added OBS Logs to Artifacts

Both workflows now upload OBS Studio logs:
```yaml
~/.config/obs-studio/logs/*.txt
~/.config/obs-studio/logs/*.log
```

This helps diagnose plugin loading and runtime issues.

## Artifact Structure Reference

### Release Artifacts (from tagged builds)

```
c64stream-0.8.0-x86_64-linux-gnu.deb
c64stream-0.8.0-x86_64-linux-gnu-dbgsym.ddeb
```

When installed via dpkg:
```
/usr/lib/obs-plugins/c64stream.so
/usr/share/obs/obs-plugins/c64stream/
```

### Branch Build Artifacts (from main)

```
c64stream-<version>-x86_64-ubuntu-gnu.tar.xz
```

Archive structure:
```
lib/x86_64-linux-gnu/obs-plugins/c64stream.so
share/obs/obs-plugins/c64stream/
```

## Testing the Fix

### Manual Testing

To test the fixes manually, trigger the "Manual E2E Test" workflow:

```bash
# Via GitHub UI:
Actions → Manual E2E Test → Run workflow
  source_type: latest_build
  format: PAL
  frames: 250
```

### Automated Testing

The E2E test runs automatically after each build on the main branch:
- Triggered by `build-project.yaml` completion
- Uses the artifact from that specific build
- Should now handle `.tar.xz` archives correctly

## Verification Checklist

- [x] Shell script syntax validated
- [x] YAML syntax validated
- [x] Verbose logging added to e2e.sh
- [x] Artifact type detection implemented
- [x] .deb package installation supported
- [x] .tar.xz archive extraction supported
- [x] Multiple plugin path fallbacks implemented
- [x] OBS logs added to artifacts
- [x] Manual workflow supports latest_build option
- [ ] Manual workflow tested successfully
- [ ] Automated E2E on main branch verified

## Next Steps

1. **Test the manual workflow** with `source_type: latest_build`
2. **Review the logs** from the test run to identify any remaining issues
3. **Iterate on fixes** based on the actual CI behavior
4. **Document any additional findings** in this file

## Known Considerations

### Artifact Download Method

We use `nightly.link` for downloading artifacts in the manual workflow because:
- GitHub Actions API requires authentication for artifact downloads
- The manual workflow is designed for external contributors
- nightly.link provides public access to artifacts without tokens

### Plugin Installation Location

The E2E tests install to user plugin directory:
```
~/.config/obs-studio/plugins/c64stream/
```

This is preferred over system installation because:
- No sudo required for most operations
- Matches typical OBS plugin development workflow
- Easier cleanup between test runs

### Release vs Branch Builds

The difference in artifact structure is intentional:
- Releases: Packages for distribution (.deb, .pkg, .exe)
- Branch builds: Archives for testing and development (.tar.xz)

Both formats are now supported by the E2E workflows.
