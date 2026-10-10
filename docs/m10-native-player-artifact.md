# M10 selected native player artifact validation

`tools/native/native-player-artifact.py` reads a **closed original-game schema-3 export**, checks its bounded format and corresponding native observations, and creates the M10F schema-3 input expected by the native reader. It does not execute the game, rewrite the archive, authorize a player, publish a save, or commit a transfer.

The implementation supports the pinned GOG executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`, and native serialization version `0x00250003`. Executable/content/loader compatibility and authenticated source ownership remain prerequisites enforced by the supervising host and coordinator.

## Inputs and producer modes

All paths are local regular files without reparse points or hard-link aliases. The database is limited to 1 MiB, each trace snapshot to 128 MiB and 200,000 rows, individual trace rows to 64 KiB, and the canonical receipt to 16 KiB. Inputs are read again and compared before outputs are written. The output directory must not already exist; its parent must exist.

JSON duplicate fields are rejected. The native trace has one narrow compatibility exception: its common envelope and some pinned event payloads both emit `epoch`. An identical repeated integer epoch is accepted only in native trace rows. A conflicting epoch, any other duplicate, or any duplicate in the canonical receipt or host trace is rejected.

The caller supplies the expected database SHA-256, 32-character local worker generation, export request, canonical player (1 or 2), native slot (1 or 2), original unique game ID, and canonical home. These values must agree with the export's root and the `native_player_graph_selection` event. The selected slot may be a secondary visiting player; the native census must still include and preserve every source player.

Two producer modes are supported:

- **`closed`**: the native trace ends with a healthy detach, and the corresponding host records successful game and worker exits. This is the default for offline evidence review.
- **`paused`**: the database has been closed and flushed while the original source process remains alive under its measured native pause. The caller supplies immutable native/host trace snapshots, a canonical source-fence receipt, and the receipt SHA-256 pinned by the authenticated coordinator frame. Game exit is not required in this mode.

`--confirmed-closed` acknowledges that the database and supplied trace snapshots are closed and immutable. The flag alone provides no pause or authorization evidence.

Historical prototype exports without a selected-export event are deliberately rejected for approval. Their bytes can be inspected independently for offline research; that does not upgrade their provenance to this contract.

### Paused-source receipt

`src/network/protocol.cpp::transfer_witness_json` defines the exact compact UTF-8 JSON followed by LF. The validator checks its complete field set, order, types, integer ranges and exact bytes. The `state` field is absent because the authenticated wire frame's `state` is the SHA-256 of this receipt. The source receipt has numeric operation 1 (`source_fence`), a nonzero preparation and challenge, and an all-zero approval hash with zero approval bytes. Host approval is produced afterward.

The receipt binds:

- Transfer, canonical player, revision, avatar generation, worker, worker generation, session, scene and baseline.
- Original source player/avatar IDs, native slot, unique game ID and canonical home.
- Complete database length and SHA-256.
- Retained native process PID and creation time.
- Pause request and input epoch; application and AI counters; zero/nonzero native delta counts; elapsed milliseconds and pause counts.

The validator requires at least 3,000 ms with advancing application updates, unchanged AI counter, nonzero zero-delta observations, no nonzero deltas, and one owned pause. The same-request `native_quiescence_observed` event must precede the selected export. The successful export must precede `network_native_transfer_receipt`, whose receipt and artifact hashes must match. A release, rejection, renewed observation, unresolved disposal or trace closure after that observation rejects paused mode.

**The tool checks receipt structure and correlation.** The coordinator must independently authenticate the received frame, compare its receipt digest and scope, hold the matching process identity and generation, keep the transfer in its source-fenced phase, and confirm that the source remains paused. A caller-created JSON file cannot advance coordinator ownership.

## Database and native state checks

The existing bounded DBPF parser handles entry bounds, overlap, duplicate keys and supported decompression. This tool additionally requires exactly six records:

| Record | Resource key (`group!instance.type`) |
|---|---|
| Selected root | `53504d50!00000001.53504d47` |
| Original persistent stage | `53504d50!00000001.53504d53` |
| Class index | `00000000!0179d304.0179d310` |
| One `cPlayer` | `00000000!0179d304.02c21781` |
| One `cCreatureAnimal` | `00000000!0179d304.018eb45e` |
| One collectables object | `00000000!0179d304.03a3ac9f` |

The class index must describe exactly those three objects. Object frame sizes, serialization identities, root references, native noun IDs, inheritance prefixes and attribute bounds are checked. The player collection reference must resolve to the single collectables object. Native owner references must be null. The exported avatar's `mNeighbors` must be empty and `mHerd` null; original source scene references must remain unchanged during the export, and both temporary descriptor substitutions must be detached successfully.

All before/after source census rows must match, including an unselected primary. The selected player's exact item status tuples, ordered unlocks and four progression values must match the artifact. Native progression values printed with nine significant digits are reconstructed as binary32 before comparison. The artifact's unlock-point value is included in the M10F data for the destination's independent comparison; the source checkpoint census currently does not emit that scalar separately.

The persistent stage record must be the exact native 188-byte serializer with its seven known attributes. It supplies ten words: brain, normalizing scale, flaps, ability mode, DNA, two deprecated-ability words, and three learned-ability words. All ten must match the same-export native stage event. Source stage, idle counter and world-pending guards must pass; a secondary export also requires the destination primary stage to remain unchanged.

Herd factory metadata comes from the same-export native herd event with matching species identity. Native serialization tokens are used only to validate the three-object graph and are never emitted into approval metadata or the M10F sidecar.

### Deliberate limits

- Original object-frame checksum values are bounded but not independently recomputed.
- The concrete-reference audit covers the typed owner, scene and collection fields plus references to all classes observed in the original eight-class graph. It is not a universal proof about unknown pointer types or external asset dependencies.
- An exact selected export, pause receipt or approval sidecar does not qualify destination native reading, ongoing simulation, save/reload, routing, once-only outcomes or crash recovery. Those remain independent native/coordinator gates.
- Fixture tests use synthetic values and establish parser behavior. They do not establish original-game compatibility or native transfer acceptance.

## M10F schema 3

All integer words are little-endian. The header is exactly 200 bytes, followed by bounded collection tuples with no trailing bytes.

| Offset | Value |
|---:|---|
| 0 | Magic `0x4630314d` |
| 4 | Schema `3` |
| 8 | 64 lowercase ASCII characters of the complete database SHA-256, without NUL |
| 72 | Database byte length |
| 76 | Native serialization version `0x00250003` |
| 80, 84 | Original source player and avatar native IDs |
| 88, 92 | Unique game ID and canonical home |
| 96, 100, 104 | Species instance, type and group |
| 108, 112 | Age and animal archetype |
| 116, 120, 124, 128 | Herd archetype, archetype group, signed-generation bits and personality |
| 132, 136, 140, 144 | Goal progress, goal total, social and combat binary32 bits |
| 148, 152, 156 | Item status count, unlock count and unlock points |
| 160–196 | Ten persistent-stage words in the order listed above |
| 200 onward | Status triples `(instance, group, status)`, then ordered unlock pairs `(instance, group)` |

Status triples are sorted by group and then instance. Both counts are at most 1,024, so the exact length is `200 + 12 * status_count + 8 * unlock_count`, with a maximum of 20,680 bytes. Canonical player and native slot belong to the authenticated receipt; the binary sidecar does not add either field.

## Invocation and outputs

For a paused source, the supervising workflow supplies its registered diagnostics database and immutable trace snapshots. The following PowerShell example uses variables supplied by that workflow; the tool does not discover or trust arbitrary live runs:

```powershell
python tools/native/native-player-artifact.py `
  --artifact $artifact --artifact-sha256 $artifactSha `
  --trace $nativeTraceSnapshot --host-trace $hostTraceSnapshot `
  --generation $localGeneration --request $transferId `
  --canonical-player $canonicalPlayer --native-slot $nativeSlot `
  --unique-game-id $uniqueGameId --canonical-home $canonicalHome `
  --producer-mode paused --source-witness $witness `
  --source-witness-sha256 $authenticatedWitnessSha `
  --confirmed-closed --output-directory $freshOutputDirectory
```

For a closed offline producer, use `--producer-mode closed` and omit both witness arguments. The selected export and successful process closure requirements still apply.

Success exits 0 and writes:

- `player-import.expected`: exact M10F schema-3 bytes.
- `approval.json`: data-only metadata including complete artifact/approval hashes and sizes, selected identity, stage words, source trace hashes and the paused-source witness when supplied. It explicitly retains `native_transfer_qualified: false` and `ownership_authorized: false`.

Failure exits 1 and reports a bounded diagnostic. Outputs are created only after validation in a fresh directory; callers must consume the complete successful pair and bind both immutable hashes into the authenticated transfer preparation.

Run the focused HOST/FIXTURE checks with:

```powershell
python -m unittest discover -s tests/unit -p test_native_player_artifact.py -v
```

These cover malformed container/class/root/stage data, unexpected references, duplicate identities, native census changes, secondary-stage protection, process/generation mismatches, exact sidecar layout, path aliases, canonical receipt bytes, missing receipt authentication pins, failed pause measurements, and receipt/export order.
