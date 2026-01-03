#!/usr/bin/env python3
"""
Test the keymap parser
"""

import os
import subprocess
import sys

def test_keymap_parser():
    """Test that we can load and parse a keymap"""

    # Build path to keymap (tests/e2e is 2 levels deep from repo root)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(os.path.dirname(script_dir))
    keymap_path = os.path.join(repo_root, "data/keymaps/symbolic_us.c64keymap.ini")

    if not os.path.exists(keymap_path):
        print(f"❌ Keymap file not found: {keymap_path}")
        return False

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

    if not meta_found or not map_found or entries == 0:
        print("❌ Keymap format invalid")
        return False

    print("✅ Keymap format valid")
    return True

if __name__ == '__main__':
    success = test_keymap_parser()
    sys.exit(0 if success else 1)
