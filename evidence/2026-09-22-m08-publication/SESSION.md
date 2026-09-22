# M07/M08 reviewed source and website publication

Date: **2026-09-22**. Classes: **BUILD, HOST/FIXTURE, PUBLICATION**.

The reviewed public source now includes the completed, bounded M07 Creature
encounter and M08 native content/editor foundation. README, STATUS, the full
M00–M20 plan, acceptance protocols and English/French website content agree:
**M08 VERIFIED for its recorded fixture; M09 next; the campaign is unfinished.**
The website has five dated articles, launcher 0.1.10 notes and the current roadmap.
The homepage progress link opens the latest article in the selected language.

## Export and qualification boundary

Changes were reapplied to public base
`13428ab9b6a1a63134a9bb66367d6874baa1e940`. No private Git history was merged,
pushed or rewritten. The existing GPL-3.0-or-later license, MIT Detours 4 SDK
migration, artifact provenance guard, SDK wrapper test, curated historical links
and evidence policy are preserved. Private logs, captures, saves, account records,
credentials and extracted assets stay outside public Git.

Native M07/M08 results refer to the frozen development payloads named in their
reports. **Original-game execution with this separately rebuilt public SDK is
NOT RUN.** Its successful build and HOST tests do not qualify that SDK for native
gameplay. The current source publication is not a public multiplayer release.
Historical hashes continue to identify the private original run; they do not
assert byte identity with later reports or a rebuilt public artifact.

## Validation

Commands run from the reviewed public checkout:

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure -V
python -X utf8 tools/check-public-evidence.py
git diff --cached --check
```

All exit0. **15/15 CTest targets pass in 35.40 seconds**, including 268 Python
tests, 54 launcher assertions, 190 content-registry checks, 937 content transport/
real Windows Schannel TLS checks and 2,963 existing network checks. The additional
SDK-wrapper target explains the difference from the native development build's
14-target suite. No build or automated test launches SPORE.

`node website/build.mjs` and `node website/check.mjs` exit0: **25 generated files
and 290 local references** pass. Current English homepage content and both new
M08 article translations were inspected in the actual browser. The progress link
was exercised and the EN/FR links preserve article identity. Website commit
`25cc33456282ea398b8e8cc0853b12bd80680cc2` deployed successfully in
[workflow 35740373625](https://github.com/raph559/sporemp-site/actions/runs/35740373625).
The live homepage and both M08 translations were then verified in the browser.

The staged-content audit has zero known private identifiers, credential/account
patterns or missing Markdown link targets. The public evidence guard passes after
staging the exact report allowlist. The [verification record](verification.json)
contains the public build identities, private log hashes and website source
commit. Raw build/test logs remain under ignored `local/`.

## Next step

M09 must make native checkpoints and creation versions durable, recover interrupted
transactions and verify worker/coordinator restart with real clients. All original
M00–M20 clauses remain intact. The public SDK native gate remains explicitly open.
