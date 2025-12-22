# Import/Export Configuration Feature Analysis

## Overview

This document analyzes the import/export configuration feature added to c64stream, which allows users to save and restore complete plugin settings via INI files.

## Feature Summary

The import/export functionality provides:
- **Export**: Save all plugin settings to a `.ini` file (network, recording, effects)
- **Import**: Load settings from a `.ini` file, replacing current configuration
- **File format**: INI-style text format with sections ([network], [effects], etc.)
- **Integration**: Export/Import buttons in properties UI, file requested in bug reports
- **Use cases**: Backup configurations, share setups, attach to bug reports

## Potential Issues and Gaps

### 1. File Format and Parsing

**Issue**: Custom INI parser implementation
```c
// src/c64-properties.c: Manual INI parsing with fgets
while (fgets(line, sizeof(line), file)) {
    char *equals = strchr(line, '=');
    ...
}
```
- **Risk**: Custom parser may not handle all edge cases
- **Gap**: No support for quoted strings, escape sequences, multi-line values
- **Impact**: Cannot export/import values containing special characters
- **Recommendation**: Consider using standard INI library or document limitations

**Issue**: Line length limit
```c
char line[512];
```
- **Risk**: Long paths (>512 chars) truncated
- **Gap**: No overflow protection or warning
- **Impact**: Partial path imported, likely invalid
- **Recommendation**: Increase buffer size or use dynamic allocation

**Issue**: Whitespace handling
```c
// trim_config_string() removes leading/trailing whitespace
trim_config_string(value);
```
- **Risk**: Intentional whitespace in paths removed
- **Gap**: No way to preserve spaces in folder names
- **Impact**: Paths with trailing spaces become invalid
- **Recommendation**: Only trim leading whitespace, or use quotes

### 2. Validation and Error Handling

**Issue**: Inconsistent validation
```c
// Ports validated:
if (port >= 1024 && port <= 65535)
    obs_data_set_int(settings, "video_port", port);

// Floats not validated:
obs_data_set_double(settings, "scan_line_distance", os_strtod(value));
```
- **Risk**: Malformed INI can set invalid float values (NaN, Inf, negative)
- **Gap**: No range checks for effect strength values
- **Impact**: Corrupted configuration, visual glitches, potential crashes
- **Recommendation**: Add validation for all imported numeric values

**Issue**: Silent failures
```c
// Import returns bool but caller doesn't use it
static bool c64_apply_ini_to_settings(obs_data_t *settings, const char *path)
```
- **Gap**: User not notified if file is corrupted or partially imported
- **Impact**: User thinks settings loaded but some values skipped
- **Recommendation**: Collect error messages, show summary to user

**Issue**: No version check
```c
// INI file has no version field
fprintf(f, "# C64 Stream Properties Export\n");
```
- **Risk**: Future plugin versions may have incompatible settings
- **Gap**: No way to detect old/new file format
- **Impact**: Import from old version may crash or misbehave
- **Recommendation**: Add version field, validate on import

### 3. File Path Handling

**Issue**: Cross-platform path separators
```c
// Uses os_fopen but no path normalization
FILE *f = os_fopen(path, "w");
```
- **Risk**: Windows paths with backslashes may not work on Linux
- **Gap**: No path separator conversion
- **Impact**: Cannot share configs across platforms
- **Recommendation**: Normalize paths on import, or document platform limitation

**Issue**: Directory creation
```c
// Recursive directory creation
static bool c64_create_directory_recursive(char *dir)
```
- **Risk**: Race condition if multiple plugins create same parent dir
- **Gap**: No atomic directory creation
- **Impact**: Export fails intermittently
- **Recommendation**: Use os_mkdirs() or handle EEXIST gracefully

**Issue**: No path length validation
```c
// Path passed directly to os_fopen
if (!c64_ensure_parent_dir_exists(path)) { ... }
```
- **Risk**: Extremely long paths cause buffer overflows
- **Gap**: No MAX_PATH check before operations
- **Impact**: Crash or undefined behavior on long paths
- **Recommendation**: Validate path length against platform limits

### 4. Security Concerns

**Issue**: No path sanitization
```c
// User-provided path used directly
FILE *f = os_fopen(path, "w");
```
- **Risk**: Path traversal attack (e.g., "../../system/file.ini")
- **Gap**: No validation that path is within safe directory
- **Impact**: User could be tricked into overwriting system files
- **Recommendation**: Validate path doesn't escape safe locations

**Issue**: Uncontrolled file write
```c
// Overwrites existing files without confirmation
FILE *f = os_fopen(path, "w");
```
- **Risk**: User accidentally overwrites important file
- **Gap**: No file existence check or confirmation
- **Impact**: Data loss
- **Recommendation**: OBS file dialog should handle this, document assumption

**Issue**: No file size limit on import
```c
// Reads entire file line by line
while (fgets(line, sizeof(line), file)) { ... }
```
- **Risk**: Malicious large file causes excessive memory/CPU usage
- **Gap**: No file size check before parsing
- **Impact**: Plugin hangs or crashes OBS
- **Recommendation**: Check file size before opening, limit to reasonable size (e.g., 1MB)

### 5. Data Integrity

**Issue**: No checksum or validation
```c
// Plain text INI with no integrity check
```
- **Risk**: File corruption undetected
- **Gap**: No way to verify file wasn't modified
- **Impact**: Partial settings applied from corrupted file
- **Recommendation**: Add simple checksum (e.g., CRC32 in header comment)

**Issue**: Partial import on error
```c
// Settings applied incrementally during parse
obs_data_set_int(settings, "video_port", port);
```
- **Risk**: Import fails midway, leaving mixed old/new settings
- **Gap**: No atomic import (all-or-nothing)
- **Impact**: Corrupted configuration state
- **Recommendation**: Parse into temp buffer, validate, then apply all settings

**Issue**: No backup before import
```c
// Directly modifies settings without saving previous values
```
- **Gap**: User cannot undo import
- **Impact**: Lost configuration if import goes wrong
- **Recommendation**: Auto-export before import, or offer undo

### 6. Preset Interaction

**Issue**: Preset vs imported settings
```c
// Import sets preset AND individual values
obs_data_set_string(settings, "crt_preset", value);
obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, value);
```
- **Risk**: Preset may override imported slider values
- **Gap**: Unclear precedence between preset and individual settings
- **Impact**: Imported tweaks lost if preset re-applied
- **Recommendation**: Document that import of preset+tweaks requires "last_applied" sync

**Issue**: Preset name validation
```c
// Preset name imported without validation
obs_data_set_string(settings, "crt_preset", value);
```
- **Risk**: Invalid preset name causes UI confusion
- **Gap**: No check if preset exists
- **Impact**: Preset dropdown shows invalid selection
- **Recommendation**: Validate preset name against known presets

### 7. User Experience

**Issue**: No progress indication
```c
// Import/export are synchronous blocking operations
```
- **Gap**: No feedback during long operations
- **Impact**: UI freezes on large files or slow storage
- **Recommendation**: Add progress callback or do async

**Issue**: No success confirmation
```c
// Property button callbacks don't show messages
```
- **Gap**: User doesn't know if export succeeded
- **Impact**: User may not check if file was created
- **Recommendation**: Show OBS info message "Settings exported to <path>"

**Issue**: File dialog defaults
```c
// Property UI uses file path string, no dialog hints
```
- **Gap**: User must type full path or browse without defaults
- **Impact**: Awkward UX for finding/creating export files
- **Recommendation**: Set default filename (e.g., "c64stream-config.ini")

### 8. Missing Features

**Issue**: No selective export
```c
// Exports all settings, no option to export subset
```
- **Gap**: Cannot export only effects, or only network settings
- **Impact**: Sharing only CRT presets requires editing file manually
- **Recommendation**: Add checkboxes for section selection

**Issue**: No import preview
```c
// Settings applied immediately without preview
```
- **Gap**: User can't see what will change before applying
- **Impact**: Accidental configuration changes
- **Recommendation**: Show diff or summary before applying

**Issue**: No recent files list
```c
// No memory of recently exported/imported files
```
- **Gap**: User must navigate to file each time
- **Impact**: Tedious workflow for frequent import/export
- **Recommendation**: Add recent files dropdown or history

### 9. Testing Coverage

**Issue**: No automated tests for import/export
```c
// Feature added without corresponding test suite
```
- **Gap**: No tests for:
  - Round-trip (export then import yields same settings)
  - Malformed INI files (missing values, wrong types)
  - Edge cases (empty strings, max values, special characters)
  - Cross-platform paths
- **Impact**: Bugs not caught until user reports
- **Recommendation**: Add unit tests for parser and integration tests

### 10. Documentation

**Issue**: INI format not documented
```c
// File format defined only in code
```
- **Gap**: Users cannot manually create/edit INI files
- **Impact**: Advanced users cannot script configuration
- **Recommendation**: Document INI format in user guide

**Issue**: Import behavior not explained
```c
// No description of what happens to unlisted settings
```
- **Gap**: User doesn't know if omitted settings are reset or kept
- **Impact**: Unexpected behavior (settings actually kept)
- **Recommendation**: Document that unmentioned settings remain unchanged

## Critical Path Issues (Must Fix)

1. **Add validation for all imported values** (crash/corruption risk)
2. **Check file size before import** (DoS risk)
3. **Add version field to INI format** (compatibility)
4. **Handle partial import failures** (atomic import or rollback)

## Medium Priority Issues (Should Fix)

5. **Path length validation** (buffer overflow risk)
6. **Show export/import success/failure** (UX feedback)
7. **Increase line buffer size** (long path support)
8. **Add checksum for integrity** (detect corruption)

## Low Priority Issues (Nice to Have)

9. **Selective export** (export only sections)
10. **Import preview** (show changes before applying)
11. **Recent files list** (convenience)
12. **Document INI format** (advanced users)

## Testing Gaps

- **No round-trip test** (export→import→verify)
- **No malformed input tests** (fuzzing, invalid values)
- **No cross-platform tests** (Windows ↔ Linux ↔ macOS)
- **No stress test** (large files, many imports)
- **No UI workflow test** (button clicks, file dialog)

## Recommendations for Future Work

1. **Add comprehensive validation**: Range checks for all numeric values
2. **Implement atomic import**: Parse fully before applying settings
3. **Add version checking**: Future-proof file format
4. **Improve error reporting**: Collect and display all issues to user
5. **Create test suite**: Unit tests for parser, integration tests for workflow
6. **Document file format**: Enable advanced usage
7. **Add progress feedback**: Async operations with status updates
8. **Implement selective export**: Export only chosen sections

## LLM-Friendly Summary

**Core feature**: Import/export saves all plugin settings to INI text file for backup, sharing, bug reports.

**Critical issues**: No validation for imported floats, no version check, no file size limit, partial import on error.

**Key gaps**: Silent failures, no integrity check, no atomic import, missing tests, format not documented.

**Quick wins**: Add numeric range validation, show success/failure message, check file size, increase line buffer.

**Long-term**: Add version field, implement atomic import, create test suite, add selective export.
