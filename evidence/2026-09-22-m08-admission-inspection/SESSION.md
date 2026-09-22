# M08 world admission and native inspection preparation

Date: **2026-09-22**. Milestone: **IN_PROGRESS**. Evidence classes:
**BUILD, HOST, FIXTURE, STATIC and real Windows TLS**. Original M08 game execution
is **NOT RUN**. This report is a reviewed scalar export; raw traces, disassembly,
logs, credentials, profile inventories and native assets remain under ignored
`local/`. No public repository publication or release was performed.

## Objective and implementation

Prepare the next original-game content experiment and close the existing Join
path's missing companion-world-file check without reducing M08's original gates.

Bridge/NativeHost **0.0.46** and launcher **0.1.10** now require six fixed world-file
identities before a network authority or player can receive a scene baseline.
The launcher hashes actual local files and NativeHost independently rechecks the
actual account before creating the original process. The coordinator compares
declared identities during TLS admission and returns the exact differing path.
Compatibility is checked before credentials; a wrong-credential peer may see a
world mismatch, but cannot receive Welcome or a scene baseline.

The bundle is `Games/Game0/Satiria.spo`, `Games/Game0/planetRecords.pkp`,
`Games/Game0/planetRecords.pkt`, `Games/Game0/PlanetScripts.pld`,
`Games/Game0/stars.db` and `Planets.package`, relative to the actual profile's Spore
data root. The C++ reader holds all six files against writes/deletion while hashing
and enforces regular local paths and size limits. All handles close before native
startup. This is prelaunch byte identity, not proof of runtime terrain completeness.

Wire schema **5 / 600 bytes** appends the six hashes and an allowlisted mismatch
index without changing preceding scalar offsets. Config schema **2** requires all
hashes; old private configs must be regenerated. The original `fixture_sha256`
meaning and existing identity/owner/scene/baseline protections remain intact.
Launcher release notes, changelog and project version are aligned.

The private `inspect_creation` command invokes the original creature-data loader
for one explicit `.crt` key. It checks thread, code prefixes, returned object
vtable, vector bounds and capability ranges; it copies bounded scalar observations
and releases the owned native reference. The worker refuses replicas, loading,
stale epochs and malformed keys. The driver sends only one request, binds it to a
captured isolated process and exact native trace provenance, and preserves uncertain
outcomes without retries. Same-key results do not prove archive-byte equivalence.
Gameplay validation, transfer approval and readiness remain false.

The native load completion also copies the home planet's generated-terrain key
through the existing qualified getter. The event explicitly leaves canonical
resource validation false. This observation still needs original execution and
correlation with closed archives and inspected terrain.

A passive editor observer now records original request and accepted/cancelled
result messages through the existing listener interface. Guarded bounded copies
retain no native pointers and invoke no editor control or save routine. The
native routing field is not proven unique; acceptance is not a committed save or
authoritative publication. This is also **IMPLEMENTED_NOT_RUN**.

Contracts: [world identity](../../docs/m08-world-identity.md),
[native probe and ownership](../../docs/m08-native-content-probe.md),
[all M08 gates](../../tests/engine/M08.md).

## Commands and observed outcomes

Commands below run from the repository root in PowerShell. Complete outputs and
command metadata are private in `local/m08-2026-09-22/integration/`,
`local/m08-world-identity/` and `local/m08-content/input-review-2026-09-22/`.

| Command | Expected | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | Build original bridge/host, coordinator and launcher without launching games | Final exit 0; launcher build has zero warnings/errors. Initial observer integration failed with C1083 because the new test's include root was wrong; corrected from `src/bridge` to `src`, then rebuilt successfully. Failed output retained. |
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` | All HOST/FIXTURE targets pass | Final exit 0; 12/12 targets, 26.93 seconds. Includes 2,963 network assertions, 280 worker assertions, 54 launcher assertions, 18 native-shaped content ABI checks, 26 editor decoder checks and 219 Python tooling tests. Earlier pre-observer run passed 11/11 in 25.17 seconds. |
| `ctest --test-dir build/win32 -C Release -R 'worker_host|native_content_abi_host|tooling_unit' --output-on-failure -V` | Affected tests pass after loading guard and final probe tests | Exit 0; 3/3 targets, 17.75 seconds, including 219 Python tooling tests. |
| `python -m unittest discover -s tests/unit -p test_m08_content_probe.py -v` | Native evidence admission/correlation fixtures pass | Exit 0; 15/15 tests, 2.412 seconds, Python 3.11.15. |
| `python -m unittest discover -s tests/unit -p test_multiplayer_launcher.py -v` | Changed/missing world file gates pass | Exit 0; 10/10 tests. |
| `cmake --build build/network-host --config Release --parallel 4` | Standalone network build | Initial exit 1 because a new test local shadowed an existing name under `/WX`; renamed the local, retry exit 0. Failed output retained. |
| `ctest --test-dir build/network-host -C Release --output-on-failure -V` | Protocol/policy/files/TLS pass | Exit 0; 1/1 target, 2,963 assertions. |
| `pwsh -NoProfile -File tests/network/cli-session.ps1 -Coordinator build/network-host/Release/SporeMP.Coordinator.exe -Output local/m08-world-identity/cli-01` | Match succeeds; changed-world and old-config probes fail precisely | Harness exit 0; changed `planetRecords.pkp` identity and legacy config each return exit 20; matching peers/reconnect succeed; server exits 0. |
| `python local/m08-native-2026-09-22/prepare.py` | Fresh closed backups and personal protection | Exit 0; two verified 27-file worker backups, empty creation trees and 29 protected personal files. No game started. |
| `python local/m08-2026-09-22/integration/verify-protected.py` | Closed source trees match exact prepared backups | Exit 0; both worker trees and all 29 personal files unchanged; no original game running. |

C++ and Python readers agree on all six hashes in each closed prepared profile;
both complete tuples match and all 12 source-file hashes remain unchanged. The
read-only C++ check also succeeds for all three current prepared workers, whose
tuples match. A changed file in each of the six positions is rejected in actual
TLS HOST tests. Same-name/same-size changes, missing files, open writers, oversized
files and directories in place of files are covered separately.

The bounded seeded parser exploration ran 16,000 mutations: 11,988 explicit
refusals, 4,012 structurally admitted variants, no unexpected exception and no
readiness promotion. Accepted arbitrary payload changes are not native semantic
validation; this is not exhaustive fuzzing. Exact command/results remain private.

## Static binding evidence

Pinned SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`.
Injector: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`.
Original executable: GOG GA 3.1.0.29 PE32, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.

`tools/native/invoke-static-audit.ps1` reused the pinned local Ghidra database.
Each listed audit exited 0; raw exported instructions/decompilation stay private:

- `m08-dependency-01`, addresses `4BC4A0,421E70,46DD10`: cast/ownership and conversion helpers.
- `m08-dependency-02`, addresses `8DE4B0,8DF700,8DE620,8DE590,4AE8D0,67DC90`: exact record lookup and distinct manager globals.
- `m08-dependency-03`, addresses `4BA2E0,46AB00`: both concrete result constructors.
- `m08-dependency-04`, address `433020`: concrete owned Release.
- `m08-world-20260922-01`, address `1021220`: existing home-planet getter.

The constructors assign vtable VA `0x013EED80`, whose Release slot is VA
`0x00433020`. LoadCreatureData VA `0x004BB500` transfers one owned reference on
the inspected success paths. These findings qualify the implementation's static
shape, not actual native invocation, lifetime or gameplay behavior. The SDK manager
getter differs from the original editor's manager helper; no guessed substitution
was introduced. The incomplete third-party creature-data reader was not adopted.

## Provenance and remaining gates

Environment: Windows 11 Pro 10.0.26200, AMD Ryzen 7 9700X, NVIDIA RTX 4080 SUPER,
33,407,430,656 bytes RAM. Pinned MSVC/SDK inputs remain those in
`config/dependencies.lock.json`; Ghidra is 12.1.3 with JDK 21.0.12.1+1.
The private base revision remains
`a33567d77ebee48442365c5cc6f1af4b93eb7803` with preserved M07/M08 working changes.
[Verification manifest](verification.json) records current source/artifact hashes
and exact command metadata. Historical .45 native evidence is not relabeled as
acceptance of .46 or the new network frame.

The next original-game experiment is one new native creature save, closed-file
comparison, exact-key native inspection and clean second-profile import/load.
Runtime capability behavior, complete dependencies, missing-part denial and
generated-terrain correlation remain unqualified. Native editor begin/commit/cancel
must preserve another original player's gameplay and publish only an authorized,
validated immutable version. Current scene/species fences remain in force; no
transaction path is claimed. All original M08 acceptance clauses remain unchanged.

Desktop input and original M08 game launches in this increment: **zero**. Current
desktop availability confirmation is required before foreground native tests, as
recorded in the user's retained working preference. Preparation and HOST results
do not close M08.
