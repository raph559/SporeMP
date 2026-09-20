# Public presentation website — 2026-09-21

## Scope and result

User requested creation and public hosting of the SporeMP presentation, news, update history, future roadmap and an optional Leetchi donation link. During implementation the user required English by default with a language selector. The deployed site uses English at the root and complete French pages at `/fr/`; language links preserve the current article, and section anchors are preserved when JavaScript is enabled.

- Public URL: https://raph559.github.io/sporemp-site/
- Dedicated public source repository: https://github.com/raph559/sporemp-site
- Final website commit: `a2b2d945ca35d2abfa44cf5342b636da728df63a`
- Successful final GitHub Actions build/deploy: https://github.com/raph559/sporemp-site/actions/runs/35541956670
- Previous French-only initial publication: `1ead9cc6bf6c84a6f66dc5dade9a46e6bb83f7bc`, superseded by the bilingual version.
- GitHub Pages reports HTTPS enforced and workflow publication.

The Leetchi URL was requested asynchronously and has not been supplied. The site contains an honest fundraiser-pending message, no fabricated link, disabled button, donation collection or substituted recipient. Next smallest step: set the user's exact fundraiser URL in `website/content.json`, rebuild, verify the external destination and republish.

## Preservation and content evidence

Read `AGENTS.md`, `GOAL.md`, `STATUS.md`, `MILESTONES.md`, `CHANGELOG.md`, `docs/testing.md` and launcher release notes. Public wording separates M04/M05 scenario tests from M06 actual network movement and future M07/full-campaign scope. A content review corrected initial M05 wording that could imply a demonstrated network combat session; final copy explicitly describes a recorded DNA result applied to a test instance. There are no public game downloads or promises of campaign completion.

The existing original AI-generated launcher artwork is reused. Its provenance is `src/launcher/Assets/README.md` and `docs/sources.md`; SHA-256 `5397f78275521e8bb8f7f4344c79e58bf6d16cdb36b0a1f66a3c5de6e625b184`. It is labeled artwork, not gameplay. No EA assets, game binaries, private game sources, saves, invitations, credentials or raw native logs were uploaded. The public repository receives only an explicit website-file allowlist. All pre-existing game work remains uncommitted and preserved; only a website status section was added to the already-modified STATUS.md. M00–M20 statuses and launcher version remain unchanged.

## Environment and identity

- Local game repository HEAD: `d106404da0b7c4531f63f98d0df2377bb897a0f7`, with substantial pre-existing modifications/untracked files.
- Windows 11 Pro 64-bit, `10.0.26200`; AMD Ryzen 7 9700X 8-Core Processor.
- Node.js `v22.16.0`; no npm dependencies. GitHub Actions uses Node 22 and commit-pinned official actions.
- Native SDK/build not changed or executed. Existing STATUS records SDK `cbf9206b9a823f0911cd9be0217104a49d72380b` and pinned GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. These are historical native qualification identifiers, not new website acceptance tests.

## Commands and observed outcomes

| Command / check | Exit / outcome | Expected versus observed |
|---|---|---|
| `gh auth status` | command unavailable | CLI absent; existing Git credential manager and GitHub API used instead, without printing or persisting the token. |
| `node <Sites plugin>/scripts/configure-execution-profile.mjs` | 1 | Bundled script unavailable at the provided path. Continued with the user-selected GitHub Pages host and dependency-free static site; no registered Sites deployment was created. |
| `node website/build.mjs` | 0 | Final build produced two language homepages, six translated articles, localized error pages and shared assets. |
| `node website/check.mjs` | 0 | 15 files and 138 local references validated; no missing local file or fragment, one h1 and correct language/default-language metadata per page. |
| `node --check website/site.js` | 0 | JavaScript syntax valid. |
| `node --check website/i18n.mjs` | 0 | Translation module syntax valid. |
| `python -m http.server 4173 --bind 127.0.0.1 --directory website/dist` | running for preview, then stopped | Static site served for browser inspection; no game process was launched. |
| `python local/website-publish.py prepare` | 0 | Verified authenticated account `raph559`, created dedicated public site repository and enabled Pages workflow hosting. |
| `python local/website-publish.py push` | 0 | Published only the reviewed website allowlist; final commit shown above. |
| `python local/website-publish.py status` | 0 | Final exact-commit workflow completed successfully; Pages HTTPS enabled. |
| `python evidence/2026-09-21-website/verify-public.py` | 0 | All 14 served pages/assets returned HTTP 200 and matched local SHA-256 hashes. |

The publish helper is under ignored `local/`. Existing GitHub credentials are obtained only for GitHub authentication and are not printed, logged or added to Git. No new account, subscription or custom domain was purchased.

## Browser verification (WEB, not native game evidence)

- Inspected desktop 1440×1000 and mobile 390×844 screenshots. Checked 320px layout for horizontal overflow; none observed.
- Inspected loaded original hero artwork and English/French content.
- Opened the version 0.1.8 disclosure and confirmed its details became visible.
- Used real link navigation to an English article, switched to its French counterpart, and back to English. Article slug and identity were retained; heading and document language changed correctly.
- Article console error/warning inspection returned no entries.
- Replaced the local preview tab with the live HTTPS English homepage and retained that tab as the user-facing deliverable. Temporary viewport override reset; temporary repository-creation tab closed.
- `public-verification.json` contains the exact HTTP/file hash checks; `source-hashes.json` contains website source hashes. Browser screenshots and accessibility observations are retained in the task tool history.

Native tests: NOT RUN because no native code or launcher behavior changed. All website verification is static build, browser interaction and HTTP publication evidence.
