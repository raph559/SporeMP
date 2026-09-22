# Native10: shared combat, native corpse presentation and ownership rejection

2026-09-21, bridge **0.0.38**, wire schema **3**, original Creature campaign, one authority and two clients. **The bounded shared-combat case succeeds; full M07 remains IN_PROGRESS / NOT_VERIFIED.** Both authenticated players damage the same native NPC, one original award gives B **7 DNA**, and both clients receive the dead state and display the original admitted-corpse pose. A subsequent real ownership challenge is rejected without a matching authority action; B remains connected and later completes a legitimate native jump.

This is reviewed **NATIVE / NETWORK-REAL** evidence for the pinned fixture. [Sanitized results and hashes](native10-summary.json) accompany this report. Raw logs, process/account identities, exact private argv, recordings and extracted frames remain under ignored `local/m07-completion-2026-09-21/`.

## Original encounter and reward

The target is native **79**, global entity **12884901967**, generation **7**, authority actor **3**, initially alive with **6 HP**. Owner A is authority actor **1**, native **232**; owner B is actor **2**, native **9601**. Both players and clients are admitted before the case. Two real client IPC submissions send `engage` for the same target generation. B submits first with a configured five-second lead; A's IPC starts approximately **5.45 seconds** later. The harness then observes for **25 seconds**. Accepted IPC results mean queued intentions; counted hits below require actual original damage entry/return and health decrease.

| Native result | Closed-trace evidence |
|---|---|
| B damages the shared NPC five times | One HP each, Animal damage scopes **2552, 2744, 2909, 3019, 3111** |
| A damages the same NPC once | One HP, scope **3048**, sequences **15000–15005** |
| NPC79 targets B and retaliates | Target request sequence **12986** selects native **9601**; three **0.5-HP** hits, scopes **2606, 2834, 3075**, reduce B **10 -> 8.5 HP** |
| Original lethal result | B's scope **3111**, sequences **15311–15322**, takes NPC health **1 -> 0**; the later source sample at tick **70352406** records `dead=true` |
| One original reward | `native_owned_reward_applied`, sequence **15320**, inside scope **3111**: B DNA **0 -> 7**, amount **7**, award count **1**; A stays **0** |
| Both replicas receive the terminal result | Bounded after-snapshots show NPC **0 HP/dead**, A DNA **0**, B DNA **7** on all three processes; each controlled client's native DNA agrees |

The independent analyzer reconstructs all nine Animal/base-damage chains from closed traces and verifies them against the bounded case's exact archived prefixes. It also checks owner/request correlation for the intentions, authority queue entries, native command beginnings and target calls. No native award call is recorded on either client; their absolute balances follow the authority.

NPC79 targets and damages **B only in this run**. Prior native05 recorded that NPC attacking A in a different encounter. These observations do not establish a target switch between both owners in one fight. Later unrelated NPCs targeting A after the tutorial are excluded. Native target telemetry is sampled: the authority reports 25 target callbacks and six intentionally omitted samples, so this is not an exhaustive decision history. Starvation damage is separate from these scoped combat hits.

## Corpse presentation and repeated-state handling

Client A sequence **3261** and client B sequence **3187** each invoke the new model helper once for the same entity/generation. Both record `callback_cleared=true`, `started=true`, original animation **92304198 / 0x05807346**. The original mapping selects resource **92304200** on A and **92304199** on B; different native resource choices are preserved.

| Readback | Client A | Client B |
|---|---:|---:|
| Helper start count in closed trace | 1 | 1 |
| Requested native animation index | 267 | 262 |
| Immediate current index | 268 | 263 |
| Later sampled original animation ID | 92304198 | 92304198 |
| Dead samples before that client's session rejection | 72 | 220 |
| Later sampled native index | 267 | 262 |

Every counted dead sample on each client reports that original animation ID and its requested native index. The immediate paired index settles by the first periodic readback; it is not treated as a rendered-frame proof. A's 72 samples end before its deliberate wrong-owner quarantine. B's 220 samples continue until the authority disconnects during shutdown. No per-update restart or presentation refusal is recorded. Three-machine agreement is claimed for the bounded combat snapshot before A's quarantine, not for future updates to the quarantined client.

The exact-process Windows Graphics Capture recordings were inspected separately. All three **100-second** extracted frames were independently reviewed for this report:

- The authority displays the original victory tutorial, the two blue actors, and the low green corpse beside them.
- Both clients display the same encounter with the green NPC visibly flattened/lying low, approximately half its living height. Client A's HUD shows **0 DNA**; client B's shows **7 DNA**.
- These frames close the visible upright-corpse mismatch recorded in native05 for this ordinary NPC. They do not establish identical animation phase, falling reaction, fade timing or all corpse styles.

The WGC requests identify each actual game process/window, and all three recorders close with exit **0** and no timeout after a requested **180 seconds**. Frames at **35/70/100 seconds** are retained privately; this report's independent visual check uses the three 100-second frames. Three separate UI get-state screenshots taken without activating each window duplicated the foreground window; those screenshots are **discarded as evidence**. Original media are not published. Capture cadence is not native frame timing, and audio was not recorded.

The module's scope remains the [qualified model core](../../docs/m07-life-presentation.md): original admitted-corpse selection, safe model/queue context, and exclusion of the creature gameplay animation callback. Fresh dead baseline, this helper's revival path, model replacement, cause-specific lethal reactions, death fade/opacity phase and game-over UI are not accepted by this combat run.

## Unauthorized client and surviving legitimate player

After combat, authenticated A submits real client IPC request **2** to jump as B's entity **12884911489**. The client emits intention sequence **7415**; the coordinator records the incoming action and a correlated outbound `wrong_actor_owner` rejection with `dispatch_queued`. Client A records rejection sequence **7419** and enters error/quarantine at **7420**.

The challenge's verified complete prefixes cover **5.593 seconds** of wall observation and **5.7207337 seconds** of authority trace time. There is no matching owner-1/request-2 `network_native_intention` or `network_actor_command_queued` in that interval; the independent closed analysis also finds none later in the closed authority trace. Equal per-client request numbers are distinguished by authenticated owner. B remains connected during the challenge.

The original victory tutorial paused authority AI while application heartbeats continued. After the operator activated the authority window and dismissed its green confirmation, a legitimate B jump was submitted. Closed events correlate B's request **2** as client intention **14290**, authority queue **35319**, receipt **35320**, accepted native jump **35330**, and landing **35623**. Queue receipt alone is not the acceptance criterion. B subsequently receives `authority_disconnected` at sequence **15960** when the authority is intentionally shut down; this is separate from A's ownership rejection. The recordings had already ended before the later tutorial dismissal and jump, so that latter result is trace-qualified, not video-qualified.

## Explicit failures and timing discrepancy

The .38 existing-herd spawn fallback is **NOT VERIFIED**. All three startup guards report `scene_herd_profile_ready: binding_mismatch` at sequence **9**. Static/native metadata identifies preferred-base absolute operands in the guard that change under ASLR. The guard leaves fallback disabled; this successful NPC79 encounter does not prove the dormant-herd creation path. The later guard correction needs its own native run.

The source/payload manifest inherited `bounded_native_run_seconds: 150`, while recorder requests specify **180 seconds** and the authority's final trace time is **305.5299066 seconds**. Client trace durations are **304.5660134** and **303.2544878 seconds**. This run exceeded the recorded 150-second bound while the original victory tutorial remained open and the operator resumed it for the surviving-player check. It is **not** a successful 150-second bounded run or an unattended-progression claim. The next protocol must align capture, wall deadline, tutorial handling and shutdown bounds rather than inherit stale values. The 25-second combat observation remains separately identified.

## Closed-file verification and reproduction

`python local/m07-completion-2026-09-21/analyze-native10.py` reads the closed archive only and returns **0**. It verifies archived byte counts/hashes, complete JSONL, contiguous sequences, monotonic per-process clocks, zero foreign callbacks, healthy `trace_stop`, detach status **0**, exact earlier-prefix hashes, scoped damage/reward chains, ownership rejection and the legitimate jump/landing. No original game is started by this analysis.

All three games and workers exit **0**, bridges dispose, and worker states are `stopped`. No trace-limit, scene-capture rejection, scene-projection rejection or life-presentation rejection appears. These native writer checks do not establish network packet-loss acceptance or a new ETW loss measurement. The optional archive CIM process census returned an error and is marked **NOT_RUN**; closure is established by the recorded exits/disposals/stopped states and the save check's zero recognized workers, not by that failed census.

| Closed actor trace | Records | SHA-256 |
|---|---:|---|
| Authority | 43,628 | `19cb5b48bf66a4fc8ab2458de3c26586253329f9a7c5202d01d27ce81498586b` |
| Client A | 10,452 | `58257241fe7dbc70b9eec15f7a52a49ad43a4a664dc8d1c93b97518895cd9847` |
| Client B | 16,018 | `2f149d4a00edc361ab85b9bcbc25b5048ef754a649cde60771fcf2aeabaf3cbc` |

All **29 personal files** remain unchanged. Each of the three disposable Games trees retains its **16** files unchanged; all three Creations trees remain empty. These are closed hash comparisons, not a claim that every possible shared operating-system path was audited in this run.

Pinned executable SHA-256: `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Loaded bridge SHA-256: `41aee23bf41b1490166b705f1bd002992b01e351225b9b197e2fb64062f24309`. NativeHost SHA-256: `f8a89078eccfd7f56bcec6d0c841096ce5fd1d12479e1e838b786f91eef32ca7`. Immutable content/configuration fingerprints are retained in the source/payload manifest and the existing [compatibility contract](../../docs/compatibility.md). This is the previously inventoried Windows 11 Pro 10.0.26200 / Ryzen 7 9700X / RTX 4080 SUPER test machine; no new adapter-selection measurement is asserted.

The actual .38 validation records are `build038.command.json` and `ctest038.command.json`, with commands `cmake --build build/win32 --config Release --parallel 4` and `ctest --test-dir build/win32 -C Release --output-on-failure -V`, both exit **0**. CTest passes **10/10** in **13.09 seconds**, labeled BUILD/HOST/FIXTURE. Their actual timestamps are **09:01:21Z / 09:01:34Z**; the source/payload manifest inherited older build/CTest timestamps, so those inherited fields are not used to date this validation.

Private case reports preserve exact commands and returns for `shared-combat` (target **12884901967**, generation **7**, verb **engage**, `--seconds 25 --b-lead-seconds 5`), `unauthorized-action` (`--seconds 5`), and archival. The closed coordinator hash, capture hashes, analyzer hash and private artifact hashes are exported in the accompanying sanitized JSON. No raw account paths, credentials, game binaries or game assets are copied into the public evidence.

Contested native pickup, reward-preserving reconnect after this encounter, the newly integrated presentation lifecycle cases, the corrected existing-herd fallback, and configured latency with real packet loss remain open at this native10 snapshot. Later evidence may qualify them; this report does not pre-accept that work or mark full M07 complete.
