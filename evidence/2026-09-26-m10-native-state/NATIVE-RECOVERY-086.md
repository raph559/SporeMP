# M10 original-worker recovery and global admission

Date: 2026-09-26. Status: **VERIFIED for the recorded original Creature location foundation**. Evidence classes: **NATIVE**,

**BUILD**, **HOST**, and **FIXTURE**, distinguished below. This report records

the M10 acceptance decision for the pinned native fixture. Full campaign and stage requirements remain intact.

Raw traces, process records, profiles, captures and credentials remain under

ignored `local/m10-native-2026-09-26/`.

## Implementation and pinned environment

Bridge 0.0.86 obtains an authenticated global admission grant in the verified

SDK initialization callback, before original startup generation. Initialization

is outside DllMain. An independent TLS control channel renews the grant while

ordinary location publication is fenced. Original producer entry points check

the grant before entering an original body that can mutate shared state.

Nonowners may execute the observed original spice operation only when its

selected value is already initialized and unchanged. A reader can follow a

higher generation of the same global owner; that refresh cannot promote it,

change the selected head or revive an expired grant.

Recovery binds actual original process identities and retained exit handles,

rotates authority generations, and restores the exact approved native player

artifact. Before commit, rollback resumes the source only after provisional

destination removal. After commit, recovery retains destination ownership.

A paired selected Save requires native retirement of a departed secondary

before its resident-only baseline can be published.

| Pin | Value |

|---|---|

| Original executable SHA-256 | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |

| SDK commit | `cbf9206b9a823f0911cd9be0217104a49d72380b` |

| SDK patch SHA-256 | `7ca2bce9880f1f6a8423f4bb28c73e5c4cf8ea10a4700cf9cf1e3edbb4479b44` |

| Bridge 0.0.86 SHA-256 | `2ae7476b9f6327a151fa24e71b092fe386a2fa1752340cc0c0d7e82d1b885eb1` |

| NativeHost SHA-256 | `5d3052f0f02a1a44527abe66ffe62bdb7a8874bf510a03559923c4ac5a9c4672` |

| Frozen source manifest SHA-256 | `0e973dcfd0c81eee380ebc5fc41a85b564255774dbd737e87ef65f7a1c58b383` |

| Selected compatibility/content identity | `06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b` |

| Complete initialized owner-world manifest SHA-256 | `0bdf260d754588eda2c9d8ff3f3a16befad6253bfb82c07e4174c2e65ea410a8` |

| OS | Windows 11 Pro 10.0.26200 |

| CPU / GPU | Ryzen 7 9700X / GeForce RTX 4080 SUPER |

| NVIDIA driver | 32.0.15.9649 |

| Native locations | Satiria: home9519123, terrain684339943; Haagos: home3788854, terrain684339955 |

The 31-file input is one complete closed original owner Save. Its Satiria

location remains unchanged from the earlier 30-file selected world. No partial

global-file merge or replacement simulation is used. Each trial stages separate

disposable OS profiles and restores all six profile roots afterwards.

## Executed build and host checks

```powershell

pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release

ctest --test-dir build/win32 -C Release --output-on-failure

python -m unittest discover -s tests/unit -p test_native_global_checkpoint.py

python -m unittest discover -s tests/unit -p test_native_global_publication.py

```

Observed: build exit 0 with no drift against the captured source manifest;

CTest exit 0, **19/19** in 58.16 seconds. The two subsequent focused Python

suites exited 0 with **6/6** and **7/7**. These are BUILD/HOST/FIXTURE results.

They do not start SPORE. The later Python change validates preservation of an

already initialized spice value under a matching authenticated owner grant;

it does not label preservation as a newly generated consequence.

## Completed native interruption checks

The table names private trial directories. Each listed case retains actual

game/host exit codes, command bytes and matching coordinator receipts, exact

artifact and native witness hashes, recovered original-game traces, and profile

restoration results. Intentional game termination returned -1; its supervisor

returned 31. Healthy replacement/survivor shutdown returned 0/0. The coordinator

remained alive during each worker recovery and subsequently stopped with exit 0.

| Durable cut | Source killed | Destination killed |

|---|---|---|

| 0 prepared | `fault-086-source-0-86011` | `fault-086-destination-0-86021` |

| 1 fence requested | `fault-086-source-1-86111` | `fault-086-destination-1-86122` |

| 2 source fenced | `fault-086-source-2-86211` | `fault-086-destination-2-86221` |

| 3 artifact bound | `fault-083-bound-source` (0.0.83) | `fault-086-destination-3-86321` |

| 4 restoration requested | `fault-086-source-4-86412` | `fault-086-destination-4-86422` |

| 5 destination restored | `fault-086-source-5-86511` | `fault-086-destination-5-86522` |

| 6 ownership committed | `fault-086-source-6-86611` | `fault-086-destination-6-86621` |

| 7 activation requested | `fault-086-source-7-86711` | `fault-086-destination-7-86721` |

| 8 destination active | `fault-086-source-8-86811` | `fault-086-destination-8-86821` |

| 9 discard requested | `fault-086-source-9-86911` | `fault-086-destination-9-86921` |

| 10 destination discarded | `fault-086-source-10-87011` | `fault-086-destination-10-87021` |

| 11 rollback requested | `fault-086-source-11-87111` | `fault-086-destination-11-87121` |

| 12 source restored | `fault-086-source-12-87211` | `fault-086-destination-12-87221` |

| 13 source activation requested | `fault-086-source-13-87311` | `fault-086-destination-13-87321` |

| 14 abort completed | `fault-086-source-14-86411` | Phase13 destination death is followed by normal resident replacement after abort reaches14 |

| 15 transfer completed | Empty source already retired | `fault-086-destination-15-86531` |

These are 29 reviewed 0.0.86 trials and one earlier 0.0.83 source recovery.

Closed review command:

```powershell

python local/m10-native-2026-09-26/analyze-faults-086.py fault-review-086-final.json

```

Exit 0, 29 cases; output SHA-256

`0307802becf91129f12b08791929538d8b7242d0225003784b33314746c75c49`.

For each successful 0.0.86 case the review verifies pinned native process/thread

identity and callback sequence, zero foreign callbacks, source census stability,

matching command receipts, original jump return followed by that actor's landing,

closed runtime logs and protected-file restoration. Destination imports preserve

the portable source census and leave the original resident census unchanged.

Primary recovery overlays reuse the original three roots, create no nouns and

preserve recorded vitals exactly. A full collection census after a primary

overlay is **NOT EMITTED**, and is not inferred from the scalar checks.

At phase 0, cancellation and normal higher-generation replacement are explicit.

Pre-export source failure at phase 1 returns to the selected checkpoint and

records checkpoint recovery; it does not invent an uncompleted export. At phase

2, the actual closed export is approved and restored. At phases 6 and 8, the

retired primary source is not recreated; the committed destination retains the

traveler. Both phase-4 trials verify the coordinator was still at phase 4 after

the actual original process died.

For fast phases7/9/11/13, the fault fixture observes publication of the exact

48-byte durable CURRENT manifest, briefly suspends only the owned coordinator's

threads, kills the original worker through its retained process handle, and

checks that the manifest remains identical through actual death. It always

resumes the coordinator in a finally block. Each recorded hold is below the

two-second fixture limit. The corresponding authenticated admin receipt identifies

the phase at that manifest revision; fresh status is read after the kill. Original

game code is not patched to delay the action. This explicit scheduling fixture

places death at a durable intent that a normal CLI round trip can otherwise miss.

Both sides subsequently complete the applicable commit or abort policy and

original gameplay. The .86 source2 case binds its actual closed export before

taking the same source-restoration path covered earlier by the .83 source3 cut.

## Native global producer checks

`global-admission-085-02` obtained the owner grant before all 32 observed

original AssignSpice callbacks. The owner executed original Save successfully

and closed normally. A nonowner loading the older uninitialized input was denied

at planet28717115 with `unowned_spice_would_mutate`, before entering the original

mutating body (game exit19732, host31). The complete closed owner Save became

the initialized 31-file input above. `global-readers-085-01` executed 32 original

nonowner spice no-ops with active guards and unchanged values. The 0.0.86 fault

trials additionally exercise readers surviving a higher global-owner generation.

This establishes the observed Creature producer admission and selected original

spice behavior. Space economy, diplomacy, disasters and other unobserved services

are not native-qualified by these callbacks. Their full stage behavior remains

subject to the original M11–M19 acceptance plan.

## Published paired Save and terminal recovery

`paired-086-02` completes outward transfer86903, original paired Save, whole-owner

checkpoint14 publication and fresh paired reload. Return86904 exports the

current visitor, overlays the original home primary, removes the departed

secondary and completes at revision26. An actual kill of the returned traveler

then recovers that exact artifact at a higher generation, completing at

revision32. A subsequent actual kill of the surviving Haagos resident loads the

selected paired Save, which still contains the departed visitor. Maintenance

removes exactly player4421/avatar4423/herd4422 before publishing the new

resident-only baseline; cleanup completes at revision36.

Both the ordinary return and recovered resident remove exactly three nouns,

change the player census from two to one, leave the original resident's full

recorded collection census unchanged, preserve its primary binding/stage and

resume original AI after a measured pause. The two recovered workers execute

original jump/landing pairs in 1006.5112ms and 1008.9423ms. All six original

process traces have coherent PID/thread/sequence and zero foreign callbacks.

The two intentional deaths are -1/31; the other four game/host pairs close 0/0.

Both profile staging cycles restore all three profiles and protected files.

```powershell

python local/m10-native-2026-09-26/analyze-paired-086.py

```

Exit0; closed review SHA-256

`72f0225f7ace5fea92f97c12edb7df67a86642235c69ef351194c6d530dfa0d5`.

Whole checkpoint SHA-256

`a93a5661111ceab74e52ae157cf6a71c5d97c70f905ae25ff7c105aee78dfa5b`;

publication approval

`125345a8f2c061fc071c8c6e0758ece238750878904c59d42fd6bf14f0470880`;

proof `dfe03d1a0687e149c812b4a2603c391d4c48e9d9532cd75f4efe5a62ecb407d2`.

The recorded global effect is preservation of initialized planet5959750 spice

480253282, with zero new assignments; it is not counted as a second consequence.

The checkpoint validator source SHA-256 is

`a1f3cbde2aba5ca1a94bcb7057d1e3cc33f4a550c71526e7a80e2b0c46ee824a`.

Five actual frames from each original Save/recovered traveler/recovered resident

capture were inspected. They show the expected Haagos or Satiria scene, native

HUD and changing original poses. The sixth contact-sheet tile is unused. The

matching native trace establishes command/jump/landing attribution. These

private captures contain no desktop fallback. This trial's first replacement

launch was refused by a test-script check referencing the already closed earlier

owner; correcting that reference to the live published owner allowed the same

recovery to continue, without a second fault or discarded native outcome.

## Retained unsuccessful attempts

- `fault-086-destination-5-86521`: a strict live trace read detected an append

  race before the intended kill. Delayed handling then reached the 120-second

  maintenance deadline. The retry86522 uses a bounded read retry and passed.

- `fault-086-destination-1-86121`: the source completed fencing while the

  replacement loaded; the harness tried a stale phase/revision. The command

  was refused and maintenance later expired. Retry86122 reads fresh status and

  passed. The timeout remains a failed trial.

- `fault-086-destination-0-86021`: the first post-recovery action used canonical

  player2 where WorkerControl requires local primary slot1. It was rejected.

  The retained corrected request produced the observed original jump/landing.

- `paired-086-01`: paired Save and publication passed, but the runner requested

  return fencing before destination startup completed. No ownership moved.

  The destination subsequently reached its maintenance deadline; the owner

  reached its checkpoint deadline. Both failures and profile restoration are

  retained. The retry adds measured native maintenance readiness before fencing.

## Final cleanup interruption and reconnect checks

All four secondary-cleanup fault cases pass with the frozen 0.0.86 bridge and
NativeHost. The last three use the corrected coordinator described below:

| Actual durable cut | Source killed | Destination killed |
|---|---|---|
| 16 cleanup requested | `secondary-086-source-16-retry` | `secondary-086-destination-16-fixed` |
| 17 secondary removed | `secondary-086-source-17-fixed` | `secondary-086-destination-17-fixed` |

Each original paired Save is published before the return. Actual process death
is retained, current traveler state remains at its committed home, and the
replacement resident removes any stale visitor from the selected paired Save
before ordinary publication. The resident census is unchanged through removal;
exact native removal/restore witnesses, resumed baselines, original jump/landing,
clean normal exits and both profile restoration cycles are verified.

The first source16 attempt failed before fault injection with Windows atomic
publication error5 at revision23/phase8. During manual handling, the source
reached its maintenance deadline and both grants ultimately fenced their
workers. The failed world and actual19732/31 exits remain recorded. Its separate
fresh retry passes. The runner handles only this exact rejected publication,
only while a fresh status proves the phase/revision unchanged, with at most two
immediate retries. It does not retry an unknown outcome or rewrite a failed
receipt. The secondary watcher polls with delete sharing at a ten-millisecond
interval and retains the same bounded coordinator-hold/death proof.

The first destination16 trial exposed a production ordering bug. Recovery
overwrote the still-pending source removal command; the original source then
completed removal but its receipt and resident baseline were rejected. That
failed trial is retained. The coordinator now returns
`native_transfer_source_cleanup_pending` while phase16 still owns the source
command. Recovery proceeds after the source's original receipt is durable.
A HOST regression proves that refusal leaves the phase/revision/generation
unchanged, then accepts the source receipt, destination restoration and both
resident baselines. The original-game fixed retry verifies the surviving
resident, destination recovery and normal closure.

```powershell
cmake --build build/win32 --config Release --target SporeMP.Coordinator sporemp_transfer_control_tests --parallel 4
ctest --test-dir build/win32 -C Release -R '^transfer_control_host$' --output-on-failure
```

Both exit0; the modified HOST test passes in21.11s. Corrected coordinator
SHA-256: `36ffa97b34934655af8a158c1d3f446c6bdc8b48410a6b33472e0b74d4394037`.
Corrected170-file source manifest SHA-256:
`4a8b8f4a9151b86e4bfae93c994b62de8d0fce3e463a61fb72c71b2b36a0a9c9`.
Only `coordinator_transfer.inc` and its HOST regression differ from the
earlier manifest. Bridge and NativeHost hashes are unchanged. Earlier trials
retain their original coordinator pins; they are not relabeled as reruns.

After the final destination recovery, an original replica joins as canonical
player1 with a reader-only global grant, closes normally, and a second original
process reconnects as that same player. Its network jump intention reaches the
current home authority and produces the matching original jump and landing.
The inspected five one-second samples show the original green Satiria Creature
scene, HUD, minimap and changing actor poses, including an airborne frame. The
sixth contact-sheet tile is unused. The independently correlated native landing
occurred 1004.0552ms after the authoritative jump returned. Visual-review
SHA-256: `ca59d33a05f784a041939e2b19ca835c00b2527791c581be4200c5b91c062f87`.
All final original processes and
their supervisors close normally before the three profiles are restored.

```powershell
python local/m10-native-2026-09-26/analyze-secondary-086.py
python local/m10-native-2026-09-26/export-recovery-results.py
```

Both commands exit0. Secondary closed-review SHA-256: `eb2793d5cd197af1aac9d06483bea22e7ef13dadba5ca1c200686d1cc7c2e4de`.
The [reviewed result metadata (archived)](../../docs/public-evidence.md#historical-artifacts) contains an explicit
allowlist of trial names, phases, revisions, exit codes and evidence hashes.
It contains no raw profiles, credentials, private account inventory or captures.

## Acceptance decision and limits

**M10 is VERIFIED for the recorded Satiria/Haagos original Creature foundation.**
Evidence combines the original separate scenes and live meeting/return runs,
trial06's original paired Save/publication/restart/reconnect, native global
producer admission and selected consequence adoption, and **36 successful native
worker-fault cases**: 29 primary cases on .86, four secondary-cleanup cases on
.86, two terminal paired-Save faults on .86, and the earlier .83 source3 fault.
Pre-export source failure explicitly recovers the selected checkpoint; it does
not pretend to restore an export that never completed. Committed recovery keeps
destination ownership; abort resumes the source only after provisional removal.

The qualification uses real original gameplay and the pinned native state that
was exercised. It does not certify arbitrary save compatibility, complete
Creature progression, native Space travel, unobserved Space economy/diplomacy
services, Internet release operation or the full campaign. M11–M19 retain all
original requirements. Full primary collection census after a primary overlay
is not emitted; the report does not invent it. Historical failed attempts and
per-operation `full_milestone_qualified:false` diagnostics remain unchanged:
individual helper acknowledgments are not the combined acceptance decision.

The earlier build passes19/19 CTest cases; focused changed Python validators
pass6/6 and7/7. The coordinator correction passes its affected HOST regression;
the corrected170-file manifest records the two intentional changes. No original game
was launched by a build/unit-test command. All native launches were explicit
developer runs against isolated disposable profiles. Personal-save checks pass.
