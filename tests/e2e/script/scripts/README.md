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

Complete E2E script test coverage for ALL C64Script commands:

### ✅ Core Language Features
- ✅ Control flow (FOR/WHILE/IF/GOTO/GOSUB) - `test_nested_loops.c64script`, `test_iteration_counts.c64script`, `test_loop.c64script`
- ✅ Boolean logic (AND/OR/NOT/XOR, comparisons) - `test_boolean_logic.c64script`, `test_comparisons.c64script`
- ✅ Variables and scope - `test_variable_scope.c64script`
- ✅ Arrays and maps - `test_arrays_maps.c64script`
- ✅ Built-in functions - `test_functions_builtin.c64script`
- ✅ User-defined functions - `test_user_functions.c64script`
- ✅ Language features (LET/REM) - `test_let_rem.c64script`

### ✅ Plugin Commands
- ✅ Visual effects (EFFECT/EFFECTPARAM) - `test_effect_params.c64script`
- ✅ Palette control (PALETTE/PALETTECOLOR) - `test_palette_commands.c64script`
- ✅ C64 control (PLAYSID/RUNPRG/MOUNTDISK/RESET/REBOOT/AUTOSTART) - `test_c64_control.c64script`
- ✅ Recording (RECORDSTART/RECORDSTOP) - `test_recording.c64script`
- ✅ Keyboard injection (TYPE/KEY) - `test_keyboard_injection.c64script`
- ✅ Memory access (POKE/PEEK) - `test_memory_access.c64script`
- ✅ Logging (LOG/LOGFILE/TRON/TROFF/PRINT) - `test_logging.c64script`
- ✅ File I/O (READFILE/WRITEFILE) - `test_file_io.c64script`
- ✅ HTTP REST API (GET/POST/PUT/DELETE/PATCH) - `test_http_rest.c64script`
- ✅ Local execution (RUNLOCAL) - `test_local_execution.c64script`
- ✅ Timing (WAIT/WAIT UNTIL) - `test_wait_until.c64script`

### ✅ Error Handling
- ✅ Parse errors (invalid commands, duplicate labels, unclosed blocks) - 6 error test scripts
- ✅ Runtime errors (missing labels, type mismatches, stack overflow) - 3 error test scripts
- ✅ Safety limits (max nesting, runaway loops) - 2 safety test scripts

**Total: 34 test scripts covering 100% of C64Script commands**
