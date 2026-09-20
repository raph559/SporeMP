# Repository workflow

The mod, launcher, tools, tests and reviewed development evidence are maintained in the public [raph559/SporeMP](https://github.com/raph559/SporeMP) repository. The local `main` branch tracks `origin/main`. Historical commit identities and private diagnostic fields were redacted before public publication; the original history and captures are preserved separately in a private archive. See [public-evidence.md](public-evidence.md).

The presentation website has a separate public repository, [raph559/sporemp-site](https://github.com/raph559/sporemp-site), and is served at https://raph559.github.io/sporemp-site/. The `website/` sources are also recorded here as part of the development workspace. A push to the mod repository does not deploy the website; website publication follows [website.md](website.md).

## Commit and publish changes

1. Inspect `git status --short` and `git diff`. Include intended new source files as well as modifications to tracked files.
2. Run the checks appropriate to the change from [testing.md](testing.md). Record HOST/FIXTURE and native results separately.
3. Stage reviewed paths with `git add -- <paths>`, inspect `git diff --cached`, and create a focused English commit message with `git commit`.
4. Publish with `git push origin main`, then confirm `git status -sb` shows no pending changes or commits to push.

New documentation, code comments and commit messages use English. Visitor translations belong in locale files. Preserve original test evidence privately and publish only reviewed copies with any redactions documented. The adopted brief is retained with personal path identifiers redacted in this public copy.

## What is stored

Project sources, build configuration, dependency pins, documentation, tests, original project artwork and reviewed text evidence are versioned. `evidence/** -text` preserves the exported evidence bytes rather than converting line endings. Historical hash manifests refer to private original artifacts unless explicitly stated otherwise; they must not be used to assert byte identity of redacted exports.

Downloaded dependencies, generated builds, local runtime configuration, credentials, game binaries and personal saves are not repository deliverables. The existing `external/`, `build/`, `artifacts/` and `local/` exclusions remain in force. New raw traces and captures should be produced under `local/`, inspected and redacted before any explicit publication. Personal account paths, email addresses, machine names, real account SIDs and session credentials do not belong in public reports. Use the GitHub noreply commit address for new commits.

Milestone status is determined by the documented acceptance evidence. A commit, upload, successful compilation or HOST test does not mark native gameplay complete.
