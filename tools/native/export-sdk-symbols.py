"""Export source-backed GA address labels for static research, not ABI bindings."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT/'external/Spore-ModAPI'
PIN = 'cbf9206b9a823f0911cd9be0217104a49d72380b'
assert subprocess.check_output(['git','-C',str(SDK),'rev-parse','HEAD'],text=True).strip() == PIN
assert not subprocess.check_output(['git','-C',str(SDK),'status','--porcelain'],text=True).strip(), 'SDK checkout is modified'
OUTPUT = ROOT/'local/m03-static/sdk-symbols.tsv'
assert not OUTPUT.exists()
symbols=[]
sources=[]
for relative in ('Spore ModAPI/SourceCode/DLL/AddressesSimulator.cpp','Spore ModAPI/SourceCode/DLL/AddressesApp.cpp'):
    path=SDK/relative
    text=path.read_text(encoding='utf-8-sig')
    sources.append(dict(path=relative,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for block in re.finditer(r'namespace\s+Addresses\((\w+)\)\s*\{([^{}]*)\}',text,re.S):
        for method in re.finditer(r'DefineAddress\((\w+),\s*SelectAddress\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\)\)',block[2]):
            address=int(method[3],16)
            if not address: continue
            line=text.count('\n',0,block.start(2)+method.start())+1
            symbols.append((f'{address:08X}',f'SDK_{block[1]}_{method[1]}',f'{relative}:{line}'))
with OUTPUT.open('x',encoding='utf-8') as f:
    for symbol in symbols:
        f.write('\t'.join(symbol)+'\n')
report=dict(evidence_class='PINNED_SDK_STATIC_LABELS_NOT_VERIFIED_ABI',sdk_commit=PIN,sources=sources,
            labels=len(symbols),output=str(OUTPUT),sha256=hashlib.sha256(OUTPUT.read_bytes()).hexdigest())
with OUTPUT.with_suffix('.json').open('x',encoding='utf-8') as f:
    json.dump(report,f,indent=2)
print(json.dumps(dict(labels=len(symbols),sdk=PIN)))
