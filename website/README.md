# SporeMP — project website

Live website: https://raph559.github.io/sporemp-site/

When this directory is viewed inside the public SporeMP mod repository, it is a reviewed website source snapshot through M08. The live publication source is the separate [sporemp-site repository](https://github.com/raph559/sporemp-site); its design and news may be newer. The editing and deployment instructions below apply to that standalone repository.

A bilingual static website, with English as the default and French available through the EN / FR selector. It has no npm dependencies, visitor accounts or trackers. This repository contains the website and its original illustration, without game files or private development data.

## Publish news and development updates

Edit `content.json` for English content and `locales/fr/content.json` for its French translation, then commit both files to `main`. GitHub Actions builds the website and publishes it through GitHub Pages. Each article must use the same English slug in both languages. The build refuses to publish mismatched article sets.

- `news`: add an article with a unique lowercase, hyphenated English `slug`, an ISO `YYYY-MM-DD` date, a category, a title, a summary and paragraphs. Articles are sorted newest first.
- `releases`: add new versions at the beginning of the list, including their changes and limitations.
- `roadmap`: update future goals and their status. Do not present planned work as available functionality.
- `site.config.json`: stores the website URL and the shared `leetchiUrl`. Replace `null` with the exact HTTPS fundraiser URL to show the donation button in both languages. Until it is configured, the site displays a fundraiser-pending message.

The initial content reflects development progress as of September 15, 2026. The listed launcher versions are development builds. The site does not announce a public multiplayer download.

## Repository language and translations

English is the repository's working language: documentation, code comments, workflow labels, identifiers and new article paths must be written in English. French visitor-facing text belongs under `locales/fr/`.

- `content.json`: default English articles, release notes and roadmap.
- `i18n.mjs`: English interface copy and locale registration.
- `locales/fr/content.json`: French content translations.
- `locales/fr/ui.json`: French interface translations, including the native language name.
- `legacy-routes.json`: compatibility redirects for already-published article URLs. Historical French URL strings are retained only so existing links keep working.
- `legacy-anchors.json`: compatibility aliases for previously shared section links.

English pages are published at the root and French pages under `/fr/`. Both use English article paths under `news/`. Language links preserve article identity and section anchors.

## Local development

With Node.js 22 or later:

```sh
node build.mjs
node check.mjs
python -m http.server 4173 --directory dist
```

Open http://localhost:4173. `build.mjs` escapes content and generates HTML pages, metadata, redirects and a sitemap in `dist/`. Articles remain readable without JavaScript. `check.mjs` validates files, local references, language metadata and section anchors.

## Artwork

`assets/universe.png` is the original SporeMP launcher illustration, created with Image Gen on September 8, 2026. It is neither a gameplay screenshot nor an asset extracted from SPORE. SHA-256: `5397f78275521e8bb8f7f4344c79e58bf6d16cdb36b0a1f66a3c5de6e625b184`.

SporeMP is an independent community project, unaffiliated with Electronic Arts or Maxis. SPORE belongs to its respective rights holders.
