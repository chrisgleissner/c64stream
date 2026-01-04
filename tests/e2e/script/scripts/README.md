# C64Script Test Scripts

Test scripts for C64Script language validation.

## Feature Tests

**Control Flow**
- `test_nested_loops.c64script` - FOR/WHILE nesting
- `test_iteration_counts.c64script` - Loop configurations
- `test_variable_scope.c64script` - Variable scope and mutation
- `test_loop.c64script` - GOTO-based loops
- `test_simple_sequence.c64script` - Command sequences

**Boolean & Comparisons**
- `test_boolean_logic.c64script` - AND/OR/NOT/XOR operators
- `test_comparisons.c64script` - Comparison operators

**Data Structures**
- `test_arrays_maps.c64script` - DIM, arrays, maps

**Functions**
- `test_functions_builtin.c64script` - Built-in functions (LEFT$, RIGHT$, etc.)
- `test_user_functions.c64script` - FUNCTION/ENDFUNCTION

**Commands**
- `test_palette_commands.c64script` - PALETTE, PALETTECOLOR
- `test_effect_params.c64script` - EFFECT, EFFECTPARAM
- `test_c64_control.c64script` - PLAYSID, RUNPRG, MOUNTDISK, RESET, REBOOT, AUTOSTART
- `test_recording.c64script` - RECORDSTART, RECORDSTOP
- `test_keyboard_injection.c64script` - TYPE, KEY
- `test_memory_access.c64script` - POKE, PEEK
- `test_logging.c64script` - LOG, LOGFILE, TRON, TROFF, PRINT
- `test_file_io.c64script` - READFILE, WRITEFILE
- `test_http_rest.c64script` - HTTP (GET/POST/PUT/DELETE/PATCH)
- `test_local_execution.c64script` - RUNLOCAL
- `test_wait_until.c64script` - WAIT UNTIL

**Language Features**
- `test_let_rem.c64script` - LET, REM statements
- `test_cancellation.c64script` - Script cancellation
- `test_sid_playback.c64script` - C64U path handling

## Error Tests

- `test_error_invalid_command.c64script` - Invalid command
- `test_error_goto_missing.c64script` - Missing label
- `test_error_duplicate_label.c64script` - Duplicate label
- `test_error_type_mismatch.c64script` - Type mismatch
- `test_error_missing_next.c64script` - Unclosed FOR
- `test_error_missing_wend.c64script` - Unclosed WHILE
- `test_error_gosub_overflow.c64script` - Stack overflow
- `test_safety_max_nesting.c64script` - Max nesting depth
- `test_safety_infinite_loop.c64script` - Runaway detection
