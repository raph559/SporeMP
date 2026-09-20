# M05 presentation and native save boundary

Date: 2026-09-13. Milestone **IN_PROGRESS**. The user asked to finish M05 and then said **“it’s available”**, authorizing this desktop session. Original requirements remain in `MILESTONES.md`; neither a build nor the replay helper marks M05 complete. This session continues the retained `.18`–`.21` combat-result/long-disconnection evidence without regenerating its accepted native reward source.

Current bridge: **0.0.24**, M05 **IN_PROGRESS**. The `.24` target-selection run is closed and archived. Native selection/clearing, movement, three denial challenges, ordinary Save denial and scene-exit fencing passed in this fixture. The remaining mixed ability/social-result, consumption/progression, replica load/reuse/death and listening gates remain open. M06 is not implemented. Launcher remains 0.1.8; these changes add no player-visible launcher feature.

## Identity and reproduction

- Original `C:\Games\SPORE\SporebinEP1\SporeApp.exe`, GOG GA 3.1.0.29, PE32, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
- SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`; injector commit `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Source/payload freezes: `source-before.json` (.21), `source-022.json`, `source-023.json`, `source-024.json`. Each has six native payload identities. Copies remain under ignored `local/m05-presentation`.
- MSVC v143 14.44.35207, Windows SDK 10.0.26100.0, Win32 Release. Windows 11 Pro 10.0.26200; Ryzen 7 9700X, RTX 4080 SUPER, driver 32.0.15.9649. Original rendered, unminimized desktop, disposable account `SporeMP-M04-02`. No concurrent game was launched into that profile.
- The prelaunch personal-backup checks and all closed postchecks cover 29 personal files. Worker-02 sealed Games digest is `f361377b1bc4fa4a41b3ab621e0fdea85ce787c48cea375afdfa563b2925a332`. The `.21` failed menu-save test changed the disposable fixture, was preserved, and was restored only after both game and supervisor closed. Personal saves were not restored or modified.
- Exact external programs, argument arrays, UTC intervals, expected and observed exits are in `*.command.json`, with output in paired logs. Commands with no output may have no log. Earlier no-argument records include a null placeholder in the JSON array; PowerShell supplied no native argument for it. No game is started by a build/test command. `run-command.ps1`, `freeze.ps1`, `archive-worker.py`, `probe-scene-exit.py`, and `analyze-closed.py` are the session's reproducible helpers.
- Native input uses the Computer Use skill and `@oai/sky`, against the sole returned window for the exact original executable. Input timestamps and inspected outcomes are in `ui-review.md`. WGC captures select the actual game PID/HWND. Static reverse engineering does not manipulate the running game.

## Closed native runs

| Run | Build / game PID / engine thread | Outcome |
|---|---|---|
| replica-01 | .21 / 8184 / 11564 | Failed before arming: rendering stayed stale, WGC produced no useful gameplay, then app updates stopped after the Windows system-menu interaction. Supervisor exit 35, `engine_update_timeout`. Correlation is not a proven root cause. Sealed Games/personal files unchanged. |
| replica-02 | .21 / 31656 / 31608 | Actual 8.75-DNA replay/challenges and visible original movement/jump. **Normal Save bypassed B29600**, showed the native saved confirmation and changed six disposable Games files. Game/supervisor exited 0; this is a failed save-denial gate. Personal files unchanged. |
| replica-03 | .22 / 5660 / 25940 | New persistence-listener guard rejects the actual Save menu request. Original jump/landing and usable video/audio captured. Clean game/supervisor exit 0; sealed Games and 29 personal files unchanged. |
| replica-04 | .23 / 20524 / 20292 | Two successful native challenges, two adversarial charm-timer probes during natural Animal Update, actual Save menu denied, native scene exit invalidates the binding and rejects stale/disconnected work. 11,439 trace rows, 2,197 later avatar samples retain projected vitals for 550.549 seconds through scene exit. Clean exits and unchanged closed hashes. |
| off-05 | .23 / 12908 / 4048 | M05 disabled: zero replica records. Native jump, starvation damage/death, normal confirmation and nest respawn to 5 HP/100 hunger observed. 43,345 trace rows; same original avatar noun 232 survives death/respawn. Clean exits, sealed Games/personal files unchanged. This is Off evidence, not a replica death adapter. |
| replica-06 | .24 / 33696 / 31488 | Native own-species selection and clearing, ground movement, three successful challenges and three marked charm-timer probes. Actual Save denial and native scene exit, followed by stale/disconnected/publication/IPC-load rejection. 19,092 rows / 9,879,005 bytes; 3,826 later avatar and DNA samples match through 958.6087522 seconds after application. Clean exit 0 at 18:15:35.217Z, sealed Games and 29 personal files unchanged. Natural social-result count remains zero. |

Native generation, start/stop times, source payload and every closed trace hash are in each `native-*/archive.json`. `native-summary-02.json` analyzes all six closed runs and includes the .24 selection/walk contact sheets and both listening excerpts. Its successful command is `analyze-closed-03.command.json`. The older five-run report remains intact. The first analysis failed on the CSV BOM and produced no report; that failure is retained.

Static audit metadata is archived under `static/`; `research-provenance.json` hashes 72 retained native instruction/decompiler exports from seven recorded audit runs. Bodies remain private/ignored. `index-research-01` refused an already archived directory; the corrected helper verifies byte identity before reusing metadata, and `index-research-02` succeeds without replacing differing evidence.

The normal Save bypass is retained in `native-save-bypass.json` and `native-replica-02`. Changed Games are backed up under ignored `local/m05-presentation/native-replica-02-changed-games`, with the closed live tree separately preserved. `fixture-restore-02-verified.json` verifies restoration from the existing closed M04 backup to the exact sealed digest; there was no silent resealing.

## Implemented corrections

**Persistence listener (.22):** original `B29960` is the manager's primary IMessageListener, bool AL, ECX receiver, two stack words, RET 8. Its save-request `01CD20F0`, deferred-save `0685DBFA` and immediate/menu `0689C9B9` branches can inline serialization through B29000/B28890 without B29600. The new guard denies before those branches. Other messages retain original handling. `01CD20F0` is a save scheduling request, **not** SDK AppUpdate (`01EE100A`); .23 names the counter `save_request`. Real `.22` and `.23` Save clicks increase the denial counter, display no false saved confirmation and leave closed Games unchanged. Unrelated cache writes and every possible future save/load entry are not claimed covered.

**Charm split (.23):** `C0B065 → C0B0C6` skips only inline charm-time decrement, expiry and intrusive charmer release. The remainder of the native Animal Update runs once. Two natural 17 ms updates were tested with a temporary, explicitly marked `1e-6` timer value on the live avatar with no charmer. It stayed unchanged and was immediately restored in the same callback. This is a native adversarial scalar fixture; it is **not** an actual social charm cast or a fabricated worker result.

**Social-result split (.23):** `C2EEE7 → C2F0F6` skips progression/reward notifications while retaining the preceding original relationship reads/cache and the original tail that supplies both UI text IDs. The actual C2EE80 thiscall takes two output pointers, RET 8; a whole-function no-op would leave invalid UI output. `.23` observed **zero armed calls**, so this was IMPLEMENTED_NOT_RUN for that natural callback. D38150/C30320 research establishes that the species-mission view is reached through native target selection; own-species selection need not create a social species mission.

**Interior x86 ABI (.23/.24):** `native_branch.h` preserves GPRs, EFLAGS, live x87 state, XMM0–7 and MXCSR around the policy call, then resumes an inspected instruction in the same original frame. `.24` also supplies default SSE masks/rounding during the C++ helper, restoring the original environment afterward. HOST tests use a real Detours interior branch, deliberately clobber registers/FP state and exercise 4,096 alternating allowed/denied paths. AVX state and unrelated native frames are not claimed qualified.

**Local target view (.24):** allow original C03DF0 only for the live, idle main avatar, from an original-module call, with a target found in the current native Creature census (or null). It retains native reference ownership and selection/UI updates. It opens no general mutation scope; AI, attacks, damage, grants and social-result progression stay guarded. Other avatars/NPCs and direct developer targeting challenges remain denied. The original native player selection at 17:59:43.914Z shows a creature name, health and relationship indicator and emits `replica_selection_view`. This view is not an authority decision or an outbound multiplayer intention.

## Presentation and timing

Actual WGC pixels were inspected for HUD, native scene, camera changes, original movement, native jumps/landings, Options/Save behavior, scene exit and the Off death/respawn sequence. `.23` replica footage contains the jump at 17:23:04.051Z; Off footage contains the jump at 17:32:51.673Z. A landing count by itself is insufficient: NPC B also emits repeated native landing callbacks in Off. Only the local avatar's input and correlated landing are used for jump review.

Two short captures ended before the intended input. The `.23` wide clip ends before the 17:27:13.956Z jump; its audio includes that late input but the video does not. The `.22` initial ambient audio likewise ends before its following jump. These are retained with their limits, not reported as synchronized action proof. Longer captures solved the scheduling problem. Original Escape skips the introductory cinematic in both modes; it is not a replica regression.

| Actual D3D9 60-second sample | M05 Off .23 | Replica .23 |
|---|---:|---:|
| Rows | 3,681 | 3,682 |
| Median interval | 16.30250 ms | 16.26585 ms |
| p99 interval | 17.53424 ms | 17.44923 ms |
| Maximum interval | 18.20710 ms | 18.39960 ms |
| Intervals >33.333 ms | 0 | 0 |

Pinned PresentMon 2.5.1 SHA `9bec3083069f58f911e6a512f4806db51a27bd096103087bc1d05ef54c80a191`; exact selected original PIDs and request/CPU data are in `timing-*/frames-request.json`. Same machine/build/save and recorders, but different camera/action framing and uncontrolled NPC timing. These are descriptive samples, not a universal overhead or responsiveness bound. They predate the .24 selection change.

The new `SporeMP.AudioCapture.exe` uses Microsoft's process-loopback API for the exact original process tree, 48 kHz stereo PCM16. It does not record the microphone or other unrelated applications and applies no EQ, noise, gain or substitute audio. Each 60-second `.23` paired capture has 2,879,520 frames, 5,999 packets, and zero reported discontinuities/timestamp errors. Capture integrity does not establish perceptual fidelity. Twelve-second copy-only excerpts were presented to the user; listening feedback remains pending. Raw audio/video/images stay ignored under `local/m05-presentation`; their hashes and probe metadata are in native summaries.

## Verification and remaining work

`build-05` passes with no warnings. `.24` focused CTest (`ctest-03`) passes **313** policy assertions, **23** replica ABI checks, **9** interior-branch ABI checks, and **34** existing actor/award/persistence ABI checks. `python-03` passes five affected replay-tool tests. Earlier worker 189 / actor 15 and full Python 103 results remain their recorded unchanged-component evidence. No HOST fixture loads SPORE.

Native scene-exit fencing in `scene-fence-04.json` and `scene-fence-06.json` starts from actual epoch 4 and zero actors: old epoch application is `stale`; current-epoch disconnected application is `invalid`; publication and unadmitted IPC load are `unavailable`; status/audit remain available. This does not establish native noun reuse after a new load, or safely cover every ordinary UI load path while armed.

Still open: natural social-result callback/award-boundary coverage, mixed ability/projectile/knockback paths, natural consumption/progression beyond the listed guards, replica death/respawn and ordinary-load/reuse handling, and perceptual sound acceptance. No motion prediction or motion snapshot transport is enabled; M06 remains later work. The current M04 supervisor labels a deliberately idle-AI replica `simulation_stalled` despite continuing app updates; this authority-oriented heuristic needs a replica presentation/liveness contract before a player-facing connected mode.

The user interrupted to ask why the work was taking so long. The session had expanded into an overly broad engine audit; all test processes were already closed. Project status and acceptance documents now reflect the verified corrections and exact remaining gates. No additional desktop/native test was started after that interruption.

Next bounded implementation: audit the original `C1E8B0` charge-impact loop, where a target impulse can occur after a denied strike. Candidate interior cut `C1EBDB → C1EA58` is static research only. Establish its stack/register and presentation contract, implement only proven authority-write suppression, then use one targeted native already-active ability test. The separate natural social-result test needs a foreign-species target created through ordinary native scene setup before arming; own-species selection is insufficient. Keep M05 IN_PROGRESS until its original acceptance gates are satisfied.
