# M04 current-desktop native increment — 2026-09-13

**Historical result: M04 IN_PROGRESS. Evidence: NATIVE, inspected captures, ETW and separate HOST checks.** Bridge 0.0.13 established bounded unattended actions, concurrent original workers, crash containment and shutdown; restored bridge ownership remained missing.

The complete session, literal commands, raw diagnostics, media and source/artifact manifests remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-13-m04-native/`. This condensation adds no native acceptance.

## Native outcomes

| Run | Result |
|---|---|
| Initial worker | Native IPC A/B actions; 629 contiguous records, clean game exit 0. |
| Concurrent worker 01 | Actions while unfocused; deliberate original-process termination -1, supervisor exit 31/status crashed. No clean-disposal claim. |
| Concurrent worker 02 | Actions, minimize/restore, peer-crash survival, further actions and original Save; game/supervisor exit 0. |
| Reload worker 02 | Manual native checkpoint reload, live scene/AI, but A/B bridge ownership remained zero. Stale requests rejected; game exit 0. |

Four unattended probes each rejected wrong owner/stale epoch then produced distinct accepted A/B jumps with matching native landings after 0.8366693–1.0087859 seconds. Queued acknowledgements alone were not counted. Operator-pipe closure left authority running; actual launcher/player closure was still pending.

Minimizing one worker stalled its app/AI counters for 15.268 seconds while its peer continued. Restoring before the 30-second timeout resumed progress. Native modal menus paused AI while app updates continued. The qualified arrangement required an unminimized rendered signed-in desktop. A short CPU/memory sample was not a capacity or frame-time benchmark.

## Isolation, save and limitations

Real account tokens/resource paths identified distinct profiles. ETW reported zero loss and no resolved peer/personal opens or completed mutations, but retained unresolved file/registry events and shared NVIDIA writes. Concurrent save writes had not yet been tested.

Original Save displayed the successful Satiria confirmation; the closed checkpoint was preserved and a new process loaded it. Missing bridge binding did not prove B's native noun had disappeared: a census/adoption experiment was required. All 29 personal hashes remained unchanged. Worker 02's own Save changed its checkpoint; later reload/closure left that closed Games tree unchanged.

Three clean game exits had shutdown delays 0.523/0.530/0.536 seconds. Only the saved run directly captured supervisor exit 0; other detached supervisors had final stopped status without captured exit codes. The reload video ended at the menu; a separately inspected live scene image supplied actual rendered-load evidence.

A native-analysis script failed syntax before a corrected read-only analysis passed; no game was repeated for the reporting error. Archived commands preserve builds, launch/control exits, capture and ETW metadata.

Configuration: GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, injector `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`; Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER.

Next was controlled native load/save plus existing-noun identity adoption, actual player/launcher independence and concurrent-save attribution. Networking and broader desktop/stage support remained unqualified.
