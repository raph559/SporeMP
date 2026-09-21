# M03 native player lifetime — 2026-09-12

**Historical result: bounded native B-player lifetime VERIFIED; full M03 BLOCKED on stage/reward ownership.** No B reward was earned in this increment. Prior reciprocal-combat evidence remained separate.

Bridge 0.0.10/0.0.11 created B through the verified native factory/owner lifecycle, retained a native reference and preserved A's manager pointers. Explicit stop and disposal used native cleanup. Resource/configuration callbacks were routed to B; campaign-global callbacks retained A ownership. This was a Creature-fixture policy, not complete multi-stage behavior.

The complete session, literal commands, original traces/media and source/artifact hashes remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-12-m03-rewards/`. This condensation adds no test or acceptance.

## Failures and accepted native run

The initial build failed on ambiguous inherited AddRef/Release and was corrected to the proper native interface. HOST tests passed 18 ABI assertions. First native player-01 rejected a relocated CreateInstance prefix before actor hooks; ASLR changed a relocation-backed immediate. The guard was corrected to compare the relocated address. That run never loaded the fixture and exited 0.

Bridge 0.0.11 player-02 passed setup → players → players off → players → native Quit/Don't Save. Two B creation/removal generations had distinct native/unique IDs, ten selection groups and separate native item storage. RemoveOwner ran once each, with native cleanup and no extra listener cleanup required. A's player/avatar pointers, health, goal/trait state and 53 unlocked items remained unchanged. B started with goal total 1 versus A's 1,000 and zero unlocked items: complete stage context was still missing.

The trace contained 2,141 contiguous records, one engine thread, zero foreign callbacks and healthy detach. Three live resource callbacks executed B's original handler; two during retirement were rejected. Game/wrapper exits were 0. All 29 personal hashes and closed disposable Games/creation contents were unchanged. Scene-exit retirement was NOT RUN.

## Static corrections and limits

The native display counter was identified as an embedded object with a deque, correcting an earlier scalar interpretation. Deferred herd evolution reads owner-sensitive player/species/goal state. Whole stage initialization would repeat global input/display/world setup; it was not called for B. Part filtering was distinguished from granting parts. No binding was installed from discarded decompiler guesses.

The inspected 60-second video covered scene/setup/first creation; retirement, recreation and quit had native trace/live observation, not continuous footage. A summary script initially mistook all Windows modules for build artifacts; correcting that report did not change native evidence.

Final bridge SHA-256: `c5b7ddb14b9b2e184a6b61fd3eecb0b93d5a9fb3ee7db5e63f66ecb44204ab2c`. SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, loader `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`, GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; Windows 11 build 26200/Ryzen 7 9700X/RTX 4080 SUPER.

The remaining gate was owner-scoped original reward calculation, goal/trait state, queued display and deferred world/species effects. Next was the implemented reward adapter followed by a bounded A/B award pair; later [M03 acceptance](../2026-09-12-m03-awards/SESSION.md) records that result.
