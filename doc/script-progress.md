# C64Script v2 Implementation Progress

This document tracks the implementation of the BASIC-inspired C64 Stream Script language as specified in `script-spec.md`.

## Mission

Implement a fast, deterministic, extensible BASIC-inspired scripting language for OBS plugin control, C64 automation, and REST/keyboard integration.

## Implementation Strategy

**Approach**: AST compiled to bytecode with a small virtual machine

**Rationale**:
- Better performance for loops and repeated operations
- Enables future features (step-through debugging, pause/resume)
- Clean separation between parsing and execution
- Supports TRON/TROFF tracing efficiently

## Progress Overview

- [ ] **Phase 1**: Architecture & Design (20 tasks)
- [ ] **Phase 2**: Tokenizer/Lexer (25 tasks)
- [ ] **Phase 3**: Parser (50 tasks)
- [ ] **Phase 4**: Bytecode & VM (40 tasks)
- [ ] **Phase 5**: Execution Engine (45 tasks)
- [ ] **Phase 6**: Plugin Integration (30 tasks)
- [ ] **Phase 7**: Testing & Validation (40 tasks)

**Total**: 250 tasks

---

## Phase 1: Architecture & Design (20 tasks)

### A. Core Data Structures (10 tasks)
- [ ] Define token structure (type, lexeme, position, cached values)
- [ ] Define AST node types (statements, expressions, operators)
- [ ] Define bytecode instruction set (opcodes, operands)
- [ ] Define runtime value structure (numeric, string, boolean)
- [ ] Define execution context (IP, variables, stacks, flags)
- [ ] Define FOR/WHILE stack structures
- [ ] Define GOSUB return stack structure
- [ ] Define label map structure (name → bytecode address)
- [ ] Define constant pool for bytecode
- [ ] Document all limits (nesting, stack sizes, script size)

### B. Module Architecture (10 tasks)
- [ ] Create `c64-script-v2.h` (public API)
- [ ] Create `c64-script-v2-token.h/.c` (tokenizer)
- [ ] Create `c64-script-v2-ast.h/.c` (AST structures)
- [ ] Create `c64-script-v2-parser.h/.c` (parser)
- [ ] Create `c64-script-v2-bytecode.h/.c` (bytecode compiler)
- [ ] Create `c64-script-v2-vm.h/.c` (virtual machine)
- [ ] Create `c64-script-v2-runtime.h/.c` (execution context)
- [ ] Create `c64-script-v2-builtins.h/.c` (built-in functions)
- [ ] Plan OBS integration points (threading, cancellation)
- [ ] Plan REST/keyboard integration points

---

## Phase 2: Tokenizer/Lexer (25 tasks)

### A. Basic Tokenization (10 tasks)
- [ ] Implement whitespace handling
- [ ] Implement comment detection (# and REM)
- [ ] Implement identifier tokenization (case-insensitive)
- [ ] Implement keyword recognition (case-insensitive)
- [ ] Implement string literal parsing (double-quoted)
- [ ] Implement escape sequences (\r, \n, \t, \xNN, \\, \")
- [ ] Implement doubled quote handling (BASIC-style)
- [ ] Implement decimal number parsing
- [ ] Implement hex literal parsing ($C000)
- [ ] Implement duration literal parsing (500ms, 1.5s)

### B. Advanced Tokenization (15 tasks)
- [ ] Implement label detection (alphanumeric and numeric)
- [ ] Implement label colon handling (optional :)
- [ ] Implement type suffix handling ($ and %)
- [ ] Implement operator tokenization (arithmetic)
- [ ] Implement operator tokenization (relational: =, ==, <>, !=, <, <=, >, >=)
- [ ] Implement operator tokenization (boolean: NOT, AND, XOR, OR)
- [ ] Implement parentheses and comma tokenization
- [ ] Implement line tracking (line number, column)
- [ ] Implement token position tracking for error messages
- [ ] Handle line continuations (if needed)
- [ ] Create unit tests for tokenizer
- [ ] Test edge cases (empty lines, long lines, special chars)
- [ ] Test string escaping edge cases
- [ ] Test number parsing edge cases (hex, decimals)
- [ ] Test comment handling edge cases

---

## Phase 3: Parser (50 tasks)

### A. Expression Parsing (15 tasks)
- [ ] Implement operator precedence (NOT > AND > XOR > OR)
- [ ] Implement operator precedence (*/  before +-)
- [ ] Implement operator precedence (relational before boolean)
- [ ] Parse primary expressions (numbers, strings, identifiers)
- [ ] Parse parenthesized expressions
- [ ] Parse unary operators (+, -, NOT)
- [ ] Parse binary operators (arithmetic)
- [ ] Parse binary operators (relational)
- [ ] Parse binary operators (boolean)
- [ ] Parse function calls (PEEK, user functions)
- [ ] Parse hex literals in expressions
- [ ] Parse duration literals in expressions
- [ ] Create unit tests for expressions
- [ ] Test operator precedence
- [ ] Test complex nested expressions

### B. Statement Parsing (20 tasks)
- [ ] Parse label definitions (line start, with/without colon)
- [ ] Parse REM statements
- [ ] Parse assignment statements (with/without LET)
- [ ] Parse IF/THEN (single-line form)
- [ ] Parse IF/THEN/ELSE (single-line form)
- [ ] Parse IF/THEN/ENDIF (block form)
- [ ] Parse IF/THEN/ELSE/ENDIF (block form)
- [ ] Parse FOR/TO/STEP/NEXT loops
- [ ] Parse WHILE/WEND loops
- [ ] Parse WHILE/ENDWHILE loops
- [ ] Parse WHILE/END WHILE loops
- [ ] Parse GOTO statements
- [ ] Parse GOSUB statements
- [ ] Parse RETURN statements
- [ ] Parse STOP/END statements
- [ ] Parse WAIT duration statements
- [ ] Parse WAIT UNTIL statements
- [ ] Create unit tests for statements
- [ ] Test block nesting
- [ ] Test label resolution

### C. Plugin Action Parsing (15 tasks)
- [ ] Parse EFFECT statements
- [ ] Parse EFFECTPARAM statements
- [ ] Parse PALETTE statements
- [ ] Parse PLAYSID statements (with SONGNR)
- [ ] Parse RUNPRG statements
- [ ] Parse MOUNTDISK statements
- [ ] Parse AUTOSTART statements
- [ ] Parse RESET statements
- [ ] Parse REBOOT statements
- [ ] Parse RECORDSTART statements
- [ ] Parse RECORDSTOP statements
- [ ] Parse TYPE statements
- [ ] Parse KEY statements
- [ ] Parse POKE statements (single and array forms)
- [ ] Create unit tests for plugin actions

### D. Advanced Parsing (5 tasks)
- [ ] Parse LOGFILE statements
- [ ] Parse LOG statements
- [ ] Parse PRINT statements
- [ ] Parse TRON/TROFF statements
- [ ] Handle parse errors with line/column info

---

## Phase 4: Bytecode & VM (40 tasks)

### A. Bytecode Design (15 tasks)
- [ ] Define opcode enum (50+ opcodes)
- [ ] Define instruction format (opcode + operands)
- [ ] Design constant pool (strings, numbers)
- [ ] Design jump target patching strategy
- [ ] Implement NOP (no operation)
- [ ] Implement PUSH_CONST (push constant pool value)
- [ ] Implement PUSH_VAR (push variable value)
- [ ] Implement POP_VAR (pop and store to variable)
- [ ] Implement arithmetic opcodes (+, -, *, /)
- [ ] Implement relational opcodes (=, <, >, etc.)
- [ ] Implement boolean opcodes (NOT, AND, XOR, OR)
- [ ] Implement JUMP (unconditional)
- [ ] Implement JUMP_IF_FALSE (conditional jump)
- [ ] Implement CALL (GOSUB)
- [ ] Implement RETURN

### B. Bytecode Compiler (15 tasks)
- [ ] Implement expression → bytecode compilation
- [ ] Implement statement → bytecode compilation
- [ ] Implement label → address mapping
- [ ] Implement jump patching (forward jumps)
- [ ] Compile IF statements
- [ ] Compile FOR loops
- [ ] Compile WHILE loops
- [ ] Compile GOTO statements
- [ ] Compile GOSUB/RETURN
- [ ] Compile plugin actions
- [ ] Compile built-in function calls (PEEK)
- [ ] Optimize constant folding
- [ ] Create bytecode unit tests
- [ ] Test jump patching
- [ ] Test nested control structures

### C. VM Execution (10 tasks)
- [ ] Implement instruction pointer (IP)
- [ ] Implement operand stack
- [ ] Implement dispatch loop
- [ ] Implement bytecode fetching
- [ ] Implement operand decoding
- [ ] Implement stack operations
- [ ] Implement cancellation checks
- [ ] Handle runtime errors
- [ ] Create VM unit tests
- [ ] Test VM performance

---

## Phase 5: Execution Engine (45 tasks)

### A. Runtime Context (10 tasks)
- [ ] Implement variable storage (hash table or array)
- [ ] Implement numeric variables
- [ ] Implement string variables
- [ ] Implement type suffix handling ($, %)
- [ ] Implement FOR stack (variable, end, step, return IP)
- [ ] Implement WHILE stack (condition IP, loop start IP)
- [ ] Implement GOSUB stack (return address)
- [ ] Implement stack limit checks
- [ ] Implement cancellation flag
- [ ] Implement trace state (TRON/TROFF)

### B. Built-in Functions (5 tasks)
- [ ] Implement PEEK(address)
- [ ] Implement REST DMA read integration
- [ ] Handle network errors in PEEK
- [ ] Handle type errors in PEEK
- [ ] Create PEEK unit tests

### C. Control Flow (10 tasks)
- [ ] Implement GOTO (update IP)
- [ ] Implement GOSUB (push return address, jump)
- [ ] Implement RETURN (pop return address, update IP)
- [ ] Implement FOR loop entry (initialize loop variable)
- [ ] Implement FOR loop increment (step handling)
- [ ] Implement FOR loop exit (check end condition)
- [ ] Implement WHILE loop entry (evaluate condition)
- [ ] Implement WHILE loop exit
- [ ] Implement IF/THEN/ELSE
- [ ] Handle stack underflow/overflow

### D. Plugin Actions (15 tasks)
- [ ] Implement EFFECT (update source settings)
- [ ] Implement EFFECTPARAM (set parameter)
- [ ] Implement PALETTE (change palette)
- [ ] Implement PLAYSID (REST API call)
- [ ] Implement RUNPRG (REST API call)
- [ ] Implement MOUNTDISK (REST API call)
- [ ] Implement AUTOSTART (keyboard injection)
- [ ] Implement RESET (REST API call)
- [ ] Implement REBOOT (REST API call)
- [ ] Implement RECORDSTART (CSV/network recording)
- [ ] Implement RECORDSTOP (CSV/network recording)
- [ ] Implement TYPE (keyboard injection with PETSCII conversion)
- [ ] Implement KEY (symbolic key injection)
- [ ] Implement POKE (single byte DMA write)
- [ ] Implement POKE array (multi-byte DMA write)

### E. Logging & Tracing (5 tasks)
- [ ] Implement LOGFILE (open log file)
- [ ] Implement LOG (append to log file)
- [ ] Implement PRINT (write to OBS log)
- [ ] Implement TRON (enable statement tracing)
- [ ] Implement TROFF (disable statement tracing)

---

## Phase 6: Plugin Integration (30 tasks)

### A. Threading & Lifecycle (10 tasks)
- [ ] Create executor structure for v2 scripts
- [ ] Implement worker thread for script execution
- [ ] Implement script start (compile + execute)
- [ ] Implement script stop (set cancellation flag)
- [ ] Implement mutex for state access
- [ ] Implement status reporting (idle/running/error)
- [ ] Implement progress reporting (% completion)
- [ ] Implement current line reporting
- [ ] Clean up resources on stop
- [ ] Handle executor cleanup

### B. Properties UI (10 tasks)
- [ ] Add "Script Version" dropdown (v1/v2)
- [ ] Update script file picker for v2
- [ ] Add v2 script status display
- [ ] Add v2 current line display
- [ ] Add v2 error message display
- [ ] Update Start/Stop/Reload buttons for v2
- [ ] Add v2-specific help text
- [ ] Handle v2 script parse errors in UI
- [ ] Handle v2 runtime errors in UI
- [ ] Test UI interaction with v2 scripts

### C. REST & Keyboard Integration (10 tasks)
- [ ] Connect PEEK to REST client
- [ ] Connect POKE to REST client
- [ ] Connect TYPE to keyboard module
- [ ] Connect KEY to keyboard module
- [ ] Connect PLAYSID/RUNPRG/MOUNTDISK to REST client
- [ ] Connect RESET/REBOOT to REST client
- [ ] Handle REST client errors in executor
- [ ] Handle keyboard errors in executor
- [ ] Test REST integration
- [ ] Test keyboard integration

---

## Phase 7: Testing & Validation (40 tasks)

### A. Unit Tests (15 tasks)
- [ ] Tokenizer tests (100+ test cases)
- [ ] Parser tests (100+ test cases)
- [ ] Bytecode compiler tests (50+ test cases)
- [ ] VM tests (50+ test cases)
- [ ] Expression evaluation tests
- [ ] Control flow tests
- [ ] Built-in function tests
- [ ] Variable storage tests
- [ ] Stack tests
- [ ] Error handling tests
- [ ] Type checking tests
- [ ] String escaping tests
- [ ] Numeric parsing tests
- [ ] Label resolution tests
- [ ] Operator precedence tests

### B. Integration Tests (15 tasks)
- [ ] Create test scripts from spec examples
- [ ] Test Example A (quoted presets)
- [ ] Test Example B (label + IF + GOTO)
- [ ] Test Example C (FOR/NEXT)
- [ ] Test Example D (GOSUB/RETURN)
- [ ] Test Example E (PEEK/POKE + TYPE)
- [ ] Test Example F (TRON/TROFF)
- [ ] Test Example G (WAIT UNTIL)
- [ ] Test Example H (line numbers)
- [ ] Test nested loops
- [ ] Test nested IF blocks
- [ ] Test complex expressions
- [ ] Test error recovery
- [ ] Test cancellation
- [ ] Test determinism (same script, same output)

### C. E2E Tests (10 tasks)
- [ ] Create E2E test with mock C64U
- [ ] Test full playback sequence
- [ ] Test keyboard injection sequence
- [ ] Test recording control
- [ ] Test effect/palette changes
- [ ] Test PEEK/POKE with mock DMA
- [ ] Test logging and tracing
- [ ] Test wall-clock WAIT UNTIL
- [ ] Test error handling in real scenarios
- [ ] Test performance (script execution speed)

---

## Implementation Milestones

### Milestone 1: Tokenizer Complete ✓
- [ ] All tokens recognized
- [ ] Unit tests pass
- [ ] Edge cases handled

### Milestone 2: Parser Complete ✓
- [ ] All statements parsed
- [ ] AST structure validated
- [ ] Unit tests pass

### Milestone 3: Bytecode Complete ✓
- [ ] All opcodes defined
- [ ] Compiler generates valid bytecode
- [ ] Jump patching works

### Milestone 4: VM Complete ✓
- [ ] VM executes all opcodes
- [ ] Stack operations correct
- [ ] Unit tests pass

### Milestone 5: Full Language Support ✓
- [ ] All control structures work
- [ ] All plugin actions work
- [ ] PEEK/POKE/TYPE/KEY work
- [ ] Integration tests pass

### Milestone 6: OBS Integration Complete ✓
- [ ] Worker thread executes scripts
- [ ] UI displays v2 status
- [ ] Start/Stop/Reload work
- [ ] E2E tests pass

### Milestone 7: Production Ready ✓
- [ ] All tests pass
- [ ] Documentation complete
- [ ] Performance acceptable
- [ ] CI passes

---

## Current Status

**Phase**: Phase 1 - Architecture & Design
**Progress**: 0/250 tasks (0%)

**Next Steps**:
1. Define core data structures
2. Create module skeleton
3. Begin tokenizer implementation
