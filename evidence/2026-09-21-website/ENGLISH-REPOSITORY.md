# English website repository — 2026-09-21

## Scope

The user requested English throughout the public website repository while retaining the previously requested visitor language selector. This increment translates the README, GitHub repository description and workflow labels; makes `content.json` the English source; isolates French UI/content in `locales/fr/`; and uses English article routes and section identifiers. The shared URL and optional donation destination now live in `site.config.json`.

Historical article URLs remain as six generated redirect pages, with canonical metadata and fallback links targeting the corresponding new article and language. Twelve homepage fragment aliases preserve six previously shared section links in both languages. Historical commit/workflow records are retained. No native gameplay files, milestones, launcher version or pre-existing game changes are modified by this increment.

Public repository: https://github.com/raph559/sporemp-site

Website: https://raph559.github.io/sporemp-site/

Published source commit: `100e0a4a5d9673afb86f949f51f63e0cac0804d3`.

Workflow: https://github.com/raph559/sporemp-site/actions/runs/35542446925

## Verification and commands

| Command / check | Exit / result | Expected versus observed |
|---|---|---|
| `node website/build.mjs` | 0 | English root, French translation, six articles, six compatibility redirects, localized error pages and shared assets generated. |
| `node website/check.mjs` | 0 | 21 output files and 222 local references passed file, fragment, h1 and language metadata validation. |
| `node --check website/i18n.mjs` | 0 | Locale registration syntax valid on Node 22. |
| `node --check website/site.js` | 0 | Browser enhancement syntax valid. |
| Read-only independent source audit | passed | Default source/docs/workflow English; French confined to translations and compatibility mappings. Locale UI keys, article slugs, release versions and roadmap counts match. |
| PowerShell here-string piped to `node --input-type=module` | 0 | Asserted all six redirects' meta-refresh destinations and canonical URLs, all twelve old homepage fragment aliases, and new articles' translated/x-default URLs. |
| `python local/website-publish.py metadata` | 0 | GitHub returned the English description: `SporeMP project website: news, development updates and roadmap.` |
| `python local/website-publish.py push "Use English throughout the website repository"` | 0 | Explicit website allowlist copied into the standalone repository, obsolete `content.en.json` removed through Git, commit created and pushed to `main`. |

Environment is unchanged from `SESSION.md`: Windows 11 Pro 10.0.26200, AMD Ryzen 7 9700X and Node.js 22.16.0. Game repository HEAD remains `d106404da0b7c4531f63f98d0df2377bb897a0f7`. Native SDK/executable identifiers remain the historical values recorded there. Native tests: NOT RUN because this increment only changes the website.

`source-hashes-english-repo.json` records SHA-256 and byte length for all 16 website source files. Original `source-hashes.json` and `public-verification.json` are preserved. The HTTP verifier accepts an optional report filename so later verifications do not overwrite earlier evidence.

The pending Leetchi destination is now configured in `website/site.config.json`, superseding the original location mentioned in `SESSION.md`. Next content step remains obtaining the user's actual fundraiser URL; no destination was fabricated.

## Live publication acceptance

- `python local/website-publish.py status` exited 0 and reported workflow `35542446925` completed successfully for exact commit `100e0a4a5d9673afb86f949f51f63e0cac0804d3`; Pages HTTPS is enforced.
- `python evidence/2026-09-21-website/verify-public.py public-verification-english-repo.json` exited 0: all 20 served files returned HTTP 200 and matched the local SHA-256 hashes, including the six compatibility redirects. The twenty-first build file is the unserved `.nojekyll` marker.
- In the browser, opening the historical `/actualites/premiere-scene-partagee.html` address navigated to `/news/first-shared-scene.html` with the expected English heading. Clicking the French selector opened `/fr/news/first-shared-scene.html` with the translated heading.
- The English homepage was returned to the user. Its first navigation used a cached earlier document; a normal reload displayed the current English section anchors and new `news/` links. The final live tab was retained as the deliverable.

This increment is complete. Evidence is static/build/browser/HTTP only.
