# TODO: E2E Script Execution Tests

## Current State
The Python test files in `tests/e2e/script/` (e.g., `test_error_handling.py`, `test_language_features.py`) only perform **syntactic validation** - they check that script files exist and contain certain language features, but **DO NOT ACTUALLY EXECUTE** the scripts.

This is inadequate because:
1. Scripts may parse correctly but fail at runtime
2. Side effects (LOG output, variable values, POKE/PEEK operations) are not validated
3. Error handling paths are not actually tested
4. Runtime behavior is not verified

## What Needs to Be Done

### 1. Create Script Execution Harness
Create a test harness that can:
- Execute c64script files using the parser → compiler → VM pipeline
- Capture runtime output (LOG statements)
- Capture runtime errors
- Capture variable values at specific points
- Simulate plugin operations (POKE, PEEK, etc.) with test stubs

### 2. Replace Syntactic Tests with Execution Tests
For each test in `test_error_handling.py` and `test_language_features.py`:

**Before (syntactic check only):**
```python
def test_error_invalid_command(self):
    script = self._load_script("error_invalid_command.c64script")
    self.assertIn("INVALID_COMMAND", script)  # Just checks file contains text
```

**After (execution test):**
```python
def test_error_invalid_command(self):
    result = execute_script("error_invalid_command.c64script")
    self.assertFalse(result.success)
    self.assertIn("Unknown command", result.error_message)
```

### 3. Test Categories to Implement

#### Error Handling Tests
- **Parse errors**: Invalid syntax, unknown commands
- **Runtime errors**: Missing labels, type mismatches, division by zero
- **Stack errors**: GOSUB depth exceeded, FOR/WHILE nesting depth exceeded
- **Bounds errors**: Array out of bounds, string index out of bounds

#### Language Feature Tests
- **Control flow**: IF/THEN/ELSE, FOR loops, WHILE loops, GOTO/GOSUB
- **Variables**: Assignment, type suffixes ($, %), arithmetic operations
- **Functions**: FUN/ENDFUN, parameters, return values, local scope, recursion
- **Arrays**: DIM, indexing, bounds checking
- **Maps**: String keys, dynamic growth, default values
- **Strings**: Concatenation, escape sequences, string functions

#### Side Effect Tests
- **Logging**: Verify LOG output appears correctly
- **Variables**: Check final variable values after script execution
- **Plugin operations**: Verify POKE/PEEK/etc. called with correct parameters

### 4. Implementation Approach

#### Option A: C-based test harness (Recommended)
Add tests to `tests/test_c64script_compiler.c` that:
- Parse and compile each test script
- Execute in VM
- Assert against results

**Pros**:
- Reuses existing test infrastructure
- Fast execution
- Easy to debug with gdb

**Cons**:
- More verbose than Python

#### Option B: Python wrapper around C test executable
Create Python tests that:
- Call `test_c64script_compiler` with script path
- Parse output (JSON or structured text)
- Assert against results

**Pros**:
- Easier to write and maintain tests
- Better error reporting

**Cons**:
- Slower execution
- Extra layer of complexity

#### Option C: Hybrid approach
- C tests for core functionality and performance-critical paths
- Python tests for complex scenarios and integration tests

### 5. Example Implementation (Option A)

Add to `tests/test_c64script_compiler.c`:

```c
TEST(execute_script_from_file)
{
    const char *script_path = "tests/e2e/script/scripts/test_user_functions.c64script";

    // Read script from file
    FILE *f = fopen(script_path, "r");
    assert(f != NULL && "Failed to open script file");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);

    // Parse, compile, execute
    char error[256];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    assert(success);

    // Assert against expected results
    c64script_value_t value;
    success = c64script_runtime_get_var(runtime, "CONSTANT", &value);
    assert(success);
    assert(value.type == VALUE_NUMBER);
    assert(value.as.number == 42.0);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    free(source);
}
```

### 6. Timeline
- **Phase 1** (High Priority): Add execution tests for existing error_handling scripts
- **Phase 2** (High Priority): Add execution tests for language feature scripts
- **Phase 3** (Medium Priority): Expand test coverage with new edge cases
- **Phase 4** (Low Priority): Add performance benchmarks

## Files to Modify
- `tests/test_c64script_compiler.c` - Add execution tests
- `tests/CMakeLists.txt` - Add script file dependencies
- `tests/e2e/script/test_*.py` - Replace or augment with execution tests

## Success Criteria
- ✅ All `.c64script` files in `tests/e2e/script/scripts/` are executed during test runs
- ✅ Runtime errors are properly caught and validated
- ✅ Side effects (LOG output, variable values) are validated
- ✅ Test coverage for all language features with execution validation
- ✅ Fast test execution (< 1 second for full suite)
