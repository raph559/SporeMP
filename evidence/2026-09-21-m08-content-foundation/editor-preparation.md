# M08 closed-copy comparison and original editor preparation

Date: 2026-09-21. Milestone: **IN_PROGRESS**. Evidence: **HOST / FIXTURE / STATIC**.
Original editor creation, original peer import/load, native ABI qualification and all
remaining M08 gameplay/terrain/editor-transaction gates are **NOT RUN**. No game was
launched and no desktop input was sent in this increment.

## Implemented behavior

The offline content CLI now has `compare-trees`. It inventories every file in two
closed private directory copies, decodes recognized DBPF/DBBF archives, and reports
added/removed/changed files separately from per-archive native resource changes.
Repacked archives with equal decoded resources stay distinguishable from content
changes. Opaque PNG and event files are hashed without claiming a valid creation or
dependency set. Resource keys remain scoped to their archive; native precedence is
not inferred.

The operation is bounded to 512 files, 512 directories, 16 directory levels, 128 MiB
per file, and 512 MiB each for stored and decoded tree bytes. Both input copies and
fresh output must be under ignored `local/`; the output cannot be inside either input.
Existing path, reparse, exact parsing and stable-read protections remain active. This
is an offline discovery tool, not an atomic snapshot, native import service or network
intake boundary. [Full contract](../../docs/m08-content.md).

## Exact validation and observed results

Commands were run in PowerShell from the repository root. Detailed commands, arguments,
expected/observed exits, timestamps and logs are private in `local/m08-content/session-02/`.
No build or test command starts SPORE.

| Command | Expected | Observed |
|---|---|---|
| `python -m unittest discover -s tests/unit -p test_spore_content.py -v` | Exit 0 | Exit 0; 32 tests, 0.576 s on Python 3.11.15. |
| `ctest --test-dir build/win32 -C Release -R tooling_unit --output-on-failure -V` | Exit 0 | Exit 0; 202 Python tests, 1/1 CTest target, 3.08 s on Python 3.14.3. |
| `python tools/content/spore_content.py compare-trees local/m08-native-2026-09-21/editor-01/worker-02-before/source-0 local/m08-native-2026-09-21/editor-01/worker-02-before/source-0 --output local/m08-content/session-02/same-copy.json` | Exit 0; identical | Exit 0; all file and resource identities equal. |
| `python tools/content/spore_content.py compare-trees local/m08-native-2026-09-21/editor-01/worker-02-before/source-0 local/m08-native-2026-09-21/editor-01/worker-03-before/source-0 --output local/m08-content/session-02/peer-profile.json` | Exit 20; existing profile differences | Exit 20; three changed files, two changed archives, four changed decoded records; no added/missing files or resource keys. |
| `python tools/content/spore_content.py compare-trees local/m08-native-2026-09-21/editor-01/worker-02-before/source-1 local/m08-native-2026-09-21/editor-01/worker-03-before/source-1 --output local/m08-content/session-02/creations-baseline.json` | Exit 0; equal empty creation folders | Exit 0; no creation files. |
| `pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m08-editor-01 -ReuseDatabase -Addresses '5FBB90,5FC3C0,4BB500,5A92C0'` | Exit 0; four static entries | Exit 0; all four decompilations completed, 6.20 s. |
| `python local/m08-content/session-02/verify-preparation.py` | Exit 0; backups intact and sources unchanged | Exit 0; two verified backups and 54 disposable files unchanged. |

The nine added executable tests cover opaque files versus archive repacking, changed
records scoped to individual archives, removed/replaced archives, empty directories,
archive signature checking regardless of extension, stored/file/directory/depth and
decoded aggregate bounds, concurrent tree changes, and CLI source/output protection
with distinct equality/mismatch/error exits. All passed on the first focused run and
the full tooling run. The first increment's retained Python timestamp failure remains
documented in [its session](SESSION.md); it was not repeated here.

A separate one-off documentation check initially used Python 3.11's default Windows
text encoding and failed to read existing UTF-8 documentation (`UnicodeDecodeError`,
exit 1). The corrected private `verify-documents.py` explicitly uses UTF-8. It verifies
source/artifact hashes, local links and unchanged original milestone clauses; this is
a documentation-check correction, not a content-parser or native-test failure.

## Closed disposable baselines

Each registered disposable profile was backed up through the existing quiescent backup
implementation, covering Spore data and My Spore Creations. The committed backup
manifest and every copied file were verified. A final read-only check compares both
live source trees with their recorded baselines. This does not replace the personal
backup and runtime isolation/content gates required immediately before a native launch.

| Baseline | Data files | Recognized archives | Active records | Compressed records | Stored file bytes | Decoded archive bytes |
|---|---:|---:|---:|---:|---:|---:|
| Worker 02 copy | 27 | 17 | 4,014 | 1,136 | 46,924,162 | 68,966,021 |
| Worker 03 copy | 27 | 17 | 4,014 | 1,136 | 46,925,980 | 68,966,021 |

The three differing files are GraphicsCache, Pollination and one opaque event file.
GraphicsCache has one changed decoded record; Pollination has three. The saved-world
archive records match. Both creation folders are empty. These are existing profile
differences, not evidence of a newly made or imported creature. Full inventories and
native bytes remain private; scalar counts and artifact hashes are reviewed exports.

## Static entry audit

The [binding investigation](../../docs/m08-editor-audit.md) records the pinned SDK
declarations, instruction observations, calling-convention evidence and unresolved
lifetimes. Ghidra 12.1.3 and JDK 21.0.12.1+1 reused the existing pinned executable
database with `-noanalysis`; completed decompilation does not establish complete
program analysis. Raw outputs remain in `local/m03-static/m08-editor-01/`.

The decisive findings are an ImportPNG branch that returns an existing output key
while returning false, a derived creature-data lookup using type `0x0F43029A`, and
editor submission through native messages. None establishes a durable save, successful
original loading or safe object ownership. These observations prevent treating a
single return value or SDK class ID as content readiness. No native code was changed
or bound in this increment.

## Identity, cleanup and next step

[Verification metadata](editor-preparation.json) freezes changed source hashes,
commands/logs/inventory hashes, static output hashes, backup manifest hashes, private
base revision and pinned SDK/executable/content-profile identities. The base checkout
already contains uncommitted M07 and M08 work; that work is preserved. The earlier
`verification.json` still describes the first increment's source bytes and counts.

The executable remains GOG GA 3.1.0.29 PE32, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`;
SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, injector
`26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Same recorded host as the first increment:
Windows 11 Pro 10.0.26200, Ryzen 7 9700X, RTX 4080 SUPER and 33,407,430,656 bytes RAM.
The final host check finds no running SPORE process. No worker, recorder or new
background service was started; private backups, logs and static analysis are retained.
Full installed-content hashing and personal-save before/after native protection were
not refreshed because no native experiment ran.

Next smallest step: after current desktop availability is confirmed, refresh the
personal-save gate and perform one original creature-editor save, close the process,
compare fresh backups, then test the new artifact through the original import/load
route in the second profile. Keep one game active at a time and bound active testing
to ten minutes per process. Inspect process-specific original views and closed-file
evidence. Dependency approval, worker-derived gameplay properties, canonical terrain
and the owner/version-fenced multiplayer editor transaction remain open requirements.
