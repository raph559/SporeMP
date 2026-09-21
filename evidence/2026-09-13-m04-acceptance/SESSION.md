# M04 native worker acceptance — 2026-09-13

**M04 VERIFIED for the recorded Creature fixture, two real OS profiles and current rendered, unminimized Windows desktop. Evidence: NATIVE, inspected UI/captures, ETW and separate HOST checks.** Bridge 0.0.14 and launcher 0.1.8 closed the bounded worker gate without networking or unlimited-capacity claims.

The complete session, literal commands, raw diagnostics, media and source/artifact manifests remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-13-m04-acceptance/`. This condensation adds no native acceptance.

## Accepted original-game results

| Gate | Observed result and scope |
|---|---|
| Original load/actions | IPC load completed after scene change and original AI progress. Distinct A/B native jumps had matching landings; wrong owner/stale epoch rejected. |
| Native checkpoint and identities | Original save completed, a closed checkpoint was sealed and loaded in a fresh worker. Existing A/B IDs/herds/species/archetypes matched; restore recorded **created_nouns: 0**, followed by successful actions. |
| Launcher/player independence | Recovered authority continued after actual WPF closure and after an actual normal-account player process exited. Two valid worker jumps were requested 179 ms and 1.330 seconds after player exit. |
| Concurrent workers/saves | Two original engine threads each recorded 488 successful own-Games writes, with actual I/O overlap **20.1228 ms** and conservative native save-call overlap at least **414.7126 ms**. |
| Crash containment | Retained 0.0.13 original-process crash produced supervisor exit 31 while the peer remained healthy and completed actions. No duplicate destructive replay. |
| Shutdown and launcher | Initial/recovered/concurrent games exited 0 through the original loop. Actual Start, selection/status and Stop were inspected; 0.1.8 retained an open selection menu across polls for 15.661 seconds. |

All current native traces were healthy and correlated to loaded payloads. Final games, supervisors, launcher and recorder were closed.

## Isolation and important failures

The profile/save boundary used actual OS denial probes, exact process lifetimes, native paths, ETW and closed hashes together. No resolved peer/personal access appeared during concurrent saves; ETW reported zero loss. **Unresolved file/registry correlations and undecoded events remained**, as did shared NVIDIA writes. This was not complete global-configuration isolation.

The original personal baseline changed after **normal-account Play**: ten files differed. The failed comparison was retained, not reset. Exact lifetime attribution found normal-player mutations for all ten, with completed writes for seven and rename/delete evidence for three; neither worker had resolved mutations/opens for those paths. This does not prove every replacement byte or decode unknown events. During the separate concurrent-worker phase, all 29 closed personal files matched that phase's baseline.

The two newly sealed concurrent checkpoints passed closed-tree/trace checks but were **not themselves reloaded**; the earlier sealed checkpoint supplied separate native recovery evidence. Clean closure/hash integrity did not prove power-loss durability or full B reward/campaign persistence. A native mismatched-sidecar rejection was NOT RUN; HOST validation was separate.

Normal Play created the SDK's expected spore_log.txt, initially causing strict inventory refusal before a worker started. A narrow regular-file exception fixed that without admitting other unknown files. Detached inherited output prevented Start completion; explicit stream redirection fixed it. One late before-close helper and a later already-stopped helper failed and were corrected without replaying gameplay. One worker showed an unexplained 47-second AI plateau; Stop still completed. These limits remain.

## Configuration and next gate

SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, injector `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`; GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Bridge SHA-256 `2554f3250f7985c661713b543b284ba24cc2d9265d8786ac80e4fe8a473e26f0`. Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER. Full source/build/command identities remain archived.

The twelve-artifact visual review included initial/restored scenes, distinct jumps, real player registration, concurrent scenes and final launcher states. It was not complete frame-by-frame or performance acceptance. Active-Start closure remained HOST-only; signed-in, unminimized rendering and an elevated developer operator were required. Headless/service/locked/RDP/VM operation and broader capacity were NOT RUN.

Next was M05 authority/replica separation. Internet play, arbitrary checkpoints and complete progression remained later milestones.
