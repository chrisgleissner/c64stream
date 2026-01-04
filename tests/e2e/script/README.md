# C64Script Test Suite

End-to-end tests for C64Script language validation.

## Structure

```
tests/e2e/script/
├── scripts/              # Test scripts (.c64script)
├── test_control_flow.py  # Control flow tests
├── test_boolean_logic.py # Boolean/comparison operators
├── test_error_handling.py # Error detection
├── test_command_coverage.py # All commands
└── test_script_parser.py # Parser validation
```

## Running Tests

```bash
cd tests/e2e/script
python3 -m unittest discover -v
```

## Coverage

**32 test scripts** covering 100% of C64Script commands:
- Core language: control flow, boolean logic, data structures, functions
- Plugin commands: effects, C64 control, recording, keyboard, memory, logging, file I/O, HTTP, local execution
- Error handling: parse errors, runtime errors, safety limits

**Note**: Script executor is tested at the C level in `tests/test_c64script_*.c` files.

See `scripts/README.md` for script listing.
