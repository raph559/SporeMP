# Public presentation website

The public site is maintained in `website/` and deployed independently from the game repository:

- URL: https://raph559.github.io/sporemp-site/
- Public website repository: https://github.com/raph559/sporemp-site
- English is the default at the root. French is under `/fr/`.
- EN / FR links preserve the current article and, with JavaScript enabled, the current section.

The site is a static development journal and project presentation, not a multiplayer release. The initial articles reflect verified, bounded M04–M06 evidence; M07 and the full campaign remain future work. No game source, binaries, saves, invitations, logs or private evidence are uploaded to the website repository. The existing original AI-generated launcher illustration is reused with its provenance disclosed.

## Content and publication

`website/content.json` contains the default English articles, selected launcher updates and public roadmap. French translations are isolated in `website/locales/fr/content.json` and `website/locales/fr/ui.json`. `website/i18n.mjs` defines English interface copy and registers the French locale. Repository documentation, workflow labels and new paths/identifiers use English. Article slugs stay aligned across languages. Content is manually reviewed against the current milestone evidence; new milestone completion does not automatically publish a news article. Legacy article URLs and section fragments remain supported through explicit compatibility mappings.

Run `node website/build.mjs`, then `node website/check.mjs`. The generated static output is `website/dist/`. GitHub Actions performs these steps on the public repository's `main` branch and publishes the result through GitHub Pages. Workflow actions are pinned to commits. No npm installation or backend service is needed.

`leetchiUrl` in `website/site.config.json` is the sole donation destination for both languages. It is configured with the user's Spore MP fundraiser: https://www.leetchi.com/fr/c/spore-mp-1526537. The clean campaign URL was verified in the browser; optional sharing/tracking parameters are omitted. Setting this field to null restores the fundraiser-pending message.

The local authenticated deployment helper and standalone website checkout are under ignored `local/`. The helper uses the existing GitHub credential manager without saving or printing tokens. It copies only an explicit list of website source files into the separate public repository.

Website changes do not change launcher version 0.1.9 or the M00–M20 acceptance plan. Website verification is browser/static/HTTP evidence only; no native SPORE tests are implied.
