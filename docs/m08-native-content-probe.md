# M08 native creation inspection probe

Current completion: [M08 native acceptance](../evidence/2026-09-22-m08-completion/SESSION.md)
and [transaction protocol](m08-content-transactions.md). Original loader, owned
release, ImportPNG and exact/mapped resource lookups are qualified for the recorded
creatures on .46–.50. Direct DecodePNG/Submit calls remain unqualified. The dated
investigation sections below preserve their original evidence boundaries.

Bridge/NativeHost **0.0.46** adds a private developer command for inspecting one
existing creature through the original game's loader. Two .46 original-game calls
now inspect an original-saved creature and its clean-profile PNG import, copying
matching 23 rigblocks/47 capability entries and completing owned release. This
qualifies the recorded diagnostic fixture, not complete M08 gameplay acceptance.
[Native session](../evidence/2026-09-22-m08-native-creation/SESSION.md).

The probe supplies a known `.crt` key to `cEditor::LoadCreatureData`, copies native
rigblock references and capability values, and releases the returned owned
reference. The original loader may generate cache resources. It does not import
the operator's archive, approve content, grant readiness, publish a creation, or
execute an editor transaction. Native values still need comparison with actual
original behavior for G03 in [the M08 acceptance map](../tests/engine/M08.md).

## Binding and ownership

The executable is GOG GA 3.1.0.29 PE32, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
SDK commit is `cbf9206b9a823f0911cd9be0217104a49d72380b`. Addresses below
are preferred virtual addresses for this executable, base `0x400000`.

| Native entry or layout | Static evidence and implemented guard |
|---|---|
| `LoadCreatureData`, `0x004BB500` | Two caller-cleaned arguments, bool in AL, plain RET. The adapter checks 14 entry bytes and calls it as cdecl on the qualified engine callback thread. |
| Derived creature-data vtable, `0x013EED80` | Both inspected constructors assign this table. The returned object must have this exact relocated vtable. |
| Owned `Release`, `0x00433020` | The table's second slot is this member function; 14 entry bytes are checked. Native code maintains its encoded reference count and invokes its own final-release callback. The adapter calls it once and never edits the count. |
| `cCreatureDataResource`, size `0x128` | Pinned SDK layout. Rigblocks at `0x98`, capability tags at `0xAC`, signed byte levels at `0xC0`. Three-word vector spans, readable memory, counts and capability slices are checked before copying. |

The inspected cached/new/variant success paths transfer one owned reference.
The cache helper's cast does not add another reference; the parent transfers its
existing reference. The new-object branch acquires and transfers one reference.
Recognized results are released even when their fields fail validation. An
unexpected vtable disables the adapter and reports an unreleased result; close
the isolated process rather than guessing a destructor or Release signature.

The derived lookup type is `0x0F43029A`; the SDK class identifier `0x03E1C247`
is not substituted for it. A returned resource must match the requested instance
and group and the derived type. Unknown negative capability-range sentinels fail
explicitly until native evidence establishes their meaning.

The adapter copies at most **512 rigblocks and 4,096 capability values**. No native
pointer or container crosses IPC. Exact scalar trace events retain the request,
key, part group/instance, native indices, capability ranges, four-byte tags,
signed levels and owned-release outcome. Completion explicitly records
`gameplay_validation:false` and `readiness:false`.

## Command admission

`inspect_creation` is appended to the existing private worker IPC enumeration.
Its three values are **instance, type, group**; all remaining payload values must
be zero. Type must be `.crt` (`0x2B978C46`), instance must be nonzero, and instance
and group cannot be the `FFFFFFFF` sentinel. The controller validates this before
opening IPC; the bridge validates it again.

Existing generation, sequence, epoch and persistence-busy gates apply. The bridge
rejects replicas and admits only stable menu/scene phases with
`Simulator::IsLoadingGameMode()` false. This explicit check covers loading begun
through the original menu, which need not set the persistence adapter's state.
There is no normal-account launcher or network route to this command. The adapter
also refuses reentrancy and repeated/non-increasing native request IDs.

## Reproducible native driver

After an authorized original editor save/import, identify the exact logical `.crt`
key and its representation in a verified closed copied archive. Original saves
can contain BEM rather than a literal CRT record; the observed PNG import instead
stores a literal CRT in Pollination. Start the isolated worker separately through the
existing worker procedure. This command inspects that already-running worker;
the illustrative key below must be replaced by the actual observed key:

```powershell
python tools/native/m08-content-probe.py --worker 02 --source-archive local/example/EditorSaves.package --save-trace local/example/closed-actors.jsonl --key '40626200!12345678.2b978c46' --output local/m08-native/example-inspection --version 0.0.46
```

The output directory must be fresh and inside ignored `local/`. The driver first
validates the entire source archive and selected record. A BEM-backed logical key
requires `--save-trace local/example/closed-actors.jsonl`: a matching original
accepted editor result, paired request/result ordering, pinned native provenance,
complete sequence, healthy closed footer, successful listener cleanup and zero
foreign callbacks. It admits only the same group/instance BEM. The trace route ID
is not a transaction nonce. A cancelled, mismatched, incomplete or unhealthy witness
cannot admit this mapping. The source and witness are reread after inspection.
It captures one worker
generation, verifies the actual original process's parent, executable path and
disposable-account SID, checks fresh status and native trace provenance, then
sends at most one inspection request. It preserves before/after trace prefixes
and the exact command/reply privately. Unknown outcomes are not retried.

Success requires matching request/key/epoch/PID/thread, contiguous trace sequence,
monotonic clock, bounded ordered records, one completion and owned release.
Missing, forged, reordered, failed or partial results cannot become success.
The copied archive is not loaded by the driver: a same-key result in the running
engine is **not proof that its bytes match the supplied copied record**. That
mapping needs the native save/import and dependency investigation.

## Terrain observation

After the existing qualified native world-load completion, the persistence
adapter now emits `native_world_observed` with the current home planet's copied
generated-terrain key and home-world flag. It reuses the existing home-planet
getter in the same engine callback and retains no pointer. An unavailable record
produces an explicit unavailable event without changing load success.

`canonical_resource_validation:false` remains explicit. The observation must be
correlated with closed archive records and inspected original terrain before
claiming G05. Native-04 now observes `4084a100!28ca32e7.011989b7` after the original
Satiria load, with an inspected world view. That exact key has not been resolved
to a closed archive record; complete terrain mapping is still open.
The separate [world identity admission guard](m08-world-identity.md)
checks prelaunch file identities; these two observations do not yet establish a
complete canonical terrain dependency graph.

## Passive original editor outcomes

The same isolated worker now registers an observation-only listener through the
existing SDK message manager. It does not invoke editor Submit, Save, Accept,
Cancel, or Post. The listener copies only bounded scalars while the message's
borrowed payload is valid, returns false, and retains no native pointer.

Static queue dispatch at VA `0x008847F0` delivers the original pointer and releases
the queue's owned reference after delivery. Request message `0xB03BC30C` contains
the `0x9C`-byte EditorRequest (primary vtable VA `0x013F6FEC`). Result message
`0x030C11C7` contains the `0x48`-byte result (vtable VA `0x013F56B4`). Relocated
constructor prefixes and exact payload classes are checked. A foreign thread,
unreadable payload, different class or malformed emitted Boolean is refused.

`editor_request_observed` copies the editor/key/calling-mode/routing fields and
visible action flags. `editor_result_observed` copies the returned key, model type,
routing field, original cancel flag at `+0x44` and play flag at `+0x45`. Original
cancel producers preserve the request key; accept producers copy the current
model's key. Native-01 through native-03 now record the original save and cancel
outcomes. A changed source draft's cancellation preserves the closed model and PNG;
the clean peer's capture also includes the complete discard/return sequence. The native
`routing_id` is not demonstrated unique and cannot by itself identify a transaction.

Request observation explicitly leaves `editor_landing_verified:false`. An accepted
result leaves `native_commit_validation:false`; both retain `observation_only:true`
and `readiness:false`. Closed-file save completion, qualified dependencies and
authoritative owner/version publication remain separate requirements. Disposal
first disables observation, removes both listeners and reports removal success
and any foreign-thread callback count.

HOST decoding tests cover complete/truncated/null payloads, exact scalar mapping,
opaque embedded pointers, invalid Boolean fields, unchanged output on refusal and
relocated class addresses. They never load the SDK or original game.

## Remaining native work

The recorded original creature save, closed comparison, clean-profile import and
matching native-derived inspection have run. Resolve exact part/dependency readiness
and generated-terrain mapping before accepting a transferable immutable version.
Then integrate native editor begin/commit/cancel with owner/version fencing while
another original client continues gameplay. The current network scene-exit and
species-identity fences remain in force; this diagnostic command does not weaken
them or constitute that transaction implementation.
