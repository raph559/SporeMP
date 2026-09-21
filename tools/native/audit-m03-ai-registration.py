"""Find static references to the observed native AI reset callback. Never loads the game."""
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
    exe = args.executable
    if args.output.exists():
        raise ValueError('Use a fresh output path')
    if hashlib.sha256(exe.read_bytes()).hexdigest() != 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37':
        raise ValueError('Unknown executable SHA-256')
    args.output.parent.mkdir(parents=True, exist_ok=True)

    data = exe.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    assert sha == 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
    u16 = lambda n: struct.unpack_from('<H',data,n)[0]
    u32 = lambda n: struct.unpack_from('<I',data,n)[0]
    pe=u32(0x3c)
    optional=pe+24
    base=u32(optional+28)
    table=optional+u16(pe+20)
    refs=[]
    for index in range(u16(pe+6)):
        start=table+index*40
        section=data[start:start+8].split(b'\0')[0].decode('ascii')
        rva,length,offset=u32(start+12),u32(start+16),u32(start+20)
        cursor=offset
        while True:
            found=data.find(struct.pack('<I',0xd672a0),cursor,offset+length)
            if found == -1: break
            refs.append(dict(section=section, pointer_operand_va=hex(base+rva+found-offset),
                             file_offset=found, bytes_before_pointer=data[max(offset,found-12):found].hex()))
            cursor=found+4
    assert len(refs)<30
    report=dict(evidence_class='STATIC_POINTER_REFERENCES_NOT_CALLER_PROOF', executable_sha256=sha,
                callback_va='0xd672a0',references=refs,native_execution='NOT RUN')
    with args.output.open('x',encoding='utf-8') as f:
        json.dump(report,f,indent=2)
    print(json.dumps(report))


if __name__ == "__main__":
    main()
