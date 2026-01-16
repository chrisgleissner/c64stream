# C64Script Coverage Matrix

This document summarizes language features vs. existing tests and tracks known gaps.
It is a living checklist for `tests/script/` coverage.

## Lexical

- Identifiers, keywords, numbers, strings: `tests/script/test_c64script_parser.c`
- String edge cases: `tests/script/test_c64script_edge_cases.c`
- Hex literals: `tests/script/scripts/test_memory_access.c64script`
- Duration literals: `tests/script/scripts/test_coverage_duration_units.c64script`
- Labels (with/without colon, numeric): `tests/script/scripts/test_coverage_label_no_colon.c64script`,
  `tests/script/scripts/test_coverage_numeric_labels.c64script`
- Keyword variants: `tests/script/scripts/test_coverage_keyword_variants.c64script`

## Grammar / Statements

- Assignment, LET, REM: `tests/script/scripts/test_let_rem.c64script`
- IF (single-line + block): `tests/script/scripts/test_coverage_if_single_line.c64script`,
  `tests/script/scripts/test_boolean_logic.c64script`
- FOR/NEXT: `tests/script/scripts/test_loop.c64script`,
  `tests/script/scripts/test_iteration_counts.c64script`
- WHILE/WEND/ENDWHILE: `tests/script/scripts/test_coverage_while_endings.c64script`
- GOTO/GOSUB/RETURN: `tests/script/scripts/test_loop.c64script`,
  `tests/script/scripts/test_coverage_gosub_params.c64script`
- FUN/ENDFUN: `tests/script/scripts/test_user_functions.c64script`,
  `tests/script/scripts/test_coverage_end_fun.c64script`
- WAIT/WAIT UNTIL: `tests/script/scripts/test_coverage_duration_units.c64script`,
  `tests/script/scripts/test_wait_until.c64script`
- WAIT memory polling: `tests/script/scripts/test_wait_memory.c64script`
- STOP/END: `tests/script/scripts/test_coverage_end_statement.c64script`

## Expressions / Operators

- Arithmetic + precedence: `tests/script/test_c64script_parser.c`,
  `tests/script/test_c64script_compiler.c`
- Relational ops (numeric + string): `tests/script/scripts/test_comparisons.c64script`
- Boolean ops: `tests/script/scripts/test_boolean_logic.c64script`

## Runtime + Limits

- Variable scope: `tests/script/scripts/test_variable_scope.c64script`
- Label errors: `tests/script/scripts/test_error_missing_label.c64script`,
  `tests/script/scripts/test_error_duplicate_label.c64script`
- Missing NEXT/WEND: `tests/script/scripts/test_error_missing_next.c64script`,
  `tests/script/scripts/test_error_missing_wend.c64script`
- GOSUB overflow / max nesting: `tests/script/scripts/test_error_gosub_overflow.c64script`,
  `tests/script/scripts/test_safety_max_nesting.c64script`
- Cancellation / infinite loop: `tests/script/scripts/test_cancellation.c64script`,
  `tests/script/scripts/test_safety_infinite_loop.c64script`
- Thread safety: `tests/script/test_c64script_thread_safety.c`

## Builtins / I/O

- PEEK/POKE: `tests/script/scripts/test_memory_access.c64script`
- String + math builtins: `tests/script/scripts/test_functions_builtin.c64script`
- Logging / tracing: `tests/script/scripts/test_logging.c64script`
- FILE IO: `tests/script/scripts/test_file_io.c64script`
- HTTP: `tests/script/scripts/test_http_rest.c64script`
- RUNLOCAL: `tests/script/scripts/test_local_execution.c64script`
- TYPE/KEY: `tests/script/scripts/test_keyboard_injection.c64script`
- EFFECT/PALLETTE/PALLETTECOLOR: `tests/script/scripts/test_effect_params.c64script`,
  `tests/script/scripts/test_palette_commands.c64script`
*** End Patch}]} commentaries to=functions.apply_patch only. abort? Wait. Use proper format. It's fine. but I used apply_patch with JSON incorrectly. Must call tool with string. Let's redo.
