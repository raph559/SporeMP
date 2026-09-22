# Native14: original starvation, corpse presentation and revival

**NATIVE + NETWORK-REAL + inspected exact-process recordings.** Bridge **0.0.42**, schema **4**, completes the observed authority A starvation/death and original revival without a projection or network error in either client. Each client starts the admitted corpse animation once and the living idle once. A separate original NPC with `dead=true`, health `0`, and combatant state `0` exercises the .42 admission correction successfully on both clients. **This is bounded lifecycle evidence, not complete M07 acceptance.** There is no fresh Join in this run and no established post-respawn action.

The reviewed [machine-readable summary](native14-summary.json) records exact sequences, state values, original hashes, sample counts, inspected frame hashes and closure. Raw traces, recordings, private process/profile inventories and command arguments remain in the ignored private archive.

## Recorded scope and identity

The run uses the pinned original GA executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` and SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`. Three original games run in the established separate disposable worker profiles on the rendered Windows desktop: authority PID **37612**, client A **3412**, client B **22252**. The frozen Bridge DLL is `1031848e63b3a18802da540e31729603132a6da42be50ebd693cfb4cc4cb0dc9`; NativeHost is `a55ea45b295f745853353bb0ec9723d86496000cacb185027495234bfc40fd9d`. This is the existing single-machine qualification, not a new hardware, headless, cross-machine or Internet acceptance claim.

The source/payload manifest is frozen before launch at `2026-09-21T10:07:55Z`. It sets a 300-second maximum target; the watchdog requests closure after 270 seconds from authority start. All three games retain their original process generations through closure. This analysis reads the final closed snapshots, verifies each actor trace against the archive hash, and inspects retained captures; it does not launch a game, alter a source file, or issue desktop input.

## A death and original revival

A remains global entity **12884902120**, generation **30**, across death and revival. Authority and client A use native noun **232**; client B's local replica uses native noun **9601**.

| Transition | Source revision | Client A | Client B | Applied/read-back state |
|---|---:|---|---|---|
| Death | `74411562` | scalar `10027`, animation `10029` | scalar `9972`, animation `9974` | dead, health `0`, hunger `0`, age `1`, combatant state `2`, scale `1`, native maximum health `10` |
| Revival | `74453250` | scalar `12428`, animation `12430` | scalar `12397`, animation `12399` | alive, health `5`, hunger `100`, age `0`, combatant state `0`, scale approximately `0.1`, native maximum health `5` |
| Later native scale | `74455828` | context `12577` | context `12550` | alive, health `5`, age `0`, scale approximately `0.6` |

The coordinator records the authority's corresponding entity updates at these exact revisions, with life state `1` at death and `0` at revival. Both clients read back the same revisions. Authority periodic samples independently record the dead state at sequence `43560` / tick `74411875`, revived state at `47911` / `74453703`, and later scale at `48250` / `74456718`. Those periodic samples occur after the transition revisions; they are not presented as three-way samples at the exact transition tick.

Each client calls the qualified corpse presentation **once**, selecting `05807346`, and later calls living idle **once**, selecting `02481DE5`. Both native starts and immediate animation readbacks succeed. Subsequent periodic samples retain the corpse animation in **41/41** client A and **42/42** client B samples, and retain the living idle in **20/20** revived samples on each client. The authority contributes 41 dead and 20 revived periodic samples; its animation getter is not observed by this client-only helper.

For independently sampled authority/client revisions that happen to coincide, **25** client A and **19** client B samples match the authority exactly on health, hunger, dead flag, age, alpha, combatant state, scale and native maximum health. None differs. These counts describe matching sampled revisions, not all transmitted frames. Unrelated local flag bits are not equated across processes.

## Actual .42 corpse-state regression coverage

An additional original NPC appears as native **68**, global **12884901956**, generation **38**, herd **1745**, species `[109087434,731352134,1080189440]`. Its state matches the combination that .41 incorrectly rejected in native13: `dead=true`, health `0`, combatant state `0`. The native slot is different from native13's slot79, and this run does not establish the previous incarnation history of slot68.

The authority retains that combination in **59** periodic samples. Both clients qualify the existing herd/profile and original factory result, then apply revision `74359078`: client A local noun **9653**, scalar event `6898`, corpse presentation `6900`; client B local noun **9623**, scalar event `6852`, corpse presentation `6854`. Both presentation calls start successfully. All **60/60** later periodic samples on each client report current animation `05807346` and retain combatant state `0`; no fabricated state-two write is needed. The authority later observes the original pool return at sequence `44282`, counter `0 -> 1`.

This establishes the corrected state's **dynamic creation and presentation**. It does not establish a fresh baseline containing that corpse, nor a visually identified rendered pose for this separate NPC in the reviewed camera views. The [binding document](../../docs/m07-life-presentation.md) records the original mbDead-driven selection and the narrow shared predicate.

## Inspected visual evidence

All recordings use Windows Graphics Capture bound to the corresponding verified game PID/window. Each is 1280×720, with a clean recorder exit `0`. Their recorded durations are approximately **241.166 / 240.866 / 241.033 seconds** for authority/A/B; the games close before the recorder's 270-second requested maximum. Capture cadence is not a measurement of native frame timing.

Frames were extracted with FFmpeg from the closed recordings. The command shape was `ffmpeg -hide_banner -loglevel error -ss <seconds> -i <private capture.mkv> -frames:v 1 -n <private frame.png>`; all extractions returned `0`. All three views were inspected at recording offsets **180, 205, 225 and 238 seconds**, plus authority **215, 220 and 223 seconds**. Exact hashes are in the summary.

- **Authority:** 180 seconds shows the original death cinematic; 205/215/220 show the original starvation message and green confirmation. At 223 an egg is visible in the nest; 225 shows the hatch/young-creature scene; 238 shows ordinary gameplay UI with health **5**. The capture brackets the original confirmation and revival; an exact input timestamp is not inferred from these sampled frames.
- **Client A:** 180/205 show the local avatar lying on its side with health **0**, resolving the earlier upright-corpse visual mismatch for this avatar. At 225/238 the ordinary UI shows health **5** and a nest view. The exact revived model is not unambiguously identifiable in those frames, so these images do not establish baby-model resource parity.
- **Client B:** the four inspected views show living B and nearby creatures. The remote A is not reliably identifiable from that camera, so its corpse/revival presentation is supported by the native animation and scalar readbacks, without a matching visual claim.

The replicas retain their ordinary UI while authority uses its original death/egg sequence. This helper does not claim to reproduce the authority's whole death cinematic, game-over interface or baby/adult model replacement.

## Combat/action limits and closure

The staged combat driver starts at `10:09:04Z` and reports **INCONCLUSIVE** because the required preposition evidence does not arrive. The later A-first driver starts at `10:11:47Z`, emits no action, and reports **Native owned actor unavailable**. These preparations do not constitute a completed encounter, target switch, kill or pickup result.

The post-respawn action harness begins at `10:12:26.171Z`. Its first three **status** requests return `0`; a later client B status request at `10:12:29.923Z` returns `3`, after watchdog shutdown. The harness stops with **Worker IPC outcome unknown; no automatic retry**. It issues no movement/jump action in the recorded command list. No post-respawn command acceptance, application or landing is claimed.

The watchdog records shutdown requests at approximately `10:12:28Z`. All three final actor traces are contiguous, healthy and end with `trace_stop`: authority **50,398** events, client A **13,646**, client B **13,618**. Across the complete traces there are **zero rejection events, zero trace-limit events and zero network-error states**. The final disconnection events are normal session closure. Every original game, worker and recorder exits **0**; the authority game exits at `10:12:28.865Z`. All **29** protected personal files and all six disposable Games/Creations tree checks remain unchanged.

| Original final actor trace | SHA-256 |
|---|---|
| Authority | `d3f9f6e4c997cafc5935f2fb06eb28eebb3677cce29e3988522499a6c9b4355a` |
| Client A | `80a0aee7fd6c7ec44c09e63299ce5dfa421da5cd01dd5b5aa24621f208ab3b4c` |
| Client B | `93b2caf0fddc7dde17bb9342991998dbc89cb5d234daf18b40c017babc637aeb` |

Native14 therefore supports the corrected dynamic corpse admission and the bounded A death/revival projection. Fresh Join after revival, a successful post-respawn client action, the complete encounter and remaining M07 acceptance gates still need their own evidence.
