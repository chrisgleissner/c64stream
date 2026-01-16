#!/usr/bin/env python3
"""
Test the keymap parser
"""

import os
import sys

def test_keymap_parser():
    """Test that we can load and parse a keymap"""

    # Build path to keymap (tests/e2e is 2 levels deep from repo root)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(os.path.dirname(script_dir))
    keymap_path = os.path.join(repo_root, "data/keymaps/symbolic_us.c64keymap.ini")

    assert os.path.exists(keymap_path), f"Keymap file not found: {keymap_path}"

    print(f"✓ Found keymap: {keymap_path}")

    # Verify keymap format
    with open(keymap_path) as f:
        lines = f.readlines()

    meta_found = False
    map_found = False
    entries = 0

    for line in lines:
        line = line.strip()
        if line == "[meta]":
            meta_found = True
        elif line == "[map]":
            map_found = True
        elif "=" in line and not line.startswith("#") and not line.startswith("["):
            entries += 1

    print(f"  [meta] section: {'✓' if meta_found else '❌'}")
    print(f"  [map] section: {'✓' if map_found else '❌'}")
    print(f"  Entries: {entries}")

    assert meta_found, "Keymap missing [meta] section"
    assert map_found, "Keymap missing [map] section"
    assert entries > 0, "Keymap has no entries"

    print("✅ Keymap format valid")

if __name__ == '__main__':
    try:
        test_keymap_parser()
    except AssertionError:
        sys.exit(1)
    sys.exit(0)
