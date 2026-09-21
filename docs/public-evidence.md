# Public evidence and privacy

Public Git contains the source, reusable tools, tests and a small selection of reviewed reports. Full development traces and operational records are archived privately. Publication and archive cleanup do not change native acceptance or the M00-M20 plan.

Start with the [evidence index](../evidence/README.md). Reports retain observed successes, failures, limitations and whether a result was BUILD, HOST, FIXTURE/MOCK, NATIVE or NETWORK-REAL. Original-game acceptance requires actual original-game execution; a source publication or successful fixture test is not a substitute.

## What belongs in public Git

- Concise milestone and acceptance reports, including failed checks and unqualified behavior.
- Reproduction instructions, exact dependency pins and a small set of useful result or artifact hashes.
- Reusable analyzers and source generators under `tools/`, with their tests under `tests/`.
- Small reviewed fixtures needed by automated tests, clearly labeled as fixtures.

`docs/public-evidence-files.txt` is the explicit allowlist for `evidence/`. After reviewing a report, add its exact path to the allowlist and stage both files with `git add -- docs/public-evidence-files.txt evidence/DATE-TOPIC/SESSION.md` (substituting the actual report path). Then run `python tools/check-public-evidence.py` and inspect `git diff --cached`. The guard checks tracked paths, so a new report must be staged first. CI rejects unlisted files, raw file types, reports over 64 KiB and a combined evidence folder over 2 MiB. These are publication limits, not permission to fill the budget with raw output. JSON is allowed only for an individually listed, compact reviewed result.

Raw traces, full build logs, repeated snapshots, account or ACL inventories, captures, saves, credentials and extracted game assets do not belong in this folder. Use ignored `local/` for raw output and private storage for durable retention. A narrowly useful redacted artifact can be shared separately when a reviewer needs it, subject to a fresh content review.

## Historical artifacts

Historical reports sometimes identify a trace, capture, command log or manifest by its original `evidence/...` path. Those paths identify archived records, not files expected in a normal public clone. Links marked **archived** point here deliberately. Complete records are available to the maintainer in the private development archive; they are not public downloads and are not required for ordinary builds or HOST tests.

Before the 2026-09-21 cleanup, the entire public history was retained in a verified Git bundle and the private archive branch `archive/public-evidence-20260921`. The original unredacted development history is retained separately in that private repository. The public archive backup includes every previously published historical trace; no original evidence was discarded during cleanup. Current reports may be shortened summaries of those preserved originals.

Historical screenshots and photos were already excluded during privacy preparation. Personal account paths, computer names, account SIDs and author email addresses in the earlier public export were redacted. Original project illustrations remain in their source locations with generation provenance; they are not gameplay evidence.

## Hashes and Git history

Redaction and report condensation change bytes. Historical source revisions and hashes identify the original run or explicitly named public export, not the current files. The Detours migration manifest distinguishes its original working-copy hashes from the Git blob hashes of its publication. These records do not claim that an evolving report remains byte-identical.

The public history is filtered to exclude archived evidence paths. Rewriting history changes commit IDs; existing public clones should be replaced with a fresh clone after preserving local work. Do not merge an old public branch or any private development history into the cleaned repository, because doing so would restore archived objects. Reapply reviewed source changes to the current public branch instead. See [repository workflow](repository.md).

## Future native sessions

Keep the complete raw run privately, including exact commands, observed exits, source/dependency/artifact identities and protected-save results. Publish a concise report with enough detail to assess the claim, reproduction prerequisites and the next smallest unresolved step. Preserve failures rather than silently omitting them. Never infer a new acceptance result merely because historical artifacts were moved out of public Git.
