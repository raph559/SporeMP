"""Record the exact pinned binary bytes behind the M03 nest-context adaptation."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    game = args.executable
    if args.output.exists():
        raise ValueError('Use a fresh output path')
    if hashlib.sha256(game.read_bytes()).hexdigest() != 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37':
        raise ValueError('Unknown executable SHA-256')
    args.output.parent.mkdir(parents=True, exist_ok=True)

    data = game.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    assert sha == 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    count, optional_size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    base = struct.unpack_from('<I', data, pe+24+28)[0]
    assert base == 0x400000
    sections = []
    for i in range(count):
        offset = pe+24+optional_size+i*40
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', data, offset+8)
        sections.append((rva, raw_size, raw_offset))
    def at(va, size):
        rva = va-base
        for section_rva, raw_size, raw_offset in sections:
            if section_rva <= rva and rva+size <= section_rva+raw_size:
                return data[raw_offset+rva-section_rva:raw_offset+rva-section_rva+size]
        raise ValueError(hex(va))
    checks = []
    for va, expected in ((0xd7d900,'f74424100010000056570f85d3000000'),
                         (0xd99930,'8b4424045685c074338b482c85c9742c')):
        observed = at(va,16).hex()
        checks.append(dict(va=hex(va), expected=expected, observed=observed))
        assert observed == expected
    decider_id, kind, decide, cleanup = struct.unpack('<IIII',at(0x1587c40,16))
    assert decider_id == 0x04a1a0f9 and decide == 0xd7d900 and cleanup == 0
    report = dict(evidence_class='STATIC_BINARY_BINDING_NOT_NATIVE_ACCEPTANCE',
                  executable=str(game), executable_sha256=sha, image_base=hex(base),
                  sdk_commit='cbf9206b9a823f0911cd9be0217104a49d72380b', checks=checks,
                  decider=dict(va='0x1587c40', id=hex(decider_id), kind=kind, decide_va=hex(decide), cleanup_va=hex(cleanup)),
                  abi=dict(decide='float cdecl(Creature*, double, uint32, void*, bool*, void*, void*)',
                           decide_stack_words=8, decide_caller_cleanup_bytes=32, decide_result='x87',
                           callsite_va='0xbc8b27', callsite_bytes=at(0xbc8b27,32).hex(),
                           memory='CreatureBase* cdecl(void*)', memory_return_site_rva='0x97d9aa',
                           memory_callsite_bytes=at(0xd7d9a4,12).hex()),
                  scratch_review='D7D900 initializes 40-byte scratch and sets dirty. Its decider cleanup callback is null; BC86E0 skips it. The adapter preserves these native outputs and changes only the score for a live bridge-owned remembered target lacking native avatar flag 0x200.',
                  references=['local/m03-context-0912/native-disassembly.txt','local/m03-static/tree-competitors/D7D900.c.txt',
                              'external/Spore-ModAPI/Spore ModAPI/Spore/Simulator/SubSystem/GameNounManager.h'])
    with args.output.open('x',encoding='utf-8') as stream:
        json.dump(report,stream,indent=2)
    print(json.dumps(report))


if __name__ == "__main__":
    main()
