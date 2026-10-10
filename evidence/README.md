# Reviewed evidence

This folder contains concise, reviewed development reports. It preserves actual successes, failed experiments, limitations and acceptance boundaries. It does not contain complete raw traces, build logs, account inventories, captures or saves. Those records remain privately archived; see [historical artifacts](../docs/public-evidence.md#historical-artifacts).

## Latest development and publication boundary

The [2026-10-11 source export](2026-10-11-source-export/SESSION.md) brings the development source through bridge 0.0.256 into this repository: the M09, M10 and M11 implementation and the native trial runner. Its native results were recorded with the development build; original-game execution with the SDK built by this checkout is NOT RUN.

M11 has 3 of 12 gates closed, each without a person in the loop: [C01 controls](2026-10-07-m11-c01-controls/INTERFACE.md), [C03 combat](2026-10-08-m11-c03-combat/SESSION.md) and [C02 eating](2026-10-10-m11-c02-eating/SESSION.md). The `RECORD-trialNNN.md` files beside them are the trial runner's own records of the runs they cite; frames, traces and saves stay in the private archive.

The earlier [M11 social progress report](2026-09-27-m11-social-progress/SESSION.md) and [2026-09-27 development update](2026-09-27-development-update/SESSION.md) keep their original payload and date boundaries.

## Milestone acceptance

| Scope | Report | Qualification boundary |
|---|---|---|
| M00-M01 | [Project and build foundation](2026-09-08-m00-m01/SESSION.md), [native lifecycle](2026-09-08-m01-native/SESSION.md), [player launcher](2026-09-08-m01-launcher-redesign/SESSION.md) | Pinned original-game configuration and the recorded launcher correction. |
| M02 | [Native observation baseline](2026-09-09-m02-completion/SESSION.md) | Matched native Off/Observe Creature fixture; no networking acceptance. |
| M03 | [Native actor rewards](2026-09-12-m03-awards/SESSION.md), [acceptance](2026-09-12-m03-awards/acceptance.json) | Original actor combat and scoped reward result; full Creature progression remains unqualified. |
| M04 | [Worker and checkpoint acceptance](2026-09-13-m04-acceptance/SESSION.md), [completion](2026-09-13-m04-completion/SESSION.md) | Rendered workers, measured isolation and exact saved identity recovery in the recorded fixture. |
| M05 | [Replica acceptance](2026-09-14-m05-ability/acceptance.md), [session](2026-09-14-m05-ability/SESSION.md) | Recorded living Creature fixture and authority/replica guards, with explicit scene and presentation limits. |
| M06 | [Network acceptance](2026-09-14-m06-network/acceptance.md), [session](2026-09-14-m06-network/SESSION.md) | Two original-game clients and one worker; living actors, movement, jumps and reconnect. Death/respawn limitations remain recorded. |
| M07 | [Shared encounter acceptance](2026-09-21-m07-completion/acceptance.md), [session](2026-09-21-m07-completion/SESSION.md) | Pinned original Creature encounter, contested feeding, result-preserving reconnect and fresh-session reset under actual packet impairment. |
| M08 | [Native content and editor acceptance](2026-09-22-m08-completion/SESSION.md), [verification](2026-09-22-m08-completion/verification.json) | Native creation transfer and properties, exact dependency refusal, canonical saved-world checks, commit/cancel and concurrent original gameplay. Locally approved creations and developer controls; M09 durability remains next. |
| M09 | [Persistence acceptance](2026-09-22-m09-persistence/acceptance.md) | Checkpoint recovery of both original players, tested inventory/progression and creation ownership; explicit protected recovery workflow, not unattended hosting. |
| M10 | [Native recovery report](2026-09-26-m10-native-state/NATIVE-RECOVERY-086.md), [paired save and return](2026-09-26-m10-native-state/LIVE-TRANSFER-06.md) | Separate Creature locations, meeting, return, paired Save/restart/reconnect and the recorded worker-fault cases. |
| M11 (3/12 gates) | [C01 controls](2026-10-07-m11-c01-controls/INTERFACE.md), [C03 combat](2026-10-08-m11-c03-combat/SESSION.md), [C02 eating](2026-10-10-m11-c02-eating/SESSION.md) | Pinned Creature fixtures; in-process scripted input, UI readback and frames inspected by the assistant. Nine gates remain open. |

The complete M00-M20 plan is in [MILESTONES.md](../MILESTONES.md). A milestone status is not a claim that all stages or configurations are supported. Earlier native evidence remains bound to its recorded payload; the Detours 4 SDK build below has separate, incomplete native qualification.

## Repository and tooling changes

- [M09 to M11 reviewed export](2026-10-11-source-export/SESSION.md): public Release build and HOST/FIXTURE results of this checkout, privacy review and what stays private; native execution of the public SDK build NOT RUN.
- [Native trial runner](2026-10-07-native-trial-runner/SESSION.md): one command restores the fixture, runs a scenario in three original-game processes and writes the record and verdict.
- [Website rebuild](2026-09-27-website-rebuild/SESSION.md): the source of the current presentation site.
- [M11 first increment](2026-09-27-m11-start/SESSION.md): Creature coverage and static social-reward investigation, read-only trace reducer and HOST/FIXTURE checks; new native acceptance NOT RUN.

- [M07/M08 reviewed export](2026-09-22-m08-publication/SESSION.md): current public Release build, 15/15 HOST/FIXTURE targets, source/privacy review and updated English/French website; native results retain their frozen development payload qualification.
- [Detours 4 migration and GPL licensing](2026-09-21-detours4-license/SESSION.md): BUILD/HOST/FIXTURE validation, with original-game execution explicitly NOT RUN.
- [Evidence cleanup](2026-09-21-evidence-cleanup/SESSION.md): private preservation, public curation, source-tool relocation and publication checks.
- [Public export](2026-09-21-public-export/SESSION.md): historical privacy preparation.
- [Presentation website](2026-09-21-website/SESSION.md): site scope and publication.

Other dated reports retain narrower historical implementation decisions and failures. Reusable analysis and generation code is under `tools/native/`; current reproduction commands are in [testing instructions](../docs/testing.md) and the protocols under `tests/engine/`. Historical script names in a report may identify archived one-off work rather than a current public command.

## Adding a report

Keep raw output under ignored `local/`. Review the smallest useful Markdown or JSON summary, add its exact path to [the allowlist](../docs/public-evidence-files.txt), and stage the report and allowlist before running `python tools/check-public-evidence.py`. Inspect the staged diff before committing. The guard reads tracked paths; the [public evidence policy](../docs/public-evidence.md) gives the staging command, size limits and private archive handling.
