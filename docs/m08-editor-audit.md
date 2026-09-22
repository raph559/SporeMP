# M08 original editor and creation import investigation

Current completion: [M08 native acceptance](../evidence/2026-09-22-m08-completion/SESSION.md)
and [transaction protocol](m08-content-transactions.md). Original loader, owned
release, ImportPNG and exact/mapped resource lookups are qualified for the recorded
creatures on .46–.50. Direct DecodePNG/Submit calls remain unqualified. The dated
investigation sections below preserve their original evidence boundaries.

Initial audit 2026-09-21; updated 2026-09-22. Evidence class: **STATIC**, not
original-game execution or runtime ABI qualification. The .46 isolated developer
probe now implements a guarded `LoadCreatureData` invocation with an additional
static ownership/Release audit; see [the probe contract](m08-native-content-probe.md).
The recorded .46 native loader calls and passive original request/accept/cancel
observations have since run successfully; see the
[native session](../evidence/2026-09-22-m08-native-creation/SESSION.md). The original
game also imports its own PNG on startup in the clean disposable profile. This
does not qualify a new direct ImportPNG/DecodePNG/Submit binding or a multiplayer
editor transaction. The static observations below retain their original scope.

The inspected GOG GA 3.1.0.29 PE32 executable has preferred base `0x400000` and SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
SDK source is pinned to `cbf9206b9a823f0911cd9be0217104a49d72380b`. Addresses below are
preferred virtual addresses, not portable offsets or approval to invoke them.

## Inspected candidates

| Entry and pinned declaration | Static observations | Remaining qualification |
|---|---|---|
| `0x5FBB90`, `Thumbnail_cImportExport::DecodePNG`; member `bool(IO::IStream*, ThumbnailDecodedMetadata&, IStreamPtr&)` | First argument at `[ESP+4]`, receiver used through `ECX+0x88`; calls the SDK-labeled PNG image-data reader; exits pop 12 bytes. Entry bytes `8b44240481ecb8000000`. This is consistent with the declared x86 member ABI. | Real receiver/stream/metadata lifetime, input size and allocation behavior, engine-thread use, decoding success and dependency completeness. |
| `0x5FC3C0`, `Thumbnail_cImportExport::ImportPNG`; member `bool(const char16_t*, ResourceKey&)` | Receiver arrives in ECX; two stack arguments and `RET 8`. Output key starts as all `FFFFFFFF`. The lookup branch at `0x5FC510` copies an existing three-word key at `0x5FC515–0x5FC524`, then returns false at `0x5FC553`. The later path calls the resource key generator and `GetPackageForSaveDirectory(0x011AC19D)`, and includes resource/database and message calls. | Actual original import, collision semantics, resource writes, closed-file outcome, engine-thread context and returned-key mapping. A false result cannot by itself mean the key is absent; a true result does not establish gameplay validity. |
| `0x4BB500`, `cEditor::LoadCreatureData`; static `bool(ResourceKey*, cCreatureDataResource**)` | Arguments are read from the caller's stack frame; plain `RET` is consistent with cdecl. At `0x4BB52D`, a derived lookup key uses type `0x0F43029A`. The fallback decompilation contains original editor-model loading/conversion and resource-cache calls; group variant handling recursively changes the group bits. | Returned ownership and lifetime, native capability observation and actual original behavior. This may populate native caches, so it is not qualified as a read-only inspector. `0x0F43029A` is an observed lookup discriminator, not a portable payload schema; the SDK class ID `0x03E1C247` must not substitute for it. |
| `0x5A92C0`, `EditorRequest::Submit`; static `bool(EditorRequest*)` | Request is a stack argument; plain `RET` is consistent with cdecl. Native code writes the prior game mode and default validation fields, sends `0xB03BC30C`, constructs a message containing `0x00E11332`, and dispatches through native message helpers. | Request reference ownership, actual editor entry, save/accept/cancel callbacks, simulator routing, owner/version fencing and another player's continued native gameplay. Submission return is not a committed creation or proof that the editor appeared. |

Pinned declarations/wrappers are in `Spore/App/Thumbnail_cImportExport.h`,
`Spore/Editors/Editor.h`, `Spore/Editors/EditorRequest.h`, and their corresponding
`SourceCode/App` and `SourceCode/Editors` files within the SDK. SDK labels and
decompiler types remain research aids; the concrete instruction observations above
do not resolve object ownership or native timing. Any later binding must stay in
`src/bridge`, pass executable/content guards, and use the qualified engine callback
context. No native pointer or SDK container may cross the process boundary.

## First original-game experiment protocol

Two prepared disposable OS profiles have verified closed backups of their Spore data
and creation folders. Each data tree contains 27 files and 17 recognized archives;
both creation folders are empty. Existing data differs in GraphicsCache, Pollination
and one opaque event file. Four decoded records differ across the two archives, with
no added or missing keys. The saved-world archive records match. These are baseline
differences, not a creation or import result.

1. Confirm current desktop availability, refresh the personal-save protection gate,
   and launch one original process with the existing isolated worker host. Limit
   active experiment time to ten minutes per process; close the owned process and
   inspect its exit before starting the peer.
2. Use the original creature editor to make and save one distinctly named creature
   from installed parts. Inspect the original editor/test-drive view and retain a
   process-specific capture. A save click alone is not a committed-file result.
3. Close the process, take a fresh verified backup, and run `compare-trees` on the
   before/after data and creation copies. Retain every changed file, native key and
   decoded hash privately. Separate new creation payloads from catalog/cache changes.
4. Only after identifying the newly saved original artifact, test the original
   import/load route in the second profile. Verify the creation was absent before
   import, inspect the loaded original model and parts, then close and compare again.
   Copying a PNG alone is not proof that import occurred.
5. Use the resulting native resource mapping to determine the next dependency and
   capability observation. An editor preview alone does not close native worker
   gameplay validation, canonical terrain or the multiplayer editor transaction.

The [preparation report](../evidence/2026-09-21-m08-content-foundation/editor-preparation.md)
records the pre-native baseline; the [executed session](../evidence/2026-09-22-m08-native-creation/SESSION.md)
records new commands, exits, representation findings, captures and hashes.
Raw disassembly, decompilation, backups and
inventories remain under ignored `local/`. All eight [M08 gates](../tests/engine/M08.md)
retain their original evidence requirements.
