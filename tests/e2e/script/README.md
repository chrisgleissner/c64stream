# C64Script Test Suite

Comprehensive end-to-end tests for the C64Script scripting language.

## Overview

This test suite validates the full C64Script language implementation without requiring OBS Studio or actual C64 hardware. Tests use mock REST servers and focus on script parsing, compilation, and execution semantics.

## Test Structure

```
tests/e2e/script/
├── scripts/              # Test script files (.c64script)
├── test_control_flow.py  # Control flow tests (loops, GOTO, GOSUB)
├── test_boolean_logic.py # Boolean and comparison operators
├── test_error_handling.py # Error detection and safety limits
├── test_script_parser.py # Script parser validation
└── test_script_executor.py # Script executor validation
```

## Test Categories

### Control Flow (`test_control_flow.py`)

Tests loop constructs, branching, and flow control:
- **Nested loops**: FOR within WHILE, WHILE within FOR, triple nesting
- **Iteration counts**: Forward/backward counting, step values, zero iterations
- **Variable scope**: Loop variable mutation, nested scope, GOSUB parameters
- **GOTO/GOSUB**: Label resolution, subroutine calls, return values

Test scripts:
- `test_nested_loops.c64script` - Multiple levels of loop nesting
- `test_iteration_counts.c64script` - Various FOR loop configurations
- `test_variable_scope.c64script` - Variable mutation and scope rules
- `test_simple_sequence.c64script` - Basic command sequences
- `test_loop.c64script` - GOTO-based loops

### Boolean Logic (`test_boolean_logic.py`)

Tests boolean operators and comparisons:
- **Boolean operators**: AND, OR, NOT, XOR
- **Comparisons**: =, ==, <>, !=, <, <=, >, >=
- **Precedence**: Operator precedence and parentheses
- **Truthiness**: 0 = false, non-zero = true
- **Bitwise operations**: Integer bitwise AND/OR/XOR

Test scripts:
- `test_boolean_logic.c64script` - All boolean operators with combinations
- `test_comparisons.c64script` - All comparison operators with edge cases

### Error Handling (`test_error_handling.py`)

Tests error detection and safety limits:
- **Parse errors**: Invalid commands, duplicate labels, unclosed blocks
- **Runtime errors**: Missing labels, type mismatches, stack overflow
- **Safety limits**: Maximum nesting depth, runaway loop detection

Test scripts:
- `test_error_invalid_command.c64script` - Parse error: invalid command
- `test_error_goto_missing.c64script` - Runtime error: missing label
- `test_error_duplicate_label.c64script` - Parse error: duplicate label
- `test_error_type_mismatch.c64script` - Runtime error: type mismatch
- `test_error_missing_next.c64script` - Parse error: unclosed FOR loop
- `test_error_missing_wend.c64script` - Parse error: unclosed WHILE loop
- `test_error_gosub_overflow.c64script` - Runtime error: GOSUB stack overflow
- `test_safety_max_nesting.c64script` - Safety: maximum nesting depth
- `test_safety_infinite_loop.c64script` - Safety: runaway loop detection

### Legacy Tests

- `test_script_parser.py` - Original parser validation tests
- `test_script_executor.py` - Original executor template tests (mostly skipped)

## Running Tests

### All Script Tests

```bash
cd tests/e2e/script
pytest -v
```

### Specific Test Category

```bash
# Control flow tests only
pytest test_control_flow.py -v

# Boolean logic tests only
pytest test_boolean_logic.py -v

# Error handling tests only
pytest test_error_handling.py -v
```

### Individual Test

```bash
pytest test_control_flow.py::TestControlFlow::test_nested_loops_for_in_while -v
```

## Test Philosophy

### Headless and Deterministic

- **No GUI required**: Tests run in CI without X11/Wayland
- **No OBS dependency**: Scripts are tested in isolation
- **Deterministic**: No timing-based flakiness
- **Fast**: Tests complete in seconds

### Mock-Based Validation

- **Mock REST server**: Simulates C64 Ultimate REST API
- **Call tracking**: Validates correct sequence of operations
- **No network dependency**: All tests run locally

### Script-Driven

- **Real scripts**: Tests execute actual .c64script files
- **Observable effects**: Validates REST calls, logs, execution flow
- **Precise assertions**: Checks ordering, counts, parameters

## Coverage Goals

The test suite aims to exercise:

✅ **Control Flow**
- FOR loops with various step values
- WHILE loops with complex conditions
- Nested loops (2-3 levels deep)
- GOTO and label resolution
- GOSUB and RETURN with parameters

✅ **Boolean Logic**
- All boolean operators (AND, OR, NOT, XOR)
- All comparison operators (=, <>, <, <=, >, >=)
- Operator precedence and parentheses
- Truthiness semantics

✅ **Error Handling**
- Parse-time error detection
- Runtime error detection
- Type mismatch errors
- Missing label errors
- Safety limit enforcement

✅ **Language Features**
- Variable assignment and mutation
- Variable scope in loops and subroutines
- String and numeric types
- Command sequencing

## Non-Goals

❌ **Out of Scope**
- OBS integration testing (covered by main E2E suite)
- Video/audio streaming validation (covered by media tests)
- Performance benchmarking (covered by benchmark suite)
- UI/Properties testing (covered by integration tests)

## Script Naming Conventions

Test scripts follow these conventions:

- `test_*.c64script` - All test scripts start with `test_`
- `test_error_*.c64script` - Scripts designed to trigger parse/runtime errors
- `test_safety_*.c64script` - Scripts that test safety limits
- Lowercase with underscores (snake_case)
- Descriptive names indicating what is tested

## Adding New Tests

To add a new test:

1. **Create test script** in `scripts/` directory
   ```c64script
   # test_my_feature.c64script
   FOR I = 1 TO 5
       EFFECT Sharp Pixels
       WAIT 100ms
   NEXT I
   STOP
   ```

2. **Add test case** to appropriate test file
   ```python
   def test_my_feature(self):
       """Test my new feature"""
       script = self._load_script("test_my_feature.c64script")
       # Add assertions
       self.assertIn("FOR I = 1 TO 5", script)
   ```

3. **Run tests** to verify
   ```bash
   pytest test_control_flow.py::TestControlFlow::test_my_feature -v
   ```

## CI Integration

These tests run automatically in CI:

- **GitHub Actions**: `.github/workflows/test.yml`
- **Local builds**: `./local-build.sh linux --tests`
- **Pre-commit**: Tests can run locally before push

## Troubleshooting

### Tests Not Found

```bash
# Ensure pytest can find tests
cd tests/e2e/script
python3 -m pytest --collect-only
```

### Mock Server Issues

```bash
# Test mock server independently
cd tests/e2e
python3 mock_c64u_server.py --port 8065
# In another terminal:
curl http://localhost:8065/v1/version
```

### Script Syntax Errors

```bash
# Validate script syntax
cd tests/e2e/script/scripts
for f in *.c64script; do
    echo "Checking $f..."
    # Scripts are validated by parser tests
done
```

## Documentation

- **Language spec**: `doc/script-spec.md`
- **Implementation**: `src/c64-script-*.c`
- **Examples**: `data/scripts/*.c64script`
- **REST API**: `doc/rest-control.md`

## Maintenance

These tests are maintained alongside the C64Script language implementation. When adding new language features:

1. Update `doc/script-spec.md` with the new feature
2. Add test scripts exercising the feature
3. Add test cases validating the feature
4. Ensure all tests pass in CI

## License

Licensed under GNU General Public License v2.0 or later.
See LICENSE file for details.
