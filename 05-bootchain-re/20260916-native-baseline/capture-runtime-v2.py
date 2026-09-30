#!/usr/bin/env python3
"""Read-only runtime and GPT acquisition, with nested shell quoting retained."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
import zlib

BASE = Path(__file__).resolve().parent
OUT = BASE / 'runtime-v2'
SERIAL = '0123456789ABCDEF'


def capture(name, argv, timeout=30):
    started = datetime.datetime.now().astimezone().isoformat()
    cp = subprocess.run(argv, capture_output=True, timeout=timeout, check=False)
    (OUT / name).write_bytes(cp.stdout)
    (OUT / (name + '.stderr')).write_bytes(cp.stderr)
    with (OUT / 'commands.jsonl').open('a') as log:
        log.write(json.dumps({'started': started, 'argv': argv,
                              'returncode': cp.returncode,
                              'stdout': name, 'bytes': len(cp.stdout)}) + '\n')
    if cp.returncode:
        raise RuntimeError(f'{name}: rc={cp.returncode}: {cp.stderr.decode(errors="replace")}')
    return cp.stdout


def remote(name, command, timeout=30):
    return capture(name, ['timeout', str(timeout), 'adb', '-s', SERIAL,
                          'exec-out', 'su -c ' + shlex.quote(command)], timeout + 5)


def read_blocks(name, disk, block_size, start, count):
    # Only the input of dd references the phone disk. stdout is saved locally.
    data = remote(name, f'exec dd if=/dev/block/{disk} bs={block_size} '
                  f'skip={start} count={count} 2>/dev/null')
    if len(data) != block_size * count:
        raise ValueError(f'{name}: short/contaminated read {len(data)}')
    return data


def gpt_header(raw):
    if raw[:8] != b'EFI PART':
        raise ValueError('GPT magic missing')
    revision, size, crc = struct.unpack_from('<III', raw, 8)
    if size < 92 or size > len(raw):
        raise ValueError('invalid GPT header size')
    copy = bytearray(raw[:size])
    copy[16:20] = bytes(4)
    if zlib.crc32(copy) != crc:
        raise ValueError('GPT header CRC mismatch')
    current, alternate, first, last = struct.unpack_from('<QQQQ', raw, 24)
    table_lba, entries, entry_size, table_crc = struct.unpack_from('<QIII', raw, 72)
    if not (1 <= entries <= 4096 and 128 <= entry_size <= 4096):
        raise ValueError('GPT entry dimensions outside acquisition bounds')
    return dict(revision=revision, header_size=size, header_crc=crc,
                current_lba=current, alternate_lba=alternate,
                first_usable=first, last_usable=last, table_lba=table_lba,
                entries=entries, entry_size=entry_size, table_crc=table_crc)


def acquire_gpt():
    result = []
    for disk in ('sda', 'sdb', 'sdc', 'sdd', 'sde', 'sdf'):
        geo = remote(f'{disk}-geometry.txt',
                     f'cat /sys/class/block/{disk}/queue/logical_block_size; '
                     f'cat /sys/class/block/{disk}/size').decode().split()
        sector, sectors512 = map(int, geo)
        total = sectors512 * 512 // sector
        if sector not in (512, 4096):
            raise ValueError('unsupported sector size')
        prefix = read_blocks(f'{disk}-mbr-primary-header.bin', disk, sector, 0, 2)
        primary = gpt_header(prefix[sector:])
        if primary['current_lba'] != 1 or primary['alternate_lba'] != total - 1:
            raise ValueError('GPT geometry mismatch')
        suffix = read_blocks(f'{disk}-backup-header.bin', disk, sector, total - 1, 1)
        backup = gpt_header(suffix)
        if backup['current_lba'] != total - 1 or backup['alternate_lba'] != 1:
            raise ValueError('backup GPT geometry mismatch')
        tables = []
        for label, hdr in (('primary', primary), ('backup', backup)):
            size = hdr['entries'] * hdr['entry_size']
            count = (size + sector - 1) // sector
            if hdr['table_lba'] + count > total:
                raise ValueError('GPT table beyond disk end')
            table = read_blocks(f'{disk}-{label}-entries.bin', disk, sector,
                                hdr['table_lba'], count)[:size]
            if zlib.crc32(table) != hdr['table_crc']:
                raise ValueError(f'{disk} {label}: entry CRC mismatch')
            tables.append(table)
        if tables[0] != tables[1]:
            raise ValueError(f'{disk}: primary/backup tables differ')
        partitions = []
        for i in range(primary['entries']):
            ent = tables[0][i * primary['entry_size']:(i + 1) * primary['entry_size']]
            if ent[:16] == bytes(16):
                continue
            first, last, attrs = struct.unpack_from('<QQQ', ent, 32)
            name = ent[56:128].decode('utf-16-le', errors='strict').split('\0', 1)[0]
            partitions.append(dict(index=i + 1, name=name, first_lba=first,
                                   last_lba=last, size_bytes=(last-first+1)*sector,
                                   attributes=f'0x{attrs:016x}'))
        row = dict(disk=disk, logical_sector_size=sector, sectors=total,
                   primary=primary, backup=backup, crc_valid=True,
                   tables_identical=True, partitions=partitions)
        result.append(row)
        (OUT / 'gpt.json').write_text(json.dumps(result, indent=2) + '\n')
        print(f'{disk}: GPT primary/backup CRC valid, {len(partitions)} partitions', flush=True)
    return result


def runtime():
    capture('host-usb.txt', ['timeout', '10', 'lsusb'])
    remote('identity.txt', 'id; uname -a; getprop ro.product.model; '
           'getprop ro.boot.slot_suffix; getprop ro.boot.dtb_idx; '
           'getprop ro.boot.dtbo_idx; getprop sys.boot_completed')
    remote('security.txt', 'getenforce; cat /proc/cmdline; '
           '[ ! -r /proc/bootconfig ] || cat /proc/bootconfig')
    remote('partitions.txt', 'ls -l /dev/block/by-name; cat /proc/partitions')
    remote('modules.txt', 'cat /proc/modules')
    remote('dmesg.txt', 'dmesg')
    remote('pstore-list.txt', 'ls -la /sys/fs/pstore')
    remote('usb.txt', 'for p in /sys/class/udc/*; do '
           'printf "udc=%s state=" "$p"; cat "$p/state"; done; '
           'for p in /config/usb_gadget/*/UDC; do '
           'printf "%s=" "$p"; cat "$p"; done')
    remote('network.txt', 'ip -details link; ip address')
    remote('config.gz', 'cat /proc/config.gz')
    remote('pstore.tar', 'tar -cf - -C /sys/fs/pstore . 2>/dev/null')
    remote('battery.txt', 'dumpsys battery')
    remote('processes.txt', 'ps -A')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--gpt-only', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    if not args.gpt_only:
        runtime()
    acquire_gpt()
    manifest = []
    for path in sorted(OUT.iterdir()):
        if path.is_file() and path.name != 'SHA256SUMS':
            manifest.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n')
    (OUT / 'SHA256SUMS').write_text(''.join(manifest))
    print(f'Captured to {OUT}')


if __name__ == '__main__':
    main()
