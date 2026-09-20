# Leetchi fundraiser publication — 2026-09-21

The user supplied the Spore MP fundraiser and requested its integration through the existing website task. Configured the canonical destination `https://www.leetchi.com/fr/c/spore-mp-1526537` in `website/site.config.json`; optional sharing query parameters were removed after verifying the clean URL opens the expected Spore MP campaign in the browser. No payment or contribution was initiated.

The existing support section now renders **Donate on Leetchi** in English and **Faire un don sur Leetchi** in French. Both link to the same campaign in a new tab with `noopener noreferrer`. The pending message is replaced by the button and voluntary-donation note. Independent source review found no issue.

## Commands and results

- `node website/build.mjs` — exit 0; donation reported configured.
- `node website/check.mjs` — exit 0; 21 output files and 222 local references validated.
- `Select-String -Path website/dist/index.html,website/dist/fr/index.html -Pattern 'href="https://www.leetchi.com[^"]+"[^>]*>[^<]+' -AllMatches | ForEach-Object { $_.Matches.Value }` — exit 0; confirmed both localized labels, exact destination, new-tab target and relationship attributes.
- `python local/website-publish.py push "Add the Spore MP Leetchi fundraiser"` — exit 0; only `site.config.json` changed in the public repository. Published commit `cec83707a720c49e54afb1dc685ef552e62a07b5`.

Repository: https://github.com/raph559/sporemp-site

Site: https://raph559.github.io/sporemp-site/

Environment and historical SDK/executable identities are unchanged from `SESSION.md`. No native files, launcher versions, milestones or personal saves changed. Native tests: NOT RUN; this is a website configuration update. Original evidence reports remain preserved.

## Live acceptance

- `python local/website-publish.py status` — exit 0; workflow https://github.com/raph559/sporemp-site/actions/runs/35542570817 completed successfully for exact commit `cec83707a720c49e54afb1dc685ef552e62a07b5`.
- `python evidence/2026-09-21-website/verify-public.py public-verification-leetchi.json` — exit 0; all 20 public files returned HTTP 200 and matched local SHA-256 hashes. Both homepages contain the configured localized donation link.
- Reloaded the live English homepage in the browser and inspected the **Donate on Leetchi** link: its destination is the canonical user-supplied campaign. Live site tab retained.
- `Get-FileHash website/site.config.json -Algorithm SHA256` — exit 0; changed source hash `921a1e52d1df2225d81ad213c61e6acf553560feb6620cfd1d83cfcfafda20d5`. Other public source files match the preceding English-repository increment.

Fundraiser integration is complete; the earlier pending-URL blocker is resolved.
