# Keymap Audit — Consolidated Open Issues

## Context

This document consolidates two prior audits (`keymap-audit-findings.md` and `keymap-audit.md`), cross-references every claim against the actual keymap files, PETSCII reference (`c64-petscii-codes.csv`), and XKB layout definitions, and resolves all open design questions.

**Guiding principle:** The user must be able to reach every C64 key from their host keyboard, regardless of which keymap they chose. Intuitiveness and least-surprise behaviour take priority over academic purity.

---

## 1. Positional: CBM Graphics Block Uses Symbolic Key Identifiers

**Status:** Bug. Not identified by either prior audit.

**Severity:** Medium. Affects 5 non-letter CBM graphic bindings in `positional_us.c64keymap.ini`.

The CBM graphics block in the positional map is byte-identical to the symbolic US map. For letter keys (`Alt+KeyA` through `Alt+KeyZ`) this is harmless — the physical code and the symbol agree. But for non-letter C64 keys, the positional map assigns different host codes than the symbolic map:

| C64 key | Positional host code | PETSCII CBM output | Current entry (wrong)   | Correct entry           |
| ------- | -------------------- | ------------------ | ----------------------- | ----------------------- |
| `@`     | `BracketLeft`        | `0xA4`             | `Shift+Alt+Digit2=0xA4` | `Alt+BracketLeft=0xA4`  |
| `+`     | `Minus`              | `0xA6`             | `Shift+Alt+Equal=0xA6`  | `Shift+Alt+Minus=0xA6`  |
| `£`     | `Backspace`          | `0xA8`             | `Alt+Backslash=0xA8`    | `Alt+Backspace=0xA8`    |
| `-`     | `Equal`              | `0xDC`             | `Alt+Minus=0xDC`        | `Alt+Equal=0xDC`        |
| `*`     | `BracketRight`       | `0xDF`             | `Shift+Alt+Digit8=0xDF` | `Alt+BracketRight=0xDF` |

The root cause is that the CBM block was copied from `symbolic_us` without adjusting for the positional key remapping.

**Fix:** Replace the 5 entries above in `positional_us.c64keymap.ini`. Add test 11d to prevent recurrence.

---

## 2. All Maps: CBM+H and CBM+I Are Missing

**Status:** Bug. Not identified by either prior audit.

**Severity:** Low. Two CBM graphic characters are unreachable.

The PETSCII table has CBM entries for every letter except `M`. Most letters appear in the `0xA0–0xBF` primary range. However, `H` and `I` appear only in the `0xE0–0xFF` mirror range:

- `CBM+H` → `0xE5`
- `CBM+I` → `0xE2`

These entries are absent from all 7 keymap files.

**Fix:** Add `Alt+KeyH=0xE5` and `Alt+KeyI=0xE2` to every keymap that carries the CBM graphics block (all 7 files).

---

## 3. Runtime: Separate AltGr from Alt/CBM

**Status:** Unfixed. Identified in second audit.

The runtime modifier model stores a single Alt bit. On European layouts, AltGr is the primary mechanism for core printable symbols (`@`, `[`, `]`, `{`, `}`, `\`, `|`, `~`, `€`). Because keymap lookup is code-first, an `AltGr+<code>` event matches an inherited `Alt+<code>=<CBM graphic>` entry before the text-based symbolic path is reached.

**Decision:** AltGr must be treated as a Level 3 shift (producing printable text), not as Alt/CBM. This is the only option that preserves reachability of standard printable characters on European layouts without surprising the user.

**Fix:**
1. Investigate what OBS delivers for the right Alt key per platform. Qt-based OBS likely exposes `Qt::Key_AltGr` or `Qt::GroupSwitchModifier` on Linux/X11. On Wayland and Windows, check whether the modifier flag or key code distinguishes AltGr from left Alt.
2. In `src/c64-source.c` and `src/c64-interact-key.c`, when AltGr is detected, do not set the CBM modifier bit. Instead, let the text-based lookup path handle the resulting printable character.
3. If OBS on a specific platform cannot distinguish AltGr from Alt (unlikely but possible), document this as a known limitation for that platform and fall back to text-first lookup when any Alt is combined with a printable text event.

**Affected files:** `src/c64-source.c`, `src/c64-interact-key.c`.

---

## 4. Symbolic Locale Maps: Replace Inherited US CBM/Color Entries

**Status:** Unfixed. Identified in second audit. Closely related to §3.

All symbolic locale files inherit `Ctrl+Digit*`, `Alt+Digit*`, `Shift+Alt+Digit2`, `Alt+Backslash`, and similar entries from the US baseline. These use `KeyboardEvent.code` identifiers that assume US physical positions.

**Decision:** Ctrl+Digit and Alt+Digit entries for color codes remain acceptable in all locale files. These target physical key codes (`Digit1`–`Digit8`), not symbols. Even on AZERTY, pressing `Ctrl+<physical Digit1 key>` is a reasonable and discoverable way to select a C64 colour — the key still has "1" printed on it (or at least on the keycap legend), and this is how the C64 itself works (Ctrl + number row position). This is a pragmatic positional shortcut embedded in an otherwise symbolic map, and it is correct to keep it.

The non-letter CBM entries (`Shift+Alt+Digit2`, `Alt+Backslash`, `Shift+Alt+Equal`, `Shift+Alt+Digit8`, `Alt+Minus`) are the problem. These encode CBM+@, CBM+£, CBM++, CBM+\*, and CBM+- using US host codes. On non-US layouts, the underlying printable symbol lives on a different key.

**Fix:** In each non-US symbolic file, replace the 5 non-letter CBM entries with locale-correct equivalents. Once §3 is implemented, these can use text-symbol keys. Until then, use the W3C code of the physical key that produces the relevant symbol on the target layout:

**UK** (`symbolic_uk.c64keymap.ini`):
- `Shift+Alt+Digit2=0xA4` → remove (@ is text-reachable via Shift+Quote; add `Alt+@=0xA4` if the symbolic engine supports text+modifier lookups, otherwise use `Shift+Alt+Quote=0xA4`)
- `Alt+Backslash=0xA8` → remove (£ is text-reachable; add `Alt+£=0xA8` or `Shift+Alt+Digit3=0xA8`)
- `Shift+Alt+Equal=0xA6` → keep (+ is on the same key as US: Equal with Shift)
- `Alt+Minus=0xDC` → keep (- is on the same key as US: Minus)
- `Shift+Alt+Digit8=0xDF` → remove (UK Shift+Digit8 produces `(`, not `*`; \* is Shift+BKSL on UK; add `Shift+Alt+Backslash=0xDF`)

**DE** (`symbolic_de.c64keymap.ini`):
- `Shift+Alt+Digit2=0xA4` → remove (@ is AltGr+Q on DE; after §3, AltGr won't trigger CBM, so add `Alt+@=0xA4` or use `Shift+Alt+KeyQ=0xA4` as interim)
- `Alt+Backslash=0xA8` → remove (£ is AltGr+Shift+Digit3 on DE — barely reachable; keep the US entry as a positional fallback until a better solution is available)
- `Shift+Alt+Equal=0xA6` → remove (+ is on BKSL key level 1 on DE; add `Alt+Backslash=0xA6` or `Alt++=0xA6`)
- `Alt+Minus=0xDC` → keep (- is on Slash key on DE, but the US Minus key has `/` on DE; remap to `Alt+Slash=0xDC`)
- `Shift+Alt+Digit8=0xDF` → remove (\* is Shift+BKSL on DE; add `Shift+Alt+Backslash=0xDF`)

**FR** (`symbolic_fr.c64keymap.ini`):
- `Shift+Alt+Digit2=0xA4` → remove (@ is AltGr+Digit0 on FR; add `Alt+@=0xA4` or `Shift+Alt+Digit0=0xA4`)
- `Alt+Backslash=0xA8` → remove (£ is Shift+AD12 on FR; add `Alt+£=0xA8` or `Shift+Alt+AD12=0xA8`)
- `Shift+Alt+Equal=0xA6` → remove (+ is Shift+AE12 on FR; add `Shift+Alt+AE12=0xA6` — but AE12 is `Equal` in W3C, so `Shift+Alt+Equal=0xA6` accidentally stays correct by coincidence; keep it)
- `Alt+Minus=0xDC` → remove (- is on AE06 base on FR which is `Digit6` in W3C; add `Alt+Digit6=0xDC`, but beware of collision with colour code Alt+Digit6; prefer `Alt+-=0xDC` text symbol lookup)
- `Shift+Alt+Digit8=0xDF` → remove (\* is Shift+BKSL on FR; add `Alt+*=0xDF` or `Shift+Alt+Backslash=0xDF`)

**IT** (`symbolic_it.c64keymap.ini`):
- `Shift+Alt+Digit2=0xA4` → remove (@ is AltGr+AC10 on IT; add `Alt+@=0xA4`)
- `Alt+Backslash=0xA8` → remove (£ is Shift+AE03 on IT; add `Alt+£=0xA8` or `Shift+Alt+Digit3=0xA8`)
- `Shift+Alt+Equal=0xA6` → remove (+ is AD12 base on IT; add `Alt+BracketRight=0xA6` — W3C code for AD12 — or `Alt++=0xA6`)
- `Alt+Minus=0xDC` → keep (- is AB10 base on IT = `Minus` W3C code, same as US)
- `Shift+Alt+Digit8=0xDF` → remove (\* is Shift+AD12 on IT; add `Shift+Alt+BracketRight=0xDF`)

**NL** (`symbolic_nl.c64keymap.ini`):
- `Shift+Alt+Digit2=0xA4` → remove (@ is TLDE base on NL; add `Alt+Backquote=0xA4` — W3C code for TLDE — or `Alt+@=0xA4`)
- `Alt+Backslash=0xA8` → remove (£ is AltGr+Digit7 on NL; add `Alt+£=0xA8`)
- `Shift+Alt+Equal=0xA6` → remove (+ is AC10 base on NL; add `Alt+Semicolon=0xA6` — W3C code for AC10)
- `Alt+Minus=0xDC` → remove (- is AB10 base on NL; W3C code is `Minus` — same as US; keep)
- `Shift+Alt+Digit8=0xDF` → remove (\* is AD12 base on NL; add `Alt+BracketRight=0xDF`)

**Note:** The cleanest fix for all of these is to support text+modifier lookups like `Alt+@=0xA4` in the symbolic engine. If the engine already supports this (text-key with modifier prefix), use that form everywhere and the locale differences resolve automatically. If not, adding that capability is strongly recommended — it eliminates the need for per-locale W3C code overrides entirely.

---

## 5. Runtime: ASCII-Centric Heuristics in UTF-8 Path

**Status:** Partially fixed.

`c64-interact-key.c` still uses single-byte printable checks for text-derived code inference. `c64-source.c` uses a single-byte `has_printable_text` heuristic before modifier classification.

**Fix:** Replace `isprint()`-style single-byte checks with a UTF-8-aware test: if `event->text` has length > 0 and the first byte is not a C0 control character (`0x00–0x1F`, `0x7F`), treat it as printable. This is simple, correct for all supported locales, and does not require a full Unicode category lookup. Add tests per §11b.

**Affected files:** `src/c64-interact-key.c`, `src/c64-source.c`.

---

## 6. UK: CBM+@ and CBM+£ Paths, and Shift+£ Surrogate

**Status:** Unfixed. Verified against actual file and XKB `gb(basic)`.

The UK-specific issues from the inherited US CBM entries are covered in §4 above. The remaining unique UK issue is:

PETSCII `0xA9` (Shift+£) has no natural symbolic path on UK. The host already requires Shift to produce plain `£` (`Shift+Digit3`), so the symbolic engine cannot distinguish "host `£`" from "C64 Shift+£."

**Decision:** Use a documented surrogate. `Shift+Alt+Digit3` is the natural choice: it keeps the same physical key as `£` (Digit3), adds Alt (CBM) alongside the existing Shift. This is discoverable and consistent with the CBM+£ binding.

**Fix:** Add `Shift+Alt+Digit3=0xA9` to `symbolic_uk.c64keymap.ini` alongside the `Alt+Digit3` or text-based CBM+£ binding. Document in a comment that this is a surrogate for C64 Shift+£.

Wait — `Alt+Digit3` is currently unused in the UK file (the Alt+Digit colour block only covers `Alt+Digit1`–`Alt+Digit8`). And `Shift+Alt+Digit3` is free. But we already want `Shift+Alt+Digit3=0xA8` for CBM+£ (from §4). These two collide.

Revised approach:
- `Shift+Alt+Digit3=0xA8` (CBM+£ — the more common operation)
- `Ctrl+Shift+Digit3=0xA9` (Shift+£ — surrogate using Ctrl as a disambiguator)

Or, if the symbolic engine can handle text+modifier: `Alt+£=0xA8` for CBM+£ and `Shift+Alt+£=0xA9` for Shift+£ — which maps perfectly to the C64 model (CBM+key and Shift+key).

**Fix:** Prefer the text+modifier form if supported. Otherwise use the Ctrl-based surrogate. Add a comment in the keymap file documenting the rationale.

**Affected file:** `symbolic_uk.c64keymap.ini`.

---

## 7. FR: Dead-Key Composed Character Normalization

**Status:** Partially fixed.

Current state: `é`, `è`, `ê`, `à`, `ù`, `ç` and uppercase forms are normalized. Verified correct.

Missing: `fr(basic)` exposes `dead_circumflex` and `dead_diaeresis` on `AD11`. The following composed outputs are reachable through normal French typing but unmapped.

**Fix:** Add these normalization entries to `symbolic_fr.c64keymap.ini`:

```ini
# Dead-key composed characters (circumflex via dead_circumflex + vowel)
â=0x41
î=0x49
ô=0x4F
û=0x55
Â=0xC1
Î=0xC9
Ô=0xCF
Û=0xD5

# Dead-key composed characters (diaeresis via dead_diaeresis + vowel)
ä=0x41
ë=0x45
ï=0x49
ö=0x4F
ü=0x55
ÿ=0x59
Ä=0xC1
Ë=0xC5
Ï=0xC9
Ö=0xCF
Ü=0xD5
Ÿ=0xD9
```

All normalize to the PETSCII base letter, following the established pattern. Characters already present in the file (like `é=0x45`) will not conflict since they map to the same value.

The `dead_grave` and `dead_acute` compose paths can also produce `á`, `í`, `ó`, `ú`, `ì`, `ò` on FR. These are uncommon in French but a user might reach them. Add for completeness:

```ini
# Less common composed characters (reachable via dead_grave / dead_acute)
á=0x41
í=0x49
ó=0x4F
ú=0x55
ì=0x49
ò=0x4F
Á=0xC1
Í=0xC9
Ó=0xCF
Ú=0xD5
Ì=0xC9
Ò=0xCF
```

**Affected file:** `symbolic_fr.c64keymap.ini`.

---

## 8. IT: Missing ç/Ç Normalization

**Status:** Unfixed. Verified absent from actual file.

`it(basic)` exposes `ccedilla` as Shift+AC10 (Shift+ograve key). Neither `ç` nor `Ç` appears in the Italian keymap.

**Fix:** Add `ç=0x43` and `Ç=0xC3` to `symbolic_it.c64keymap.ini`.

Also, IT has `dead_circumflex` (AE12 level 4), `dead_diaeresis` (AB09 level 4), `dead_acute` (AB08 level 3), `dead_tilde` (AE03 level 4), and `dead_grave` (BKSL level 3). For completeness, add the same composed character set as FR §7 (the vowel normalizations are identical). Italian users can compose `à`, `è`, `é`, `ì`, `ò`, `ù` directly, but circumflex/diaeresis forms like `â`, `ê`, `ë`, `ï` are reachable and should not produce silent failures.

**Affected file:** `symbolic_it.c64keymap.ini`.

---

## 9. NL: Dead-Key Composed Character Normalization

**Status:** Unfixed. The file contains zero locale-specific additions.

**Policy decision (made):** All composed Latin characters reachable through `nl(basic)` dead keys and direct AltGr outputs normalize to their PETSCII base letter. This is consistent with DE, FR, and IT. Silent drops are unacceptable — a user pressing a key must see *something* appear.

**Fix:** Add the following to `symbolic_nl.c64keymap.ini`:

```ini
# Locale-specific additions (differences vs Symbolic US)
#
# Policy: all accented Latin characters reachable through nl(basic)
# dead keys or AltGr combinations normalize to their PETSCII base
# letter (lowercase) or shifted base letter (uppercase).

# Diaeresis (dead_diaeresis on AD11 base — very common in Dutch)
ä=0x41
ë=0x45
ï=0x49
ö=0x4F
ü=0x55
Ä=0xC1
Ë=0xC5
Ï=0xC9
Ö=0xCF
Ü=0xD5

# Circumflex (dead_circumflex on AD11 shift)
â=0x41
ê=0x45
î=0x49
ô=0x4F
û=0x55
Â=0xC1
Ê=0xC5
Î=0xC9
Ô=0xCF
Û=0xD5

# Acute (dead_acute on AC11 base / AC10 level 3)
á=0x41
é=0x45
í=0x49
ó=0x4F
ú=0x55
ý=0x59
Á=0xC1
É=0xC5
Í=0xC9
Ó=0xCF
Ú=0xD5
Ý=0xD9

# Grave (dead_grave on AC11 shift / BKSL level 3)
à=0x41
è=0x45
ì=0x49
ò=0x4F
ù=0x55
À=0xC1
È=0xC5
Ì=0xC9
Ò=0xCF
Ù=0xD5

# Tilde (dead_tilde on AE12 shift / AD12 level 3)
ã=0x41
ñ=0x4E
õ=0x4F
Ã=0xC1
Ñ=0xCE
Õ=0xCF

# Cedilla (dead_cedilla on AE12 level 3)
ç=0x43
Ç=0xC3

# Direct AltGr outputs (level 3/4 on nl basic)
ÿ=0x59
Ÿ=0xD9
```

**Affected file:** `symbolic_nl.c64keymap.ini`.

---

## 10. Positional: IntlBackslash Entry

**Status:** Unfixed. Verified absent from actual file.

**Decision:** `positional_us` targets US ANSI 101-key keyboards as its primary audience. However, many users have ISO 102-key keyboards (common in Europe), and the guiding principle requires all C64 keys to be reachable. The `IntlBackslash` key should not be silently ignored.

On the C64, there is no direct physical equivalent for the ISO extra key. The most useful mapping is to make it a duplicate of an otherwise hard-to-reach C64 key. The C64 `←` (left arrow, `0x5F`) is on the `Backquote` position, which is sometimes awkward on ISO keyboards. Alternatively, it could map to `\` which the C64 doesn't have at all.

**Decision:** Map `IntlBackslash` to `0x5F` (C64 `←`), providing an alternative way to type the left arrow. This is the least surprising choice — the key is adjacent to the Z row and doesn't duplicate anything already easy to reach.

**Fix:** Add `IntlBackslash=0x5F` and `Shift+IntlBackslash=0x5F` (no distinct shifted output for `←`) to `positional_us.c64keymap.ini`. Add a comment noting this is an ISO 102-key convenience duplicate.

**Affected file:** `positional_us.c64keymap.ini`.

---

## 11. Testing

### 11a. XKB-Derived Verification Fixtures

Generate locale fixtures from XKB `basic` sections. Assert that every locale-specific symbolic addition corresponds to a real XKB output. Add a negative check that no two entries in the same file map different key inputs to the same PETSCII code unintentionally (duplicate values are fine when they're deliberate normalizations, like `ä` and `â` both mapping to `0x41`).

### 11b. UTF-8 Runtime Regression Tests

Add direct tests for multi-byte `event->text` values reaching `c64_keymap_convert` unchanged. Minimum coverage: `ä`, `ö`, `ü`, `é`, `è`, `à`, `ù`, `ç`, `ì`, `ò`, `£`, `ñ`, `ë`, `ï`. Verify that the UTF-8-aware printability check from §5 classifies all of these as printable.

### 11c. Dead-Key Policy Tests

Encode the normalization policy from §7/§9 in tests. Exercise composed text after dead-key resolution (not the dead-key keypress events themselves). Every entry in the locale-specific sections of FR, IT, NL, and DE must have a corresponding test asserting the expected PETSCII output.

### 11d. Positional CBM Consistency Check

Add a test that verifies each `Alt+<code>=<PETSCII>` entry in the positional map against the unmodified `<code>=<base PETSCII>` entry, confirming the PETSCII CBM value corresponds to the C64 key at that physical position. This would have caught §1.

### 11e. Full C64 Character Reachability Test

For each keymap file, verify that every PETSCII code in the printable range (`0x20–0x7E`), the shifted letter range (`0xC1–0xDA`), the shifted punctuation range (`0xA9`, `0xBA`, `0xDB`, `0xDD`, `0xDE`), and the CBM graphics range (`0xA1–0xA8`, `0xAB–0xAF`, `0xB1–0xB9`, `0xBB–0xBF`, `0xDC`, `0xDF`, `0xE2`, `0xE5`) is reachable by at least one key combination in that file. Flag any unreachable codes.

---

## 12. Documentation

- Add a comment block at the top of `positional_us.c64keymap.ini` stating it targets US ANSI 101-key keyboards, with `IntlBackslash` as a convenience entry for ISO 102-key users.
- Add a comment block at the top of each symbolic locale file documenting the normalization policy: "Accented Latin characters normalize to their PETSCII base letter. This means `ä` → `A`, `é` → `E`, etc."
- Document the `Backspace=0x5C` surrogate (host Backspace → C64 `£`) in `positional_us.c64keymap.ini` with a comment explaining the rationale.
- Document the UK `Shift+£` surrogate (§6) with a comment in `symbolic_uk.c64keymap.ini`.
- Document `PageUp`, `PageDown`, `End`, `CapsLock`, `Escape`, `Pause` as policy mappings in `positional_us.c64keymap.ini` with a comment block listing each one and its C64 function equivalent.
