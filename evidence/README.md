# Reviewed evidence

This folder contains concise, reviewed development reports. It preserves actual successes, failed experiments, limitations and acceptance boundaries. It does not contain complete raw traces, build logs, account inventories, captures or saves. Those records remain privately archived; see [historical artifacts](../docs/public-evidence.md#historical-artifacts).

## Milestone acceptance

| Scope | Report | Qualification boundary |
|---|---|---|
| M00-M01 | [Project and build foundation](2026-09-08-m00-m01/SESSION.md), [native lifecycle](2026-09-08-m01-native/SESSION.md), [player launcher](2026-09-08-m01-launcher-redesign/SESSION.md) | Pinned original-game configuration and the recorded launcher correction. |
| M02 | [Native observation baseline](2026-09-09-m02-completion/SESSION.md) | Matched native Off/Observe Creature fixture; no networking acceptance. |
| M03 | [Native actor rewards](2026-09-12-m03-awards/SESSION.md), [acceptance](2026-09-12-m03-awards/acceptance.json) | Original actor combat and scoped reward result; full Creature progression remains unqualified. |
| M04 | [Worker and checkpoint acceptance](2026-09-13-m04-acceptance/SESSION.md), [completion](2026-09-13-m04-completion/SESSION.md) | Rendered workers, measured isolation and exact saved identity recovery in the recorded fixture. |
| M05 | [Replica acceptance](2026-09-14-m05-ability/acceptance.md), [session](2026-09-14-m05-ability/SESSION.md) | Recorded living Creature fixture and authority/replica guards, with explicit scene and presentation limits. |
| M06 | [Network acceptance](2026-09-14-m06-network/acceptance.md), [session](2026-09-14-m06-network/SESSION.md) | Two original-game clients and one worker; living actors, movement, jumps and reconnect. Death/respawn limitations remain recorded. |

The complete M00-M20 plan is in [MILESTONES.md](../MILESTONES.md). A milestone status is not a claim that all stages or configurations are supported. Earlier native evidence remains bound to its recorded payload; the Detours 4 SDK build below has separate, incomplete native qualification.

## Repository and tooling changes

- [Detours 4 migration and GPL licensing](2026-09-21-detours4-license/SESSION.md): BUILD/HOST/FIXTURE validation, with original-game execution explicitly NOT RUN.
- [Evidence cleanup](2026-09-21-evidence-cleanup/SESSION.md): private preservation, public curation, source-tool relocation and publication checks.
- [Public export](2026-09-21-public-export/SESSION.md): historical privacy preparation.
- [Presentation website](2026-09-21-website/SESSION.md): site scope and publication.

Other dated reports retain narrower historical implementation decisions and failures. Reusable analysis and generation code is under `tools/native/`; current reproduction commands are in [testing instructions](../docs/testing.md) and the protocols under `tests/engine/`. Historical script names in a report may identify archived one-off work rather than a current public command.

## Adding a report

Keep raw output under ignored `local/`. Review the smallest useful Markdown or JSON summary, add its exact path to [the allowlist](../docs/public-evidence-files.txt), and stage the report and allowlist before running `python tools/check-public-evidence.py`. Inspect the staged diff before committing. The guard reads tracked paths; the [public evidence policy](../docs/public-evidence.md) gives the staging command, size limits and private archive handling.
