# M08 canonical-world admission increment

This increment closes a concrete gap in the existing Join path: matching
`Satiria.spo` alone previously permitted different companion world files. The
coordinator now compares six declared file identities during TLS session
admission. A failed comparison cannot obtain a scene baseline or submit gameplay
intentions. The launcher computes the identities from local files before opening
SPORE, and NativeHost independently rechecks them against its session config.
TLS authenticates the pinned server before any credentials are sent. As in the
existing handshake, the coordinator evaluates identity compatibility before the
player/authority credential; a wrong-credential peer can therefore receive a
world-file mismatch diagnostic, but cannot receive Welcome or a scene baseline.

**Current qualification: native canonical-bundle loading and resource mapping,
plus HOST/TLS and actual-profile prelaunch mismatch refusal.** Original authority
and receiver in native08/11 load the complete copied canonical fixture and resolve
runtime terrain `4084a100!28ca32e7.011989b7` to its `.prop` record
`4084a100!28ca32e7.00b1b104` in `PlanetScripts.pld`, using both original exact/mapped
lookup paths. All six files remain byte-identical after closure.

The final .50 mismatch experiment changes one byte inside that persisted record in
a backed-up disposable profile while keeping the file size and Satiria bytes.
NativeHost returns20 with `canonical_world_mismatch:Games/Game0/PlanetScripts.pld`
before creating a game process; the entire profile is restored and verified.
[Completion and limits](../evidence/2026-09-22-m08-completion/SESSION.md).
This qualifies the fixed bundle, not dynamic planet streaming or all generated worlds.

## Fixed bundle version 1

Paths are relative to the actual Windows account's `%APPDATA%/Spore` directory.
They are compiled allowlisted identities; no peer supplies a filesystem path.

| Index | Required path | Config field |
|---|---|---|
| 0 | `Games/Game0/Satiria.spo` | `world_satiria_sha256` |
| 1 | `Games/Game0/planetRecords.pkp` | `world_planet_records_sha256` |
| 2 | `Games/Game0/planetRecords.pkt` | `world_planet_records_temp_sha256` |
| 3 | `Games/Game0/PlanetScripts.pld` | `world_planet_scripts_sha256` |
| 4 | `Games/Game0/stars.db` | `world_stars_sha256` |
| 5 | `Planets.package` | `world_planets_sha256` |

All six names exist in both closed, backed-up M08 profile baselines. Pinned SDK
commit `cbf9206b9a823f0911cd9be0217104a49d72380b` additionally identifies
`planetRecords.pkp` and `.pkt` as the main and temporary planet databases in
`Simulator/SubSystem/StarManager.h`; `Resource/Paths.h` identifies the Planets
package. `Simulator/cPlanetRecord.h` exposes a generated terrain resource key at
offset `0x1A4`, and its generator requests a `.prop` key. Those declarations originally supplied investigation leads; native05/06/08/11
subsequently qualify the recorded home terrain/property relationship. This does
not prove that this bundle covers every terrain type or other installed profile. The previously
inspected Planets package had zero active records.

The manifest is the ordered tuple of six SHA-256 values under bundle version 1.
Each digest covers the entire closed file, including native archive structure;
it is deliberately stricter than decoded-record equivalence. A repacked archive
may therefore be rejected even if its decoded records are identical. The
existing `fixture_sha256` retains its original meaning, the complete Satiria
file hash, and must equal bundle entry 0. Existing build, executable, installed
content and native baseline acknowledgment checks remain required.

The C++ reader opens all six files with read sharing only, denies writers and
deletion while hashing, rejects nonlocal/reparse/nonregular paths, and enforces
128 MiB per file and 512 MiB per bundle. The launcher performs bounded stable
reads; NativeHost performs the independent locked check before game creation.
Handles close before launching SPORE so the original engine can use its normal
databases. This establishes prelaunch bytes, not a guarantee about subsequent
native writes, runtime resource resolution, or an unchanged terrain map after
loading. The guard itself does not copy or repair data. Developer provisioning separately
copies the complete closed original fixture and verifies every file before use.

## Protocol and configuration

Wire protocol remains 1, now **schema 7 and 600-byte frames**; schema5 introduced
the world fields and schema6/7 added the separate content transaction operations. All schema 4
fields at offsets 0–399 retain their positions. Six 32-byte digests occupy
400–591, the explicit world mismatch index occupies 592–595, and 596–599 are
reserved zero bytes. A mismatch index is legal only with
`reject / world_mismatch`; values outside the six-entry allowlist are rejected.
No paths or native structures cross the wire.

Server and peer configuration schema is **2**. Every world digest is mandatory
and nonzero. Older configurations and wire schemas are rejected; there is no
implicit Satiria-only fallback. The launcher, coordinator config writer,
`start-m06-server.ps1`, CLI regression and TLS impairment HOST fixtures produce
the new schema. Existing saved private configs need regeneration with the new
build. Invitations keep their endpoint/certificate/token format.

The handshake distinguishes a world-file mismatch from the existing build or
installed-content error. For example, an altered planet database returns
`canonical_world_mismatch:Games/Game0/planetRecords.pkp` and exit 20 from the
coordinator probe. Missing/unreadable local files identify the exact allowlisted
path and prevent startup. NativeHost records
`canonical_world_prelaunch_validated` only after its actual-profile check; the
record explicitly carries `runtime_terrain_qualified:false`.

`SporeMP.Coordinator.exe --world-identity --root ABSOLUTE_SPORE_PROFILE_ROOT`
performs the same read-only C++ check and prints the six config fields. Its
output is labeled HOST and native validation NOT_RUN. It launches no game.

The frame grows from 400 to 600 bytes. The unchanged bounded queue and TLS
batching path passes the existing 2,048-entity HOST baseline, and native08/11 qualify content transfer with continued shared movement/jumps
on the new frame. A full repeat of M07 packet impairment/combat acceptance on .50
is **NOT RUN**; earlier M06/M07 evidence remains tied to its original builds.

## Validation on 2026-09-22

| Command / observation | Expected | Observed |
|---|---|---|
| `cmake --build build/network-host --config Release --parallel 4` | Successful standalone build | Initial exit 1: a new test variable shadowed an existing local and `/WX` rejected it. Renamed the variable; retry exit 0. |
| `ctest --test-dir build/network-host -C Release --output-on-failure -V` | Protocol, policy, files and real TLS pass | Exit 0; 1/1 target, 2,963 assertions. |
| `python -m unittest discover -s tests/unit -p test_multiplayer_launcher.py -v` | Launcher preflight/readiness gates pass | Exit 0; 10/10 tests. |
| `pwsh -NoProfile -File tests/network/cli-session.ps1 -Coordinator build/network-host/Release/SporeMP.Coordinator.exe -Output local/m08-world-identity/cli-01` | Exact world mismatch refusal without game startup | Exit 0; changed planetRecords file identity rejected with exact path and probe exit 20; legacy config rejected with exit 20; matching peers authenticate; server closes with exit 0. |
| C++ and Python readers over both closed profile copies | Same digests from both implementations; sources unchanged | Exit 0 for each C++ read; six digests agree with Python for each profile; both profiles' tuples agree; all 12 source-file hashes unchanged. |
| Read-only .46 C++ check of all three current disposable profiles | Required files exist and the declared fixture is still consistent | Exit 0 for all three; all six digests agree across the three accounts. No game starts. |

File tests cover each absent component, actual SHA-256 known vectors, same-name
and same-size changed bytes, denied open-writer sharing, oversized input and a
directory in place of a file. Policy tests reject every absent digest before
Welcome. Actual TLS tests reject each of the six changed identities and retain
the exact file index. Launcher tests verify every missing companion file stops
before networking or game creation, and that Python field ordering matches the
native schema. Legacy build/owner/generation/reconnect and entity field offset
checks remain in the same passing network target.

Private evidence is under `local/m08-world-identity/`: `cli-01/result.json` and
`closed-baselines-01/verification.json` retain commands, exits, hashes and exact
local paths. `integration-01.json` records source hashes and build/test commands;
`current-profiles-01.json` records the read-only three-profile follow-up. The
standalone coordinator hash for the initial runs is
`d113b56d3d54dd58104086dd7abd1366d9119316e0e56b1b2b6f0424db791882`.
The integrated bridge/NativeHost .46 build and original-game testing are tracked
by the parent M08 session; they are not implied by the standalone results above.

## Qualification boundary

The native mapping and actual-profile refusal above close the fixed-bundle M08
gate. The whole original fixture is provisioned, not regenerated from a seed.
Dynamic planet synchronization, arbitrary worlds and durable restore belong to
the later location/stage/persistence work. This guard does not authorize a species
or model mutation through an ordinary motion packet or weaken owner/scene fences.
