# M03 native decision correction — 2026-09-12

**Delivered: bridge 0.0.9 restores reciprocal B/NPC combat in the bounded original Creature fixture. Full M03 remains BLOCKED on independent native player/progression ownership.** The user asked to finish M03, reduce repeated testing, prioritize reverse engineering, and continue working. This increment locates and fixes a native ownership assumption, preserves the failed observations, and maps the remaining reward dependencies without replaying the full acceptance matrix.

The prior `../2026-09-12-m03-context/` bundle is preserved. This session changes the bridge, shared ABI types, affected ABI fixture, research tools/evidence and current M03 documentation. It does not change the player launcher, install a mod into the personal game, or introduce network gameplay. The existing dirty workspace at HEAD `d106404da0b7c4531f63f98d0df2377bb897a0f7` is preserved; no commit or reset is made. Original M00–M20 work, deliverables and acceptance clauses remain intact.

## Configuration and identity

- Installed executable: `C:\Games\SPORE\SporebinEP1\SporeApp.exe`, GOG GA 3.1.0.29, PE32 preferred base `0x400000`, 24,895,536 bytes, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
- Tested gameplay: existing **Satiria original Creature campaign**, not an Adventure. The existing 108-file content/configuration guard is checked before injection; no other executable/content configuration is qualified here.
- SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Injector source: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`.
- OS/hardware recorded earlier the same day in `../2026-09-12-m03-context/provenance-0.0.5.json`: Windows 11 Pro 10.0.26200, Ryzen 7 9700X (8 cores/16 threads), RTX 4080 SUPER driver 32.0.15.9649. The source manifest records that reference explicitly; no separate hardware comparison is claimed.
- Disposable account: `SporeMP-M01`, SID `S-1-5-21-1000000000-2000000000-3000000000-1007`. One original game process at a time, measured profile isolation and closed backups. Personal sources are independently backed up/hashed by the guarded wrapper.
- Native display: fullscreen 2560×1440. Desktop actions were performed by the assistant under the existing authorization and recorded in `input-actions.jsonl`. The game was closed through native Quit/Don't Save after each run.

Versioned source snapshots and four built-artifact hashes are in `provenance-0.0.7.json`, `provenance-0.0.8.json`, `provenance-0.0.9.json`; source copies remain under ignored `local/m03-decisions-0912/source-VERSION/`. The tested bridge 0.0.9 DLL SHA-256 is **090a22b20c1ca862a60377cd0df34ca2484c2d8fa9a8eb3705211dfce67ee9cc**. Source `native_actors.cpp` SHA-256 is `66bb06ed70f6f39308e77074eb98b211a03274d011edf43e0f19ba5e5deb7d03`.

## Cause and implementation

Earlier bridge 0.0.6 traces locate NPC attacks being cleared during the next native AI tick, but cannot identify the selecting decider. Bridge 0.0.7 observes the original five-word cdecl attack-stop predicate. Bridge 0.0.8 adds a guarded read of the actual native selection frame at the known attack-deactivation path. It checks callback return RVA `0x7C87D0`, cleanup return RVA `0x7C957D`, selected-child bounds and the live native node. It never writes that frame.

`decision-02` records **seven preemptions by decider index 25, ID 0x04A1A0F9**, static record VA `0x1587C40`, Decide VA `0xD7D900`. That native rule checks the acting creature against the manager's avatar home-herd position and examines a remembered target. A native avatar target is exempt through general flag 0x200; bridge-owned B is not classified that way.

Bridge 0.0.9 extends only that existing exemption to a live bridge-owned remembered target lacking the native avatar bit. The original decider is called once with its original arguments. The original remembered-creature lookup `D99930` is called once and its exact pointer/null is returned. An engine-thread stack scope captures identity only at return RVA `0x97D9AA`; nested scopes restore the previous scope. If the original nest score is positive and that exact target is a live owner, the score becomes zero. Native scratch/dirty output remains intact. Static inspection verifies the decider cleanup slot is null, so its scratch has no cleanup ownership to discharge when another branch wins.

The decider ABI uses seven C++ parameters/eight stack words, including a double native clock, caller cleanup 0x20 and x87 float return. The lookup has one cdecl word. Prefixes, table identity, callsite bytes, context and ABI are recorded in `nest-binding-audit.json`. The harness installs 17 hooks after 12 prefix and 3 virtual-slot guards. Hook installation/disposal stays in verified SDK callbacks; DllMain receives no new work.

This change does not set health, force an NPC attack, replace the manager avatar, alter native general flags, suppress native AI ticks, or invent a reward. Native decision selection, ability execution, delayed effects, damage and death still cause the outcome.

## Native results

| Run | Bridge / game PID / engine thread | Expected and observed | Exit / trace |
|---|---|---|---|
| decision-01 | 0.0.7 /29208 /23692 | Observe original stop predicate. Target remains eligible in the sampled stop checks; original predicate returns false. Cleanup follows another branch. A was already hungry/starving, so this is not a healthy A-control experiment. | Game0, wrapper0;10,741 records, clean detach, foreign0. |
| decision-02 | 0.0.8 /30368 /32152 | Resolve exact native preemption. Seven logged active-attack deactivations select nest decider 0x04A1A0F9. B lands one native hit; NPC lands none. A remains healthy at 10. | Game0, wrapper0;3,540 records, clean detach, foreign0. |
| ownership-01 | 0.0.9 /6160 /11960 | Verify the narrow exemption: six native B→NPC hits and six native NPC→B hits. B10→4, NPC6→0/dead, A10 unchanged. No DNA award. | Game0, wrapper0;3,134 records, clean detach, foreign0. |

All three strict trace reports are structurally valid with contiguous sequence/process/thread/clock identity. Each run has one same-engine-thread initialize/dispose, healthy trace stop and detach status 0. All 29 personal files retain their hashes after each run. The four closed 27-file disposable snapshots verify backup contents and unchanged Games/creation trees; cache/event files can change normally.

Ownership-01 creates A1/owner1/native232, B2/owner 2/native7425 and NPC3/owner0/native7427. Console commands are **`sporemp_actors setup`**, then **`sporemp_actors duel 2`**. The fixture queues one initial B attack. No `engage` or forced `npc` command is submitted in this run; original AI selects later attacks. The strict analyzer records B six effect dispatches, NPC six effect dispatches and matching nested Animal/base-damage calls. NPC's first damage into B is call35, trace sequence590, with strike scope33 and animal-damage scope34. B→NPC damage calls are7,167,215,226,236,246; NPC→B calls35,162,200,221,231,241. Each lowers the intended receiver by one. There are 603 logged native nest exemptions and no logged NPC3 active-attack deactivation in this run. This count does not imply the trace observes every possible deactivation of any creature.

Complete ownership trace SHA-256: **3418d7a4ff106613e24f25dff7074361541670a70225176206c903653a774d8e**. Decision-02 SHA-256: `e2f8a2230c44fd026e49a55efc7e96736af30b8225bb99d8e6455b6e99523e85`. Decision-01 SHA-256: `104afd0b051ebf759f0a8416bf3383565857407d24afbe91e235d22342bb16aa`.

`native-summary.json` correlates these results; the per-run `context-analysis.json` reports retain `native_acceptance=NOT_VERIFIED`. The bounded combat criterion is evaluated separately in `acceptance.json`; no host or parser pass is promoted to full M03 acceptance.

## Commands and focused validation

Every recorded execution retains an argv array, expected/observed exit and timing in its JSON report; corresponding stdout/stderr is in `.log` or the per-run files. `commands.json` indexes those records, native launch completions, recorder invocations, extraction commands and nested Ghidra commands. It does not silently rerun them. The early wrapper used `.json`; the corrected wrapper uses `.command.json` to avoid collisions.

The final implementation build and affected test were:

```powershell
cmake --build build/win32 --config Release --parallel 4
ctest --test-dir build/win32 -C Release -R native_actor_abi_host --output-on-failure -V
```

Both exit 0 (`build-03.command.json`, `checks-02.command.json`). The ABI fixture passes 15 assertions across eight real pinned Detours signatures, including all double-clock argument slots, dirty output, positive/negative x87 results and exact pointer/null return. CTest passes1/1 in 0.07s. Earlier build01/02 also exit 0. Earlier `checks-01` runs only `native_actor_abi_host|tooling_unit`, passing2/2 (12 ABI assertions at that version and67 Python tests). The full M01/M02/native matrix is not repeated.

Each native launch uses a fresh RunKey through:

```powershell
pwsh -NoProfile -File evidence/2026-09-12-m03-decisions/launch.ps1 -RunKey ownership-01 -Mode Actors
```

That actual run's wrapper argv is preserved in `ownership-01/completion.json`: `tools/native/invoke-isolated.ps1 -Action Launch -RunName m03-0912-ownership-01 -Configuration Release -ObservationMode Actors -DisplayMode Fullscreen -Resolution 2560x1440`. The same fresh-name pattern was used for decision-01/02. Build/test commands themselves never launch a game.

The source audit, native analyzer, fixture backup/correlation and selected Ghidra exports run through `run-command.ps1`; exact arguments and outcomes are retained. `record-results.py` records already reviewed evidence. `seal-evidence.py` checks source/artifact identity and report consistency and writes the final manifest; it does not run another game or gameplay test. Removing an unused, result-independent comprehension from `summarize-native.py` does not change its saved analysis.

## Capture review and limits

The guarded WGC/FFmpeg recordings use actual game PID matching and terminate with recorder exit 0/no wall timeout. Raw recordings stay ignored under `local/m03-captures/0912-RUN/`. Actual capture requests, executable hashes, commands and completion reports are copied into each run's `capture-01/`. Three contact sheets with nine extracted samples each were visually inspected. `capture-extraction.json` records exact FFmpeg argv/hashes; `visual-review.json` records what was seen.

Decision-01's90-second samples include a starvation warning, console, pause and quit confirmation; it is a diagnostic observation, not a healthy controlled combat case. Decision-02's60-second video ends before the full encounter and quit. Ownership-01's90-second video covers galaxy/save selection, loading, healthy A, setup and the start of combat, then ends before the complete fight. Later live tool screenshots showed the collapsed NPC and healthy A, but the full six-hit exchange has native trace evidence rather than continuous recorded visual coverage. No run was repeated just to replace the short recording. WGC cadence is not native frame timing.

Before ownership commands, a real first recorded frame was inspected with the recorder's FFmpeg binary using `-v error -ss 1 -i local/m03-captures/0912-ownership-01/capture.mkv -vf scale=960:-2 -frames:v 1 -n local/m03-captures/0912-ownership-01/first-frame.png`, exit 0. The actual recorder path is in request.json. That command was issued before capture completion and established captured content, not the later fight outcome.

## Further reverse engineering without new native sessions

The reused pinned Ghidra12.1.3/Temurin21.0.12.1+1 project is partially analyzed from the earlier 300-second initial analysis. Selected exports succeed; their inferred prototypes are not accepted as native bindings without instruction/ABI review. Private exports/database stay under ignored `local/m03-static/`.

Reward lifecycle exports identify native noun factory `B20BF0`, player factory/constructor `B1E960/C7B930`, SetOwner `C7C870`, RemoveOwner `C7C3C0`, listener registration `C75380` and destructor `C7AE80`. Native player noun ID is0x02C21781, size0x12D8. Native Cast/GetCastID use0x02C216ED, unlike SDK TYPE0x03C609F8; the base cast does not accept the SDK ID. No second native player is instantiated here.

Award calculation `C042A0/C035A0` depends on campaign brain level `0x169E370`, before AddEvolutionPoints changes global DNA. Native player goal progress, social/combat trait fields, collectable state, display accumulation/queue and action dispatch are separate dependencies. `D47910` constructs five action handlers, with executable vtables/functions pinned in `reward-binding-audit.json`. Native brain progression `D3FCA0` also changes avatar/same-species state and emits messages. The consumer of strategy+0x48, set by the award dispatcher, remains unresolved.

The existing September 9 crash dumps were inspected offline for handler addresses. Their memory streams retain the strategy pointer but omit the required handler-vector heap pages. `offline-handler-context.json` records both dump hashes and exact missing addresses. Static initialization supplied handler definitions instead. This is not a recovered live vector or new successful gameplay evidence.

Incorrect leads remain explicit: B215E0 is inside a noun configuration loader, not CreateInstance; C7AEC0 is inside a routine, not the player destructor; the vtable corrects it to C7AE80. D3FEDE belongs to brain-progression entryD3FCA0. Strategy vtable slots0x28/0x2C are serialization, not Update/PostUpdate; exports D39800/D46350 confirm that. `ISimulatorStrategy` is declared inside `SubSystem/cStrategy.h`, not its own guessed header. No native binding was installed from these mistaken labels/addresses.

## Preserved failures and boundary

The initial `fixture-pre-decision01` wrapper collided with the backup's report filename and exited 1 **after** a valid27-file backup. `fixture-pre-decision01-wrapper-failure.json` preserves that failure. The wrapper now writes `.command.json`; the valid backup was reused without another game run.

Other read-only corrections included incorrect SDK/file paths, PowerShell wildcard-path `rg` errors, a too-broad address-pattern search, and two failed atomic documentation patches caused by incomplete matching lines. Corrected reads/patches succeeded; these are not gameplay successes or native failures. One native registration-dialog click was refused because another app occluded the target; a fresh window observation/activation and retry handled it without input to the other app. A decision-02 intro Escape minimized the game and required reactivation; that activation was not appended by the action helper, so the input log is not claimed as a complete desktop audit.

All native game processes and recorders from this session have exited. The last verified closed fixture is `local/m03-fixtures/0912-post-ownership01/`; its backup can be reused before the next run while quiescent. Game binaries, saves, raw capture frames/videos, crash dumps and decompiled game code remain local/excluded from Git.

The remaining implementation is native B player/progression ownership, including deferred effects and lifetime routing. Active Stop/scene cancellation, nonzero resource costs, native server-only-avatar/area activation and dead-B retirement remain unqualified. The next reward experiment requires the concrete owner adapter and a bounded native A/B award pair showing that each owner's progress remains separate through subsequent native updates. An unchanged fight or full-suite repeat would not resolve that implementation gap.
