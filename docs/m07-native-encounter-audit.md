# M07 native encounter bindings and remaining pickup gate

Updated 2026-09-21. This note separates reused native M03 acceptance, new source integration, and static pickup candidates. The new network encounter behavior and contested pickup have **NOT RUN** native acceptance in this increment. M07's original acceptance clauses remain unchanged.

All addresses below are preferred virtual addresses in the PE32 GOG GA 3.1.0.29 executable, preferred base `0x400000`, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. SDK source is pinned to `cbf9206b9a823f0911cd9be0217104a49d72380b`. Native reads and calls stay on the verified SDK app-update thread. Only scalar IDs, generations, values and intentions leave the bridge.

## Reused combat and award paths

The [M03 audit](m03-native-context-audit.md) and [recorded M03 encounter](../evidence/2026-09-12-m03-awards/SESSION.md) already qualify original target assignment, ability execution, native reciprocal NPC damage, and B's separate native DNA/goal/combat reward for the recorded same-species first-brain fixture. They do not prove simultaneous network contention, pickup ownership, or B respawn persistence.

| Binding | Evidence retained | New integration use |
|---|---|---|
| Animal `SetCreatureTarget`, vtable slot `0x84`, VA `C03DF0` | Existing prefix/vtable guards and M03 original target/AI behavior | Queue attack/approach/engage through the existing `ActorCommands` dispatcher; native target reference ownership remains unchanged. |
| `WalkTo`, `PlayAbility`, native ability/cooldown/range/damage paths | Original M03 and M06 execution | No custom damage formula or coordinator combat simulation. |
| Player noun factory `B20BF0`, native owner lifecycle and scoped B reward handlers | M03 actual player construction/destruction and 8.75-DNA award | Authority bootstrap creates the same qualified B player/reward context before publishing its balance. |
| Stage DNA setter/getter `D2E480` / `D2E350` | M05 original scalar projection without `AddEvolutionPoints` replay | Publish observed A/B DNA values; clients project the controlled owner's value through existing guarded scalar presentation. |

`native_actor_network_command(epoch, owner, actor_native_id, target_native_id, verb, direction, request)` is a scalar bridge entry. The caller first checks the authoritative global entity and generation. The entry then independently checks current actor epoch, owner, native noun ID and lifetime. A targeted request enumerates the live native noun list and adopts its NPC target into `ActorCommands`; no pointer supplied by the network is accepted. Only move, jump, stop, attack, approach and engage are admitted. Player-owned targets are rejected because this encounter increment does not configure PvP. NPC diagnostic registration remains bounded by the existing eight actor slots and returns busy on exhaustion. Queue acceptance is not native completion.

`native_actor_network_prepare_rewards()` is an idempotent authority-only wrapper over the qualified M03 construction path. It neither initializes client native progression nor resets an existing nonzero B goal. `native_actor_owner_dna(owner, dna, current_native)` reports only a finite nonnegative observed balance. A retired B reward context retains one scalar snapshot keyed by actor ID and epoch; it is readable only while that same resolved actor is dead, with `current_native=false`. Scene evidence records that cached balance as source state and leaves current native DNA absent. A living B after reward-context retirement fails closed. This snapshot is not persistent storage and is never written back into a native B stage or player.

The existing `actor_state` diagnostics now include the SDK fields `mbHasBeenEaten` and `mFoodValue`. This adds observation only; neither is used to implement consumption.

## Contested pickup candidates: static evidence only

The SDK's `App/cCreatureModeStrategy.h` declares `EatFruit`, `EatMeat`, `Pickup` and `Interact` payloads. Those actions are dispatched by native behavior **after** an interaction reaches an outcome. Invoking the action dispatcher directly would skip native approach, animation, claim and consumption and is not an implemented pickup command. Several other SDK payloads share inconsistent IDs, so names are research leads rather than permission to call an inferred ABI.

| Candidate | Inspected pinned behavior | Missing evidence |
|---|---|---|
| Corpse eating `D70FD0` | Checks a dead enabled animal, retains `mpWhoIsInteractingWithMe` at `+B50`, approaches through original movement, and advances the original animation state. State 1 tests the eater's native avatar bit, the corpse's `mbHasBeenEaten` at `+B5F`, and native diet predicate `C0B8E0`. `D71398` sets the consumed-reward flag before `D713AD` calls original `AddEvolutionPoints`; then it emits `D335362D` (`EatMeat`). | A verified input/behavior admission route, its actual calling convention and argument lifetime, and a native A trace through the complete operation. B's non-avatar classification and reward caller require a separately audited context adaptation; the existing kill-only B reward scope does not cover this path. |
| Corpse food transfer `D70A80` | Subtracts `native configured rate * native dt` from corpse `mFoodValue` at `+B84`, invokes hunger/resource helper `D85A50`, and conditionally applies original health changes. | This is gradual consumption, not evidence that two eat requests produce one exclusive whole-corpse grant. Native behavior, resource identity and terminal consumption must be observed. |
| Fruit eating `D87590` | Native animation/event path emits `D335362B`, destroys a qualifying fruit noun through original `DestroyInstance`, applies hunger/health, and has a native avatar-gated DNA call at `D8836F`. | Fruit entity type/identity, exact input path, argument/lifetime validation, ownership and actual terminal-consumption evidence. Current shared-scene schema is Creature animals only. |
| Generic pickup `D7EE10` | Checks a current interactable object and its native interacting creature, runs original approach/animation, and emits `D3353636`. The only raw literal occurrence of that action ID in the pinned PE is at `D7F65E`. | Full behavior tick ABI and input admission are unresolved. No callable binding was added. |
| Ornament interaction `D869B0` | Casts to `cInteractiveOrnament` (`TYPE 0x03A25119`), retains its `+108` interacting creature, runs native approach/animation, then emits `D3353638`. | SDK `cInteractiveOrnament` identifies bones/pickable objects but does not establish an accepted native interaction command. No collectible entity replication or complete part-unlock ownership has been established. |
| Native reward record `D475E0` → `D46BE0` through handler `D47860` | Locates the original object/action reward record, applies configured native effects, decrements its positive remaining-use count and sets the exhausted flag when it reaches zero. | Record lifetime and lookup ABI, native request trigger and actual contention outcome. Calling this terminal helper would not prove native pickup interaction. |

Corpse interaction is the smallest candidate because the encounter already has the dead Animal's canonical identity. Its one-time first-eating DNA flag is promising but does not prove that the complete native food resource is exclusive. This distinction must be resolved by the experiment, not by redefining M07's simultaneous-pickup criterion.

## Next bounded native experiment

1. Use the accepted disposable Creature fixture and existing isolation/save-protection gates. Record the exact source/payload hashes and capture the original game window.
2. Complete one native NPC kill with A. Use the ordinary original interaction on its corpse while observing `actor_state` consumed flag/food quantity, original `native_global_dna_enter/return` caller RVAs, target changes, animation callbacks and noun removal.
3. Correlate the one accepted input with its original behavior tick and entry ABI. Determine whether the retained native request path can admit both owned actors without rewriting behavior or rewards. Observe the native diet predicate rather than assuming the SDK label describes every branch.
4. Only after that path is pinned, add two queued requests for the same canonical corpse/resource incarnation and verify native consumption and one reward outcome. A competing request must neither recreate the resource nor replay its terminal action. Apply the resulting absolute balance/consumed state to both clients and exercise reconnect.

The original network shared-NPC encounter, original final death/reward agreement, simultaneous pickup, post-death reconnect, latency/loss and unauthorized-action acceptance remain required. A build or this static export cannot close those gates.

## Prepared original-process harness

`tools/native/m07-harness.py` provides `inspect`, `shared-combat`, `unauthorized-action` and `archive`. Importing it or requesting `--help` performs no native operation. `inspect` archives bounded public traces/payload hashes for the already-running original authority and clients, then lists actually sampled living NPC IDs/generations and player distances. It does not select or attack one. `shared-combat` requires an explicit target ID and generation from that inspection; it submits one intention through each real client's private IPC and authenticated connection, then observes for 1–45 seconds. It never launches/stops games, sends desktop input, restores saves or substitutes a pickup fixture.

The parser correlates each client player/request/target incarnation through network intention, authority receipt, `network_actor_command_queued`, original target dispatch, paired native damage calls and an actual health decrease. Request numbers from the two clients cannot be interchanged. Superseding actor commands and scene exits fence attribution. Requested damage and accepted queue responses do not count as native damage. A native DNA entry without its return/value increase does not count as a completed grant.

Reports include normalized sampled NPC health/death and published owner balances for the three machines, plus separate actual controlled-avatar DNA readback. Cached B corpse DNA is not accepted as current native readback. `full_m07` remains `NOT_VERIFIED` unconditionally: contested pickup, post-death reconnect, configured latency/loss, unauthorized-action repetition and inspected footage are separate gates.

`unauthorized-action` requires the actual coordinator run directory and three connected original processes with the same living owner-2 actor incarnation. Through client `02`'s real IPC it submits authenticated player `1`, owner `2`'s global actor ID, original `jump` verb `1`, direction `0`, target `0` and target generation `0`. It requires the client intention and later `network_rejected` with `wrong_actor_owner`, plus the same coordinator connection/request/player/entity/generation at incoming action and dispatched rejection. A local IPC refusal or missing coordinator record cannot pass. A finite 1–45 second observation compares complete matching trace/log prefixes and requires no authority receipt/queue or coordinator forwarding for that player/request; client `03` must stay connected. The rejected requesting client may enter its existing quarantine policy. This is a bounded rejection observation, not proof about future events or the complete G08 world/action-continuation gate. It retains before/after archives and `full_m07: NOT_VERIFIED`.

HOST-only checks executed successfully (each exit 0):

```powershell
python -m unittest discover -s tests/unit -p test_m07_harness.py -v
python -m py_compile tools/native/m07-harness.py
python tools/native/m07-harness.py --help
git diff --check
```

The twelve synthetic parser tests cover queued-versus-applied separation, actual health change, player/request and generation correlation, nested native return/scope pairing, lifecycle/supersession cuts, ambiguity, all-three-machine state and nullable native-DNA provenance, tombstones, paired reward completion, and remote ownership rejection with required coordinator records and bounded absence of forwarding. Missing remote evidence, a local refusal, wrong identity, an authority queue, peer loss or incomplete observation cannot satisfy the negative-action check. No native game or harness encounter was executed.

## Static command provenance

Both commands below returned exit **0**, matching expected exit 0. They run the hash-gated existing Ghidra database and do not start SPORE. Ghidra version is 12.1.3; JDK is 21.0.12.1+1. The export script SHA-256 recorded by both run reports is `b2809b1726c1e2b5d9facda7681a22c579248ca42602c2b97890de308a87e221`. Complete commands, UTC timestamps, executable identity and `native_execution: NOT RUN` are retained in the ignored local report JSONs.

```powershell
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-pickup-dispatch -ReuseDatabase -Addresses D716F0,D70FD0,D87590,D85A50,D9BBD0,D47860
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-pickup-native -ReuseDatabase -Addresses D7EE10,D869B0,D70A80,D475E0
```

Reports: `local/m03-static/m07-pickup-dispatch.json` and `local/m03-static/m07-pickup-native.json`. Each sibling directory contains complete instruction and decompiler exports plus a call/reference index; the game-derived exports are retained locally and are not committed.

| Local instruction artifact | SHA-256 |
|---|---|
| `m07-pickup-dispatch/D70FD0.asm.txt` | `c730f0364d2f73e8d3e2c3d67e6a259fde9a6478171c950fa1434bd51b6095d8` |
| `m07-pickup-native/D7EE10.asm.txt` | `412e21037a73ff93b3df9726bc64f3354738a9be7b85e1384068ca32acdb7ed1` |
| `m07-pickup-native/D869B0.asm.txt` | `758548d4e5b328fd852e764241f1479844c52a7e58c39f5ec66645fc5fc3d2f0` |

The parent M07 session owns final build/test commands, whole-tree hashes and the integrated acceptance state.
