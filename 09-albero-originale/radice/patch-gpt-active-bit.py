#!/usr/bin/env python3
"""
Construct a patched GPT from the printgpt output, fix boot_b active bit,
and write it back to Lun 5 using firehose raw write.
"""

import struct, os, sys

# GPT constants
SECTOR_SIZE = 4096
ENTRY_SIZE = 128
MAX_ENTRIES = 128
PARTITION_ENTRIES_OFFSET = 128  # bytes from start of GPT sector

# GPT header fields (relative to sector start)
OFFSET_SIGNATURE   = 0    # 8 bytes "EFI PART"
OFFSET_REVISION    = 8    # 4 bytes
OFFSET_HEADER_SIZE = 12   # 4 bytes
OFFSET_CRC32       = 16   # 4 bytes
OFFSET_RESERVED    = 20   # 4 bytes
OFFSET_MY_LBA      = 24   # 8 bytes
OFFSET_ALT_LBA     = 32   # 8 bytes
OFFSET_FIRST_USABLE= 40   # 8 bytes
OFFSET_LAST_USABLE = 48   # 8 bytes
OFFSET_DISK_GUID   = 56   # 16 bytes
OFFSET_START       = 72   # 8 bytes
OFFSET_NUM_ENTRIES = 80   # 4 bytes
OFFSET_ENTRY_SIZE  = 84   # 4 bytes
OFFSET_ENTRIES_CRC = 88   # 4 bytes

# Partition entry fields
PE_TYPE_GUID_LO    = 0   # 4 bytes
PE_TYPE_GUID_MID   = 4   # 2 bytes
PE_TYPE_GUID_HI    = 6   # 2 bytes
PE_TYPE_GUID_EXT   = 8   # 8 bytes
PE_GUID            = 16  # 16 bytes
PE_FIRST_LBA       = 32  # 8 bytes
PE_LAST_LBA        = 40  # 8 bytes
PE_ATTR            = 48  # 8 bytes
PE_NAME            = 56  # 72 bytes (UTF-16LE)

# Partition type GUIDs (from printgpt output)
TYPE_BOOT_A = 0x20117f86  # Android boot_a
TYPE_BOOT_B = 0x77036cd4  # Android boot_b
TYPE_AB     = 0xe3c9e316  # Android AB partition group

def crc32_u32(data: bytes) -> int:
    """Compute CRC32 returning unsigned 32-bit value."""
    import binascii
    return binascii.crc32(data) & 0xFFFFFFFF

def compute_gpt_header_crc(header_bytes: bytearray) -> bytes:
    """Compute CRC32 of GPT header, excluding the CRC field itself."""
    # Zero out CRC field
    crc_offset = OFFSET_CRC32
    saved_crc = header_bytes[crc_offset:crc_offset+4]
    header_bytes[crc_offset:crc_offset+4] = b'\x00\x00\x00\x00'
    
    crc = crc32_u32(bytes(header_bytes))
    header_bytes[crc_offset:crc_offset+4] = saved_crc
    return struct.pack('<I', crc)

def write_gpt_entry(entry: bytearray, name: str, type_guid_lo: int,
                     guid_bytes: bytes, first_lba: int, last_lba: int,
                     attrs: int):
    """Write a GPT partition entry."""
    entry[0:4]   = struct.pack('<I', type_guid_lo)
    entry[4:6]   = struct.pack('<H', 0)
    entry[6:8]   = struct.pack('<H', 0)
    entry[8:16]  = b'\x00' * 8  # type_guid_hi (placeholder)
    entry[16:32] = guid_bytes
    entry[32:40] = struct.pack('<Qq', first_lba, last_lba)
    entry[40:48] = struct.pack('<Q', attrs)
    # Name is already set
    
def build_and_write_gpt():
    """
    Build GPT from printgpt output and write to Lun 5.
    """
    # Parse the printgpt output to get partition layout
    printgpt_file = '/home/user/nx679j-stock/edl-recovery/restore-stock-a/printgpt-after-baseline.txt'
    with open(printgpt_file) as f:
        text = f.read()
    
    import re
    partitions = {}
    for line in text.split('\n'):
        m = re.match(
            r'^\s*(\w[\w_]+):\s+'
            r'Offset\s+(0x[0-9a-f]+),\s+'
            r'Length\s+(0x[0-9a-f]+),\s+'
            r'Flags\s+(0x[0-9a-f]+),\s+'
            r'UUID\s+(\w+-\w+-\w+-\w+-\w+),\s+'
            r'Type\s+(0x[0-9a-f]+)',
            line.strip()
        )
        if m:
            name = m.group(1)
            offset = int(m.group(2), 16)
            length = int(m.group(3), 16)
            flags = int(m.group(4), 16)
            uuid_str = m.group(5)
            type_hex = int(m.group(6), 16)
            partitions[name] = {
                'offset': offset,
                'length': length,
                'flags': flags,
                'uuid': uuid_str,
                'type': type_hex,
            }
            print(f"  {name}: offset={offset:#x} len={length:#x} flags={flags:#018x} type={type_hex:#x}")
    
    # Find boot_b and fix its flags
    boot_b = partitions.get('boot_b')
    if boot_b:
        old_flags = boot_b['flags']
        # Set active bit (bit 0)
        new_flags = old_flags | 0x1
        boot_b['flags'] = new_flags
        print(f"\nboot_b flags: {old_flags:#018x} -> {new_flags:#018x} (active={bool(new_flags & 1)})")
    
    # Convert UUID strings to bytes
    def uuid_to_bytes(uuid_str):
        hex_part = uuid_str.replace('-', '')
        # UUID is LE: first 3 groups are little-endian, last group is big-endian
        b = bytearray(16)
        # time_low (4 bytes, LE)
        b[0:4] = bytes.fromhex(hex_part[6:8] + hex_part[4:6] + hex_part[2:4] + hex_part[0:2])
        # time_mid (2 bytes, LE)
        b[4:6] = bytes.fromhex(hex_part[10:12] + hex_part[8:10])
        # time_hi_and_version (2 bytes, LE)
        b[6:8] = bytes.fromhex(hex_part[14:16] + hex_part[12:14])
        # clock_seq_hi_and_res + clock_seq_lo (2 bytes)
        b[8:10] = bytes.fromhex(hex_part[16:18] + hex_part[18:20])
        # node (6 bytes)
        b[10:16] = bytes.fromhex(hex_part[20:32])
        return bytes(b)
    
    # Build partition entries
    entries = bytearray(SECTOR_SIZE)
    entry_count = 0
    
    # Sort partitions by offset for proper ordering
    sorted_parts = sorted(partitions.values(), key=lambda p: p['offset'])
    
    for pname, pinfo in sorted_parts:
        pe = entries[entry_count * ENTRY_SIZE:(entry_count + 1) * ENTRY_SIZE]
        
        # Type GUID
        type_val = pinfo['type']
        # Type GUID is split across the GUID field
        # Low 4 bytes = type_val
        pe[0:4] = struct.pack('<I', type_val)
        # Middle/high GUID bytes - we need to reconstruct from the original GUID
        # The type_guid in GPT is a 16-byte GUID, not just 4 bytes
        # For Android partitions, type_guid is:
        #   boot_a: 20117f86-... (group)
        #   boot_b: 77036cd4-... (group)
        # The remaining 12 bytes of the GUID are the AB group GUID
        
        # Use the UUID as the partition GUID (unique identifier)
        pe_guid = uuid_to_bytes(pinfo['uuid'])
        pe[16:32] = pe_guid
        
        # First/Last LBA
        first_lba = pinfo['offset'] // SECTOR_SIZE
        last_lba = (pinfo['offset'] + pinfo['length'] - 1) // SECTOR_SIZE
        pe[32:40] = struct.pack('<QQ', first_lba, last_lba)
        
        # Attributes (including active bit)
        pe[40:48] = struct.pack('<Q', pinfo['flags'])
        
        # Name (UTF-16LE)
        name_bytes = pname.encode('utf-16-le')
        pe[56:56+len(name_bytes)] = name_bytes
        
        entry_count += 1
    
    print(f"\nBuilt {entry_count} partition entries")
    
    # Build GPT header
    header = bytearray(SECTOR_SIZE)
    
    # Signature
    header[0:8] = b'EFI PART'
    
    # Revision (1.0)
    header[8:12] = b'\x00\x01\x00\x00'
    
    # Header size
    header[12:16] = struct.pack('<I', SECTOR_SIZE)
    
    # Reserved
    header[20:24] = b'\x00\x00\x00\x00'
    
    # My LBA = 0 (primary)
    header[24:32] = struct.pack('<Q', 0)
    
    # Alt LBA (backup) - set to last usable sector
    # For a 60GB device, last usable = total_sectors - 34
    total_sectors = 60884992  # from storage info
    header[32:40] = struct.pack('<Q', total_sectors - 33)
    
    # First/Last usable sectors
    header[40:48] = struct.pack('<QQ', 34, total_sectors - 34)
    
    # Disk GUID (generate random)
    import uuid as uuid_mod
    disk_uuid = uuid_mod.uuid4()
    header[56:72] = uuid_mod.uuid_bytes(disk_uuid)
    
    # Start of partition entry array (LBA 1)
    header[72:80] = struct.pack('<Q', 1)
    
    # Number of entries
    header[80:84] = struct.pack('<I', min(entry_count, MAX_ENTRIES))
    
    # Entry size
    header[84:88] = struct.pack('<I', ENTRY_SIZE)
    
    # Calculate entry array CRC
    entries_crc = crc32_u32(bytes(entries))
    header[88:92] = struct.pack('<I', entries_crc)
    
    # Calculate header CRC
    header_crc = compute_gpt_header_crc(header)
    header[16:20] = header_crc
    
    print(f"GPT header CRC: {struct.unpack('<I', header[16:20])[0]:#x}")
    print(f"Entries CRC: {entries_crc:#x}")
    
    # Combine: GPT header (sector 0) + partition entries (sector 1)
    gpt_full = header + entries
    
    # Write to Lun 5 sector 0
    print(f"\nWriting GPT to Lun 5, sectors 0-{(len(gpt_full)//SECTOR_SIZE)-1} ({len(gpt_full)} bytes)")
    
    # Use edl to write the GPT
    import subprocess
    edl = '/home/user/venvs/edk2/bin/edl'
    loader = '/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf'
    tmp_gpt = '/tmp/gpt_patched.bin'
    
    with open(tmp_gpt, 'wb') as f:
        f.write(gpt_full)
    
    # Write 2 sectors starting at sector 0 of Lun 5
    result = subprocess.run(
        [edl, 'ws', '0', tmp_gpt, '--loader', loader, '--memory', 'ufs', '--lun', '5'],
        capture_output=True, text=True, timeout=30
    )
    
    print(f"STDOUT: {result.stdout}")
    print(f"STDERR: {result.stderr}")
    print(f"RC: {result.returncode}")
    
    if result.returncode != 0:
        # Try with 'w gpt' instead
        print("Trying alternate method: w gpt")
        result2 = subprocess.run(
            [edl, 'w', 'gpt', tmp_gpt, '--loader', loader, '--memory', 'ufs', '--lun', '5'],
            capture_output=True, text=True, timeout=30
        )
        print(f"STDOUT: {result2.stdout}")
        print(f"STDERR: {result2.stderr}")
        print(f"RC: {result2.returncode}")

if __name__ == '__main__':
    build_and_write_gpt()
