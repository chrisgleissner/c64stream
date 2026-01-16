#!/usr/bin/env python3

"""Generate a canonical 00-FF PETSCII table CSV.

Benefits:
- Keeps doc/c64/c64-petscii-codes.csv reproducible (always 256 rows, stable columns).
- Preserves your existing key mapping fields (c64_key/key_modifier/notes) as source-of-truth.
- Normalizes only the modifier token values (left/right -> cbm/shift).
- Backfills missing meaning values from repo sources (src/c64-keyboard.c + doc/c64/c64-character-set.csv).

When to run:
- After editing doc/c64/c64-petscii-codes.csv (to repair/normalize structure).
- After changing src/c64-keyboard.c or doc/c64/c64-character-set.csv (to refresh derived meanings).

What to do with the output:
- Validate: generate to a temp file and diff against the checked-in CSV.
    python3 tools/generate-c64-petscii-codes-csv.py > /tmp/c64-petscii-codes.csv
    diff -u doc/c64/c64-petscii-codes.csv /tmp/c64-petscii-codes.csv
- Regenerate: overwrite the checked-in CSV (then review the diff).
    python3 tools/generate-c64-petscii-codes-csv.py > doc/c64/c64-petscii-codes.csv
"""

import csv
import re
import sys
from pathlib import Path


def load_existing(path: Path) -> dict[int, dict[str, str]]:
    existing: dict[int, dict[str, str]] = {}
    with path.open(newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            hx = (row.get('petscii_hex') or row.get('petscii_code_hex') or '').strip().upper()
            if not hx:
                continue
            existing[int(hx, 16)] = {
                'petscii_hex': hx,
                'c64_key': (row.get('c64_key') or '').strip(),
                'key_modifier': (row.get('key_modifier') or '').strip(),
                'meaning': (row.get('meaning') or '').strip(),
                'notes': (row.get('notes') or '').strip(),
            }
    return existing


def load_symbolic_keys_from_c(path: Path) -> dict[int, str]:
    text = path.read_text(encoding='utf-8')
    symbolic: dict[int, str] = {}

    # Matches entries like: {"c64:CURSOR_UP", 0x91}
    for match in re.finditer(r'\{"c64:([A-Z0-9_]+)"\s*,\s*(0x[0-9A-Fa-f]{2})\}', text):
        name = match.group(1)
        code = int(match.group(2), 16)
        symbolic[code] = name

    return symbolic


def load_charset_best(path: Path) -> dict[int, dict[str, str]]:
    best: dict[int, dict[str, str]] = {}

    with path.open(newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            petscii_code = (row.get('petscii_code') or '').strip()
            screen_code = (row.get('screen_code') or '').strip()
            ch = (row.get('char') or '').strip()
            uni = (row.get('unicode') or '').strip()
            name = (row.get('name') or '').strip()

            if not petscii_code.startswith('$'):
                continue

            try:
                petscii_val = int(petscii_code[1:], 16)
            except ValueError:
                continue

            if not (ch or name):
                continue

            # Prefer "normal" glyphs (screen_code < 0x80) and avoid "(reverse)" names.
            score = 0
            if screen_code.startswith('$'):
                try:
                    sc_val = int(screen_code[1:], 16)
                    if sc_val < 0x80:
                        score += 10
                except ValueError:
                    pass

            if name and '(reverse)' not in name:
                score += 3
            if ch:
                score += 2
            if name:
                score += 1

            cur = best.get(petscii_val)
            if cur is None or score > int(cur['score']):
                best[petscii_val] = {
                    'score': str(score),
                    'char': ch,
                    'unicode': uni,
                    'name': name,
                }

    return best


def symbolic_to_meaning(symbolic: str) -> str:
    color_map = {
        'COLOR_BLACK': (0, 'black'),
        'COLOR_WHITE': (1, 'white'),
        'COLOR_RED': (2, 'red'),
        'COLOR_CYAN': (3, 'cyan'),
        'COLOR_PURPLE': (4, 'purple'),
        'COLOR_GREEN': (5, 'green'),
        'COLOR_BLUE': (6, 'blue'),
        'COLOR_YELLOW': (7, 'yellow'),
        'COLOR_ORANGE': (8, 'orange'),
        'COLOR_BROWN': (9, 'brown'),
        'COLOR_LT_RED': (10, 'lt. red'),
        'COLOR_DK_GREY': (11, 'dk. grey'),
        'COLOR_GREY': (12, 'md. grey'),
        'COLOR_LT_GREEN': (13, 'light green'),
        'COLOR_LT_BLUE': (14, 'light blue'),
        'COLOR_LT_GREY': (15, 'light gray'),
    }

    if symbolic.startswith('COLOR_'):
        num_label = color_map.get(symbolic)
        if not num_label:
            return ''
        num, label = num_label
        return f'color no {num} ({label})'

    if symbolic == 'RETURN':
        return 'carriage return'
    if symbolic == 'RUNSTOP':
        return 'stop'
    if symbolic in ('HOME', 'CLR_HOME'):
        return 'cursor home'
    if symbolic == 'CLEAR':
        return 'clear screen'
    if symbolic in ('DELETE', 'INSTDEL'):
        return 'delete'
    if symbolic == 'INSERT':
        return 'insert'
    if symbolic == 'CURSOR_UP':
        return 'cursor up'
    if symbolic == 'CURSOR_DOWN':
        return 'cursor down'
    if symbolic == 'CURSOR_LEFT':
        return 'cursor left'
    if symbolic == 'CURSOR_RIGHT':
        return 'cursor right'
    if symbolic == 'RVS_ON':
        return 'reverse'
    if symbolic == 'RVS_OFF':
        return 'rvs off'
    if symbolic == 'TAB':
        return 'tab'
    if symbolic == 'SHIFT_SPACE':
        return 'shift space'
    if symbolic == 'SHIFT_RETURN':
        return 'shift return'

    if re.fullmatch(r'F[1-8]', symbolic):
        return symbolic

    return symbolic.lower().replace('_', ' ')


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]

    codes_path = repo_root / 'doc/c64/c64-petscii-codes.csv'
    charset_path = repo_root / 'doc/c64/c64-character-set.csv'
    keyboard_path = repo_root / 'src/c64-keyboard.c'

    existing = load_existing(codes_path)
    symbolic_keys = load_symbolic_keys_from_c(keyboard_path)
    charset_best = load_charset_best(charset_path)

    writer = csv.DictWriter(
        sys.stdout,
        fieldnames=['petscii_hex', 'c64_key', 'key_modifier', 'meaning', 'notes'],
        lineterminator='\n',
    )
    writer.writeheader()

    for b in range(0x100):
        row = existing.get(b)
        if row is None:
            row = {
                'petscii_hex': f'{b:02X}',
                'c64_key': '',
                'key_modifier': '',
                'meaning': '',
                'notes': '',
            }

        # Normalize modifier column only
        if row['key_modifier'] == 'left':
            row['key_modifier'] = 'cbm'
        elif row['key_modifier'] == 'right':
            row['key_modifier'] = 'shift'

        # Fill meaning only when missing
        if not row['meaning']:
            sym = symbolic_keys.get(b)
            if sym:
                row['meaning'] = symbolic_to_meaning(sym)

        if not row['meaning']:
            best = charset_best.get(b)
            if best:
                row['meaning'] = best.get('char') or best.get('name') or ''

                # Keep diffs small: only add a note when we used a name (not a visible glyph)
                if not row['notes'] and not best.get('char') and best.get('unicode'):
                    row['notes'] = f"unicode={best['unicode']}"

        writer.writerow(row)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
