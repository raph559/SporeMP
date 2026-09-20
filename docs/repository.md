# Repository workflow

The mod, launcher, tools, tests and development evidence are maintained in the private [raph559/SporeMP](https://github.com/raph559/SporeMP) repository. The local `main` branch tracks `origin/main`. The original local history is retained.

The presentation website has a separate public repository, [raph559/sporemp-site](https://github.com/raph559/sporemp-site), and is served at https://raph559.github.io/sporemp-site/. The `website/` sources are also recorded here as part of the development workspace. A push to the mod repository does not deploy the website; website publication follows [website.md](website.md).

## Commit and publish changes

1. Inspect `git status --short` and `git diff`. Include intended new source files as well as modifications to tracked files.
2. Run the checks appropriate to the change from [testing.md](testing.md). Record HOST/FIXTURE and native results separately.
3. Stage reviewed paths with `git add -- <paths>`, inspect `git diff --cached`, and create a focused English commit message with `git commit`.
4. Publish with `git push origin main`, then confirm `git status -sb` shows no pending changes or commits to push.

New documentation, code comments and commit messages use English. Visitor translations belong in locale files. Preserve the adopted implementation brief and historical test evidence verbatim.

## What is stored

Project sources, build configuration, dependency pins, documentation, tests, original project artwork and recorded evidence are versioned. `evidence/** -text` preserves recorded evidence bytes rather than converting line endings; whitespace in original logs is intentionally retained.

Downloaded dependencies, generated builds, local runtime configuration, credentials, game binaries and personal saves are not repository deliverables. The existing `external/`, `build/`, `artifacts/` and `local/` exclusions remain in force. Raw diagnostic traces include machine paths and metadata, so the development repository remains private. A public source release would require a separate review of that historical material.

Milestone status is determined by the documented acceptance evidence. A commit, upload, successful compilation or HOST test does not mark native gameplay complete.
