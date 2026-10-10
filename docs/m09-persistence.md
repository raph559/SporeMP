# M09 persistence contract

The pinned two-player Creature checkpoint implementation is VERIFIED within the
recorded native boundary; acceptance is tracked in [M09](../tests/engine/M09.md).
This is an experimental foundation for later campaign milestones, not arbitrary
campaign save compatibility or a public hosting release.

## Acknowledgment boundaries

| Result | Meaning |
|---|---|
| Worker IPC `accepted`, network action result | Admitted/queued; neither native application nor persistence is implied. |
| Correlated original gameplay return and observed state | The original game applied the operation. It remains non-durable until the containing checkpoint is committed. |
| Original `native_save_return` | The original save call returned. This alone is not a durable world acknowledgment. |
| Checkpoint `seal` result | Closed native artifacts and their complete barrier witness have been verified. `durable` remains false. |
| Persistent content commit success | Immutable creation bytes, manifest and exact owner head have been flushed into the selected coordinator revision. |
| Checkpoint commit receipt with `durable:true` | One selected coordinator revision contains the exact sealed native artifact, current world identity, credentials, immutable content and owner heads. |

There is no automatic replay of non-durable gameplay commands. Reconnecting uses
fresh transport/session/baseline fences. An exact checkpoint retry after a lost
acknowledgment is idempotent; a stale unrelated revision is refused. Duplicate
creation commits cannot replace the published version. Original saved corpse
state and restored first-feed beneficiary metadata prevent a committed pickup
from becoming a fresh first feed after recovery. Original combat/progression
state is loaded, not reconstructed by awarding its balance again.

## Native consistent barrier

All engine work occurs in the existing verified app-update callback. A complete
save requires two validated live actors and original players, no pending bridge
commands or active native action scopes, and both original reward display queues
empty with counter state zero. A diagnostic `save_probe` is never sealable.

The original full-save function runs once with its pinned ABI. A registered
ModAPI persistence extension roots the actual second `cPlayer` through SPORE's
own COM serializer, including its original inventory graph. An explicit scalar
header records actor/native-ID fingerprints, B's native Creature stage values,
its pending native world-update input and observed corpse first-feed owners.
No pointer, native container or memory image crosses the worker IPC boundary.

On a fresh original load, a checked callsite hook preserves exactly that loaded
second player through the original single-player cleanup. Other destruction
calls are unchanged. The bridge validates and adopts the already loaded player
and actors, refreshes the original player-list cache, restores B's scalar stage
context and rebinds corpse metadata by existing native ID. Original load preserves
remaining corpse food but clears `mbHasBeenEaten`. The extension restores the
observed saved flag at the verified post-load cleanup callsite, before the first
gameplay tick; remaining food is left unchanged. It does not create a
replacement player, reset inventory or replay DNA/progression rewards.

The full native profile and creation files are read only after the worker has
closed cleanly. Sealing checks contiguous single-thread native evidence, exact
save/barrier/extension/return ordering, the same scene and request, a healthy
trace stop, original actor/player IDs and the matching built payload. It takes
and verifies protected backups, hashes every member and all six canonical world
files, and rejects files changed during sealing. Abruptly terminated workers and
queued-only or diagnostic saves cannot produce a seal.

## Atomic coordinator publication

The coordinator owns the exclusive writer handle for a local fixed NTFS store.
A commit writes a bounded hashed immutable record, flushes it, publishes its
content-derived filename, writes and flushes a new `CURRENT` marker, then replaces
that marker with `MOVEFILE_WRITE_THROUGH`. Old records remain available. Recovery
uses only the record selected by `CURRENT`; orphaned writes do not replace it.

Checkpoint commit is an offline operation: stop the coordinator and native
participants, seal the complete save, then open the same existing store with its
expected revision. Native artifact bytes and matching world/content metadata
share one store record and one publication point. No collection of independently
updated sidecars can accidentally mix two revisions. The artifact limit is
80 MiB; the containing store limit is 100 MiB. Oversize input fails closed.

Artifacts contain only bounded scalar metadata and individually hashed original
files under `spore/` and `creations/`. Both decoders reject truncation, trailing
bytes, missing world members, duplicate case-insensitive names, traversal,
reserved Windows names and mismatched hashes. File operations reject reparse
points and multiply linked regular files. Unknown schema/build/executable/content
identities, corrupt selected records and concurrent writers refuse recovery.
There is no implicit fallback to an older acknowledged revision.

## Rollback budget and recovery status

A recovered network authority receives a **300,000 ms (five-minute) maximum
uncheckpointed execution window**, measured from successful native restore with
Windows `GetTickCount64`. The authenticated worker status carries the original
fixed deadline. The supervisor latches it and terminates its owned job when the
budget expires. Reconnect, paused/stalled gameplay, later status messages and a
native save return cannot renew it. A fresh process must load a selected sealed
checkpoint to begin another window. This bounds simulated work lost on a crash;
startup/loading time does not add unsaved gameplay to that window.

Initial fixture provisioning, before its first sealed checkpoint, has no durable
world acknowledgment or recovery deadline. The five-minute guarantee starts with
an authority that has actually restored a selected checkpoint; it is not a claim
that arbitrary fresh campaign sessions automatically become persistent.

This experimental milestone uses an explicit stop/save/seal/commit/restart
workflow. It does not provide unattended periodic checkpoint rotation. Operators
must complete a checkpoint before the deadline to retain new work. Expiration
is recorded as `checkpoint_required`, never as a successful save. Continuous
hosting orchestration and player-facing host controls remain part of M18.

The coordinator supplies its committed checkpoint revision in the authenticated
welcome. A participant cannot become recovery-ready without a successful native
extension restore and an applied current baseline. Launcher 0.1.11 uses that
combination to show that the saved shared world was restored. Merely copying an
artifact or receiving a queued command cannot trigger the recovery indicator.
Ordinary Play remains on the user's normal account; disposable profile controls
are developer tools, not a prerequisite in that flow.

## Protected recovery commands

All paths below are explicit absolute paths. Use a new ignored `local/` output
directory per command, the original matching server configuration, the recorded
current revision, and only prepared disposable workers 01-03. The manager never
launches a game or writes personal profiles.

```powershell
# After a successful full native save and clean worker/coordinator shutdown:
python tools/native/durable-checkpoint.py seal --worker 01 --config ABSOLUTE_SERVER_CONFIG --output NEW_LOCAL_SEAL
build/win32/Release/SporeMP.Coordinator.exe --checkpoint-commit --config ABSOLUTE_SERVER_CONFIG --state ABSOLUTE_EXISTING_STATE --output NEW_PRIVATE_RECEIPT --artifact ABSOLUTE_SEALED_SMP --expected-revision CURRENT_REVISION

# Export exactly the selected acknowledged checkpoint:
build/win32/Release/SporeMP.Coordinator.exe --checkpoint-export --config ABSOLUTE_SERVER_CONFIG --state ABSOLUTE_EXISTING_STATE --output NEW_PRIVATE_EXPORT
python tools/native/durable-checkpoint.py inspect --artifact ABSOLUTE_EXPORTED_SMP

# Close every original game before replacing the owned test profiles:
python tools/native/durable-checkpoint.py restore --artifact ABSOLUTE_EXPORTED_SMP --workers 01 02 03 --output NEW_LOCAL_RESTORE
build/win32/Release/SporeMP.Coordinator.exe --serve --config ABSOLUTE_SERVER_CONFIG --state ABSOLUTE_EXISTING_STATE --output NEW_PRIVATE_SESSION

# After testing and closing all games, retain the test results and restore the
# exact original disposable trees from the journal's protected locations:
python tools/native/durable-checkpoint.py undo --journal ABSOLUTE_RESTORE_JSON
```

Restore validates and stages every target before the first same-volume switch.
A journal retains the old trees and hashes. `undo` preserves post-restore work
before reinstating the verified originals. Keep the journal, backups and retained
directories together. A restore command reports native load as `NOT_RUN`; only
actual original-game execution can validate the loaded checkpoint.

Back up `CURRENT` together with its selected immutable record and permissions
while the coordinator is stopped. The universe envelope is schema 2, native
extension schema 3 and artifact container schema 1. Earlier developmental formats
require an explicit reviewed migration; no migration is implemented. Never delete
`CURRENT` to force reinitialization and never silently choose an older record.

The original Windows account also holds the named CNG private key used by the
stored TLS certificate. Recovery checks that exact key/certificate pair, retaining
invitation credentials and the certificate pin. A missing/expired identity is
refused, not replaced. The state directory alone is not a portable machine/account
backup. Cross-machine key migration and physical power-loss/hardware-flush testing
are not qualified by these process-termination tests.

## Validation and native binding provenance

The automated suite separates HOST/FIXTURE codec, backup, transaction, TLS,
launcher and supervisor checks from NATIVE original-game execution. Storage fault
injection terminates a real coordinator process before record write, after record
flush, after record publication, after marker flush and after marker publication
before acknowledgment. Native fault injection terminates only the owned test
worker immediately before the original save or after its return, without issuing
a clean-save/durable acknowledgment.

The pinned executable is GOG `SporeApp.exe`, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`;
SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable address family 2.
Original serializer slots are WritePointer `+0x2c` / RVA `0x29d980` and
ReadObjectPointer `+0x28` / RVA `0x29e170`. WritePointer's SDK boolean return is
not a reliable native success value; the original stream's `IsGood` is checked.
The inspected non-primary cleanup returns at RVA `0x72455f`, with an exact
checked callsite prefix. Player-cache invalidation uses original RVA `0x7200f0`.
The corpse flag is the pinned SDK `cCreatureBase::mbHasBeenEaten` at `+0xb5f`,
already qualified by M07 original pickup observations and replica projection.
Both display counters use the previously audited original `+0x128` receiver and
counter/deque layout. These are x86, engine-thread bindings gated by the pinned
executable and byte prefixes; they do not authorize another executable profile.

The early native save failure and its investigation remain preserved in the
[session history (archived)](public-evidence.md#historical-artifacts). Current results,
commands, reviewed evidence and unqualified boundaries belong in the
[acceptance matrix](../tests/engine/M09.md).
