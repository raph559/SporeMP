"""Compile the reviewed content inventory and exact built DLL hashes into the guard."""
import hashlib
import json
from pathlib import Path
import sys

repo, configuration, output = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
candidate = json.loads((repo / 'config/compatibility.candidate.json').read_text())
def literal(value):
    return 'L' + json.dumps(value, ensure_ascii=True)
lines = ['#pragma once', 'struct GuardFile { const wchar_t* path; const char* sha; };', 'inline constexpr GuardFile game_files[] = {']
for root, inventory in candidate['content'].items():
    for entry in inventory['files']:
        lines.append('{' + literal(root + '/' + entry['path']) + ',"' + entry['sha256'] + '"},')
lines += ['};', 'inline constexpr const wchar_t* game_directories[] = {']
for root, inventory in candidate['content'].items():
    for directory in ['', *inventory['directories']]:
        lines.append(literal(root + ('/' + directory if directory else '')) + ',')
lines += ['};', 'inline constexpr GuardFile payload_files[] = {']
for name, path in [
    ('ModAPI.DLLInjector.dll', repo / 'build/injector' / configuration / 'ModAPI.DLLInjector.dll'),
    ('mLibs/SporeModAPI.dll', repo / 'build/sdk' / configuration / 'SporeModAPI.dll'),
    ('mLibs/SporeMP.Bridge.dll', repo / 'build/win32' / configuration / 'SporeMP.Bridge.dll')]:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    lines.append('{' + literal(name) + ',"' + digest + '"},')
lines += ['};']
output.parent.mkdir(parents=True, exist_ok=True)
text = '\n'.join(lines) + '\n'
if not output.exists() or output.read_text() != text:
    output.write_text(text)
