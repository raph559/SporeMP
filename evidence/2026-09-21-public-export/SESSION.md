# Public source preparation — 2026-09-21

The maintainer requested public publication of SporeMP. This export contains the already recorded M06 implementation, its reviewed history and public contributor support. Independent M07 work was active in the original development directory, so publication used an isolated checkout and did not reset or include that unfinished work.

## Privacy and preservation

The original GitHub repository remains private under `raph559/SporeMP-private-archive`, and a verified local Git bundle preserves the full original history. The public repository at https://github.com/raph559/SporeMP is independent of that archive. This avoids exposing original cached commit objects by merely changing the old repository's visibility.

A targeted review covered 3,188 original reachable blobs and commit metadata. No real API tokens or private keys were detected. Identified personal account paths, computer identifiers, account SID prefixes and commit email addresses were filtered throughout history. Native behavior, actor IDs, timestamps, acceptance limitations and outcomes were retained. Thirty-two historical evidence images were omitted; original project artwork remains included with provenance.

Filtering used official `git-filter-repo` v2.47.0, upstream commit `6f79afc8c90c592a3052e6cc53c2ca8907515bca`. Downloaded script SHA-256: `67447413e273fc76809289111748870b6f6072f08b17efe94863a92d810b7d94`. The first callback invocation rejected an incorrect callback signature before processing commits; correcting the metadata argument allowed the complete run to finish. The successful run rewrote six commits and redacted 1,166 text blobs. One SID prefix was pseudonymized consistently while retaining account RID distinctions. Private mapping values remain outside this public export.

Post-filter verification scanned all 3,179 then-reachable blob/commit objects, totaling 818,165,774 bytes, and found zero remaining matches for the identified personal values, zero evidence images and zero prohibited executable/save/dependency paths. This is a targeted check, not proof against all possible encodings. The checked preparation commit is `fcc115d5241a9371ad90236db023022496a80e0f`.

Historical original hashes refer to private artifacts, not byte-identical public copies. See [public-evidence.md](../../docs/public-evidence.md). The rewritten Git history preserves the chronological implementation sequence but changes commit identifiers. Do not merge the unfiltered private history back into the public repository.

## Public contributor setup

- English contributor guide, issue forms and pull request template.
- SHA-pinned GitHub workflow for Windows Python HOST/FIXTURE tests and the static website build/check.
- Third-party notices for the pinned SDK, loader, both Detours versions, EASTL and original artwork.
- Public-source link in both website locales and the existing Leetchi fundraiser in GitHub funding metadata.
- Ignore rules for new raw captures, traces and private credentials; reviewed exports are required.

No project license was selected during preparation. README records that public visibility alone does not assign an open-source license. Third-party terms remain separate; no compiled game package is published.

## Validation

| Command | Exit | Result |
|---|---|---|
| `git bundle create <private-backup> --branches --tags` and `git bundle verify <private-backup>` | 0 | Full original/preparation history preserved locally. |
| Official `git-filter-repo` with text/metadata callbacks and evidence-image path exclusion | 0 | Reviewed independent public history created. |
| Private `verify-public-export.py` over all reachable public objects | 0 | Zero known personal-identifier matches or prohibited paths. |
| `python -m unittest discover -s tests/unit -p 'test_*.py' -v` in the sanitized checkout | 0 | 112 tests passed in 1.146 seconds on Windows with Python 3.11. |
| `node website/build.mjs` | 0 | English/French website generated with the user-supplied fundraiser and public source link. |
| `node website/check.mjs` | 0 | 21 files and 222 local references checked. |

The source code is the previously built M06 snapshot; filtering changed a synthetic test path but no production gameplay behavior. Native tests: NOT RUN, because publication makes no native implementation change. The full C++/WPF build result is the immediately preceding private-publication record; hosted CI intentionally checks Python tooling and website generation only.

SDK and loader remain pinned in `config/dependencies.lock.json`. Game candidate identity and the bounded native acceptance constraints remain as recorded in STATUS.md. Future gameplay work still follows M07 and the full M00–M20 plan.
