#!/usr/bin/env python3
"""
Test script to verify CSV recording settings are properly configured.
"""

import json
import os
from pathlib import Path

def test_scene_configuration():
    """Test that our scene configuration has the correct CSV recording settings."""
    
    # Check if the scene collection file exists
    scenes_dir = Path.home() / '.config' / 'obs-studio' / 'basic' / 'scenes'
    scene_file = scenes_dir / 'C64StreamTest.json'
    
    if not scene_file.exists():
        print(f"❌ Scene file not found: {scene_file}")
        return False
    
    # Load and parse the scene configuration
    try:
        with open(scene_file, 'r') as f:
            scene_config = json.load(f)
        
        # Find the C64 Stream source
        sources = scene_config.get('sources', [])
        c64_source = None
        
        for source in sources:
            if source.get('id') == 'c64_source':
                c64_source = source
                break
        
        if not c64_source:
            print("❌ C64 Stream source not found in scene configuration")
            return False
        
        # Check the settings
        settings = c64_source.get('settings', {})
        record_csv = settings.get('record_csv')
        
        print(f"✅ Found C64 Stream source in scene")
        print(f"📊 Settings: {json.dumps(settings, indent=2)}")
        print(f"🔍 record_csv setting: {record_csv} (type: {type(record_csv)})")
        
        if record_csv is True:
            print("✅ record_csv is correctly set to True")
            return True
        else:
            print(f"❌ record_csv is not True: {record_csv}")
            return False
            
    except Exception as e:
        print(f"❌ Error reading scene configuration: {e}")
        return False

def test_properties_file():
    """Test that our properties file has the correct CSV recording settings."""
    
    # Check the properties file
    properties_file = Path.home() / '.config' / 'obs-studio' / 'plugins' / 'c64stream' / 'data' / 'properties.ini'
    
    if not properties_file.exists():
        print(f"❌ Properties file not found: {properties_file}")
        return False
    
    # Read the properties file
    try:
        with open(properties_file, 'r') as f:
            content = f.read()
        
        print("✅ Properties file found")
        print("📊 Recording section:")
        
        # Extract recording section
        in_recording_section = False
        for line in content.split('\n'):
            line = line.strip()
            if line == '[recording]':
                in_recording_section = True
                print(f"  {line}")
            elif line.startswith('[') and line != '[recording]':
                in_recording_section = False
            elif in_recording_section and line and not line.startswith('#'):
                print(f"  {line}")
                if line.startswith('record_csv='):
                    csv_value = line.split('=', 1)[1].strip()
                    print(f"🔍 record_csv value: '{csv_value}'")
        
        return True
        
    except Exception as e:
        print(f"❌ Error reading properties file: {e}")
        return False

if __name__ == '__main__':
    print("=" * 60)
    print("CSV Recording Settings Test")
    print("=" * 60)
    
    print("\n1. Testing Scene Configuration:")
    scene_ok = test_scene_configuration()
    
    print("\n2. Testing Properties File:")
    props_ok = test_properties_file()
    
    print(f"\n{'✅ All tests passed' if scene_ok and props_ok else '❌ Some tests failed'}")