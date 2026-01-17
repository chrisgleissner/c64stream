# C64 Script E2E Test Scripts

This directory contains test scripts for E2E validation of the script automation system.

## Test Scripts

### test_simple_sequence.c64script
Tests basic command execution and timing:
- Applies 3 different effect presets
- Uses 2-second waits between effects
- Validates commands execute in order
- Total duration: ~6 seconds

**Expected result:** All effects apply successfully, timing accurate to ±100ms

### test_sid_playback.c64script
Tests C64U filesystem path handling and SID playback:
- Plays a SID file from C64U filesystem
- Tests `c64u:` path prefix
- Tests `songnr` parameter
- 30-second playback duration

**Expected result:** REST API call to /v1/runners:sidplay with correct path

### test_loop.c64script
Tests control flow with labels and goto:
- Defines label "start"
- Alternates between two effects
- Loops infinitely via goto
- Must be stopped manually

**Expected result:** Continuous alternation between effects until stopped

### test_error_invalid.c64script
Tests error handling for invalid commands:
- Contains intentional parse error (invalid_command_here)
- Should fail during parse phase

**Expected result:** Parse error, script never executes

### test_error_missing_label.c64script
Tests error handling for missing goto target:
- Goto references non-existent label
- Should fail during execution

**Expected result:** Executor reports error, stops gracefully

### test_cancellation.c64script
Tests immediate cancellation during execution:
- Begins long wait (60 seconds)
- Should be stopped manually after 1-2 seconds
- Subsequent commands should not execute

**Expected result:** Stops within 100ms of stop button click

## Running Tests

### Manual Testing
1. Load C64 Stream source in OBS
2. Enable script automation in properties
3. Select test script
4. Click "Start Script" and observe behavior
5. For cancellation test, click "Stop Script" after 1-2 seconds

### Automated Testing (Future)
E2E scenarios would integrate with existing test harness:
```bash
cd tests/e2e
./e2e.sh --scenario script_simple_sequence --duration 10
./e2e.sh --scenario script_sid_playback --duration 35
./e2e.sh --scenario script_loop --duration 10  # (will auto-stop after 10s)
./e2e.sh --scenario ntsc_script_recording --duration 12
```

## Integration with E2E Framework

To integrate with the existing E2E test framework, create scenario directories:

```
scenarios/script_simple_sequence/
├── scenario.yaml
├── overrides/
│   └── basic/
│       └── scenes/
│           └── C64StreamTest.json
└── test.c64script -> ../../scripts/test_simple_sequence.c64script
```

scenario.yaml example:
```yaml
name: "Script: Simple Sequence"
description: "Test basic script command execution"
format: NTSC
duration: 10
script_enabled: true
script_path: "test.c64script"
assertions:
  - type: effect_changes
    count: 3
  - type: timing_accuracy
    tolerance_ms: 100
```

## Coverage

Current E2E script test coverage:
- ✅ Basic command execution (effect, wait, stop)
- ✅ Control flow (label, goto)
- ✅ C64U path handling (play_sid, run_prg, mount_disk)
- ✅ Error handling (invalid commands, missing labels)
- ✅ Cancellation (stop during execution)

Not yet covered (requires additional test scripts):
- Palette changes
- Reset/reboot commands
- Effect parameter commands
- Complex control flow (nested loops, conditional goto)
- Multiple path types (local files vs C64U paths)
