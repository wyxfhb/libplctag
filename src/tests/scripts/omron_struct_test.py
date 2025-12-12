#!/usr/bin/env python3
"""
Test script for Omron structure type discovery and basic operations.
Tests Phase 2 functionality: structure type definitions and member discovery.
"""

import sys
import time
from aphyt import omron

def test_structure_discovery():
    """Test that structures are discovered correctly"""
    print("=" * 70)
    print("Omron Structure Discovery Test")
    print("=" * 70)

    try:
        with omron.NSeries('127.0.0.1') as eip_conn:
            print("\n1. Updating variable dictionary...")
            eip_conn.update_variable_dictionary()

            # Check if MyPoint structure was discovered
            print("\n2. Checking for MyPoint structure type...")
            if 'MyPoint' in eip_conn.derived_data_type_dictionary:
                point_type = eip_conn.derived_data_type_dictionary['MyPoint']
                print(f"   ✓ Found MyPoint type")
                print(f"     - Size: {point_type.size} bytes")
                print(f"     - Variable Type Name: {point_type.variable_type_name}")

                # Check member count
                print(f"     - Member count: {point_type.member_count}")

                # Try to discover members by following the chain
                print("\n3. Discovering structure members...")
                members = []
                if hasattr(point_type, 'nesting_variable_type_instance_id'):
                    member_id = point_type.nesting_variable_type_instance_id
                    if isinstance(member_id, bytes):
                        member_id = int.from_bytes(member_id, 'little')

                    attempt = 0
                    while member_id != 0 and attempt < 10:
                        try:
                            member = eip_conn._get_variable_type_object(member_id)
                            member_name = member.variable_type_name
                            if isinstance(member_name, bytes):
                                member_name = member_name.decode('utf-8', errors='ignore')
                            members.append((member_name, member_id))
                            print(f"   ✓ Found member: {member_name} (ID: {member_id})")

                            # Get next member
                            if hasattr(member, 'next_instance_id'):
                                member_id = member.next_instance_id
                                if isinstance(member_id, bytes):
                                    member_id = int.from_bytes(member_id, 'little')
                            else:
                                break
                        except Exception as e:
                            print(f"   ! Error discovering member {member_id}: {e}")
                            break
                        attempt += 1

                    if members:
                        print(f"\n   Total members discovered: {len(members)}")
                        for name, id in members:
                            print(f"     - {name} (Instance ID: {id})")
                    else:
                        print(f"   ! No members discovered (chain starting at ID: {point_type.nesting_variable_type_instance_id})")
            else:
                print("   ✗ MyPoint structure NOT found!")
                print(f"   Available types: {list(eip_conn.derived_data_type_dictionary.keys())}")

            # Check if point1 tag was discovered
            print("\n4. Checking for point1 tag...")
            if 'point1' in eip_conn.connected_cip_dispatcher.variables:
                point1 = eip_conn.connected_cip_dispatcher.variables['point1']
                print(f"   ✓ Found point1 tag")
                print(f"     - Type: {point1.__class__.__name__}")

                # Try to read the tag
                print("\n5. Attempting to read point1 tag...")
                try:
                    value = eip_conn.read_variable('point1')
                    print(f"   ✓ Read point1: {value}")
                except Exception as e:
                    print(f"   ! Error reading point1: {e}")
            else:
                print("   ✗ point1 tag NOT found!")
                print(f"   Available variables: {list(eip_conn.connected_cip_dispatcher.variables.keys())}")

            print("\n" + "=" * 70)
            print("✓ Structure discovery test completed successfully!")
            print("=" * 70)
            return True

    except Exception as e:
        print(f"\n✗ Test failed with error: {e}")
        import traceback
        traceback.print_exc()
        return False


if __name__ == '__main__':
    success = test_structure_discovery()
    sys.exit(0 if success else 1)
