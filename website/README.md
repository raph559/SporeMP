# SporeMP website

A completely new, bilingual presentation and development journal, built as a small static site. The public destination is https://raph559.github.io/sporemp-site/.

## Build

Node.js 22 or later; no package installation or runtime service required. The build fetches Outfit and its license from a pinned official revision and verifies both SHA-256 values. Network access is required at build time; the generated website serves its own font files.

```powershell
node website/build.mjs
node website/check.mjs
```

From this directory, use `node build.mjs` and `node check.mjs`. Output goes to `dist/`. The separate website repository builds and publishes that folder through GitHub Pages.

## Edit

- `src/locales/en.mjs` and `src/locales/fr.mjs`: independently authored visitor copy, journal entries and selected development launcher notes. Keep article slugs aligned.
- `src/render.mjs`: new static HTML templates and route-aware navigation.
- `design.css`: new responsive visual system. Body text is 16px or larger; smaller type is reserved for secondary labels.
- `interactions.js`: mobile navigation, keyboard-accessible stage tabs, section indicators, and language/bookmark continuity.
- `assets/`: the two original illustrations, favicon, and provenance. The pinned Outfit font and license are fetched by the build and included only in generated output.
- `site.config.json`: canonical URL and the user-supplied Leetchi fundraiser.

English is at `/`; French is at `/fr/`. Journal archives are `/news/` and `/fr/news/`. Existing article URLs continue to work with freshly written articles; three historic French routes redirect to their counterparts. Existing section bookmarks are translated by the new script. The language switch preserves the current article or section, including the selected stage.

CSS, JavaScript, image and font filenames include their content hashes. No old design, template, stylesheet, script or illustration is used by the new site. The artwork is illustrative and explicitly labelled as not gameplay. No private capture or game asset is distributed.

## Verification

`check.mjs` checks all generated local links, fragments, language routes, landmarks, headings, and asset/font paths. Inspect English/French home pages, archive and article pages in a browser on desktop and mobile. Check menu and Escape, stage selection and keyboard navigation, language continuity, disclosures, reduced motion, narrow screens, and enlarged text. A build does not establish visual quality or native gameplay acceptance.

Only explicitly reviewed website files may be copied into the separate `raph559/sporemp-site` repository. Never push the parent development history there.
