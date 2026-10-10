# Public presentation website

The website was rebuilt from scratch on 2026-09-27. The new visual system, static templates, bilingual copy, interaction code and two original illustrations replace the previous site. It remains a presentation and development journal for the original SPORE multiplayer project.

- Live English: https://raph559.github.io/sporemp-site/
- Live French: https://raph559.github.io/sporemp-site/fr/
- Separate public repository: https://github.com/raph559/sporemp-site
- Source in this development checkout: `website/`

## Visitor experience

A spacious illustrated opening leads to the project idea, an interactive five-stage explorer, development journal, four-phase roadmap, selected launcher notes, questions, and voluntary support. Journal archives and individual articles have their own readable layouts. All visible copy is authored in English and French. Switching languages retains the current article or selected section/stage.

The site states that SporeMP is experimental. M00–M10 are qualified only within their recorded foundations and Creature scenarios; M11 is in progress, and the complete five-stage campaign remains unfinished. There is no invented download, release date, player count, community invitation, or completed-campaign claim. The source link points to the reviewed public mod repository, which has its own publication boundary.

`site.config.json` retains the user-provided donation destination: https://www.leetchi.com/fr/c/spore-mp-1526537. A contribution does not buy access or promise a release date.

## Implementation

`src/locales/en.mjs` and `src/locales/fr.mjs` contain the new content. `src/render.mjs`, `design.css`, and `interactions.js` implement the site. There are no npm dependencies or remote runtime requests. The build fetches Outfit and its SIL OFL license from a pinned official source revision, verifies both SHA-256 values, and includes them only in generated output. New AI-assisted concept illustrations have provenance and their exact prompts in `assets/credits.txt`; they are labelled as illustrations rather than gameplay captures.

The build generates English/French home pages, archives, nine articles per language, two not-found pages, and six historical redirects. It preserves established article URLs and translates old section fragments. CSS, JavaScript, images and fonts use content-hashed filenames to prevent stale deployment assets. The sitemap contains the 22 indexable pages.

```powershell
node website/build.mjs
node website/check.mjs
```

The static checker verifies all 30 HTML documents and their local links, anchors, language pairs and assets. Browser verification covers desktop, tablet and mobile layouts from 320 to 1440 pixels, 200% text size, stage selection and keyboard navigation, menu/Escape, language continuity, article reading, expandable notes/questions, historical links, reduced motion, and readable content without JavaScript. These are website checks, not native SPORE evidence.

## Publishing boundary

The ignored `local/website-publish.py` helper copies an explicit allowlist into the existing standalone `local/sporemp-site-public` checkout and removes the named obsolete website files. It uses the existing Git credential manager without logging secrets. The website repository's pinned GitHub Actions workflow builds, checks and deploys only `dist/`.

The parent development repository has private history and unrelated work. Never push that history to either public repository. The redesign does not modify game code, launcher version, native acceptance or the M00–M20 plan. Original website files were retained under ignored `local/site-redesign-2026-09-27/original-website/` before replacement.

Session commands, actual results, source hashes and publication verification are recorded in `evidence/2026-09-27-website-rebuild/SESSION.md`; raw browser material remains local.

## M11 social progress publication — 2026-09-27

The ninth English/French article, `social-rewards-and-saved-progress`, describes
the bounded native social result, owner-only First DNA guidance, delayed
objective correction and original save/load recovery. It retains the shared
species, exclusive-action and developer-control limits; M11 remains unfinished.
The selected launcher notes now include development version 0.1.14.

Site commit `bbf37a1e770b4a27819bef8b1465c79feadfdcac` deployed successfully in
[run 36325967670](https://github.com/raph559/sporemp-site/actions/runs/36325967670).
Build/check exit 0: 22 indexable pages, 30 HTML documents and 632 local links/assets.
All 41 generated live files match local bytes. Eighteen local and eighteen live
browser checks pass for home/article routes, language continuity and 320, 390
and 1440 pixel layouts; live English desktop and French mobile article captures
were inspected. Private outputs are under `local/m11-resume-2026-09-27/`.
