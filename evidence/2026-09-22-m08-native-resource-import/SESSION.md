# M08 native resource resolution and direct PNG import

Date: 2026-09-22. Evidence classes: **BUILD, HOST/FIXTURE, NATIVE**.
M08 status: **IN_PROGRESS**. The user explicitly authorized computer use.
This increment does not qualify multiplayer publication or complete M08.

Native05 uses frozen bridge/NativeHost **0.0.47**, isolated worker03, original PID
46448. Both audited original resource-manager getters return the same manager.
Exact and mapped lookups resolve all **nine unique part property records** through
`PatchData.package`. The imported CRT resolves through `Pollination.package`.
The original creature loader again returns **23 rigblocks and 47 capability
entries**, with its owned reference released once.

After loading the original Satiria world, the observed generated-terrain key is
`4084a100!28ca32e7.011989b7`. That runtime type does not name the persisted property
record. The original property lookup instead resolves
`4084a100!28ca32e7.00b1b104` through `PlanetScripts.pld`, for both manager getters
and both exact/mapped lookup paths. Static inspection of the original terrain
manager independently confirms the instance/group property lookup. Its fallback
can generate/write terrain; that fallback was **not called**. This corrects the
earlier exact-type search assumption without claiming a complete terrain graph.

Native06 uses frozen bridge/NativeHost **0.0.48**, isolated worker01, original PID
70964. Before this run, its verified closed backup contains 27 files and no
creation-folder files. The guarded original `ImportPNG` binding is called once
while the existing Satiria world is loaded. It accepts the original saved PNG,
SHA-256 `61a90fb4a58ff0ee5b17c37224a467975a35605b44c214a6bbc3178abcadcd8f`,
28,667 bytes, and returns a new local CRT key:
`40626200!28dc9d58.2b978c46`. The immediate original loader inspection returns
the same 23/47 copied native values as the prior source and clean-profile import.
The common canonical scalar digest is
`35cdece5f47b461a6dc9323811637835969f1a49b88020e1d9d82914280b0f4b`.

The original Sporepedia visibly contains **M08 Native One**, with its cyan image.
The original world resumes after leaving Sporepedia; inspected footage shows
native animation and continuing hunger changes. This does not replace the
campaign actor with the imported creature or demonstrate its campaign combat.

Both games and supervisors exit with code 0 after explicit shutdown requests.
Both actor traces have complete healthy footers and successful observer cleanup.
The 29 protected personal files and all six guarded world files remain unchanged.
The source archive, PNG and imported engine-produced resources stay private.
The reviewed scalar export and exact binary/source/capture identities are in
[verification.json](verification.json).

## Commands and results

Commands run from the private checkout in PowerShell. Paths below are private
evidence locations, not additional files published with this report.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure -V
python -m unittest discover -s tests/unit -p test_m08_import_probe.py -v
python local/m08-2026-09-22/freeze-resource-047.py
python local/m08-native-2026-09-22/launch-editor.py 03 local/m08-native-2026-09-22/editor-01/native-05 local/m08-2026-09-22/resource-047/verification.json
python tools/native/m08-content-probe.py --worker 03 --source-archive local/m08-native-2026-09-22/editor-01/native-03/worker-03-after/source-0/Pollination.package --key '40626200!28dc92ab.2b978c46' --output local/m08-native-2026-09-22/editor-01/native-05/inspection-01 --version 0.0.47
python local/m08-native-2026-09-22/observe-world.py 03 local/m08-native-2026-09-22/editor-01/native-05/world-01 0.0.47
python local/m08-native-2026-09-22/close-editor.py local/m08-native-2026-09-22/editor-01/native-05
python local/m08-2026-09-22/prepare-import-048.py
python local/m08-native-2026-09-22/launch-editor.py 01 local/m08-native-2026-09-22/editor-01/native-06 local/m08-2026-09-22/import-048/verification.json
python local/m08-native-2026-09-22/observe-world.py 01 local/m08-native-2026-09-22/editor-01/native-06/world-01 0.0.48
python tools/native/m08-import-probe.py --worker 01 --png 'local/m08-native-2026-09-22/editor-01/native-01/worker-02-after/source-1/Creatures/M08 Native One.png' --output local/m08-native-2026-09-22/editor-01/native-06/import-01
python local/m08-native-2026-09-22/close-editor.py local/m08-native-2026-09-22/editor-01/native-06
python local/m08-native-2026-09-22/seal-resource-import.py
```

All listed operations have expected/observed exit 0. The .47 full suite passes
12/12 in 28.45s; the .48 full suite passes 12/12 in 28.48s, including 236 Python
tooling tests and 294 worker assertions. The dedicated import driver passes 13
tests. Static resource-resolution runs01–06 and import-manager runs01–05 complete
with exit 0 through the pinned existing audit tooling; raw output remains private.

Process-specific WGC capture uses `capture-game.ps1 -GamePid 46448 -Seconds 120`
and `-GamePid 70964 -Seconds 180`, each with its run's fresh `capture-01` directory.
Both recorders return 0 without wall timeout. Native06's 180-second container
contains 4,127 video frames; a requested frame at155s is absent. Actual frames
at2s,75s and110s were inspected, covering the original intro, imported card and
returned world. Container duration is not proof of continuous frames or frame
timing. Native05's original intro and loaded-world frames were also inspected.

## Binding and scope

Pinned executable SHA-256:
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`.
The verification export records OS/hardware, injector/content identity, private
base revision, source-set digests and every frozen payload hash.

All calls occur on the verified SDK/app-update thread. The borrowed resource and
import managers are never released as owned values. Original `ImportPNG` uses
ECX plus two stack arguments and returns bool in AL with RET8. A false result can
still return an existing key; it is never treated as a successful new import.
The fixed quarantine path is locked and checked against its expected SHA-256,
regular-file identity, link/reparse constraints, size, PNG chunks and CRCs before
the single original call. The Python driver additionally bounds decompression,
pixel length and PNG row filters. This is not arbitrary Internet PNG acceptance.

Complete dependency closure, canonical multiplayer terrain synchronization,
owner/version editor publication, concurrent multiplayer editing and adversarial
native readiness remain separate gates. The next increment connects original
inspection to authenticated content transfer and immutable editor transactions.
