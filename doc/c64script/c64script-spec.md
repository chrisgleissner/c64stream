# C64Script Language Specification

This document defines the BASIC-inspired `.c64script` language as implemented by the C64Stream OBS Studio plugin.

MUST READ:

- c64script-spec: Detailed language spec.
- `c64script-tasks.md`: Breaks down spec into high-level tasks and fills in blanks, but spec remains source of truth where there are conflicts.
- `c64script-progress.md`: Contains concrete, low-level tasks, and tracks progress

Non-goals:

- This document does not specify UI/OBS integration details (Properties UI, key capture UX, etc.).
- This document does not guarantee that every command is fully implemented at runtime (see “Runtime support notes”).

---

## 1. File format

- **File extension**: `.c64script`
- **Encoding**: UTF-8 (ASCII subset is sufficient and recommended)
- **Line endings**: `LF` (`\n`) or `CRLF` (`\r\n`)
- **Size limit (current parser)**: ≤ 1 MiB
- **Primary mental model**: a script is a sequence of statements executed by a worker thread, sequentially, unless control flow changes the next statement.

---

## 2. C64Script Language

BASIC-Inspired, Label-Oriented

### 2.1 Design Goals

- **Familiar to Commodore 64 users**:
  - BASIC-like keywords (`IF`, `THEN`, `FOR`, `NEXT`, `GOSUB`, `RETURN`, `REM`, …)
  - Numeric truthiness (`0` = false, non-zero = true)
  - Optional `LET`
  - Classic built-in functions (`LEFT$`, `RIGHT$`, `MID$`, `CHR$`, `ASC`, `RND`, etc.)
- **Labels first; line numbers optional**:
  - Labels allow assembly-like structure (`START:`) without forcing line numbers.
  - BASIC-style line numbers (`10`, `20`, …) are optional and act like implicit labels for `GOTO`/`GOSUB`.
  - Labels are case-insensitive and may be alphanumeric or numeric-only.
- **Proper structured blocks**:
  - `FOR … NEXT`, `WHILE … WEND` (or `ENDWHILE`), block `IF … THEN … ENDIF`
  - Nested blocks are allowed.
- **Modern programming features**:
  - User-defined functions with `FUN`/`ENDFUN` and local scope
  - Parameterized `GOSUB` for BASIC-style subroutines with arguments
  - Arrays (`DIM DATA(10)`) and maps (`CONFIG{"key"}`) for structured data
  - Extended variable types: numeric, string, integer, boolean, array, map
  - Automatic type casting with clear rules
  - Extended duration units: milliseconds, seconds, minutes, hours, days
- **Script ergonomics**:
  - Quoted strings allow spaces in preset names and file paths.
  - Rich built-in function library for string manipulation, math, random numbers
- **Useful for this plugin**:
  - Variables for paths/durations, basic arithmetic, boolean logic.
  - Optional `POKE`/`PEEK` style access to REST DMA (natural to C64 users).
  - Optional `TYPE`/`KEY` for keystroke injection (autostart, menu navigation).
  - BASIC-like tracing and progress logs (`LOG`, `LOGFILE`, `TRON`, `TROFF`).
  - HTTP REST calls for external API integration
  - Local program execution for workflow automation
  - File I/O for configuration and data processing
  - Fine-grained effect parameter control (`EFFECTPARAM`)
  - Per-color palette customization (`PALETTECOLOR`)

### 2.2 Lexical Rules

#### 2.2.1 Case sensitivity

- Keywords, labels, and identifiers are **case-insensitive** (`goto`, `GOTO`, and `GoTo` are the same).
- A recommended canonical form for display/logging is uppercase keywords and labels (BASIC style).

#### 2.2.2 Whitespace

- Spaces and tabs separate tokens, except inside quoted strings.

#### 2.2.3 Comments

- `REM` is a statement that comments out the rest of the line (BASIC style). After `REM`, the remainder of the line is ignored.

#### 2.2.4 Strings

- Double-quoted: `"..."`.
- To include a quote, either:
  - use doubled quotes: `"He said ""RUN""."` (BASIC style), or
  - use an escape sequence: `"He said \"RUN\"."`
- Recommended escape sequences:
  - `\\` (literal backslash), `\"` (quote)
  - `\r` (RETURN), `\n` (line feed), `\t` (tab)
  - `\xNN` (byte value, hex; useful for PETSCII/control bytes)

#### 2.2.5 Identifiers

- Letter followed by letters/digits/underscore.
- Optional BASIC-like type suffix:
  - `$` string variable (e.g., `PATH$`)
  - `%` integer variable (optional; implementer may treat it as an integer constraint)
  - `()` array variable (e.g., `DATA()` - subscripts specified at access time)
  - `{}` map variable (e.g., `CONFIG{}` - keys specified at access time)
- Variables without suffixes are numeric (double precision) by default.

#### 2.2.6 Labels (including line numbers)

- A line may optionally start with a **label** that acts like an implicit jump target.
- Labels may be:
  - alphanumeric (e.g., `START`, `PLAY2`, `DEMO2026`), or
  - numeric-only (e.g., `10`, `12345`) to resemble BASIC line numbers.
- Labels may end with `:` (recommended), but the colon is optional.
- Label names should not use reserved keywords (e.g., avoid naming a label `IF` or `GOTO`).
- A label can appear on the same line as code, or on a line by itself to label the following line.
- Disambiguation rule (keeps the language usable without becoming a hybrid monster):
  - At the start of a line, an alphanumeric token is treated as a label **only if** it is followed by `:` or end-of-line, or it is followed by whitespace that is **not** immediately followed by `=`.
  - This ensures `I = 0` is an assignment, while `START I=0` can be a label + statement.

#### 2.2.8 Numbers

- Decimal integers and reals: `10`, `1.5`
- Optional hex integer literals (C64/assembly friendly):
  - `$C000` (hex), `$00C6` (hex)

#### 2.2.9 Durations

- Duration literal: `number` + unit: `500ms`, `1.5s`, `0.5m`, `2h`, `3d`
- Supported units:
  - `ms` (milliseconds)
  - `s` (seconds)
  - `m` (minutes)
  - `h` (hours)
  - `d` (days)
- Additionally, allow `WAIT <expr> [unit]` with default unit `s`:
  - `WAIT 1.5` means `1.5s`

### 2.3 Grammar

The complete formal grammar for C64Script is maintained in a separate file:

**[`c64script-grammar.ebnf`](c64script-grammar.ebnf)**

This EBNF grammar defines the language syntax including:

- **Lexemes**: Identifiers, numbers (decimal/hex), strings, duration literals
- **Top-level structure**: Scripts, lines, labels, comments
- **Statements**: All control flow, assignments, and command statements
- **Plugin actions**: EFFECT, PALETTE, C64 control, recording, keyboard, HTTP, file I/O
- **Expressions**: Complete operator precedence and expression syntax

The grammar serves as the authoritative reference for:

- Implementing parsers and validators
- Generating syntax highlighters and language tools
- Understanding language structure and precedence rules

### 2.4 Semantics

#### 2.4.1 Variables and Types

- Variables are global by default (BASIC-like simplicity).
- **Supported types**:
  - **Numeric**: IEEE-754 `double` (default for unsuffixed variables)
  - **String**: UTF-8 (variables with `$` suffix)
  - **Integer**: 32-bit signed integer (variables with `%` suffix; stored as numeric but constrained to integer range)
  - **Boolean**: derived from numeric truthiness (`0` false, non-zero true)
  - **Array**: one-dimensional indexed collection (variables with `()` suffix; zero-based indexing)
    - Declaration: `DIM DATA(10)` creates an array with 11 elements (indices 0-10)
    - Access: `DATA(5)` reads/writes element at index 5
    - Arrays store numeric values by default; use `DATA$()` for string arrays
  - **Map**: key-value collection (variables with `{}` suffix; string keys)
    - Access: `CONFIG{"host"}` reads/writes value for key "host"
    - Keys are always strings; values are numeric by default; use `CONFIG${}` for string-valued maps
- **Type inference**:
  - Variables are created on first assignment.
  - Type is determined by suffix or value type at first assignment.
- **Assignments**:

  - Array/map elements: `DATA(3) = 42`, `CONFIG{"port"} = 8080`

**Array Declaration with `DIM`**:

- `DIM <array_var>(<size>)` allocates an array with `<size>` elements (indices 0 to `<size>-1`).
- Example: `DIM VALUES(10)` creates an array with 10 numeric elements (indices 0-9).
- Example: `DIM NAMES$(5)` creates a string array with 5 elements (indices 0-4).
- Arrays are initialized with default values: `0` for numeric, `""` for strings.
- `<size>` must be greater than 0.
- Re-declaring an array with `DIM` reallocates it, discarding previous contents.
- Maps (`{}` suffix) do not require `DIM`; they are created on first access and grow dynamically.

**Automatic Type Casting Rules**:

The language performs automatic type conversion in specific contexts to maintain BASIC-like simplicity while preventing common errors:

1. **Numeric to String** (automatic):
   - When a numeric value is used in string context (concatenation, string assignment).
   - Format: decimal representation with minimal precision (e.g., `123`, `3.14`).
   - Example: `MSG$ = "Count: " + 42` results in `"Count: 42"`

2. **String to Numeric** (explicit only):
   - Strings are not automatically converted to numbers in arithmetic or relational contexts.
   - Use `VAL(<string>)` for explicit conversion.
   - Using a string in numeric context raises "TYPE MISMATCH".
   - Example: `X = VAL("123") + 1` results in `124`

3. **Numeric to Boolean** (automatic):
   - `0` becomes false; any non-zero value becomes true.
   - Used in `IF`, `WHILE`, and boolean operators.

4. **Boolean to Numeric** (automatic):
   - `false` becomes `0`; `true` becomes `-1` (BASIC convention).

5. **Array/Map to Scalar** (NOT automatic):
   - Arrays and maps cannot be automatically converted to scalar values.
   - Use explicit indexing: `DATA(0)` not `DATA()`.
   - Attempting to use array/map without index raises "TYPE MISMATCH" error.

6. **Integer suffix `%`** (constraining cast):
   - When assigning to integer variable, value is truncated toward zero.
   - Range: -2147483648 to 2147483647
   - Overflow wraps around (implementation-defined behavior).
   - Example: `I% = 3.7` stores `3`

**Type Mismatch Errors**:

The following operations raise "TYPE MISMATCH" errors:

- Using array/map without subscript in scalar context
- Using scalar in array/map subscript context
- Passing wrong type to built-in functions (e.g., `PEEK("hello")`)
- Invalid string-to-numeric conversion

**Type Compatibility in Expressions**:

- Numeric operators (`+`, `-`, `*`, `/`) require numeric operands.
- String operator (`+` for concatenation) casts operands to string when at least one operand is string.
- Relational operators (`=`, `<`, `>`, etc.) compare:
  - numbers if both operands are numeric, or
  - strings if both operands are strings (case-sensitive lexicographic order).
- Mixed numeric/string relational comparisons raise "TYPE MISMATCH".

#### 2.4.2 Control Flow and Stacks

**Script Termination**:

- Scripts terminate when reaching: `STOP` (or `END`), `GOTO`, `LOOP`, or **running out of instructions** (implicit termination).
- Implicit termination: If the instruction pointer reaches the end of bytecode without encountering an explicit termination statement, the script completes successfully.
- This allows scripts to omit trailing `STOP` statements for cleaner code.

The executor maintains explicit stacks:

- `FOR` stack: loop variable, end value, step, loop start location.
- `WHILE` stack: loop condition location, loop start location.
- `GOSUB` stack: return address, saved parameter values (if any), saved local variables (for functions).
- `FUNCTION` stack: function name, parameter names, local variable scope.

**Functions and Parameterized Subroutines**:

**User-defined functions** (modern approach):

- Syntax: `FUN <name>([param1, param2, ...])` ... `ENDFUN`
- Functions create a local scope; parameters and variables declared inside are local.
- Call with `<name>([arg1, arg2, ...])` in expression context.
- Return value with `RETURN <expr>`; returns 0 if no expression provided.
- Example:

  ```basic
  FUN ADD(A, B)
      RETURN A + B
  ENDFUN

  RESULT = ADD(3, 5)  REM RESULT = 8
  ```

**Parameterized GOSUB** (BASIC-inspired approach):

- Syntax: `GOSUB <label>([expr1, expr2, ...])`
- Parameters are passed by creating numbered local variables `PARAM1`, `PARAM2`, etc.
- These variables are accessible within the subroutine until `RETURN`.
- After `RETURN`, parameter variables are destroyed.
- Optional return value: `RETURN <expr>` stores result in special variable `RESULT`.
- Example:

  ```basic
  GOSUB MULTIPLY(5, 7)
  PRINT RESULT  REM Prints 35
  END

  MULTIPLY:
      RESULT = PARAM1 * PARAM2
      RETURN
  ```

**Design note**: Both mechanisms are supported:

- `FUN`/`ENDFUN` is recommended for new scripts (clearer scoping, modern style).
- `GOSUB` with parameters maintains BASIC heritage and is convenient for simple parameterized subroutines.
- `FUN` calls can appear in expressions; `GOSUB` calls are statements only.

Labels and line numbers:

- A label is either an alphanumeric name (e.g., `START`) or a numeric-only “line number” (e.g., `10`).
- Labels are case-insensitive; numeric labels should be normalized by value (`0010` == `10`).
- Numeric labels are labels only (no implicit ordering requirement), but authors may choose to keep them increasing to resemble BASIC listings.
- A label can be defined:
  - as a line prefix: `START:` / `START` / `10:` / `10`, optionally followed by a statement on the same line, or
  - via the compatibility statement `LABEL <label_ref>`.
- A label-only line (label prefix with no statements) labels the **next executable statement line**, skipping empty lines and `#` comment lines.
- `GOTO`/`GOSUB` jump to the labeled location; `RETURN` resumes after the `GOSUB` call site.

`WHILE` terminators:

- `WHILE … WEND` has historic precedent in the Microsoft BASIC family (GW-BASIC/QBasic/QuickBASIC), so `WEND` is recognizable to many BASIC users.
- `ENDWHILE` (and `END WHILE`) are allowed as clearer modern spellings.

Limits should be explicit (e.g., max nesting depth) and produce clear runtime errors when exceeded.

#### 2.4.3 Boolean Logic

- Relational operators return numeric truth values (`0` false, `1` true).
- Boolean operators follow BASIC-like precedence (`NOT` > `AND` > `XOR` > `OR`).
  - `NOT`/`AND`/`OR`/`XOR` operate as **bitwise operators** on integer-truncated operands (useful for common C64 patterns like `PEEK(addr) AND mask`).
  - For bitwise operations, operands are truncated toward zero to signed 32-bit integers; results are returned as signed 32-bit integers.
  - Aliases: `==` for `=`, `!=` for `<>`.
  - Parentheses always override precedence.

#### 2.4.4 Waiting (Duration vs Wall Clock)

`WAIT` supports three forms:

- `WAIT <duration>`
  - If a unit is omitted, the default unit is seconds (e.g., `WAIT 1.5` means `1.5s`).
  - Implementations should sleep in short intervals (polling) to remain cancellable.

  - Current executor timing resolution: waits are executed in 100 ms polling steps and may overshoot. Small waits like `WAIT 50ms` will effectively sleep ~100 ms.

- `WAIT UNTIL <expr>`
  - Waits until a **host wall-clock** target time is reached (or exceeded).
  - `<expr>` may evaluate to:
    - a number: interpreted as Unix epoch seconds (UTC) as a `double`, or
    - a string: parsed as a wall-clock time in one of these recommended formats:
      - `"HH:MM"` or `"HH:MM:SS"` (local time; if already passed today, treat as “tomorrow”),
      - `"YYYY-MM-DD HH:MM:SS"` (local time),
      - ISO-8601 `"YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]"`.
  - If the computed target time is ≤ “now”, the wait completes immediately.
  - If the time cannot be parsed, raise a BASIC-style runtime error (recommended: “ILLEGAL QUANTITY”).

- `WAIT <address>, <mask>[, <value>] [EVERY <duration>]`
  - Reads one byte from `<address>` and compares `(byte & mask)` to `<value>`.
  - If `<value>` is omitted, it defaults to `<mask>` (BASIC V2 behavior).
  - `<address>` must be in `0..65535` and `<mask>/<value>` must be in `0..255`.
  - Polling interval defaults to **500ms** and can be overridden with `EVERY <duration>`.
  - Uses REST DMA reads; failures raise a runtime error.

#### 2.4.5 Plugin-Specific I/O

These statements/functions exist to make C64 automation scripts practical in the context of this plugin (REST control, keyboard injection, and reproducible captures).

##### 2.4.5.1 Effects / palettes

- `EFFECT`, `EFFECTPARAM`, `PALETTE` update OBS source settings.

#### 2.4.5.2 C64U runners / machine control

- `PLAYSID`, `RUNPRG`, `MOUNTDISK`, `RESET`, `REBOOT` call Ultimate 64 REST actions.

#### 2.4.5.3 Memory access (`PEEK`/`POKE`)

- `POKE <address>, <value>` writes one byte to C64 memory via DMA.
  - `<address>` must be in `0..65535` (hex form: `$0000..$FFFF`).
  - `<value>` is truncated to 8-bit (`value & 255`), matching C64 expectations.
  - If `<address>`/`<value>` are not numeric at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `POKE <address>, [legacy format, C64Script, ...]` writes a contiguous byte block starting at `<address>`.
  - Each element is truncated to 8-bit.
  - Implementations should chunk writes to match REST constraints (e.g., ≤ 128 bytes per DMA write).
- `PEEK(<address>)` returns a numeric value `0..255` read from C64 memory via DMA.
  - `PEEK` is a built-in function; unknown function calls should raise an “UNDEF'D FUNCTION” style error.
  - Built-in function names should be treated case-insensitively (`peek($00C6)` is valid).
  - If `<address>` is not numeric at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
  - `PEEK`/`POKE` are network operations and may fail or time out; failures should stop execution with a clear error message by default.

#### 2.4.5.4 Keyboard injection (`TYPE`/`KEY`)

- `TYPE <string_expr>` enqueues keystrokes derived from text.
  - The string is converted to injected bytes using the “BASIC-friendly ASCII→PETSCII” rules described in `doc/c64/c64u-rest-api.md`.
  - Escape sequences like `\r` are useful for RETURN.
  - If `<string_expr>` is not a string at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `KEY <name>` enqueues one symbolic key press (or one raw byte).
  - `<name>` is an identifier or string like `RETURN`, `RUNSTOP`, `HOME`, `CLEAR`, `CURSOR_UP`, `CURSOR_DOWN`, `CURSOR_LEFT`, `CURSOR_RIGHT`.
  - If a numeric/hex literal is provided (`13` or `$0D`), it injects that byte value directly.
  - Key names should be treated case-insensitively (`return`, `RETURN`, `ReTuRn`).
  - `TYPE`/`KEY` enqueue locally; they do not imply the C64 has already consumed the keystrokes. Use `WAIT` or memory polling (`PEEK`) where necessary.

Important injection constraint:

- Keyboard injection is KERNAL keyboard-buffer based; it will not work for software that reads the CIA keyboard matrix directly.

**Built-in Functions**:

The language provides several built-in functions callable in expression context:

- `PEEK(<address>)` - Read byte from C64 memory
  - Returns: numeric value 0-255
  - Example: `B = PEEK($D020)`

- `TIME$()` - Current wall-clock time
  - Returns: string in format "YYYY-MM-DD HH:MM:SS"
  - Example: `LOG "Timestamp: " + TIME$()`

- `LEN(<string>)` - String length
  - Returns: numeric length in characters
  - Example: `IF LEN(PATH$) > 0 THEN ...`

- `LEFT$(<string>, <count>)` - Left substring
  - Returns: leftmost `<count>` characters
  - Example: `PREFIX$ = LEFT$("HELLO", 3)` → `"HEL"`

- `RIGHT$(<string>, <count>)` - Right substring
  - Returns: rightmost `<count>` characters
  - Example: `SUFFIX$ = RIGHT$("HELLO", 2)` → `"LO"`

- `MID$(<string>, <start>, <count>)` - Middle substring
  - Returns: `<count>` characters starting at position `<start>` (1-based)
  - Example: `MIDDLE$ = MID$("HELLO", 2, 3)` → `"ELL"`

- `STR$(<number>)` - Convert number to string
  - Returns: decimal string representation
  - Example: `S$ = STR$(42)` → `"42"`

- `VAL(<string>)` - Convert string to number
  - Returns: numeric value, or 0 if invalid
  - Example: `N = VAL("123.45")` → `123.45`

- `CHR$(<code>)` - Character from code
  - Returns: single-character string from ASCII/PETSCII code
  - Example: `CR$ = CHR$(13)` → carriage return

- `ASC(<string>)` - Code from character
  - Returns: numeric code of first character (0-255)
  - Example: `CODE = ASC("A")` → `65`

- `ABS(<number>)` - Absolute value
  - Example: `A = ABS(-5)` → `5`

- `INT(<number>)` - Integer part (truncate toward zero)
  - Example: `I = INT(3.7)` → `3`

- `RND(<max>)` - Random number
  - Returns: random value in range [0, `<max>`)
  - Example: `DICE = INT(RND(6)) + 1` → 1-6

- `SIN(<angle>)`, `COS(<angle>)`, `TAN(<angle>)` - Trigonometric functions
  - `<angle>` in radians
  - Example: `Y = SIN(3.14159 / 2)` → `1.0`

- `SQRT(<number>)` - Square root
  - Example: `R = SQRT(144)` → `12`

- `LOG(<number>)` - Natural logarithm
  - Example: `L = LOG(2.71828)` → `1.0`

- `EXP(<power>)` - e raised to power
  - Example: `E = EXP(1)` → `2.71828`

**Error handling for built-in functions**:

- Unknown function names raise "UNDEF'D FUNCTION" error
- Type mismatches (wrong argument types) raise "TYPE MISMATCH" error
- Invalid numeric operations (e.g., `SQRT(-1)`) raise "ILLEGAL QUANTITY" error
- All function names are case-insensitive
**Palette color control (`PALETTE_COLOR`)**
- `PALETTECOLOR <index>, <r>, <g>, <b>` sets a specific palette color by index (0-15) to RGB values.
  - `<index>` must be in range 0-15 (palette indices).
  - `<r>`, `<g>`, `<b>` are color components in range 0-255.
  - Example: `PALETTECOLOR 0, 0, 0, 0` sets color 0 (background) to black.
  - Example: `PALETTECOLOR 6, 128, 64, 192` sets color 6 to custom RGB.
  - This allows fine-tuned palette customization beyond preset selection.
  - Invalid index or color values raise "ILLEGAL QUANTITY" error.

**HTTP REST calls (`HTTP`)**

- `HTTP <method> <url> [HEADERS <headers_map>] [BODY <body_expr>] [STATUS <status_var>] [RESPONSE <response_var>]`
- Performs an HTTP request and optionally captures response.
- `<method>`: `GET`, `POST`, `PUT`, `DELETE`, or `PATCH`
- `<url>`: string expression with target URL
- `HEADERS <headers_map>`: optional map variable with header key-value pairs (e.g., `HEADERS{"Content-Type"} = "application/json"`)
- `BODY <body_expr>`: optional request body (string)
- `STATUS <status_var>`: optional variable to receive HTTP status code (e.g., 200, 404, 500)
- `RESPONSE <response_var>`: optional variable to receive response body (string)
- Example:

  ```basic
  HTTP GET "http://example.com/api/status" STATUS S RESPONSE R$
  IF S = 200 THEN
      LOG "Success: " + R$
  ELSE
      LOG "Error: " + S
  ENDIF
  ```

- Network errors raise runtime errors unless STATUS variable is provided (then error code is stored).
- Timeout: implementation-defined (recommended: 30 seconds).

**Local program execution (`RUN_LOCAL`)**

- `RUNLOCAL <path> [ARGS <args_string>] [STATUS <status_var>] [OUTPUT <output_var>]`
- Executes a local program/script and optionally captures result.
- `<path>`: file path to executable (relative to script directory or absolute)
- `ARGS <args_string>`: optional command-line arguments (string)
- `STATUS <status_var>`: optional variable to receive exit code (0 = success)
- `OUTPUT <output_var>`: optional variable to receive stdout+stderr (string)
- Example:

  ```basic
  RUNLOCAL "convert_image.sh" ARGS "input.png output.d64" STATUS CODE OUTPUT OUT$
  IF CODE <> 0 THEN
      LOG "Conversion failed: " + OUT$
  ENDIF
  ```

- Security note: Scripts should validate/sanitize paths to prevent arbitrary code execution.
- Execution is blocking; script waits for program to complete.
- Maximum output capture: implementation-defined (recommended: 1 MB; excess is truncated).

**File I/O operations (`READFILE`, `WRITEFILE`)**

- `READFILE <path>, <var>` reads entire file content into variable.
  - `<path>`: file path (relative to script directory or absolute)
  - `<var>`: destination variable (typically string variable with `$` suffix)
  - Text files are read as UTF-8 strings.
  - Binary files can be read into string variable (bytes as characters).
  - File not found or read errors raise "FILE NOT FOUND" or "I/O ERROR".
  - Example: `READFILE "config.txt", CONFIG$`

- `WRITEFILE <path>, <expr> [APPEND|TRUNCATE]` writes content to file.
  - `<path>`: file path (created if doesn't exist)
  - `<expr>`: content to write (string or numeric; numeric values are converted to strings)
  - `APPEND`: append to existing file (default if mode omitted)
  - `TRUNCATE`: overwrite/create new file
  - Write errors raise "I/O ERROR".
  - Example: `WRITEFILE "output.txt", "Result: " + RESULT, TRUNCATE`
  - Example: `WRITEFILE "log.txt", "Event at " + TIME$(), APPEND`
**Logging / tracing (`LOG`, `LOGFILE`, `TRON`, `TROFF`)**
- `LOGFILE <path> [APPEND|TRUNCATE]` selects a script log file destination.
  - Relative paths should resolve relative to the script file’s directory.
  - If the mode is omitted, default is `APPEND`.
  - If `LOGFILE` was never called, the first `LOG` or `TRON` should implicitly open a default log file (recommended: `<script_basename>.log` next to the script).
  - If `<path>` is not a string at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `LOG <expr>` appends a line to the script log file (string values written as-is; numeric values formatted in decimal).
- `TRON` enables tracing: the executor logs every executed statement automatically (recommended to include line number + label + progress).
- `TROFF` disables tracing.
- `PRINT <expr>` writes to the OBS log (not the script log file).

### 2.5 Effect Parameters Reference

The `EFFECTPARAM` statement allows fine-grained control of visual effects beyond preset selection. Each effect type supports different parameters. Effect names and parameter names are **case-insensitive**.

**General usage**:

```basic
EFFECT "Classic CRT"
EFFECTPARAM "scanline_intensity" 0.7
EFFECTPARAM "phosphor_persistence" 0.3
```

**Common effect types and their parameters**:

#### CRT Effects

Effect presets: `"Classic CRT"`, `"Vintage TV"`, `"Arcade Cabinet"`

Parameters:

- `scanline_intensity` (0.0 - 1.0): Strength of horizontal scanlines (default: 0.5)
- `scanline_thickness` (0.1 - 2.0): Thickness of scanlines in pixels (default: 1.0)
- `curvature` (0.0 - 0.2): Screen curvature amount (default: 0.05)
- `corner_radius` (0.0 - 50.0): Rounded corner radius in pixels (default: 10.0)
- `vignette` (0.0 - 1.0): Edge darkening amount (default: 0.3)
- `phosphor_persistence` (0.0 - 0.9): Afterglow/motion blur (default: 0.2)
- `bloom` (0.0 - 2.0): Glow around bright areas (default: 0.3)
- `noise` (0.0 - 0.5): Random noise/grain (default: 0.1)

Example:

```basic
EFFECT "Classic CRT"
EFFECTPARAM "scanline_intensity" 0.8
EFFECTPARAM "curvature" 0.1
EFFECTPARAM "phosphor_persistence" 0.4
EFFECTPARAM "bloom" 0.5
```

#### Sharp/Pixel Perfect

Effect presets: `"Sharp Pixels"`, `"Pixel Perfect"`

Parameters:

- `grid_intensity` (0.0 - 1.0): Pixel grid visibility (default: 0.0)
- `grid_color_r` (0 - 255): Grid color red component (default: 0)
- `grid_color_g` (0 - 255): Grid color green component (default: 0)
- `grid_color_b` (0 - 255): Grid color blue component (default: 0)

Example:

```basic
EFFECT "Sharp Pixels"
EFFECTPARAM "grid_intensity" 0.3
EFFECTPARAM "grid_color_r" 20
EFFECTPARAM "grid_color_g" 20
EFFECTPARAM "grid_color_b" 20
```

#### Monitor Emulation

Effect presets: `"Amber Monitor"`, `"Green Monitor"`

Parameters:

- `tint_r` (0.0 - 1.0): Red tint component (default: depends on monitor type)
- `tint_g` (0.0 - 1.0): Green tint component (default: depends on monitor type)
- `tint_b` (0.0 - 1.0): Blue tint component (default: depends on monitor type)
- `tint_intensity` (0.0 - 1.0): Overall tint strength (default: 0.8)
- `brightness` (0.5 - 2.0): Screen brightness (default: 1.0)
- `contrast` (0.5 - 2.0): Screen contrast (default: 1.0)

Example:

```basic
EFFECT "Amber Monitor"
EFFECTPARAM "tint_intensity" 0.9
EFFECTPARAM "brightness" 1.2
EFFECTPARAM "contrast" 1.1
```

#### Blur/Smoothing

Effect presets: `"Soft Blur"`, `"CRT Blur"`

Parameters:

- `blur_radius` (0.0 - 10.0): Blur amount in pixels (default: 1.5)
- `blur_direction` (0 - 2): 0=horizontal, 1=vertical, 2=both (default: 2)

Example:

```basic
EFFECT "Soft Blur"
EFFECTPARAM "blur_radius" 2.5
EFFECTPARAM "blur_direction" 2
```

#### Color Adjustments

Available via `EFFECTPARAM` with any effect active:

- `saturation` (0.0 - 2.0): Color saturation (1.0 = normal, default: 1.0)
- `hue_shift` (-180.0 - 180.0): Hue rotation in degrees (default: 0.0)
- `gamma` (0.5 - 2.5): Gamma correction (default: 1.0)
- `brightness` (0.0 - 2.0): Brightness multiplier (default: 1.0)
- `contrast` (0.0 - 2.0): Contrast multiplier (default: 1.0)

Example:

```basic
EFFECT "Classic CRT"
EFFECTPARAM "saturation" 1.2
EFFECTPARAM "brightness" 1.1
EFFECTPARAM "gamma" 0.9
```

**Parameter discovery**:
To discover available parameters for a specific effect at runtime:

1. Use the OBS Studio UI to inspect effect properties
2. Consult `data/effect_presets.ini` for preset configurations
3. Check effect shader source code in `data/effects/` directory
4. Parameters not listed here are implementation-specific and may vary

**Error handling**:

- Unknown effect names: runtime warning, effect unchanged
- Unknown parameter names: runtime warning, parameter unchanged
- Invalid parameter values: runtime warning, clamped to valid range
- Type mismatches: "TYPE MISMATCH" error (parameters must be numeric)

### 2.6 Examples

#### Example 0: Label and line-number forms

These are alternative forms; in a real script, label names must be unique.

```basic
REM Label on the same line
START: I = 0

REM Label on its own line (labels the next line)
START:
I = 0

REM Numeric labels (BASIC-style line numbers)
10 I = 0
10: I = 0
12345:
I = 0
```

#### Example A: BASIC-like sequence with quoted preset names

```basic
REM Fade in, then run a demo
EFFECT "Classic CRT"
PALETTE "colodore"
WAIT 2
RUNPRG "c64u:/Programs/demo.prg"
WAIT 60s
END
```

#### Example B: Label + IF + GOTO (no line numbers)

```basic
START:
I = 0

LOOP:
I = I + 1
EFFECT "Vintage TV"
WAIT 1.5

IF I < 10 THEN GOTO LOOP ELSE GOTO DONE

DONE:
EFFECT "Default"
END
```

#### Example C: FOR/NEXT

```basic
FOR I = 1 TO 5
    PALETTE "pepto_ntsc"
    WAIT 1
    PALETTE "vice_new"
    WAIT 1
NEXT I
END
```

#### Example D: GOSUB/RETURN as “functions”

```basic
PATH$ = "c64u:/Temp/music/galway_collection.sid"

GOSUB PLAYTRACK
TRACK = 2
GOSUB PLAYTRACK
TRACK = 3
GOSUB PLAYTRACK
END

PLAYTRACK:
PLAYSID PATH$ SONGNR=TRACK
WAIT 20
RETURN
```

#### Example E: PEEK/POKE + typing an autostart sequence

```basic
LOGFILE "run.log" TRUNCATE
LOG "Starting"

REM Write a small marker block (example RAM area)
POKE $C000, [1, 2, 3, 4]
LOG "Marker written"

REM Wait until the KERNAL keyboard buffer is empty, then type
WHILE PEEK($00C6) <> 0
    WAIT 50ms
WEND

TYPE "LOAD\"*\",8,1\rRUN\r"
WAIT 10s
END
```

#### Example F: TRON/TROFF for automatic progress logging

```basic
LOGFILE "trace.log" TRUNCATE
TRON

EFFECT "Classic CRT"
PALETTE "colodore"
WAIT 2
EFFECT "Default"

TROFF
LOG "Done"
END
```

#### Example G: Wait until a wall-clock time

```basic
LOGFILE "schedule.log" APPEND
LOG "Waiting for 20:00..."

WAIT UNTIL "20:00:00"
LOG "Starting now"

RECORDSTART
WAIT 60s
RECORDSTOP
END
```

#### Example H: BASIC-style program with line numbers (optional)

Line numbers are optional in C64Script; they behave exactly like labels and exist mainly for that classic BASIC feel.

```basic
10 REM Line numbers are optional; they act like labels for GOTO/GOSUB.
20 LOGFILE "line-numbered.log" TRUNCATE
30 TRON

40 I = 0
50 GOSUB 1000
60 I = I + 1
70 IF I < 3 THEN GOTO 50

80 TROFF
90 LOG "Done"
100 END

1000 REM A tiny “subroutine” (GOSUB/RETURN)
1010 PALETTE "colodore"
1020 WAIT 0.5
1030 PALETTE "pepto_ntsc"
1040 WAIT 0.5
1050 RETURN
```

#### Example I: User-defined functions with parameters

```basic
REM Define a function to apply effect and wait
FUNCTION APPLY_EFFECT(EFFECT_NAME$, DURATION)
    EFFECT EFFECT_NAME$
    WAIT DURATION
    RETURN 1
ENDFUNCTION

REM Define a function to calculate delay based on mode
FUNCTION CALC_DELAY(MODE)
    IF MODE = 1 THEN RETURN 0.5
    IF MODE = 2 THEN RETURN 1.5
    RETURN 1.0
ENDFUNCTION

REM Use the functions
MODE = 2
DELAY = CALC_DELAY(MODE)
RESULT = APPLY_EFFECT("Classic CRT", DELAY)
LOG "Effect applied with delay: " + DELAY
END
```

#### Example J: GOSUB with parameters (BASIC-inspired)

```basic
REM Call subroutine with parameters
GOSUB CONFIGURE("colodore", 2.5)
GOSUB CONFIGURE("pepto_ntsc", 1.0)
END

CONFIGURE:
    REM PARAM1 = palette name, PARAM2 = wait duration
    PALETTE PARAM1
    WAIT PARAM2
    RETURN
```

#### Example K: Arrays and maps

```basic
REM Arrays for storing palette sequence
DIM PALETTES$(5)
PALETTES$(0) = "colodore"
PALETTES$(1) = "pepto_ntsc"
PALETTES$(2) = "vice_new"
PALETTES$(3) = "deekay"
PALETTES$(4) = "ptoing"

REM Map for configuration
CONFIG${"host"} = "192.168.1.64"
CONFIG{"port"} = 8080
CONFIG{"timeout"} = 5000

REM Iterate through palettes
FOR I = 0 TO 4
    PALETTE PALETTES$(I)
    WAIT 2s
NEXT I

REM Use configuration
LOG "Connecting to " + CONFIG${"host"} + ":" + CONFIG{"port"}
END
```

#### Example L: HTTP REST API integration

```basic
REM Configure API endpoint
API_URL$ = "http://192.168.1.64:8080/api/legacy format"

REM Check C64 Ultimate status
HTTP GET API_URL$ + "/status" STATUS STATUS_CODE RESPONSE RESP$
IF STATUS_CODE = 200 THEN
    LOG "C64 Status: " + RESP$
ELSE
    LOG "API Error: " + STATUS_CODE
    STOP
ENDIF

REM Send command via POST
HEADERS${"Content-Type"} = "application/json"
BODY$ = "{\"command\": \"reset\", \"delay\": 1000}"
HTTP POST API_URL$ + "/command" HEADERS HEADERS$ BODY BODY$ STATUS CODE
IF CODE = 200 THEN
    LOG "Command sent successfully"
ENDIF
END
```

#### Example M: Local file processing and program execution

```basic
REM Read configuration file
READFILE "config.txt", CONFIG$
LOG "Configuration loaded: " + CONFIG$

REM Process data with local script
RUNLOCAL "process_data.py" ARGS "input.txt output.d64" STATUS EXIT_CODE OUTPUT OUT$
IF EXIT_CODE <> 0 THEN
    LOG "Processing failed: " + OUT$
    WRITEFILE "errors.log", OUT$, APPEND
    STOP
ELSE
    LOG "Processing completed successfully"
    WRITEFILE "success.log", "Processed at " + TIME$(), APPEND
ENDIF

REM Use the processed file
MOUNTDISK "output.d64"
AUTOSTART
WAIT 30s
END
```

#### Example N: Custom palette colors

```basic
REM Start with a base palette
PALETTE "colodore"

REM Customize specific colors (index, R, G, B)
PALETTECOLOR 0, 0, 0, 0          REM Black background
PALETTECOLOR 1, 255, 255, 255    REM White
PALETTECOLOR 6, 0, 0, 170        REM Dark blue
PALETTECOLOR 14, 128, 192, 255   REM Light blue

REM Apply effect and capture
EFFECT "Classic CRT"
WAIT 2s
RECORDSTART
WAIT 60s
RECORDSTOP
END
```

#### Example O: Long-duration waits and scheduling

```basic
REM Wait various durations
WAIT 500ms          REM Half a second
WAIT 30s            REM Half a minute
WAIT 2.5m           REM Two and a half minutes
WAIT 1h             REM One hour
WAIT 0.5d           REM Half a day (12 hours)

REM Schedule for specific time
WAIT UNTIL "22:30:00"
LOG "Starting nightly demo capture"
RECORDSTART
RUNPRG "c64u:/Demos/nightly_demo.prg"
WAIT 2h
RECORDSTOP
LOG "Nightly capture completed"
END
```

#### Example P: Complex automation with all features

```basic
REM Complete automation example combining all features
LOGFILE "automation.log" TRUNCATE
TRON

REM Configuration
DIM DEMOS$(3)
DEMOS$(0) = "c64u:/Demos/fairlight.prg"
DEMOS$(1) = "c64u:/Demos/booze.prg"
DEMOS$(2) = "c64u:/Demos/eldorado.prg"

CONFIG${"output_dir"} = "/recordings"
CONFIG{"demo_duration"} = 180

REM Function to capture demo
FUNCTION CAPTURE_DEMO(DEMO_PATH$, DURATION)
    LOG "Capturing: " + DEMO_PATH$

    REM Reset machine
    RESET
    WAIT 2s

    REM Load and run
    RUNPRG DEMO_PATH$
    WAIT 5s

    REM Record
    RECORDSTART
    WAIT DURATION
    RECORDSTOP

    RETURN 1
ENDFUNCTION

REM Check system status via HTTP
HTTP GET "http://192.168.1.64:8080/status" STATUS S
IF S <> 200 THEN
    LOG "C64 Ultimate not responding"
    STOP
ENDIF

REM Process each demo
FOR I = 0 TO 2
    REM Custom palette for each demo
    IF I = 0 THEN PALETTE "colodore"
    IF I = 1 THEN PALETTE "pepto_ntsc"
    IF I = 2 THEN PALETTE "vice_new"

    REM Apply CRT effect
    EFFECT "Classic CRT"
    EFFECTPARAM "scanline_intensity" 0.7

    REM Capture the demo
    RESULT = CAPTURE_DEMO(DEMOS$(I), CONFIG{"demo_duration"})

    REM Generate report
    REPORT$ = "Demo " + I + " captured at " + TIME$()
    WRITEFILE CONFIG${"output_dir"} + "/report.txt", REPORT$, APPEND

    WAIT 10s
NEXT I

TROFF
LOG "All captures completed"
END
```

## 3. Implementation Notes

### 3.1 General

- `c64u:` filesystem paths are supported for `PLAYSID`, `RUNPRG`, and `MOUNTDISK` (path-based REST API).
- Local file upload variants are fully supported (uploads file data via REST API for all three commands).
- `AUTOSTART` injects the default template `LOAD"*",8,1\rRUN\r` via keyboard buffer.
- D64 autostart template is customizable via automation configuration (see `c64-automation.h`).
- HTTP requests are parsed and compiled but require libcurl integration for full execution (VM currently returns placeholder values).

### 3.2 Limits

- Max script size: **1 MiB**
- Max line length: **1024** bytes (parser line buffer)
- Max labels: **256** (`C64SCRIPT_MAX_LABELS`)
- Max loop nesting: **16** for FOR and WHILE loops each (`C64SCRIPT_MAX_FOR_NESTING`, `C64SCRIPT_MAX_WHILE_NESTING`)
- Max GOSUB depth: **32** (`C64SCRIPT_MAX_GOSUB_DEPTH`)
- Max variables: **512** (`C64SCRIPT_MAX_VARIABLES`)
- Max bytecode size: **256 KiB** (`C64SCRIPT_MAX_BYTECODE_SIZE`)

### 3.3 Differences from C64 BASIC V2

- **Execution model:** label-oriented scripts; line numbers are optional.
- **Truth values:** relational operators return `1` for true (BASIC uses `-1`).
- **Arrays:** `DIM X(n)` allocates `n` elements (`0..n-1`), not `n+1` elements.
- **WAIT:** supports BASIC-style `WAIT addr,mask[,value]` plus optional `EVERY <duration>` polling (default 500ms).
- **Strings:** UTF-8 strings; comparisons are case-sensitive lexicographic.
