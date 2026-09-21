# M02 native baseline completion — 2026-09-09

**M02 VERIFIED for the recorded original Creature fixture. Evidence: NATIVE, inspected video, native D3D9 timing and separate HOST/FIXTURE tests.** No multiplayer identity, ownership or gameplay mutation was implemented.

The complete session, literal commands, original traces/media and source/artifact hashes remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-09-m02-completion/`. This condensation adds no test or acceptance.

## Accepted native comparison

The same hash-verified closed 27-file fixture was used for Off (game 29324) and Observe (game 32344), at 2560×1440 through the guarded disposable account. Each performed two original jumps, movement, exit without saving, reload and a third jump. Four usable WGC captures and six action contact sheets were inspected: original jump/landing per input, normal movement/NPC behavior, no duplicated visible effects and matching starting scene after reload. Both read-only console outputs were verified onscreen.

All 21 correlation/provenance checks passed. Observe contained 28,614 records, zero loss/foreign callbacks, three accepted avatar calls and subsequent same-avatar landings after 0.9910–0.9948 seconds. It recorded 8,266 entity invalidations, 499,571 NPC AI and 17,485 avatar AI entries. Reload changed diagnostic identity 1104/epoch 3 to 10467/epoch 6 despite reuse of native ID 232. These were local diagnostics, not multiplayer identities.

Both game/wrapper exits were 0 with one same-thread initialize/dispose and matching payload hashes. All 29 personal files, disposable Games trees and creations retained hashes; four native cache/event files changed normally.

## Measured sample and checks

Pinned PresentMon 2.5.1 measured 60 seconds of actual D3D9 presentation per condition. Off/Observe median intervals were **16.55235/16.54640 ms**, p99 **17.9772/18.0072 ms**, with none above 33.333 ms. Whole-process CPU was 18.578/19.297 CPU-seconds over about 60.06 seconds: 0.01196 effective cores difference (+3.87% relative). This was one capped stationary pair with uncontrolled NPC/background timing, not a universal overhead bound or isolated hook cost. Video cadence was not used as native timing.

The native launch used the guarded wrapper with explicit Off/Observe and fresh names. Pair/media/provenance analysis and final verification exited 0; `ctest --test-dir build/win32 -C Release --output-on-failure -V` passed 4/4 HOST/FIXTURE targets. Exact arguments and source/artifact hashes remain archived, distinct from this condensed narrative.

SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`; loader `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`; GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER.

The earlier failed GDI/PresentMon attempts remain historical failures, not accepted captures. Sampling is not exhaustive visual equivalence; other stages, full lifetimes, damage/death, progression/inventory and save/load interception remain unqualified. Next was M03 native independent actors; the full original plan is unchanged.
