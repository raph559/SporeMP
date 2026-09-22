# M08 native creation, import and inspection

Date: 2026-09-22. Evidence: **NATIVE**, inspected process-specific captures,
**HOST** closed-file comparisons, and **HOST/FIXTURE** driver tests.
M08 remains **IN_PROGRESS**; no readiness or multiplayer publication is granted.

## Result and scope

Four original GOG GA processes ran sequentially in the prepared disposable OS
profiles, using the unchanged frozen Bridge/NativeHost **0.0.46**. The user explicitly
authorized computer use. The original editor saved **M08 Native One**, a cyan-painted,
renamed derivative of the installed Alderbrook template. Two attempts to attach a
mouth manually did not attach; this report does not claim a newly added part.

The original save returned logical key `40626200!28dc8ee1.2b978c46`. Its closed
EditorSaves package contains five records: pollen metadata, derived creature data,
a `.bem` model, and two PNG records. It contains no literal `.crt` record. The
28,667-byte standalone PNG exactly matches the package's main PNG record, SHA-256
`61a90fb4a58ff0ee5b17c37224a467975a35605b44c214a6bbc3178abcadcd8f`.

Only that PNG was placed in the verified closed, initially creation-free peer
profile. On startup, original SPORE imported it into Pollination as a literal
`.crt`, with local key `40626200!28dc92ab.2b978c46`. The PNG record remained identical.
The peer's EditorSaves package stayed empty. No retail assets, cache package,
Pollination database, or saved world was copied between profiles for this import.

Both original editor views show the cyan creature, its saved name, and matching
displayed carnivore/bite/singing/speed/health information. Both original test-drive
views show native movement following the selected ground target. Separate native
loader calls return exactly matching copied model type, **23 rigblocks**, and
**47 raw capability entries**, including **nine unique part references**. The
canonical JSON hash of those copied scalars is
`35cdece5f47b461a6dc9323811637835969f1a49b88020e1d9d82914280b0f4b`.
Both calls complete their owned release. These observations do not establish full
campaign combat behavior, every model property, a complete dependency graph, or
authority acceptance of a client submission. Raw BEM/CRT bytes differ.

## Runs

| Run | Native work | Observed result |
|---|---|---|
| native-01 / worker 02 | Original editor save, then blank draft cancel | Accepted logical creation key; five closed EditorSaves records and one standalone PNG. Blank cancel returns the original zero key. |
| native-02 / worker 02 | Fresh native loader inspection, saved editor reopen, test drive, orange draft cancel | 23/47 records copied and released. Cancel returns the original saved key. Closed EditorSaves and standalone PNG remain byte-identical to native-01. |
| native-03 / worker 03 | Startup import of the one staged PNG, editor reopen, test drive, orange draft cancel | New local CRT key in Pollination, matching cyan view and displayed abilities. Capture contains orange draft, discard prompt, and cyan Sporepedia return. |
| native-04 / worker 03 | Fresh imported-CRT inspection, existing native Satiria world load | Exact copied-scalar match with native-02. Native world-load completion and home terrain key recorded; original world view inspected. |

The imported model was not used as the native-04 campaign avatar. Native-04 loads
the existing controlled Satiria fixture solely for terrain observation.

Native-02's 120-second capture ends before the final Cancel click. Its orange draft
is captured; final cancellation is supported separately by the observed UI, paired
native cancellation event, and unchanged closed model/PNG. Native-03 records the
complete corresponding visual sequence. The capture helper's default
`assistant_desktop_input:false` field describes that helper, not the whole session:
the session did send authorized UI input through Computer Use.

## Reproduction and checks

The frozen native build and prior **12/12 CTest** result remain identified by the
[build manifest](../2026-09-22-m08-admission-inspection/verification.json). No C++ or
native payload changed during these four runs. The Python inspection driver and
its tests changed; their exact hashes are in [verification.json](verification.json).
The pinned executable, SDK/injector commits, compiler, Windows build, GPU/driver and
hardware are recorded there and in the original build report.

The native driver now accepts a BEM-backed logical CRT key only with an explicit
closed original save trace. It requires pinned provenance, contiguous sequence,
one matching accepted result, sequential request/result pairing, observer cleanup,
zero foreign callbacks, and a healthy final footer. It still validates every source
archive record and rechecks the source/witness after its single request. It does not
import the supplied archive or claim that matching native keys prove byte identity.
Literal CRT records, including the actual peer import, use the existing exact-record
admission path.

Commands were run from the private repository root in PowerShell. The common raw
root below is `local/m08-native-2026-09-22/editor-01/`.

```powershell
python -m unittest discover -s tests/unit -p test_m08_content_probe.py -v
ctest --test-dir build/win32 -C Release -R tooling_unit --output-on-failure -V
python tools/native/m08-content-probe.py --worker 02 --source-archive local/m08-native-2026-09-22/editor-01/native-01/worker-02-after/source-0/EditorSaves.package --save-trace local/m08-native-2026-09-22/editor-01/native-01/closed-run/actors-7212.jsonl --key '40626200!28dc8ee1.2b978c46' --output local/m08-native-2026-09-22/editor-01/native-02/inspection-01 --version 0.0.46
python tools/native/m08-content-probe.py --worker 03 --source-archive local/m08-native-2026-09-22/editor-01/native-03/worker-03-after/source-0/Pollination.package --key '40626200!28dc92ab.2b978c46' --output local/m08-native-2026-09-22/editor-01/native-04/inspection-01 --version 0.0.46
```

Final focused driver coverage is **19 tests**, exit 0. The final affected CTest
target passes **223 tooling tests**, exit 0, in **6.72 seconds**. Both native probe
commands return exit 0 and `probe_checks_passed:true`; their reports deliberately
retain `native_acceptance:NOT_VERIFIED`, `gameplay_validation:false` and
`readiness:false`.

Private `launch-editor.py` invokes the existing `start-worker.ps1` separately from
build/tests, verifies all frozen artifacts, requests a fresh generation/current
desktop, refreshes personal-save protection and starts a generation-specific
600-second shutdown watcher. Each launch exits 0. `capture-game.ps1` targets exact
game PIDs 7212, 32832, 35024 and 60840; five recordings finish with exit 0 and no wall
timeout. `close-editor.py` requests shutdown, verifies actual exit, archives the
closed run, creates and verifies a closed profile backup, and runs the personal
preservation check. Each close exits 0. Exact subprocess commands/replies and raw
launch/capture/close metadata remain under the common private root.

`compare-trees` returns **20 for differences**, as expected, and **0 for equality**.
Native-02's creation tree compares equal; its data changes are limited to editor
offline metadata, GraphicsCache, Pollination and an event file. Native-03's original
baseline comparison shows the imported records and staged PNG. The first native-03
review script exits 1 because it expected a BEM; the preserved comparison instead
reveals the actual CRT representation. The corrected review exits 0 without
overwriting that comparison. Closed-file comparison is HOST evidence on original
native-produced bytes, not an additional native execution.

## Terrain and remaining acceptance

Native-04 reports home generated-terrain key `4084a100!28ca32e7.011989b7` after the
matching original load completes and native AI advances. The inspected original
view shows the existing Satiria terrain and creatures. The exact key is absent
from the inspected closed archive indexes; a bounded search for its three-word
little/big-endian serialization in the six guarded world files also finds no
occurrence. That search is not a complete serializer decoder. The saved
Planets package is empty. Complete generated-terrain resource mapping remains
unqualified; the six-file admission guard is not promoted to a complete terrain
synchronization claim.

All four games and supervisors exit 0 on requested shutdown. All actor traces
have contiguous sequences and healthy detach status 0; each observer removes its
listeners and reports zero foreign callbacks. The **29 personal files** and all
**six guarded world files** remain unchanged across every closed run. Raw assets,
accounts, saves, logs, and captures remain private. The reviewed manifest contains
only scalar findings and artifact identities.

Next: qualify exact installed part/resource resolution and the generated-terrain
mapping, then connect quarantined native validation and owner/base-version fencing
to the C++ content registry and editor transaction. Missing dependencies and
changed terrain must deny readiness precisely. The native multiplayer
begin/commit/cancel test with another player's continuing world is still NOT RUN.
All original [M08 gates](../../tests/engine/M08.md) remain in force.
