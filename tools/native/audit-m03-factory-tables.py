"""Read pinned PE tables for the actor factory investigation; no executable loading."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    EXE = args.executable
    if args.output.exists():
        raise ValueError('Use a fresh output path')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37':
        raise ValueError('Unknown executable SHA-256')
    args.output.parent.mkdir(parents=True, exist_ok=True)

    SHA = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
    data = EXE.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA
    u16 = lambda n: struct.unpack_from('<H', data, n)[0]
    u32 = lambda n: struct.unpack_from('<I', data, n)[0]
    pe = u32(0x3c)
    optional = pe + 24
    base = u32(optional + 28)
    table = optional + u16(pe + 20)
    sections = [(u32(table + i*40 + 12), u32(table + i*40 + 16),
                 u32(table + i*40 + 20)) for i in range(u16(pe + 6))]

    def read(va, count):
        for rva, length, offset in sections:
            if rva <= va-base and va-base+count <= rva+length:
                return data[offset+va-base-rva:offset+va-base-rva+count]
        raise ValueError(hex(va))

    slots = []
    for vtable, offsets in [(0x146a080, [0x28, 0x4c, 0x54, 0xac, 0xc0, 0xcc, 0xd0]),
                           (0x1469ec0, [0x10, 0x14, 0x48, 0x4c, 0x58]),
                           (0x1469e30, [0x08, 0x0c, 0x10])]:
        for offset in offsets:
            target = struct.unpack('<I', read(vtable+offset, 4))[0]
            slots.append(dict(vtable_va=hex(vtable), slot=hex(offset), target_va=hex(target)))
    report = dict(evidence_class='STATIC_TABLE_READ_NOT_NATIVE_EXECUTION', executable_sha256=SHA,
                  slots=slots,
                  attack_behavior=dict(table_va='0x1583f30', words=[hex(v) for v in struct.unpack('<4I', read(0x1583f30, 16))]))
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
    print(json.dumps(report))


if __name__ == "__main__":
    main()
