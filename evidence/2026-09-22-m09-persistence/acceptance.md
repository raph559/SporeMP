# M09 native checkpoint and crash-recovery acceptance

Date: 2026-09-22. **VERIFIED for the pinned two-player original Creature fixture.**
The complete M00–M20 plan and original M09 acceptance clauses are unchanged.
This qualifies the persistence foundation used by later stage milestones, not
arbitrary campaign saves, unattended production hosting or all-stage multiplayer.
The user explicitly authorized PC use for these native tests in this session.

## Result and implementation

Restarting the coordinator and original worker, then reconnecting two fresh
original clients, restores the selected world, existing actor IDs 232/9601 and
original player IDs 1/9602, immutable creation ownership, native inventory and
positive progression. There are no replacement avatars or replayed rewards.
The original serializer owns the second player's complete item graph; a pinned
post-load binding preserves that player through single-player cleanup. A checked
extension restores B's stage scalars and previously observed corpse ownership.
All gameplay remains in original SPORE.

The coordinator atomically selects an immutable record containing the complete
native artifact and matching metadata/content. A seal is not durable; the
successful coordinator commit receipt is. Its exact retry after a lost receipt
is idempotent. A recovered authority has a fixed 300,000 ms execution budget;
the supervisor stops its owned job when another checkpoint is required.
[Full persistence and operator contract](../../docs/m09-persistence.md).

## NATIVE and NETWORK-REAL evidence

Raw logs, save data, credentials, frame captures and protected backups are private
under `local/m09-2026-09-22/`. The reviewed reducer is
`review-evidence.py`; its full output and member comparisons are in
`review-evidence.json`. Its 20 fresh original loads cover native23/after,
native26/before and after, native28/after and final, and all five native27 cases.
Native23 also contains the three original processes that produced the checkpoint.
Failed attempts below are retained separately and do not become passing tests.

| Check | Expected and observed |
|---|---|
| Full production save | Original save executes once with two validated native players, both original reward queues empty and state zero, no pending command/action scope; extension, native success, completion and healthy closed trace are correlated. Diagnostic saves cannot qualify. |
| Native identities | All 20 fresh loads adopt existing A/B actor IDs 232/9601 and original players 1/9602; `created_nouns:0`. |
| Original inventory | Exact item-key/status comparisons in every load: A has 53 unlocked items, B has 0, and both have 312 item-status entries (624 total). Player identity and sampled goal/social/combat values match the save. |
| Positive progression | B's original combat/goal/DNA value 8.75 survives every load with zero `native_owned_reward_applied` events. |
| Creation and ownership | Native23 re-inspects the published creation after recovery: all 23 rigblocks and 47 capability entries match; owner 1 retains the exact version, owner 2 remains empty. Duplicate/stale publication cannot advance its head. |
| Both clients | Fresh original clients authenticate with unchanged invitations/certificate and apply current baselines before reporting `checkpoint_recovered:true`, revision 3. Client-sourced native movement is observed in native23, native26/before and both native28 phases. |
| Critical resubmission | Native26 resubmits two authenticated B attack intentions against the saved dead target; the coordinator refuses both before native dispatch. A's resubmitted feeding reaches 120 original callbacks and retains owner 1; food falls from 54.8952942 to 30.62533 while hunger rises normally. No DNA grant or first-feed event is replayed. |
| Before original save | Native26/before exits 91 immediately after the consistent barrier and before the original save. Both clients observe disconnection. Coordinator is abruptly terminated; the next original triplet restores revision 3. |
| After original save | Native28/after exits 92 immediately after original Save returns true, before save completion/acknowledgment. Both clients disconnect; coordinator is terminated; the final fresh triplet again recovers revision 3. |
| Interrupted native artifact | The production manager rejects both actual crash traces: a cleanly closed matching worker is required. Neither interrupted save becomes sealable or replaces the selected artifact. |
| Measured rollback | Restore-to-crash intervals are 81.5838718 s and 78.2298606 s, inside the 300 s contract. The later fresh load restores B's exact original saved position, rolling back 5.3266337 units of transient displacement; native corpse food returns to the selected checkpoint value. |
| Deadline | Native27's revision-4 authority is inspected, paused through the original Options menu and left running. Its supervisor terminates the owned job with `checkpoint_required` at the fixed deadline. Paused native AI cannot extend the budget. |
| Isolation and cleanup | Three separate verified OS profiles, retained original trees, 29 protected personal files unchanged per lifecycle. All test games, supervisors, coordinators and capture processes are closed; every restoration journal is undone and its original trees hash-verified. |

Native23 and native28 recovery captures were inspected at 0, 2 and 4 seconds
for authority and both clients. Original terrain, creatures, nest and HUD are
visible; animation changes across the samples. B's integer HUD shows 8; the
separate original native scalar is exactly 8.75. These frames do not establish
game frame timing. Native27's actual original Options/pause view is separately
inspected. Capture requests record exact game PID, window handle, recorder hash,
argv and exit code. Private review records preserve the sampled frame hashes.

## Actual coordinator interruption matrix

Native28's final normal save produces a second, distinct sealed artifact. For
each boundary, a real coordinator process opens a separate protected copy of the
stopped revision-3 store, attempts this commit and is terminated with exit 91.
The exported selected bytes are compared exactly, then loaded by a fresh
original authority with restored player/inventory/progression checks.

| Termination boundary | Selected revision | Original load |
|---|---:|---|
| Before record write | 3, previous artifact | PASS |
| After record flush | 3, previous artifact | PASS |
| After immutable record publication | 3, previous artifact | PASS |
| After new CURRENT marker flush | 3, previous artifact | PASS |
| After CURRENT publication, before receipt | 4, new complete artifact | PASS |

The last case's retry with the same artifact and prior expected revision returns
revision 4 again. It does not create revision 5 or apply any gameplay again.
Orphaned records never override CURRENT. Corrupt selected records, unknown
schemas, conflicting writers and incompatible identities fail closed in HOST
tests; these checks do not silently roll back an acknowledged revision.

## BUILD / HOST / FIXTURE validation and commands

Full Release build exits 0. Full CTest passes **16/16 in 34.67 s**: 251 Python
tooling tests, 302 worker assertions, 71 durable-store checks, 25 checkpoint
codec/transaction checks, 246 content-registry checks, 970 content/TLS checks,
2,963 protocol/Schannel assertions and the remaining native-ABI/replica fixtures.
The subsequent focused launcher build and CTest pass **55 launcher assertions**
in 4.85 s, including actual child-process progress transport into Rejoin's new
recovered state. This is HOST evidence; the new WPF recovery text was not visually
exercised in this native run. Its underlying status is verified in original
clients, and Python tests refuse queued-only, missing-baseline or missing-revision
claims. Earlier actual WPF Join/Rejoin acceptance remains M06 evidence.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure -V
dotnet build tests/launcher/SporeMP.Launcher.Tests.csproj -c Release -o build/launcher-tests/Release --nologo
ctest --test-dir build/win32 -C Release -R launcher_host --output-on-failure -V
python local/m09-2026-09-22/network-checkpoint-23.py
python local/m09-2026-09-22/recovery-crash-26.py
python local/m09-2026-09-22/recovery-crash-28.py
python local/m09-2026-09-22/store-crash-27.py
python local/m09-2026-09-22/review-evidence.py
```

Private `run.py NAME ...` command records preserve exact argv, cwd, UTC, elapsed
time, exit code and log hash. Drivers 23, 28 and 27 exit 0. Driver 26 exits 1
after its successful before-save crash/recovery because a later movement check
misses its threshold; only independently verified portions are used above.
Their nested command records include every real worker launch, save/fault
request, seal, commit, export, restore, capture, shutdown and undo. The protected
reproduction commands and schema boundaries are in the persistence contract.

After the final supervisor deadline fix, NativeHost alone was built with
`MSBuild.exe build/win32/SporeMP.NativeHost.vcxproj /t:Build
/p:BuildProjectReferences=false /p:Configuration=Release /p:Platform=Win32
/m:2 /v:minimal /nologo`, exit 0. It embeds the exact already-qualified .64 bridge
hash. Re-linking unchanged bridge code changes the binary identity, so the frozen
bridge was retained for the complete checkpoint/crash matrix. The final host
executes native26/28/27; native23 used the preceding host. Both artifacts and the
exact source manifest are retained, rather than conflating two binary hashes.

## Reproduction failures retained

Native02 demonstrates the original two-player save defect: only primary player 1
survives fresh load. Native03–06 identify original serialization and post-load
cleanup; preserving the actual serialized B fixes identity/item recovery.
Native07–12 establish positive original 8.75 reward state and stage context;
pending native world-update input must be saved, not discarded. Both original
reward queues are required idle at the production barrier.

Native17 then exposes the pickup recovery defect: original load retains corpse
food but clears `mbHasBeenEaten`. Native21/.63 isolates the exact state; native22/.64
restores the observed flag before the first gameplay tick and leaves original
food unchanged. Native23 is the fresh production proof with the final bridge,
not a compatibility claim based on those diagnostic saves.

Other incomplete attempts retain their actual errors: transient status-file
sharing denial, startup readiness timeout, a control request before pipe
availability, and an original tutorial popup pausing feeding. The tutorial was
inspected and dismissed in the authorized production run. Native24 rejects a
relinked bridge/config mismatch before launching any game. Native26's later
movement command reaches original Walk but covers only 0.9649653 units against a
1-unit exercise threshold; the threshold is retained and the failed attempt
remains recorded. Native28 uses a separately logged direction and passes.
No accepted/queued response is used as proof of native movement, feeding or save.

## Pinned identities and artifact hashes

Windows 11 Pro build 26200; AMD Ryzen 7 9700X; NVIDIA RTX 4080 SUPER.
GOG GA 3.1.0.29 original x86 executable, rendered signed-in Windows desktop;
bridge/NativeHost **0.0.64**, launcher **0.1.11**, wire schema 7, config schema 2.
SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`;
loader commit `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`.

| Artifact | SHA-256 |
|---|---|
| Original executable | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |
| Compatibility/content inventory | `06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b` |
| Frozen bridge | `f03f9a27e3535cf75221adb71cbdb610ac39de7304eb337a8f73afa82226a6c0` |
| Final NativeHost | `6600620f7b99a8922d8152014c41a7e9ac8ada954818f0f74bbd70ebc9134094` |
| Native23 NativeHost | `c890ac17e29684d72ba5358383b49b97ce3f519de0b37c9b9242a49271bf0365` |
| Coordinator | `9a4a7af6032ffdf48968887395655fc5846509e1171f3709f7e372a31fc74055` |
| Native23 committed artifact | `93f3f75a127bf64280a7543647a3af8156bc9bdbe54936f708533ae267e80636` |
| Native28 second sealed artifact | `88d52e3d4a2dbe8343be70555e15d5ebc28956e3cf958ba591407bbc344b56b0` |
| Final private source manifest | `2207d44a3ef3b0f3d25569335745f07a38ae9610452df1486115e08b5b4cb149` |

Per-member world/file hashes, source paths, full payload hashes, native trace
hashes and capture hashes remain in the private seal/proof/payload/command/review
records. Native bindings and their ABI/thread/context gates are documented in
the persistence contract. No game binaries, saves, raw logs or credentials are
published by this report.

## Qualification boundary and next milestone

The five-minute bound applies to authorities restored from a selected sealed
checkpoint. Initial fixture provisioning has no durable world acknowledgment.
Checkpoint rotation currently uses explicit stop/save/seal/commit/restart; there
is no unattended periodic-save orchestration. A native save return cannot renew
the deadline. Continuous hosting controls remain M18.

Unknown developmental schemas require an explicit migration; none is implemented.
The saved TLS identity requires its existing Windows-account CNG private key.
Cross-machine key migration and physical power-loss/hardware-flush behavior are
NOT RUN. Arbitrary profiles/content, headless/service workers, full campaign
progression and all-stage gameplay retain their later milestone gates. These
limitations do not reduce the unchanged M00–M20 scope.

The next increment is **M10: one universe across multiple native locations**,
including fenced authority transfer and recovery without duplicate or lost
persistent avatars. M10 is TODO; no M10 implementation is claimed here.
