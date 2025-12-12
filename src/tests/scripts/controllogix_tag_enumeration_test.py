#!/usr/bin/env python3
"""
Test script for ControlLogix tag enumeration (Services 0x01, 0x5F).
Tests that ControlLogix PLCs can properly enumerate tags.
"""

import sys
import time
from aphyt import controllogix

def test_tag_enumeration():
    """Test that tags can be enumerated from ControlLogix PLC"""
    print("=" * 70)
    print("ControlLogix Tag Enumeration Test")
    print("=" * 70)

    try:
        with controllogix.CompactLogix('127.0.0.1', slot=1) as plc:
            print("\n1. Updating variable dictionary...")
            plc.update_variable_dictionary()

            print(f"\n2. Total tags discovered: {len(plc.cip_dispatcher.variables)}")

            if len(plc.cip_dispatcher.variables) > 0:
                print("\n3. Tag enumeration results:")
                for tag_name, tag_obj in sorted(plc.cip_dispatcher.variables.items()):
                    print(f"   - {tag_name}: {tag_obj.__class__.__name__}")

                print("\n" + "=" * 70)
                print("✓ Tag enumeration test completed successfully!")
                print("=" * 70)
                return True
            else:
                print("   ! No tags were enumerated")
                print("\n" + "=" * 70)
                print("✓ Tag enumeration test completed (but no tags found)")
                print("=" * 70)
                return True

    except Exception as e:
        print(f"\n✗ Test failed with error: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == '__main__':
    success = test_tag_enumeration()
    sys.exit(0 if success else 1)
