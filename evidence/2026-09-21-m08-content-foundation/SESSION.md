# M08 content foundation — 2026-09-21

Outcome: **M08 IN_PROGRESS**, first offline content increment implemented and tested.
Evidence classes: **HOST / FIXTURE**. New original-game execution and native M08
acceptance: **NOT RUN**. No C++/launcher change or new native build is part of this
increment. M06/M07 remain verified for their recorded fixtures; the complete M00–M20
plan and original M08 clauses are retained.

## Change and result

`tools/content/spore_content.py` implements bounded DBPF/DBBF index validation,
RefPack decoding, native resource-key/decoded-byte hashes, exact archive comparison,
and a private immutable candidate quarantine. Cache selectors cannot become filesystem
paths. Invalid/truncated/oversized/overlapping/duplicate inputs are refused. Interrupted
writes stay uncommitted, existing corrupt candidates are never replaced, and all cache
reuse verifies the complete manifest and payload hashes. No candidate can grant native
validation, dependency approval, transfer approval or multiplayer readiness.

`tests/unit/test_spore_content.py` adds 23 HOST/FIXTURE tests. The existing CMake
`tooling_unit` target discovers them automatically; no build-system change is needed.
[Implementation contract](../../docs/m08-content.md) and [native gate map](../../tests/engine/M08.md)
retain the unimplemented dependency/terrain/editor work explicitly.

## Commands and exits

All commands ran in PowerShell from the private development checkout. Exact child
argument arrays, UTC start/finish timestamps and exits are in ignored
`local/m08-content/session-01/final-02/*.command.json`; raw logs and environment
records remain there. The [reviewed verification export](verification.json) retains
their hashes and command outcomes without personal absolute paths.

| Command | Expected | Observed |
|---|---|---|
| `python -m unittest discover -s tests/unit -p test_spore_content.py -v` | Exit 0; offline parser/cache checks pass | Final: exit 0, 23 tests, 0.209 seconds, Python 3.11.15. |
| `ctest --test-dir build/win32 -C Release -R tooling_unit --output-on-failure -V` | Exit 0; existing Python tooling plus M08 pass | Final: exit 0, 1/1 CTest target; 193 tests on Python 3.14.3, 2.84 seconds wall time. |
| `pwsh -NoProfile -File local/m08-content/session-01/validate.ps1 -RunName final-01` | Execute focused/full tooling and archive/cache checks | Exit 1 after CTest exit 8; initial Windows timestamp discrepancy, below. Archive/cache probes were not reached in this attempt. |
| `pwsh -NoProfile -File local/m08-content/session-01/validate.ps1 -RunName final-02` | Corrected tests and complete offline experiment | Exit 0. All 11 recorded child commands exit 0; six source archives unchanged. |
| `git ls-remote https://github.com/Spore-Community/SporeModder-FX.git refs/heads/master` | Resolve immutable format-reference revision | Exit 0, `f60de8aa0ef4bd83768b07a7acf0f718470f8cd6`; pinned source subsequently read. No dependency installed. |
| `git diff --check -- MILESTONES.md STATUS.md docs/testing.md` | No whitespace errors | Exit 0. Existing line-ending notices only. |

The final validation wrapper executes `inspect` for the six archives below. Source
prefix is `local/m04-native/worker-02-after-concurrent/source-0/`; output prefix is
`local/m08-content/session-01/final-02/`. Each exact child command has the form:

```powershell
python tools/content/spore_content.py inspect SOURCE --output REPORT
```

It also executes the following with output paths under that final output prefix:

```powershell
python tools/content/spore_content.py quarantine local/m04-native/worker-02-after-concurrent/source-0/GraphicsCache.package --key '40626202!065cb811.00e6bce5' --output local/m08-content/session-01/final-02/quarantine.json
python tools/content/spore_content.py quarantine local/m04-native/worker-02-after-concurrent/source-0/GraphicsCache.package --key '40626202!065cb811.00e6bce5' --output local/m08-content/session-01/final-02/quarantine-reuse.json
python tools/content/spore_content.py compare local/m04-native/worker-02-after-reload/source-0/Games/Game0/Satiria.spo local/m04-native/worker-02-after-concurrent/source-0/Games/Game0/Satiria.spo --output local/m08-content/session-01/final-02/world-comparison.json
```

The wrapper supplies absolute private output paths; the equivalent relative paths
above keep this report publishable. Use fresh outputs when reproducing; existing
evidence is intentionally not overwritten. The comparison of these two historical
world archives finds equal decoded records and still reports readiness false. Mismatch
exit 20 is exercised separately by the HOST/FIXTURE CLI test.

## Actual file observations

| Source under the prefix above | Report | Container | Active records | Compressed | Expanded bytes |
|---|---|---|---:|---:|---:|
| `Pollination.package` | `pollination.json` | DBBF | 2,617 | 0 | 1,192,903 |
| `GraphicsCache.package` | `graphics.json` | DBBF | 221 | 0 | 34,150,184 |
| `Games/Game0/Satiria.spo` | `world.json` | DBPF | 39 | 35 | 2,225,367 |
| `Games/Game0/planetRecords.pkp` | `planet_records.json` | DBPF | 73 | 73 | 29,428 |
| `Planets.package` | `planets.json` | DBPF | 0 | 0 | 0 |
| `EditorSaves.package` | `editor.json` | DBPF | 0 | 0 | 0 |

Total: **2,950 active records, 108 compressed**. All six before/after archive SHA-256
values match. File sizes, full archive hashes and canonical record-set hashes appear
in `verification.json`. Original files, decoded payloads and full key inventories
remain private. The nonempty Planets file has no active records: file size or leftover
archive bytes must not be mistaken for canonical generated terrain.

The quarantine experiment stores one existing generated-model record, not a newly
created creature or complete dependency set. Its candidate identity is
`84b7b89639c068e063a8987d5382ad912c8fc552a6db826981b386f411811831`.
The second call verifies/reuses the same candidate and returns the same result. Its
native validation is NOT_RUN, transfer approval false and readiness false.

The sampled Pollination archive contains 2,588 catalog-summary records and no `.crt`
record. Empty EditorSaves plus an existing graphics cache cannot establish a portable
custom creature. The SDK describes PNGs carrying model data/metadata, but native PNG
decoding, complete dependency extraction and cross-profile loading remain unqualified.

## Retained failure and correction

The initial focused tests passed, but full `tooling_unit` on the CTest-configured Python
3.14 failed with one failure and two errors: fresh cache verification reported
`source_changed`. A minimal repeated local cache-write reproduction found equal file
IDs, sizes, modification times and birth times, but `os.fstat().st_ctime_ns` represented
change time while `Path.stat().st_ctime_ns` represented birth time. The private
`python314-stat-repro.log` preserves the actual values.

The correction compares change time only across the two file-handle queries, while
retaining file ID/device/size/mtime comparisons against the final path query. A specific
regression fixture models the divergent timestamp meanings. A separate actual concurrent
file append still produces `source_changed`; corrupt-cache refusal remains intact.
The final 23/193 tests pass without weakening parsing, hash or publication checks.

Two guessed source paths and one guessed web header filename were absent during
read-only discovery; verified `rg --files`, pinned headers and `DatabasePackedFile.java`
supplied the correct sources. One multi-file documentation patch failed its context
check without applying changes; the corrected patch applied. These research/editing
errors are not native probes or acceptance results.

## Provenance and limitations

Current inspected host: Windows 11 Pro 10.0.26200, Ryzen 7 9700X, RTX 4080 SUPER,
33,407,430,656 bytes RAM. SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`;
injector `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. The actual executable hash is
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The compatibility-profile file hash is
`06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b`.
This checks the pinned identity; it does not refresh every installed content byte or
qualify a new native configuration. Exact changed-tool/test/reference hashes and the
private base revision plus dirty-source boundary are recorded in `verification.json`.

The existing uncommitted M07 implementation was preserved. New code does not change
the bridge/coordinator/launcher or attach new hooks. Zero game processes were started
and no desktop input was sent. Native captures, personal live-save before/after hashing,
native import/load and gameplay/editor acceptance are NOT RUN in this offline session.
Only the six inspected archived source files receive the fresh unchanged-hash claim.
Public export review includes scalar results/hashes only; raw logs, archived native
data, environment inventories and cache blobs remain under ignored `local/`.

Next smallest step: qualify one original editor creation in an inventoried disposable
profile, capture its save/native key, compare closed resource/PNG changes, and load the
candidate in a clean second disposable profile. That requires current desktop
availability and refreshed native save/isolation gates. It precedes dependency approval,
authoritative capability validation, terrain readiness and editor transaction wiring.
