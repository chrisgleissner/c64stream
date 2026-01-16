# C64Script Trace Validation System

## Overview

The trace validation system ensures all `.c64script` files in the repository are executable and produce consistent, expected behavior. This prevents shipping broken scripts and documents expected execution paths.

## Key Features

### 1. Automatic Trace Recording

- Traces capture line-by-line execution with variable states
- YAML format for human readability
- Automatic during test execution when `.expected-trace.yaml` exists

### 2. Trace Size Limit

- **Maximum: 1000 steps** (enforced at runtime)
- Prevents huge trace files from being checked into the repository
- Scripts exceeding limit fail with error message
- Encourages writing focused, testable scripts

### 3. Execution Protection

- **5-second timeout** per script (alarm + setjmp/longjmp)
- **100,000 iteration limit** (prevents infinite loops)
- Both mechanisms work together for comprehensive protection

## Trace Format

### File Naming

- Script: `script_name.c64script`
- Trace: `script_name.expected-trace.yaml` (same directory)

### YAML Structure

```yaml
# Execution trace
script: "script_name.c64script"
program: |
  1: REM First line
  2: x = 1
  3: y = x + 1
trace:
- line: 1
  content: REM First line
  variables: {}
- line: 2
  content: x = 1
  variables:
    x: 1
- line: 3
  content: y = x + 1
  variables:
    x: 1
    y: 2
```

## Usage

### Running Script Validation Tests

#### Local (standalone)

```bash
./build_x86_64/tests/script/test_c64script_all_scripts
```

#### Local (via build script)

```bash
./local-build.sh linux --script-tests
```

#### CI/GitHub Actions

Automatically runs as part of standard build:

```yaml
- name: Run C64Script Validation Tests 📝
  run: |
    ./build_x86_64/tests/script/test_c64script_all_scripts
```

### Creating Expected Traces

1. **Write your script** (`tests/my_test.c64script`)
2. **Run with trace recording** (generates actual trace)
3. **Review the trace** for correctness
4. **Rename to expected**: `my_test.expected-trace.yaml`
5. **Commit both files**

Example:

```bash
# Script will generate trace in /tmp if expected trace exists
./build_x86_64/tests/script/test_c64script_all_scripts
# Review and copy
cp /tmp/c64script_trace_12345.yaml tests/my_test.expected-trace.yaml
# Commit
git add tests/my_test.c64script tests/my_test.expected-trace.yaml
```

## Test Infrastructure

### Components

- `tests/script/test_c64script_all_scripts.c` - Main test runner
- `tests/script/c64script_test_stubs.c/h` - Mocked OBS/C64U dependencies
- `src/c64-script-vm.c` - Trace recording logic

### Mocked Dependencies

- **c64_rest_client**: Simulated C64 Ultimate REST API
- **c64_keyboard**: Simulated keyboard injection
- No real OBS or hardware required

### Timeout Mechanisms

1. **Alarm (CPU-bound)**: 5 seconds via `SIGALRM`
2. **Iteration limit**: 100,000 iterations max
3. **Trace limit**: 1000 steps max

## Integration Points

### CMakeLists.txt

```cmake
add_test(NAME c64script_all_scripts
         COMMAND test_c64script_all_scripts)
```

### GitHub Workflow

```yaml
- name: Run C64Script Validation Tests 📝
  run: ./build_x86_64/tests/script/test_c64script_all_scripts
```

### Local Build Script

```bash
./local-build.sh linux --script-tests
```

## Best Practices

### Script Design

- **Keep scripts short**: Aim for < 100 trace steps
- **Test one thing**: Focus on single functionality
- **Use REM comments**: Document expected behavior
- **Avoid long waits**: Use minimal delays in tests

### Trace Maintenance

- **Review regularly**: Ensure traces match current behavior
- **Update on changes**: Regenerate when script logic changes
- **Check diffs carefully**: Unexpected trace changes indicate bugs
- **Keep small**: Scripts with > 1000 steps must be refactored

### Debugging Failed Tests

1. **Check error message**: Often indicates the issue
2. **Review trace diff**: Compare expected vs actual
3. **Run locally**: Easier to debug than in CI
4. **Check recent changes**: Script or VM modifications

## Error Messages

### Trace Limit Exceeded

```
Trace step limit exceeded (1000 steps max)
```

**Solution**: Refactor script to be shorter or split into multiple tests

### Execution Timeout

```
Execution timeout after 5 seconds
```

**Solution**: Remove long waits or mark as expected failure

### Iteration Limit

```
Iteration limit exceeded (100000 iterations)
```

**Solution**: Fix infinite loop or mark as expected failure

## Architecture

### Runtime Structure (c64-script.h)

```c
typedef struct c64script_runtime {
    // ... other fields ...
    bool trace_recording_enabled;
    FILE *trace_file;
    char trace_filename[512];
    size_t trace_step_count;  // Current step count
    // ...
} c64script_runtime_t;
```

### Trace Recording (c64-script-vm.c)

```c
static void record_trace_entry(c64script_runtime_t *runtime, int line_num)
{
    // Enforce 1k limit
    if (runtime->trace_step_count >= 1000) {
        runtime->should_stop = true;
        return;
    }
    runtime->trace_step_count++;

    // Write YAML entry
    fprintf(runtime->trace_file, "- line: %d\n", line_num);
    fprintf(runtime->trace_file, "  content: %s\n", line_content);
    // ... write variables ...
}
```

## Future Enhancements

- Trace comparison/diff logic (currently skipped for simplicity)
- Parallel test execution
- Trace visualization tools
- Coverage analysis (which scripts lack traces)
- Automatic trace generation for new scripts
