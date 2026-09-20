"""Read-only strings, pointer references and x86 RTTI from the pinned original PE."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXE = Path('C:/Games/SPORE/SporebinEP1/SporeApp.exe')
SHA = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
data = EXE.read_bytes()
if hashlib.sha256(data).hexdigest() != SHA:
    raise ValueError('Unknown executable')
nt = struct.unpack_from('<I', data, 0x3c)[0]
optional = nt + 24
base = struct.unpack_from('<I', data, optional + 28)[0]
length = struct.unpack_from('<H', data, nt + 20)[0]
sections = [struct.unpack_from('<8sIIII', data, optional + length + i * 40)
            for i in range(struct.unpack_from('<H', data, nt + 6)[0])]

def offset(va):
    for _, _, start, size, raw in sections:
        if base + start <= va < base + start + size:
            return raw + va - base - start
    raise ValueError(hex(va))

def address(at):
    for _, _, start, size, raw in sections:
        if raw <= at < raw + size:
            return base + start + at - raw
    raise ValueError(at)

def refs(va):
    return [address(match.start()) for match in re.finditer(re.escape(struct.pack('<I', va)), data)
            if any(raw <= match.start() < raw + size for _, _, _, size, raw in sections)]

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('needle')
    parser.add_argument('--rtti', action='store_true')
    args = parser.parse_args()
    for match in re.finditer(rb'[ -~]{4,}', data):
        text = match.group().decode('ascii')
        if args.needle.lower() not in text.lower():
            continue
        va = address(match.start())
        item = {'text': text[:240], 'va': hex(va), 'pointer_references': [hex(n) for n in refs(va)]}
        if args.rtti and text.startswith('.?AV'):
            tables = []
            for reference in refs(va - 8):
                locator = reference - 12
                words = struct.unpack_from('<IIIII', data, offset(locator))
                if words[0] == 0 and words[1] < 0x100 and words[2] == 0 and words[3] == va - 8:
                    for table_ref in refs(locator):
                        entries = struct.unpack_from('<12I', data, offset(table_ref) + 4)
                        tables.append({'locator': hex(locator), 'this_offset': words[1],
                                       'vtable': hex(table_ref + 4), 'entries': [hex(n) for n in entries]})
            item['rtti_candidates'] = tables
        print(json.dumps(item))
