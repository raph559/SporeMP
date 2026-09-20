# Initial private mod repository publication — 2026-09-21

## Scope and outcome

The user requested the missing commits and online publication of the mod itself. Created the private repository https://github.com/raph559/SporeMP, retained the existing two local commits and recorded the accumulated implementation without inventing per-milestone historical commits. The separate public website repository and deployment remain intact.

- Original HEAD: `d106404da0b7c4531f63f98d0df2377bb897a0f7`.
- Core implementation/evidence commit: `801d8a9e9ff4a672f535ae92aec755e5fb968d68` — `Record native multiplayer development through M06`.
- Website commit: `00ba28f1ad2ec45b5fe5b59331f92f5152834fae` — `Add the bilingual project website and Leetchi support`.
- Initial published source tree: `9088973b3be7fef45a6f7e2371f49b312a723b53`.
- `first-push-verification.json` records authenticated API confirmation that the private repository's `main` matches the local website commit.
- This publication record, current build/test evidence and repository instructions are recorded in a final documentation commit.

The README now describes bounded M00–M06 progress, bridge/NativeHost 0.0.30, launcher 0.1.9 and the unfinished full campaign. No milestone was promoted and no native gameplay was launched for this publication task.

## Publication review

A read-only inventory reviewed 3,368 tracked/nonignored candidate files totaling 827,891,273 bytes and 210 historical blobs totaling 3,783,503 bytes. Targeted scans found no real GitHub/API tokens, PEM private keys, JWTs, embedded URL credentials, native invitation secrets or plaintext passwords. Apparent matches were serialization code, explicit test fixtures or authorization prose. Representative Join/Rejoin screenshots mask invitations. No game binaries, save databases, packaged dependencies or generated executables were included. Private paths, machine metadata and SIDs remain in historical diagnostic evidence; the repository is private.

The review is a targeted publication check, not proof that arbitrary secret encodings cannot exist. Existing ignored `external/`, `build/`, `artifacts/` and `local/` directories remain excluded. Authentication helpers stay under ignored `local/`, use existing GitHub credentials in memory and never print tokens.

Raw JSONL evidence was retained: 801,391,014 raw bytes compress to 38,981,207 bytes at zlib level 1. Git object packs after the two commits total approximately 31.99 MiB. No new source artifact exceeds 50 MB. Added `evidence/** -text` to preserve evidence bytes. The first index check identified older evidence entries previously normalized by Git; `git add --renormalize -- evidence` reindexed the unchanged working files with the new attribute. The subsequent check verified all 3,156 core evidence files match their indexed blob bytes exactly. Original historical commits were not rewritten.

## Current-source validation

| Command | Exit | Expected versus observed |
|---|---|---|
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` before rebuilding | 0 | All 9 prebuilt HOST/FIXTURE targets passed. A subsequent fresh build was run to verify current source. |
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | Existing pinned dependencies validated; current native/launcher targets built successfully. No fetching, installation or game launch. |
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` after rebuilding | 0 | All 9 targets passed in 11.32 seconds, including 112 Python tests, 54 launcher HOST assertions and 2,540 network assertions. |
| `git diff --cached --check` | 2 | Historical evidence retains whitespace/CRLF from original logs. These records were not cosmetically rewritten. |
| `git diff --cached --check -- . ':!evidence'` | 0 | Source/documentation whitespace check passed. |
| `python local/verify-publication-index.py` after reindexing evidence | 0 | No evidence-byte mismatch and no prohibited staged path. |
| `python local/mod-publication.py create` | 0 | Verified account identity and created empty private `raph559/SporeMP`. |
| `git remote add origin https://github.com/raph559/SporeMP.git` | 0 | Configured the new private repository as origin. |
| `git push --set-upstream origin main` | 0 | Published existing history and both new commits; configured branch tracking. |
| `python local/mod-publication.py verify` | 0 | GitHub API confirmed private visibility and exact remote/local SHA match. |
| `git rev-parse 'HEAD^{tree}'` | 0 | Recorded the initial publication tree above. An earlier unquoted PowerShell invocation failed to parse; quoting corrected it without changing repository content. |
| `git fsck --full --no-reflogs` | 0 | No corrupt/missing reachable objects; only harmless dangling blobs from prior indexing were reported. |

Full current-source build/test logs and command metadata are stored beside this report. `build-hashes.json` records log, configuration and generated artifact SHA-256 values; its original `local/publication-validation` log paths identify byte-identical copies now retained here.

- Build log SHA-256: `44fbc9dbeb6c003b3541bce7b8fad346518318fd99f317ac416ff3a4b5ed1150`.
- Post-build test log SHA-256: `70a31907ea7c76d5e9229d797961874ef91db48902452820fad64af4f2def5f1`.
- Bridge DLL SHA-256: `a4d13e882438ba104d8a1dec079340b5e81f770e29dc9bc786cc90baa1e6542c`.
- NativeHost EXE SHA-256: `ee7e1d6bec365c92fc4c4399488b3a777a84de153682b7b0eaf86c45f7463111`.

These are incremental current-source build and HOST/FIXTURE results, not a clean-room build or new native acceptance. Generated executables remain ignored.

## Environment and next step

Windows 11 Pro 10.0.26200 64-bit, AMD Ryzen 7 9700X, PowerShell 7.6.5, pinned MSVC 14.44.35207 and Windows SDK 10.0.26100.0, .NET SDK 8.0.418 and framework 8.0.24. See `environment.json`.

SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Loader commit: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Configured GOG GA 3.1.0.29 candidate executable SHA-256: `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; read from pinned configuration, not rehashed or launched in this task.

Native tests: NOT RUN; the publication does not alter native implementation. The next gameplay step remains the bounded M07 complete shared encounter and death/respawn work described in STATUS.md. Future source changes can be committed and pushed normally through the configured origin.
