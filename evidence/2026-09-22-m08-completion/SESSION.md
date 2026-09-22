# M08 completion: native content, canonical terrain and editor transactions

Date: **2026-09-22**. Status: **VERIFIED for the recorded Creature content/editor foundation**.
Evidence classes: **BUILD, HOST/FIXTURE (including real local TLS), NATIVE**. The original
M08 clauses remain unchanged. This qualification does not complete the campaign,
arbitrary creation support, normal-player editor sharing or M09 restart durability.

## Acceptance

| Original requirement | Result and evidence |
|---|---|
| A new native creation loads consistently on authority and a clean second client | Original named/painted Alderbrook derivatives are saved through SPORE. Native01–04 establish BEM save versus CRT import, including a creation-empty receiver. Native07–11 transfer two additional fresh PNG candidates over authenticated TLS to original authority/receiver processes. Both original imports return true and allocate distinct local keys absent from their preceding backups. Source, authority and receiver return exactly equal 23 rigblocks and 47 capability entries. Original model, paint, displayed abilities and test-drive movement were inspected, including the received orange model in native09. |
| Gameplay properties come from the original engine | The authority imports and loads the creation through qualified original bindings, copies bounded native rigblocks/capabilities and resolves its nine part roots. A changed client capability is refused as `properties_mismatch` in native08. Original editor/test-drive behavior supplies the bounded behavioral evidence. The imported model is not installed as a campaign avatar and its campaign combat is not claimed. |
| Missing dependencies and terrain mismatches block readiness precisely | Native11 resolves a deliberately absent part through both original resource-manager getters: exact and mapped lookups all fail for `40626000!00fefefe.00b1b104`. This negative input comes from an explicitly labeled HOST adversarial authority. The native receiver reports the exact key before PNG fetch/import, the owner receives the error, commit returns `not_ready`, and publication remains zero. A fresh real authority then supplies the actual nine requirements; the receiver resolves them before a successful original import. Separately, an actual disposable-profile terrain-record byte change is rejected by NativeHost before process creation, with exit20 and `canonical_world_mismatch:Games/Game0/PlanetScripts.pld`. All profile bytes are restored and checked. |
| Canonical generated terrain and world records | Complete closed original fixtures are copied and hash-verified by the existing worker provisioner. Six world files match across source/authority/receiver. Native05/06/08/11 correlate runtime terrain `4084a100!28ca32e7.011989b7` with persisted property record `4084a100!28ca32e7.00b1b104` in `PlanetScripts.pld` through original exact/mapped lookups. The mismatch experiment changes that record while retaining the Satiria bytes and file sizes. This qualifies the fixed canonical bundle, not dynamic arbitrary planets. |
| Editing does not pause every player or publish drafts | Native07/08 have a separate original editor and original authority/receiver. In native08, the other player moves 1.00224 world units and jumps/lands while a changed draft is open. Native Cancel is observed; the entire saved EditorSaves archive remains byte-identical. Transaction cancellation publishes nothing. The valid closed source is independently imported/attested, all live participants acknowledge native readiness, and one immutable version is committed. Duplicate commit is refused; a later cancel preserves the current version. Further native movement/jump/landing passes after publication. |
| Corrupt, oversized and unsafe content fails safely | Bounded parser, PNG/CRC, transport, cache, manifest and path tests pass before native intake. Native receiver paths are generated from locally approved SHA-256 values, never peer paths. Native imports are restricted to the reviewed original-produced candidates. This is not arbitrary untrusted Internet PNG acceptance. |

## Native sessions and outcomes

The [verification manifest](verification.json) checks **ten closed original processes**
across native07–11. Each actor trace has a healthy footer and successful editor
listener cleanup; each game and supervisor exits0. Every run preserves the 29
protected personal files and all six canonical world files. Earlier native01–06
remain documented in the [creation](../2026-09-22-m08-native-creation/SESSION.md)
and [resource/import](../2026-09-22-m08-native-resource-import/SESSION.md) reports.

| Session | Build | Result |
|---|---|---|
| Native07 | 0.0.49 | Orange native source creation and successful concurrent movement/jump while editing. A later test-drive action fails after the original authority starves; the death screen is inspected and retained. Its five-minute draft expires without publication. |
| Native08 | 0.0.49 | Changed draft Cancel, concurrent gameplay, original authority/receiver import, altered-property/world/missing-witness refusals, one immutable commit and continued movement/jump. |
| Native09 | 0.0.49 | The receiver's imported orange creature is opened in the original 3D editor/test drive. Movement and displayed abilities are inspected; native data again matches the source. No new save. |
| Native10 | 0.0.50 | A fresh purple creation is saved, inspected, closed and prepared as a new candidate. |
| Native11 | 0.0.50 | Actual native missing-part refusal before import, then real-authority transfer with native preflight, one immutable publication, duplicate refusal and continued movement/jump. |
| World mismatch | 0.0.50 | Actual-profile NativeHost guard refuses the modified persisted terrain record; **HOST**, no original process created. Complete profile restored. |

Native08 publishes version
`d8316dfa23e62f60bad0d681d6dbd499f7962964bf01061de123cd991a876c91`
from PNG `8ebdf46d2054dea2c75eb50f775478b8e446e0ded77825a4a55e0c2e690536d1`.
Native11 publishes version
`e824c00d2d6e8895507779c029255f2df0f4e6a50abb1f6e35877a7affb16e79`
from PNG `f0ea2c1326d3e0c278663d1c31474dae58c66cf83884905fcc9e2869a30cb3d8`.
The shared authority-derived native-property digest is
`3d7917d8b5745a2ddea177d1bc25f9131e760f13a8a0c064219fc73c1b0e6789`.
Global version, PNG identity and local native resource key have separate meanings.

## Commands, checks and provenance

PowerShell, private checkout. Build/test commands launch no game:

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure -V
python local/m08-2026-09-22/content-cli-smoke-050.py
python local/m08-native-2026-09-22/integration-050.py native-11 prepare-next
pwsh -NoProfile -File tools/native/start-m06-server.ps1 -RunName m08-native11 -Port 27068
python local/m08-native-2026-09-22/integration-050.py native-11 launch 03 local/m06-network/m08-native11/private/player-2.conf
python local/m08-native-2026-09-22/native-final-02.py
python local/m08-native-2026-09-22/integration-050.py native-11 launch 01 local/m06-network/m08-native11/private/authority.conf
python local/m08-native-2026-09-22/native-positive.py
python local/m08-native-2026-09-22/integration-050.py native-11 action after-content
python local/m08-native-2026-09-22/integration-050.py native-11 close 03
python local/m08-native-2026-09-22/integration-050.py native-11 close 01
python local/m08-native-2026-09-22/world-mismatch.py
python local/m08-native-2026-09-22/seal-completion.py
```

Final Release build exits0; **14/14 CTest targets pass in 32.77s**, including
244 Python tooling tests, 190 registry checks, 937 content transport/TLS checks
and 2,963 existing network checks. Six executable/private-pipe/TLS smoke checks
pass. Frozen .50 source set:
`abf90e82788a11aa11bbe840d3e219c4c0857fca09c50f0d6c275e71647a881b`.
The manifest pins 191 sources through that digest and nine payload artifacts;
private frozen manifests retain every source hash and exact command output.

Successful final native preparation, launches, inspection, positive transfer,
actions, closure and sealing exit0. Native11's first negative driver exits1:
the notification preceded receiver authentication, so no native event arrived.
The second negative phase passes. Its owner pipe subsequently exits2 when the
fixture authority disconnects; reusing that closed pipe fails, so the positive
phase uses a fresh authenticated owner and exits0. These failed driver outcomes
are retained, not relabeled as successful native trials. Initial .50 CTest also
retains a stale schema6 assertion failure; the corrected schema7 suite passes.

Pinned GOG GA3.1.0.29 executable SHA-256:
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`; injector
`26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`.
Windows11 Pro10.0.26200, Ryzen7 9700X, RTX4080 SUPER driver32.0.15.9649.
Exact content identity, OS/hardware, artifact hashes and retained failures are
in the verification manifest. Raw traces, process captures, saves, extracted
creations and credentials stay under ignored `local/`.

Capture frames were inspected, not merely containers. Native10 needs two clips:
the first ends before editing; the second covers the source editor/test-drive/save
return. Native11's final received catalog is inspected live after the receiver's
240-second recording ends; recorded transfer/continuation views are separate.
Frame timing/performance is not measured by these recordings.

## Remaining scope

The registry/cache is in memory; M09 owns durable version/checkpoint persistence,
restart and interrupted-transaction recovery. The basic owner editor controls use
private developer pipes, and native receivers require explicit local approval in
disposable profiles. The normal launcher has world-file checks, not a new creation
sharing UI. The fixed installed-content profile covers the observed native part
roots and their installed assets; arbitrary mods, expansions, other planets,
campaign species replacement and all-stage editors require further qualification.
The complete M00–M20 plan and original-game authority requirement remain intact.
