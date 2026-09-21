# Repository workflow

The mod, launcher, tools, tests and reviewed development evidence are maintained in the public [raph559/SporeMP](https://github.com/raph559/SporeMP) repository. The local `main` branch tracks `origin/main`. Historical commit identities and private diagnostic fields were redacted before public publication; the original history and captures are preserved separately in a private archive. See [public-evidence.md](public-evidence.md).

In the original private development workspace, `origin` points to `raph559/SporeMP-private-archive` and active milestone work continues there. Its public counterpart is the independent checkout at `local/public-preparation/public-export`. Review and transfer intended source changes to that checkout; never merge or push the unfiltered original history into the public repository. The commands below refer to the public checkout when publishing public changes.

The presentation website has a separate public repository, [raph559/sporemp-site](https://github.com/raph559/sporemp-site), and is served at https://raph559.github.io/sporemp-site/. The `website/` sources are also recorded here as part of the development workspace. A push to the mod repository does not deploy the website; website publication follows [website.md](website.md).

## Commit and publish changes

1. Inspect `git status --short` and `git diff`. Include intended new source files as well as modifications to tracked files.
2. Run the checks appropriate to the change from [testing.md](testing.md). Record HOST/FIXTURE and native results separately.
3. Stage reviewed paths with `git add -- <paths>`, inspect `git diff --cached`, and create a focused English commit message with `git commit`.
4. Publish with `git push origin main`, then confirm `git status -sb` shows no pending changes or commits to push.

New documentation, code comments and commit messages use English. Visitor translations belong in locale files. Preserve original test evidence privately and publish only reviewed copies with any redactions documented. The adopted brief is retained with personal path identifiers redacted in this public copy.

## What is stored

Project sources, build configuration, dependency pins, documentation, tests, original project artwork and explicitly allowlisted evidence reports are versioned. `evidence/** -text` preserves the reviewed report bytes rather than converting line endings. Historical hash manifests refer to their recorded original artifacts or named public exports; they must not be used to assert byte identity of condensed current reports.

Raw historical evidence is excluded from the cleaned public Git history as well as the latest tree. The complete pre-cleanup history is backed up privately. Existing public clones need a fresh clone after local work is saved; reapply intended source changes instead of merging an old branch. Only `main` existed at cleanup, with no public forks, open pull requests or tags. The private M07 development checkout remains independent and is not rewritten.

For a new public evidence report, review its content, add its exact path to `docs/public-evidence-files.txt`, and stage both files before running `python tools/check-public-evidence.py`. This check uses Git's tracked file list, so it follows staging for a new report. Inspect the staged diff before committing. CI enforces the file types, allowlist and size limits. Reusable analysis code belongs in `tools/`; raw test outputs belong under ignored `local/`.

Downloaded dependencies, generated builds, local runtime configuration, credentials, game binaries and personal saves are not repository deliverables. The existing `external/`, `build/`, `artifacts/` and `local/` exclusions remain in force. New raw traces and captures should be produced under `local/`, inspected and redacted before any explicit publication. Personal account paths, email addresses, machine names, real account SIDs and session credentials do not belong in public reports. Use the GitHub noreply commit address for new commits.

Milestone status is determined by the documented acceptance evidence. A commit, upload, successful compilation or HOST test does not mark native gameplay complete.
