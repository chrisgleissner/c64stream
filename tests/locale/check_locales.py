#!/usr/bin/env python3
"""Checks that every locale in data/locale defines the same keys as en-US.ini
and uses the same printf placeholders in each text. The plugin substitutes
the file name into a translated status line, so a translation with a missing
or extra "%s" would show a wrong text."""

import pathlib
import re
import sys

PLACEHOLDER = re.compile(r"%[-+ #0-9.]*[a-zA-Z%]")


def load(path):
    entries = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith(("#", ";", "[")):
            continue
        key, sep, value = line.partition("=")
        if not sep or not value.startswith('"') or not value.endswith('"') or len(value) < 2:
            raise ValueError(f"{path.name}:{number}: not a key=\"text\" line: {line}")
        key = key.strip()
        if key in entries:
            raise ValueError(f"{path.name}:{number}: duplicate key {key}")
        entries[key] = value[1:-1]
    return entries


def main():
    directory = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "data/locale")
    reference = load(directory / "en-US.ini")
    problems = []
    for path in sorted(directory.glob("*.ini")):
        try:
            entries = load(path)
        except ValueError as error:
            problems.append(str(error))
            continue
        for key in sorted(reference.keys() - entries.keys()):
            problems.append(f"{path.name}: missing {key}")
        for key in sorted(entries.keys() - reference.keys()):
            problems.append(f"{path.name}: unknown {key}")
        for key in sorted(reference.keys() & entries.keys()):
            expected = PLACEHOLDER.findall(reference[key])
            found = PLACEHOLDER.findall(entries[key])
            if expected != found:
                problems.append(f"{path.name}: {key} has placeholders {found}, en-US has {expected}")
    for problem in problems:
        print(problem)
    print(f"{len(list(directory.glob('*.ini')))} locales, {len(reference)} keys, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
