# M03 completion — native actor and reward ownership

**M03: VERIFIED for the recorded original Creature fixture.** Bridge 0.0.12 closes the missing B reward path. This decision applies to the unchanged M03 acceptance clauses in `MILESTONES.md`, using this run together with the preserved movement, asynchronous-action, ownership and reload evidence. It does not certify a network session, unattended worker or independent Creature campaign. M04 is the next milestone; M11 retains the full Creature progression requirements.

## Result

One original-game run, `award-01`, executed `sporemp_actors setup`, `players`, `rewards`, `duel 2`, and `duel 1`. Each duel queues one initial attack; original native AI chooses subsequent attacks. The adapter does not set combat health or grant a fabricated reward.

| Native result | B encounter | A encounter |
|---|---|---|
| Actor / NPC | owner 2, actor 2 / NPC 3 | owner 1, actor 1 / NPC 4 |
| Native damage | B hits NPC six times; NPC hits B seven times | A hits NPC once; original untracked allies hit five times; NPC hits A twice |
| Death attribution | NPC health 6 to 0; last attacker B | NPC health 6 to 0; final attacker is an untracked native ally, not A |
| Native award | B DNA, goal and combat progress 0 to 8.75; A remains 0 | A DNA, goal and combat progress 0 to 8.75; B remains 8.75 |
| Native item state | A remains 53 unlocked, B remains 0 | A becomes 54 unlocked, B remains 0 |

B's native damage scope 254 produces the original 8.75 amount at trace sequence 3255. The native display queue receives it, the original action dispatcher handles `045AB96E`, and sequence 3258 records B's original DNA/goal application. The native counter dequeues it at sequence 3265 and returns to state 0 with an empty queue. Its final state has exactly one award. A's later assisted reward occurs at sequences 6236–6237 and leaves B's balances unchanged. The native part-unlock tutorial was observed after A's encounter; this is not evidence of B part-unlock coverage.

A's health remains 10 throughout B's fight. B is at 3 when that fight ends, then recovers through native updates to 12.5 **before A's attack command** (sequence 4527 versus 5348). That recovery is not caused by A's reward. Exact native regeneration/maximum-health rules remain unqualified. A takes two hits, regenerates, and later begins losing health from hunger while the run is being closed; it does not die. No health setter is used to produce these outcomes.

All 23 observed native energy calls have amount zero, as expected for this Creature Bite. Acting-owner cooldown attribution was already observed in the retained earlier runs. There is no observed cross-owner debit; nonzero-cost abilities remain NOT RUN. This does not claim spending coverage for the later stage economies.

## What changed

`src/bridge/native_award_context.cpp` implements the inspected native award call chain. B has its own native cPlayer, retained Creature stage data and an originally constructed display deque/timer. The game computes the reward amount, applies DNA and goal progress, updates the combat trait, dispatches native actions and processes deferred display work. Scoped getters select B only inside the audited B reward contexts. The manager's persistent player/avatar pointers remain A's. Entire world AI/physics updates do not execute inside the temporary stage-data scope.

The same-species fixture can select B's pending native goal input for the existing world update. There are 4,711 selected passes, but no observed clearing of that pending work. Native herd evolution completion is therefore **NOT VERIFIED**. A brain-level threshold retains queued work pending the later progression adapter; no brain upgrade is claimed. The shared first-victory tutorial and hunt counter reaching 2/3 demonstrate remaining campaign-global presentation/quest state. Independent quests, part entitlement, species progression and editor/transition behavior remain M11/M17 work.

The SDK's bundled Detours 3 rejects the native four-byte getters with `ERROR_INVALID_BLOCK` (9). The build now uses Detours 4.0.1 source already present in the pinned injector checkout. It recognizes the verified padding after the native return instruction. No new dependency was fetched. All 27 hooks attach in the native run, including 17 additional exact code-prefix guards with six ASLR relocation entries. See [binding audit](binding-audit-02.json) and [native context audit](../../docs/m03-native-context-audit.md).

## Verification and limitations

- Release `build-05` succeeds. Focused `abi-02` passes 2/2 HOST targets in 0.18 seconds: 26 actor/award ABI assertions and 22 observation/Detours assertions. These are host fixtures, not native gameplay. No unrelated full-suite rerun was performed.
- Native PID 26936, host PID 12812, engine thread 32704. All 18,733 records are contiguous, with zero foreign callbacks, paired native combat calls, healthy trace stop and detach status 0. Native award counter destruction precedes B player retirement; original RemoveOwner runs once with no extra listener cleanup. Game and wrapper exit 0.
- The guarded disposable account uses SID `S-1-5-21-1000000000-2000000000-3000000000-1007`. All 29 personal save/creation hashes remain unchanged. The user profile was not used for the probe.
- The disposable comparison expected unchanged Games/creation contents and exited 1: `Games/Game0/GGEUserData.dat` changed from 413 to 416 bytes despite native Don't Save. The other Games files and all creations match. Four cache/event files also changed. Both complete snapshots are preserved under `local/m03-fixtures/0912-post-player02` and `0912-post-award01`; no rollback or semantic interpretation of the changed file is claimed. Do not reuse the live disposable tree as the old identical fixture without restoring its backed-up baseline.
- The 300-second WGC recording exits 0. Nine actual samples were inspected from galaxy/load through setup and the encounter period. B's fight is partly at the right edge; the recording is not a continuous close view of every hit. It ends before A's reward and final shutdown. Full trace and live observations establish those later results. Capture cadence is not native frame timing.
- Previous active Stop/scene cancellation attempts do not qualify those branches. Nonzero costs, B part unlocks, independent species, completed herd evolution, brain advancement, save/restore of B, and server-only-avatar/area activation remain NOT RUN or unqualified. No server-only avatar is introduced by M03; M04 must resolve that conditional worker requirement before adopting such an architecture.

## Identity and reproducibility

Game: installed GOG GA 3.1.0.29 Win32 executable running the **original Creature campaign, Satiria**, not a Galactic Adventures adventure. Executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; guarded inventory 108 files. SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`; injector/Detours source `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Windows 11 Pro 10.0.26200, Ryzen 7 9700X 8/16, RTX 4080 SUPER driver 32.0.15.9649; environment reference in [provenance](provenance-0.0.12.json).

Loaded bridge SHA-256 `aac74a533191b0255e0b5b383bdc6392d8e5baba92a22c35c5562aa4feb1d390`. Native trace SHA-256 `90c534414a890cfe555e0b6e9bfb40611293bf438b49f89ab19dbcb3dd38f39b`. All 40 source/artifact identities match the pre-run provenance; 36 source files have retained copies under `local/m03-awards-0912/source-0.0.12`. Working-tree HEAD remains `d106404da0b7c4531f63f98d0df2377bb897a0f7`; these changes are not committed. Older sealed evidence is preserved.

Every build/static-analysis command has exact argv, timestamps, expected exit 0 and observed exit in its adjacent `*.command.json` and log. The initial prefix generator rejected a truncated relocation word; later prefix generation passes. Intermediate host diagnostics exposed the tiny-getter Detours failure; build-04 had an incorrect dependency-lock key, corrected in build-05. Summary-01 excluded untracked native assists, summary-02 expected death before its deferred native state update, and summary-03 assumed every artifact had a source-copy field; these report-script errors are retained, corrected in summary-04, and never rerun the game. The disposable comparison failure above is retained as an actual observed difference.

Exact native launch: [completion](award-01/completion.json). Actual UI actions: [input log](award-01/input.jsonl). Recorder argv/hash: [capture request](award-01/capture-01/request.json). Analysis: [native summary](native-summary.json), [trace structure](trace-analysis.json), [capture processing](capture-extraction.json), [acceptance](acceptance.json).

Next smallest step: begin M04's real worker supervision/IPC and native readiness work using the verified actor command path. Preserve the full M00–M20 acceptance plan and carry the listed campaign-global behavior into M11; do not treat M03 as playable co-op.
