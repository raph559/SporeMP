# M08 native content foundation

Status: **VERIFIED for the recorded Creature content/editor foundation**, 2026-09-22.
The C++ registry/cache now connects authenticated owner transactions, quarantined
PNG transfer, independent original-authority inspection, receiver dependency
preflight and immutable publication. Native source/authority/receiver agree on
23 rigblocks, 47 capability entries and nine installed part roots. Original
editor cancellation preserves the saved creation while another player continues.
Canonical terrain mapping and exact mismatch rejection are recorded.

[Completion](../evidence/2026-09-22-m08-completion/SESSION.md),
[all gates](../tests/engine/M08.md), [transaction protocol](m08-content-transactions.md),
[world identity](m08-world-identity.md), [native inspection](m08-native-content-probe.md).
The basic developer path requires locally approved native creations and the pinned
installed profile/fixed world bundle. Arbitrary content/planets, campaign species
replacement and M09 restart durability are not claimed. Offline tools below retain
their own HOST-only results; equal bytes alone never confer native readiness.

## Record inventory

`tools/content/spore_content.py` reads closed DBPF 3.0 and DBBF 2.0 archives with index
version 0.3. It checks index/count/offset arithmetic, shared index fields, compression
and record flags, duplicate keys, overlapping active spans, and resource budgets before
decoding any record. Historical unused bytes are not treated as active resources.
Supported RefPack 10FB/50FB streams must have consistent declared and decoded sizes,
valid back references, an end marker, and no trailing bytes. Other layouts fail explicitly.

Each native resource key maps to the SHA-256 and size of its **decoded record bytes**.
Text keys use lowercase `group!instance.type`, eight hex digits per component. The
SDK's `ResourceKey` fields/constructor instead use `instanceID, typeID, groupID`.
No pointers or native containers are serialized.

`record_set_sha256` hashes a JSON array sorted by key, with sorted object fields,
compact separators and ASCII encoding. Each element contains exactly `key`, `sha256`,
and `size`. Repacking, archive holes, index order and compression do not change this
identity when decoded records are identical. The archive hash is retained separately.
This is an exact byte identity; changed native serialization is not treated as equivalent.

`compare` rereads both archives and reports exact missing, changed and unexpected keys.
It does not accept a caller-provided inventory. Equal records still report readiness
false and native validation NOT_RUN. The separate native world qualification selects the fixed six-file bundle;
identical seeds or a matching empty package cannot pass its terrain gate.

Limits per invocation: 128 MiB per archive, 16,384 indexed records, 32 MiB stored or
decoded per record, and 256 MiB total decoded bytes. These are tooling limits, not
maximum supported creation sizes. There is no partial-inventory or permissive mode.

## Closed-copy tree comparison

`compare-trees` compares two operator-supplied directory copies under repository
`local/`. It fingerprints every file, including opaque PNG/event bytes, and parses
DBPF/DBBF by signature regardless of filename. The report retains the full before
and after file/resource inventories privately, plus added/removed/changed paths and
per-archive added/missing/changed native keys. Archive byte changes with equal decoded
records are reported explicitly. Equal keys in different archives stay separate;
the tool does not guess native search precedence or dependency ownership.

Each tree is limited to 512 files, 512 directories, 16 nested directory levels,
128 MiB per file, 512 MiB total stored bytes and 512 MiB total decoded archive bytes.
The existing archive/record limits also apply. Links/reparse paths, case-insensitive
path collisions, unsupported archives and a changed tree during the read fail closed.
Outputs must be fresh files outside both input trees, still within `local/`.

This command does not make a snapshot or prove the original game was closed. Use
verified quiescent backups. It detects ordinary concurrent tree changes but is not
an atomic filesystem snapshot or a sandbox against malicious same-user replacement.
An opaque PNG's hash does not validate its embedded creation data; changed records
are discovery evidence, not an approved dependency set. Native validation remains
NOT_RUN, dependency status UNKNOWN and readiness false.

## Private quarantine

`quarantine` requires 1–128 explicitly selected resource keys from a fully validated
archive. It writes decoded blobs using SHA-256 filenames inside ignored
`local/m08-content/cache/`. A manifest binds keys to blob hashes/sizes; the hash of its
canonical bytes names the immutable candidate directory. Equal blobs within a candidate
share one file. Changed records produce a new candidate identity.

Files are created exclusively in a fresh `.incomplete-*` directory and flushed. The
manifest is written last before directory publication. An interrupted operation leaves
an uncommitted directory. Reuse verifies the manifest, exact directory entries, sizes
and hashes; corrupt existing candidates are refused without replacement. This offline
commit marker does **not** establish M09 crash/power-loss durability. Concurrent writers
can leave a redundant incomplete directory, which never grants readiness.

No archive-derived filesystem paths are accepted. Resource selectors have a strict hex
grammar; blob names are generated hashes. CLI reports must remain inside repository
`local/`. Existing symlinks, junctions, reparse ancestors and alternate data streams are
rejected. Inputs must be regular, bounded files unchanged during the read. Existing
evidence reports are never overwritten.

Input/cache paths are local operator-controlled paths. This is **not a network upload
handler** or a sandbox against malicious same-user filesystem replacement. Uncompressed
arbitrary bytes have no intrinsic checksum: the inspector identifies them but cannot
certify their native meaning. Nothing is installed in game search paths, live profiles
or native databases. All candidates retain dependency status UNKNOWN, native validation
NOT_RUN, transfer approval false and readiness false, with no promotion switch. Retail
assets and private records remain local; this tool does not publish or transfer them.

## Pinned representation evidence

The [native creation session](../evidence/2026-09-22-m08-native-creation/SESSION.md)
now distinguishes the actual representations. The original editor returns a logical
CRT key but saves BEM, derived data, metadata and PNG records in EditorSaves. Placing
only its original-produced PNG into the creation-free disposable peer leads original
SPORE to import a literal CRT into Pollination on startup, under a different local
key. Both original views/test-drive and native scalar inspections agree for the
recorded creature. PNG bytes match; BEM/CRT model bytes do not. This is evidence for
this fixture's native conversion, not permission to treat arbitrary images as
complete, safe, transferable content or reuse a source's local resource key.

SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`.
[`Thumbnail_cImportExport.h`](https://github.com/Spore-Community/Spore-ModAPI/blob/cbf9206b9a823f0911cd9be0217104a49d72380b/Spore%20ModAPI/Spore/App/Thumbnail_cImportExport.h)
describes Model-in-Picture PNG encoding of a resource through its resource factory,
associated pollen metadata, and separate decode/import operations. A PNG can carry
creation data; visible pixels or PNG validity do not prove complete dependencies,
successful native loading or authorized gameplay properties.

`Spore/Editors/cEditorResource.h` describes saved editor models with part references.
`cCreatureDataResource.h` exposes rigblocks/capability data; its `TYPE` is a class
identifier, not a verified portable schema or file extension. `Spore/CommonIDs.h`
identifies `.crt`, `.bem`, `.gmdl`, `.raster`, `.png` and `.prop` keys.
`Spore/Simulator/cPlanetRecord.h` retains generated-terrain keys and additional planet
state. These declarations guide investigation; the inspector does not parse these
native payloads or trust client capability numbers.

The sampled archived worker has 2,617 Pollination records, including 2,588 `.summary`
records (`Spore/Sporepedia/OTDBParameters.h`), and no `.crt` record. Its EditorSaves
package is empty. GraphicsCache has 221 generated-resource records. Its world `.spo`
has 39 records, 35 compressed. Copying Pollination or assuming this checkpoint contains
a new creature would be premature. These are HOST reads of historical native-produced
artifacts, not new original-game execution. [Session and hashes](../evidence/2026-09-21-m08-content-foundation/SESSION.md).

Container/RefPack references are SporeModder-FX commit
`f60de8aa0ef4bd83768b07a7acf0f718470f8cd6`:
[`DatabasePackedFile.java`](https://github.com/Spore-Community/SporeModder-FX/blob/f60de8aa0ef4bd83768b07a7acf0f718470f8cd6/src/sporemodder/file/dbpf/DatabasePackedFile.java),
[`DBPFIndex.java`](https://github.com/Spore-Community/SporeModder-FX/blob/f60de8aa0ef4bd83768b07a7acf0f718470f8cd6/src/sporemodder/file/dbpf/DBPFIndex.java),
[`DBPFItem.java`](https://github.com/Spore-Community/SporeModder-FX/blob/f60de8aa0ef4bd83768b07a7acf0f718470f8cd6/src/sporemodder/file/dbpf/DBPFItem.java), and
[`RefPackCompression.java`](https://github.com/Spore-Community/SporeModder-FX/blob/f60de8aa0ef4bd83768b07a7acf0f718470f8cd6/src/sporemodder/file/dbpf/RefPackCompression.java).
These are format research references, not a new build/runtime dependency. The inspector
adds explicit bounds and exact consumption, and handles DBBF offsets/index sizes as
64-bit values. The observed native files fit those fields without truncation.

## Native binding investigation queue

These are the pinned SDK's second SelectAddress entries for March2017. All four
have a [static executable audit](m08-editor-audit.md). The .46 isolated developer
inspection implements `LoadCreatureData` with additional static ownership/Release
qualification; the recorded original save and peer import now pass its native call.
The direct ImportPNG and resource lookup bindings subsequently ran in native05–11.
DecodePNG and direct EditorRequest::Submit remain research candidates; their
runtime ABI/lifetime and behavior remain unqualified.
Target: GOG GA
3.1.0.29 PE32, executable SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.

| Candidate | SDK signature and source | Candidate VA | Missing evidence |
|---|---|---|---|
| `Thumbnail_cImportExport::DecodePNG` | Member `bool(IStream*, ThumbnailDecodedMetadata&, IStreamPtr&)`; `SourceCode/App/App.cpp`, `AddressesApp.cpp` | `0x5FBB90` | Runtime ABI, engine-thread entry, stream/metadata lifetime, bounded decoding and dependencies. |
| `Thumbnail_cImportExport::ImportPNG` | Member `bool(const char16_t*, ResourceKey&)`; same sources | `0x5FC3C0` | Native06/08/11 qualify true import/new key and subsequent load. False-with-existing-key is not success. Only independent native attestation plus readiness can authorize publication. |
| `cEditor::LoadCreatureData` | Static `bool(ResourceKey*, cCreatureDataResource**)`; `Editors/Editor.h`, `SourceCode/Editors/Editor.cpp`, `AddressesEditors.cpp` | `0x4BB500` | Native source/authority/receiver inspection and owned release pass; copied properties, nine native part roots and original test-drive behavior agree within the pinned profile. |
| `EditorRequest::Submit` | Static `bool(EditorRequest*)`; `EditorRequest.h/.cpp`, `AddressesEditors.cpp` | `0x5A92C0` | Engine context/reference ownership, begin/save/accept/cancel, player-local pause. Simulator entry uses separate routing. |

All new engine dependencies belong in `src/bridge`. SDK declarations alone do not prove
ABI, lifetime or behavior. The older entering-editor tutorial uses different field
names from the pinned headers; do not copy it literally.

## Next milestone

M09 adds durable checkpoints/content versions and recovery after interrupted
commits or server restarts. Current mappings/cache are in memory. Other native
stages, world locations and arbitrary dependency profiles retain their original
milestone gates; M08 does not authorize model replacement through motion packets.
