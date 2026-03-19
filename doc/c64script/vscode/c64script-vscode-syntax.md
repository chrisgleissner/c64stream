# C64Script VS Code Syntax Highlighting

This directory contains declarative TextMate grammar files for native VS Code syntax highlighting of C64Script (`.c64script`) files.

## Files

- **[`c64script.tmLanguage.json`](c64script.tmLanguage.json)** - TextMate grammar defining syntax patterns and scopes
- **[`c64script-language-configuration.json`](c64script-language-configuration.json)** - Language configuration for brackets, comments, auto-closing pairs
- **[`c64script-vscode-package.json`](c64script-vscode-package.json)** - VS Code extension manifest (for reference)

## Installation

### Option 1: Direct VS Code Configuration (Recommended for Development)

1. **Copy language files to VS Code settings directory:**

   ```bash
   # Linux/macOS
   mkdir -p ~/.vscode/extensions/c64script-syntax-0.1.0
   cp doc/c64script/vscode/c64script.tmLanguage.json ~/.vscode/extensions/c64script-syntax-0.1.0/
   cp doc/c64script/vscode/c64script-language-configuration.json ~/.vscode/extensions/c64script-syntax-0.1.0/
   cp doc/c64script/vscode/c64script-vscode-package.json ~/.vscode/extensions/c64script-syntax-0.1.0/package.json

   # Windows
   mkdir %USERPROFILE%\.vscode\extensions\c64script-syntax-0.1.0
   copy doc\c64script\vscode\c64script.tmLanguage.json %USERPROFILE%\.vscode\extensions\c64script-syntax-0.1.0\
   copy doc\c64script\vscode\c64script-language-configuration.json %USERPROFILE%\.vscode\extensions\c64script-syntax-0.1.0\
   copy doc\c64script\vscode\c64script-vscode-package.json %USERPROFILE%\.vscode\extensions\c64script-syntax-0.1.0\package.json
   ```

2. **Reload VS Code:**
   - Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on macOS)
   - Type "Reload Window" and press Enter

3. **Verify installation:**
   - Open any `.c64script` file
   - Check the language mode in the bottom-right corner - it should show "C64Script"
   - Keywords, strings, numbers, and comments should be syntax highlighted

### Option 2: VS Code Extension Package (For Distribution)

To create a distributable `.vsix` extension package:

1. **Install vsce (VS Code Extension Manager):**

   ```bash
   npm install -g @vscode/vsce
   ```

2. **Create extension directory structure:**

   ```bash
   mkdir -p c64script-vscode-extension
   cd c64script-vscode-extension
   cp ../doc/c64script/vscode/c64script.tmLanguage.json ./
   cp ../doc/c64script/vscode/c64script-language-configuration.json ./
   cp ../doc/c64script/vscode/c64script-vscode-package.json ./package.json
   ```

3. **Package the extension:**

   ```bash
   vsce package
   ```

4. **Install the `.vsix` file:**
   - In VS Code: Extensions → `⋯` menu → "Install from VSIX..."
   - Select the generated `.vsix` file

## Language Features

### Syntax Highlighting

The TextMate grammar provides comprehensive syntax highlighting for:

#### Comments

- `REM Comments` - BASIC-style

#### Strings

- `"Double-quoted strings"`
- Escape sequences: `\"`, `\\`, `\n`, `\r`, `\t`, `\xNN`
- BASIC-style escaping: `""`

#### Numbers

- Decimal: `42`, `3.14`, `-10`
- Hexadecimal: `$C000`, `$FF`, `$D020`
- Duration literals: `500ms`, `1.5s`, `2m`, `1h`

#### Control Keywords

- `IF`/`THEN`/`ELSE`/`ENDIF`
- `FOR`/`TO`/`STEP`/`NEXT`
- `WHILE`/`WEND`/`ENDWHILE`
- `GOTO`/`GOSUB`/`RETURN`
- `FUN`/`ENDFUN`
- `STOP`/`END`

#### Logical Operators

- `NOT`, `AND`, `OR`, `XOR`

#### Commands (all forms supported)

- **Effects:** `EFFECT`, `EFFECT_PARAM`/`EFFECTPARAM`, `PALETTE`, `PALETTE_COLOR`/`PALETTECOLOR`
- **C64 Control:** `PLAY_SID`/`PLAYSID`, `RUN_PRG`/`RUNPRG`, `MOUNT_DISK`/`MOUNTDISK`, `RESET`, `REBOOT`
- **OBS / Assertions:** `OBS`, `SCREENSHOT`, `TARGET`, `SOURCE`, `PREVIEW`, `PATH`, `RECORDING`, `START`, `FRAMES`, `ASSERT`, `IMAGE_EQUALS`, `TOLERANCE`
- **Keyboard:** `TYPE`, `KEY`
- **HTTP:** `HTTP`/`CALL_HTTP`/`CALLHTTP`, `GET`, `POST`, `PUT`, `DELETE`, `PATCH`
- **File I/O:** `READ_FILE`/`READFILE`, `WRITE_FILE`/`WRITEFILE`, `APPEND`, `TRUNCATE`
- **Logging:** `LOG`, `LOGFILE`, `PRINT`, `TRON`, `TROFF`
- **Memory:** `POKE`, `PEEK`
- **Timing:** `WAIT`

#### Built-in Functions

- **String:** `LEN`, `LEFT$`, `RIGHT$`, `MID$`, `CHR$`, `ASC`, `STR$`, `VAL`
- **Math:** `ABS`, `INT`, `RND`, `SIN`, `COS`, `TAN`, `SQRT`, `LOG`, `EXP`
- **Time:** `TIME$()`

#### Labels

- Numeric: `10`, `100`, `1000` (BASIC-style line numbers)
- Alphanumeric: `START:`, `LOOP`, `DONE:`

#### Variables with Type Suffixes

- `NAME$` - String variable
- `COUNT%` - Integer variable
- `DATA()` - Array variable
- `CONFIG{}` - Map variable

### Editor Features

The language configuration provides:

- **Auto-closing pairs:** `()`, `{}`, `[]`, `""`
- **Comment toggling:** `#` (line comment)
- **Bracket matching:** Highlights matching brackets
- **Auto-indentation:** Increases indent after `IF`, `FOR`, `WHILE`, `FUN`; decreases after `ENDIF`, `NEXT`, `WEND`, `ENDFUN`

## Grammar Development

The TextMate grammar in `c64script.tmLanguage.json` was generated from the authoritative EBNF grammar in [`c64script-grammar.ebnf`](c64script-grammar.ebnf).

### Key Design Decisions

1. **Case Insensitivity**: All keywords use `(?i)` regex flag to match any case
2. **Multi-form Keywords**: Both `EFFECTPARAM` and `EFFECT_PARAM` are recognized
3. **Backward Compatibility**: Both `#` and `REM` comment styles are supported
4. **Type Safety**: Variable type suffixes (`$`, `%`, `()`, `{}`) are highlighted distinctly

### Testing

Test the grammar with example scripts in [`data/scripts/`](../data/scripts/):

```bash
code data/scripts/demo_palette_cycle.c64script
code data/scripts/demo_basic_hello_world.c64script
```

Verify that:

- Keywords are highlighted correctly (any case)
- Strings and escape sequences are styled properly
- Comments work for both `#` and `REM`
- Numbers (decimal/hex/duration) are distinguished
- Labels (numeric and alphanumeric) are visible
- Built-in functions are distinguished from commands

## References

- **Language Specification:** [`c64script-spec.md`](c64script-spec.md)
- **EBNF Grammar:** [`c64script-grammar.ebnf`](c64script-grammar.ebnf)
- **Debugging Guide:** [`c64script-debugging.md`](c64script-debugging.md)
- **TextMate Grammar Documentation:** <https://macromates.com/manual/en/language_grammars>
- **VS Code Language Extensions:** <https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide>
